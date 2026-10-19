#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"

namespace
{
constexpr wgpu::BindGroupLayoutEntry InputBindGroupLayoutEntries[]//
{
    // Texture
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 0,
        .visibility = wgpu::ShaderStage::Fragment,
        .texture =
        {
            .sampleType = wgpu::TextureSampleType::Float,
            .viewDimension = wgpu::TextureViewDimension::e2D,
            .multisampled = false,
        },
    },
    // Sampler
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 1,
        .visibility = wgpu::ShaderStage::Fragment,
        .sampler =
        {
            .type = wgpu::SamplerBindingType::Filtering,
        },
    },
};

auto
CreateInputBindGroupEntries(const GpuCompositorPass::Inputs& inputs, const wgpu::Sampler& sampler)
{
    const std::array entries //
        {
            wgpu::BindGroupEntry //
            {
                .binding = 0,
                .textureView = inputs.Texture.CreateView(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 1,
                .sampler = sampler,
            },
        };

    static_assert(std::size(entries) == std::size(InputBindGroupLayoutEntries),
        "Bind group layout entries and bind group entries must have the same size");

    return entries;
}

Result<wgpu::Sampler>
CreateSampler(const GpuHelper& gpuHelper)
{
    static constexpr wgpu::SamplerDescriptor samplerDesc //
        {
            .label = "GpuCompositorPass",
            .addressModeU = wgpu::AddressMode::ClampToEdge,
            .addressModeV = wgpu::AddressMode::ClampToEdge,
            .addressModeW = wgpu::AddressMode::Undefined,
            .magFilter = wgpu::FilterMode::Nearest,
            .minFilter = wgpu::FilterMode::Nearest,
            .mipmapFilter = wgpu::MipmapFilterMode::Undefined,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .compare = wgpu::CompareFunction::Undefined,
            .maxAnisotropy = 1,
        };

    const wgpu::Sampler sampler = gpuHelper.GetDevice().CreateSampler(&samplerDesc);
    MLG_CHECK(sampler, "Failed to create sampler");

    return sampler;
}

Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const GpuHelper& gpuHelper)
{
    static constexpr wgpu::BindGroupLayoutDescriptor desc //
        {
            .label = "GpuCompositorPass",
            .entryCount = std::size(InputBindGroupLayoutEntries),
            .entries = &InputBindGroupLayoutEntries[0],
        };

    const wgpu::BindGroupLayout bindGroupLayout =
        gpuHelper.GetDevice().CreateBindGroupLayout(&desc);
    MLG_CHECK(bindGroupLayout, "Failed to create bind group layout");

    return bindGroupLayout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const GpuHelper& gpuHelper, const wgpu::BindGroupLayout& bindGroupLayout)
{
    MLG_CHECKV(bindGroupLayout, "Bind group layout is not valid");

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuCompositorPass",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };

    const wgpu::PipelineLayout pipelineLayout =
        gpuHelper.GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}
} // namespace

/// GpuCompositorPass::CreateTask

GpuCompositorPass::CreateTask::CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_GpuHelper(&gpuHelper),
      m_ShaderFetcher(gpuHelper, fileFetcher)
{
}

Result<std::unique_ptr<GpuCompositorPass>>
GpuCompositorPass::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;
    
    auto shader = m_ShaderFetcher.Take();
    MLG_CHECK(shader, "Failed to fetch shader: {}", ShaderPath);

    auto sampler = CreateSampler(*m_GpuHelper);
    MLG_CHECK(sampler);

    auto bindGroupLayout = CreateBindGroupLayout(*m_GpuHelper);
    MLG_CHECK(bindGroupLayout);

    auto pipelineLayout = CreatePipelineLayout(*m_GpuHelper, *bindGroupLayout);
    MLG_CHECK(pipelineLayout);

    return std::unique_ptr<GpuCompositorPass>(
        new GpuCompositorPass(*m_GpuHelper, *shader, *sampler, *bindGroupLayout, *pipelineLayout));
}

// private:

Result<>
GpuCompositorPass::CreateTask::OnStart()
{
    MLG_DEBUG("Creating compositor pass...");

    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    m_Stage = Stage::Failed;

    auto shaderPath = DirectoryPath::GetCurrentDirectory().Join(ShaderPath);
    MLG_CHECKV(shaderPath,
        "Failed to construct shader path: {}/{}",
        DirectoryPath::GetCurrentDirectory(),
        ShaderPath);

    MLG_CHECK(m_ShaderFetcher.Start(*shaderPath));

    m_Stage = Stage::FetchingShader;

    return Result<>::Ok;
}

void
GpuCompositorPass::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::FetchingShader:
            if(m_ShaderFetcher.IsRunning())
            {
                m_ShaderFetcher.Update();
            }
            else
            {
                MLG_DEBUG("Created compositor pass");
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("Failed to create compositor pass");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

/// GpuCompositorPass

Result<>
GpuCompositorPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(inputs != m_Inputs)
    {
        if(!m_Inputs || inputs.Texture != m_Inputs->Texture)
        {
            // Rebuild the bind group
            m_InputsBindGroup = {};
        }

        m_Inputs = inputs;
    }

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    if(outputs != m_Outputs)
    {
        if(!m_Outputs || outputs.RenderTarget->GetFormat() != m_Outputs->RenderTarget->GetFormat())
        {
            // Rebuild the pipeline
            m_Pipeline = {};
        }

        m_Outputs = outputs;
    }

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::Execute()
{
    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();
    
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(Execute(cmdEncoder));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::Execute(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuCompositorPass.Execute")

    auto renderPassResult = CreateRenderPassEncoder(cmdEncoder);
    MLG_CHECK(renderPassResult, "Failed to create render pass encoder");

    const wgpu::RenderPassEncoder renderPass = std::move(*renderPassResult);

    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    return Result<>::Ok;
}

// private:

Result<>
GpuCompositorPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    static constexpr wgpu::BlendState blendState //
    {
        .color =
        {
            .operation = wgpu::BlendOperation::Add,
            .srcFactor = wgpu::BlendFactor::One,
            .dstFactor = wgpu::BlendFactor::Zero,
        },
        .alpha =
        {
            .operation = wgpu::BlendOperation::Add,
            .srcFactor = wgpu::BlendFactor::One,
            .dstFactor = wgpu::BlendFactor::Zero,
        },
    };

    const wgpu::TextureFormat targetFormat = m_Outputs->RenderTarget->GetFormat();

    const wgpu::ColorTargetState colorTargetState //
        {
            .format = targetFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const char* fragmentEntry = nullptr;

    if(wgpu::TextureFormat::RGBA8UnormSrgb == targetFormat
        || wgpu::TextureFormat::BGRA8UnormSrgb == targetFormat)
    {
        fragmentEntry = FragmentEntry_srgb;
    }
    else
    {
        fragmentEntry = FragmentEntry_non_srgb;
    }

    const wgpu::FragmentState fragmentState //
        {
            .module = m_Shader,
            .entryPoint = fragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::RenderPipelineDescriptor desc//
    {
        .label = "GpuCompositorPass",
        .layout = m_PipelineLayout,
        .vertex =
        {
            .module = m_Shader,
            .entryPoint = VertexEntry,
            .bufferCount = 0,
            .buffers = nullptr,
        },
        .primitive =
        {
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CW,
            .cullMode = wgpu::CullMode::Back,
            .unclippedDepth = false,
        },
        .depthStencil = nullptr, // No depth/stencil for this pipeline
        .multisample =
        {
            .count = 1,
            .mask = 0xFFFFFFFF,
            .alphaToCoverageEnabled = false,
        },
        .fragment = &fragmentState,
    };

    m_Pipeline = m_GpuHelper->GetDevice().CreateRenderPipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::EnsureInputsBindGroup()
{
    if(m_InputsBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");

    auto entries = CreateInputBindGroupEntries(m_Inputs.value(), m_Sampler);

    const wgpu::BindGroupDescriptor desc //
        {
            .label = "GpuCompositorPass::Inputs",
            .layout = m_BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    m_InputsBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECK(m_InputsBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputsBindGroup());

    MLG_CHECK(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECK(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    MLG_CHECKV(m_Outputs->RenderTarget.Get() != m_Inputs->Texture,
        "Output texture must be different from input texture");

    const Rect targetRect({ .X = 0,
        .Y = 0,
        .Width = m_Outputs->RenderTarget->GetWidth(),
        .Height = m_Outputs->RenderTarget->GetHeight() });

    Rect dstRect = m_Inputs->DstRect;

    if(!targetRect.Contains(m_Inputs->DstRect))
    {
        MLG_CHECKV(targetRect.Intersects(m_Inputs->DstRect), "DstRect is outside of target rect");

        dstRect = targetRect.Intersect(m_Inputs->DstRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs->RenderTarget->CreateView(),
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "GpuCompositorPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
        };

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    renderPass.SetPipeline(m_Pipeline);
    renderPass.SetBindGroup(0, m_InputsBindGroup, 0, nullptr);

    {
        const float x = static_cast<float>(dstRect.GetX());
        const float y = static_cast<float>(dstRect.GetY());
        const float width = static_cast<float>(dstRect.GetWidth());
        const float height = static_cast<float>(dstRect.GetHeight());

        renderPass.SetViewport(x, y, width, height, 0, 1);
    }
    {
        const uint32_t x = static_cast<uint32_t>(dstRect.GetX());
        const uint32_t y = static_cast<uint32_t>(dstRect.GetY());
        const uint32_t width = static_cast<uint32_t>(dstRect.GetWidth());
        const uint32_t height = static_cast<uint32_t>(dstRect.GetHeight());

        renderPass.SetScissorRect(x, y, width, height);
    }

    return renderPass;
}