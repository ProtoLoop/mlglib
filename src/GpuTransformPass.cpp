#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

namespace
{

constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
constexpr const char* ComputeEntry = "cs_main";
constexpr const char* kWorkgroupSizeOverride = "WorkgroupSizeOverride";
constexpr size_t kWorkgroupSize = 64;

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice)
{
    auto bindGroup0Layout = GpuTransformPass::BindGroup0Helper::CreateBindGroupLayout(gpuDevice,
        "GpuTransformPass::BindGroup0Layout");
    MLG_CHECK(bindGroup0Layout);

    auto bindGroup1Layout = GpuTransformPass::BindGroup1Helper::CreateBindGroupLayout(gpuDevice,
        "GpuTransformPass::BindGroup1Layout");
    MLG_CHECK(bindGroup1Layout);

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            *bindGroup0Layout,
            *bindGroup1Layout,
        };

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuTransformPass",
            .bindGroupLayoutCount = std::size(bindGroupLayouts),
            .bindGroupLayouts = &bindGroupLayouts[0],
        };

    wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}

Result<wgpu::ComputePipeline>
CreatePipeline(const wgpu::Device& gpuDevice, const wgpu::ShaderModule& shader)
{
    static constexpr wgpu::ConstantEntry constants[] //
        {
            {
                .key = kWorkgroupSizeOverride,
                .value = static_cast<double>(kWorkgroupSize),
            },
        };

    auto pipelineLayout = CreatePipelineLayout(gpuDevice);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");

    const wgpu::ComputePipelineDescriptor desc //
        {
            .label = "GpuTransformPass",
            .layout = *pipelineLayout,
            .compute //
            {
                .module = shader,
                .entryPoint = ComputeEntry,
                .constantCount = std::size(constants),
                .constants = &constants[0],
            },
        };
    ;

    wgpu::ComputePipeline pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(pipeline, "Failed to create pipeline");

    return pipeline;
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

    auto pipelineResult = CreatePipeline(gpuDevice, *shader);
    MLG_CHECK(pipelineResult, "Failed to create pipeline");

    return std::unique_ptr<GpuTransformPass>(new GpuTransformPass(*m_GpuHelper, *pipelineResult));
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

GpuTransformPass::GpuTransformPass(const GpuHelper& gpuHelper, wgpu::ComputePipeline pipeline)
    : m_GpuHelper(&gpuHelper),
      m_Pipeline(std::move(pipeline))
{
    MLG_ASSERT(m_Pipeline, "Pipeline is not valid");
}

Result<>
GpuTransformPass::SetBindGroups(const BindGroup0Helper& bg0, const BindGroup1Helper& bg1)
{
    if(bg0 != m_BindGroup0Helper)
    {
        m_BindGroup0Helper = bg0;

        // Rebuild the bind group
        m_BindGroup0 = {};
    }

    if(bg1 != m_BindGroup1Helper)
    {
        m_BindGroup1Helper = bg1;

        // Rebuild the bind group
        m_BindGroup1 = {};
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
GpuTransformPass::Execute(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("GpuTransformPass.Execute")

    auto computePassResult = CreateComputePassEncoder(cmdEncoder);
    MLG_CHECK(computePassResult, "Failed to create compute pass encoder");

    const wgpu::ComputePassEncoder computePass = std::move(*computePassResult);

    MLG_CHECKV(m_BindGroup0Helper, "Bind group is not valid - forget to call SetBindGroups()?");
    const GpuWorldTransformBuffer& worldTransforms =
        m_BindGroup0Helper->GetResource<GpuWorldTransformBuffer>();
    const size_t instanceCount = worldTransforms.Count();

    // Number of workgroups to dispatch is the number of instances divided by the workgroup size,
    // rounded up.
    const size_t workgroupCountX =
        (instanceCount / kWorkgroupSize) + (instanceCount % kWorkgroupSize != 0);

    computePass.DispatchWorkgroups(static_cast<uint32_t>(workgroupCountX));
    computePass.End();

    return Result<>::Ok;
}

// private:

Result<>
GpuTransformPass::EnsureBindGroups()
{
    if(!m_BindGroup0)
    {
        MLG_CHECKV(m_BindGroup0Helper,
            "BindGroup 0 parameters are not valid - forget to call SetBindGroups()?");
        MLG_CHECKV(m_Pipeline, "Pipeline is not valid");

        const auto bgResult = m_BindGroup0Helper->CreateBindGroup(m_GpuHelper->GetDevice(),
            m_Pipeline.GetBindGroupLayout(0),
            "GpuTransformPass::BindGroup0");
        MLG_CHECKV(bgResult, "Failed to create bind group 0");

        m_BindGroup0 = *bgResult;
    }

    if(!m_BindGroup1)
    {
        MLG_CHECKV(m_BindGroup1Helper,
            "BindGroup 1 parameters are not valid - forget to call SetBindGroups()?");

        const auto bgResult = m_BindGroup1Helper->CreateBindGroup(m_GpuHelper->GetDevice(),
            m_Pipeline.GetBindGroupLayout(1),
            "GpuTransformPass::BindGroup1");
        MLG_CHECKV(bgResult, "Failed to create bind group 1");

        m_BindGroup1 = *bgResult;
    }

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::CreateComputePassEncoder(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsureBindGroups());

    MLG_CHECKV(m_BindGroup0Helper,
        "Bind group 0 parameters are not valid - forget to call SetBindGroups()?");

    const GpuWorldTransformBuffer& worldTransforms =
        m_BindGroup0Helper->GetResource<GpuWorldTransformBuffer>();
    const GpuClipSpaceTransformBuffer& clipSpaceTransforms =
        m_BindGroup0Helper->GetResource<GpuClipSpaceTransformBuffer>();
    MLG_CHECK(worldTransforms.GetSize() <= clipSpaceTransforms.GetSize(),
        "The ClipSpaceTransforms buffer must be at least as big as the WorldTransforms buffer");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    MLG_CHECK(computePass, "Failed to begin compute pass");

    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_BindGroup0);
    computePass.SetBindGroup(1, m_BindGroup1);

    return computePass;
}