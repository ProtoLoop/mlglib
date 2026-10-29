#pragma once

#include "VecMath.h"

struct Projection
{
    constexpr static float kDefaultFovDegrees = 45.0f;
    constexpr static float kDefaultAspectRatio = 1.0f;
    constexpr static float kDefaultNearClip = 0.1f;
    constexpr static float kDefaultFarClip = 1000.0f;
    constexpr static float kDefaultHeight = 2.0f;

    /// Perspective projection settings.
    /// Fov is the vertical field of view in radians.
    /// Aspect ratio is width / height.
    /// Near and far clip define the depth range.
    struct Perspective
    {
        Radiansf Fov{ Radiansf::FromDegrees(kDefaultFovDegrees) };
        float AspectRatio{ kDefaultAspectRatio };
        float NearClip{ kDefaultNearClip };
        float FarClip{ kDefaultFarClip };
    };

    /// Orthographic projection settings.
    /// Height is the full vertical span in camera-space units.
    /// Aspect ratio is width / height.
    /// Near and far clip define the depth range.
    struct Orthographic
    {
        float Height{ kDefaultHeight };
        float AspectRatio{ kDefaultAspectRatio };
        float NearClip{ kDefaultNearClip };
        float FarClip{ kDefaultFarClip };
    };
};