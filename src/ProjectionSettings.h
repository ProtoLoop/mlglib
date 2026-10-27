#pragma once

#include "VecMath.h"

struct ProjectionSettings
{
public:
    constexpr static float kDefaultFovDegrees = 45.0f;
    constexpr static float kDefaultAspectRatio = 1.0f;
    constexpr static float kDefaultNearClip = 0.1f;
    constexpr static float kDefaultFarClip = 1000.0f;

    Radiansf Fov{ Radiansf::FromDegrees(kDefaultFovDegrees) };
    float AspectRatio{ kDefaultAspectRatio };
    float NearClip{ kDefaultNearClip };
    float FarClip{ kDefaultFarClip };
};