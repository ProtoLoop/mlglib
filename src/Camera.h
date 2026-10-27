#pragma once

#include "Projection.h"

/// Composite camera properties.
class Camera
{
public:
    Camera() = delete;

    Camera(const PerspectiveProjection& projection, const TrTransformf& wsCameraXForm)
        : m_Projection(projection),
          m_WsXform(wsCameraXForm)
    {
    }

    const PerspectiveProjection& GetProjection() const { return m_Projection; }
    void SetProjection(const PerspectiveProjection& projection) { m_Projection = projection; }

    const TrTransformf& GetWorldSpaceTransform() const { return m_WsXform; }
    void SetWorldSpaceTransform(const TrTransformf& wsXform) { m_WsXform = wsXform; }

    TrTransformf GetViewTransform() const { return m_WsXform.Inverse(); }

    /// Projection * View matrix for transforming into clip space.
    Mat44f GetViewProj() const { return GetProjection().GetMatrix() * GetViewTransform().ToMatrix(); }

private:
    PerspectiveProjection m_Projection;
    TrTransformf m_WsXform;
};