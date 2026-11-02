#include "BoundingVolumes.h"

namespace
{
Vec3f
ComputeMin(const Vec3f& p0, const Vec3f& p1)
{
    return //
        {
            std::min(p0.x, p1.x),
            std::min(p0.y, p1.y),
            std::min(p0.z, p1.z),
        };
}

Vec3f
ComputeMax(const Vec3f& p0, const Vec3f& p1)
{
    return //
        {
            std::max(p0.x, p1.x),
            std::max(p0.y, p1.y),
            std::max(p0.z, p1.z),
        };
}

Vec3f
ComputeCenter(const Vec3f& p0, const Vec3f& p1)
{
    const Vec3f min = ComputeMin(p0, p1);
    const Vec3f max = ComputeMax(p0, p1);
    return min + (max - min) * 0.5f;
}

Vec3f
ComputeHalfExtents(const Vec3f& p0, const Vec3f& p1)
{
    const Vec3f min = ComputeMin(p0, p1);
    const Vec3f max = ComputeMax(p0, p1);
    return (max - min) * 0.5f;
}
} // namespace

BoundingBox::BoundingBox(const Vec3f& p0, const Vec3f& p1) noexcept
    : m_Center(ComputeCenter(p0, p1)),
      m_HalfExtents(ComputeHalfExtents(p0, p1))
{
}

BoundingBox
operator+(const BoundingBox& a, const BoundingBox& b) noexcept // NOLINT(misc-use-internal-linkage)
{
    const Vec3f minA = a.GetCenter() - a.GetHalfExtents();
    const Vec3f maxA = a.GetCenter() + a.GetHalfExtents();
    const Vec3f minB = b.GetCenter() - b.GetHalfExtents();
    const Vec3f maxB = b.GetCenter() + b.GetHalfExtents();

    const Vec3f min = ComputeMin(minA, minB);
    const Vec3f max = ComputeMax(maxA, maxB);

    return BoundingBox(min, max);
}

BoundingBox
BoundingBox::FromVertices(std::span<const Vertex> vertices, std::span<const VertexIndex> indices)
{
    MLG_ABORTIF(vertices.empty(), "Cannot compute bounding box from empty vertex list");
    MLG_ABORTIF(indices.empty(), "Cannot compute bounding box from empty index list");

    const VertexIndex firstIndex = indices[0];

    MLG_ABORTIF(firstIndex >= vertices.size(), "First index out of bounds");

    Vec3f min = vertices[firstIndex].pos;
    Vec3f max = vertices[firstIndex].pos;

    if(indices.size() > 1)
    {
        for(const VertexIndex index : indices.subspan(1))
        {
            MLG_ABORTIF(index >= vertices.size(), "Index out of bounds");

            const Vec3f& pos = vertices[index].pos;
            min.x = std::min(min.x, pos.x);
            min.y = std::min(min.y, pos.y);
            min.z = std::min(min.z, pos.z);

            max.x = std::max(max.x, pos.x);
            max.y = std::max(max.y, pos.y);
            max.z = std::max(max.z, pos.z);
        }
    }

    return BoundingBox(min, max);
}

BoundingBox
BoundingBox::FromVertices(std::span<const Vec3f> vertices)
{
    MLG_ABORTIF(vertices.empty(), "Cannot compute bounding box from empty vertex list");

    Vec3f min = vertices[0];
    Vec3f max = vertices[0];

    if(vertices.size() > 1)
    {
        for(size_t i = 1; i < vertices.size(); ++i)
        {
            const Vec3f& pos = vertices[i];
            min.x = std::min(min.x, pos.x);
            min.y = std::min(min.y, pos.y);
            min.z = std::min(min.z, pos.z);

            max.x = std::max(max.x, pos.x);
            max.y = std::max(max.y, pos.y);
            max.z = std::max(max.z, pos.z);
        }
    }

    return BoundingBox(min, max);
}

// BoundingSphere

BoundingSphere
operator+(const BoundingSphere& a, const BoundingSphere& b) noexcept
{
    const Vec3f centerOffset = b.GetCenter() - a.GetCenter();
    const float distance = centerOffset.Length();

    if(a.GetRadius() >= distance + b.GetRadius())
    {
        // Sphere A fully contains sphere B.
        return a;
    }

    if(b.GetRadius() >= distance + a.GetRadius())
    {
        // Sphere B fully contains sphere A.
        return b;
    }

    // Spheres partially overlap or are disjoint.
    const float newRadius = (distance + a.GetRadius() + b.GetRadius()) * 0.5f;
    const Vec3f newCenter =
        a.GetCenter() + (centerOffset * ((newRadius - a.GetRadius()) / distance));
    return BoundingSphere(newCenter, newRadius);
}