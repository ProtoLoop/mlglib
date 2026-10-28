#include "PhysicsTypes.h"

#include <box3d/Box3D.h>

/// RigidBodyIdentifier

Result<RigidBodyIdentifier>
RigidBodyIdentifier::Create(const b3BodyId bodyId)
{
    MLG_CHECKV(b3Body_IsValid(bodyId), "Node does not have a valid body id");
    return RigidBodyIdentifier(bodyId);
}

RigidBodyIdentifier::RigidBodyIdentifier(const b3BodyId bodyId)
: m_BodyIdStorage(b3StoreBodyId(bodyId))
{
}

b3BodyId RigidBodyIdentifier::GetBodyId() const
{
    return b3LoadBodyId(m_BodyIdStorage);
}

/// WorldIdentifier

Result<WorldIdentifier>
WorldIdentifier::Create(const b3WorldId worldId)
{
    MLG_CHECKV(b3World_IsValid(worldId), "Node does not have a valid world id");
    return WorldIdentifier(worldId);
}

WorldIdentifier::WorldIdentifier(const b3WorldId worldId)
: m_WorldIdStorage(b3StoreWorldId(worldId))
{
}

b3WorldId WorldIdentifier::GetWorldId() const
{
    return b3LoadWorldId(m_WorldIdStorage);
}