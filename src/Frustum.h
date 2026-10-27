#pragma once

#include "ProjectionSettings.h"
#include "Result.h"
#include "VecMath.h"

class BoundingSphere;

/// Represents a view frustum defined by a camera's projection and optionally a selection rectangle
/// in screen space. Upon construction the frustum is in local space. Call Transform() to move the
/// frustum into world space or any other desired space.
class Frustum
{
public:
    Frustum()
        : Frustum(ProjectionSettings{})
    {
    }

    static Result<Frustum> Create(const ProjectionSettings& settings);

    static Result<Frustum> Create(const ProjectionSettings& settings,
        const Rect& selectRect,
        const uint32_t screenWidth,
        const uint32_t screenHeight);

    enum class ContainsResult
    {
        Outside,
        Intersects,
        Inside,
    };

    /// Transform the frustum into the space defined by the transform.
    Frustum Transform(const TrTransformf& transform) const;

    /// Transform the frustum into the space defined by the transform matrix.
    Frustum Transform(const Mat44f& transform) const;

    const ProjectionSettings& GetSettings() const { return m_Settings; }

    Mat44f GetMatrix() const { return m_Matrix; }

    /// Check if the given bounding sphere is inside, outside, or intersects the frustum.
    /// The bounding sphere must be in the same space as the frustum.
    ContainsResult Contains(const BoundingSphere& sphere) const;

    const Vec4f& GetLeft() const { return m_Planes[kLeft]; }
    const Vec4f& GetRight() const { return m_Planes[kRight]; }
    const Vec4f& GetTop() const { return m_Planes[kTop]; }
    const Vec4f& GetBottom() const { return m_Planes[kBottom]; }
    const Vec4f& GetNear() const { return m_Planes[kNear]; }
    const Vec4f& GetFar() const { return m_Planes[kFar]; }

private:
    constexpr static size_t kLeft = 0;
    constexpr static size_t kRight = 1;
    constexpr static size_t kTop = 2;
    constexpr static size_t kBottom = 3;
    constexpr static size_t kNear = 4;
    constexpr static size_t kFar = 5;
    constexpr static size_t kNumPlanes = 6;

    /// Construct a frustum from a parameterized projection.
    explicit Frustum(const ProjectionSettings& settings);

    /// Construct a frustum from a projection and a selection rectangle in screen space.
    Frustum(const ProjectionSettings& settings,
        const Rect& selectRect,
        const uint32_t screenWidth,
        const uint32_t screenHeight);

    Frustum(const Vec4f& left,
        const Vec4f& right,
        const Vec4f& top,
        const Vec4f& bottom,
        const Vec4f& near,
        const Vec4f& far,
        const Mat44f& matrix,
        const ProjectionSettings& settings)
        : m_Planes{ left, right, top, bottom, near, far },
          m_Matrix(matrix),
          m_Settings(settings)
    {
    }

    Vec4f m_Planes[kNumPlanes]{};

    Mat44f m_Matrix;

    ProjectionSettings m_Settings;
};