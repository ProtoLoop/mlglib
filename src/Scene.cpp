#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Containers.h"
#include "Defer.h"
#include "PhysicsTypes.h"
#include "ResourceBundle.h"

#include <box3d/Box3D.h>
#include <box3d/collision.h>
#include <cstddef>
#include <ranges>

namespace
{

Result<b3ShapeId>
AttachShapeToBody(const b3BodyId bodyId, const Mass& mass, const ColliderResource& colliderRsrc)
{
    b3ShapeDef shapeDef = b3DefaultShapeDef();
    constexpr float kDefaultRestitution = 0.8f;
    shapeDef.baseMaterial.restitution = kDefaultRestitution;

    switch(colliderRsrc.CollisionType)
    {
        case CollisionType::Block:
            shapeDef.isSensor = false;
            shapeDef.enableSensorEvents = false;
            break;
        case CollisionType::Trigger:
            shapeDef.isSensor = true;
            shapeDef.enableSensorEvents = true;
            break;
        default:
            MLG_ERROR("Invalid collision type");
            return Result<>::Fail;
    }

    constexpr float pi = std::numbers::pi_v<float>;

    switch(colliderRsrc.ShapeType)
    {
        case ColliderShapeType::Sphere:
        {
            const ColliderResource::Sphere& sphereRsrc = colliderRsrc.GetSphere();
            const Vec3f center = sphereRsrc.Center;
            const b3Sphere sphere //
                {
                    .center = b3Pos{ .x = center.x, .y = center.y, .z = center.z },
                    .radius = sphereRsrc.Radius,
                };

            const float r3 = sphereRsrc.Radius * sphereRsrc.Radius * sphereRsrc.Radius;
            const float volume = (4.0f / 3.0f) * pi * r3;
            shapeDef.density = mass.Value() / volume;

            return b3CreateSphereShape(bodyId, &shapeDef, &sphere);
        }
        break;
        case ColliderShapeType::Box:
        {
            const ColliderResource::Box& boxRsrc = colliderRsrc.GetBox();
            const Vec3f halfExtents = boxRsrc.HalfExtents;
            const b3BoxHull dynamicBox = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
            const float volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
            shapeDef.density = mass.Value() / volume;

            return b3CreateHullShape(bodyId, &shapeDef, &dynamicBox.base);
        }
        break;
        case ColliderShapeType::Capsule:
        {
            const ColliderResource::Capsule& capsuleRsrc = colliderRsrc.GetCapsule();
            const float halfHeight = capsuleRsrc.HalfHeight;
            const Vec3f& center = capsuleRsrc.Center;
            const b3Capsule capsule //
                {
                    .center1 //
                    {
                        .x = center.x,
                        .y = center.y - halfHeight,
                        .z = center.z,
                    },
                    .center2 //
                    {
                        .x = center.x,
                        .y = center.y + halfHeight,
                        .z = center.z,
                    },
                    .radius = capsuleRsrc.Radius,
                };
            const float r2 = capsuleRsrc.Radius * capsuleRsrc.Radius;
            const float volume =
                ((4.0f / 3.0f) * pi * r2 * capsuleRsrc.Radius) + (4.0f * halfHeight * pi * r2);
            shapeDef.density = mass.Value() / volume;

            return b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);
        }
        break;
    }

    MLG_ERROR("Invalid bounding volume type");
    return Result<>::Fail;
}

Result<RigidBodyIdentifier>
CreateRigidBody(const RigidBodyResource& rigidBodyRsrc,
    const TrTransformf& localTransform,
    const std::span<const ColliderResource> colliderRsrcs,
    const WorldIdentifier worldId)
{
    b3BodyDef bodyDef = b3DefaultBodyDef();
    switch(rigidBodyRsrc.MotionType)
    {
        case MotionType::Static:
            bodyDef.type = b3_staticBody;
            break;
        case MotionType::Kinematic:
            bodyDef.type = b3_kinematicBody;
            break;
        case MotionType::Dynamic:
            bodyDef.type = b3_dynamicBody;
            break;
        default:
            MLG_ERROR("Invalid motion type for rigid body");
            return Result<>::Fail;
    }

    const Vec3f& pos = localTransform.T;
    const Vec4f rot = localTransform.R.ToVector();
    bodyDef.position = b3Pos{ .x = pos.x, .y = pos.y, .z = pos.z };
    bodyDef.rotation = b3Quat //
        {
            .v = { .x = rot.x, .y = rot.y, .z = rot.z },
            .s = rot.w,
        };

    const b3BodyId bodyId = b3CreateBody(worldId.GetWorldId(), &bodyDef);
    MLG_CHECK(b3Body_IsValid(bodyId), "Failed to create body for node");

    bool failed = true;
    MLG_DEFER
    {
        if(failed)
        {
            b3DestroyBody(bodyId);
        }
    };

    for(const ColliderResource& colliderRsrc : colliderRsrcs)
    {
        auto shapeId = AttachShapeToBody(bodyId, Mass(rigidBodyRsrc.Mass), colliderRsrc);
        MLG_CHECK(shapeId);
    }

    auto rigidBodyIdResult = RigidBodyIdentifier::Create(bodyId);
    MLG_CHECK(rigidBodyIdResult, "Failed to create rigid body identifier");

    // Cancel the deferred destruction of the body since creation succeeded.
    failed = false;

    return rigidBodyIdResult;
}

size_t
CalculateStringStorage(const ResourceBundle& resourceBundle)
{
    size_t totalSize = 0;
    const std::span nodeRsrcs = resourceBundle.GetResources<SceneNodeResource>();
    for(const auto& nodeRsrc : nodeRsrcs)
    {
        totalSize += resourceBundle.GetStringView(nodeRsrc.Name).size();
    }
    return totalSize;
}

/// Collects all scene nodes from the resource bundle and stores their names in the provided string
/// storage. If there are no camera nodes, a node will be added to which a camera can attach.
Result<BoundedVector<SceneNode>>
CollectNodes(const ResourceBundle& resourceBundle, BoundedVector<char>& stringStorage)
{
    MLG_INFO("Collecting scene nodes...");

    const std::span nodeRsrcs = resourceBundle.GetResources<SceneNodeResource>();

    const bool haveCameras = !resourceBundle.GetResources<CameraResource>().empty();
    const bool haveDirectionalLights =
        !resourceBundle.GetResources<DirectionalLightResource>().empty();

    size_t nodeCount = nodeRsrcs.size();
    if(!haveCameras)
    {
        MLG_INFO("No cameras found, reserving space for a default camera node.");
        // If there are no cameras, we reserve capacity for adding a default camera node.
        // The node will be added later.
        nodeCount += 1;
    }

    if(!haveDirectionalLights)
    {
        MLG_INFO(
            "No directional lights found, reserving space for a default directional light node.");
        // If there are no directional lights, we reserve capacity for adding a default directional
        // light node. The node will be added later.
        nodeCount += 1;
    }

    BoundedVector<SceneNode> nodes(nodeCount);

    for(const auto& nodeRsrc : nodeRsrcs)
    {
        const SceneNode* parent = nullptr;
        if(nodeRsrc.ParentIndex != ResourceBundle::kInvalidIndex)
        {
            parent = &nodes[nodeRsrc.ParentIndex];
        }

        std::string_view name = resourceBundle.GetStringView(nodeRsrc.Name);

        const size_t sizeofStringStorage = stringStorage.size();

        stringStorage.append_range(name);

        name = std::string_view(stringStorage.data(), stringStorage.size())
                   .substr(sizeofStringStorage);

        TrsTransformf transform;
        transform.T = nodeRsrc.LocalPos;
        transform.R = UnitQuatf(nodeRsrc.LocalRot);
        transform.S = nodeRsrc.LocalScale;

        if(parent)
        {
            nodes.emplace_back(name, transform, *parent);
        }
        else
        {
            // Root nodes do not have a parent.
            // Niether do they have scale, so use TrTransform instead of TrsTransform.
            nodes.emplace_back(name, transform.ToTrTransform());
        }
    }

    MLG_INFO("Collected scene nodes: {}", nodes.size());

    return nodes;
}

Result<BoundedVector<Mesh>>
CollectMeshes(const ResourceBundle& resourceBundle)
{
    MLG_INFO("Collecting meshes...");

    const std::span<const MeshResource> meshRsrcs = resourceBundle.GetResources<MeshResource>();
    BoundedVector<Mesh> meshes(meshRsrcs.size());

    for(const MeshResource& mesh : meshRsrcs)
    {
        const Mesh::Params params //
            {
                .IndexCount = mesh.IndexCount,
                .FirstIndex = mesh.FirstIndex,
                .BaseVertex = mesh.BaseVertex,
                .MaterialIndex = mesh.MaterialIndex,
                .BoundingSphere = BoundingSphere(mesh.BoundingBox),
            };

        meshes.emplace_back(params);
    }

    MLG_INFO("Collected meshes: {}", meshes.size());

    return meshes;
}

Result<BoundedVector<ModelNode>>
CollectModelNodes(const ResourceBundle& resourceBundle,
    const std::span<const Mesh> meshes,
    const std::span<const SceneNode> nodes)
{
    constexpr size_t kMaxMeshInstances =
        std::numeric_limits<uint32_t>::max() > std::vector<MeshInstance>().max_size()
        ? std::vector<MeshInstance>().max_size()
        : static_cast<size_t>(std::numeric_limits<uint32_t>::max());

    MLG_INFO("Collecting model nodes...");

    const std::span modelRsrcs = resourceBundle.GetResources<ModelResource>();
    const std::span modelInstanceRsrcs = resourceBundle.GetResources<ModelInstanceResource>();

    BoundedVector<ModelNode> modelNodes(modelInstanceRsrcs.size());

    uint32_t meshInstanceIndex = 0;

    for(const ModelInstanceResource& modelInstanceRsrc : modelInstanceRsrcs)
    {
        const SceneNode& sceneNode = nodes[modelInstanceRsrc.NodeIndex];

        const ModelResource& modelRsrc = modelRsrcs[modelInstanceRsrc.ModelIndex];

        const std::span modelMeshes = meshes.subspan(modelRsrc.FirstMeshIndex, modelRsrc.MeshCount);

        modelNodes.emplace_back(sceneNode,
            BoundingSphere(modelRsrc.BoundingBox),
            modelMeshes,
            meshInstanceIndex);

        MLG_CHECKV(modelRsrc.MeshCount > 0, "Model has no mesh instances");
        MLG_CHECKV(modelRsrc.MeshCount <= kMaxMeshInstances, "Too many mesh instances");
        MLG_CHECKV(kMaxMeshInstances - meshInstanceIndex >= modelRsrc.MeshCount,
            "Too many mesh instances");

        meshInstanceIndex += modelRsrc.MeshCount;
    }

    MLG_INFO("Collected model nodes: {}", modelNodes.size());

    return modelNodes;
}

Result<BoundedVector<PhysicsNode>>
CollectPhysicsNodes(const WorldIdentifier worldId,
    const ResourceBundle& resourceBundle,
    const std::span<SceneNode> nodes)
{
    MLG_INFO("Collecting physics nodes...");

    const std::span rigidBodies = resourceBundle.GetResources<RigidBodyResource>();
    BoundedVector<PhysicsNode> physicsNodes(rigidBodies.size());

    for(const RigidBodyResource& rigidBody : rigidBodies)
    {
        const SceneNode& sceneNode = nodes[rigidBody.NodeIndex];

        MLG_CHECKV(sceneNode.GetParent() == nullptr,
            "PhysicsNode must be attached to a root scene node");

        const std::span colliders = resourceBundle.GetColliders(rigidBody);

        auto bodyId = CreateRigidBody(rigidBody,
            sceneNode.GetTransform().ToTrTransform(),
            colliders,
            worldId);
        MLG_CHECK(bodyId, "Failed to create rigid body for node");

        physicsNodes.emplace_back(sceneNode, *bodyId);
    }

    MLG_INFO("Collected physics nodes: {}", physicsNodes.size());

    return physicsNodes;
}

Result<BoundedVector<DirectionalLightNode>>
CollectDirectionalLights(const ResourceBundle& resourceBundle, const std::span<SceneNode> nodes)
{
    MLG_INFO("Collecting directional light nodes...");

    const std::span lights = resourceBundle.GetResources<DirectionalLightResource>();

    BoundedVector<DirectionalLightNode> lightNodes(lights.size());

    for(const DirectionalLightResource& light : lights)
    {
        SceneNode& sceneNode = nodes[light.NodeIndex];

        lightNodes.emplace_back(sceneNode, light.Color);
    }

    MLG_INFO("Collected directional light nodes: {}", lightNodes.size());

    return lightNodes;
}

Result<BoundedVector<PointLightNode>>
CollectPointLights(const ResourceBundle& resourceBundle, const std::span<SceneNode> nodes)
{
    MLG_INFO("Collecting point light nodes...");

    const std::span lights = resourceBundle.GetResources<PointLightResource>();

    BoundedVector<PointLightNode> lightNodes(lights.size());

    for(const PointLightResource& light : lights)
    {
        SceneNode& sceneNode = nodes[light.NodeIndex];

        lightNodes.emplace_back(sceneNode, light.Color);
    }

    MLG_INFO("Collected point light nodes: {}", lightNodes.size());

    return lightNodes;
}

Result<BoundedVector<CameraNode>>
CollectCameraNodes(const ResourceBundle& resourceBundle, const std::span<SceneNode> nodes)
{
    MLG_INFO("Collecting camera nodes...");

    const std::span cameras = resourceBundle.GetResources<CameraResource>();

    BoundedVector<CameraNode> cameraNodes(cameras.size());

    for(const CameraResource& cameraRsrc : cameras)
    {
        SceneNode& sceneNode = nodes[cameraRsrc.NodeIndex];

        CameraNode& cameraNode = cameraNodes.emplace_back(sceneNode);

        switch(cameraRsrc.ProjectionType)
        {
            case ProjectionTypeResource::Perspective:
            {
                const Projection::Perspective proj //
                    { .Fov = Radiansf::FromDegrees(cameraRsrc.PerspFovYDegrees),
                        .AspectRatio = cameraRsrc.AspectRatio,
                        .NearClip = cameraRsrc.NearClip,
                        .FarClip = cameraRsrc.FarClip };

                MLG_CHECK(cameraNode.SetProjection(proj), "Failed to set camera projection");
            }
            break;
            case ProjectionTypeResource::Orthographic:
            {
                const Projection::Orthographic proj //
                    { .Height = cameraRsrc.OrthoHeight,
                        .AspectRatio = cameraRsrc.AspectRatio,
                        .NearClip = cameraRsrc.NearClip,
                        .FarClip = cameraRsrc.FarClip };
                MLG_CHECK(cameraNode.SetProjection(proj), "Failed to set camera projection");
            }
            break;
            default:
                MLG_ERROR("Unknown projection type");
                return Result<>::Fail;
                break;
        }
    }

    MLG_INFO("Collected camera nodes: {}", cameraNodes.size());

    return cameraNodes;
}

template<typename Range>
auto
FindNodeByName(Range& nodes, const std::string_view name) -> decltype(&nodes[0])
{
    using NodeType = decltype(nodes[0]);

    for(NodeType& node : nodes)
    {
        if(node.GetName() == name)
        {
            return &node;
        }
    }

    return nullptr;
}
} // namespace

Result<std::unique_ptr<Scene>>
Scene::Create(const ResourceBundle& resourceBundle)
{
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.restitutionThreshold = 0.0f;
    worldDef.gravity = b3Vec3{ .x = 0.0f, .y = 0.0f, .z = 0.0f };

    const b3WorldId worldId = b3CreateWorld(&worldDef);
    MLG_ASSERT(b3World_IsValid(worldId));

    auto cleanup = MLG_MAKE_DEFERRED
    {
        if(b3World_IsValid(worldId))
        {
            b3DestroyWorld(worldId);
        }
    };

    const auto worldIdentifierResult = WorldIdentifier::Create(worldId);
    MLG_CHECK(worldIdentifierResult, "Failed to create world identifier");

    BoundedVector<char> stringStorage(CalculateStringStorage(resourceBundle));

    auto sceneNodes = CollectNodes(resourceBundle, stringStorage);
    MLG_CHECK(sceneNodes, "Failed to collect scene nodes");

    // Populate child nodes.
    const std::span nodeSpan = std::span(*sceneNodes);

    for(const auto& [nodeRsrc, node] :
        std::views::zip(resourceBundle.GetResources<SceneNodeResource>(), *sceneNodes))
    {
        if(nodeRsrc.ChildCount > 0)
        {
            node.m_Children = nodeSpan.subspan(nodeRsrc.FirstChildIndex, nodeRsrc.ChildCount);
        }
    }

    auto meshes = CollectMeshes(resourceBundle);
    MLG_CHECK(meshes, "Failed to collect meshes");

    auto modelNodes = CollectModelNodes(resourceBundle, *meshes, *sceneNodes);
    MLG_CHECK(modelNodes, "Failed to collect model nodes");

    auto physicsNodes = CollectPhysicsNodes(*worldIdentifierResult, resourceBundle, *sceneNodes);
    MLG_CHECK(physicsNodes, "Failed to collect physics nodes");

    auto pointLightNodesResult = CollectPointLights(resourceBundle, *sceneNodes);
    MLG_CHECK(pointLightNodesResult, "Failed to collect point light nodes");

    auto directionalLightNodesResult = CollectDirectionalLights(resourceBundle, *sceneNodes);
    MLG_CHECK(directionalLightNodesResult, "Failed to collect directional light nodes");

    BoundedVector<DirectionalLightNode> directionalLightNodes =
        std::move(*directionalLightNodesResult);
    // If there are no directional light nodes, add a default directional light node.
    if(directionalLightNodes.empty())
    {
        MLG_INFO("No directional lights found, adding a default directional light node.");

        MLG_ASSERT(sceneNodes->capacity() > sceneNodes->size(),
            "Insufficient capacity to add default directional light node");

        directionalLightNodes = BoundedVector<DirectionalLightNode>(1);

        SceneNode& newNode =
            sceneNodes->emplace_back(DirectionalLightNode::kDefaultDirectionalLightName,
                TrTransformf{});
        const UnitQuatf defaultRotation = UnitQuatf::FromZDirection(Vec3f{ 1.0f, -1.0f, 1.0f });
        newNode.SetRotation(defaultRotation);

        const RgbColorf defaultColor{ 1.0f, 1.0f, 1.0f };
        directionalLightNodes.emplace_back(newNode, defaultColor);
    }

    auto cameraNodesResult = CollectCameraNodes(resourceBundle, *sceneNodes);
    MLG_CHECK(cameraNodesResult, "Failed to collect camera nodes");

    BoundedVector<CameraNode> cameraNodes = std::move(*cameraNodesResult);

    // If there are no camera nodes, add a default camera node.
    if(cameraNodes.empty())
    {
        MLG_INFO("No camera nodes found, adding a default camera node.");

        MLG_ASSERT(sceneNodes->capacity() > sceneNodes->size(),
            "Insufficient capacity to add default camera node");

        cameraNodes = BoundedVector<CameraNode>(1);

        SceneNode& newNode =
            sceneNodes->emplace_back(CameraNode::kDefaultCameraName, TrTransformf{});
        cameraNodes.emplace_back(newNode);
    }

    cleanup.release();

    return std::unique_ptr<Scene>(new Scene(std::move(*sceneNodes),
        std::move(stringStorage),
        std::move(*physicsNodes),
        std::move(*modelNodes),
        std::move(cameraNodes),
        std::move(*pointLightNodesResult),
        std::move(directionalLightNodes),
        std::move(*meshes),
        *worldIdentifierResult));
}

Scene::Scene(BoundedVector<SceneNode>&& nodes,
    BoundedVector<char>&& stringStorage,
    BoundedVector<PhysicsNode>&& physicsNodes,
    BoundedVector<ModelNode>&& modelNodes,
    BoundedVector<CameraNode>&& cameraNodes,
    BoundedVector<PointLightNode>&& pointLightNodes,
    BoundedVector<DirectionalLightNode>&& directionalLightNodes,
    BoundedVector<Mesh>&& meshes,
    const WorldIdentifier worldId)
    : m_Nodes(std::move(nodes)),
      m_StringStorage(std::move(stringStorage)),
      m_PhysicsNodes(std::move(physicsNodes)),
      m_ModelNodes(std::move(modelNodes)),
      m_CameraNodes(std::move(cameraNodes)),
      m_PointLightNodes(std::move(pointLightNodes)),
      m_DirectionalLightNodes(std::move(directionalLightNodes)),
      m_Meshes(std::move(meshes)),
      m_WorldId(worldId)
{
    UpdateWorldTransforms();
}

Scene::~Scene()
{
    b3DestroyWorld(m_WorldId.GetWorldId());
}

Result<CameraNode*>
Scene::GetCameraNode(const std::string_view name)
{
    CameraNode* node = FindNodeByName(m_CameraNodes, name);

    MLG_CHECK(node, "Failed to find camera node");

    return node;
}

Result<const CameraNode*>
Scene::GetCameraNode(const std::string_view name) const
{
    const CameraNode* node = FindNodeByName(m_CameraNodes, name);

    MLG_CHECK(node, "Failed to find camera node");

    return node;
}

Result<>
Scene::SetActive(const PhysicsNode& physicsNode, bool active)
{
    MLG_CHECK(GetMutableNode(physicsNode), "Invalid or nonexistent node for physics node");

    if(!active)
    {
        b3Body_Disable(physicsNode.m_RigidBodyId.GetBodyId());
    }
    else
    {
        b3Body_Enable(physicsNode.m_RigidBodyId.GetBodyId());
    }

    return Result<>::Ok;
}

void
Scene::Update(const float timeStep)
{
    constexpr int kSubStepCount = 4;

    b3World_Step(m_WorldId.GetWorldId(), timeStep, kSubStepCount);

    // Sync to scene nodes.
    for(const PhysicsNode& physicsNode : m_PhysicsNodes)
    {
        const b3Pos pos = b3Body_GetPosition(physicsNode.m_RigidBodyId.GetBodyId());
        const b3Quat rot = b3Body_GetRotation(physicsNode.m_RigidBodyId.GetBodyId());

        const TrTransformf localTransform //
            {
                .R = UnitQuatf{ rot.v.x, rot.v.y, rot.v.z, rot.s },
                .T = Vec3f{ pos.x, pos.y, pos.z },
            };

        SceneNode* sceneNode = GetMutableNode(*physicsNode.m_SceneNode);
        if(MLG_VERIFY(sceneNode, "Failed to get mutable scene node for physics node"))
        {
            sceneNode->m_LocalTransform = localTransform.ToTrsTransform();
        }
    }

    UpdateWorldTransforms();
}

// private:

SceneNode*
Scene::GetMutableNode(const SceneNode& node)
{
    if(!MLG_VERIFY(!m_Nodes.empty() && &node >= m_Nodes.data() && &node <= &m_Nodes.back(),
           "Node is not in scene"))
    {
        return nullptr;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<SceneNode*>(&node);
}

PhysicsNode*
Scene::GetMutableNode(const PhysicsNode& node)
{
    if(!MLG_VERIFY(!m_PhysicsNodes.empty()
               && &node >= m_PhysicsNodes.data()
               && &node <= &m_PhysicsNodes.back(),
           "Node is not in scene"))
    {
        return nullptr;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<PhysicsNode*>(&node);
}

void
Scene::UpdateWorldTransforms()
{
    // Nodes are stored breadth first, enabling a simple non-recursive traversal for updating world
    // transforms.

    for(SceneNode& node : m_Nodes)
    {
        if(!node.GetParent())
        {
            node.m_WsMatrix = node.GetLsMatrix();
        }
        else
        {
            node.m_WsMatrix = node.GetParent()->GetWsMatrix() * node.GetLsMatrix();
        }
    }
}