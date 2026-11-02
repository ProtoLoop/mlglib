#pragma once

#include "BindGroupHelper.h"
#include "CoopTask.h"
#include "GpuTypes.h"
#include "ShaderFetcher.h"

#include <memory>
#include <optional>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;
class MeshInstance;

class GpuShadowPass
{
public:
    /// The size of the shadow buffer (both width and height).
    static constexpr uint32_t kShadowBufferSize = 1024;

    class CreateTask;

    using ClipSpaceBufferBinding = BufferBinding<GpuClipSpaceTransformBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Vertex>;

    using MeshInstanceParamsBufferBinding = BufferBinding<GpuMeshInstanceParamsBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment>;

    using BindGroup0Helper = BindGroupHelper<ClipSpaceBufferBinding, MeshInstanceParamsBufferBinding>;

    GpuShadowPass() = delete;
    ~GpuShadowPass() = default;
    GpuShadowPass(const GpuShadowPass&) = delete;
    GpuShadowPass& operator=(const GpuShadowPass&) = delete;
    GpuShadowPass(GpuShadowPass&&) = delete;
    GpuShadowPass& operator=(GpuShadowPass&&) = delete;

    Result<> SetGeometryBuffers(const GpuVertexBuffer& vertices, const GpuIndexBuffer& indices);
    Result<> SetBindGroup(const BindGroup0Helper& bg0);

    /// Returns the depth target used by the shadow pass.
    const GpuDepthTarget& GetDepthTarget() { return m_DepthTarget; }

    /// Executes the pass by rendering the meshses to the render attachments.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(std::span<MeshInstance> meshes);

    /// Executes the pass by rendering the meshses to the render attachments.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(std::span<MeshInstance> meshes, const wgpu::CommandEncoder& cmdEncoder);

private:
    explicit GpuShadowPass(
        const GpuHelper& gpuHelper, GpuDepthTarget depthTarget, wgpu::RenderPipeline pipeline);

    Result<> EnsureBindGroup0();
    Result<wgpu::RenderPassEncoder> CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<GpuVertexBuffer> m_Vertices;
    std::optional<GpuIndexBuffer> m_Indices;
    std::optional<BindGroup0Helper> m_BindGroup0Helper;
    GpuDepthTarget m_DepthTarget;

    wgpu::BindGroup m_BindGroup0;
    wgpu::RenderPipeline m_Pipeline;
};

class GpuShadowPass::CreateTask : public ICoopTask<>
{
public:
    CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuShadowPass>> Take();

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