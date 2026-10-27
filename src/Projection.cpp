#include "Projection.h"

PerspectiveProjection&
PerspectiveProjection::SetFov(const Radiansf fov)
{
    MLG_ASSERT(fov.GetValue() > 0 && fov.GetValue() < std::numbers::pi_v<float>,
        "FOV must be between 0 and 180 degrees");

    m_Fov = fov;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
    return *this;
}

PerspectiveProjection&
PerspectiveProjection::SetNearClip(const float nearClip)
{
    MLG_ASSERT(nearClip > 0, "Near clip must be greater than 0");
    MLG_ASSERT(nearClip < m_FarClip, "Near clip must be less than far clip");

    m_NearClip = nearClip;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
    return *this;
}

PerspectiveProjection&
PerspectiveProjection::SetFarClip(const float farClip)
{
    MLG_ASSERT(farClip > 0, "Far clip must be greater than 0");
    MLG_ASSERT(farClip > m_NearClip, "Far clip must be greater than near clip");

    m_FarClip = farClip;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
    return *this;
}

PerspectiveProjection&
PerspectiveProjection::SetAspectRatio(const float aspectRatio)
{
    MLG_ASSERT(aspectRatio > 0, "Aspect ratio must be greater than 0");

    m_AspectRatio = aspectRatio;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
    return *this;
}

const Mat44f&
PerspectiveProjection::GetMatrix() const
{
    return m_Proj;
}