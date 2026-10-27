#pragma once

#include "CoopTask.h"
#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "GpuTypes.h"
#include "SceneTypes.h"
#include "TextureFetcher.h"

#include <memory>
#include <optional>
#include <vector>

class Camera;
class ResourceBundle;
class System;
class ThreadPool;
class Scene;

/// A view of the scene.
/// Responsible for rendering and compositing the 3D scene.
class View
{
public:
    View() = delete;
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) = delete;
    View& operator=(View&&) = delete;

    class CreateTask;

    static Result<std::unique_ptr<CreateTask>> Create(System& system,
        const DirectoryPath& parentPath,
        const ResourceBundle& resourceBundle,
        const Scene& scene);

    /// Renders the scene from the perspective of the camera into the specified extents.
    /// The render target will be resized if necessary to accommodate the specified extents.
    Result<> Render(const Camera& camera, const Dimension2& dstExtents);

    Result<> Composite(const GpuRenderTarget& target);

    Result<> Composite(const GpuRenderTarget& target, const Rect& dstRect);

private:
    View(const GpuHelper& gpuHelper,
        const Scene& scene,
        std::unique_ptr<GpuColorPass>&& colorPass,
        std::unique_ptr<GpuCompositorPass>&& compositorPass,
        std::unique_ptr<GpuTransformPass>&& transformPass,
        GpuVertexBuffer&& vertexBuffer,
        GpuIndexBuffer&& indexBuffer,
        GpuWorldTransformBuffer&& worldTransformBuffer,
        GpuClipSpaceBuffer&& clipSpaceBuffer,
        GpuMeshInstanceParamsBuffer&& meshInstanceParamsBuffer,
        GpuCameraParamsBuffer&& cameraParamsBuffer,
        std::vector<GpuMaterialProperties>&& materialProperties);

    /// Collects all camera-visible meshes.
    void CullMeshes(const Camera& camera);

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu();

    /// Calls a GPU compute shader to transform the scene nodes into camera space.
    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const Camera& camera);

    const GpuHelper* m_GpuHelper{ nullptr };

    const Scene* m_Scene{ nullptr };

    std::optional<GpuColorPass::Outputs> m_ColorPassOutputs;
    std::unique_ptr<GpuColorPass> m_ColorPass;
    std::unique_ptr<GpuCompositorPass> m_CompositorPass;
    std::unique_ptr<GpuTransformPass> m_TransformPass;

    GpuVertexBuffer m_VertexBuffer;
    GpuIndexBuffer m_IndexBuffer;
    GpuWorldTransformBuffer m_WorldTransformBuffer;
    GpuClipSpaceBuffer m_ClipSpaceBuffer;
    GpuMeshInstanceParamsBuffer m_MeshInstanceParamsBuffer;
    GpuCameraParamsBuffer m_CameraParamsBuffer;

    std::vector<GpuMaterialProperties> m_MaterialProperties;

    /// Scratch space for storing culled meshes each frame.
    std::vector<MeshInstance> m_CulledMeshes;

    /// The number of mesh instances in the scene.
    size_t m_TotalMeshInstanceCount{ 0 };
};

class View::CreateTask : public ICoopTask<>
{
public:

    CreateTask() = delete;
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<View>> Take();

private:
    friend class View;
    
    enum class Stage
    {
        None,
        Running,
        Succeeded,
        Failed
    };

    CreateTask(System& system,
        const DirectoryPath& parentPath,
        const ResourceBundle& resourceBundle,
        const Scene& scene);

    Result<> OnStart() override;

    void OnUpdate() override;

    System* m_System{ nullptr };
    const ResourceBundle* m_ResourceBundle{ nullptr };
    const Scene* m_Scene{ nullptr };
    DirectoryPath m_ParentPath;

    std::optional<TextureFetcher> m_TextureFetcher;
    GpuColorPass::CreateTask m_ColorPassTask;
    GpuCompositorPass::CreateTask m_CompositorPassTask;
    GpuTransformPass::CreateTask m_TransformPassTask;
    std::optional<CoopTaskBatch> m_TaskBatch;
    bool m_Consumed{ false };

    Stage m_Stage{ Stage::None };
};