#include "SceneTypes.h"

#include <box3d/Box3D.h>

namespace
{

b3BodyId
GetBodyId(const RigidBodyIdentifier rigidBodyId)
{
    MLG_ASSERT(rigidBodyId.IsValid(), "RigidBodyIdentifier must be valid");
    const b3BodyId bodyId = b3LoadBodyId(rigidBodyId.GetValue());
    MLG_ASSERT(b3Body_IsValid(bodyId), "Node does not have a valid body id");
    return bodyId;
}

} // namespace

/// PhysicsNode

PhysicsNode::PhysicsNode(const SceneNode& node, const RigidBodyIdentifier rigidBodyId)
    : m_Node(&node),
      m_RigidBodyId(rigidBodyId)
{
    MLG_ASSERT(!node.GetParent(), "PhysicsNode must be associated with a root SceneNode");
    MLG_ASSERT(rigidBodyId.IsValid(),
        "PhysicsNode must be associated with a SceneNode that has a valid rigid body ID");
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
    const b3BodyId bodyId = GetBodyId(m_RigidBodyId);
    b3Body_ApplyLinearImpulseToCenter(bodyId, impulseVec, true);
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
    const b3BodyId bodyId = GetBodyId(m_RigidBodyId);
    b3Body_ApplyForceToCenter(bodyId, forceVec, true);
}

Vec3f
PhysicsNode::GetPosition() const
{
    return m_Node->GetLocalTransform().T;
}

UnitQuatf
PhysicsNode::GetRotation() const
{
    return m_Node->GetLocalTransform().R;
}

Vec3f
PhysicsNode::GetLinearVelocity() const
{
    return m_Node->GetLinearVelocity();
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
    const b3BodyId bodyId = GetBodyId(m_RigidBodyId);
    b3Body_SetLinearVelocity(bodyId, velVec);
}

Vec3f
PhysicsNode::GetAngularVelocity() const
{
    return m_Node->GetAngularVelocity();
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
    const b3BodyId bodyId = GetBodyId(m_RigidBodyId);
    b3Body_SetAngularVelocity(bodyId, angVelVec);
}

float
PhysicsNode::GetInverseMass() const
{
    const b3BodyId bodyId = GetBodyId(m_RigidBodyId);
    return b3Body_GetInverseMass(bodyId);
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

CameraNode::CameraNode(const SceneNode& node, const ProjectionSettings& projectionSettings)
    : m_Node(&node),
      m_TargetLocalTransform(node.GetLocalTransform().ToTrTransform()),
      m_ProjectionSettings(projectionSettings)
{
    MLG_ASSERT(!node.GetParent(), "CameraNode must be associated with a root SceneNode");
}