#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "VecMath.h"

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// Type aliases for convenience
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Mat44f = Mat44<float>;
using UnitQuatf = UnitQuat<float>;
using Radiansf = Radians<float>;

namespace
{
    constexpr float EPS = 1e-5f;
}

TEST(Mat44f, Identity)
{
    const Mat44f& I = Mat44f::Identity();

    EXPECT_FLOAT_EQ(I[0][0], 1.0f);
    EXPECT_FLOAT_EQ(I[1][1], 1.0f);
    EXPECT_FLOAT_EQ(I[2][2], 1.0f);
    EXPECT_FLOAT_EQ(I[3][3], 1.0f);

    EXPECT_FLOAT_EQ(I[0][1], 0.0f);
    EXPECT_FLOAT_EQ(I[0][2], 0.0f);
    EXPECT_FLOAT_EQ(I[0][3], 0.0f);
    EXPECT_FLOAT_EQ(I[1][0], 0.0f);
    EXPECT_FLOAT_EQ(I[1][2], 0.0f);
    EXPECT_FLOAT_EQ(I[1][3], 0.0f);
    EXPECT_FLOAT_EQ(I[2][0], 0.0f);
    EXPECT_FLOAT_EQ(I[2][1], 0.0f);
    EXPECT_FLOAT_EQ(I[2][3], 0.0f);
    EXPECT_FLOAT_EQ(I[3][0], 0.0f);
    EXPECT_FLOAT_EQ(I[3][1], 0.0f);
    EXPECT_FLOAT_EQ(I[3][2], 0.0f);
}

TEST(Mat44f, Construction_DiagonalValue)
{
    Mat44f M(2.0f);

    EXPECT_FLOAT_EQ(M[0][0], 2.0f);
    EXPECT_FLOAT_EQ(M[1][1], 2.0f);
    EXPECT_FLOAT_EQ(M[2][2], 2.0f);
    EXPECT_FLOAT_EQ(M[3][3], 2.0f);

    EXPECT_FLOAT_EQ(M[0][1], 0.0f);
    EXPECT_FLOAT_EQ(M[0][2], 0.0f);
    EXPECT_FLOAT_EQ(M[0][3], 0.0f);
    EXPECT_FLOAT_EQ(M[1][0], 0.0f);
    EXPECT_FLOAT_EQ(M[2][0], 0.0f);
    EXPECT_FLOAT_EQ(M[3][0], 0.0f);
}

TEST(Mat44f, Construction_Elements)
{
    Mat44f M(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );

    EXPECT_FLOAT_EQ(M[0][0], 1.0f);
    EXPECT_FLOAT_EQ(M[0][1], 2.0f);
    EXPECT_FLOAT_EQ(M[0][2], 3.0f);
    EXPECT_FLOAT_EQ(M[0][3], 4.0f);
    EXPECT_FLOAT_EQ(M[3][0], 13.0f);
    EXPECT_FLOAT_EQ(M[3][1], 14.0f);
    EXPECT_FLOAT_EQ(M[3][2], 15.0f);
    EXPECT_FLOAT_EQ(M[3][3], 16.0f);
}

TEST(Mat44f, Construction_FromQuat)
{
    const float angle = std::numbers::pi_v<float> / 2.0f;
    const UnitQuatf q(Radiansf(angle), Vec3f(0.0f, 0.0f, 1.0f));
    const Mat44f M = q.ToMatrix();

    const Vec4f v(1.0f, 0.0f, 0.0f, 1.0f);
    const Vec4f r = M * v;

    EXPECT_NEAR(r.x, 0.0f, EPS);
    EXPECT_NEAR(r.y, 1.0f, EPS);
    EXPECT_NEAR(r.z, 0.0f, EPS);
    EXPECT_NEAR(r.w, 1.0f, EPS);
}

TEST(Mat44f, Multiply_Matrix)
{
    const Mat44f A(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );

    const Mat44f& I = Mat44f::Identity();
    const Mat44f C = A * I;

    EXPECT_TRUE(C == A);
}

TEST(Mat44f, Mul_Matrix_Function)
{
    const Mat44f A(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    const Mat44f B(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );

    const Mat44f C = A.Mul(B);
    const Mat44f expected = A * B;

    EXPECT_TRUE(C == expected);
}

TEST(Mat44f, Multiply_Assign)
{
    Mat44f A(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );

    const Mat44f B(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    const Mat44f expected = A * B;
    A *= B;
    EXPECT_TRUE(A == expected);
}

TEST(Mat44f, Multiply_Vector4)
{
    const Mat44f M(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    const Vec4f v(1.0f, 2.0f, 3.0f, 1.0f);
    const Vec4f r = M * v;

    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.y, 4.0f);
    EXPECT_FLOAT_EQ(r.z, 9.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);

    const Vec4f r2 = M.m[0] * v.x + M.m[1] * v.y + M.m[2] * v.z + M.m[3] * v.w;

    EXPECT_EQ(r, r2);
}

TEST(Mat44f, Mul_Vector4_Function)
{
    const Mat44f M(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    const Vec4f v(1.0f, 2.0f, 3.0f, 1.0f);
    const Vec4f r = M.Mul(v);

    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 6.0f);
    EXPECT_FLOAT_EQ(r.z, 12.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Mat44f, Multiply_Vector3_WithTranslation)
{
    const Mat44f M(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        10.0f, 20.0f, 30.0f, 1.0f
    );

    const Vec3f v(1.0f, 2.0f, 3.0f);
    const Vec4f r = M * v;

    EXPECT_FLOAT_EQ(r.x, 11.0f);
    EXPECT_FLOAT_EQ(r.y, 22.0f);
    EXPECT_FLOAT_EQ(r.z, 33.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Mat44f, Multiply_Vector3_NoTranslation)
{
    const Mat44f M(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    const Vec3f v(1.0f, 2.0f, 3.0f);
    const Vec4f r = M * v;

    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 6.0f);
    EXPECT_FLOAT_EQ(r.z, 12.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Mat44f, Mul_Vector3_Function)
{
    const Mat44f M(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 3.0f, 0.0f,
        5.0f, 6.0f, 7.0f, 1.0f
    );

    const Vec3f v(1.0f, 1.0f, 1.0f);
    const Vec4f r = M.Mul(v);

    EXPECT_FLOAT_EQ(r.x, 6.0f);
    EXPECT_FLOAT_EQ(r.y, 8.0f);
    EXPECT_FLOAT_EQ(r.z, 10.0f);
    EXPECT_FLOAT_EQ(r.w, 1.0f);
}

TEST(Mat44f, Transpose)
{
    const Mat44f M(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );

    const Mat44f T = M.Transpose();

    EXPECT_FLOAT_EQ(T[0][1], 5.0f);
    EXPECT_FLOAT_EQ(T[1][0], 2.0f);
    EXPECT_FLOAT_EQ(T[2][3], 15.0f);
    EXPECT_FLOAT_EQ(T[3][2], 12.0f);
}

TEST(Mat44f, OperatorIndex_ReadWrite)
{
    Mat44f M(0.0f);

    M[0][0] = 1.0f;
    M[1][1] = 2.0f;
    M[2][2] = 3.0f;
    M[3][3] = 4.0f;

    EXPECT_FLOAT_EQ(M[0][0], 1.0f);
    EXPECT_FLOAT_EQ(M[1][1], 2.0f);
    EXPECT_FLOAT_EQ(M[2][2], 3.0f);
    EXPECT_FLOAT_EQ(M[3][3], 4.0f);

    const Mat44f& CM = M;
    EXPECT_FLOAT_EQ(CM[0][0], 1.0f);
    EXPECT_FLOAT_EQ(CM[1][1], 2.0f);
    EXPECT_FLOAT_EQ(CM[2][2], 3.0f);
    EXPECT_FLOAT_EQ(CM[3][3], 4.0f);
}

TEST(Mat44f, Equality)
{
    const Mat44f A(1.0f);
    const Mat44f B(1.0f);
    const Mat44f C(2.0f);

    EXPECT_TRUE(A == B);
    EXPECT_FALSE(A == C);
}

TEST(Mat44f, Decompose_Trs)
{
    const Vec3f translation(3.0f, 4.0f, 5.0f);
    const Vec3f scale(2.0f, 3.0f, 4.0f);

    const Mat44f M(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        translation.x, translation.y, translation.z, 1.0f
    );

    Vec3f outT(0.0f, 0.0f, 0.0f);
    Vec3f outS(0.0f, 0.0f, 0.0f);
    UnitQuatf outR(0.0f, 0.0f, 0.0f, 1.0f);

    M.Decompose(outT, outR, outS);

    EXPECT_NEAR(outT.x, translation.x, EPS);
    EXPECT_NEAR(outT.y, translation.y, EPS);
    EXPECT_NEAR(outT.z, translation.z, EPS);

    EXPECT_NEAR(outS.x, scale.x, EPS);
    EXPECT_NEAR(outS.y, scale.y, EPS);
    EXPECT_NEAR(outS.z, scale.z, EPS);

    EXPECT_NEAR(outR.ToVector().x, 0.0f, EPS);
    EXPECT_NEAR(outR.ToVector().y, 0.0f, EPS);
    EXPECT_NEAR(outR.ToVector().z, 0.0f, EPS);
    EXPECT_NEAR(outR.ToVector().w, 1.0f, EPS);
}

TEST(Mat44f, Inverse_Identity)
{
    const Mat44f& I = Mat44f::Identity();
    const Mat44f inv = I.Inverse();
    EXPECT_TRUE(inv == I);
}

TEST(Mat44f, Inverse_ScaleTranslation)
{
    const Mat44f M(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        10.0f, 20.0f, 30.0f, 1.0f
    );

    const Mat44f inv = M.Inverse();
    const Mat44f I = M * inv;

    EXPECT_NEAR(I[0][0], 1.0f, EPS);
    EXPECT_NEAR(I[1][1], 1.0f, EPS);
    EXPECT_NEAR(I[2][2], 1.0f, EPS);
    EXPECT_NEAR(I[3][3], 1.0f, EPS);
    EXPECT_NEAR(I[0][1], 0.0f, EPS);
    EXPECT_NEAR(I[1][0], 0.0f, EPS);
}

TEST(Mat44f, Inverse_SingularMatrixReturnsZero)
{
    const Mat44f M(
        1.0f, 2.0f, 3.0f, 4.0f,
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f
    );

    const Mat44f inv = M.Inverse();

    EXPECT_FLOAT_EQ(inv[0][0], 0.0f);
    EXPECT_FLOAT_EQ(inv[1][1], 0.0f);
    EXPECT_FLOAT_EQ(inv[2][2], 0.0f);
    EXPECT_FLOAT_EQ(inv[3][3], 0.0f);
}

TEST(Mat44f, Inverse_NonAffine)
{
    // A perspective projection matrix is non-affine and should be invertible
    const Mat44f M = Mat44f::PerspectiveLH(Radiansf(std::numbers::pi_v<float> / 3.0f),
        16.0f / 9.0f,
        0.1f,
        100.0f);

    const Mat44f inv = M.Inverse();
    const Mat44f I = M * inv;
    // The product should be close to identity
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(I[i][j], (i == j) ? 1.0f : 0.0f, 1e-3f);
        }
    }
}

TEST(Mat44f, Inverse_Random)
{
    // A random invertible matrix
    const Mat44f M(
        4.0f, 7.0f, 2.0f, 3.0f,
        0.0f, 5.0f, 9.0f, 1.0f,
        6.0f, 8.0f, 2.0f, 5.0f,
        1.0f, 0.0f, 3.0f, 2.0f
    );
    const Mat44f inv = M.Inverse();
    const Mat44f I = M * inv;
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(I[i][j], (i == j) ? 1.0f : 0.0f, 1e-3f);
        }
    }
}

TEST(Mat44f, InverseAffine_Basic)
{
    // Affine matrix: scale, rotate, translate
    const Mat44f M(
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        5.0f, 6.0f, 7.0f, 1.0f
    );
    const Mat44f inv = M.InverseAffine();
    const Mat44f I = M * inv;
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(I[i][j], (i == j) ? 1.0f : 0.0f, 1e-4f);
        }
    }
}

TEST(Mat44f, InverseAffine_NonInvertible)
{
    // Affine matrix with zero scale (non-invertible)
    const Mat44f M(
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    const Mat44f inv = M.InverseAffine();
    // Should return zero matrix
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_FLOAT_EQ(inv[i][j], 0.0f);
        }
    }
}

TEST(Mat44f, InverseAffine_TranslationOnly)
{
    // Affine matrix with only translation
    Mat44f M = Mat44f::Identity();
    M[3][0] = 10.0f;
    M[3][1] = -5.0f;
    M[3][2] = 2.5f;
    const Mat44f inv = M.InverseAffine();
    const Mat44f I = M * inv;
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(I[i][j], (i == j) ? 1.0f : 0.0f, 1e-5f);
        }
    }
}

TEST(Mat44f, PerspectiveLH)
{
    const float fov = std::numbers::pi_v<float> / 2.0f;
    const float width = 1280.0f;
    const float height = 720.0f;
    const float nearClip = 0.1f;
    const float farClip = 100.0f;
    const float aspectRatio = width / height;

    Mat44f P = Mat44f::PerspectiveLH(Radiansf(fov), aspectRatio, nearClip, farClip);

    const float h = std::cos(0.5f * fov) / std::sin(0.5f * fov);
    const float w = h * height / width;

    EXPECT_NEAR(P[0][0], w, EPS);
    EXPECT_NEAR(P[1][1], h, EPS);
    EXPECT_NEAR(P[2][2], farClip / (farClip - nearClip), EPS);
    EXPECT_NEAR(P[2][3], 1.0f, EPS);
    EXPECT_NEAR(P[3][2], -(farClip * nearClip) / (farClip - nearClip), EPS);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)