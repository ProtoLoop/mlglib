#pragma once

#include "VecMath.h"

class BoundingSphere;
class Camera;

class Frustum
{
public:
    Frustum() = delete;

    /// Construct a frustum from a camera and its world transform.
    explicit Frustum(const Camera& camera);

    /// Construct a frustum from a camera, its world transform, and a selection rectangle in screen
    /// space.
    Frustum(const Camera& camera,
        const Rect& selectRect,
        const uint32_t screenWidth,
        const uint32_t screenHeight);

    enum class ContainsResult
    {
        Outside,
        Intersects,
        Inside,
    };

    /// Check if the given bounding sphere is inside, outside, or intersects the frustum.
    /// The bounding sphere must be in world space (assuming the frustum was constructed from a
    /// camera and its world transform).
    ContainsResult Contains(const BoundingSphere& sphere) const;

    const Vec4f& GetLeft() const { return m_Planes[kLeft]; }
    const Vec4f& GetRight() const { return m_Planes[kRight]; }
    const Vec4f& GetTop() const { return m_Planes[kTop]; }
    const Vec4f& GetBottom() const { return m_Planes[kBottom]; }
    const Vec4f& GetNear() const { return m_Planes[kNear]; }
    const Vec4f& GetFar() const { return m_Planes[kFar]; }

private:

    Frustum(const Vec4f& left,
        const Vec4f& right,
        const Vec4f& top,
        const Vec4f& bottom,
        const Vec4f& near,
        const Vec4f& far)
        : m_Planes{ left, right, top, bottom, near, far }
    {
    }

    constexpr static size_t kLeft = 0;
    constexpr static size_t kRight = 1;
    constexpr static size_t kTop = 2;
    constexpr static size_t kBottom = 3;
    constexpr static size_t kNear = 4;
    constexpr static size_t kFar = 5;
    constexpr static size_t kNumPlanes = 6;

    Vec4f m_Planes[kNumPlanes];
};