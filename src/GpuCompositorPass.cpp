#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

namespace
{
constexpr const char* ShaderPath = "shaders/CompositorShader.wgsl";
constexpr const char* VertexEntry = "vs_main";

/// Entry point for the fragment shader when outputting to a non-sRGB framebuffer.
constexpr const char* FragmentEntry_non_srgb = "fs_main_non_srgb";
/// Entry point for the fragment shader when outputting to an sRGB framebuffer.
constexpr const char* FragmentEntry_srgb = "fs_main_srgb";

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
    const auto entries = GpuCompositorPass::BindGroup0::LayoutEntries;

    const wgpu::BindGroupLayoutDescriptor desc //
        {
            .label = "GpuCompositorPass",
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    const wgpu::BindGroupLayout bindGroupLayout =
        gpuHelper.GetDevice().CreateBindGroupLayout(&desc);
    MLG_CHECK(bindGroupLayout, "Failed to create bind group layout");

    return bindGroupLayout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice, const wgpu::BindGroupLayout& bindGroupLayout)
{
    MLG_CHECKV(bindGroupLayout, "Bind group layout is not valid");

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuCompositorPass",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };

    const wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}

Result<wgpu::RenderPipeline>
CreatePipeline(const wgpu::Device& gpuDevice,
    const wgpu::PipelineLayout& pipelineLayout,
    const wgpu::ShaderModule& shader,
    const wgpu::TextureFormat targetFormat)
{
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
            .module = shader,
            .entryPoint = fragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::RenderPipelineDescriptor desc//
    {
        .label = "GpuCompositorPass",
        .layout = pipelineLayout,
        .vertex =
        {
            .module = shader,
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

    wgpu::RenderPipeline pipeline = gpuDevice.CreateRenderPipeline(&desc);
    MLG_CHECK(pipeline, "Failed to create pipeline");

    return pipeline;
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

    auto pipelineLayout = CreatePipelineLayout(m_GpuHelper->GetDevice(), *bindGroupLayout);
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

GpuCompositorPass::GpuCompositorPass(const GpuHelper& gpuHelper,
    wgpu::ShaderModule shader,
    wgpu::Sampler sampler,
    wgpu::BindGroupLayout bindGroupLayout,
    wgpu::PipelineLayout pipelineLayout)
    : m_GpuHelper(&gpuHelper),
      m_Shader(std::move(shader)),
      m_Sampler(std::move(sampler)),
      m_BindGroup0Layout(std::move(bindGroupLayout)),
      m_PipelineLayout(std::move(pipelineLayout))
{
    MLG_ASSERT(m_Shader, "Shader module is not valid");
    MLG_ASSERT(m_Sampler, "Sampler is not valid");
    MLG_ASSERT(m_BindGroup0Layout, "Bind group layout is not valid");
    MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
}

Result<>
GpuCompositorPass::SetSourceRenderTarget(const GpuRenderTarget& src)
{
    if(!m_SrcRenderTarget || src != *m_SrcRenderTarget)
    {
        m_SrcRenderTarget = src;
        // Rebuild the bind group
        m_BindGroup0 = {};
    }

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::Execute(const GpuRenderTarget& dst, const Rect& dstRect)
{
    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(Execute(dst, dstRect, cmdEncoder));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::Execute(
    const GpuRenderTarget& dst, const Rect& dstRect, const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuCompositorPass.Execute")

    auto renderPassResult = CreateRenderPassEncoder(dst, dstRect, cmdEncoder);
    MLG_CHECK(renderPassResult, "Failed to create render pass encoder");

    const wgpu::RenderPassEncoder renderPass = std::move(*renderPassResult);

    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    return Result<>::Ok;
}

// private:

Result<>
GpuCompositorPass::EnsurePipeline(const wgpu::TextureFormat targetFormat)
{
    if(m_TargetFormat != targetFormat && wgpu::TextureFormat::Undefined != m_TargetFormat)
    {
        MLG_WARN("Target format changed, recreating pipeline");
        m_Pipeline = {};
    }

    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    auto pipelineResult =
        CreatePipeline(m_GpuHelper->GetDevice(), m_PipelineLayout, m_Shader, targetFormat);
    MLG_CHECK(pipelineResult, "Failed to create pipeline");
    m_Pipeline = *pipelineResult;
    m_TargetFormat = targetFormat;

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::EnsureBindGroup0()
{
    if(m_BindGroup0)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(m_SrcRenderTarget,
        "Source render target is not valid - forget to call SetSourceRenderTarget()?");

    const auto bgResult = BindGroup0::Create(m_SrcRenderTarget->GetTexture(), m_Sampler);
    MLG_CHECK(bgResult, "Failed to create bind group 0");

    const auto entries = bgResult->GetBindGroupEntries();

    const wgpu::BindGroupDescriptor desc //
        {
            .label = "GpuCompositorPass::BindGroup0",
            .layout = m_BindGroup0Layout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    m_BindGroup0 = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECK(m_BindGroup0, "Failed to create bind group");

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::CreateRenderPassEncoder(
    const GpuRenderTarget& dst, const Rect& dstRect, const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline(dst->GetFormat()));
    MLG_CHECK(EnsureBindGroup0());

    MLG_CHECK(m_SrcRenderTarget,
        "Source render target not valid - forget to call SetSourceRenderTarget()?");

    MLG_CHECKV(dst != *m_SrcRenderTarget, "Output texture must be different from input texture");

    const Rect targetRect({ .X = 0, .Y = 0, .Width = dst->GetWidth(), .Height = dst->GetHeight() });

    Rect viewportRect = dstRect;

    if(!targetRect.Contains(viewportRect))
    {
        MLG_CHECKV(targetRect.Intersects(viewportRect), "dstRect is outside of target rect");

        viewportRect = targetRect.Intersect(viewportRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = dst->CreateView(),
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
    renderPass.SetBindGroup(0, m_BindGroup0, 0, nullptr);

    {
        const float x = static_cast<float>(viewportRect.GetX());
        const float y = static_cast<float>(viewportRect.GetY());
        const float width = static_cast<float>(viewportRect.GetWidth());
        const float height = static_cast<float>(viewportRect.GetHeight());

        renderPass.SetViewport(x, y, width, height, 0, 1);
    }
    {
        const uint32_t x = static_cast<uint32_t>(viewportRect.GetX());
        const uint32_t y = static_cast<uint32_t>(viewportRect.GetY());
        const uint32_t width = static_cast<uint32_t>(viewportRect.GetWidth());
        const uint32_t height = static_cast<uint32_t>(viewportRect.GetHeight());

        renderPass.SetScissorRect(x, y, width, height);
    }

    return renderPass;
}