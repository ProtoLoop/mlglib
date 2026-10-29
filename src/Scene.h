#pragma once

#include "Containers.h"
#include "Result.h"
#include "SceneTypes.h"

#include <memory>
#include <span>
#include <string_view>

class ResourceBundle;

class Scene
{
public:
    static Result<std::unique_ptr<Scene>> Create(const ResourceBundle& resourceBundle);

    Scene() = delete;
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    /// Returns all nodes in the scene, in breadth-first order.
    std::span<const SceneNode> GetAllNodes() const { return m_Nodes; }

    /// Returns all physics nodes in the scene, in breadth-first order.
    std::span<const PhysicsNode> GetAllPhysicsNodes() const { return m_PhysicsNodes; }
    std::span<PhysicsNode> GetAllPhysicsNodes() { return m_PhysicsNodes; }

    /// Returns all model nodes in the scene, in breadth-first order.
    std::span<const ModelNode> GetAllModelNodes() const { return m_ModelNodes; }
    std::span<ModelNode> GetAllModelNodes() { return m_ModelNodes; }

    Result<CameraNode*> GetCameraNode(const std::string_view name);

    Result<const CameraNode*> GetCameraNode(const std::string_view name) const;

    Result<CameraNode*> GetDefaultCameraNode()
    {
        return GetCameraNode(CameraNode::kDefaultCameraName);
    }

    Result<const CameraNode*> GetDefaultCameraNode() const
    {
        return GetCameraNode(CameraNode::kDefaultCameraName);
    }

    Result<> SetActive(const PhysicsNode& physicsNode, bool active);

    void Update(const float timeStep);

private:
    Scene(BoundedVector<SceneNode>&& nodes,
        BoundedVector<char>&& stringStorage,
        BoundedVector<PhysicsNode>&& physicsNodes,
        BoundedVector<ModelNode>&& modelNodes,
        BoundedVector<CameraNode>&& cameraNodes,
        BoundedVector<Mesh>&& meshes,
        const WorldIdentifier worldId);

    /// Returns a mutable pointer to the scene node corresponding to the node reference, or
    /// nullptr if the node is not part of the scene.
    PhysicsNode* GetMutableNode(const PhysicsNode& node);

    void UpdateWorldTransforms();

    BoundedVector<SceneNode> m_Nodes;
    BoundedVector<char> m_StringStorage;
    BoundedVector<PhysicsNode> m_PhysicsNodes;
    BoundedVector<ModelNode> m_ModelNodes;
    BoundedVector<CameraNode> m_CameraNodes;
    BoundedVector<Mesh> m_Meshes;
    WorldIdentifier m_WorldId;
};