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

    using ViewProjBufferBinding = BufferBinding<GpuViewProjBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Compute>;

    using BindGroup0Helper = BindGroupHelper<WorldTransformBufferBinding, ClipSpaceBufferBinding>;

    using BindGroup1Helper = BindGroupHelper<ViewProjBufferBinding>;

    GpuTransformPass() = delete;
    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = delete;
    GpuTransformPass& operator=(GpuTransformPass&&) = delete;

    Result<> SetBindGroups(const BindGroup0Helper& bg0, const BindGroup1Helper& bg1);

    /// Executes the pass by performing the transform computation.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    Result<> Execute();

    /// Executes the pass by performing the transform computation.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    Result<> Execute(const wgpu::CommandEncoder& cmdEncoder);

private:
    GpuTransformPass(const GpuHelper& gpuHelper, wgpu::ComputePipeline pipeline);

    Result<> EnsureBindGroups();

    Result<wgpu::ComputePassEncoder> CreateComputePassEncoder(
        const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper;

    std::optional<BindGroup0Helper> m_BindGroup0Helper;
    std::optional<BindGroup1Helper> m_BindGroup1Helper;

    wgpu::BindGroup m_BindGroup0;
    wgpu::BindGroup m_BindGroup1;
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