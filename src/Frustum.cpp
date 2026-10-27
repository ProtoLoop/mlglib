#include "Frustum.h"

#include "BoundingVolumes.h"
#include "Camera.h"

namespace
{
Vec2f
ScreenToNdc(const Vec2f& screenPos, const uint32_t screenWidth, const uint32_t screenHeight)
{
    const float x = screenPos.x / static_cast<float>(screenWidth);
    const float y = screenPos.y / static_cast<float>(screenHeight);

    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}
} // namespace

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
Frustum::Frustum(const Camera& camera)
{
    const Mat44f& VP = camera.GetViewProj();
    const Vec4f r0(VP[0][0], VP[1][0], VP[2][0], VP[3][0]);
    const Vec4f r1(VP[0][1], VP[1][1], VP[2][1], VP[3][1]);
    const Vec4f r2(VP[0][2], VP[1][2], VP[2][2], VP[3][2]);
    const Vec4f r3(VP[0][3], VP[1][3], VP[2][3], VP[3][3]);

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

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
Frustum::Frustum(const Camera& camera,
    const Rect& selectRect,
    const uint32_t screenWidth,
    const uint32_t screenHeight)
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

    const PerspectiveProjection& projection = camera.GetProjection();

    // Normalized device coordinates.
    const Vec2f ndc00 = ScreenToNdc(p00, srcW, scrH);
    const Vec2f ndc11 = ScreenToNdc(p11, srcW, scrH);
    const Vec2f ndc01 = ScreenToNdc(p01, srcW, scrH);
    const Vec2f ndc10 = ScreenToNdc(p10, srcW, scrH);

    // Unprojected rays in camera space.
    const float tanHalfFov = std::tan(projection.GetFov().GetValue() * 0.5f);
    const float aspect = projection.GetAspectRatio();
    const float xScale = aspect * tanHalfFov;
    const Vec3f ray00 = Vec3f(ndc00.x * xScale, ndc00.y * tanHalfFov, 1);
    const Vec3f ray11 = Vec3f(ndc11.x * xScale, ndc11.y * tanHalfFov, 1);
    const Vec3f ray01 = Vec3f(ndc01.x * xScale, ndc01.y * tanHalfFov, 1);
    const Vec3f ray10 = Vec3f(ndc10.x * xScale, ndc10.y * tanHalfFov, 1);

    // Frustum plane normals in world space.
    const TrTransformf& wsCameraXForm = camera.GetWorldSpaceTransform();
    const Vec3f leftNormal = (wsCameraXForm.R * ray00.Cross(ray01)).Normalize();
    const Vec3f rightNormal = (wsCameraXForm.R * ray11.Cross(ray10)).Normalize();
    const Vec3f topNormal = (wsCameraXForm.R * ray10.Cross(ray00)).Normalize();
    const Vec3f bottomNormal = (wsCameraXForm.R * ray01.Cross(ray11)).Normalize();
    const Vec3f nearNormal = (wsCameraXForm.R * Vec3f(0, 0, 1)).Normalize();
    const Vec3f farNormal = -nearNormal;

    // Point on the near plane in world space.
    const Vec3f pnear = wsCameraXForm.T + (wsCameraXForm.R * Vec3f(0, 0, projection.GetNearClip()));

    // Point on the far plane in world space.
    const Vec3f pfar = wsCameraXForm.T + (wsCameraXForm.R * Vec3f(0, 0, projection.GetFarClip()));

    // Frustum planes in world space.
    m_Planes[kLeft] = Vec4f(leftNormal, -leftNormal.Dot(wsCameraXForm.T));
    m_Planes[kRight] = Vec4f(rightNormal, -rightNormal.Dot(wsCameraXForm.T));
    m_Planes[kTop] = Vec4f(topNormal, -topNormal.Dot(wsCameraXForm.T));
    m_Planes[kBottom] = Vec4f(bottomNormal, -bottomNormal.Dot(wsCameraXForm.T));
    m_Planes[kNear] = Vec4f(nearNormal, -nearNormal.Dot(pnear));
    m_Planes[kFar] = Vec4f(farNormal, -farNormal.Dot(pfar));
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