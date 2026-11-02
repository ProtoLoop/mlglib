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

    using ShadowViewProjBufferBinding = BufferBinding<GpuViewProjBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Vertex>;

    using PointLightBufferBinding = BufferBinding<GpuPointLightBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Fragment>;

    using DirectionalLightBufferBinding = BufferBinding<GpuDirectionalLightBuffer,
        wgpu::BufferBindingType::ReadOnlyStorage,
        wgpu::ShaderStage::Fragment>;

    using MaterialConstantsBufferBinding = BufferBinding<GpuMaterialConstantsBuffer,
        wgpu::BufferBindingType::Uniform,
        wgpu::ShaderStage::Fragment>;

    using ShadowMapBinding = DepthBufferBinding<wgpu::ShaderStage::Fragment>;

    using ShadowSamplerBinding = DepthSamplerBinding<wgpu::ShaderStage::Fragment>;

    using BindGroup0Helper = BindGroupHelper<WorldTransformBufferBinding,
        ClipSpaceBufferBinding,
        MeshInstanceParamsBufferBinding,
        PointLightBufferBinding,
        DirectionalLightBufferBinding,
        ShadowViewProjBufferBinding,
        ShadowMapBinding>;

    using BindGroup1Helper = BindGroupHelper<ShadowSamplerBinding>;

    using BindGroup2Helper = BindGroupHelper<TextureBinding<wgpu::ShaderStage::Fragment>,
        TextureSamplerBinding<wgpu::ShaderStage::Fragment>,
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
    Result<> SetBindGroups(const BindGroup0Helper& bg0, const BindGroup1Helper& bg1);
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
        wgpu::RenderPipeline opaquePipeline,
        wgpu::RenderPipeline translucentPipeline,
        wgpu::Sampler textureSampler);

    Result<> EnsureBindGroups();
    Result<wgpu::RenderPassEncoder> CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<GpuVertexBuffer> m_Vertices;
    std::optional<GpuIndexBuffer> m_Indices;
    std::optional<BindGroup0Helper> m_BindGroup0Helper;
    std::optional<BindGroup1Helper> m_BindGroup1Helper;
    std::optional<RenderAttachments> m_RenderAttachments;

    wgpu::BindGroup m_BindGroup0;
    wgpu::BindGroup m_BindGroup1;
    wgpu::RenderPipeline m_OpaquePipeline;
    wgpu::RenderPipeline m_TranslucentPipeline;

    wgpu::Sampler m_TextureSampler;
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