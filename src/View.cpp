#define MLG_LOGGER_NAME "VIEW"

#include "View.h"

#include "Frustum.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "Scene.h"
#include "shaders/ShaderInterop.h"
#include "System.h"
#include "TextureFetcher.h"

#include <span>
#include <webgpu/webgpu_cpp.h>

namespace
{

size_t
CountMeshInstances(const std::span<const ModelNode> modelNodes)
{
    size_t count = 0;

    for(const ModelNode& node : modelNodes)
    {
        count += node.GetMeshCount();
    }

    return count;
}

/// Builds a GPU buffer containing ShaderInterop::MeshInstanceParams.
Result<GpuMeshInstanceParamsBuffer>
BuildMeshInstanceParamsBuffer(const GpuHelper& gpuHelper,
    const std::span<const ModelNode> modelNodes)
{
    const size_t meshInstanceCount = CountMeshInstances(modelNodes);
    std::vector<ShaderInterop::MeshInstanceParams> meshInstanceParams;
    meshInstanceParams.reserve(meshInstanceCount);

    uint32_t transformIndex = 0;

    for(const ModelNode& modelNode : modelNodes)
    {
        for(uint32_t i = 0; i < modelNode.GetMeshCount(); ++i)
        {
            const ShaderInterop::MeshInstanceParams mip //
                {
                    .TransformIndex = transformIndex,
                };

            meshInstanceParams.push_back(mip);
        }

        ++transformIndex;
    }

    auto buffer =
        gpuHelper.CreateTypedBuffer<GpuMeshInstanceParamsBuffer>(meshInstanceParams.size(),
            "MeshInstanceParamsBuffer");
    MLG_CHECK(buffer);

    buffer->Store(meshInstanceParams);

    return buffer;
}

/// Creates the render target that will be used as the output for GpuColorPass.
Result<GpuColorPass::RenderAttachments>
CreateColorPassAttachments(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height)
{
    MLG_DEBUG("Creating new color/depth target with size {}x{}", width, height);

    auto renderTarget = gpuHelper.CreateRenderTarget(width, height, "ColorPass::RenderTarget");
    MLG_CHECK(renderTarget, "Failed to create color render target");

    auto depthBuffer = gpuHelper.CreateDepthBuffer(width, height, "ColorPass::DepthBuffer");
    MLG_CHECK(depthBuffer, "Failed to create color depth buffer");

    return GpuColorPass::RenderAttachments //
        {
            .RenderTarget = *renderTarget,
            .DepthBuffer = *depthBuffer,
        };
}

Result<std::vector<GpuMaterialProperties>>
CreateMaterialProperties(const GpuHelper& gpuHelper,
    const GpuColorPass& gpuColorPass,
    const ResourceBundle& resourceBundle,
    const std::span<const wgpu::Texture> textures)
{
    MLG_INFO("Collecting materials...");

    const std::span materialRsrcs = resourceBundle.GetResources<MaterialResource>();
    const std::span textureRsrcs = resourceBundle.GetResources<TextureResource>();

    MLG_CHECKV(textures.size() == textureRsrcs.size(), "Texture count mismatch");

    std::vector<GpuMaterialProperties> materialProperties;
    materialProperties.reserve(materialRsrcs.size());

    for(const MaterialResource& mtlRsrc : materialRsrcs)
    {
        MLG_CHECKV(mtlRsrc.BaseTextureIndex == ResourceBundle::kInvalidIndex
                || mtlRsrc.BaseTextureIndex < textures.size(),
            "Invalid base texture index");

        wgpu::Texture baseTexture;

        std::string_view bindGroupName;

        if(mtlRsrc.BaseTextureIndex == ResourceBundle::kInvalidIndex)
        {
            baseTexture = gpuHelper.GetDefaultTexture();
            bindGroupName = "<default>";
        }
        else
        {
            const TextureResource& textureRsrc = textureRsrcs[mtlRsrc.BaseTextureIndex];
            baseTexture = textures[mtlRsrc.BaseTextureIndex];
            // Use the tecture path as the debug name for the bind group.
            bindGroupName = resourceBundle.GetStringView(textureRsrc.TexturePath);
        }

        const ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlRsrc.Color,
                .Metalness = mtlRsrc.Metalness,
                .Roughness = mtlRsrc.Roughness,
            };

        auto buffer =
            gpuHelper.CreateTypedBuffer<GpuMaterialConstantsBuffer>(1, "MaterialConstants");
        MLG_CHECK(buffer);

        buffer->Store(std::span(&mc, 1));

        auto bindGroup = gpuColorPass.CreateMaterialBindGroup(baseTexture, *buffer, bindGroupName);
        MLG_CHECK(bindGroup);

        materialProperties.emplace_back(mtlRsrc.AlphaMode, std::move(*bindGroup));
    }

    MLG_INFO("Collected materials: {}", materialProperties.size());

    return materialProperties;
}

Result<GpuPointLightBuffer>
CreatePointLightBuffer(const GpuHelper& gpuHelper,
    const std::span<const PointLightNode> pointLights)
{
    const size_t bufferSize =
        ShaderInterop::PointLightBufferHeader::CalcBufferSize(pointLights.size());

    const wgpu::BufferDescriptor desc //
        {
            .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            .size = bufferSize,
        };

    const wgpu::Buffer buffer = gpuHelper.GetDevice().CreateBuffer(&desc);
    MLG_CHECK(buffer, "Failed to create point light buffer");

    return GpuPointLightBuffer::Create(buffer);
}

Result<GpuDirectionalLightBuffer>
CreateDirectionalLightBuffer(const GpuHelper& gpuHelper,
    const std::span<const DirectionalLightNode> directionalLights)
{
    const size_t bufferSize =
        ShaderInterop::DirectionalLightBufferHeader::CalcBufferSize(directionalLights.size());

    const wgpu::BufferDescriptor desc //
        {
            .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            .size = bufferSize,
        };

    const wgpu::Buffer buffer = gpuHelper.GetDevice().CreateBuffer(&desc);
    MLG_CHECK(buffer, "Failed to create directional light buffer");

    return GpuDirectionalLightBuffer::Create(buffer);
}

Result<wgpu::Sampler>
CreateShadowSampler(const GpuHelper& gpuHelper)
{
    static constexpr wgpu::SamplerDescriptor samplerDesc //
        {
            .addressModeU = wgpu::AddressMode::ClampToEdge,
            .addressModeV = wgpu::AddressMode::ClampToEdge,
            .addressModeW = wgpu::AddressMode::ClampToEdge,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .mipmapFilter = wgpu::MipmapFilterMode::Linear,
            .compare = wgpu::CompareFunction::LessEqual,
        };

    wgpu::Sampler sampler = gpuHelper.GetDevice().CreateSampler(&samplerDesc);
    MLG_CHECK(sampler, "Failed to create default sampler");

    return sampler;
}
} // namespace

View::View(const GpuHelper& gpuHelper,
    const Scene& scene,
    std::unique_ptr<GpuColorPass>&& colorPass,
    std::unique_ptr<GpuShadowPass>&& shadowPass,
    std::unique_ptr<GpuCompositorPass>&& compositorPass,
    std::unique_ptr<GpuTransformPass>&& transformPass,
    GpuVertexBuffer&& vertexBuffer,
    GpuIndexBuffer&& indexBuffer,
    GpuWorldTransformBuffer&& worldTransformBuffer,
    GpuClipSpaceTransformBuffer&& clipSpaceBuffer,
    GpuMeshInstanceParamsBuffer&& meshInstanceParamsBuffer,
    GpuPointLightBuffer&& pointLightBuffer,
    GpuDirectionalLightBuffer&& directionalLightBuffer,
    GpuViewProjBuffer&& cameraViewProjBuffer,
    GpuViewProjBuffer&& shadowViewProjBuffer,
    wgpu::Sampler&& shadowSampler,
    std::vector<GpuMaterialProperties>&& materialProperties)
    : m_GpuHelper(&gpuHelper),
      m_Scene(&scene),
      m_ColorPass(std::move(colorPass)),
      m_ShadowPass(std::move(shadowPass)),
      m_CompositorPass(std::move(compositorPass)),
      m_TransformPass(std::move(transformPass)),
      m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_ClipSpaceBuffer(std::move(clipSpaceBuffer)),
      m_MeshInstanceParamsBuffer(std::move(meshInstanceParamsBuffer)),
      m_PointLightBuffer(std::move(pointLightBuffer)),
      m_DirectionalLightBuffer(std::move(directionalLightBuffer)),
      m_CameraViewProjBuffer(std::move(cameraViewProjBuffer)),
      m_ShadowViewProjBuffer(std::move(shadowViewProjBuffer)),
      m_ShadowSampler(std::move(shadowSampler)),
      m_MaterialProperties(std::move(materialProperties)),
      m_TotalMeshInstanceCount(CountMeshInstances(scene.GetAllModelNodes()))
{
    m_CulledMeshes.reserve(m_TotalMeshInstanceCount);
}

View::~View() = default;

Result<std::unique_ptr<View::CreateTask>>
View::Create(System& system,
    const DirectoryPath& parentPath,
    const ResourceBundle& resourceBundle,
    const Scene& scene)
{
    std::unique_ptr<CreateTask> task(new CreateTask(system, parentPath, resourceBundle, scene));
    MLG_CHECK(task->Start(), "Failed to start view creation task");
    return task;
}

Result<>
View::Render(const Dimension2d& dstExtents)
{
    MLG_SCOPED_TIMER("View.Render");

    MLG_CHECKV(dstExtents.GetWidth() > 0 && dstExtents.GetHeight() > 0,
        "Invalid destination extents: {},{}",
        dstExtents.GetWidth(),
        dstExtents.GetHeight());

    MLG_CHECK(SyncToGpu());

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    const CameraNode& cameraNode = m_Scene->GetAllCameraNodes().front();
    const float aspectRatio = static_cast<float>(dstExtents.GetWidth()) / static_cast<float>(dstExtents.GetHeight());
    auto frustumResult = cameraNode.CreateFrustum(aspectRatio);
    MLG_CHECK(frustumResult);
    const Frustum frustum = *frustumResult;

    const TrTransformf cameraWsXForm = cameraNode.GetWsTransform();

    MLG_CHECK(RenderShadowPass(cameraWsXForm,frustum, cmdEncoder));

    MLG_CHECK(TransformNodes(cmdEncoder, cameraWsXForm, frustum, m_CameraViewProjBuffer));

    if(!m_RenderAttachments
        || m_RenderAttachments->RenderTarget->GetWidth() != dstExtents.GetWidth()
        || m_RenderAttachments->RenderTarget->GetHeight() != dstExtents.GetHeight())
    {
        auto colorPassAttachments =
            CreateColorPassAttachments(*m_GpuHelper, dstExtents.GetWidth(), dstExtents.GetHeight());
        MLG_CHECK(colorPassAttachments);

        m_RenderAttachments = std::move(*colorPassAttachments);
    }

    const auto bg0Result = GpuColorPass::BindGroup0Helper::Create(m_WorldTransformBuffer,
        m_ClipSpaceBuffer,
        m_MeshInstanceParamsBuffer,
        m_PointLightBuffer,
        m_DirectionalLightBuffer,
        m_ShadowViewProjBuffer,
        m_ShadowPass->GetDepthTarget().GetTexture());
    MLG_CHECK(bg0Result, "Failed to create bind group 0 for color pass");

    const auto bg1Result = GpuColorPass::BindGroup1Helper::Create(m_ShadowSampler);
    MLG_CHECK(bg1Result, "Failed to create bind group 1 for color pass");

    MLG_CHECK(m_ColorPass->SetGeometryBuffers(m_VertexBuffer, m_IndexBuffer));
    MLG_CHECK(m_ColorPass->SetBindGroups(*bg0Result, *bg1Result));
    MLG_CHECK(m_ColorPass->SetRenderAttachments(*m_RenderAttachments));

    CullMeshes(cameraWsXForm, frustum);

    MLG_CHECK(m_ColorPass->Execute(m_CulledMeshes, m_MaterialProperties, cmdEncoder));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
View::Composite(const GpuRenderTarget& target)
{
    const Rect dstRect(
        { .X = 0, .Y = 0, .Width = target->GetWidth(), .Height = target->GetHeight() });

    return Composite(target, dstRect);
}

Result<>
View::Composite(const GpuRenderTarget& target, const Rect& dstRect)
{
    MLG_CHECKV(m_RenderAttachments, "Color pass attachments are not valid");

    MLG_CHECK(m_CompositorPass->SetSourceRenderTarget(m_RenderAttachments->RenderTarget));

    return m_CompositorPass->Execute(target, dstRect);
}

// private:

void
View::CullMeshes(const TrTransformf& wsCameraXForm, const Frustum& frustum)
{
    static PerfCounter pcTotalMeshes({ .Name = "View.Meshes.Total" });
    static PerfCounter pcVisibleMeshes({ .Name = "View.Meshes.Visible" });

    m_CulledMeshes.clear();

    const Vec3f cameraPos = wsCameraXForm.T;
    const Vec3f cameraForward = wsCameraXForm.R * Vec3f::ZAXIS();
    const Frustum wsFrustum = frustum.Transform(wsCameraXForm);

    for(const ModelNode& modelNode : m_Scene->GetAllModelNodes())
    {
        const Mat44f& wsMatrix = modelNode.GetWsMatrix();
        const BoundingSphere wsModelBounds = wsMatrix * modelNode.GetLocalSpaceBoundingSphere();
        const Frustum::ContainsResult containsResult = wsFrustum.Contains(wsModelBounds);

        if(containsResult == Frustum::ContainsResult::Outside)
        {
            continue;
        }

        uint32_t instanceIndex = modelNode.GetFirstMeshInstanceIndex();

        if(containsResult == Frustum::ContainsResult::Inside)
        {
            for(const Mesh& mesh : modelNode.GetMeshes())
            {
                const BoundingSphere wsMeshBounds = wsMatrix * mesh.GetBoundingSphere();

                const uint32_t matIndex = mesh.GetMaterialIndex();
                MLG_ASSERT(matIndex < m_MaterialProperties.size(), "Material index out of bounds");

                const AlphaMode alphaMode = m_MaterialProperties[matIndex].GetAlphaMode();

                // Camera space depth of the mesh's bounding sphere center.
                // Used for depth sorting.
                const float depth = cameraForward.Dot(wsMeshBounds.GetCenter() - cameraPos);

                m_CulledMeshes.emplace_back(mesh, instanceIndex++, alphaMode, depth);
            }
        }
        else
        {
            for(const Mesh& mesh : modelNode.GetMeshes())
            {
                ++instanceIndex;
                const BoundingSphere wsMeshBounds = wsMatrix * mesh.GetBoundingSphere();
                if(wsFrustum.Contains(wsMeshBounds) == Frustum::ContainsResult::Outside)
                {
                    continue;
                }

                const uint32_t matIndex = mesh.GetMaterialIndex();
                MLG_ASSERT(matIndex < m_MaterialProperties.size(), "Material index out of bounds");

                const AlphaMode alphaMode = m_MaterialProperties[matIndex].GetAlphaMode();

                // Camera space depth of the mesh's bounding sphere center.
                // Used for depth sorting.
                const float depth = cameraForward.Dot(wsMeshBounds.GetCenter() - cameraPos);

                m_CulledMeshes.emplace_back(mesh, instanceIndex - 1, alphaMode, depth);
            }
        }
    }

    pcTotalMeshes.Increment(m_TotalMeshInstanceCount);
    pcVisibleMeshes.Increment(m_CulledMeshes.size());
}

Result<>
View::SyncToGpu()
{
    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    // Brute force copy everything for now.
    uint64_t bufferOffset = 0;
    for(const ModelNode& modelNode : m_Scene->GetAllModelNodes())
    {
        const ShaderInterop::WorldTransform transform{ .Transform = modelNode.GetWsMatrix() };
        gpuDevice.GetQueue().WriteBuffer(m_WorldTransformBuffer.GetBuffer(),
            bufferOffset,
            &transform,
            sizeof(transform));

        bufferOffset += sizeof(transform);
    }

    const std::span directionalLights = m_Scene->GetAllDirectionalLights();

    const ShaderInterop::DirectionalLightBufferHeader header //
        {
            .Count = static_cast<std::uint32_t>(directionalLights.size()),
        };

    gpuDevice.GetQueue().WriteBuffer(m_DirectionalLightBuffer.GetBuffer(),
        0,
        &header,
        sizeof(header));

    bufferOffset = sizeof(ShaderInterop::DirectionalLightBufferHeader);

    for(const DirectionalLightNode& directionalLight : directionalLights)
    {
        const ShaderInterop::DirectionalLight shaderLight //
            {
                .Direction = directionalLight.GetDirection(),
                .Color = directionalLight.GetColor(),
            };

        gpuDevice.GetQueue().WriteBuffer(m_DirectionalLightBuffer.GetBuffer(),
            bufferOffset,
            &shaderLight,
            sizeof(shaderLight));

        bufferOffset += sizeof(shaderLight);
    }

    return Result<>::Ok;
}

Result<>
View::RenderShadowPass(const TrTransformf& cameraWsXForm,
    const Frustum& cameraFrustum,
    const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_SCOPED_TIMER("View.RenderShadowPass");

    // Only consider the first directional light for now.
    const DirectionalLightNode& lightNode = m_Scene->GetAllDirectionalLights().front();
    
    // Transform the camera frustum corners from camera spaceinto light space.

    const std::array corners = cameraFrustum.GetCorners();

    TrTransformf lightWsXForm = lightNode.GetWsTransform();

    // Compute the bounding box of the camera frustum in camera space.
    // This will be used as the stable sized height/width for the orthographic projection.
    // IOW, the dimensions of this box don't change as the camera moves so it keeps
    // the ortho proj stable.
    const BoundingBox camSpaceBbox = BoundingBox::FromVertices(corners);
    const Vec3f& csHalfExtents = camSpaceBbox.GetHalfExtents();
    const float radius = std::hypot(csHalfExtents.x, csHalfExtents.y, csHalfExtents.z);

    // Compute the matrix that transforms from camera space to light space. Only use the rotation
    // part of the light's world-space transform - translation doesn't matter for directional
    // lights.
    const Mat44f m = lightWsXForm.R.Inverse().ToMatrix() * cameraWsXForm.ToMatrix();

    // Transform the camera frustum corners into light space.
    const std::array<Vec3f, std::size(corners)> lightSpaceCorners = //
        {
            (m * Vec4f(corners[0], 1)).xyz(),
            (m * Vec4f(corners[1], 1)).xyz(),
            (m * Vec4f(corners[2], 1)).xyz(),
            (m * Vec4f(corners[3], 1)).xyz(),
            (m * Vec4f(corners[4], 1)).xyz(),
            (m * Vec4f(corners[5], 1)).xyz(),
            (m * Vec4f(corners[6], 1)).xyz(),
            (m * Vec4f(corners[7], 1)).xyz(),
        };

    // Re-compute the bounding box of the camera frustum, but this time in light space.
    // This will be used to find the depth range for the orthographic projection.
    // It will also be used to position the light's camera (lightcam) correctly.
    const BoundingBox lightSpaceBbox = BoundingBox::FromVertices(lightSpaceCorners);

    constexpr float nearClip = Projection::kDefaultNearClip;

    // Ortho projection depth range.
    const float orthoMinZ = lightSpaceBbox.GetCenter().z - lightSpaceBbox.GetHalfExtents().z;
    const float orthoMaxZ = lightSpaceBbox.GetCenter().z + lightSpaceBbox.GetHalfExtents().z;

    float orthoHeight = 2.0f * radius;

    const float kShadowBufferSize = GpuShadowPass::kShadowBufferSize;

    //  Expand the height to account for the box texel snap done below.
    orthoHeight *= kShadowBufferSize / (kShadowBufferSize - 1.0f);
    const float texelSize = orthoHeight / kShadowBufferSize;

    constexpr float kExtraDepth = 500; // DO NOT SUBMIT

    // Position the lightcam back from the front face by a distance
    // equal to the near clip.
    Vec3f position;
    position.x = lightSpaceBbox.GetCenter().x;
    position.y = lightSpaceBbox.GetCenter().y;
    position.z = orthoMinZ - nearClip - kExtraDepth;
    
    // Snap the lightcam position to the nearest texel to reduce shadow swimming.
    position.x = std::round(position.x / texelSize) * texelSize;
    position.y = std::round(position.y / texelSize) * texelSize;

    // Transform the position into world space and use it for the lightcam's translation.
    lightWsXForm.T = lightWsXForm.R * position;

    const Projection::Orthographic orthoProj//
    {
        .Height = orthoHeight,
        .AspectRatio = 1.0f,
        .NearClip = nearClip,
        .FarClip = orthoMaxZ - orthoMinZ + nearClip + kExtraDepth,
    };

    const auto frustumResult = Frustum::Create(orthoProj);
    const Frustum frustum = *frustumResult;

    MLG_CHECK(TransformNodes(cmdEncoder, lightWsXForm, frustum, m_ShadowViewProjBuffer));

    const auto bg0Result =
        GpuShadowPass::BindGroup0Helper::Create(m_ClipSpaceBuffer, m_MeshInstanceParamsBuffer);
    MLG_CHECK(bg0Result, "Failed to create bind group 0 for shadow pass");

    MLG_CHECK(m_ShadowPass->SetGeometryBuffers(m_VertexBuffer, m_IndexBuffer));
    MLG_CHECK(m_ShadowPass->SetBindGroup(*bg0Result));

    CullMeshes(lightWsXForm, frustum);

    MLG_CHECK(m_ShadowPass->Execute(m_CulledMeshes, cmdEncoder));

    return Result<>::Ok;
}

Result<>
View::TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
    const TrTransformf& wsCameraXForm,
    const Frustum& frustum,
    GpuViewProjBuffer& viewProjBuffer)
{
    const Mat44f view = wsCameraXForm.Inverse().ToMatrix();
    const Mat44f proj = frustum.GetMatrix();
    const ShaderInterop::ViewProj viewProj //
        {
            .View = view,
            .Projection = proj,
            .ViewProj = proj * view,
        };

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    gpuDevice.GetQueue().WriteBuffer(viewProjBuffer.GetBuffer(),
        0,
        &viewProj,
        sizeof(ShaderInterop::ViewProj));

    const auto bg0Result = GpuTransformPass::BindGroup0Helper::Create(m_WorldTransformBuffer,
        m_ClipSpaceBuffer);
    MLG_CHECK(bg0Result, "Failed to create bind group 0 for transform pass");

    const auto bg1Result = GpuTransformPass::BindGroup1Helper::Create(viewProjBuffer);
    MLG_CHECK(bg1Result, "Failed to create bind group 1 for transform pass");

    MLG_CHECK(m_TransformPass->SetBindGroups(*bg0Result, *bg1Result));

    return m_TransformPass->Execute(cmdEncoder);
}

// View::CreateTask
View::CreateTask::CreateTask(System& system,
    const DirectoryPath& parentPath,
    const ResourceBundle& resourceBundle,
    const Scene& scene)
    : m_System(&system),
      m_ResourceBundle(&resourceBundle),
      m_Scene(&scene),
      m_ParentPath(parentPath),
      m_ColorPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher()),
      m_ShadowPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher()),
      m_CompositorPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher()),
      m_TransformPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher())
{
}

Result<std::unique_ptr<View>>
View::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    MLG_CHECKV(m_TextureFetcher, "Texture fetcher is not valid");
    auto textures = m_TextureFetcher->Take();
    MLG_CHECK(textures, "Failed to fetch textures");

    auto gpuColorPassResult = m_ColorPassTask.Take();
    MLG_CHECK(gpuColorPassResult, "Failed to create GpuColorPass");

    auto gpuShadowPassResult = m_ShadowPassTask.Take();
    MLG_CHECK(gpuShadowPassResult, "Failed to create GpuShadowPass");

    auto gpuCompositorPassResult = m_CompositorPassTask.Take();
    MLG_CHECK(gpuCompositorPassResult, "Failed to create GpuCompositorPass");

    auto gpuTransformPassResult = m_TransformPassTask.Take();
    MLG_CHECK(gpuTransformPassResult, "Failed to create GpuTransformPass");

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    auto materialProperties =
        CreateMaterialProperties(gpuHelper, **gpuColorPassResult, *m_ResourceBundle, *textures);
    MLG_CHECK(materialProperties);

    MLG_INFO("Collecting vertices...");
    const std::span vertices = m_ResourceBundle->GetResources<Vertex>();
    auto vertexBuffer = gpuHelper.CreateTypedBuffer<GpuVertexBuffer>(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);
    vertexBuffer->Store(vertices);
    MLG_INFO("Collected vertices: {}", vertices.size());

    MLG_INFO("Collecting indices...");
    const std::span indices = m_ResourceBundle->GetResources<VertexIndex>();
    auto indexBuffer = gpuHelper.CreateTypedBuffer<GpuIndexBuffer>(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);
    indexBuffer->Store(indices);
    MLG_INFO("Collected indices: {}", indices.size());

    const std::span modelNodes = m_Scene->GetAllModelNodes();

    auto transformBuffer = gpuHelper.CreateTypedBuffer<GpuWorldTransformBuffer>(modelNodes.size(),
        "WorldTransforms");
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        gpuHelper.CreateTypedBuffer<GpuClipSpaceTransformBuffer>(modelNodes.size(), "ClipSpaceTransforms");
    MLG_CHECK(clipSpaceBuffer);

    auto meshInstanceParamsBuffer = BuildMeshInstanceParamsBuffer(gpuHelper, modelNodes);
    MLG_CHECK(meshInstanceParamsBuffer);

    const std::span pointLights = m_Scene->GetAllPointLights();
    auto pointLightBuffer = CreatePointLightBuffer(gpuHelper, pointLights);
    MLG_CHECK(pointLightBuffer);

    const std::span directionalLights = m_Scene->GetAllDirectionalLights();
    auto directionalLightBuffer = CreateDirectionalLightBuffer(gpuHelper, directionalLights);
    MLG_CHECK(directionalLightBuffer);

    auto cameraViewProjBuf = gpuHelper.CreateTypedBuffer<GpuViewProjBuffer>(1, "CameraViewProj");
    MLG_CHECK(cameraViewProjBuf);

    auto shadowViewProjBuf = gpuHelper.CreateTypedBuffer<GpuViewProjBuffer>(1, "ShadowViewProj");
    MLG_CHECK(shadowViewProjBuf);

    auto shadowSamplerResult = CreateShadowSampler(gpuHelper);
    MLG_CHECK(shadowSamplerResult, "Failed to create shadow sampler");

    std::unique_ptr<View> view(new View(gpuHelper,
        *m_Scene,
        std::move(*gpuColorPassResult),
        std::move(*gpuShadowPassResult),
        std::move(*gpuCompositorPassResult),
        std::move(*gpuTransformPassResult),
        std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(*transformBuffer),
        std::move(*clipSpaceBuffer),
        std::move(*meshInstanceParamsBuffer),
        std::move(*pointLightBuffer),
        std::move(*directionalLightBuffer),
        std::move(*cameraViewProjBuf),
        std::move(*shadowViewProjBuf),
        std::move(*shadowSamplerResult),
        std::move(*materialProperties)));

    MLG_CHECK(view->SyncToGpu());

    return view;
}

// private:

Result<>
View::CreateTask::OnStart()
{
    m_Stage = Stage::Failed;

    m_TextureFetcher.emplace(m_System->GetGpuHelper(),
        m_System->GetFileFetcher(),
        m_System->GetThreadPool(),
        m_ParentPath,
        *m_ResourceBundle);

    // If one task fails to start, it will not be added to the task batch.
    // But we still have to let the tasks that did start run to completion.    

    constexpr size_t kNumTasks = 5;
    
    InplaceVector<ICoopTask<>*, kNumTasks> tasks;

    if(!m_TextureFetcher->Start())
    {
        MLG_ERROR("Failed to start texture fetcher");
    }
    else
    {
        tasks.push_back(&m_TextureFetcher.value());
    }

    if(!m_ColorPassTask.Start())
    {
        MLG_ERROR("Failed to start color pass task");
    }
    else
    {
        tasks.push_back(&m_ColorPassTask);
    }

    if(!m_ShadowPassTask.Start())
    {
        MLG_ERROR("Failed to start shadow pass task");
    }
    else
    {
        tasks.push_back(&m_ShadowPassTask);
    }

    if(!m_CompositorPassTask.Start())
    {
        MLG_ERROR("Failed to start compositor pass task");
    }
    else
    {
        tasks.push_back(&m_CompositorPassTask);
    }

    if(!m_TransformPassTask.Start())
    {
        MLG_ERROR("Failed to start transform pass task");
    }
    else
    {
        tasks.push_back(&m_TransformPassTask);
    }

    m_TaskBatch.emplace(tasks);

    MLG_CHECK(m_TaskBatch->Start(), "Failed to begin task batch");

    m_Stage = Stage::Running;

    return Result<>::Ok;
}
void
View::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Running:
            if(!MLG_VERIFY(m_TaskBatch, "Task batch is not valid"))
            {
                m_Stage = Stage::Failed;
            }
            else if(m_TaskBatch->IsRunning())
            {
                m_TaskBatch->Update();
            }
            else
            {
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("View creation failed");
            [[fallthrough]];
        case Stage::Succeeded:
            SetComplete();
            break;
    }
}