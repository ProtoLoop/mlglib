#include "Frustum.h"

#include "BoundingVolumes.h"

#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <SDL3/SDL_assert.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
constexpr float EPS = 1e-5f;
using ContainsResult = Frustum::ContainsResult;

float
SignedDistanceToPlane(const Vec4f& plane, const Vec3f& point)
{
    return plane.Dot(Vec4f(point, 1.0f));
}

std::array<Vec4f, 6>
GetPlanes(const Frustum& frustum)
{
    return { frustum.GetLeft(),
        frustum.GetRight(),
        frustum.GetTop(),
        frustum.GetBottom(),
        frustum.GetNear(),
        frustum.GetFar() };
}

Projection::Perspective
MakePerspectiveSettings()
{
    return { .Fov = Radiansf::FromDegrees(73.0f),
        .AspectRatio = 137.0f / 89.0f,
        .NearClip = 0.71f,
        .FarClip = 18.3f };
}

Projection::Perspective
MakePerspectiveGeometrySettings()
{
    return { .Fov = Radiansf::FromDegrees(90), .AspectRatio = 2, .NearClip = 1, .FarClip = 8 };
}

Projection::Orthographic
MakeOrthographicSettings()
{
    return { .Height = 6, .AspectRatio = 2, .NearClip = 1, .FarClip = 9 };
}

TrTransformf
MakeTranslatedRotatedCameraTransform()
{
    TrTransformf xform;
    xform.T = Vec3f(4.7f, -2.3f, 8.1f);
    xform.R = UnitQuatf(Radiansf::FromDegrees(31.0f), Vec3f::YAXIS())
        * UnitQuatf(Radiansf::FromDegrees(-17.0f), Vec3f::XAXIS());
    return xform;
}

Vec2f
ScreenToNdcForTest(const Vec2f& screenPos, const uint32_t screenWidth, const uint32_t screenHeight)
{
    const float x = screenPos.x / static_cast<float>(screenWidth);
    const float y = screenPos.y / static_cast<float>(screenHeight);
    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}

Vec3f
WorldFromScreenPoint(const Frustum& localFrustum,
    const TrTransformf& cameraXform,
    const Vec2f& screenPos,
    const float ndcDepth,
    const uint32_t screenWidth,
    const uint32_t screenHeight)
{
    const Vec2f ndc = ScreenToNdcForTest(screenPos, screenWidth, screenHeight);
    const Mat44f invViewProj = cameraXform.ToMatrix() * localFrustum.GetMatrix().Inverse();
    const Vec4f world = invViewProj * Vec4f(ndc, ndcDepth, 1.0f);
    return world.xyz() / world.w;
}

/// Lets failure-path tests check the Result after MLG_CHECKV reports an assertion.
class IgnoreFrustumAssertions final
{
public:
    IgnoreFrustumAssertions()
        : m_PreviousHandler(SDL_GetAssertionHandler(&m_PreviousUserData))
    {
        SDL_SetAssertionHandler(&Ignore, nullptr);
    }

    ~IgnoreFrustumAssertions() { SDL_SetAssertionHandler(m_PreviousHandler, m_PreviousUserData); }

    IgnoreFrustumAssertions(const IgnoreFrustumAssertions&) = delete;
    IgnoreFrustumAssertions& operator=(const IgnoreFrustumAssertions&) = delete;
    IgnoreFrustumAssertions(IgnoreFrustumAssertions&&) = delete;
    IgnoreFrustumAssertions& operator=(IgnoreFrustumAssertions&&) = delete;

private:
    static SDL_AssertState SDLCALL Ignore(const SDL_AssertData*, void*)
    {
        return SDL_ASSERTION_IGNORE;
    }

    void* m_PreviousUserData{ nullptr };
    SDL_AssertionHandler m_PreviousHandler{ nullptr };
};
} // namespace

TEST(Frustum, DefaultConstruction_UsesDefaultPerspective)
{
    const Frustum frustum;
    const auto result = Frustum::Create(Projection::Perspective{});
    ASSERT_TRUE(result);
    EXPECT_EQ(frustum.GetMatrix(), result->GetMatrix());
    EXPECT_EQ(GetPlanes(frustum), GetPlanes(*result));
    EXPECT_EQ(frustum.Contains(BoundingSphere(Vec3f(0, 0, 5), 0.1f)), ContainsResult::Inside);
    for(const auto& plane : GetPlanes(frustum))
    {
        EXPECT_NEAR(plane.xyz().Length(), 1, EPS);
    }
}

TEST(Frustum, Perspective_Create_PlanesMatchKnownGeometry)
{
    const auto result = Frustum::Create(MakePerspectiveGeometrySettings());
    ASSERT_TRUE(result);
    struct Face
    {
        const char* Name;
        Vec4f Plane;
        Vec3f PointOnPlane;
        Vec3f InwardNormal;
    };
    const Face faces[] //
        {
            { .Name = "left",
                .Plane = result->GetLeft(),
                .PointOnPlane = Vec3f(-8, 0, 4),
                .InwardNormal = Vec3f(1, 0, 2).Normalize() },
            { .Name = "right",
                .Plane = result->GetRight(),
                .PointOnPlane = Vec3f(8, 0, 4),
                .InwardNormal = Vec3f(-1, 0, 2).Normalize() },
            { .Name = "top",
                .Plane = result->GetTop(),
                .PointOnPlane = Vec3f(0, 4, 4),
                .InwardNormal = Vec3f(0, -1, 1).Normalize() },
            { .Name = "bottom",
                .Plane = result->GetBottom(),
                .PointOnPlane = Vec3f(0, -4, 4),
                .InwardNormal = Vec3f(0, 1, 1).Normalize() },
            { .Name = "near",
                .Plane = result->GetNear(),
                .PointOnPlane = Vec3f(0, 0, 1),
                .InwardNormal = Vec3f(0, 0, 1) },
            { .Name = "far",
                .Plane = result->GetFar(),
                .PointOnPlane = Vec3f(0, 0, 8),
                .InwardNormal = Vec3f(0, 0, -1) },
        };
    for(const auto& face : faces)
    {
        SCOPED_TRACE(face.Name);
        EXPECT_NEAR(SignedDistanceToPlane(face.Plane, face.PointOnPlane), 0, EPS);
        EXPECT_NEAR(face.Plane.x, face.InwardNormal.x, EPS);
        EXPECT_NEAR(face.Plane.y, face.InwardNormal.y, EPS);
        EXPECT_NEAR(face.Plane.z, face.InwardNormal.z, EPS);
        EXPECT_NEAR(face.Plane.xyz().Length(), 1, EPS);
        EXPECT_GT(SignedDistanceToPlane(face.Plane, Vec3f(0, 0, 4)), 0);
    }
}

TEST(Frustum, Perspective_Contains_ClassifiesSphereDistancesAcrossEveryFace)
{
    const auto result = Frustum::Create(MakePerspectiveGeometrySettings());
    ASSERT_TRUE(result);
    struct Face
    {
        const char* Name;
        Vec3f PointOnPlane;
        Vec3f InwardNormal;
    };
    const Face faces[] //
        {
            { .Name = "left",
                .PointOnPlane = Vec3f(-8, 0, 4),
                .InwardNormal = Vec3f(1, 0, 2).Normalize() },
            { .Name = "right",
                .PointOnPlane = Vec3f(8, 0, 4),
                .InwardNormal = Vec3f(-1, 0, 2).Normalize() },
            { .Name = "top",
                .PointOnPlane = Vec3f(0, 4, 4),
                .InwardNormal = Vec3f(0, -1, 1).Normalize() },
            { .Name = "bottom",
                .PointOnPlane = Vec3f(0, -4, 4),
                .InwardNormal = Vec3f(0, 1, 1).Normalize() },
            { .Name = "near", .PointOnPlane = Vec3f(0, 0, 1), .InwardNormal = Vec3f(0, 0, 1) },
            { .Name = "far", .PointOnPlane = Vec3f(0, 0, 8), .InwardNormal = Vec3f(0, 0, -1) },
        };
    struct Case
    {
        const char* Name;
        float Distance;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "outside", .Distance = -0.75f, .Expected = ContainsResult::Outside },
            { .Name = "external overlap",
                .Distance = -0.25f,
                .Expected = ContainsResult::Intersects },
            { .Name = "on face", .Distance = 0, .Expected = ContainsResult::Intersects },
            { .Name = "internal overlap",
                .Distance = 0.25f,
                .Expected = ContainsResult::Intersects },
            { .Name = "inside", .Distance = 0.75f, .Expected = ContainsResult::Inside },
        };
    // Keep margins around sloped faces to avoid rounding-sensitive exact tangency.
    const auto transform = MakeTranslatedRotatedCameraTransform();
    const auto world = result->Transform(transform);
    for(const auto& face : faces)
    {
        SCOPED_TRACE(face.Name);
        for(const auto& testCase : cases)
        {
            SCOPED_TRACE(testCase.Name);
            const Vec3f center = face.PointOnPlane + face.InwardNormal * testCase.Distance;
            EXPECT_EQ(result->Contains(BoundingSphere(center, 0.5f)), testCase.Expected);
            EXPECT_EQ(world.Contains(BoundingSphere(transform * center, 0.5f)), testCase.Expected);
        }
    }
}

TEST(Frustum, Perspective_Contains_ClassifiesNearAndFarBoundaryDistances)
{
    // These clip distances produce exactly representable near/far plane coefficients.
    const auto result = Frustum::Create({ .Fov = Radiansf::FromDegrees(90.0f),
        .AspectRatio = 2.0f,
        .NearClip = 1.0f,
        .FarClip = 2.0f });
    ASSERT_TRUE(result);
    const Frustum& frustum = *result;

    struct Case
    {
        const char* Name;
        float Depth;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "near outside", .Depth = 0.8125f, .Expected = ContainsResult::Outside },
            { .Name = "near external tangent",
                .Depth = 0.875f,
                .Expected = ContainsResult::Outside },
            { .Name = "near intersection",
                .Depth = 0.9375f,
                .Expected = ContainsResult::Intersects },
            { .Name = "near internal overlap",
                .Depth = 1.0625f,
                .Expected = ContainsResult::Intersects },
            { .Name = "near internal tangent",
                .Depth = 1.125f,
                .Expected = ContainsResult::Inside },
            { .Name = "near inside", .Depth = 1.1875f, .Expected = ContainsResult::Inside },
            { .Name = "far inside", .Depth = 1.8125f, .Expected = ContainsResult::Inside },
            { .Name = "far internal tangent", .Depth = 1.875f, .Expected = ContainsResult::Inside },
            { .Name = "far internal overlap",
                .Depth = 1.9375f,
                .Expected = ContainsResult::Intersects },
            { .Name = "far intersection",
                .Depth = 2.0625f,
                .Expected = ContainsResult::Intersects },
            { .Name = "far external tangent",
                .Depth = 2.125f,
                .Expected = ContainsResult::Outside },
            { .Name = "far outside", .Depth = 2.1875f, .Expected = ContainsResult::Outside },
        };
    for(const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_EQ(frustum.Contains(BoundingSphere(Vec3f(0.0f, 0.0f, testCase.Depth), 0.125f)),
            testCase.Expected);
    }
}

TEST(Frustum, Perspective_Create_IsIndependentOfInputSettings)
{
    auto settings = MakePerspectiveGeometrySettings();
    const auto result = Frustum::Create(settings);
    ASSERT_TRUE(result);
    const auto matrix = result->GetMatrix();
    const auto planes = GetPlanes(*result);
    settings = Projection::Perspective{};
    EXPECT_EQ(result->GetMatrix(), matrix);
    EXPECT_EQ(GetPlanes(*result), planes);
}

TEST(Frustum, Perspective_Projection_MapsKnownPointsToNdc)
{
    const auto result = Frustum::Create(MakePerspectiveGeometrySettings());
    ASSERT_TRUE(result);
    struct Case
    {
        const char* Name;
        Vec3f Point;
        Vec3f Ndc;
        float W;
    };
    const Case cases[] //
        {
            { .Name = "near center", .Point = Vec3f(0, 0, 1), .Ndc = Vec3f(0, 0, 0), .W = 1 },
            { .Name = "near bottom left",
                .Point = Vec3f(-2, -1, 1),
                .Ndc = Vec3f(-1, -1, 0),
                .W = 1 },
            { .Name = "near bottom right",
                .Point = Vec3f(2, -1, 1),
                .Ndc = Vec3f(1, -1, 0),
                .W = 1 },
            { .Name = "near top left", .Point = Vec3f(-2, 1, 1), .Ndc = Vec3f(-1, 1, 0), .W = 1 },
            { .Name = "near top right", .Point = Vec3f(2, 1, 1), .Ndc = Vec3f(1, 1, 0), .W = 1 },
            { .Name = "middle center",
                .Point = Vec3f(0, 0, 4),
                .Ndc = Vec3f(0, 0, 6.0f / 7.0f),
                .W = 4 },
            { .Name = "middle bottom left",
                .Point = Vec3f(-8, -4, 4),
                .Ndc = Vec3f(-1, -1, 6.0f / 7.0f),
                .W = 4 },
            { .Name = "middle bottom right",
                .Point = Vec3f(8, -4, 4),
                .Ndc = Vec3f(1, -1, 6.0f / 7.0f),
                .W = 4 },
            { .Name = "middle top left",
                .Point = Vec3f(-8, 4, 4),
                .Ndc = Vec3f(-1, 1, 6.0f / 7.0f),
                .W = 4 },
            { .Name = "middle top right",
                .Point = Vec3f(8, 4, 4),
                .Ndc = Vec3f(1, 1, 6.0f / 7.0f),
                .W = 4 },
            { .Name = "far center", .Point = Vec3f(0, 0, 8), .Ndc = Vec3f(0, 0, 1), .W = 8 },
            { .Name = "far bottom left",
                .Point = Vec3f(-16, -8, 8),
                .Ndc = Vec3f(-1, -1, 1),
                .W = 8 },
            { .Name = "far bottom right",
                .Point = Vec3f(16, -8, 8),
                .Ndc = Vec3f(1, -1, 1),
                .W = 8 },
            { .Name = "far top left", .Point = Vec3f(-16, 8, 8), .Ndc = Vec3f(-1, 1, 1), .W = 8 },
            { .Name = "far top right", .Point = Vec3f(16, 8, 8), .Ndc = Vec3f(1, 1, 1), .W = 8 },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const Vec4f clip = result->GetMatrix() * Vec4f(testCase.Point, 1);
        ASSERT_GT(clip.w, 0);
        EXPECT_NEAR(clip.w, testCase.W, EPS);
        const Vec3f ndc = clip.xyz() / clip.w;
        EXPECT_NEAR(ndc.x, testCase.Ndc.x, EPS);
        EXPECT_NEAR(ndc.y, testCase.Ndc.y, EPS);
        EXPECT_NEAR(ndc.z, testCase.Ndc.z, EPS);
    }
}

TEST(Frustum, Perspective_Projection_AspectChangesOnlyHorizontalBounds)
{
    auto settings = MakePerspectiveGeometrySettings();
    settings.AspectRatio = 1;
    const auto square = Frustum::Create(settings);
    settings.AspectRatio = 2;
    const auto wide = Frustum::Create(settings);
    ASSERT_TRUE(square);
    ASSERT_TRUE(wide);
    EXPECT_EQ(square->GetTop(), wide->GetTop());
    EXPECT_EQ(square->GetBottom(), wide->GetBottom());
    EXPECT_EQ(square->GetNear(), wide->GetNear());
    EXPECT_EQ(square->GetFar(), wide->GetFar());
    struct Case
    {
        const char* Name;
        Vec3f Center;
    };
    const Case cases[] //
        {
            { .Name = "left", .Center = Vec3f(-6, 0, 4) },
            { .Name = "right", .Center = Vec3f(6, 0, 4) },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const BoundingSphere sphere(testCase.Center, 0.25f);
        EXPECT_EQ(square->Contains(sphere), ContainsResult::Outside);
        EXPECT_EQ(wide->Contains(sphere), ContainsResult::Inside);
    }
}

TEST(Frustum, Perspective_Create_RejectsInvalidSettings)
{
    const IgnoreFrustumAssertions ignoreAssertions;
    const Rect rect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    struct Case
    {
        const char* Name{ nullptr };
        Projection::Perspective Settings;
    };
    const Case cases[] //
        {
            { .Name = "zero fov", .Settings = { .Fov = Radiansf(0) } },
            { .Name = "negative fov", .Settings = { .Fov = Radiansf(-1) } },
            { .Name = "pi fov", .Settings = { .Fov = Radiansf(std::numbers::pi_v<float>) } },
            { .Name = "excessive fov", .Settings = { .Fov = Radiansf(4) } },
            { .Name = "zero aspect", .Settings = { .AspectRatio = 0 } },
            { .Name = "negative aspect", .Settings = { .AspectRatio = -1 } },
            { .Name = "zero near", .Settings = { .NearClip = 0 } },
            { .Name = "negative near", .Settings = { .NearClip = -1 } },
            { .Name = "zero far", .Settings = { .FarClip = 0 } },
            { .Name = "negative far", .Settings = { .FarClip = -1 } },
            { .Name = "equal clips", .Settings = { .NearClip = 2, .FarClip = 2 } },
            { .Name = "reversed clips", .Settings = { .NearClip = 3, .FarClip = 2 } },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_FALSE(Frustum::Create(testCase.Settings));
        EXPECT_FALSE(Frustum::Create(testCase.Settings, rect, 137, 89));
    }
}

TEST(Frustum, Perspective_Transform_PreservesContainmentAndAgreesAcrossOverloads)
{
    const auto result = Frustum::Create(MakePerspectiveGeometrySettings());
    ASSERT_TRUE(result);
    const auto transform = MakeTranslatedRotatedCameraTransform();
    const auto identity = result->Transform(TrTransformf::Identity());
    const auto world = result->Transform(transform);
    const auto matrixWorld = result->Transform(transform.ToMatrix());
    struct PlaneCase
    {
        const char* Name;
        Vec4f Local;
        Vec4f Identity;
        Vec4f World;
        Vec4f MatrixWorld;
    };
    const PlaneCase planes[] //
        {
            { .Name = "left",
                .Local = result->GetLeft(),
                .Identity = identity.GetLeft(),
                .World = world.GetLeft(),
                .MatrixWorld = matrixWorld.GetLeft() },
            { .Name = "right",
                .Local = result->GetRight(),
                .Identity = identity.GetRight(),
                .World = world.GetRight(),
                .MatrixWorld = matrixWorld.GetRight() },
            { .Name = "top",
                .Local = result->GetTop(),
                .Identity = identity.GetTop(),
                .World = world.GetTop(),
                .MatrixWorld = matrixWorld.GetTop() },
            { .Name = "bottom",
                .Local = result->GetBottom(),
                .Identity = identity.GetBottom(),
                .World = world.GetBottom(),
                .MatrixWorld = matrixWorld.GetBottom() },
            { .Name = "near",
                .Local = result->GetNear(),
                .Identity = identity.GetNear(),
                .World = world.GetNear(),
                .MatrixWorld = matrixWorld.GetNear() },
            { .Name = "far",
                .Local = result->GetFar(),
                .Identity = identity.GetFar(),
                .World = world.GetFar(),
                .MatrixWorld = matrixWorld.GetFar() },
        };
    for(const auto& plane : planes)
    {
        SCOPED_TRACE(plane.Name);
        EXPECT_NEAR((plane.Local - plane.Identity).Length(), 0, EPS);
        EXPECT_NEAR((plane.World - plane.MatrixWorld).Length(), 0, EPS);
        EXPECT_NEAR(plane.World.xyz().Length(), 1, EPS);
    }
    struct Case
    {
        const char* Name;
        Vec3f Center;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside", .Center = Vec3f(0, 0, 4), .Expected = ContainsResult::Inside },
            { .Name = "outside", .Center = Vec3f(0, 0, 10), .Expected = ContainsResult::Outside },
            { .Name = "left", .Center = Vec3f(-8, 0, 4), .Expected = ContainsResult::Intersects },
            { .Name = "right", .Center = Vec3f(8, 0, 4), .Expected = ContainsResult::Intersects },
            { .Name = "top", .Center = Vec3f(0, 4, 4), .Expected = ContainsResult::Intersects },
            { .Name = "bottom", .Center = Vec3f(0, -4, 4), .Expected = ContainsResult::Intersects },
            { .Name = "near", .Center = Vec3f(0, 0, 1), .Expected = ContainsResult::Intersects },
            { .Name = "far", .Center = Vec3f(0, 0, 8), .Expected = ContainsResult::Intersects },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const BoundingSphere localSphere(testCase.Center, 0.25f);
        const BoundingSphere worldSphere(transform * testCase.Center, 0.25f);
        EXPECT_EQ(result->Contains(localSphere), testCase.Expected);
        EXPECT_EQ(identity.Contains(localSphere), testCase.Expected);
        EXPECT_EQ(world.Contains(worldSphere), testCase.Expected);
        EXPECT_EQ(matrixWorld.Contains(worldSphere), testCase.Expected);
    }
}

TEST(Frustum, Perspective_Transform_NonuniformScaleKeepsPlanesNormalized)
{
    const auto result = Frustum::Create(MakePerspectiveGeometrySettings());
    ASSERT_TRUE(result);
    TrsTransformf transform;
    transform.S = Vec3f(2, 3, 4);
    const auto scaled = result->Transform(transform.ToMatrix());
    struct PlaneCase
    {
        const char* Name;
        Vec4f Plane;
    };
    const PlaneCase planes[] //
        {
            { .Name = "left", .Plane = scaled.GetLeft() },
            { .Name = "right", .Plane = scaled.GetRight() },
            { .Name = "top", .Plane = scaled.GetTop() },
            { .Name = "bottom", .Plane = scaled.GetBottom() },
            { .Name = "near", .Plane = scaled.GetNear() },
            { .Name = "far", .Plane = scaled.GetFar() },
        };
    for(const auto& plane : planes)
    {
        SCOPED_TRACE(plane.Name);
        EXPECT_NEAR(plane.Plane.xyz().Length(), 1, EPS);
    }
    struct Case
    {
        const char* Name;
        Vec3f Center;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside", .Center = Vec3f(0, 0, 16), .Expected = ContainsResult::Inside },
            { .Name = "outside", .Center = Vec3f(0, 0, 40), .Expected = ContainsResult::Outside },
            { .Name = "left", .Center = Vec3f(-16, 0, 16), .Expected = ContainsResult::Intersects },
            { .Name = "right", .Center = Vec3f(16, 0, 16), .Expected = ContainsResult::Intersects },
            { .Name = "top", .Center = Vec3f(0, 12, 16), .Expected = ContainsResult::Intersects },
            { .Name = "bottom",
                .Center = Vec3f(0, -12, 16),
                .Expected = ContainsResult::Intersects },
            { .Name = "near", .Center = Vec3f(0, 0, 4), .Expected = ContainsResult::Intersects },
            { .Name = "far", .Center = Vec3f(0, 0, 32), .Expected = ContainsResult::Intersects },
            { .Name = "near external overlap",
                .Center = Vec3f(0, 0, 3.75f),
                .Expected = ContainsResult::Intersects },
            { .Name = "far external overlap",
                .Center = Vec3f(0, 0, 32.25f),
                .Expected = ContainsResult::Intersects },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_EQ(scaled.Contains(BoundingSphere(testCase.Center, 0.5f)), testCase.Expected);
    }
}

TEST(Frustum, Orthographic_Create_PlanesMatchKnownGeometry)
{
    const auto result = Frustum::Create(MakeOrthographicSettings());
    ASSERT_TRUE(result);
    struct Face
    {
        const char* Name;
        Vec4f Plane;
        Vec3f PointOnPlane;
        Vec3f InwardNormal;
    };
    const Face faces[] //
        {
            { .Name = "left",
                .Plane = result->GetLeft(),
                .PointOnPlane = Vec3f(-6, 0, 5),
                .InwardNormal = Vec3f(1, 0, 0) },
            { .Name = "right",
                .Plane = result->GetRight(),
                .PointOnPlane = Vec3f(6, 0, 5),
                .InwardNormal = Vec3f(-1, 0, 0) },
            { .Name = "top",
                .Plane = result->GetTop(),
                .PointOnPlane = Vec3f(0, 3, 5),
                .InwardNormal = Vec3f(0, -1, 0) },
            { .Name = "bottom",
                .Plane = result->GetBottom(),
                .PointOnPlane = Vec3f(0, -3, 5),
                .InwardNormal = Vec3f(0, 1, 0) },
            { .Name = "near",
                .Plane = result->GetNear(),
                .PointOnPlane = Vec3f(0, 0, 1),
                .InwardNormal = Vec3f(0, 0, 1) },
            { .Name = "far",
                .Plane = result->GetFar(),
                .PointOnPlane = Vec3f(0, 0, 9),
                .InwardNormal = Vec3f(0, 0, -1) },
        };
    for(const auto& face : faces)
    {
        SCOPED_TRACE(face.Name);
        EXPECT_NEAR(SignedDistanceToPlane(face.Plane, face.PointOnPlane), 0, EPS);
        EXPECT_NEAR(face.Plane.x, face.InwardNormal.x, EPS);
        EXPECT_NEAR(face.Plane.y, face.InwardNormal.y, EPS);
        EXPECT_NEAR(face.Plane.z, face.InwardNormal.z, EPS);
        EXPECT_NEAR(face.Plane.xyz().Length(), 1, EPS);
        EXPECT_GT(SignedDistanceToPlane(face.Plane, Vec3f(0, 0, 4)), 0);
    }
}

TEST(Frustum, Orthographic_Contains_ClassifiesSphereDistancesAcrossEveryFace)
{
    const auto result = Frustum::Create(MakeOrthographicSettings());
    ASSERT_TRUE(result);
    struct Face
    {
        const char* Name;
        Vec3f PointOnPlane;
        Vec3f InwardNormal;
    };
    const Face faces[] //
        {
            { .Name = "left", .PointOnPlane = Vec3f(-6, 0, 5), .InwardNormal = Vec3f(1, 0, 0) },
            { .Name = "right", .PointOnPlane = Vec3f(6, 0, 5), .InwardNormal = Vec3f(-1, 0, 0) },
            { .Name = "top", .PointOnPlane = Vec3f(0, 3, 5), .InwardNormal = Vec3f(0, -1, 0) },
            { .Name = "bottom", .PointOnPlane = Vec3f(0, -3, 5), .InwardNormal = Vec3f(0, 1, 0) },
            { .Name = "near", .PointOnPlane = Vec3f(0, 0, 1), .InwardNormal = Vec3f(0, 0, 1) },
            { .Name = "far", .PointOnPlane = Vec3f(0, 0, 9), .InwardNormal = Vec3f(0, 0, -1) },
        };
    struct Case
    {
        const char* Name;
        float Distance;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "outside", .Distance = -0.75f, .Expected = ContainsResult::Outside },
            { .Name = "external tangent", .Distance = -0.5f, .Expected = ContainsResult::Outside },
            { .Name = "external overlap",
                .Distance = -0.25f,
                .Expected = ContainsResult::Intersects },
            { .Name = "on face", .Distance = 0, .Expected = ContainsResult::Intersects },
            { .Name = "internal overlap",
                .Distance = 0.25f,
                .Expected = ContainsResult::Intersects },
            { .Name = "internal tangent", .Distance = 0.5f, .Expected = ContainsResult::Inside },
            { .Name = "inside", .Distance = 0.75f, .Expected = ContainsResult::Inside },
        };
    const auto transform = MakeTranslatedRotatedCameraTransform();
    const auto world = result->Transform(transform);
    for(const auto& face : faces)
    {
        SCOPED_TRACE(face.Name);
        for(const auto& testCase : cases)
        {
            SCOPED_TRACE(testCase.Name);
            const Vec3f center = face.PointOnPlane + face.InwardNormal * testCase.Distance;
            EXPECT_EQ(result->Contains(BoundingSphere(center, 0.5f)), testCase.Expected);
            // Rotation introduces rounding, so test exact tangency only in local space.
            if(std::abs(testCase.Distance) != 0.5f)
            {
                EXPECT_EQ(world.Contains(BoundingSphere(transform * center, 0.5f)),
                    testCase.Expected);
            }
        }
    }
}

TEST(Frustum, Orthographic_Create_IsIndependentOfInputSettings)
{
    auto settings = MakeOrthographicSettings();
    const auto result = Frustum::Create(settings);
    ASSERT_TRUE(result);
    const auto matrix = result->GetMatrix();
    const auto planes = GetPlanes(*result);
    settings = Projection::Orthographic{};
    EXPECT_EQ(result->GetMatrix(), matrix);
    EXPECT_EQ(GetPlanes(*result), planes);
}

TEST(Frustum, Orthographic_Projection_MapsKnownPointsToNdc)
{
    const auto result = Frustum::Create(MakeOrthographicSettings());
    ASSERT_TRUE(result);
    struct Case
    {
        const char* Name;
        Vec3f Point;
        Vec3f Ndc;
        float W;
    };
    const Case cases[] //
        {
            { .Name = "near center", .Point = Vec3f(0, 0, 1), .Ndc = Vec3f(0, 0, 0), .W = 1 },
            { .Name = "near bottom left",
                .Point = Vec3f(-6, -3, 1),
                .Ndc = Vec3f(-1, -1, 0),
                .W = 1 },
            { .Name = "near bottom right",
                .Point = Vec3f(6, -3, 1),
                .Ndc = Vec3f(1, -1, 0),
                .W = 1 },
            { .Name = "near top left", .Point = Vec3f(-6, 3, 1), .Ndc = Vec3f(-1, 1, 0), .W = 1 },
            { .Name = "near top right", .Point = Vec3f(6, 3, 1), .Ndc = Vec3f(1, 1, 0), .W = 1 },
            { .Name = "middle center", .Point = Vec3f(0, 0, 5), .Ndc = Vec3f(0, 0, 0.5f), .W = 1 },
            { .Name = "middle bottom left",
                .Point = Vec3f(-6, -3, 5),
                .Ndc = Vec3f(-1, -1, 0.5f),
                .W = 1 },
            { .Name = "middle bottom right",
                .Point = Vec3f(6, -3, 5),
                .Ndc = Vec3f(1, -1, 0.5f),
                .W = 1 },
            { .Name = "middle top left",
                .Point = Vec3f(-6, 3, 5),
                .Ndc = Vec3f(-1, 1, 0.5f),
                .W = 1 },
            { .Name = "middle top right",
                .Point = Vec3f(6, 3, 5),
                .Ndc = Vec3f(1, 1, 0.5f),
                .W = 1 },
            { .Name = "far center", .Point = Vec3f(0, 0, 9), .Ndc = Vec3f(0, 0, 1), .W = 1 },
            { .Name = "far bottom left",
                .Point = Vec3f(-6, -3, 9),
                .Ndc = Vec3f(-1, -1, 1),
                .W = 1 },
            { .Name = "far bottom right",
                .Point = Vec3f(6, -3, 9),
                .Ndc = Vec3f(1, -1, 1),
                .W = 1 },
            { .Name = "far top left", .Point = Vec3f(-6, 3, 9), .Ndc = Vec3f(-1, 1, 1), .W = 1 },
            { .Name = "far top right", .Point = Vec3f(6, 3, 9), .Ndc = Vec3f(1, 1, 1), .W = 1 },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const Vec4f clip = result->GetMatrix() * Vec4f(testCase.Point, 1);
        ASSERT_GT(clip.w, 0);
        EXPECT_NEAR(clip.w, testCase.W, EPS);
        const Vec3f ndc = clip.xyz() / clip.w;
        EXPECT_NEAR(ndc.x, testCase.Ndc.x, EPS);
        EXPECT_NEAR(ndc.y, testCase.Ndc.y, EPS);
        EXPECT_NEAR(ndc.z, testCase.Ndc.z, EPS);
    }
}

TEST(Frustum, Orthographic_Projection_AspectChangesOnlyHorizontalBounds)
{
    auto settings = MakeOrthographicSettings();
    settings.AspectRatio = 1;
    const auto square = Frustum::Create(settings);
    settings.AspectRatio = 2;
    const auto wide = Frustum::Create(settings);
    ASSERT_TRUE(square);
    ASSERT_TRUE(wide);
    EXPECT_EQ(square->GetTop(), wide->GetTop());
    EXPECT_EQ(square->GetBottom(), wide->GetBottom());
    EXPECT_EQ(square->GetNear(), wide->GetNear());
    EXPECT_EQ(square->GetFar(), wide->GetFar());
    struct Case
    {
        const char* Name;
        Vec3f Center;
    };
    const Case cases[] //
        {
            { .Name = "left", .Center = Vec3f(-4, 0, 4) },
            { .Name = "right", .Center = Vec3f(4, 0, 4) },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const BoundingSphere sphere(testCase.Center, 0.25f);
        EXPECT_EQ(square->Contains(sphere), ContainsResult::Outside);
        EXPECT_EQ(wide->Contains(sphere), ContainsResult::Inside);
    }
}

TEST(Frustum, Orthographic_Create_RejectsInvalidSettings)
{
    const IgnoreFrustumAssertions ignoreAssertions;
    struct Case
    {
        const char* Name{ nullptr };
        Projection::Orthographic Settings;
    };
    const Case cases[] //
        {
            { .Name = "zero height", .Settings = { .Height = 0 } },
            { .Name = "negative height", .Settings = { .Height = -1 } },
            { .Name = "zero aspect", .Settings = { .AspectRatio = 0 } },
            { .Name = "negative aspect", .Settings = { .AspectRatio = -1 } },
            { .Name = "zero near", .Settings = { .NearClip = 0 } },
            { .Name = "negative near", .Settings = { .NearClip = -1 } },
            { .Name = "zero far", .Settings = { .FarClip = 0 } },
            { .Name = "negative far", .Settings = { .FarClip = -1 } },
            { .Name = "equal clips", .Settings = { .NearClip = 2, .FarClip = 2 } },
            { .Name = "reversed clips", .Settings = { .NearClip = 3, .FarClip = 2 } },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_FALSE(Frustum::Create(testCase.Settings));
    }
}

TEST(Frustum, Orthographic_Transform_PreservesContainmentAndAgreesAcrossOverloads)
{
    const auto result = Frustum::Create(MakeOrthographicSettings());
    ASSERT_TRUE(result);
    const auto transform = MakeTranslatedRotatedCameraTransform();
    const auto identity = result->Transform(TrTransformf::Identity());
    const auto world = result->Transform(transform);
    const auto matrixWorld = result->Transform(transform.ToMatrix());
    struct PlaneCase
    {
        const char* Name;
        Vec4f Local;
        Vec4f Identity;
        Vec4f World;
        Vec4f MatrixWorld;
    };
    const PlaneCase planes[] //
        {
            { .Name = "left",
                .Local = result->GetLeft(),
                .Identity = identity.GetLeft(),
                .World = world.GetLeft(),
                .MatrixWorld = matrixWorld.GetLeft() },
            { .Name = "right",
                .Local = result->GetRight(),
                .Identity = identity.GetRight(),
                .World = world.GetRight(),
                .MatrixWorld = matrixWorld.GetRight() },
            { .Name = "top",
                .Local = result->GetTop(),
                .Identity = identity.GetTop(),
                .World = world.GetTop(),
                .MatrixWorld = matrixWorld.GetTop() },
            { .Name = "bottom",
                .Local = result->GetBottom(),
                .Identity = identity.GetBottom(),
                .World = world.GetBottom(),
                .MatrixWorld = matrixWorld.GetBottom() },
            { .Name = "near",
                .Local = result->GetNear(),
                .Identity = identity.GetNear(),
                .World = world.GetNear(),
                .MatrixWorld = matrixWorld.GetNear() },
            { .Name = "far",
                .Local = result->GetFar(),
                .Identity = identity.GetFar(),
                .World = world.GetFar(),
                .MatrixWorld = matrixWorld.GetFar() },
        };
    for(const auto& plane : planes)
    {
        SCOPED_TRACE(plane.Name);
        EXPECT_NEAR((plane.Local - plane.Identity).Length(), 0, EPS);
        EXPECT_NEAR((plane.World - plane.MatrixWorld).Length(), 0, EPS);
        EXPECT_NEAR(plane.World.xyz().Length(), 1, EPS);
    }
    struct Case
    {
        const char* Name;
        Vec3f Center;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside", .Center = Vec3f(0, 0, 4), .Expected = ContainsResult::Inside },
            { .Name = "outside", .Center = Vec3f(0, 0, 10), .Expected = ContainsResult::Outside },
            { .Name = "left", .Center = Vec3f(-6, 0, 5), .Expected = ContainsResult::Intersects },
            { .Name = "right", .Center = Vec3f(6, 0, 5), .Expected = ContainsResult::Intersects },
            { .Name = "top", .Center = Vec3f(0, 3, 5), .Expected = ContainsResult::Intersects },
            { .Name = "bottom", .Center = Vec3f(0, -3, 5), .Expected = ContainsResult::Intersects },
            { .Name = "near", .Center = Vec3f(0, 0, 1), .Expected = ContainsResult::Intersects },
            { .Name = "far", .Center = Vec3f(0, 0, 9), .Expected = ContainsResult::Intersects },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const BoundingSphere localSphere(testCase.Center, 0.25f);
        const BoundingSphere worldSphere(transform * testCase.Center, 0.25f);
        EXPECT_EQ(result->Contains(localSphere), testCase.Expected);
        EXPECT_EQ(identity.Contains(localSphere), testCase.Expected);
        EXPECT_EQ(world.Contains(worldSphere), testCase.Expected);
        EXPECT_EQ(matrixWorld.Contains(worldSphere), testCase.Expected);
    }
}

TEST(Frustum, Orthographic_Transform_NonuniformScaleKeepsPlanesNormalized)
{
    const auto result = Frustum::Create(MakeOrthographicSettings());
    ASSERT_TRUE(result);
    TrsTransformf transform;
    transform.S = Vec3f(2, 3, 4);
    const auto scaled = result->Transform(transform.ToMatrix());
    struct PlaneCase
    {
        const char* Name;
        Vec4f Plane;
    };
    const PlaneCase planes[] //
        {
            { .Name = "left", .Plane = scaled.GetLeft() },
            { .Name = "right", .Plane = scaled.GetRight() },
            { .Name = "top", .Plane = scaled.GetTop() },
            { .Name = "bottom", .Plane = scaled.GetBottom() },
            { .Name = "near", .Plane = scaled.GetNear() },
            { .Name = "far", .Plane = scaled.GetFar() },
        };
    for(const auto& plane : planes)
    {
        SCOPED_TRACE(plane.Name);
        EXPECT_NEAR(plane.Plane.xyz().Length(), 1, EPS);
    }
    struct Case
    {
        const char* Name;
        Vec3f Center;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside", .Center = Vec3f(0, 0, 16), .Expected = ContainsResult::Inside },
            { .Name = "outside", .Center = Vec3f(0, 0, 40), .Expected = ContainsResult::Outside },
            { .Name = "left", .Center = Vec3f(-12, 0, 20), .Expected = ContainsResult::Intersects },
            { .Name = "right", .Center = Vec3f(12, 0, 20), .Expected = ContainsResult::Intersects },
            { .Name = "top", .Center = Vec3f(0, 9, 20), .Expected = ContainsResult::Intersects },
            { .Name = "bottom",
                .Center = Vec3f(0, -9, 20),
                .Expected = ContainsResult::Intersects },
            { .Name = "near", .Center = Vec3f(0, 0, 4), .Expected = ContainsResult::Intersects },
            { .Name = "far", .Center = Vec3f(0, 0, 36), .Expected = ContainsResult::Intersects },
            { .Name = "near external overlap",
                .Center = Vec3f(0, 0, 3.75f),
                .Expected = ContainsResult::Intersects },
            { .Name = "far external overlap",
                .Center = Vec3f(0, 0, 36.25f),
                .Expected = ContainsResult::Intersects },
        };
    for(const auto& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_EQ(scaled.Contains(BoundingSphere(testCase.Center, 0.5f)), testCase.Expected);
    }
}

TEST(Frustum, Selection_ContainsOnlySelectedScreenRegion)
{
    const auto settings = MakePerspectiveSettings();
    const auto frustumResult = Frustum::Create(settings);
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum fullFrustum = localFrustum.Transform(cameraXform);
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const auto selectionFrustumResult = Frustum::Create(settings, selectRect, 137, 89);
    ASSERT_TRUE(selectionFrustumResult);
    const Frustum selectionFrustum = selectionFrustumResult->Transform(cameraXform);

    const Vec3f selectedPoint =
        WorldFromScreenPoint(localFrustum, cameraXform, Vec2f(54.5f, 38.5f), 0.43f, 137, 89);
    const Vec3f outsideSelectionPoint =
        WorldFromScreenPoint(localFrustum, cameraXform, Vec2f(14.7f, 39.3f), 0.43f, 137, 89);

    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)), ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Outside);
}

TEST(Frustum, Selection_FullScreenMatchesFullFrustum)
{
    const auto settings = MakePerspectiveSettings();
    const auto frustumResult = Frustum::Create(settings);
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum fullFrustum = localFrustum.Transform(cameraXform);
    const Rect selectRect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    const auto selectionFrustumResult = Frustum::Create(settings, selectRect, 137, 89);
    ASSERT_TRUE(selectionFrustumResult);
    const Frustum selectionFrustum = selectionFrustumResult->Transform(cameraXform);

    struct Case
    {
        const char* Name;
        Vec3f CameraPoint;
        float Radius;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside",
                .CameraPoint = Vec3f(0.31f, -0.27f, 6.4f),
                .Radius = 0.43f,
                .Expected = ContainsResult::Inside },
            { .Name = "before near",
                .CameraPoint = Vec3f(0, 0, 0.2f),
                .Radius = 0.1f,
                .Expected = ContainsResult::Outside },
            { .Name = "beyond far",
                .CameraPoint = Vec3f(0, 0, 19.1f),
                .Radius = 0.31f,
                .Expected = ContainsResult::Outside },
            { .Name = "left",
                .CameraPoint = Vec3f(-9.7f, 0, 6.2f),
                .Radius = 0.37f,
                .Expected = ContainsResult::Outside },
            { .Name = "right",
                .CameraPoint = Vec3f(9.7f, 0, 6.2f),
                .Radius = 0.37f,
                .Expected = ContainsResult::Outside },
            { .Name = "bottom",
                .CameraPoint = Vec3f(0, -7, 6.2f),
                .Radius = 0.37f,
                .Expected = ContainsResult::Outside },
            { .Name = "top",
                .CameraPoint = Vec3f(0, 7, 6.2f),
                .Radius = 0.37f,
                .Expected = ContainsResult::Outside },
            { .Name = "near intersection",
                .CameraPoint = Vec3f(0, 0, 0.71f),
                .Radius = 0.1f,
                .Expected = ContainsResult::Intersects },
            { .Name = "far intersection",
                .CameraPoint = Vec3f(0, 0, 18.1f),
                .Radius = 0.73f,
                .Expected = ContainsResult::Intersects },
        };
    for(const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const BoundingSphere sphere(cameraXform * testCase.CameraPoint, testCase.Radius);
        EXPECT_EQ(fullFrustum.Contains(sphere), testCase.Expected);
        EXPECT_EQ(selectionFrustum.Contains(sphere), testCase.Expected);
    }
}

TEST(Frustum, Selection_ScalingScreenAndRectanglePreservesSelection)
{
    const auto settings = MakePerspectiveSettings();
    const auto frustumResult = Frustum::Create(settings);
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const Rect scaledRect({ .X = 108, .Y = 72, .Width = 111, .Height = 87 });
    const auto selectionFrustumResult = Frustum::Create(settings, selectRect, 137, 89);
    ASSERT_TRUE(selectionFrustumResult);
    const Frustum selectionFrustum = selectionFrustumResult->Transform(cameraXform);
    const auto scaledFrustumResult = Frustum::Create(settings, scaledRect, 411, 267);
    ASSERT_TRUE(scaledFrustumResult);
    const Frustum scaledFrustum = scaledFrustumResult->Transform(cameraXform);

    struct Case
    {
        const char* Name;
        Vec2f ScreenPoint;
        float NdcDepth;
        ContainsResult Expected;
    };
    const Case cases[] //
        {
            { .Name = "inside",
                .ScreenPoint = Vec2f(54.5f, 38.5f),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Inside },
            { .Name = "left",
                .ScreenPoint = Vec2f(14.7f, 39.3f),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Outside },
            { .Name = "right",
                .ScreenPoint = Vec2f(94, 38.5f),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Outside },
            { .Name = "top",
                .ScreenPoint = Vec2f(54.5f, 10),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Outside },
            { .Name = "bottom",
                .ScreenPoint = Vec2f(54.5f, 75),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Outside },
            { .Name = "left intersection",
                .ScreenPoint = Vec2f(36, 38.5f),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Intersects },
            { .Name = "right intersection",
                .ScreenPoint = Vec2f(73, 38.5f),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Intersects },
            { .Name = "top intersection",
                .ScreenPoint = Vec2f(54.5f, 24),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Intersects },
            { .Name = "bottom intersection",
                .ScreenPoint = Vec2f(54.5f, 53),
                .NdcDepth = 0.43f,
                .Expected = ContainsResult::Intersects },
            { .Name = "before near",
                .ScreenPoint = Vec2f(54.5f, 38.5f),
                .NdcDepth = -1.0f,
                .Expected = ContainsResult::Outside },
            { .Name = "beyond far",
                .ScreenPoint = Vec2f(54.5f, 38.5f),
                .NdcDepth = 1.001f,
                .Expected = ContainsResult::Outside },
            { .Name = "near intersection",
                .ScreenPoint = Vec2f(54.5f, 38.5f),
                .NdcDepth = 0.0f,
                .Expected = ContainsResult::Intersects },
            { .Name = "far intersection",
                .ScreenPoint = Vec2f(54.5f, 38.5f),
                .NdcDepth = 1.0f,
                .Expected = ContainsResult::Intersects },
        };
    for(const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        const Vec3f worldPoint = WorldFromScreenPoint(localFrustum,
            cameraXform,
            testCase.ScreenPoint,
            testCase.NdcDepth,
            137,
            89);
        const BoundingSphere sphere(worldPoint, 0.017f);
        EXPECT_EQ(selectionFrustum.Contains(sphere), testCase.Expected);
        EXPECT_EQ(scaledFrustum.Contains(sphere), testCase.Expected);
    }
}

TEST(Frustum, Selection_Create_RejectsZeroScreenDimensions)
{
    const IgnoreFrustumAssertions ignoreAssertions;
    const auto settings = MakePerspectiveSettings();
    const Rect rect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    EXPECT_FALSE(Frustum::Create(settings, rect, 0, 89));
    EXPECT_FALSE(Frustum::Create(settings, rect, 137, 0));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
