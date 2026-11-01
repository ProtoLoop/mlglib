#include <webgpu/webgpu_cpp.h>
#define MLG_LOGGER_NAME "CPAS"

#include "FileFetcher.h"
#include "GpuColorPass.h"
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

template<typename BindGroupType>
Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const wgpu::Device& gpuDevice, const std::string_view label)
{
    const wgpu::BindGroupLayoutDescriptor bgLayoutDesc //
        {
            .label = label,
            .entryCount = std::size(BindGroupType::LayoutEntries),
            .entries = BindGroupType::LayoutEntries.data(),
        };

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&bgLayoutDesc);
    MLG_CHECK(layout, "Failed to create bind group layout");

    return layout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& bindGroup0Layout,
    const wgpu::BindGroupLayout& bindGroup1Layout)
{
    MLG_CHECK(bindGroup0Layout, "Bind group 0 layout is not valid");
    MLG_CHECK(bindGroup1Layout, "Bind group 1 layout is not valid");

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            bindGroup0Layout,
            bindGroup1Layout,
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
CreateRenderPipelines(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& bg0Layout,
    const wgpu::BindGroupLayout& bg1Layout,
    const wgpu::ShaderModule& shader)
{
    auto pipelineLayout = CreatePipelineLayout(gpuDevice, bg0Layout, bg1Layout);
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
CreateDefaultSampler(const wgpu::Device& gpuDevice)
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

    auto bg0LayoutResult =
        CreateBindGroupLayout<BindGroup0>(gpuDevice, "GpuColorPass::BindGroup0Layout");
    MLG_CHECK(bg0LayoutResult, "Failed to create bind group 0 layout");

    auto bg1LayoutResult =
        CreateBindGroupLayout<BindGroup1>(gpuDevice, "GpuColorPass::BindGroup1Layout");
    MLG_CHECK(bg1LayoutResult, "Failed to create bind group 1 layout");

    auto samplerResult = CreateDefaultSampler(gpuDevice);
    MLG_CHECK(samplerResult, "Failed to create default sampler");

    const auto pipelineResult =
        CreateRenderPipelines(gpuDevice, *bg0LayoutResult, *bg1LayoutResult, *shaderResult);

    MLG_CHECK(pipelineResult, "Failed to create render pipelines");

    return std::unique_ptr<GpuColorPass>(new GpuColorPass(*m_GpuHelper,
        *bg0LayoutResult,
        *bg1LayoutResult,
        pipelineResult->OpaquePipeline,
        pipelineResult->TranslucentPipeline,
        *samplerResult));
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
    wgpu::BindGroupLayout bg0Layout,
    wgpu::BindGroupLayout bg1Layout,
    wgpu::RenderPipeline opaquePipeline,
    wgpu::RenderPipeline translucentPipeline,
    wgpu::Sampler defaultSampler)
    : m_GpuHelper(&gpuHelper),
      m_BindGroup0Layout(std::move(bg0Layout)),
      m_BindGroup1Layout(std::move(bg1Layout)),
      m_OpaquePipeline(std::move(opaquePipeline)),
      m_TranslucentPipeline(std::move(translucentPipeline)),
      m_DefaultSampler(std::move(defaultSampler))
{
    MLG_ASSERT(m_BindGroup0Layout, "Bind group 0 layout is not valid");
    MLG_ASSERT(m_BindGroup1Layout, "Bind group 1 layout is not valid");
    MLG_ASSERT(m_OpaquePipeline, "Opaque pipeline is not valid");
    MLG_ASSERT(m_TranslucentPipeline, "Translucent pipeline is not valid");
    MLG_ASSERT(m_DefaultSampler, "Default sampler is not valid");
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
GpuColorPass::SetBindGroup(const GpuColorPass::BindGroup0& bg0)
{
    if(!m_BindGroup0Params || m_BindGroup0Params != bg0)
    {
        // Rebuild the bind group
        m_BindGroup0 = {};
    }

    m_BindGroup0Params = bg0;

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
    std::ranges::sort(meshes, {}, &MeshInstance::GetSortKey);

    // Find the first alpha-blended mesh in the sorted list
    // so we can separate opaque and alpha-blended meshes.
    size_t firstAlphaIndex = meshes.size();

    for(; firstAlphaIndex > 0;)
    {
        if(meshes[firstAlphaIndex - 1].GetAlphaMode() != AlphaMode::Blend)
        {
            break;
        }
        --firstAlphaIndex;
    }

    // Split the sorted meshes into opaque and alpha-blended subsets.

    const std::span<MeshInstance> allMeshes = std::span(meshes);
    const std::span<MeshInstance> opaqueMeshes = allMeshes.subspan(0, firstAlphaIndex);
    const std::span<MeshInstance> alphaMeshes = allMeshes.subspan(firstAlphaIndex);

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
            renderPass.SetBindGroup(1, materialProperties[matIndex].GetBindGroup(), 0, nullptr);
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
                renderPass.SetBindGroup(1, materialProperties[matIndex].GetBindGroup(), 0, nullptr);
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
    auto bgResult = BindGroup1::Create(texture, m_DefaultSampler, materialConstants);
    MLG_CHECK(bgResult, "Failed to create bind group 1");

    const auto entries = bgResult->GetBindGroupEntries();

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = name,
            .layout = m_BindGroup1Layout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    const wgpu::BindGroup bindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(bindGroup, "Failed to create texture bind group");

    return bindGroup;
}

// private:

Result<>
GpuColorPass::EnsureBindGroup0()
{
    if(m_BindGroup0)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_BindGroup0Params, "BindGroup 0 not valid - forget to call SetBindGroup()?");

    const auto entries = m_BindGroup0Params->GetBindGroupEntries();

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuColorPass::BindGroup0",
            .layout = m_BindGroup0Layout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    m_BindGroup0 = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(m_BindGroup0, "Failed to create bind group 0");

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECKV(m_BindGroup0Params, "BindGroup 0 not valid - forget to call SetBindGroup()?");
    MLG_CHECKV(m_Vertices, "Vertex buffer not set - forget to call SetGeometryBuffers()?");
    MLG_CHECKV(m_Indices, "Index buffer not set - forget to call SetGeometryBuffers()?");
    MLG_CHECKV(m_RenderAttachments,
        "Render attachments are not valid - forget to call SetRenderAttachments()?");

    MLG_CHECKV(m_RenderAttachments->RenderTarget->GetFormat() == GpuHelper::kRenderTargetFormat,
        "Unexpected render target format");

    MLG_CHECKV(EnsureBindGroup0());

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