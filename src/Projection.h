#pragma once

#include "VecMath.h"

class PerspectiveProjection
{
public:
    PerspectiveProjection() = delete;

    explicit PerspectiveProjection(const float aspectRatio)
    {
        SetAspectRatio(aspectRatio);
    }

    PerspectiveProjection& SetFov(const Radiansf fov);

    Radiansf GetFov() const { return m_Fov; }

    PerspectiveProjection& SetNearClip(const float nearClip);

    float GetNearClip() const { return m_NearClip; }

    PerspectiveProjection& SetFarClip(const float farClip);

    float GetFarClip() const { return m_FarClip; }

    PerspectiveProjection& SetAspectRatio(const float aspectRatio);

    float GetAspectRatio() const { return m_AspectRatio; }

    const Mat44f& GetMatrix() const;

private:
    constexpr static float kDefaultFovDegrees = 45.0f;
    constexpr static float kDefaultAspectRatio = 16.0f / 9.0f;
    constexpr static float kDefaultNearClip = 0.1f;
    constexpr static float kDefaultFarClip = 1000.0f;

    Radiansf m_Fov{ Radiansf::FromDegrees(kDefaultFovDegrees) };
    float m_AspectRatio{ kDefaultAspectRatio };
    float m_NearClip{ kDefaultNearClip };
    float m_FarClip{ kDefaultFarClip };
    Mat44f m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
};