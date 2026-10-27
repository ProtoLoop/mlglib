#include "BoundingVolumes.h"
#include "Camera.h"
#include "Frustum.h"
#include "Projection.h"

#include <gtest/gtest.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
constexpr float EPS = 1e-5f;
using ContainsResult = Frustum::ContainsResult;

void
ExpectMatrixNear(const Mat44f& actual, const Mat44f& expected)
{
    for(size_t col = 0; col < 4; ++col)
    {
        for(size_t row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(actual[col][row], expected[col][row], EPS)
                << "column "
                << col
                << ", row "
                << row;
        }
    }
}

float
SignedDistanceToPlane(const Vec4f& plane, const Vec3f& point)
{
    return plane.Dot(Vec4f(point, 1.0f));
}

Vec3f
PlaneNormal(const Vec4f& plane)
{
    return Vec3f(plane.x, plane.y, plane.z);
}

PerspectiveProjection
MakeNonDefaultProjection()
{
    PerspectiveProjection projection(137.0f / 89.0f);
    projection.SetFov(Radiansf::FromDegrees(73.0f)).SetNearClip(0.71f).SetFarClip(18.3f);
    return projection;
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

Vec3f
WorldFromCameraPoint(const TrTransformf& cameraXform, const Vec3f& cameraPoint)
{
    return cameraXform * cameraPoint;
}

Vec2f
ScreenToNdcForTest(const Vec2f& screenPos, const uint32_t screenWidth, const uint32_t screenHeight)
{
    const float x = screenPos.x / static_cast<float>(screenWidth);
    const float y = screenPos.y / static_cast<float>(screenHeight);
    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}

Vec3f
WorldFromScreenPoint(const PerspectiveProjection& projection,
    const TrTransformf& cameraXform,
    const Vec2f& screenPos,
    const float ndcDepth,
    const uint32_t screenWidth,
    const uint32_t screenHeight)
{
    const Vec2f ndc = ScreenToNdcForTest(screenPos, screenWidth, screenHeight);
    const Mat44f invViewProj = cameraXform.ToMatrix() * projection.GetMatrix().Inverse();
    const Vec4f world = invViewProj * Vec4f(ndc, ndcDepth, 1.0f);
    return world.xyz() / world.w;
}
} // namespace

TEST(PerspectiveProjection, Constructor_UsesProvidedAspectRatioWithDefaultProjection)
{
    const PerspectiveProjection projection(853.0f / 497.0f);

    EXPECT_NEAR(projection.GetFov().GetValue(), Radiansf::FromDegrees(45.0f).GetValue(), EPS);
    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), 853.0f / 497.0f);
    EXPECT_FLOAT_EQ(projection.GetNearClip(), 0.1f);
    EXPECT_FLOAT_EQ(projection.GetFarClip(), 1000.0f);
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(projection.GetFov(),
            projection.GetAspectRatio(),
            projection.GetNearClip(),
            projection.GetFarClip()));
}

TEST(PerspectiveProjection, Setters_SupportChaining)
{
    PerspectiveProjection projection(1280.0f / 720.0f);
    const Radiansf fov = Radiansf::FromDegrees(57.0f);
    const float aspectRatio = 1031.0f / 587.0f;
    const float nearClip = 0.37f;
    const float farClip = 263.0f;

    const PerspectiveProjection& result = projection.SetFov(fov)
                                        .SetAspectRatio(aspectRatio)
                                        .SetNearClip(nearClip)
                                        .SetFarClip(farClip);

    EXPECT_EQ(&result, &projection);

    EXPECT_NEAR(projection.GetFov().GetValue(), fov.GetValue(), EPS);
    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), aspectRatio);
    EXPECT_FLOAT_EQ(projection.GetNearClip(), nearClip);
    EXPECT_FLOAT_EQ(projection.GetFarClip(), farClip);
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(fov, aspectRatio, nearClip, farClip));
}

TEST(PerspectiveProjection, SetFov_UpdatesProjectionAndPreservesOtherParams)
{
    PerspectiveProjection projection = MakeNonDefaultProjection();
    const PerspectiveProjection before = projection;
    const Radiansf value = Radiansf::FromDegrees(68.0f);

    projection.SetFov(value);

    EXPECT_FLOAT_EQ(projection.GetFov().GetValue(), value.GetValue());
    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), before.GetAspectRatio());
    EXPECT_FLOAT_EQ(projection.GetNearClip(), before.GetNearClip());
    EXPECT_FLOAT_EQ(projection.GetFarClip(), before.GetFarClip());
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(value,
            before.GetAspectRatio(),
            before.GetNearClip(),
            before.GetFarClip()));
}

TEST(PerspectiveProjection, SetNearClip_UpdatesProjectionAndPreservesOtherParams)
{
    PerspectiveProjection projection = MakeNonDefaultProjection();
    const PerspectiveProjection before = projection;
    const float value = 0.43f;

    projection.SetNearClip(value);

    EXPECT_FLOAT_EQ(projection.GetFov().GetValue(), before.GetFov().GetValue());
    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), before.GetAspectRatio());
    EXPECT_FLOAT_EQ(projection.GetNearClip(), value);
    EXPECT_FLOAT_EQ(projection.GetFarClip(), before.GetFarClip());
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(before.GetFov(),
            before.GetAspectRatio(),
            value,
            before.GetFarClip()));
}

TEST(PerspectiveProjection, SetFarClip_UpdatesProjectionAndPreservesOtherParams)
{
    PerspectiveProjection projection = MakeNonDefaultProjection();
    const PerspectiveProjection before = projection;
    const float value = 617.0f;

    projection.SetFarClip(value);

    EXPECT_FLOAT_EQ(projection.GetFov().GetValue(), before.GetFov().GetValue());
    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), before.GetAspectRatio());
    EXPECT_FLOAT_EQ(projection.GetNearClip(), before.GetNearClip());
    EXPECT_FLOAT_EQ(projection.GetFarClip(), value);
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(before.GetFov(),
            before.GetAspectRatio(),
            before.GetNearClip(),
            value));
}

TEST(PerspectiveProjection, SetAspectRatio_UpdatesProjectionAndPreservesOtherParams)
{
    PerspectiveProjection projection = MakeNonDefaultProjection();
    const Radiansf fov = projection.GetFov();
    const float nearClip = projection.GetNearClip();
    const float farClip = projection.GetFarClip();

    projection.SetAspectRatio(317.0f / 907.0f);

    EXPECT_FLOAT_EQ(projection.GetAspectRatio(), 317.0f / 907.0f);
    EXPECT_FLOAT_EQ(projection.GetFov().GetValue(), fov.GetValue());
    EXPECT_FLOAT_EQ(projection.GetNearClip(), nearClip);
    EXPECT_FLOAT_EQ(projection.GetFarClip(), farClip);
    ExpectMatrixNear(projection.GetMatrix(),
        Mat44f::PerspectiveLH(fov, 317.0f / 907.0f, nearClip, farClip));
}

TEST(Camera, ViewTransform_MapsCameraPositionToOrigin)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf transform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, transform);

    const Vec3f origin = camera.GetViewTransform() * transform.T;
    EXPECT_NEAR(origin.x, 0.0f, EPS);
    EXPECT_NEAR(origin.y, 0.0f, EPS);
    EXPECT_NEAR(origin.z, 0.0f, EPS);
}

TEST(Camera, ViewProjection_ProjectsForwardPointToScreenCenter)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf transform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, transform);
    const Vec3f worldPoint = transform * Vec3f(0.0f, 0.0f, 6.0f);

    const Vec3f viewPoint = camera.GetViewTransform() * worldPoint;
    EXPECT_NEAR(viewPoint.x, 0.0f, EPS);
    EXPECT_NEAR(viewPoint.y, 0.0f, EPS);
    EXPECT_NEAR(viewPoint.z, 6.0f, EPS);

    const Vec4f clip = camera.GetViewProj() * Vec4f(worldPoint, 1.0f);
    ASSERT_GT(clip.w, 0.0f);
    EXPECT_NEAR(clip.x / clip.w, 0.0f, EPS);
    EXPECT_NEAR(clip.y / clip.w, 0.0f, EPS);
    EXPECT_GT(clip.z / clip.w, 0.0f);
    EXPECT_LT(clip.z / clip.w, 1.0f);
}

TEST(Camera, Constructor_CopiesProjectionAndTransform)
{
    PerspectiveProjection projection = MakeNonDefaultProjection();
    TrTransformf transform = MakeTranslatedRotatedCameraTransform();
    const PerspectiveProjection before = projection;
    const TrTransformf originalTransform = transform;
    const Camera camera(projection, transform);
    const Mat44f originalView = camera.GetViewTransform().ToMatrix();
    const Mat44f originalViewProj = camera.GetViewProj();

    projection.SetFov(Radiansf::FromDegrees(51.0f))
        .SetAspectRatio(0.75f)
        .SetNearClip(1.0f)
        .SetFarClip(40.0f);
    transform.T = Vec3f(-10.0f, 12.0f, 3.0f);
    transform.R = UnitQuatf(Radiansf::FromDegrees(82.0f), Vec3f::XAXIS());

    EXPECT_FLOAT_EQ(camera.GetProjection().GetFov().GetValue(), before.GetFov().GetValue());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetAspectRatio(), before.GetAspectRatio());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetNearClip(), before.GetNearClip());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetFarClip(), before.GetFarClip());
    ExpectMatrixNear(camera.GetProjection().GetMatrix(), before.GetMatrix());
    ExpectMatrixNear(camera.GetWorldSpaceTransform().ToMatrix(), originalTransform.ToMatrix());
    ExpectMatrixNear(camera.GetViewTransform().ToMatrix(), originalView);
    ExpectMatrixNear(camera.GetViewProj(), originalViewProj);
}

TEST(Camera, SetProjection_UpdatesViewProjectionAndPreservesTransform)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf transform = MakeTranslatedRotatedCameraTransform();
    Camera camera(projection, transform);
    const Mat44f originalView = camera.GetViewTransform().ToMatrix();
    ExpectMatrixNear(camera.GetViewProj(), projection.GetMatrix() * originalView);

    PerspectiveProjection replacement(0.75f);
    replacement.SetFov(Radiansf::FromDegrees(51.0f)).SetNearClip(1.0f).SetFarClip(40.0f);
    const PerspectiveProjection expected = replacement;
    camera.SetProjection(replacement);

    // The setter stores a copy, independent of subsequent changes to its argument.
    replacement.SetFov(Radiansf::FromDegrees(82.0f)).SetAspectRatio(2.0f);

    EXPECT_FLOAT_EQ(camera.GetProjection().GetFov().GetValue(), expected.GetFov().GetValue());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetAspectRatio(), expected.GetAspectRatio());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetNearClip(), expected.GetNearClip());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetFarClip(), expected.GetFarClip());
    ExpectMatrixNear(camera.GetProjection().GetMatrix(), expected.GetMatrix());
    ExpectMatrixNear(camera.GetWorldSpaceTransform().ToMatrix(), transform.ToMatrix());
    ExpectMatrixNear(camera.GetViewTransform().ToMatrix(), originalView);
    ExpectMatrixNear(camera.GetViewProj(), expected.GetMatrix() * originalView);
}

TEST(Camera, SetWorldSpaceTransform_UpdatesViewAndViewProjectionAndPreservesProjection)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf originalTransform = MakeTranslatedRotatedCameraTransform();
    Camera camera(projection, originalTransform);
    const Mat44f originalView = originalTransform.Inverse().ToMatrix();
    ExpectMatrixNear(camera.GetViewTransform().ToMatrix(), originalView);
    ExpectMatrixNear(camera.GetViewProj(), projection.GetMatrix() * originalView);

    TrTransformf replacement;
    replacement.T = Vec3f(-10.0f, 12.0f, 3.0f);
    replacement.R = UnitQuatf(Radiansf::FromDegrees(82.0f), Vec3f::XAXIS());
    const TrTransformf expected = replacement;
    camera.SetWorldSpaceTransform(replacement);

    replacement = originalTransform;

    EXPECT_FLOAT_EQ(camera.GetProjection().GetFov().GetValue(), projection.GetFov().GetValue());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetAspectRatio(), projection.GetAspectRatio());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetNearClip(), projection.GetNearClip());
    EXPECT_FLOAT_EQ(camera.GetProjection().GetFarClip(), projection.GetFarClip());
    ExpectMatrixNear(camera.GetProjection().GetMatrix(), projection.GetMatrix());
    ExpectMatrixNear(camera.GetWorldSpaceTransform().ToMatrix(), expected.ToMatrix());
    ExpectMatrixNear(camera.GetViewTransform().ToMatrix(), expected.Inverse().ToMatrix());
    ExpectMatrixNear(camera.GetViewProj(), projection.GetMatrix() * expected.Inverse().ToMatrix());

    const Vec3f origin = camera.GetViewTransform() * expected.T;
    EXPECT_NEAR(origin.x, 0.0f, EPS);
    EXPECT_NEAR(origin.y, 0.0f, EPS);
    EXPECT_NEAR(origin.z, 0.0f, EPS);
}

TEST(Frustum, Contains_ReturnsInsideForSphereInsideCameraFrustum)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Frustum frustum(camera);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(0.31f, -0.27f, 6.4f)), 0.43f)),
        ContainsResult::Inside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(1.7f, 0.83f, 9.2f)), 0.61f)),
        ContainsResult::Inside);
}

TEST(Frustum, Contains_ReturnsOutsideForSphereOutsideCameraFrustum)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Frustum frustum(camera);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(0.19f, -0.11f, 19.1f)), 0.31f)),
        ContainsResult::Outside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(9.7f, 0.41f, 6.2f)), 0.37f)),
        ContainsResult::Outside);
}

TEST(Frustum, Contains_ReturnsIntersectsForSphereStraddlingFrustumPlanes)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Frustum frustum(camera);

    const Vec3f interiorPoint = WorldFromCameraPoint(cameraXform, Vec3f(0.31f, -0.27f, 6.4f));
    const Vec4f& leftPlane = frustum.GetLeft();
    const float leftInteriorDistance = SignedDistanceToPlane(leftPlane, interiorPoint);
    const Vec3f leftStraddlingCenter =
        interiorPoint - (PlaneNormal(leftPlane) * (leftInteriorDistance + 0.29f));

    EXPECT_LT(SignedDistanceToPlane(leftPlane, leftStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(leftStraddlingCenter, 0.67f)),
        ContainsResult::Intersects);

    const Vec4f& farPlane = frustum.GetFar();
    const float farInteriorDistance = SignedDistanceToPlane(farPlane, interiorPoint);
    const Vec3f farStraddlingCenter =
        interiorPoint - (PlaneNormal(farPlane) * (farInteriorDistance + 0.37f));
    EXPECT_LT(SignedDistanceToPlane(frustum.GetFar(), farStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(farStraddlingCenter, 0.73f)),
        ContainsResult::Intersects);
}

TEST(Frustum, Contains_ClassifiesNearAndFarBoundaryDistances)
{
    // These clip distances produce exactly representable near/far plane coefficients.
    PerspectiveProjection projection(2.0f);
    projection.SetFov(Radiansf::FromDegrees(90.0f)).SetNearClip(1.0f).SetFarClip(2.0f);
    const Camera camera(projection, TrTransformf{});
    const Frustum frustum(camera);

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

TEST(Frustum, Constructor_PlanesMatchKnownCameraGeometry)
{
    PerspectiveProjection projection(2.0f);
    projection.SetFov(Radiansf::FromDegrees(90.0f)).SetNearClip(1.0f).SetFarClip(8.0f);
    const Camera camera(projection, TrTransformf{});
    const Frustum frustum(camera);

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

TEST(Frustum, SelectionRectConstructor_ContainsOnlySelectedScreenRegion)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Frustum fullFrustum(camera);
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const Frustum selectionFrustum(camera, selectRect, 137, 89);

    const Vec3f selectedPoint =
        WorldFromScreenPoint(projection, cameraXform, Vec2f(54.5f, 38.5f), 0.43f, 137, 89);
    const Vec3f outsideSelectionPoint =
        WorldFromScreenPoint(projection, cameraXform, Vec2f(14.7f, 39.3f), 0.43f, 137, 89);

    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)), ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Outside);
}

TEST(Frustum, SelectionRectConstructor_FullScreenMatchesCameraFrustum)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Frustum fullFrustum(camera);
    const Rect selectRect({ .X = 0, .Y = 0, .Width = 137, .Height = 89 });
    const Frustum selectionFrustum(camera, selectRect, 137, 89);

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
        const BoundingSphere sphere(WorldFromCameraPoint(cameraXform, testCase.CameraPoint),
            testCase.Radius);
        EXPECT_EQ(fullFrustum.Contains(sphere), testCase.Expected);
        EXPECT_EQ(selectionFrustum.Contains(sphere), testCase.Expected);
    }
}

TEST(Frustum, SelectionRectConstructor_ScalingScreenAndRectanglePreservesSelection)
{
    const PerspectiveProjection projection = MakeNonDefaultProjection();
    const TrTransformf cameraXform = MakeTranslatedRotatedCameraTransform();
    const Camera camera(projection, cameraXform);
    const Rect selectRect({ .X = 36, .Y = 24, .Width = 37, .Height = 29 });
    const Rect scaledRect({ .X = 108, .Y = 72, .Width = 111, .Height = 87 });
    const Frustum selectionFrustum(camera, selectRect, 137, 89);
    const Frustum scaledFrustum(camera, scaledRect, 411, 267);

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
        const Vec3f worldPoint = WorldFromScreenPoint(projection,
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

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
