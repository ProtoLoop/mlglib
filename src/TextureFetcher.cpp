#define MLG_LOGGER_NAME "TEXF"

#include "TextureFetcher.h"

#include "GpuHelper.h"
#include "System.h"

#include <ranges>

TextureFetcher::TextureFetcher(const GpuHelper& gpuHelper,
    FileFetcher& fileFetcher,
    ThreadPool& threadPool,
    const DirectoryPath& parentPath,
    std::vector<RelativeFilePath> texturePaths)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_ThreadPool(&threadPool),
      m_Tasks(texturePaths.size()),
      m_ParentPath(parentPath),
      m_TexturePaths(std::move(texturePaths))
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

    if(m_TexturePaths.empty())
    {
        MLG_DEBUG("No texture paths provided");
        m_Stage = Stage::Succeeded;
        return Result<>::Ok;
    }

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    m_CommandEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder();
    MLG_CHECK(m_CommandEncoder, "Failed to create command encoder");

    // Initialize the textures vector with default textures for each path.
    // If a texture fails to load then we'll get the default texture.
    m_Textures.resize(m_TexturePaths.size(), m_GpuHelper->GetDefaultTexture());

    std::vector<ICoopTask*> taskBatch;
    taskBatch.reserve(m_TexturePaths.size());

    for(RelativeFilePath texPath : m_TexturePaths)
    {
        if(m_GpuHelper->HasTextureFormat(TextureFormat::FormatIdentifier::BC7))
        {
            const auto bc7PathResult =
                texPath.ReplaceExtension(TextureFormat::GetBC7().Extension.AsStringView());
            MLG_CHECK(bc7PathResult,
                "Failed to replace extension for {}/{}",
                m_ParentPath,
                texPath);
            texPath = *bc7PathResult;
        }

        auto pathResult = m_ParentPath.Join(texPath);
        MLG_CHECK(pathResult, "Failed to construct full path for {}/{}", m_ParentPath, texPath);

        TextureFetchTask& task = m_Tasks.emplace_back(*m_GpuHelper,
            *m_FileFetcher,
            *m_ThreadPool,
            *pathResult,
            m_CommandEncoder);

        taskBatch.push_back(&task);
    }

    m_TaskBatch.emplace(std::move(taskBatch));

    MLG_CHECK(m_TaskBatch->Start());

    m_Stage = Stage::Fetching;

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

                for(auto [task, texture] : std::views::zip(m_Tasks, m_Textures))
                {
                    auto result = task.Take();
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