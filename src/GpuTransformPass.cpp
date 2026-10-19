#define MLG_LOGGER_NAME "TPAS"

#include "GpuHelper.h"
#include "GpuTransformPass.h"
#include "PerfMetrics.h"

namespace
{
constexpr wgpu::BindGroupLayoutEntry InputOutputBindGroupLayoutEntries[]//
{
    // World transform.
    wgpu::BindGroupLayoutEntry//
    {
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
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
        .visibility = wgpu::ShaderStage::Compute,
        .buffer =
        {
            .type = wgpu::BufferBindingType::Storage,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
        },
    },
    // Camera parameters
    wgpu::BindGroupLayoutEntry{
        .binding = 2,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer =
        {
            .type = wgpu::BufferBindingType::Uniform,
            .hasDynamicOffset = false,
            .minBindingSize = sizeof(ShaderInterop::CameraParams),
        },
    },
};

auto
CreateInputOutputBindGroupEntries(const GpuTransformPass::Inputs& inputs,
    const GpuTransformPass::Outputs& outputs)
{
    const std::array entries = //
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
                .buffer = outputs.ClipSpaceTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = outputs.ClipSpaceTransforms.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 2,
                .buffer = inputs.CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.CameraParams.BufferSize(),
            },
        };

    static_assert(std::size(entries) == std::size(InputOutputBindGroupLayoutEntries),
        "Bind group layout entries and bind group entries must have the same size");

    return entries;
}

Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const wgpu::Device& gpuDevice)
{
    static constexpr wgpu::BindGroupLayoutDescriptor desc //
        {
            .label = "GpuTransformPass",
            .entryCount = std::size(InputOutputBindGroupLayoutEntries),
            .entries = &InputOutputBindGroupLayoutEntries[0],
        };

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&desc);
    MLG_CHECK(layout, "Failed to create bind group layout");

    return layout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice, const wgpu::BindGroupLayout& bindGroupLayout)
{
    MLG_CHECKV(bindGroupLayout, "Bind group layout is not valid");

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuTransformPass",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };

    wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}

} // namespace

/// GpuTransformPass::CreateTask

GpuTransformPass::CreateTask::CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_GpuHelper(&gpuHelper),
      m_ShaderFetcher(gpuHelper, fileFetcher)
{
}

Result<std::unique_ptr<GpuTransformPass>>
GpuTransformPass::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    auto shader = m_ShaderFetcher.Take();
    MLG_CHECK(shader, "Failed to fetch shader: {}", ShaderPath);

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    auto bindGroupLayout = CreateBindGroupLayout(gpuDevice);
    MLG_CHECK(bindGroupLayout);

    auto pipelineLayout = CreatePipelineLayout(gpuDevice, *bindGroupLayout);
    MLG_CHECK(pipelineLayout);

    return std::unique_ptr<GpuTransformPass>(
        new GpuTransformPass(*m_GpuHelper, *shader, *bindGroupLayout, *pipelineLayout));
}

// private:

Result<>
GpuTransformPass::CreateTask::OnStart()
{
    MLG_DEBUG("Creating transform pass...");

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
GpuTransformPass::CreateTask::OnUpdate()
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
                MLG_DEBUG("Created transform pass");
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("Failed to create transform pass");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}

/// GpuTransformPass

Result<>
GpuTransformPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(inputs != m_Inputs)
    {
        m_Inputs = inputs;

        // Rebuild the bind group
        m_InputOutputBindGroup = {};
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    if(outputs != m_Outputs)
    {
        m_Outputs = outputs;

        // Rebuild the bind group
        m_InputOutputBindGroup = {};
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::Execute()
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
GpuTransformPass::Execute(wgpu::CommandEncoder cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuTransformPass.Execute")

    auto computePassResult = CreateComputePassEncoder(cmdEncoder);
    MLG_CHECK(computePassResult, "Failed to create compute pass encoder");

    const wgpu::ComputePassEncoder computePass = std::move(*computePassResult);

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    const size_t instanceCount = m_Inputs->WorldTransforms.Count();

    // Number of workgroups to dispatch is the number of instances divided by the workgroup size,
    // rounded up.
    const size_t workgroupCountX = (instanceCount / GpuTransformPass::kWorkgroupSize)
        + (instanceCount % GpuTransformPass::kWorkgroupSize != 0);

    computePass.DispatchWorkgroups(static_cast<uint32_t>(workgroupCountX));
    computePass.End();

    return Result<>::Ok;
}

// private:

Result<>
GpuTransformPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();
    static constexpr wgpu::ConstantEntry constants[] //
        {
            {
                .key = kWorkgroupSizeOverride,
                .value = static_cast<double>(kWorkgroupSize),
            },
        };

    const wgpu::ComputePipelineDescriptor desc //
        {
            .label = "GpuTransformPass",
            .layout = m_PipelineLayout,
            .compute //
            {
                .module = m_Shader,
                .entryPoint = ComputeEntry,
                .constantCount = std::size(constants),
                .constants = &constants[0],
            },
        };
    ;

    m_Pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}

Result<>
GpuTransformPass::EnsureInputOutputBindGroup()
{
    if(m_InputOutputBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    auto entries = CreateInputOutputBindGroupEntries(m_Inputs.value(), m_Outputs.value());

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuTransformPass",
            .layout = m_BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
        };

    m_InputOutputBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(m_InputOutputBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::CreateComputePassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputOutputBindGroup());

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    MLG_CHECK(m_Inputs->WorldTransforms.BufferSize() <= m_Outputs->ClipSpaceTransforms.BufferSize(),
        "The ClipSpaceTransforms buffer must be at least as big as the WorldTransforms buffer");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    MLG_CHECK(computePass, "Failed to begin compute pass");

    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_InputOutputBindGroup);

    return computePass;
}