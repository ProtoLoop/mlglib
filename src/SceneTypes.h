#pragma once

#include "BoundingVolumes.h"
#include "Color.h"
#include "FilePath.h"
#include "InplaceString.h"
#include "PhysicsTypes.h"
#include "ProjectionSettings.h"
#include "VecMath.h"

#include <optional>
#include <span>
#include <vector>

/// Definitions for scene structure, including materials, meshes, models, and nodes.
/// Used to declaratively define the structure and properties of a scene.
/// For example:
/// ```
/// ModelDef modelDef
/// {
///     .Name = "MyModel",
///     .MeshDefs =
///     {
///         {
///             .Vertices = { /* ... */ },
///             .Indices = { /* ... */ },
///             .MaterialDef = { /* ... */ }
///         },
///         {
///             .Vertices = { /* ... */ },
///             .Indices = { /* ... */ },
///             .MaterialDef = { /* ... */ }
///         },
///     }
/// }
/// ```

namespace SceneDefs
{
constexpr size_t kMaxNameLen = 31;
using NameString = InplaceString<kMaxNameLen>;

using RelativePathString = RelativeFilePath::StringStorageType;

} // namespace SceneDefs

struct MaterialDef final
{
    SceneDefs::RelativePathString BaseTexturePath;
    RgbaColorf Color{ 1, 1, 1, 1 };
    AlphaMode AlphaMode{ AlphaMode::Opaque };
    float Metalness{ 0.0f };
    float Roughness{ 0.0f };
};

struct MeshDef final
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    MaterialDef MaterialDef;
};

struct ModelDef final
{
    SceneDefs::NameString Name;
    std::vector<MeshDef> MeshDefs;
};

struct ModelRef final
{
    SceneDefs::NameString Name;
};

struct BoxDef final
{
    Vec3f Center{ 0 };
    Vec3f HalfExtents{ 0 };
};

struct CapsuleDef final
{
    Vec3f Center{ 0 };
    float Radius{ 0 };
    float HalfHeight{ 0 };
};

struct SphereDef final
{
    Vec3f Center{ 0 };
    float Radius{ 0 };
};

struct ColliderShapeDef final
{
    ColliderShapeDef() = delete;

    ColliderShapeDef(const SphereDef& sphereDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Sphere),
          m_Sphere(sphereDef)
    {
    }

    ColliderShapeDef(const BoxDef& boxDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Box),
          m_Box(boxDef)
    {
    }

    ColliderShapeDef(const CapsuleDef& capsuleDef) // NOLINT(google-explicit-constructor)
        : m_Type(ColliderShapeType::Capsule),
          m_Capsule(capsuleDef)
    {
    }

    ColliderShapeType GetType() const { return m_Type; }

    const SphereDef& GetSphere() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Sphere, "ColliderShapeDef is not a Sphere");
        return m_Sphere; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const BoxDef& GetBox() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Box, "ColliderShapeDef is not a Box");
        return m_Box; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const CapsuleDef& GetCapsule() const
    {
        MLG_VERIFY(m_Type == ColliderShapeType::Capsule, "ColliderShapeDef is not a Capsule");
        return m_Capsule; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

private:
    ColliderShapeType m_Type;

    union
    {
        SphereDef m_Sphere;
        BoxDef m_Box;
        CapsuleDef m_Capsule;
    };
};

struct ColliderDef final
{
    ColliderShapeDef Shape;
    CollisionType CollisionType{ CollisionType::Block };
};

struct RigidBodyDef final
{
    Mass Mass;
    MotionType MotionType{ MotionType::Static };
    std::vector<ColliderDef> Colliders;
};

struct CameraDef final
{
};

struct ChildNodeDef final
{
    SceneDefs::NameString Name;
    TrsTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
};

struct RootNodeDef final
{
    SceneDefs::NameString Name;
    TrTransformf Transform;
    std::vector<ChildNodeDef> Children;
    std::optional<ModelRef> Model;
    std::optional<RigidBodyDef> Body;
    std::optional<CameraDef> Camera;
};

struct SceneDef final
{
    std::vector<ModelDef> ModelDefs;
    std::vector<RootNodeDef> NodeDefs;
};

/// Runtime of scene elements.
/// Unlike the declarative scene definitions above, these classes represent
/// the runtime instances of the scene elements.

class Mesh
{
public:
    Mesh() = delete;

    struct Params
    {
        uint32_t IndexCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
        uint32_t MaterialIndex;
        BoundingSphere BoundingSphere;
    };

    explicit Mesh(const Params& params)
        : m_Params(params)
    {
    }

    uint32_t GetMaterialIndex() const { return m_Params.MaterialIndex; }
    uint32_t GetIndexCount() const { return m_Params.IndexCount; }
    uint32_t GetFirstIndex() const { return m_Params.FirstIndex; }
    uint32_t GetBaseVertex() const { return m_Params.BaseVertex; }
    const BoundingSphere& GetBoundingSphere() const { return m_Params.BoundingSphere; }

private:
    Params m_Params;
};

/// Represents an instance of a mesh within the scene, including its sorting key for rendering.
class MeshInstance
{
public:
    static_assert(AlphaMode::Opaque < AlphaMode::Mask && AlphaMode::Mask < AlphaMode::Blend,
        "AlphaMode ordering must be Opaque < Mask < Blend");

    static_assert(std::to_underlying(AlphaMode::Opaque) == 0);
    static_assert(std::to_underlying(AlphaMode::Mask) == 1);
    static_assert(std::to_underlying(AlphaMode::Blend) == 2);

    // Sort Priority:
    // 1. Alpha mode (Opaque < Mask < Blend)
    // 2. Material index (to minimize state changes)
    // Encoding is:
    // [63:62] Alpha mode
    // [61:30] Material index
    // [29:0] Reserved for future use
    static constexpr uint64_t kAlphaBits = 2;
    static constexpr uint64_t kAlphaBitShift = 64 - kAlphaBits;
    static constexpr uint64_t kAlphaBitMask = (1ULL << kAlphaBits) - 1;
    static constexpr uint64_t kMaterialBits = 32;
    static constexpr uint64_t kMaterialBitShift = 64 - (kAlphaBits + kMaterialBits);
    static constexpr uint64_t kMaterialBitMask = (1ULL << kMaterialBits) - 1;

    MeshInstance() = delete;

    MeshInstance(const Mesh& mesh,
        const uint32_t instanceIndex,
        const AlphaMode alphaMode,
        const float depth)
        : m_Mesh(&mesh),
          m_InstanceIndex(instanceIndex),
          m_SortDepth(depth)
    {
        MLG_ASSERT(alphaMode == AlphaMode::Opaque
            || alphaMode == AlphaMode::Mask
            || alphaMode == AlphaMode::Blend);

        m_SortKey |= (static_cast<uint64_t>(alphaMode) & kAlphaBitMask) << kAlphaBitShift;
        m_SortKey |= (static_cast<uint64_t>(mesh.GetMaterialIndex()) & kMaterialBitMask)
            << kMaterialBitShift;
    }

    /// Returns the mesh of which this MeshInstance is an instance.
    const Mesh& GetMesh() const { return *m_Mesh; }

    /// Returns the index of this mesh instance within the scene's mesh instance collection.
    uint32_t GetInstanceIndex() const { return m_InstanceIndex; }

    /// Returns the depth used for sorting.
    /// E.g. back to front for translucent meshes, front to back for opaque meshes.
    float GetSortDepth() const { return m_SortDepth; }

    /// Returns the sort key used for rendering this mesh instance.
    uint64_t GetSortKey() const { return m_SortKey; }

    /// Returns the alpha mode of the material used by this mesh instance.
    AlphaMode GetAlphaMode() const
    {
        return static_cast<AlphaMode>((m_SortKey >> kAlphaBitShift) & kAlphaBitMask);
    }

    /// Returns the material index of the material used by this mesh instance.
    uint32_t GetMaterialIndex() const
    {
        return static_cast<uint32_t>((m_SortKey >> kMaterialBitShift) & kMaterialBitMask);
    }

private:
    const Mesh* m_Mesh;
    uint32_t m_InstanceIndex{ 0 };
    float m_SortDepth;
    uint64_t m_SortKey{ 0 };
};

class SceneNode;

struct RootNodeData
{
    SceneNode* m_SceneNode{ nullptr };
    TrTransformf m_LocalTransform;
};

struct ChildNodeData
{
    SceneNode* m_SceneNode{ nullptr };
    TrsTransformf m_LocalTransform;
    const SceneNode* m_Parent{ nullptr };
};

/// Represents a node within the scene graph, including its local and world transforms,
/// and hierarchical relationships.
/// Note that the name of the node is stored as a string view and must remain valid for the lifetime
/// of the SceneNode.
class SceneNode
{
public:
    SceneNode(
        const std::string_view name, const TrsTransformf& localTransform, const SceneNode& parent)
        : m_Name(name),
          m_NodeData(ChildNodeData //
              {
                  .m_SceneNode = this,
                  .m_LocalTransform = localTransform,
                  .m_Parent = &parent,
              })
    {
    }

    SceneNode(
        const std::string_view name, const TrTransformf& localTransform, const SceneNode& parent)
        : m_Name(name),
          m_NodeData(ChildNodeData //
              {
                  .m_SceneNode = this,
                  .m_LocalTransform = localTransform.ToTrsTransform(),
                  .m_Parent = &parent,
              })
    {
    }

    SceneNode(const std::string_view name, const TrTransformf& localTransform)
        : m_Name(name),
          m_NodeData(RootNodeData //
              {
                  .m_SceneNode = this,
                  .m_LocalTransform = localTransform,
              })
    {
    }

    SceneNode() = delete;
    ~SceneNode() = default;
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&) = delete;
    SceneNode& operator=(SceneNode&&) = delete;

    std::string_view GetName() const { return m_Name; }

    Mat44f GetLsMatrix() const
    {
        if(std::holds_alternative<RootNodeData>(m_NodeData))
        {
            return std::get<RootNodeData>(m_NodeData).m_LocalTransform.ToMatrix();
        }
        return std::get<ChildNodeData>(m_NodeData).m_LocalTransform.ToMatrix();
    }

    const Mat44f& GetWsMatrix() const { return m_WsMatrix; }

    const SceneNode* GetParent() const
    {
        if(std::holds_alternative<ChildNodeData>(m_NodeData))
        {
            return std::get<ChildNodeData>(m_NodeData).m_Parent;
        }
        return nullptr;
    }

    Result<RootNodeData*> GetRootNodeData()
    {
        MLG_CHECKV(std::holds_alternative<RootNodeData>(m_NodeData),
            "Node does not contain root node data");
        return &std::get<RootNodeData>(m_NodeData);
    }

private:
    friend class Scene;

    std::string_view m_Name;
    std::variant<RootNodeData, ChildNodeData> m_NodeData;
    Mat44f m_WsMatrix{ 1 };
    std::span<SceneNode> m_Children;
};

/// Represents a node in the scene to which physics can be applied.
/// All PhysicsNodes are associated with a RootNode.
/// PhysicsNodes can only appear at the root of the scene graph.
class PhysicsNode
{
public:
    PhysicsNode(RootNodeData& rootNodeData, const RigidBodyIdentifier rigidBodyId);

    PhysicsNode() = delete;
    ~PhysicsNode() = default;
    PhysicsNode(const PhysicsNode&) = delete;
    PhysicsNode& operator=(const PhysicsNode&) = delete;
    PhysicsNode(PhysicsNode&&) = delete;
    PhysicsNode& operator=(PhysicsNode&&) = delete;

    std::string_view GetName() const { return m_RootNodeData->m_SceneNode->GetName(); }

    void ApplyImpulse(const Vec3f& impulse);

    void AddForce(const Vec3f& force);

    Vec3f GetPosition() const;

    UnitQuatf GetRotation() const;

    Vec3f GetLinearVelocity() const;

    void SetLinearVelocity(const Vec3f& velocity);

    // Radians per second
    Vec3f GetAngularVelocity() const;

    // Radians per second
    void SetAngularVelocity(const Vec3f& angularVelocity);

    float GetInverseMass() const;

private:
    friend class Scene;

    RootNodeData* m_RootNodeData{ nullptr };
    RigidBodyIdentifier m_RigidBodyId;
};

/// Represents a renderable model within the scene, including its associated meshes and bounding
/// volume. All ModelNodes are associated with a SceneNode.
class ModelNode
{
public:
    /// Constructs a ModelNode with:
    /// - associated scene node
    /// - bounding sphere
    /// - meshes belonging to the model
    /// - index of the first mesh instance within the scene's mesh instance collection.
    ///
    /// firstMeshInstanceIndex is an index into a "virtual" collection
    /// of mesh instances.  Actual mesh instances are materialzed only after
    /// culling and just prior to rendering.
    ModelNode(const SceneNode& node,
        const BoundingSphere& boundingSphere,
        std::span<const Mesh> meshes,
        const uint32_t firstMeshInstanceIndex);

    ModelNode() = delete;
    ~ModelNode() = default;
    ModelNode(const ModelNode&) = delete;
    ModelNode& operator=(const ModelNode&) = delete;
    ModelNode(ModelNode&&) = delete;
    ModelNode& operator=(ModelNode&&) = delete;

    std::string_view GetName() const { return m_Node->GetName(); }

    const Mat44f& GetWsMatrix() const { return m_Node->GetWsMatrix(); }

    const BoundingSphere& GetLocalSpaceBoundingSphere() const { return m_LocalSpaceBoundingSphere; }

    uint32_t GetMeshCount() const { return static_cast<uint32_t>(m_Meshes.size()); }

    /// Returns the index of the first mesh instance associated with this model.
    uint32_t GetFirstMeshInstanceIndex() const { return m_FirstMeshInstanceIndex; }

    std::span<const Mesh> GetMeshes() const { return m_Meshes; }

private:
    friend class Scene;

    const SceneNode* m_Node{ nullptr };
    BoundingSphere m_LocalSpaceBoundingSphere;
    std::span<const Mesh> m_Meshes;
    // Index of the first mesh instance in the scene's mesh instance collection
    uint32_t m_FirstMeshInstanceIndex{ 0 };
};

/// Represents a camera node within the scene graph.
/// All CameraNodes are associated with a RootNode.
/// CameraNodes can only appear at the root of the scene graph.
class CameraNode
{
public:
    static constexpr std::string_view kDefaultCameraName{ "DefaultCamera" };

    CameraNode(RootNodeData& rootNodeData, const ProjectionSettings& projectionSettings);

    CameraNode() = delete;
    ~CameraNode() = default;
    CameraNode(const CameraNode&) = delete;
    CameraNode& operator=(const CameraNode&) = delete;
    CameraNode(CameraNode&&) = delete;
    CameraNode& operator=(CameraNode&&) = delete;

    std::string_view GetName() const { return m_RootNodeData->m_SceneNode->GetName(); }

    const TrTransformf& GetLocalTransform() const { return m_RootNodeData->m_LocalTransform; }

    const TrTransformf& GetTargetLocalTransform() const { return m_TargetLocalTransform; }

    void SetTargetLocalTransform(const TrTransformf& transform)
    {
        m_TargetLocalTransform = transform;
    }

    const ProjectionSettings& GetProjectionSettings() const { return m_ProjectionSettings; }

private:
    friend class Scene;

    RootNodeData* m_RootNodeData{ nullptr };
    TrTransformf m_TargetLocalTransform;
    ProjectionSettings m_ProjectionSettings;
};