#define MLG_LOGGER_NAME "CPAS"

#include "GpuColorPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"
#include "SceneTypes.h"

#include <algorithm>
#include <span>

namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PerfCounter pcTriangleCount({ .Name = "GpuColorPass.Triangles" });

constexpr const char* ShaderPath = "shaders/ColorShader.wgsl";
constexpr const char* ShaderVertexEntry = "vs_main";
constexpr const char* ShaderFragmentEntry = "fs_main";
constexpr float kClearDepth = 1.0f;

struct PipelinePair
{
    wgpu::RenderPipeline OpaquePipeline;
    wgpu::RenderPipeline TranslucentPipeline;
};

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice)
{
    auto bg0LayoutResult =
        GpuColorPass::BindGroup0Helper::CreateBindGroupLayout(gpuDevice, "GpuColorPass::BindGroup0Layout");
    MLG_CHECK(bg0LayoutResult, "Failed to create bind group 0 layout");

    auto bg1LayoutResult =
        GpuColorPass::BindGroup1Helper::CreateBindGroupLayout(gpuDevice, "GpuColorPass::BindGroup1Layout");
    MLG_CHECK(bg1LayoutResult, "Failed to create bind group 1 layout");

    auto bg2LayoutResult =
        GpuColorPass::BindGroup2Helper::CreateBindGroupLayout(gpuDevice, "GpuColorPass::BindGroup2Layout");
    MLG_CHECK(bg2LayoutResult, "Failed to create bind group 2 layout");
    
    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            *bg0LayoutResult,
            *bg1LayoutResult,
            *bg2LayoutResult,
        };

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuColorPass",
            .bindGroupLayoutCount = std::size(bindGroupLayouts),
            .bindGroupLayouts = &bindGroupLayouts[0],
        };

    const wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create color pipeline layout");

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
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, normal),
                .shaderLocation = 1,
            },
            {
                .format = wgpu::VertexFormat::Float32x2,
                .offset = offsetof(Vertex, uvs[0]),
                .shaderLocation = 2,
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

Result<PipelinePair>
CreateRenderPipelines(const wgpu::Device& gpuDevice, const wgpu::ShaderModule& shader)
{
    auto pipelineLayout = CreatePipelineLayout(gpuDevice);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");

    static constexpr wgpu::BlendState translucentBlendState //
        {
            .color =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::SrcAlpha,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
            },
            .alpha =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::Zero,
            },
        };

    static constexpr wgpu::ColorTargetState translucentTargetState //
        {
            .format = GpuHelper::kRenderTargetFormat,
            .blend = &translucentBlendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    static constexpr wgpu::ColorTargetState opaqueTargetState //
        {
            .format = GpuHelper::kRenderTargetFormat,
            .blend = nullptr,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::FragmentState translucentFragmentState //
        {
            .module = shader,
            .entryPoint = ShaderFragmentEntry,
            .targetCount = 1,
            .targets = &translucentTargetState,
        };

    const wgpu::FragmentState opaqueFragmentState //
        {
            .module = shader,
            .entryPoint = ShaderFragmentEntry,
            .targetCount = 1,
            .targets = &opaqueTargetState,
        };

    static constexpr wgpu::DepthStencilState opaqueDepthStencilState //
        {
            .format = GpuHelper::kDepthBufferFormat,
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
            /*.stencilFront =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilBack =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,*/
            .depthBias = 0,
            .depthBiasSlopeScale = 0.0f,
            .depthBiasClamp = 0.0f,
        };

    static constexpr wgpu::DepthStencilState translucentDepthStencilState //
        {
            .format = GpuHelper::kDepthBufferFormat,
            .depthWriteEnabled = false, // Disabled for translucent objects.
            .depthCompare = wgpu::CompareFunction::Less,
            /*.stencilFront =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilBack =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,*/
            .depthBias = 0,
            .depthBiasSlopeScale = 0.0f,
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

    const wgpu::RenderPipelineDescriptor opaqueDescriptor //
        {
            .label = "GpuColorPass_Opaque",
            .layout = *pipelineLayout,
            .vertex = vertexState,
            .primitive = primitiveState,
            .depthStencil = &opaqueDepthStencilState,
            .multisample = multisampleState,
            .fragment = &opaqueFragmentState,
        };

    const wgpu::RenderPipelineDescriptor translucentDescriptor //
        {
            .label = "GpuColorPass_Translucent",
            .layout = *pipelineLayout,
            .vertex = vertexState,
            .primitive = primitiveState,
            .depthStencil = &translucentDepthStencilState,
            .multisample = multisampleState,
            .fragment = &translucentFragmentState,
        };

    wgpu::RenderPipeline opaquePipeline = gpuDevice.CreateRenderPipeline(&opaqueDescriptor);
    MLG_CHECK(opaquePipeline, "Failed to create opaque render pipeline");

    wgpu::RenderPipeline translucentPipeline =
        gpuDevice.CreateRenderPipeline(&translucentDescriptor);
    MLG_CHECK(translucentPipeline, "Failed to create translucent render pipeline");

    return PipelinePair //
        {
            .OpaquePipeline = std::move(opaquePipeline),
            .TranslucentPipeline = std::move(translucentPipeline),
        };
}

Result<wgpu::Sampler>
CreateTextureSampler(const wgpu::Device& gpuDevice)
{
    static constexpr wgpu::SamplerDescriptor samplerDesc //
        {
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .addressModeW = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .mipmapFilter = wgpu::MipmapFilterMode::Linear,
        };

    wgpu::Sampler sampler = gpuDevice.CreateSampler(&samplerDesc);
    MLG_CHECK(sampler, "Failed to create default sampler");

    return sampler;
}
} // namespace

/// GpuColorPass::CreateTask

GpuColorPass::CreateTask::CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_GpuHelper(&gpuHelper),
      m_ShaderFetcher(gpuHelper, fileFetcher)
{
}

Result<std::unique_ptr<GpuColorPass>>
GpuColorPass::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    auto shaderResult = m_ShaderFetcher.Take();
    MLG_CHECK(shaderResult, "Failed to fetch shader: {}", ShaderPath);

    auto textureSamplerResult = CreateTextureSampler(gpuDevice);
    MLG_CHECK(textureSamplerResult, "Failed to create texture sampler");

    const auto pipelineResult = CreateRenderPipelines(gpuDevice, *shaderResult);

    MLG_CHECK(pipelineResult, "Failed to create render pipelines");

    return std::unique_ptr<GpuColorPass>(new GpuColorPass(*m_GpuHelper,
        pipelineResult->OpaquePipeline,
        pipelineResult->TranslucentPipeline,
        *textureSamplerResult));
}

// private:

Result<>
GpuColorPass::CreateTask::OnStart()
{
    MLG_DEBUG("Creating color pass...");

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
GpuColorPass::CreateTask::OnUpdate()
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
                MLG_DEBUG("Created color pass");
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("Failed to create color pass");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

/// GpuColorPass

GpuColorPass::GpuColorPass(const GpuHelper& gpuHelper,
    wgpu::RenderPipeline opaquePipeline,
    wgpu::RenderPipeline translucentPipeline,
    wgpu::Sampler textureSampler)
    : m_GpuHelper(&gpuHelper),
      m_OpaquePipeline(std::move(opaquePipeline)),
      m_TranslucentPipeline(std::move(translucentPipeline)),
      m_TextureSampler(std::move(textureSampler))
{
    MLG_ASSERT(m_OpaquePipeline, "Opaque pipeline is not valid");
    MLG_ASSERT(m_TranslucentPipeline, "Translucent pipeline is not valid");
    MLG_ASSERT(m_TextureSampler, "Texture sampler is not valid");
}

Result<>
GpuColorPass::SetGeometryBuffers(const GpuVertexBuffer& vertices, const GpuIndexBuffer& indices)
{
    MLG_CHECKV(vertices.IsValid(), "Invalid vertices buffer");
    MLG_CHECKV(indices.IsValid(), "Invalid indices buffer");

    m_Vertices = vertices;
    m_Indices = indices;

    return Result<>::Ok;
}

Result<>
GpuColorPass::SetBindGroups(const GpuColorPass::BindGroup0Helper& bg0, const GpuColorPass::BindGroup1Helper& bg1)
{
    if(!m_BindGroup0Helper || m_BindGroup0Helper != bg0)
    {
        // Rebuild the bind group
        m_BindGroup0 = {};
    }

    m_BindGroup0Helper = bg0;

    if(!m_BindGroup1Helper || m_BindGroup1Helper != bg1)
    {
        // Rebuild the bind group
        m_BindGroup1 = {};
    }

    m_BindGroup1Helper = bg1;

    return Result<>::Ok;
}

Result<>
GpuColorPass::SetRenderAttachments(const RenderAttachments& attachments)
{
    MLG_CHECK(attachments.Validate(), "Render attachments are not valid");

    m_RenderAttachments = attachments;

    return Result<>::Ok;
}

Result<>
GpuColorPass::Execute(std::span<MeshInstance> meshes,
    const std::span<const GpuMaterialProperties> materialProperties)
{
    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(Execute(meshes, materialProperties, cmdEncoder));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
GpuColorPass::Execute(std::span<MeshInstance> meshes,
    const std::span<const GpuMaterialProperties> materialProperties,
    const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuColorPass.Execute")

    MLG_CHECKV(m_RenderAttachments,
        "Render attachments are not valid - forget to call SetRenderAttachments()?");

    auto renderPassResult = CreateRenderPassEncoder(cmdEncoder);
    MLG_CHECK(renderPassResult, "Failed to create render pass encoder");

    const wgpu::RenderPassEncoder renderPass = std::move(*renderPassResult);

    const uint32_t w = m_RenderAttachments->RenderTarget->GetWidth();
    const uint32_t h = m_RenderAttachments->RenderTarget->GetHeight();

    renderPass.SetViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1);

    renderPass.SetScissorRect(0, 0, w, h);

    // Sort the meshes based on material properties to minimize state changes.
    // Opaque meshes should come first, followed by masked meshes, followed by alpha-blended meshes.
    std::ranges::sort(meshes, {}, &MeshInstance::GetSortKey);

    // Separate opaque, masked, and alpha-blended meshes.
    // Search backwards assuming there are fewer mask/alpha-blended meshes than opaque.
    size_t opaqueCount = meshes.size();
    size_t maskedCount = 0;

    for(; opaqueCount > 0; --opaqueCount)
    {
        if(meshes[opaqueCount - 1].GetAlphaMode() != AlphaMode::Blend)
        {
            break;
        }
    }

    for(; opaqueCount > 0; --opaqueCount, ++maskedCount)
    {
        if(meshes[opaqueCount - 1].GetAlphaMode() != AlphaMode::Mask)
        {
            break;
        }
    }

    // Combine opaque and masked meshes for now
    const std::span<MeshInstance> opaqueMeshes = meshes.subspan(0, opaqueCount);
    const std::span<MeshInstance> maskedMeshes = meshes.subspan(opaqueCount, maskedCount);
    const std::span<MeshInstance> alphaMeshes = meshes.subspan(opaqueCount + maskedCount);

    // Track how many times we have to change materials, pipelines.
    static PerfCounter pcMaterialChanges({ .Name = "GpuColorPass.Execute.MaterialChanges" });
    static PerfCounter pcPipelineChanges({ .Name = "GpuColorPass.Execute.PipelineChanges" });

    uint32_t currentMaterialIndex = UINT32_MAX;

    // Render opaque meshes.

    renderPass.SetPipeline(m_OpaquePipeline);

    for(const MeshInstance& meshInstance : opaqueMeshes)
    {
        const Mesh& mesh = meshInstance.GetMesh();
        const uint32_t matIndex = mesh.GetMaterialIndex();
        if(currentMaterialIndex != matIndex)
        {
            MLG_ASSERT(matIndex < materialProperties.size(), "Material index out of bounds");

            pcMaterialChanges.Increment(1);
            currentMaterialIndex = matIndex;
            renderPass.SetBindGroup(2, materialProperties[matIndex].GetBindGroup(), 0, nullptr);
        }

        const uint32_t indexCount = mesh.GetIndexCount();
        pcTriangleCount.Increment(indexCount / 3);

        renderPass.DrawIndexed(indexCount,
            1,
            mesh.GetFirstIndex(),
            static_cast<int32_t>(mesh.GetBaseVertex()),
            meshInstance.GetInstanceIndex());
    }

    // For now just render masked meshes the same as opaque meshes.
    for(const MeshInstance& meshInstance : maskedMeshes)
    {
        const Mesh& mesh = meshInstance.GetMesh();
        const uint32_t matIndex = mesh.GetMaterialIndex();
        if(currentMaterialIndex != matIndex)
        {
            MLG_ASSERT(matIndex < materialProperties.size(), "Material index out of bounds");

            pcMaterialChanges.Increment(1);
            currentMaterialIndex = matIndex;
            renderPass.SetBindGroup(2, materialProperties[matIndex].GetBindGroup(), 0, nullptr);
        }

        const uint32_t indexCount = mesh.GetIndexCount();
        pcTriangleCount.Increment(indexCount / 3);

        renderPass.DrawIndexed(indexCount,
            1,
            mesh.GetFirstIndex(),
            static_cast<int32_t>(mesh.GetBaseVertex()),
            meshInstance.GetInstanceIndex());
    }

    // Render alpha-blended meshes.
    if(!alphaMeshes.empty())
    {
        // Sort back to front.
        std::ranges::sort(alphaMeshes, std::ranges::greater{}, &MeshInstance::GetSortDepth);

        renderPass.SetPipeline(m_TranslucentPipeline);
        pcPipelineChanges.Increment(1);

        currentMaterialIndex = UINT32_MAX;
        for(const MeshInstance& meshInstance : alphaMeshes)
        {
            const Mesh& mesh = meshInstance.GetMesh();
            const uint32_t matIndex = mesh.GetMaterialIndex();
            if(currentMaterialIndex != matIndex)
            {
                MLG_ASSERT(matIndex < materialProperties.size(), "Material index out of bounds");

                pcMaterialChanges.Increment(1);
                currentMaterialIndex = matIndex;
                renderPass.SetBindGroup(2, materialProperties[matIndex].GetBindGroup(), 0, nullptr);
            }

            renderPass.DrawIndexed(mesh.GetIndexCount(),
                1,
                mesh.GetFirstIndex(),
                static_cast<int32_t>(mesh.GetBaseVertex()),
                meshInstance.GetInstanceIndex());
        }
    }

    renderPass.End();

    return Result<>::Ok;
}

Result<wgpu::BindGroup>
GpuColorPass::CreateMaterialBindGroup(const wgpu::Texture& texture,
    const GpuMaterialConstantsBuffer& materialConstants,
    const std::string_view name) const
{
    MLG_CHECKV(m_OpaquePipeline, "Pipeline is not valid");

    auto bgHelperResult = BindGroup2Helper::Create(texture, m_TextureSampler, materialConstants);
    MLG_CHECK(bgHelperResult, "Failed to create bind group 2");

    const auto bgResult = bgHelperResult->CreateBindGroup(m_GpuHelper->GetDevice(),
        m_OpaquePipeline.GetBindGroupLayout(2),
        name);
    MLG_CHECKV(bgResult, "Failed to create bind group 2");

    return bgResult;
}

// private:

Result<>
GpuColorPass::EnsureBindGroups()
{
    if(!m_BindGroup0)
    {
        MLG_CHECKV(m_BindGroup0Helper, "BindGroup 0 not valid - forget to call SetBindGroups()?");
        MLG_CHECKV(m_OpaquePipeline, "Pipeline is not valid");

        const auto bgResult = m_BindGroup0Helper->CreateBindGroup(m_GpuHelper->GetDevice(),
            m_OpaquePipeline.GetBindGroupLayout(0),
            "GpuColorPass::BindGroup0");
        MLG_CHECKV(bgResult, "Failed to create bind group 0");
        m_BindGroup0 = *bgResult;
    }
    
    if(!m_BindGroup1)
    {
        MLG_CHECKV(m_BindGroup1Helper, "BindGroup 1 not valid - forget to call SetBindGroups()?");
        MLG_CHECKV(m_OpaquePipeline, "Pipeline is not valid");

        const auto bgResult = m_BindGroup1Helper->CreateBindGroup(m_GpuHelper->GetDevice(),
            m_OpaquePipeline.GetBindGroupLayout(1),
            "GpuColorPass::BindGroup1");
        MLG_CHECKV(bgResult, "Failed to create bind group 1");
        m_BindGroup1 = *bgResult;
    }    

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECKV(m_Vertices, "Vertex buffer not set - forget to call SetGeometryBuffers()?");
    MLG_CHECKV(m_Indices, "Index buffer not set - forget to call SetGeometryBuffers()?");
    MLG_CHECKV(m_RenderAttachments,
        "Render attachments are not valid - forget to call SetRenderAttachments()?");

    MLG_CHECKV(m_RenderAttachments->RenderTarget->GetFormat() == GpuHelper::kRenderTargetFormat,
        "Unexpected render target format");

    MLG_CHECKV(EnsureBindGroups());

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_RenderAttachments->RenderTarget->CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_RenderAttachments->DepthBuffer->CreateView(),
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = kClearDepth,
            .stencilLoadOp = wgpu::LoadOp::Undefined,
            .stencilStoreOp = wgpu::StoreOp::Undefined,
            .stencilClearValue = 0,
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "GpuColorPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
            .depthStencilAttachment = &depthStencilAttachment,
        };

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    {
        MLG_SCOPED_TIMER("GpuColorPass.Prepare.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, m_BindGroup0, 0, nullptr);
        renderPass.SetBindGroup(1, m_BindGroup1, 0, nullptr);
    }

    {
        MLG_SCOPED_TIMER("GpuColorPass.Prepare.SetBuffers");

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