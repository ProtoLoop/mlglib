#include "BoundingVolumes.h"
#include "Frustum.h"

#include <gtest/gtest.h>
#include <SDL3/SDL_assert.h>

#include <array>
#include <numbers>

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

ProjectionSettings
MakeNonDefaultSettings()
{
    return { .Fov = Radiansf::FromDegrees(73.0f),
        .AspectRatio = 137.0f / 89.0f,
        .NearClip = 0.71f,
        .FarClip = 18.3f };
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

TEST(Frustum, Contains_ReturnsInsideForSphereInsideCameraFrustum)
{
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum frustum = localFrustum.Transform(cameraXform);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(cameraXform * Vec3f(0.31f, -0.27f, 6.4f), 0.43f)),
        ContainsResult::Inside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(cameraXform * Vec3f(1.7f, 0.83f, 9.2f), 0.61f)),
        ContainsResult::Inside);
}

TEST(Frustum, Contains_ReturnsOutsideForSphereOutsideCameraFrustum)
{
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum frustum = localFrustum.Transform(cameraXform);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(cameraXform * Vec3f(0.19f, -0.11f, 19.1f), 0.31f)),
        ContainsResult::Outside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(cameraXform * Vec3f(9.7f, 0.41f, 6.2f), 0.37f)),
        ContainsResult::Outside);
}

TEST(Frustum, Contains_ReturnsIntersectsForSphereStraddlingFrustumPlanes)
{
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum frustum = localFrustum.Transform(cameraXform);

    const Vec3f interiorPoint = cameraXform * Vec3f(0.31f, -0.27f, 6.4f);
    const Vec4f& leftPlane = frustum.GetLeft();
    const float leftInteriorDistance = SignedDistanceToPlane(leftPlane, interiorPoint);
    const Vec3f leftStraddlingCenter =
        interiorPoint - (leftPlane.xyz() * (leftInteriorDistance + 0.29f));

    EXPECT_LT(SignedDistanceToPlane(leftPlane, leftStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(leftStraddlingCenter, 0.67f)),
        ContainsResult::Intersects);

    const Vec4f& farPlane = frustum.GetFar();
    const float farInteriorDistance = SignedDistanceToPlane(farPlane, interiorPoint);
    const Vec3f farStraddlingCenter =
        interiorPoint - (farPlane.xyz() * (farInteriorDistance + 0.37f));
    EXPECT_LT(SignedDistanceToPlane(frustum.GetFar(), farStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(farStraddlingCenter, 0.73f)),
        ContainsResult::Intersects);
}

TEST(Frustum, Contains_ClassifiesNearAndFarBoundaryDistances)
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

TEST(Frustum, Create_PlanesMatchKnownGeometry)
{
    const auto result = Frustum::Create({ .Fov = Radiansf::FromDegrees(90.0f),
        .AspectRatio = 2.0f,
        .NearClip = 1.0f,
        .FarClip = 8.0f });
    ASSERT_TRUE(result);
    const Frustum& frustum = *result;

    struct Case
    {
        const char* Name;
        Vec4f Plane;
        Vec3f PointOnPlane;
        Vec3f InwardNormal;
    };
    const Case cases[] //
        {
            { .Name = "left",
                .Plane = frustum.GetLeft(),
                .PointOnPlane = Vec3f(-8, 0, 4),
                .InwardNormal = Vec3f(1, 0, 2).Normalize() },
            { .Name = "right",
                .Plane = frustum.GetRight(),
                .PointOnPlane = Vec3f(8, 0, 4),
                .InwardNormal = Vec3f(-1, 0, 2).Normalize() },
            { .Name = "top",
                .Plane = frustum.GetTop(),
                .PointOnPlane = Vec3f(0, 4, 4),
                .InwardNormal = Vec3f(0, -1, 1).Normalize() },
            { .Name = "bottom",
                .Plane = frustum.GetBottom(),
                .PointOnPlane = Vec3f(0, -4, 4),
                .InwardNormal = Vec3f(0, 1, 1).Normalize() },
            { .Name = "near",
                .Plane = frustum.GetNear(),
                .PointOnPlane = Vec3f(0, 0, 1),
                .InwardNormal = Vec3f(0, 0, 1) },
            { .Name = "far",
                .Plane = frustum.GetFar(),
                .PointOnPlane = Vec3f(0, 0, 8),
                .InwardNormal = Vec3f(0, 0, -1) },
        };
    for(const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.Name);
        EXPECT_NEAR(SignedDistanceToPlane(testCase.Plane, testCase.PointOnPlane), 0.0f, EPS);
        EXPECT_NEAR(testCase.Plane.x, testCase.InwardNormal.x, EPS);
        EXPECT_NEAR(testCase.Plane.y, testCase.InwardNormal.y, EPS);
        EXPECT_NEAR(testCase.Plane.z, testCase.InwardNormal.z, EPS);
    }
}

TEST(Frustum, Selection_ContainsOnlySelectedScreenRegion)
{
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum fullFrustum = localFrustum.Transform(cameraXform);
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const auto selectionFrustumResult = Frustum::Create(localFrustum.GetSettings(), selectRect, 137, 89);
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
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Frustum fullFrustum = localFrustum.Transform(cameraXform);
    const Rect selectRect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    const auto selectionFrustumResult = Frustum::Create(localFrustum.GetSettings(), selectRect, 137, 89);
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
        const BoundingSphere sphere(cameraXform * testCase.CameraPoint,
            testCase.Radius);
        EXPECT_EQ(fullFrustum.Contains(sphere), testCase.Expected);
        EXPECT_EQ(selectionFrustum.Contains(sphere), testCase.Expected);
    }
}

TEST(Frustum, Selection_ScalingScreenAndRectanglePreservesSelection)
{
    const auto frustumResult = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(frustumResult);
    const Frustum& localFrustum = *frustumResult;
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const Rect scaledRect({ .X = 108, .Y = 72, .Width = 111, .Height = 87 });
    const auto selectionFrustumResult = Frustum::Create(localFrustum.GetSettings(), selectRect, 137, 89);
    ASSERT_TRUE(selectionFrustumResult);
    const Frustum selectionFrustum = selectionFrustumResult->Transform(cameraXform);
    const auto scaledFrustumResult = Frustum::Create(localFrustum.GetSettings(), scaledRect, 411, 267);
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


TEST(Frustum, DefaultConstruction_UsesDefaultPerspective)
{
    const Frustum frustum;
    const auto& settings = frustum.GetSettings();
    EXPECT_NEAR(settings.Fov.GetValue(), Radiansf::FromDegrees(45.0f).GetValue(), EPS);
    EXPECT_FLOAT_EQ(settings.AspectRatio, 1.0f);
    EXPECT_FLOAT_EQ(settings.NearClip, 0.1f);
    EXPECT_FLOAT_EQ(settings.FarClip, 1000.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(Vec3f(0, 0, 5), 0.1f)), ContainsResult::Inside);
}

TEST(Frustum, Create_CopiesCustomSettings)
{
    auto settings = MakeNonDefaultSettings();
    const auto expected = settings;
    const auto result = Frustum::Create(settings);
    ASSERT_TRUE(result);
    settings = ProjectionSettings{};
    EXPECT_FLOAT_EQ(result->GetSettings().Fov.GetValue(), expected.Fov.GetValue());
    EXPECT_FLOAT_EQ(result->GetSettings().AspectRatio, expected.AspectRatio);
    EXPECT_FLOAT_EQ(result->GetSettings().NearClip, expected.NearClip);
    EXPECT_FLOAT_EQ(result->GetSettings().FarClip, expected.FarClip);
}

TEST(Frustum, Projection_MapsKnownPointsToNdc)
{
    const auto result = Frustum::Create({ .Fov = Radiansf::FromDegrees(90.0f),
        .AspectRatio = 2.0f,
        .NearClip = 1.0f,
        .FarClip = 8.0f });
    ASSERT_TRUE(result);
    struct Case
    {
        Vec3f Point;
        Vec3f Ndc;
    };
    const Case cases[] //
        {
            { .Point = Vec3f(0, 0, 1), .Ndc = Vec3f(0, 0, 0) },
            { .Point = Vec3f(0, 0, 8), .Ndc = Vec3f(0, 0, 1) },
            { .Point = Vec3f(8, 4, 4), .Ndc = Vec3f(1, 1, 6.0f / 7.0f) },
            { .Point = Vec3f(-8, -4, 4), .Ndc = Vec3f(-1, -1, 6.0f / 7.0f) },
        };
    for(const auto& testCase : cases)
    {
        const Vec4f clip = result->GetMatrix() * Vec4f(testCase.Point, 1);
        ASSERT_GT(clip.w, 0);
        const Vec3f ndc = clip.xyz() / clip.w;
        EXPECT_NEAR(ndc.x, testCase.Ndc.x, EPS);
        EXPECT_NEAR(ndc.y, testCase.Ndc.y, EPS);
        EXPECT_NEAR(ndc.z, testCase.Ndc.z, EPS);
    }
}

TEST(Frustum, Create_RejectsInvalidSettingsInBothFactories)
{
    const IgnoreFrustumAssertions ignoreAssertions;
    const Rect rect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    const auto valid = MakeNonDefaultSettings();
    struct Case
    {
        const char* Name{""};
        ProjectionSettings Settings;
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
    EXPECT_FALSE(Frustum::Create(valid, rect, 0, 89));
    EXPECT_FALSE(Frustum::Create(valid, rect, 137, 0));
}

TEST(Frustum, Transform_PreservesContainmentAndAgreesAcrossOverloads)
{
    const auto result = Frustum::Create(MakeNonDefaultSettings());
    ASSERT_TRUE(result);
    const Frustum& local = *result;
    const auto transform = MakeTranslatedRotatedCameraTransform();
    const Frustum identity = local.Transform(TrTransformf::Identity);
    const Frustum world = local.Transform(transform);
    const Frustum matrixWorld = local.Transform(transform.ToMatrix());
    const auto localPlanes = GetPlanes(local);
    const auto identityPlanes = GetPlanes(identity);
    const auto worldPlanes = GetPlanes(world);
    const auto matrixPlanes = GetPlanes(matrixWorld);
    for(size_t i = 0; i < localPlanes.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_NEAR((localPlanes[i] - identityPlanes[i]).Length(), 0, EPS);
        EXPECT_NEAR((worldPlanes[i] - matrixPlanes[i]).Length(), 0, EPS);
        EXPECT_NEAR(worldPlanes[i].xyz().Length(), 1, EPS);
    }
    const Vec3f centers[] = //
        {
            Vec3f(0, 0, 5),
            Vec3f(0, 0, 30),
            Vec3f(0, 0, local.GetSettings().NearClip),
            Vec3f(0, 0, local.GetSettings().FarClip),
        };
    for(const auto& center : centers)
    {
        const auto expected = local.Contains(BoundingSphere(center, 0.1f));
        EXPECT_EQ(identity.Contains(BoundingSphere(center, 0.1f)), expected);
        EXPECT_EQ(world.Contains(BoundingSphere(transform * center, 0.1f)), expected);
        EXPECT_EQ(matrixWorld.Contains(BoundingSphere(transform * center, 0.1f)), expected);
    }
}

TEST(Frustum, Transform_NonuniformScaleKeepsPlanesNormalized)
{
    const auto result = Frustum::Create({ .Fov = Radiansf::FromDegrees(90.0f),
        .AspectRatio = 1.0f,
        .NearClip = 1.0f,
        .FarClip = 8.0f });
    ASSERT_TRUE(result);
    TrsTransformf transform;
    transform.S = Vec3f(2, 3, 4);
    const Frustum scaled = result->Transform(transform.ToMatrix());
    const auto planes = GetPlanes(scaled);
    for(const auto& plane : planes)
    {
        EXPECT_NEAR(plane.xyz().Length(), 1, EPS);
    }
    // Scaling moves the near/far planes to z=4 and z=32.
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(0, 0, 16), 0.5f)), ContainsResult::Inside);
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(0, 0, 3.75f), 0.5f)), ContainsResult::Intersects);
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(0, 0, 32.25f), 0.5f)), ContainsResult::Intersects);
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(0, 0, 33), 0.5f)), ContainsResult::Outside);
    // At z=16 the side boundaries are x=+/-8 and y=+/-12.
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(8, 0, 16), 0.5f)), ContainsResult::Intersects);
    EXPECT_EQ(scaled.Contains(BoundingSphere(Vec3f(0, 12, 16), 0.5f)), ContainsResult::Intersects);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
