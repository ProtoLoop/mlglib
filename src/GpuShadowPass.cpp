#define MLG_LOGGER_NAME "SPAS"

#include "GpuShadowPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"
#include "SceneTypes.h"

#include <span>

namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PerfCounter pcTriangleCount({ .Name = "GpuShadowPass.Triangles" });

constexpr const char* ShaderPath = "shaders/ShadowShader.wgsl";
constexpr const char* ShaderVertexEntry = "vs_main";
constexpr float kClearDepth = 1.0f;

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice)
{
    auto bgLayoutResult = GpuShadowPass::BindGroup0Helper::CreateBindGroupLayout(gpuDevice,
        "GpuShadowPass::BindGroup0Layout");
    MLG_CHECK(bgLayoutResult, "Failed to create bind group 0 layout");

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        { *bgLayoutResult };

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuShadowPass",
            .bindGroupLayoutCount = std::size(bindGroupLayouts),
            .bindGroupLayouts = &bindGroupLayouts[0],
        };

    const wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create shadow pipeline layout");

    return pipelineLayout;
}

constexpr wgpu::VertexBufferLayout
GetVertexBufferLayout()
{
    static constexpr wgpu::VertexAttribute attributes[] = //
        {
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, pos),
                .shaderLocation = 0,
            },
        };

    static constexpr wgpu::VertexBufferLayout layout = //
        {
            .stepMode = wgpu::VertexStepMode::Vertex,
            .arrayStride = sizeof(Vertex),
            .attributeCount = std::size(attributes),
            .attributes = &attributes[0],
        };

    return layout;
}

Result<wgpu::RenderPipeline>
CreateRenderPipeline(const wgpu::Device& gpuDevice, const wgpu::ShaderModule& shader)
{
    auto pipelineLayout = CreatePipelineLayout(gpuDevice);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");

    static constexpr wgpu::DepthStencilState depthStencilState //
        {
            .format = GpuHelper::kDepthBufferFormat,
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
            .depthBias = 2,
            .depthBiasSlopeScale = 2.0f,
            .depthBiasClamp = 0.0f,
        };

    static constexpr wgpu::VertexBufferLayout vertexBufferLayouts[] //
        {
            GetVertexBufferLayout(),
        };

    const wgpu::VertexState vertexState //
        {
            .module = shader,
            .entryPoint = ShaderVertexEntry,
            .bufferCount = 1,
            .buffers = &vertexBufferLayouts[0],
        };

    static constexpr wgpu::PrimitiveState primitiveState //
        {
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CW,
            .cullMode = wgpu::CullMode::Back,
            .unclippedDepth = false,
        };

    static constexpr wgpu::MultisampleState multisampleState //
        {
            .count = 1,
            .mask = 0xFFFFFFFF,
            .alphaToCoverageEnabled = false,
        };

    const wgpu::RenderPipelineDescriptor pipelineDesc //
        {
            .label = "GpuShadowPass",
            .layout = *pipelineLayout,
            .vertex = vertexState,
            .primitive = primitiveState,
            .depthStencil = &depthStencilState,
            .multisample = multisampleState,
            .fragment = nullptr,
        };

    wgpu::RenderPipeline pipeline = gpuDevice.CreateRenderPipeline(&pipelineDesc);
    MLG_CHECK(pipeline, "Failed to create render pipeline");

    return pipeline;
}
} // namespace

/// GpuShadowPass::CreateTask

GpuShadowPass::CreateTask::CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_GpuHelper(&gpuHelper),
      m_ShaderFetcher(gpuHelper, fileFetcher)
{
}

Result<std::unique_ptr<GpuShadowPass>>
GpuShadowPass::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    auto depthBufferResult = m_GpuHelper->CreateShadowDepthBuffer(kShadowBufferSize,
        kShadowBufferSize,
        "GpuShadowPass::DepthBuffer");
    MLG_CHECK(depthBufferResult, "Failed to create depth buffer");

    auto shaderResult = m_ShaderFetcher.Take();
    MLG_CHECK(shaderResult, "Failed to fetch shader: {}", ShaderPath);

    const auto pipelineResult = CreateRenderPipeline(gpuDevice, *shaderResult);
    MLG_CHECK(pipelineResult, "Failed to create render pipelines");

    return std::unique_ptr<GpuShadowPass>(
        new GpuShadowPass(*m_GpuHelper, *depthBufferResult, *pipelineResult));
}

// private:

Result<>
GpuShadowPass::CreateTask::OnStart()
{
    MLG_DEBUG("Creating shadow pass...");

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
GpuShadowPass::CreateTask::OnUpdate()
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
                MLG_DEBUG("Created shadow pass");
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("Failed to create shadow pass");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

/// GpuShadowPass

GpuShadowPass::GpuShadowPass(
    const GpuHelper& gpuHelper, GpuDepthTarget depthTarget, wgpu::RenderPipeline pipeline)
    : m_GpuHelper(&gpuHelper),
      m_DepthTarget(std::move(depthTarget)),
      m_Pipeline(std::move(pipeline))
{
    MLG_ASSERT(m_DepthTarget.IsValid(), "Depth target is not valid");
    MLG_ASSERT(m_Pipeline, "Pipeline is not valid");
}

Result<>
GpuShadowPass::SetGeometryBuffers(const GpuVertexBuffer& vertices, const GpuIndexBuffer& indices)
{
    MLG_CHECKV(vertices.IsValid(), "Invalid vertices buffer");
    MLG_CHECKV(indices.IsValid(), "Invalid indices buffer");

    m_Vertices = vertices;
    m_Indices = indices;

    return Result<>::Ok;
}

Result<>
GpuShadowPass::SetBindGroup(const GpuShadowPass::BindGroup0Helper& bg0)
{
    if(!m_BindGroup0Helper || m_BindGroup0Helper != bg0)
    {
        // Rebuild the bind group
        m_BindGroup0 = {};
    }

    m_BindGroup0Helper = bg0;

    return Result<>::Ok;
}

Result<>
GpuShadowPass::Execute(std::span<MeshInstance> meshes)
{
    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(Execute(meshes, cmdEncoder));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
GpuShadowPass::Execute(std::span<MeshInstance> meshes, const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuShadowPass.Execute")

    auto renderPassResult = CreateRenderPassEncoder(cmdEncoder);
    MLG_CHECK(renderPassResult, "Failed to create render pass encoder");

    const wgpu::RenderPassEncoder renderPass = std::move(*renderPassResult);

    const uint32_t w = m_DepthTarget->GetWidth();
    const uint32_t h = m_DepthTarget->GetHeight();

    renderPass.SetViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1);

    renderPass.SetScissorRect(0, 0, w, h);

    renderPass.SetPipeline(m_Pipeline);

    // Sort the meshes based on material properties to minimize state changes.
    // Opaque meshes should come first, followed by masked meshes, followed by alpha-blended meshes.
    std::ranges::sort(meshes, {}, &MeshInstance::GetSortKey);

    // Separate opaque meshes.
    // Search backwards assuming there are fewer mask/alpha-blended meshes than opaque.
    size_t opaqueCount = meshes.size();

    for(; opaqueCount > 0; --opaqueCount)
    {
        if(meshes[opaqueCount - 1].GetAlphaMode() == AlphaMode::Opaque)
        {
            break;
        }
    }

    const std::span<MeshInstance> opaqueMeshes = meshes.subspan(0, opaqueCount);

    for(const MeshInstance& meshInstance : opaqueMeshes)
    {
        const Mesh& mesh = meshInstance.GetMesh();

        const uint32_t indexCount = mesh.GetIndexCount();
        pcTriangleCount.Increment(indexCount / 3);

        renderPass.DrawIndexed(indexCount,
            1,
            mesh.GetFirstIndex(),
            static_cast<int32_t>(mesh.GetBaseVertex()),
            meshInstance.GetInstanceIndex());
    }

    renderPass.End();

    return Result<>::Ok;
}

// private:

Result<>
GpuShadowPass::EnsureBindGroup0()
{
    if(m_BindGroup0)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_BindGroup0Helper, "BindGroup 0 not valid - forget to call SetBindGroup()?");
    MLG_CHECKV(m_Pipeline, "Pipeline not set?");

    const auto bgResult = m_BindGroup0Helper->CreateBindGroup(m_GpuHelper->GetDevice(),
        m_Pipeline.GetBindGroupLayout(0),
        "GpuShadowPass::BindGroup0");
    MLG_CHECK(bgResult, "Failed to create bind group 0");

    m_BindGroup0 = *bgResult;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuShadowPass::CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECKV(m_BindGroup0Helper, "BindGroup 0 not valid - forget to call SetBindGroup()?");
    MLG_CHECKV(m_Vertices, "Vertex buffer not set - forget to call SetGeometryBuffers()?");
    MLG_CHECKV(m_Indices, "Index buffer not set - forget to call SetGeometryBuffers()?");

    MLG_CHECKV(EnsureBindGroup0());

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_DepthTarget->CreateView(),
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = kClearDepth,
            .stencilLoadOp = wgpu::LoadOp::Undefined,
            .stencilStoreOp = wgpu::StoreOp::Undefined,
            .stencilClearValue = 0,
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "GpuShadowPass",
            .colorAttachmentCount = 0,
            .colorAttachments = nullptr,
            .depthStencilAttachment = &depthStencilAttachment,
        };

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    {
        MLG_SCOPED_TIMER("GpuShadowPass.Prepare.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, m_BindGroup0, 0, nullptr);
    }

    {
        MLG_SCOPED_TIMER("GpuShadowPass.Prepare.SetBuffers");

        constexpr size_t kU16BitWidth = 16;
        constexpr size_t kU32BitWidth = 32;

        static_assert(VERTEX_INDEX_BITS == kU32BitWidth || VERTEX_INDEX_BITS == kU16BitWidth,
            "Unsupported index buffer format: only 16-bit and 32-bit indices are supported");

        constexpr wgpu::IndexFormat idxFmt = (VERTEX_INDEX_BITS == kU32BitWidth)
            ? wgpu::IndexFormat::Uint32
            : wgpu::IndexFormat::Uint16;

        renderPass.SetVertexBuffer(0, m_Vertices->GetBuffer(), 0, m_Vertices->GetSize());
        renderPass.SetIndexBuffer(m_Indices->GetBuffer(), idxFmt, 0, m_Indices->GetSize());
    }

    return renderPass;
}