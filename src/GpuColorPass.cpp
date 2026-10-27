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

constexpr wgpu::BindGroupLayoutEntry InputBindGroupLayoutEntries[]//
{
    // World transform.
    wgpu::BindGroupLayoutEntry//
        {
        .binding = 0,
        .visibility = wgpu::ShaderStage::Vertex,
        .buffer =
        {
            .type = wgpu::BufferBindingType::ReadOnlyStorage,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::WorldTransform),
        },
    },
    // Clip transform.
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 1,
        .visibility = wgpu::ShaderStage::Vertex,
        .buffer =
        {
            .type = wgpu::BufferBindingType::ReadOnlyStorage,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
        },
    },
    // Mesh instance parameters.
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 2,
        .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
        .buffer =
        {
            .type = wgpu::BufferBindingType::ReadOnlyStorage,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::MeshInstanceParams),
        },
    },
    // Camera parameters
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 3,
        .visibility = wgpu::ShaderStage::Vertex,
        .buffer =
        {
            .type = wgpu::BufferBindingType::Uniform,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::CameraParams),
        },
    },
};

constexpr wgpu::BindGroupLayoutDescriptor InputBindGroupLayoutDescriptor //
    {
        .label = "GpuColorPass::InputsBindGroupLayout",
        .entryCount = std::size(InputBindGroupLayoutEntries),
        .entries = &InputBindGroupLayoutEntries[0],
    };

constexpr wgpu::BindGroupLayoutEntry MaterialBindGroupLayoutEntries[]//
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
    // Material constants
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 2,
        .visibility = wgpu::ShaderStage::Fragment,
        .buffer =
        {
            .type = wgpu::BufferBindingType::Uniform,
            .minBindingSize = sizeof(ShaderInterop::MaterialConstants)
        },
    },
};

constexpr wgpu::BindGroupLayoutDescriptor MaterialBindGroupLayoutDescriptor //
    {
        .label = "GpuColorPass::MaterialBindGroupLayout",
        .entryCount = std::size(MaterialBindGroupLayoutEntries),
        .entries = &MaterialBindGroupLayoutEntries[0],
    };

auto
CreateInputBindGroupEntries(const GpuColorPass::Inputs& inputs)
{
    const std::array entries //
        {
            wgpu::BindGroupEntry //
            {
                .binding = 0,
                .buffer = inputs.WorldTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.WorldTransforms.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 1,
                .buffer = inputs.ClipSpaceTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.ClipSpaceTransforms.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 2,
                .buffer = inputs.MeshInstanceParams.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.MeshInstanceParams.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 3,
                .buffer = inputs.CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.CameraParams.BufferSize(),
            },
        };

    static_assert(std::size(entries) == std::size(InputBindGroupLayoutEntries),
        "Bind group layout entries and bind group entries must have the same size");

    return entries;
}

auto
CreateMaterialBindGroupEntries(const wgpu::Texture& texture,
    const wgpu::Sampler& sampler,
    const GpuMaterialConstantsBuffer& materialConstants)
{
    const std::array entries //
        {
            wgpu::BindGroupEntry //
            {
                .binding = 0,
                .textureView = texture.CreateView(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 1,
                .sampler = sampler,
            },
            wgpu::BindGroupEntry //
            {
                .binding = 2,
                .buffer = materialConstants.GetGpuBuffer(),
                .offset = 0,
                .size = materialConstants.BufferSize(),
            },
        };

    static_assert(std::size(entries) == std::size(MaterialBindGroupLayoutEntries),
        "Bind group layout entries and bind group entries must have the same size");

    return entries;
}

// Creates a bind group layout for the inputs of the color pass.
Result<wgpu::BindGroupLayout>
CreateInputsBindGroupLayout(const wgpu::Device& gpuDevice)
{
    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&InputBindGroupLayoutDescriptor);
    MLG_CHECK(layout, "Failed to create Inputs bind group layout");

    return layout;
}

Result<wgpu::BindGroupLayout>
CreateMaterialBindGroupLayout(const wgpu::Device& gpuDevice)
{
    wgpu::BindGroupLayout layout =
        gpuDevice.CreateBindGroupLayout(&MaterialBindGroupLayoutDescriptor);
    MLG_CHECK(layout, "Failed to create Material bind group layout");

    return layout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& inputsBindGroupLayout,
    const wgpu::BindGroupLayout& materialBindGroupLayout)
{
    MLG_CHECK(inputsBindGroupLayout, "Inputs bind group layout is not valid");
    MLG_CHECK(materialBindGroupLayout, "Material bind group layout is not valid");

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            inputsBindGroupLayout,
            materialBindGroupLayout,
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

bool
BindGroup0NeedsRefresh(const GpuColorPass::Inputs& currentInputs,
    const GpuColorPass::Inputs& newInputs)
{
    return currentInputs.WorldTransforms.GetGpuBuffer().Get()
        != newInputs.WorldTransforms.GetGpuBuffer().Get()
        || currentInputs.ClipSpaceTransforms.GetGpuBuffer().Get()
        != newInputs.ClipSpaceTransforms.GetGpuBuffer().Get()
        || currentInputs.MeshInstanceParams.GetGpuBuffer().Get()
        != newInputs.MeshInstanceParams.GetGpuBuffer().Get()
        || currentInputs.CameraParams.GetGpuBuffer().Get()
        != newInputs.CameraParams.GetGpuBuffer().Get();
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

    auto shader = m_ShaderFetcher.Take();
    MLG_CHECK(shader, "Failed to fetch shader: {}", ShaderPath);

    auto inputsBindGroupLayout = CreateInputsBindGroupLayout(gpuDevice);
    MLG_CHECK(inputsBindGroupLayout, "Failed to create Inputs bind group layout");

    auto materialBindGroupLayout = CreateMaterialBindGroupLayout(gpuDevice);
    MLG_CHECK(materialBindGroupLayout, "Failed to create Material bind group layout");

    auto pipelineLayout =
        CreatePipelineLayout(gpuDevice, *inputsBindGroupLayout, *materialBindGroupLayout);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");

    auto defaultSampler = CreateDefaultSampler(gpuDevice);
    MLG_CHECK(defaultSampler, "Failed to create default sampler");

    return std::unique_ptr<GpuColorPass>(new GpuColorPass(*m_GpuHelper,
        *shader,
        *inputsBindGroupLayout,
        *materialBindGroupLayout,
        *pipelineLayout,
        *defaultSampler));
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

Result<>
GpuColorPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(!m_Inputs || BindGroup0NeedsRefresh(*m_Inputs, inputs))
    {
        // Rebuild the bind group
        m_InputsBindGroup = {};
    }

    m_Inputs = inputs;

    return Result<>::Ok;
}

Result<>
GpuColorPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    m_Outputs = outputs;

    return Result<>::Ok;
}

Result<>
GpuColorPass::Execute(std::span<MeshInstance> meshes,
    const std::span<const GpuMaterialProperties> materialProperties)
{
    const wgpu::Device gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder();
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(Execute(cmdEncoder, meshes, materialProperties));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
GpuColorPass::Execute(const wgpu::CommandEncoder& cmdEncoder,
    std::span<MeshInstance> meshes,
    const std::span<const GpuMaterialProperties> materialProperties)
{
    MLG_SCOPED_TIMER("GpuColorPass.Execute")

    auto renderPassResult = CreateRenderPassEncoder(cmdEncoder);
    MLG_CHECK(renderPassResult, "Failed to create render pass encoder");

    const wgpu::RenderPassEncoder renderPass = std::move(*renderPassResult);

    const uint32_t w = m_Outputs->RenderTarget->GetWidth();
    const uint32_t h = m_Outputs->RenderTarget->GetHeight();

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
    auto entries = CreateMaterialBindGroupEntries(texture, m_DefaultSampler, materialConstants);

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = name,
            .layout = m_MaterialBindGroupLayout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    const wgpu::BindGroup bindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(bindGroup, "Failed to create texture bind group");

    return bindGroup;
}

// private:

Result<>
GpuColorPass::EnsurePipeline()
{
    if(m_TranslucentPipeline && m_OpaquePipeline)
    {
        return Result<>::Ok;
    }

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
            .module = m_Shader,
            .entryPoint = FragmentEntry,
            .targetCount = 1,
            .targets = &translucentTargetState,
        };

    const wgpu::FragmentState opaqueFragmentState //
        {
            .module = m_Shader,
            .entryPoint = FragmentEntry,
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
            .module = m_Shader,
            .entryPoint = VertexEntry,
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
            .layout = m_PipelineLayout,
            .vertex = vertexState,
            .primitive = primitiveState,
            .depthStencil = &opaqueDepthStencilState,
            .multisample = multisampleState,
            .fragment = &opaqueFragmentState,
        };

    const wgpu::RenderPipelineDescriptor translucentDescriptor //
        {
            .label = "GpuColorPass_Translucent",
            .layout = m_PipelineLayout,
            .vertex = vertexState,
            .primitive = primitiveState,
            .depthStencil = &translucentDepthStencilState,
            .multisample = multisampleState,
            .fragment = &translucentFragmentState,
        };

    m_OpaquePipeline = m_GpuHelper->GetDevice().CreateRenderPipeline(&opaqueDescriptor);
    MLG_CHECK(m_OpaquePipeline, "Failed to create render pipeline");

    m_TranslucentPipeline = m_GpuHelper->GetDevice().CreateRenderPipeline(&translucentDescriptor);
    MLG_CHECK(m_TranslucentPipeline, "Failed to create render pipeline");

    return Result<>::Ok;
}

Result<>
GpuColorPass::EnsureInputsBindGroup()
{
    if(m_InputsBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");

    auto entries = CreateInputBindGroupEntries(m_Inputs.value());

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuColorPass::InputsBindGroup",
            .layout = m_InputsBindGroupLayout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    m_InputsBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(m_InputsBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputsBindGroup());

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    MLG_CHECKV(m_Outputs->RenderTarget->GetFormat() == GpuHelper::kRenderTargetFormat,
        "Unexpected render target format");

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs->RenderTarget->CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_Outputs->DepthBuffer->CreateView(),
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
        renderPass.SetBindGroup(0, m_InputsBindGroup, 0, nullptr);
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

        renderPass.SetVertexBuffer(0,
            m_Inputs->Vertices.GetGpuBuffer(),
            0,
            m_Inputs->Vertices.BufferSize());

        renderPass.SetIndexBuffer(m_Inputs->Indices.GetGpuBuffer(),
            idxFmt,
            0,
            m_Inputs->Indices.BufferSize());
    }

    return renderPass;
}