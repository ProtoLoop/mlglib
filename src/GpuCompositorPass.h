#pragma once

#include "BindGroup.h"
#include "CoopTask.h"
#include "GpuTypes.h"
#include "ShaderFetcher.h"
#include "VecMath.h"

#include <memory>
#include <optional>

class FileFetcher;
class GpuHelper;

/// A GPU pass that composites a texture onto another texture.
class GpuCompositorPass
{
public:
    class CreateTask;

    using BindGroup0 = BindGroup<TextureBinding<wgpu::ShaderStage::Fragment>,
        SamplerBinding<wgpu::ShaderStage::Fragment>>;

    GpuCompositorPass() = delete;
    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = delete;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = delete;

    /// Sets the source render target for the compositor pass.
    /// The source render target will be composited onto the destination render target.
    Result<> SetSourceRenderTarget(const GpuRenderTarget& src);

    /// Executes the pass by compositing the source render target onto the destination rectangle
    /// of the destination render target.
    /// We do not store the destination render target internally because if it's from a swap chain,
    /// it may change every frame.
    /// This variant of Execute creates a command encoder that's owned and submitted to the GPU
    /// before returning.
    Result<> Execute(const GpuRenderTarget& dst, const Rect& dstRect);

    /// Executes the pass by compositing the source render target onto the destination rectangle
    /// of the destination render target.
    /// We do not store the destination render target internally because if it's from a swap chain,
    /// it may change every frame.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    Result<> Execute(
        const GpuRenderTarget& dst, const Rect& dstRect, const wgpu::CommandEncoder& cmdEncoder);

private:
    GpuCompositorPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::Sampler sampler,
        wgpu::BindGroupLayout bindGroupLayout,
        wgpu::PipelineLayout pipelineLayout);

    Result<> EnsurePipeline(const wgpu::TextureFormat targetFormat);
    Result<> EnsureBindGroup0();
    Result<wgpu::RenderPassEncoder> CreateRenderPassEncoder(
        const GpuRenderTarget& dst, const Rect& dstRect, const wgpu::CommandEncoder& cmdEncoder);

    std::optional<GpuRenderTarget> m_SrcRenderTarget;

    const GpuHelper* m_GpuHelper{ nullptr };
    wgpu::ShaderModule m_Shader;
    wgpu::Sampler m_Sampler;
    wgpu::BindGroupLayout m_BindGroup0Layout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_BindGroup0;
    wgpu::RenderPipeline m_Pipeline;
    wgpu::TextureFormat m_TargetFormat{ wgpu::TextureFormat::Undefined };
};

class GpuCompositorPass::CreateTask : public ICoopTask<>
{
public:
    CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuCompositorPass>> Take();

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