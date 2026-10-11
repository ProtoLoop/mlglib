#include "ShapeDefs.h"
#include "AssertHelper.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

struct TriangleUvs
{
    UV2 Corners[3];
};
}

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

MeshDef
ShapeDefs::Box(const BoxParams& params)
{
    MLG_ASSERT(params.Width > 0);
    MLG_ASSERT(params.Height > 0);
    MLG_ASSERT(params.Depth > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    vertices.reserve(24);
    indices.reserve(36);

    const float hw = params.Width * 0.5f;
    const float hh = params.Height * 0.5f;
    const float hd = params.Depth * 0.5f;

    const VertexPos backBottomLeft{ -hw, -hh, -hd };
    const VertexPos backBottomRight{ hw, -hh, -hd };
    const VertexPos backTopRight{ hw, hh, -hd };
    const VertexPos backTopLeft{ -hw, hh, -hd };
    const VertexPos frontBottomLeft{ -hw, -hh, hd };
    const VertexPos frontBottomRight{ hw, -hh, hd };
    const VertexPos frontTopRight{ hw, hh, hd };
    const VertexPos frontTopLeft{ -hw, hh, hd };

    // A corner needs a separate vertex on each face to keep its normal and UVs flat.
    auto addFace = [&](const VertexPos& a, const VertexPos& b, const VertexPos& c,
                       const VertexPos& d, VertexNormal normal) {
        const VertexIndex start = static_cast<VertexIndex>(vertices.size());
        vertices.push_back(Vertex{ .pos = a, .normal = normal,
            .uvs{ UV2{ .u = 0.0f, .v = 0.0f } } });
        vertices.push_back(Vertex{ .pos = b, .normal = normal,
            .uvs{ UV2{ .u = 1.0f, .v = 0.0f } } });
        vertices.push_back(Vertex{ .pos = c, .normal = normal,
            .uvs{ UV2{ .u = 1.0f, .v = 1.0f } } });
        vertices.push_back(Vertex{ .pos = d, .normal = normal,
            .uvs{ UV2{ .u = 0.0f, .v = 1.0f } } });
        indices.push_back(start); indices.push_back(start + 1); indices.push_back(start + 2);
        indices.push_back(start); indices.push_back(start + 2); indices.push_back(start + 3);
    };

    addFace(frontBottomLeft, frontBottomRight, frontTopRight, frontTopLeft,
        VertexNormal{ 0.0f, 0.0f, 1.0f });
    addFace(backBottomRight, backBottomLeft, backTopLeft, backTopRight,
        VertexNormal{ 0.0f, 0.0f, -1.0f });
    addFace(frontBottomRight, backBottomRight, backTopRight, frontTopRight,
        VertexNormal{ 1.0f, 0.0f, 0.0f });
    addFace(backBottomLeft, frontBottomLeft, frontTopLeft, backTopLeft,
        VertexNormal{ -1.0f, 0.0f, 0.0f });
    addFace(frontTopLeft, frontTopRight, backTopRight, backTopLeft,
        VertexNormal{ 0.0f, 1.0f, 0.0f });
    addFace(backBottomLeft, backBottomRight, frontBottomRight, frontBottomLeft,
        VertexNormal{ 0.0f, -1.0f, 0.0f });

    return MeshDef //
        {
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MaterialDef = MaterialDef{},
        };
}

MeshDef
ShapeDefs::Ball(const BallParams& params)
{
    MLG_ASSERT(params.Radius > 0);
    MLG_ASSERT(params.Smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    // Clamp smoothness to determine subdivision level
    const float smoothness = std::max(1.0f, std::min(kMaxSmoothness, params.Smoothness));
    const size_t subdivisions = static_cast<size_t>(smoothness * 0.3f); // 0 to 3 subdivisions

    // Build shared geometry first. The atlas adds seam vertices after subdivision.
    // Shared geometry has V = 10 * 4^n + 2 vertices after n subdivisions.
    const size_t finalTriangles = 20 * (1uz << (2 * subdivisions)); // 20 * 4^subdivisions
    const size_t finalIndices = finalTriangles * 3;
    const size_t totalVertices = (subdivisions > 0)
        ? ((10 * (1uz << (2 * subdivisions))) + 2) : 12;

    vertices.reserve(totalVertices);
    indices.reserve(finalIndices);

    // Create icosahedron base vertices
    const float t = std::numbers::phi_v<float>; // Golden ratio
    const float len = std::sqrt(1.0f + (t * t));
    const float a = 1.0f / len;
    const float b = t / len;

    // 12 vertices of icosahedron
    vertices.push_back(Vertex{ .pos{ -a,  b,  0 }, .normal{ -a,  b,  0 }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  a,  b,  0 }, .normal{  a,  b,  0 }, .uvs{} });
    vertices.push_back(Vertex{ .pos{ -a, -b,  0 }, .normal{ -a, -b,  0 }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  a, -b,  0 }, .normal{  a, -b,  0 }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  0, -a,  b }, .normal{  0, -a,  b }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  0,  a,  b }, .normal{  0,  a,  b }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  0, -a, -b }, .normal{  0, -a, -b }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  0,  a, -b }, .normal{  0,  a, -b }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  b,  0, -a }, .normal{  b,  0, -a }, .uvs{} });
    vertices.push_back(Vertex{ .pos{  b,  0,  a }, .normal{  b,  0,  a }, .uvs{} });
    vertices.push_back(Vertex{ .pos{ -b,  0, -a }, .normal{ -b,  0, -a }, .uvs{} });
    vertices.push_back(Vertex{ .pos{ -b,  0,  a }, .normal{ -b,  0,  a }, .uvs{} });

    // 20 faces of icosahedron (clockwise winding)
    const VertexIndex faces[][3] =
    {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    std::vector<TriangleUvs> triangleUvs;
    std::vector<uint8_t> triangleFaces;
    triangleUvs.reserve(finalTriangles);
    triangleFaces.reserve(finalTriangles);

    // Each original face gets its own triangle in a 5 by 4 texture atlas.
    for (uint8_t faceIndex = 0; faceIndex < 20; ++faceIndex)
    {
        const auto& face = faces[faceIndex];
        indices.push_back(face[0]);
        indices.push_back(face[1]);
        indices.push_back(face[2]);

        constexpr float cellU = 1.0f / 5.0f;
        constexpr float cellV = 1.0f / 4.0f;
        // Remainder picks the column, and integer division picks the row.
        const uint8_t column = faceIndex % 5;
        const uint8_t row = faceIndex / 5;
        const float u = static_cast<float>(column) * cellU;
        const float v = static_cast<float>(row) * cellV;
        triangleUvs.push_back(TriangleUvs{ .Corners{
            UV2{ .u = u + (cellU * 0.05f), .v = v + (cellV * 0.05f) },
            UV2{ .u = u + (cellU * 0.95f), .v = v + (cellV * 0.05f) },
            UV2{ .u = u + (cellU * 0.05f), .v = v + (cellV * 0.95f) }
        } });
        triangleFaces.push_back(faceIndex);
    }

    // Hoist midpoint cache outside the loop
    std::unordered_map<uint64_t, VertexIndex> midpointCache;

    // Lambda to get or create midpoint vertex
    auto getMidpoint = [&](VertexIndex v0, VertexIndex v1) -> VertexIndex {
        // Ensure consistent ordering for the key
        if (v0 > v1) {std::swap(v0, v1);}
        const uint64_t key = (static_cast<uint64_t>(v0) << 32) | v1;

        auto it = midpointCache.find(key);
        if (it != midpointCache.end()) {
            return it->second;
        }

        // Calculate new midpoint
        VertexPos mid{
            (vertices[v0].pos.x + vertices[v1].pos.x) * 0.5f,
            (vertices[v0].pos.y + vertices[v1].pos.y) * 0.5f,
            (vertices[v0].pos.z + vertices[v1].pos.z) * 0.5f
        };

        // Normalize to sphere
        mid = mid.Normalize();

        const VertexIndex newIdx = static_cast<VertexIndex>(vertices.size());
        vertices.push_back(Vertex{ .pos = mid, .normal{mid.x, mid.y, mid.z}, .uvs{} });
        midpointCache[key] = newIdx;
        return newIdx;
    };

    // Subdivide triangles with vertex deduplication
    for (size_t subdiv = 0; subdiv < subdivisions; ++subdiv)
    {
        midpointCache.clear(); // Clear cache for each subdivision level

        const size_t currentTriangleCount = indices.size() / 3;
        const size_t newIndexCount = currentTriangleCount * 12; // Each triangle becomes 4 triangles (12 indices)

        // Resize to final size for this iteration
        indices.resize(newIndexCount);
        triangleUvs.resize(currentTriangleCount * 4);
        triangleFaces.resize(currentTriangleCount * 4);

        // Process triangles from back to front to avoid overwriting data we still need
        for (int i = static_cast<int>(currentTriangleCount) - 1; i >= 0; --i)
        {
            const size_t oldOffset = static_cast<size_t>(i) * 3;
            const size_t newOffset = static_cast<size_t>(i) * 12;

            const VertexIndex v0 = indices[oldOffset];
            const VertexIndex v1 = indices[oldOffset + 1];
            const VertexIndex v2 = indices[oldOffset + 2];
            const auto oldUvs = triangleUvs[static_cast<size_t>(i)];
            const uint8_t faceIndex = triangleFaces[static_cast<size_t>(i)];
            const auto midpointUv = [](UV2 uvA, UV2 uvB) -> UV2 {
                return UV2{ .u = (uvA.u + uvB.u) * 0.5f, .v = (uvA.v + uvB.v) * 0.5f };
            };
            const UV2 uv01 = midpointUv(oldUvs.Corners[0], oldUvs.Corners[1]);
            const UV2 uv12 = midpointUv(oldUvs.Corners[1], oldUvs.Corners[2]);
            const UV2 uv20 = midpointUv(oldUvs.Corners[2], oldUvs.Corners[0]);

            // Get or create midpoint vertices (with deduplication)
            const VertexIndex m01 = getMidpoint(v0, v1);
            const VertexIndex m12 = getMidpoint(v1, v2);
            const VertexIndex m20 = getMidpoint(v2, v0);

            // Create 4 new triangles directly in the output positions
            indices[newOffset + 0] = v0;   indices[newOffset + 1] = m01;  indices[newOffset + 2] = m20;
            indices[newOffset + 3] = v1;   indices[newOffset + 4] = m12;  indices[newOffset + 5] = m01;
            indices[newOffset + 6] = v2;   indices[newOffset + 7] = m20;  indices[newOffset + 8] = m12;
            indices[newOffset + 9] = m01;  indices[newOffset + 10] = m12; indices[newOffset + 11] = m20;

            // Carry the parent face's UVs into each of its four children.
            const size_t firstChild = static_cast<size_t>(i) * 4;
            triangleUvs[firstChild] = TriangleUvs{ .Corners{ oldUvs.Corners[0], uv01, uv20 } };
            triangleUvs[firstChild + 1] = TriangleUvs{ .Corners{ oldUvs.Corners[1], uv12, uv01 } };
            triangleUvs[firstChild + 2] = TriangleUvs{ .Corners{ oldUvs.Corners[2], uv20, uv12 } };
            triangleUvs[firstChild + 3] = TriangleUvs{ .Corners{ uv01, uv12, uv20 } };
            for (size_t child = 0; child < 4; ++child)
            {
                triangleFaces[firstChild + child] = faceIndex;
            }
        }
    }

    // A shared edge needs separate UVs for each face, even though its positions match.
    const size_t edgeSegments = 1uz << subdivisions;
    std::vector<Vertex> atlasVertices;
    atlasVertices.reserve(20 * (edgeSegments + 1) * (edgeSegments + 2) / 2);
    std::vector<VertexIndex> atlasIndices(
        20 * totalVertices, std::numeric_limits<VertexIndex>::max());
    for (size_t triangle = 0; triangle < finalTriangles; ++triangle)
    {
        for (size_t corner = 0; corner < 3; ++corner)
        {
            VertexIndex& index = indices[(triangle * 3) + corner];
            const size_t atlasSlot =
                (static_cast<size_t>(triangleFaces[triangle]) * totalVertices) + index;
            VertexIndex& atlasIndex = atlasIndices[atlasSlot];
            if (atlasIndex == std::numeric_limits<VertexIndex>::max())
            {
                atlasIndex = static_cast<VertexIndex>(atlasVertices.size());
                atlasVertices.push_back(vertices[index]);
                atlasVertices.back().uvs[0] = triangleUvs[triangle].Corners[corner];
            }
            index = atlasIndex;
        }
    }
    vertices = std::move(atlasVertices);

    // Scale to desired radius
    for (auto& vertex : vertices)
    {
        vertex.pos.x *= params.Radius;
        vertex.pos.y *= params.Radius;
        vertex.pos.z *= params.Radius;
    }

    return MeshDef //
        {
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MaterialDef = MaterialDef{},
        };
}

MeshDef
ShapeDefs::Cylinder(const CylinderParams& params)
{
    MLG_ASSERT(params.Height > 0);
    MLG_ASSERT(params.Radius > 0);
    MLG_ASSERT(params.Smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    const float halfHeight = params.Height * 0.5f;

    // Clamp smoothness and calculate segments
    const float smoothness = std::max(1.0f, std::min(kMaxSmoothness, params.Smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + (smoothness * 4)); // 12 to 48 segments

    // Reserve exact sizes
    const uint32_t totalVertices = (segments * 4) + 4; // sides with seam + cap rings + centers
    const uint32_t totalIndices = segments * 12; // Two side and two cap triangles per segment
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // The last side pair repeats the first position with U = 1, so no triangle crosses the seam.
    for (uint32_t seg = 0; seg <= segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float cosTheta = (seg == segments) ? 1.0f : std::cos(theta);
        const float sinTheta = (seg == segments) ? 0.0f : std::sin(theta);
        const float x = params.Radius * cosTheta;
        const float z = params.Radius * sinTheta;
        const VertexNormal normal{ cosTheta, 0.0f, sinTheta };
        const float u = static_cast<float>(seg) / static_cast<float>(segments);

        vertices.push_back(Vertex{ .pos{ x, -halfHeight, z }, .normal = normal,
            .uvs{ UV2{ .u = u, .v = 0.0f } } });
        vertices.push_back(Vertex{ .pos{ x, halfHeight, z }, .normal = normal,
            .uvs{ UV2{ .u = u, .v = 1.0f } } });
    }

    // Generate side indices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = seg * 2;
        const uint32_t next = (seg + 1) * 2;

        // First triangle (clockwise)
        indices.push_back(current);
        indices.push_back(current + 1);
        indices.push_back(next);

        // Second triangle (clockwise)
        indices.push_back(next);
        indices.push_back(current + 1);
        indices.push_back(next + 1);
    }

    // Cap vertices (separate from side vertices due to different normals)
    const uint32_t bottomCapStart = (segments + 1) * 2;
    const uint32_t topCapStart = bottomCapStart + segments + 1;

    // Bottom cap center
    vertices.push_back(Vertex{ .pos{ 0.0f, -halfHeight, 0.0f },
        .normal{ 0.0f, -1.0f, 0.0f }, .uvs{ UV2{ .u = 0.5f, .v = 0.5f } } });

    // Bottom cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = params.Radius * std::cos(theta);
        const float z = params.Radius * std::sin(theta);
        vertices.push_back(Vertex{ .pos{ x, -halfHeight, z }, .normal{ 0.0f, -1.0f, 0.0f },
            .uvs{ UV2{ .u = 0.5f + ((0.5f * x) / params.Radius),
                .v = 0.5f + ((0.5f * z) / params.Radius) } } });
    }

    // Top cap center
    vertices.push_back(Vertex{ .pos{ 0.0f, halfHeight, 0.0f },
        .normal{ 0.0f, 1.0f, 0.0f }, .uvs{ UV2{ .u = 0.5f, .v = 0.5f } } });

    // Top cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = params.Radius * std::cos(theta);
        const float z = params.Radius * std::sin(theta);
        vertices.push_back(Vertex{ .pos{ x, halfHeight, z }, .normal{ 0.0f, 1.0f, 0.0f },
            .uvs{ UV2{ .u = 0.5f + ((0.5f * x) / params.Radius),
                .v = 0.5f + ((0.5f * z) / params.Radius) } } });
    }

    // Bottom cap indices (clockwise from below)
    const uint32_t bottomCenter = bottomCapStart;
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = bottomCapStart + 1 + seg;
        const uint32_t next = bottomCapStart + 1 + ((seg + 1) % segments);

        indices.push_back(bottomCenter);
        indices.push_back(current);
        indices.push_back(next);
    }

    // Top cap indices (clockwise from above)
    const uint32_t topCenter = topCapStart;
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = topCapStart + 1 + seg;
        const uint32_t next = topCapStart + 1 + ((seg + 1) % segments);

        indices.push_back(topCenter);
        indices.push_back(next);
        indices.push_back(current);
    }

    return MeshDef //
        {
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MaterialDef = MaterialDef{},
        };
}

MeshDef
ShapeDefs::Cone(const ConeParams& params)
{
    MLG_ASSERT(params.Radius1 >= 0);
    MLG_ASSERT(params.Radius2 >= 0);
    MLG_ASSERT((params.Radius1 > 0) || (params.Radius2 > 0));
    MLG_ASSERT(params.Smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float height = 1.0f;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float smoothness = std::max(1.0f, std::min(kMaxSmoothness, params.Smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + (smoothness * 4)); // 12 to 48 segments

    // Calculate exact sizes
    const bool hasBottomCap = params.Radius1 > 0.0f;
    const bool hasTopCap = params.Radius2 > 0.0f;
    const bool hasSideQuads = hasBottomCap && hasTopCap;

    uint32_t totalVertices = (segments + 1) * 2; // Side vertices with UV seam
    if (hasBottomCap) {totalVertices += (segments + 1); } // Bottom cap ring + center
    if (hasTopCap) {totalVertices += (segments + 1); }    // Top cap ring + center

    uint32_t totalIndices = segments * 3; // At least triangular side
    if (hasSideQuads) {totalIndices += (segments * 3); } // Additional triangles for quads
    if (hasBottomCap) {totalIndices += (segments * 3); }
    if (hasTopCap) {totalIndices += (segments * 3); }

    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Calculate slant normal for the cone's side
    const float dr = params.Radius2 - params.Radius1;
    const float slantLength = std::sqrt((dr * dr) + (height * height));
    const float normalY = (-dr) / slantLength;
    const float normalXZ = height / slantLength;

    // The last pair repeats the first position so U can end at 1 without wrapping a triangle.
    for (uint32_t seg = 0; seg <= segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float cosTheta = (seg == segments) ? 1.0f : std::cos(theta);
        const float sinTheta = (seg == segments) ? 0.0f : std::sin(theta);
        const float u = static_cast<float>(seg) / static_cast<float>(segments);

        const float x1 = params.Radius1 * cosTheta;
        const float z1 = params.Radius1 * sinTheta;
        const float x2 = params.Radius2 * cosTheta;
        const float z2 = params.Radius2 * sinTheta;

        const VertexNormal normal = VertexNormal{
            cosTheta * normalXZ,
            normalY,
            sinTheta * normalXZ
        }.Normalize();

        // Bottom vertex
        vertices.push_back(Vertex{ .pos{ x1, -halfHeight, z1 }, .normal = normal,
            .uvs{ UV2{ .u = u, .v = 0.0f } } });
        // Top vertex
        vertices.push_back(Vertex{ .pos{ x2, halfHeight, z2 }, .normal = normal,
            .uvs{ UV2{ .u = u, .v = 1.0f } } });
    }

    // Generate side indices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = seg * 2;
        const uint32_t next = (seg + 1) * 2;

        // Skip the triangle whose two vertices collapse onto a zero-radius tip.
        if (hasBottomCap)
        {
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);
        }
        if (hasTopCap)
        {
            indices.push_back(next);
            indices.push_back(current + 1);
            indices.push_back(next + 1);
        }
    }

    uint32_t currentVertexOffset = (segments + 1) * 2;

    // Bottom cap (only if radius1 > 0)
    if (hasBottomCap)
    {
        const uint32_t bottomCenter = currentVertexOffset;
        const uint32_t bottomRingStart = currentVertexOffset + 1;

        // Bottom cap center
        vertices.push_back(Vertex{ .pos{ 0.0f, -halfHeight, 0.0f },
            .normal{ 0.0f, -1.0f, 0.0f }, .uvs{ UV2{ .u = 0.5f, .v = 0.5f } } });

        // Bottom cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = params.Radius1 * std::cos(theta);
            const float z = params.Radius1 * std::sin(theta);
            vertices.push_back(Vertex{ .pos{ x, -halfHeight, z }, .normal{ 0.0f, -1.0f, 0.0f },
                .uvs{ UV2{ .u = 0.5f + ((0.5f * x) / params.Radius1),
                    .v = 0.5f + ((0.5f * z) / params.Radius1) } } });
        }

        // Bottom cap indices
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = bottomRingStart + seg;
            const uint32_t next = bottomRingStart + ((seg + 1) % segments);

            indices.push_back(bottomCenter);
            indices.push_back(current);
            indices.push_back(next);
        }

        currentVertexOffset += (segments + 1);
    }

    // Top cap (only if radius2 > 0)
    if (hasTopCap)
    {
        const uint32_t topCenter = currentVertexOffset;
        const uint32_t topRingStart = currentVertexOffset + 1;

        // Top cap center
        vertices.push_back(Vertex{ .pos{ 0.0f, halfHeight, 0.0f },
            .normal{ 0.0f, 1.0f, 0.0f }, .uvs{ UV2{ .u = 0.5f, .v = 0.5f } } });

        // Top cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = params.Radius2 * std::cos(theta);
            const float z = params.Radius2 * std::sin(theta);
            vertices.push_back(Vertex{ .pos{ x, halfHeight, z }, .normal{ 0.0f, 1.0f, 0.0f },
                .uvs{ UV2{ .u = 0.5f + ((0.5f * x) / params.Radius2),
                    .v = 0.5f + ((0.5f * z) / params.Radius2) } } });
        }

        // Top cap indices
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = topRingStart + seg;
            const uint32_t next = topRingStart + ((seg + 1) % segments);

            indices.push_back(topCenter);
            indices.push_back(next);
            indices.push_back(current);
        }
    }

    return MeshDef //
        {
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MaterialDef = MaterialDef{},
        };
}

MeshDef
ShapeDefs::Torus(const TorusParams& params)
{
    MLG_ASSERT(std::isfinite(params.RingRadius) && std::isfinite(params.TubeRadius) &&
        (params.TubeRadius > 0.0f) && (params.RingRadius > params.TubeRadius),
        "Torus requires finite radii with RingRadius > TubeRadius > 0");
    MLG_ASSERT(params.Smoothness > 0);

    const float smoothness = std::max(1.0f, std::min(kMaxSmoothness, params.Smoothness));

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    // Keep both circular silhouettes round even at the lowest smoothness.
    constexpr size_t kBaseSegments = 12;
    const size_t numSegmentsMajor = kBaseSegments + static_cast<size_t>(smoothness * 4);
    const size_t numSegmentsMinor = kBaseSegments + static_cast<size_t>(smoothness * 4);
    MLG_ASSERT(numSegmentsMajor > 0);
    MLG_ASSERT(numSegmentsMinor > 0);

    MLG_ABORTIF(numSegmentsMajor == 0 || numSegmentsMinor == 0,
        "Torus segment counts must be nonzero");

    const float dTheta = 2.0f * kPi / static_cast<float>(numSegmentsMajor);
    const float dPhi = 2.0f * kPi / static_cast<float>(numSegmentsMinor);

    // Reserve exact memory
    const size_t totalVertices = (numSegmentsMajor + 1) * (numSegmentsMinor + 1);
    const size_t totalIndices = numSegmentsMajor * numSegmentsMinor * 6;
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    constexpr size_t kMaxSegments = kBaseSegments + static_cast<size_t>(ShapeDefs::kMaxSmoothness * 4);

    // Precompute trig values for major circle
    float cosThetaCache[kMaxSegments];
    float sinThetaCache[kMaxSegments];
    for (size_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float theta = static_cast<float>(i) * dTheta;
        cosThetaCache[i] = std::cos(theta);
        sinThetaCache[i] = std::sin(theta);
    }

    // Precompute trig values for minor circle
    float cosPhiCache[kMaxSegments];
    float sinPhiCache[kMaxSegments];
    for (size_t j = 0; j < numSegmentsMinor; ++j)
    {
        const float phi = static_cast<float>(j) * dPhi;
        cosPhiCache[j] = std::cos(phi);
        sinPhiCache[j] = std::sin(phi);
    }

    // Repeat both boundary rows with UV 1 so neither texture seam crosses a triangle.
    for (size_t i = 0; i <= numSegmentsMajor; ++i)
    {
        const float cosTheta = cosThetaCache[i % numSegmentsMajor];
        const float sinTheta = sinThetaCache[i % numSegmentsMajor];

        for (size_t j = 0; j <= numSegmentsMinor; ++j)
        {
            const float cosPhi = cosPhiCache[j % numSegmentsMinor];
            const float sinPhi = sinPhiCache[j % numSegmentsMinor];

            // Vertex position (left-handed)
            const float distanceFromCenter = params.RingRadius + (params.TubeRadius * cosPhi);
            const float x = distanceFromCenter * cosTheta;
            const float y = distanceFromCenter * sinTheta;
            const float z = params.TubeRadius * sinPhi;

            // Unit normal (same calculation but clearer with explicit names)
            const float nx = cosTheta * cosPhi;
            const float ny = sinTheta * cosPhi;
            const float nz = sinPhi;

            vertices.push_back(Vertex{ .pos{ x, y, z }, .normal{ nx, ny, nz },
                .uvs{ UV2{
                    .u = static_cast<float>(i) / static_cast<float>(numSegmentsMajor),
                    .v = static_cast<float>(j) / static_cast<float>(numSegmentsMinor)
                } } });
        }
    }

    // Generate triangle indices (clockwise for left-handed system)
    for (size_t i = 0; i < numSegmentsMajor; ++i)
    {
        const size_t rowOffset = i * (numSegmentsMinor + 1);
        const size_t nextRowOffset = (i + 1) * (numSegmentsMinor + 1);

        for (size_t j = 0; j < numSegmentsMinor; ++j)
        {
            const size_t i0 = rowOffset + j;
            const size_t i1 = nextRowOffset + j;
            const size_t i2 = nextRowOffset + j + 1;
            const size_t i3 = rowOffset + j + 1;

            // First triangle
            indices.push_back(static_cast<VertexIndex>(i0));
            indices.push_back(static_cast<VertexIndex>(i1));
            indices.push_back(static_cast<VertexIndex>(i2));

            // Second triangle
            indices.push_back(static_cast<VertexIndex>(i0));
            indices.push_back(static_cast<VertexIndex>(i2));
            indices.push_back(static_cast<VertexIndex>(i3));
        }
    }

    return MeshDef //
        {
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MaterialDef = MaterialDef{},
        };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)