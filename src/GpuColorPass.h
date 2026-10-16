#pragma once

#include "Camera.h"
#include "CoopTask.h"
#include "GpuTypes.h"
#include "ShaderFetcher.h"

#include <memory>
#include <optional>

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

    struct Inputs
    {
        GpuVertexBuffer Vertices;
        GpuIndexBuffer Indices;
        GpuWorldTransformBuffer WorldTransforms;
        GpuClipSpaceBuffer ClipSpaceTransforms;
        GpuMeshInstanceParamsBuffer MeshInstanceParams;
        GpuCameraParamsBuffer CameraParams;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const Inputs& a, const Inputs& b)
        {
            return a.Vertices == b.Vertices
                && a.Indices == b.Indices
                && a.WorldTransforms == b.WorldTransforms
                && a.ClipSpaceTransforms == b.ClipSpaceTransforms
                && a.MeshInstanceParams == b.MeshInstanceParams
                && a.CameraParams == b.CameraParams;
        }
    };

    struct Outputs
    {
        GpuRenderTarget RenderTarget;
        GpuDepthTarget DepthBuffer;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& a, const Outputs& b) = default;
    };

    GpuColorPass() = delete;
    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = delete;
    GpuColorPass& operator=(GpuColorPass&&) = delete;

    Result<> SetInputs(const Inputs& inputs);
    Result<> SetOutputs(const Outputs& outputs);

    /// Executes the pass using the supplied inputs and outputs.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(const CameraView& cameraView,
        std::span<MeshInstance> meshes,
        const std::span<const GpuMaterialProperties> materialProperties);

    /// Executes the pass using the supplied inputs and outputs.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    /// Note that the meshes span is non-const to allow sorting for optimal rendering order.
    Result<> Execute(const wgpu::CommandEncoder& cmdEncoder,
        const CameraView& cameraView,
        std::span<MeshInstance> meshes,
        const std::span<const GpuMaterialProperties> materialProperties);

    /// Creates a material bind group for the color pass.
    Result<wgpu::BindGroup> CreateMaterialBindGroup(const wgpu::Texture& texture,
        const GpuMaterialConstantsBuffer& materialConstants,
        const std::string_view name) const;

private:
    static constexpr const char* ShaderPath = "shaders/ColorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";
    static constexpr float kClearDepth = 1.0f;

    explicit GpuColorPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::BindGroupLayout inputsBindGroupLayout,
        wgpu::BindGroupLayout materialBindGroupLayout,
        wgpu::PipelineLayout pipelineLayout,
        wgpu::Sampler defaultSampler)
        : m_GpuHelper(&gpuHelper),
          m_Shader(std::move(shader)),
          m_InputsBindGroupLayout(std::move(inputsBindGroupLayout)),
          m_MaterialBindGroupLayout(std::move(materialBindGroupLayout)),
          m_PipelineLayout(std::move(pipelineLayout)),
          m_DefaultSampler(std::move(defaultSampler))
    {
        MLG_ASSERT(m_Shader, "Shader module is not valid");
        MLG_ASSERT(m_InputsBindGroupLayout, "Inputs bind group layout is not valid");
        MLG_ASSERT(m_MaterialBindGroupLayout, "Material bind group layout is not valid");
        MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
        MLG_ASSERT(m_DefaultSampler, "Default sampler is not valid");
    }

    Result<> EnsurePipeline();
    Result<> EnsureInputsBindGroup();
    Result<wgpu::RenderPassEncoder> CreateRenderPassEncoder(const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_InputsBindGroupLayout;
    wgpu::BindGroupLayout m_MaterialBindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_TranslucentPipeline;
    wgpu::RenderPipeline m_OpaquePipeline;

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