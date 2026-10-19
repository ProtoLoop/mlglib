#define MLG_LOGGER_NAME "SHDR"

#include "ShaderFetcher.h"

#include "FileFetcher.h"
#include "GpuHelper.h"

#include <webgpu/webgpu_cpp.h>

ShaderFetcher::ShaderFetcher(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher)
{
}

Result<wgpu::ShaderModule>
ShaderFetcher::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task has not succeeded");

    MLG_CHECKV(m_ShaderModule, "Shader module already consumed");

    wgpu::ShaderModule shaderModule = m_ShaderModule;
    m_ShaderModule = nullptr; // Invalidate the shader module so it can only be taken once

    return shaderModule;
}

// private

Result<>
ShaderFetcher::OnStart(const FilePath& path)
{
    MLG_LOG_SCOPE(path);

    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    MLG_DEBUG("Loading shader...");

    m_Stage = Stage::Failed;

    auto requestId = m_FileFetcher->Fetch(path);
    MLG_CHECK(requestId);

    m_RequestId = *requestId;

    m_Stage = Stage::Fetching;

    m_DiagPath = path.GetFilename();

    return Result<>::Ok;
}

void
ShaderFetcher::OnUpdate()
{
    MLG_LOG_SCOPE(m_DiagPath);

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Fetching:
            if(!m_FileFetcher->IsPending(m_RequestId))
            {
                auto shaderData = m_FileFetcher->Take(m_RequestId);

                if(shaderData && CreateShaderModule(*shaderData))
                {
                    MLG_DEBUG("Loaded shader");
                    m_Stage = Stage::Succeeded;
                }
                else
                {
                    m_Stage = Stage::Failed;
                }
            }
            break;
        case Stage::Failed:
            MLG_ERROR("Failed to load shader");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

Result<>
ShaderFetcher::CreateShaderModule(const std::span<const std::byte> shaderData)
{
    MLG_LOG_SCOPE(m_DiagPath);

    const void* dataPtr = shaderData.data();
    const wgpu::StringView shaderCode{ static_cast<const char*>(dataPtr), shaderData.size() };
    const wgpu::StringView label = std::string_view(m_DiagPath);
    const wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCode } };
    const wgpu::ShaderModuleDescriptor desc{ .nextInChain = &wgsl, .label = label };

    MLG_DEBUG("Creating shader module...");

    m_ShaderModule = m_GpuHelper->GetDevice().CreateShaderModule(&desc);
    MLG_CHECK(m_ShaderModule, "Failed to create shader module");

    return Result<>::Ok;
}