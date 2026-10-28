#pragma once

#include "AssertHelper.h"
#include "Result.h"
#include <cstddef>
#include <cstdint>

struct b3BodyId;
struct b3WorldId;

enum class MotionType
{
    Static,
    Kinematic,
    Dynamic
};

enum class CollisionType
{
    Block,
    Trigger
};

enum class ColliderShapeType
{
    Sphere,
    Box,
    Capsule,
};

class Mass
{
public:
    Mass() = delete;
    constexpr explicit Mass(const float value)
        : m_Value(value),
          m_InvValue(value > 0.0f ? 1.0f / m_Value : 0.0f)
    {
        MLG_ABORTIF(value <= 0.0f, "Mass must be positive");
    }

    constexpr float Value() const { return m_Value; }
    constexpr float InvValue() const { return m_InvValue; }

    friend constexpr bool operator==(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, Mass b) = default;
    friend constexpr auto operator<=>(Mass a, float b) { return a.Value() <=> b; }
    friend constexpr auto operator<=>(float a, Mass b) { return b <=> a; }

    friend constexpr Mass operator+(Mass a, Mass b) { return Mass(a.Value() + b.Value()); }

    friend constexpr Mass operator-(Mass a, Mass b) { return Mass(a.Value() - b.Value()); }

    friend constexpr Mass operator*(Mass a, float scalar) { return Mass(a.Value() * scalar); }

    friend constexpr Mass operator*(float scalar, Mass a) { return a * scalar; }

    friend constexpr Mass operator/(Mass a, float scalar) { return Mass(a.Value() / scalar); }

    // Division of mass by mass results in a dimensionless ratio.
    friend constexpr float operator/(Mass a, Mass b) { return a.Value() / b.Value(); }

    constexpr Mass operator-() const { return Mass(-Value()); }

    constexpr Mass& operator+=(Mass other)
    {
        *this = *this + other;
        return *this;
    }

    constexpr Mass& operator-=(Mass other) { return *this = *this - other; }

    constexpr Mass& operator*=(float scalar) { return *this = *this * scalar; }

    constexpr Mass& operator/=(float scalar) { return *this = *this / scalar; }

private:
    float m_Value;
    float m_InvValue; // Inverse value
};

/// A strongly-typed identifier for a Box3d RigidBody that is valid by construction.
class RigidBodyIdentifier
{
public:
    RigidBodyIdentifier() = delete;

    static Result<RigidBodyIdentifier> Create(const b3BodyId bodyId);

    b3BodyId GetBodyId() const;

private:
    explicit RigidBodyIdentifier(const b3BodyId bodyId);

    uint64_t m_BodyIdStorage;
};

/// A strongly-typed identifier for a Box3d world that is valid by construction.
class WorldIdentifier
{
public:
    WorldIdentifier() = delete;

    static Result<WorldIdentifier> Create(const b3WorldId worldId);

    b3WorldId GetWorldId() const;

private:
    explicit WorldIdentifier(const b3WorldId worldId);

    uint32_t m_WorldIdStorage;
};