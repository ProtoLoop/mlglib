#pragma once

#include "BindGroup.h"
#include "CoopTask.h"
#include "GpuTypes.h"
#include "ShaderFetcher.h"

#include <memory>
#include <optional>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;
class MeshInstance;

/// Represents the GPU-side material properties, including alpha mode and bind group.
/// The bind group encapsulates ShaderInterop::MaterialConstants, a texture, and a sampler.
class GpuMaterialProperties
{
public:
    GpuMaterialProperties() = delete;

    explicit GpuMaterialProperties(const AlphaMode alphaMode, wgpu::BindGroup bindGroup)
        : m_AlphaMode(alphaMode),
          m_BindGroup(std::move(bindGroup))
    {
    }

    AlphaMode GetAlphaMode() const { return m_AlphaMode; }
    const wgpu::BindGroup& GetBindGroup() const { return m_BindGroup; }

private:
    AlphaMode m_AlphaMode;
    wgpu::BindGroup m_BindGroup;
};

class GpuColorPass
{
public:
    class CreateTask;

    using WorldTransformBufferBinding = BufferBinding<GpuWorldTransformBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Vertex>;

    using ClipSpaceBufferBinding = BufferBinding<GpuClipSpaceTransformBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Vertex>;

    using MeshInstanceParamsBufferBinding = BufferBinding<GpuMeshInstanceParamsBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment>;

    using PointLightBufferBinding = BufferBinding<GpuPointLightBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Fragment>;

    using DirectionalLightBufferBinding = BufferBinding<GpuDirectionalLightBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Fragment>;

    using CameraParamsBufferBinding = BufferBinding<GpuCameraParamsBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Vertex>;

    using MaterialConstantsBufferBinding = BufferBinding<GpuMaterialConstantsBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Fragment>;

    using BindGroup0 = BindGroup<WorldTransformBufferBinding,
        ClipSpaceBufferBinding,
        MeshInstanceParamsBufferBinding,
        PointLightBufferBinding,
        DirectionalLightBufferBinding,
        CameraParamsBufferBinding>;

    using BindGroup1 = BindGroup<TextureBinding<wgpu::ShaderStage::Fragment>,
        SamplerBinding<wgpu::ShaderStage::Fragment>,
        MaterialConstantsBufferBinding>;

    struct RenderAttachments
    {
        GpuRenderTarget RenderTarget;
        GpuDepthTarget DepthBuffer;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const RenderAttachments& a, const RenderAttachments& b) = default;
    };

    GpuColorPass() = delete;
    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = delete;
    GpuColorPass& operator=(GpuColorPass&&) = delete;

    Result<> SetGeometryBuffers(const GpuVertexBuffer& vertices, const GpuIndexBuffer& indices);
    Result<> SetBindGroup(const BindGroup0& bg0);
    Result<> SetRenderAttachments(const RenderAttachments& attachments);

    /// Executes the pass by rendering the meshses to the render attachments.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(std::span<MeshInstance> meshes,
        const std::span<const GpuMaterialProperties> materialProperties);

    /// Executes the pass by rendering the meshses to the render attachments.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(std::span<MeshInstance> meshes,
        const std::span<const GpuMaterialProperties> materialProperties,
        const wgpu::CommandEncoder& cmdEncoder);

    /// Creates a material bind group for the color pass.
    Result<wgpu::BindGroup> CreateMaterialBindGroup(const wgpu::Texture& texture,
        const GpuMaterialConstantsBuffer& materialConstants,
        const std::string_view name) const;

private:
    explicit GpuColorPass(const GpuHelper& gpuHelper,
        wgpu::BindGroupLayout bg0Layout,
        wgpu::BindGroupLayout bg1Layout,
        wgpu::RenderPipeline opaquePipeline,
        wgpu::RenderPipeline translucentPipeline,
        wgpu::Sampler defaultSampler);

    Result<> EnsureBindGroup0();
    Result<wgpu::RenderPassEncoder> CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<GpuVertexBuffer> m_Vertices;
    std::optional<GpuIndexBuffer> m_Indices;
    std::optional<BindGroup0> m_BindGroup0Params;
    std::optional<RenderAttachments> m_RenderAttachments;

    wgpu::BindGroupLayout m_BindGroup0Layout;
    wgpu::BindGroupLayout m_BindGroup1Layout;
    wgpu::BindGroup m_BindGroup0;
    wgpu::RenderPipeline m_OpaquePipeline;
    wgpu::RenderPipeline m_TranslucentPipeline;

    wgpu::Sampler m_DefaultSampler;
};

class GpuColorPass::CreateTask : public ICoopTask<>
{
public:
    CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuColorPass>> Take();

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