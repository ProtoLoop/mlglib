#include "SceneTypes.h"

#include <box3d/Box3D.h>

/// PhysicsNode

PhysicsNode::PhysicsNode(RootNodeData& rootNodeData, const RigidBodyIdentifier rigidBodyId)
    : m_RootNodeData(&rootNodeData),
      m_RigidBodyId(rigidBodyId)
{
}

void
PhysicsNode::ApplyImpulse(const Vec3f& impulse)
{
    const b3Vec3 impulseVec //
        {
            .x = impulse.x,
            .y = impulse.y,
            .z = impulse.z,
        };
    b3Body_ApplyLinearImpulseToCenter(m_RigidBodyId.GetBodyId(), impulseVec, true);
}

void
PhysicsNode::AddForce(const Vec3f& force)
{
    const b3Vec3 forceVec //
        {
            .x = force.x,
            .y = force.y,
            .z = force.z,
        };
    b3Body_ApplyForceToCenter(m_RigidBodyId.GetBodyId(), forceVec, true);
}

Vec3f
PhysicsNode::GetPosition() const
{
    return m_RootNodeData->m_LocalTransform.T;
}

UnitQuatf
PhysicsNode::GetRotation() const
{
    return m_RootNodeData->m_LocalTransform.R;
}

Vec3f
PhysicsNode::GetLinearVelocity() const
{
    const b3Vec3 vel = b3Body_GetLinearVelocity(m_RigidBodyId.GetBodyId());
    return Vec3f{ vel.x, vel.y, vel.z };
}

void
PhysicsNode::SetLinearVelocity(const Vec3f& velocity)
{
    const b3Vec3 velVec //
        {
            .x = velocity.x,
            .y = velocity.y,
            .z = velocity.z,
        };
    b3Body_SetLinearVelocity(m_RigidBodyId.GetBodyId(), velVec);
}

Vec3f
PhysicsNode::GetAngularVelocity() const
{
    const b3Vec3 angVel = b3Body_GetAngularVelocity(m_RigidBodyId.GetBodyId());
    return Vec3f{ angVel.x, angVel.y, angVel.z };
}

void
PhysicsNode::SetAngularVelocity(const Vec3f& angularVelocity)
{
    const b3Vec3 angVelVec //
        {
            .x = angularVelocity.x,
            .y = angularVelocity.y,
            .z = angularVelocity.z,
        };
    b3Body_SetAngularVelocity(m_RigidBodyId.GetBodyId(), angVelVec);
}

float
PhysicsNode::GetInverseMass() const
{
    return b3Body_GetInverseMass(m_RigidBodyId.GetBodyId());
}

/// ModelNode

ModelNode::ModelNode(const SceneNode& node,
    const BoundingSphere& boundingSphere,
    std::span<const Mesh> meshes,
    const uint32_t firstMeshInstanceIndex)
    : m_Node(&node),
      m_LocalSpaceBoundingSphere(boundingSphere),
      m_Meshes(meshes),
      m_FirstMeshInstanceIndex(firstMeshInstanceIndex)
{
}

/// CameraNode

CameraNode::CameraNode(RootNodeData& rootNodeData, const ProjectionSettings& projectionSettings)
    : m_RootNodeData(&rootNodeData),
      m_TargetLocalTransform(rootNodeData.m_LocalTransform),
      m_ProjectionSettings(projectionSettings)
{
    MLG_ASSERT(rootNodeData.m_SceneNode, "Invalid root node data");
}