#pragma once

#include "Color.h"
#include "VecMath.h"

namespace ShaderInterop
{
constexpr size_t kSizeofVec2 = 8;
constexpr size_t kSizeofVec3 = 12;
constexpr size_t kSizeofVec4 = 16;

static_assert(std::is_floating_point_v<RgbaColorf::ValueType>,
    "RgbaColorf must use a floating-point type");
static_assert(sizeof(RgbaColorf) == kSizeofVec4,
    "RgbaColorf must be 16 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec4f::ValueType>, "Vec4f must use a floating-point type");
static_assert(sizeof(Vec4f) == kSizeofVec4,
    "Vec4f must be 16 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec3f::ValueType>, "Vec3f must use a floating-point type");
static_assert(sizeof(Vec3f) == kSizeofVec3,
    "Vec3f must be 12 bytes to ensure proper alignment in uniform/storage buffers");

static_assert(std::is_floating_point_v<Vec2f::ValueType>, "Vec2f must use a floating-point type");
static_assert(sizeof(Vec2f) == kSizeofVec2,
    "Vec2f must be 8 bytes to ensure proper alignment in uniform/storage buffers");

constexpr size_t kVec4Alignment = 16;

// These types mirror the corresponding types defined in shaders.
struct DrawIndirectParams
{
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

struct MaterialConstants
{
    /// Base color of the material.
    RgbaColorf Color;

    /// Metalness factor of the material.
    float Metalness{ 0 };

    /// Roughness factor of the material.
    float Roughness{ 0 };

    // Align to 16 bytes due to Vec4.
    float pad0{ 0 };
    float pad1{ 0 };
};

struct WorldTransform
{
    Mat44f Transform;
};

struct ClipSpaceTransform
{
    Mat44f Transform;
};

struct MeshInstanceParams
{
    uint32_t TransformIndex;
};

struct CameraParams
{
    Mat44f View;
    Mat44f Projection;
    Mat44f ViewProj;
};

struct alignas(kVec4Alignment) PointLight
{
    Vec3f Location{ 0, 0, 0 };
    // Align to 16 bytes due to Vec3.
    float pad0{ 0 };

    RgbColorf Color;

    // Align to 16 bytes due to Vec3.
    float pad1{ 0 };
};

struct PointLightBufferHeader
{
    std::uint32_t Count{ 0 };
    std::uint32_t padding[3]{}; // lights start at byte 16

    /// Calculates the required GPU buffer size for the given number of lights.
    static constexpr size_t CalcBufferSize(size_t lightCount)
    {
        constexpr size_t kSizeofHeader = sizeof(PointLightBufferHeader);
        // When there are no lights we still need a minimum sized buffer.
        // Because of the way the light buffer is declared in the shader the
        // min size includes space for at least one light.
        constexpr size_t kMinSize = kSizeofHeader + sizeof(PointLight);

        const size_t sizeofLights = lightCount * sizeof(PointLight);
        const size_t sizeofBuffer = kSizeofHeader + sizeofLights;

        return lightCount > 0 ? sizeofBuffer : kMinSize;
    }
};

struct alignas(kVec4Alignment) DirectionalLight
{
    Vec3f Direction{ 0, -1, 0 };
    // Align to 16 bytes due to Vec3.
    float pad0{ 0 };

    RgbColorf Color;

    // Align to 16 bytes due to Vec3.
    float pad1{ 0 };
};

struct DirectionalLightBufferHeader
{
    std::uint32_t Count{ 0 };
    std::uint32_t padding[3]{}; // lights start at byte 16

    /// Calculates the required GPU buffer size for the given number of lights.
    static constexpr size_t CalcBufferSize(size_t lightCount)
    {
        constexpr size_t kSizeofHeader = sizeof(DirectionalLightBufferHeader);
        // When there are no lights we still need a minimum sized buffer.
        // Because of the way the light buffer is declared in the shader the
        // min size includes space for at least one light.
        constexpr size_t kMinSize = kSizeofHeader + sizeof(DirectionalLight);

        const size_t sizeofLights = lightCount * sizeof(DirectionalLight);
        const size_t sizeofBuffer = kSizeofHeader + sizeofLights;

        return lightCount > 0 ? sizeofBuffer : kMinSize;
    }
};

// NOLINTBEGIN(readability-magic-numbers)
static_assert(sizeof(DirectionalLight) == 32);
static_assert(sizeof(PointLight) == 32);
static_assert(sizeof(PointLightBufferHeader) == 16);
static_assert(sizeof(DirectionalLightBufferHeader) == 16);
// NOLINTEND(readability-magic-numbers)

} // namespace ShaderInterop