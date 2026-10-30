#define MLG_LOGGER_NAME "TEXF"

#include "TextureFetcher.h"

#include "GpuHelper.h"
#include "ResourceBundle.h"
#include "System.h"

#include <ranges>

TextureFetcher::TextureFetcher(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const DirectoryPath& parentPath,
    const ResourceBundle& resourceBundle)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_ParentPath(parentPath),
      m_ResourceBundle(&resourceBundle)
{
}

Result<std::vector<wgpu::Texture>>
TextureFetcher::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    auto bye = std::move(m_Textures);
    return bye;
}

// private:

Result<>
TextureFetcher::OnStart()
{
    MLG_CHECKV(m_Stage == Stage::None, "Task is already in progress");

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    const std::span<const TextureResource> texRsrcs = m_ResourceBundle->GetResources<TextureResource>();
    if(texRsrcs.empty())
    {
        MLG_DEBUG("No texture resources found");
        m_Stage = Stage::Succeeded;
        return Result<>::Ok;
    }

    m_CommandEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECK(m_CommandEncoder, "Failed to create command encoder");

    // Initialize the textures vector with default textures for each path.
    // If a texture fails to load then we'll get the default texture.
    m_Textures.resize(texRsrcs.size(), m_GpuHelper->GetDefaultTexture());

    m_Loaders.reserve(texRsrcs.size());

    std::vector<ICoopTask*> taskBatch;
    taskBatch.reserve(texRsrcs.size());

    for(const TextureResource& texRsrc : texRsrcs)
    {
        auto loaderResult = TextureLoader::LoadFromResource(*m_GpuHelper,
            *m_FileFetcher,
            *m_ThreadPool,
            m_ParentPath,
            *m_ResourceBundle,
            texRsrc,
            m_CommandEncoder);

        if(loaderResult)
        {
            m_Loaders.emplace_back(std::move(*loaderResult));
            taskBatch.push_back(m_Loaders.back().get());
        }
        else
        {
            // If the load fails don't bail, keep loading the remaining textures.

            std::string_view sv = m_ResourceBundle->GetStringView(texRsrc.TexturePath);
            sv = sv.empty() ? "<unknown>" : sv;
            MLG_ERROR("Failed to create texture loader for resource: {}", sv);

            // Add a null loader to maintain the index alignment with m_Textures.
            m_Loaders.emplace_back(nullptr);
        }

    }

    if(!taskBatch.empty())
    {
        m_TaskBatch.emplace(std::move(taskBatch));

        MLG_CHECK(m_TaskBatch->Start());

        m_Stage = Stage::Fetching;
    }
    else
    {
        // No valid texture loaders were created.
        // The caller will get a list of default textures.
        m_Stage = Stage::Succeeded;
    }

    return Result<>::Ok;
}

void
TextureFetcher::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Fetching:
            if(m_TaskBatch->IsRunning())
            {
                m_TaskBatch->Update();
            }
            else
            {
                const wgpu::CommandBuffer commandBuffer = m_CommandEncoder.Finish();
                m_GpuHelper->GetDevice().GetQueue().Submit(1, &commandBuffer);

                for(auto [loader, texture] : std::views::zip(m_Loaders, m_Textures))
                {
                    if(!loader)
                    {
                        // Null loaders correspond to failed texture loads.
                        continue;
                    }

                    auto result = loader->Take();
                    if(result)
                    {
                        texture = std::move(*result);
                    }
                }

                m_Stage = Stage::Succeeded;

                m_TaskBatch.reset();
            }
            break;

        case Stage::Failed:
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}