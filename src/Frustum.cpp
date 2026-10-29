#include "Frustum.h"

#include "BoundingVolumes.h"

namespace
{
Vec2f
ScreenToNdc(const Vec2f& screenPos, const uint32_t screenWidth, const uint32_t screenHeight)
{
    const float x = screenPos.x / static_cast<float>(screenWidth);
    const float y = screenPos.y / static_cast<float>(screenHeight);

    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}

Vec4f
NormalizePlane(const Vec4f& plane)
{
    return plane / plane.xyz().Length();
}
} // namespace

Result<Frustum>
Frustum::Create(const ProjectionSettings& settings)
{
    MLG_CHECKV(settings.Fov.GetValue() > 0 && settings.Fov.GetValue() < std::numbers::pi_v<float>,
        "FOV must be between 0 and 180 degrees");
    MLG_CHECKV(settings.NearClip > 0, "Near clip must be greater than 0");
    MLG_CHECKV(settings.FarClip > 0, "Far clip must be greater than 0");
    MLG_CHECKV(settings.NearClip < settings.FarClip, "Near clip must be less than far clip");
    MLG_CHECKV(settings.AspectRatio > 0, "Aspect ratio must be greater than 0");

    return Frustum(settings);
}

Result<Frustum>
Frustum::Create(const ProjectionSettings& settings,
    const Rect& selectRect,
    const uint32_t screenWidth,
    const uint32_t screenHeight)
{
    MLG_CHECKV(settings.Fov.GetValue() > 0 && settings.Fov.GetValue() < std::numbers::pi_v<float>,
        "FOV must be between 0 and 180 degrees");
    MLG_CHECKV(settings.NearClip > 0, "Near clip must be greater than 0");
    MLG_CHECKV(settings.FarClip > 0, "Far clip must be greater than 0");
    MLG_CHECKV(settings.NearClip < settings.FarClip, "Near clip must be less than far clip");
    MLG_CHECKV(settings.AspectRatio > 0, "Aspect ratio must be greater than 0");
    MLG_CHECKV(screenWidth > 0, "Screen width must be non-zero");
    MLG_CHECKV(screenHeight > 0, "Screen height must be non-zero");

    return Frustum(settings, selectRect, screenWidth, screenHeight);
}

Frustum::Frustum(const ProjectionSettings& settings)
    : m_Matrix(Mat44f::PerspectiveLH(
          settings.Fov, settings.AspectRatio, settings.NearClip, settings.FarClip)),
      m_Settings(settings)
{
    const Vec4f r0(m_Matrix[0][0], m_Matrix[1][0], m_Matrix[2][0], m_Matrix[3][0]);
    const Vec4f r1(m_Matrix[0][1], m_Matrix[1][1], m_Matrix[2][1], m_Matrix[3][1]);
    const Vec4f r2(m_Matrix[0][2], m_Matrix[1][2], m_Matrix[2][2], m_Matrix[3][2]);
    const Vec4f r3(m_Matrix[0][3], m_Matrix[1][3], m_Matrix[2][3], m_Matrix[3][3]);

    m_Planes[kLeft] = Vec4f(r3 + r0);
    m_Planes[kRight] = Vec4f(r3 - r0);
    m_Planes[kTop] = Vec4f(r3 - r1);
    m_Planes[kBottom] = Vec4f(r3 + r1);
    m_Planes[kNear] = Vec4f(r2);
    m_Planes[kFar] = Vec4f(r3 - r2);

    const float ll = Vec3f(m_Planes[kLeft].x, m_Planes[kLeft].y, m_Planes[kLeft].z).Length();
    const float lr = Vec3f(m_Planes[kRight].x, m_Planes[kRight].y, m_Planes[kRight].z).Length();
    const float lt = Vec3f(m_Planes[kTop].x, m_Planes[kTop].y, m_Planes[kTop].z).Length();
    const float lb = Vec3f(m_Planes[kBottom].x, m_Planes[kBottom].y, m_Planes[kBottom].z).Length();
    const float ln = Vec3f(m_Planes[kNear].x, m_Planes[kNear].y, m_Planes[kNear].z).Length();
    const float lf = Vec3f(m_Planes[kFar].x, m_Planes[kFar].y, m_Planes[kFar].z).Length();
    m_Planes[kLeft] /= ll;
    m_Planes[kRight] /= lr;
    m_Planes[kTop] /= lt;
    m_Planes[kBottom] /= lb;
    m_Planes[kNear] /= ln;
    m_Planes[kFar] /= lf;
}

Frustum::Frustum(const ProjectionSettings& settings,
    const Rect& selectRect,
    const uint32_t screenWidth,
    const uint32_t screenHeight)
    : m_Matrix(Mat44f::PerspectiveLH(
          settings.Fov, settings.AspectRatio, settings.NearClip, settings.FarClip)),
      m_Settings(settings)
{
    uint32_t srcW = screenWidth, scrH = screenHeight;
    if(!MLG_VERIFY(screenWidth > 0, "Screen width must be non-zero"))
    {
        srcW = 1;
    }
    if(!MLG_VERIFY(screenHeight > 0, "Screen height must be non-zero"))
    {
        scrH = 1;
    }
    // Screen space points.
    const Vec2f p00(static_cast<float>(selectRect.GetX()), static_cast<float>(selectRect.GetY()));
    const Vec2f p11 = p00
        + Vec2f(static_cast<float>(selectRect.GetWidth()),
            static_cast<float>(selectRect.GetHeight()));
    const Vec2f p01(p00.x, p11.y);
    const Vec2f p10(p11.x, p00.y);

    // Normalized device coordinates.
    const Vec2f ndc00 = ScreenToNdc(p00, srcW, scrH);
    const Vec2f ndc11 = ScreenToNdc(p11, srcW, scrH);
    const Vec2f ndc01 = ScreenToNdc(p01, srcW, scrH);
    const Vec2f ndc10 = ScreenToNdc(p10, srcW, scrH);

    // Unprojected rays in camera space.
    const float tanHalfFov = std::tan(settings.Fov.GetValue() * 0.5f);
    const float aspect = settings.AspectRatio;
    const float xScale = aspect * tanHalfFov;
    const Vec3f ray00 = Vec3f(ndc00.x * xScale, ndc00.y * tanHalfFov, 1);
    const Vec3f ray11 = Vec3f(ndc11.x * xScale, ndc11.y * tanHalfFov, 1);
    const Vec3f ray01 = Vec3f(ndc01.x * xScale, ndc01.y * tanHalfFov, 1);
    const Vec3f ray10 = Vec3f(ndc10.x * xScale, ndc10.y * tanHalfFov, 1);

    // Frustum plane normals in local space.
    const Vec3f leftNormal = ray00.Cross(ray01).Normalize();
    const Vec3f rightNormal = ray11.Cross(ray10).Normalize();
    const Vec3f topNormal = ray10.Cross(ray00).Normalize();
    const Vec3f bottomNormal = ray01.Cross(ray11).Normalize();

    // Frustum planes in local space.
    m_Planes[kLeft] = Vec4f(leftNormal, 0);
    m_Planes[kRight] = Vec4f(rightNormal, 0);
    m_Planes[kTop] = Vec4f(topNormal, 0);
    m_Planes[kBottom] = Vec4f(bottomNormal, 0);
    m_Planes[kNear] = Vec4f(0, 0, 1, -settings.NearClip);
    m_Planes[kFar] = Vec4f(0, 0, -1, settings.FarClip);
}

Frustum
Frustum::Transform(const TrTransformf& transform) const
{
    return Transform(transform.ToMatrix());
}

Frustum
Frustum::Transform(const Mat44f& transform) const
{
    const Mat44f invTranspose = transform.InverseAffine().Transpose();

    return Frustum(NormalizePlane(invTranspose * m_Planes[kLeft]),
        NormalizePlane(invTranspose * m_Planes[kRight]),
        NormalizePlane(invTranspose * m_Planes[kTop]),
        NormalizePlane(invTranspose * m_Planes[kBottom]),
        NormalizePlane(invTranspose * m_Planes[kNear]),
        NormalizePlane(invTranspose * m_Planes[kFar]),
        m_Matrix,
        m_Settings);
}

Frustum::ContainsResult
Frustum::Contains(const BoundingSphere& sphere) const
{
    const Vec4f pos4(sphere.GetCenter(), 1);
    const float radius = sphere.GetRadius();

    ContainsResult result = ContainsResult::Inside;

    for(const Vec4f& plane : m_Planes)
    {
        const float distance = plane.Dot(pos4);

        if(distance <= -radius)
        {
            return Frustum::ContainsResult::Outside;
        }

        if(distance < radius)
        {
            result = Frustum::ContainsResult::Intersects;
        }
    }

    return result;
}