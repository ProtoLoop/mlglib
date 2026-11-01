#pragma once

#include "BindGroupHelper.h"
#include "CoopTask.h"
#include "GpuTypes.h"
#include "Result.h"
#include "ShaderFetcher.h"

#include <memory>
#include <optional>

class FileFetcher;
class GpuHelper;

class GpuTransformPass
{
public:
    class CreateTask;

    using WorldTransformBufferBinding = BufferBinding<GpuWorldTransformBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Compute>;

    using ClipSpaceBufferBinding = BufferBinding<GpuClipSpaceTransformBuffer,
        wgpu::BufferBindingType::Storage,
        wgpu::ShaderStage::Compute>;

    using CameraParamsBufferBinding = BufferBinding<GpuCameraParamsBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Compute>;

    using BindGroup0 =
        BindGroupHelper<WorldTransformBufferBinding, ClipSpaceBufferBinding, CameraParamsBufferBinding>;

    GpuTransformPass() = delete;
    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = delete;
    GpuTransformPass& operator=(GpuTransformPass&&) = delete;

    Result<> SetBindGroup(const BindGroup0& bg0);

    /// Executes the pass by performing the transform computation.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    Result<> Execute();

    /// Executes the pass by performing the transform computation.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    Result<> Execute(const wgpu::CommandEncoder& cmdEncoder);

private:
    GpuTransformPass(const GpuHelper& gpuHelper,
        wgpu::BindGroupLayout bindGroupLayout,
        wgpu::ComputePipeline pipeline);

    Result<> EnsureBindGroup0();
    Result<wgpu::ComputePassEncoder> CreateComputePassEncoder(
        const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper;

    std::optional<BindGroup0> m_BindGroup0Params;

    wgpu::BindGroupLayout m_BindGroup0Layout;
    wgpu::BindGroup m_BindGroup0;
    wgpu::ComputePipeline m_Pipeline;
};

class GpuTransformPass::CreateTask : public ICoopTask<>
{
public:
    CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuTransformPass>> Take();

private:
    enum class Stage
    {
        None,
        FetchingShader,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    const GpuHelper* m_GpuHelper{ nullptr };
    ShaderFetcher m_ShaderFetcher;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};