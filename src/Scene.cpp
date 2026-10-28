#include "Scene.h"

#include "Defer.h"
#include "PhysicsTypes.h"
#include "ResourceBundle.h"

#include <box3d/Box3D.h>
#include <box3d/collision.h>
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
CreateRigidBody(const SceneNode& node,
    const RigidBodyResource& rigidBodyRsrc,
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

    const Vec3f& pos = node.GetLocalTransform().T;
    const Vec4f rot = node.GetLocalTransform().R.ToVector();
    bodyDef.position = b3Pos{ .x = pos.x, .y = pos.y, .z = pos.z };
    bodyDef.rotation = b3Quat //
        {
            .v = { .x = rot.x, .y = rot.y, .z = rot.z },
            .s = rot.w,
        };

    const b3BodyId bodyId = b3CreateBody(b3LoadWorldId(worldId.GetValue()), &bodyDef);
    MLG_CHECK(b3Body_IsValid(bodyId), "Failed to create body for node");

    for(const ColliderResource& colliderRsrc : colliderRsrcs)
    {
        auto shapeId = AttachShapeToBody(bodyId, Mass(rigidBodyRsrc.Mass), colliderRsrc);
        MLG_CHECK(shapeId);
    }

    return RigidBodyIdentifier{ b3StoreBodyId(bodyId) };
}

Result<BoundedVector<SceneNode>>
CollectNodes(const ResourceBundle& resourceBundle)
{
    const std::span nodeRsrcs = resourceBundle.GetNodes();

    BoundedVector<SceneNode> nodes(nodeRsrcs.size());

    for(const auto& nodeRsrc : nodeRsrcs)
    {
        const SceneNode* parent = nullptr;
        if(nodeRsrc.ParentIndex != ResourceBundle::kInvalidIndex)
        {
            parent = &nodes[nodeRsrc.ParentIndex];
        }

        const std::string_view name = resourceBundle.GetStringView(nodeRsrc.Name);

        TrsTransformf transform;
        transform.T = nodeRsrc.LocalPos;
        transform.R = UnitQuatf(nodeRsrc.LocalRot);
        transform.S = nodeRsrc.LocalScale;

        nodes.emplace_back(SceneNode::NameType(name), transform, parent);
    }

    return nodes;
}

Result<BoundedVector<Mesh>>
CollectMeshes(const ResourceBundle& resourceBundle)
{
    const std::span<const MeshResource> meshRsrcs = resourceBundle.GetMeshes();
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

    return meshes;
}

Result<BoundedVector<ModelNode>>
CollectModelNodes(const ResourceBundle& resourceBundle,
    const std::span<const Mesh>& meshes,
    const std::span<const SceneNode>& nodes)
{
    constexpr size_t kMaxMeshInstances =
        std::numeric_limits<uint32_t>::max() > std::vector<MeshInstance>().max_size()
        ? std::vector<MeshInstance>().max_size()
        : static_cast<size_t>(std::numeric_limits<uint32_t>::max());

    const std::span modelRsrcs = resourceBundle.GetModels();
    const std::span modelInstanceRsrcs = resourceBundle.GetModelInstances();

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
        MLG_CHECKV(kMaxMeshInstances - meshInstanceIndex >= modelRsrc.MeshCount, "Too many mesh instances");

        meshInstanceIndex += modelRsrc.MeshCount;
    }

    return modelNodes;
}

Result<BoundedVector<PhysicsNode>>
CollectPhysicsNodes(const WorldIdentifier worldId,
    const ResourceBundle& resourceBundle,
    const std::span<const SceneNode>& nodes)
{
    const std::span rigidBodies = resourceBundle.GetRigidBodies();
    BoundedVector<PhysicsNode> physicsNodes(rigidBodies.size());

    for(const RigidBodyResource& rigidBody : rigidBodies)
    {
        const SceneNode& sceneNode = nodes[rigidBody.NodeIndex];

        const std::span colliders = resourceBundle.GetColliders(rigidBody);

        auto bodyId = CreateRigidBody(sceneNode, rigidBody, colliders, worldId);
        MLG_CHECK(bodyId, "Failed to create rigid body for node");

        physicsNodes.emplace_back(sceneNode, *bodyId);
    }

    return physicsNodes;
}

Result<BoundedVector<CameraNode>>
CollectCameraNodes(const ResourceBundle& resourceBundle, const std::span<const SceneNode>& nodes)
{
    const std::span cameras = resourceBundle.GetCameras();

    MLG_CHECKV(!cameras.empty(), "No cameras found");

    BoundedVector<CameraNode> cameraNodes(cameras.size());

    for(const CameraResource& camera : cameras)
    {
        const SceneNode& sceneNode = nodes[camera.NodeIndex];
        cameraNodes.emplace_back(sceneNode, ProjectionSettings{});
    }

    return cameraNodes;
}

b3BodyId
GetBodyId(const RigidBodyIdentifier rigidBodyId)
{
    MLG_ASSERT(rigidBodyId.IsValid(), "RigidBodyIdentifier must be valid");
    const b3BodyId bodyId = b3LoadBodyId(rigidBodyId.GetValue());
    MLG_ASSERT(b3Body_IsValid(bodyId), "Node does not have a valid body id");
    return bodyId;
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

    const WorldIdentifier worldIdentifier{ b3StoreWorldId(worldId) };

    auto sceneNodes = CollectNodes(resourceBundle);
    MLG_CHECK(sceneNodes, "Failed to collect scene nodes");

    // Populate child nodes.
    const std::span nodeSpan = std::span(*sceneNodes);

    for(const auto& [nodeRsrc, node] : std::views::zip(resourceBundle.GetNodes(), *sceneNodes))
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

    auto physicsNodes = CollectPhysicsNodes(worldIdentifier, resourceBundle, *sceneNodes);
    MLG_CHECK(physicsNodes, "Failed to collect physics nodes");

    auto cameraNodes = CollectCameraNodes(resourceBundle, *sceneNodes);
    MLG_CHECK(cameraNodes, "Failed to collect camera nodes");

    cleanup.release();

    return std::unique_ptr<Scene>(new Scene(std::move(*sceneNodes),
        std::move(*physicsNodes),
        std::move(*modelNodes),
        std::move(*cameraNodes),
        std::move(*meshes),
        worldIdentifier));
}

Scene::Scene(BoundedVector<SceneNode>&& nodes,
    BoundedVector<PhysicsNode>&& physicsNodes,
    BoundedVector<ModelNode>&& modelNodes,
    BoundedVector<CameraNode>&& cameraNodes,
    BoundedVector<Mesh>&& meshes,
    const WorldIdentifier worldId)
    : m_Nodes(std::move(nodes)),
      m_PhysicsNodes(std::move(physicsNodes)),
      m_ModelNodes(std::move(modelNodes)),
      m_CameraNodes(std::move(cameraNodes)),
      m_Meshes(std::move(meshes)),
      m_WorldId(worldId)
{
    UpdateWorldTransforms();
}

Scene::~Scene()
{
    if(m_WorldId.IsValid())
    {
        const b3WorldId worldId = b3LoadWorldId(m_WorldId.GetValue());
        MLG_ASSERT(b3World_IsValid(worldId));
        b3DestroyWorld(worldId);

        m_WorldId = {};
    }
}

Result<CameraNode*>
Scene::GetCameraNode(const std::string_view name)
{
    const SceneNode::NameType nodeName{name};

    for(CameraNode& cameraNode : m_CameraNodes)
    {
        if(cameraNode.m_Node->GetName() == nodeName)
        {
            return &cameraNode;
        }
    }
    
    MLG_ERROR("Camera node not found: {}", name);

    return Result<>::Fail;
}

Result<const CameraNode*>
Scene::GetCameraNode(const std::string_view name) const
{
    const SceneNode::NameType nodeName{name};

    for(const CameraNode& cameraNode : m_CameraNodes)
    {
        if(cameraNode.m_Node->GetName() == nodeName)
        {
            return &cameraNode;
        }
    }
    
    MLG_ERROR("Camera node not found: {}", name);

    return Result<>::Fail;
}

Result<>
Scene::SetActive(const PhysicsNode& physicsNode, bool active)
{
    MLG_CHECK(GetMutableNode(*physicsNode.m_Node), "Invalid or nonexistent node for physics node");

    const b3BodyId bodyId = GetBodyId(physicsNode.m_RigidBodyId);

    if(!active)
    {
        b3Body_Disable(bodyId);
    }
    else
    {
        b3Body_Enable(bodyId);
    }

    return Result<>::Ok;
}

void
Scene::Update(const float timeStep)
{
    constexpr int kSubStepCount = 4;
    const b3WorldId worldId = b3LoadWorldId(m_WorldId.GetValue());
    MLG_ASSERT(b3World_IsValid(worldId));
    b3World_Step(worldId, timeStep, kSubStepCount);

    // Sync to scene nodes.
    for(const PhysicsNode& physicsNode : m_PhysicsNodes)
    {
        SceneNode* node = GetMutableNode(*physicsNode.m_Node);
        if(!MLG_VERIFY(node, "Invalid or nonexistent node for physics node"))
        {
            continue;
        }

        const b3BodyId bodyId = GetBodyId(physicsNode.m_RigidBodyId);
        const b3Pos pos = b3Body_GetPosition(bodyId);
        const b3Quat rot = b3Body_GetRotation(bodyId);

        // Rigid bodies can only be attached to root nodes.
        // Updating the local transform of a root node is equivalent to updating its world
        // transform.
        node->m_LocalTransform.T = Vec3f{ pos.x, pos.y, pos.z };
        node->m_LocalTransform.R = UnitQuatf{ rot.v.x, rot.v.y, rot.v.z, rot.s };
    }

    for(const CameraNode& cameraNode : m_CameraNodes)
    {
        SceneNode* node = GetMutableNode(*cameraNode.m_Node);
        if(!MLG_VERIFY(node, "Invalid or nonexistent node for camera node"))
        {
            continue;
        }

        node->m_LocalTransform = cameraNode.GetTargetLocalTransform().ToTrsTransform();
    }

    UpdateWorldTransforms();
}

// private:

SceneNode*
Scene::GetMutableNode(const SceneNode& nodeRef)
{
    if(!MLG_VERIFY(&nodeRef >= m_Nodes.data() && &nodeRef <= &m_Nodes.back(),
           "Node is not in scene"))
    {
        return nullptr;
    }

    const ptrdiff_t offset = &nodeRef - m_Nodes.data();
    if(!MLG_VERIFY(offset >= 0, "Node index out of range"))
    {
        return nullptr;
    }

    const size_t index = static_cast<size_t>(offset);
    if(!MLG_VERIFY(index < m_Nodes.size(), "Node index out of range"))
    {
        return nullptr;
    }

    return &m_Nodes[index];
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
            node.m_WorldTransform = node.m_LocalTransform.ToMatrix();
        }
        else
        {
            node.m_WorldTransform =
                node.GetParent()->m_WorldTransform * node.m_LocalTransform.ToMatrix();
        }
    }
}