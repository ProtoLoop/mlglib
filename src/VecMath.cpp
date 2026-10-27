#include "VecMath.h"
#include <limits>

template<typename T>
const UnitQuat<T> UnitQuat<T>::Identity{ T{ 0 }, T{ 0 }, T{ 0 }, T{ 1 } };

template<typename T>
const Mat44<T> Mat44<T>::Identity{ T{ 1 } };

template<typename NumType>
const TrTransform<NumType> TrTransform<NumType>::Identity //
    {
        .T = Vec3<NumType>{ 0 },
        .R = UnitQuat<NumType>::Identity,
    };

template<typename NumType>
const TrsTransform<NumType> TrsTransform<NumType>::Identity //
    {
        .T = Vec3<NumType>{ 0 },
        .R = UnitQuat<NumType>::Identity,
        .S = Vec3<NumType>{ 1 },
    };

template<typename T>
Mat44<T>
UnitQuat<T>::ToMatrix() const noexcept
{
    const T xx = m_Vec.x * m_Vec.x;
    const T yy = m_Vec.y * m_Vec.y;
    const T zz = m_Vec.z * m_Vec.z;
    const T xy = m_Vec.x * m_Vec.y;
    const T xz = m_Vec.x * m_Vec.z;
    const T yz = m_Vec.y * m_Vec.z;
    const T wx = m_Vec.w * m_Vec.x;
    const T wy = m_Vec.w * m_Vec.y;
    const T wz = m_Vec.w * m_Vec.z;

    // Column-major 4x4 rotation matrix
    return Mat44<T>{
        Vec4<T>(1 - (2 * (yy + zz)), 2 * (xy + wz), 2 * (xz - wy), 0),
        Vec4<T>(2 * (xy - wz), 1 - (2 * (xx + zz)), 2 * (yz + wx), 0),
        Vec4<T>(2 * (xz + wy), 2 * (yz - wx), 1 - (2 * (xx + yy)), 0),
        Vec4<T>(0, 0, 0, 1),
    };
}

template<typename T>
Mat44<T>
Mat44<T>::Inverse() const noexcept
{
    constexpr T epsilon = T{ 1e-8f };

    // Math-style names:
    //
    //     aRC = row R, column C
    //
    // Storage:
    //
    //     m[column][row]

    const T a00 = m[0][0];
    const T a01 = m[1][0];
    const T a02 = m[2][0];
    const T a03 = m[3][0];

    const T a10 = m[0][1];
    const T a11 = m[1][1];
    const T a12 = m[2][1];
    const T a13 = m[3][1];

    const T a20 = m[0][2];
    const T a21 = m[1][2];
    const T a22 = m[2][2];
    const T a23 = m[3][2];

    const T a30 = m[0][3];
    const T a31 = m[1][3];
    const T a32 = m[2][3];
    const T a33 = m[3][3];

    // 2x2 determinants reused by the 4x4 cofactors.
    const T s0 = (a00 * a11) - (a10 * a01);
    const T s1 = (a00 * a12) - (a10 * a02);
    const T s2 = (a00 * a13) - (a10 * a03);
    const T s3 = (a01 * a12) - (a11 * a02);
    const T s4 = (a01 * a13) - (a11 * a03);
    const T s5 = (a02 * a13) - (a12 * a03);

    const T c0 = (a20 * a31) - (a30 * a21);
    const T c1 = (a20 * a32) - (a30 * a22);
    const T c2 = (a20 * a33) - (a30 * a23);
    const T c3 = (a21 * a32) - (a31 * a22);
    const T c4 = (a21 * a33) - (a31 * a23);
    const T c5 = (a22 * a33) - (a32 * a23);

    const T det = (s0 * c5) - (s1 * c4) + (s2 * c3) + (s3 * c2) - (s4 * c1) + (s5 * c0);

    if(std::abs(det) <= epsilon)
    {
        return Mat44<T>{ 0 };
    }

    const T invDet = 1 / det;

    // Math matrix inverse entries, named row/column.
    const T b00 = ((a11 * c5) - (a12 * c4) + (a13 * c3)) * invDet;
    const T b01 = (-(a01 * c5) + (a02 * c4) - (a03 * c3)) * invDet;
    const T b02 = ((a31 * s5) - (a32 * s4) + (a33 * s3)) * invDet;
    const T b03 = (-(a21 * s5) + (a22 * s4) - (a23 * s3)) * invDet;

    const T b10 = (-(a10 * c5) + (a12 * c2) - (a13 * c1)) * invDet;
    const T b11 = ((a00 * c5) - (a02 * c2) + (a03 * c1)) * invDet;
    const T b12 = (-(a30 * s5) + (a32 * s2) - (a33 * s1)) * invDet;
    const T b13 = ((a20 * s5) - (a22 * s2) + (a23 * s1)) * invDet;

    const T b20 = ((a10 * c4) - (a11 * c2) + (a13 * c0)) * invDet;
    const T b21 = (-(a00 * c4) + (a01 * c2) - (a03 * c0)) * invDet;
    const T b22 = ((a30 * s4) - (a31 * s2) + (a33 * s0)) * invDet;
    const T b23 = (-(a20 * s4) + (a21 * s2) - (a23 * s0)) * invDet;

    const T b30 = (-(a10 * c3) + (a11 * c1) - (a12 * c0)) * invDet;
    const T b31 = ((a00 * c3) - (a01 * c1) + (a02 * c0)) * invDet;
    const T b32 = (-(a30 * s3) + (a31 * s1) - (a32 * s0)) * invDet;
    const T b33 = ((a20 * s3) - (a21 * s1) + (a22 * s0)) * invDet;

    return Mat44<T>{
        Vec4<T>(b00, b10, b20, b30),
        Vec4<T>(b01, b11, b21, b31),
        Vec4<T>(b02, b12, b22, b32),
        Vec4<T>(b03, b13, b23, b33),
    };
}

template<typename T>
Mat44<T>
Mat44<T>::InverseAffine() const noexcept
{
    constexpr T epsilon = T{ 1e-8f };

    // Assumes column-vector convention and storage as:
    //
    //     m[column][row]
    //
    // Affine matrix layout:
    //
    //     [ a00 a01 a02 tx ]
    //     [ a10 a11 a12 ty ]
    //     [ a20 a21 a22 tz ]
    //     [  0   0   0  1 ]

    const T a00 = m[0][0];
    const T a01 = m[1][0];
    const T a02 = m[2][0];

    const T a10 = m[0][1];
    const T a11 = m[1][1];
    const T a12 = m[2][1];

    const T a20 = m[0][2];
    const T a21 = m[1][2];
    const T a22 = m[2][2];

    const T tx = m[3][0];
    const T ty = m[3][1];
    const T tz = m[3][2];

    const T c00 = (a11 * a22) - (a12 * a21);
    const T c01 = (a02 * a21) - (a01 * a22);
    const T c02 = (a01 * a12) - (a02 * a11);

    const T c10 = (a12 * a20) - (a10 * a22);
    const T c11 = (a00 * a22) - (a02 * a20);
    const T c12 = (a02 * a10) - (a00 * a12);

    const T c20 = (a10 * a21) - (a11 * a20);
    const T c21 = (a01 * a20) - (a00 * a21);
    const T c22 = (a00 * a11) - (a01 * a10);

    const T det = (a00 * c00) + (a01 * c10) + (a02 * c20);

    if(std::abs(det) <= epsilon)
    {
        return Mat44(0);
    }

    const T invDet = 1 / det;

    const T b00 = c00 * invDet;
    const T b01 = c01 * invDet;
    const T b02 = c02 * invDet;

    const T b10 = c10 * invDet;
    const T b11 = c11 * invDet;
    const T b12 = c12 * invDet;

    const T b20 = c20 * invDet;
    const T b21 = c21 * invDet;
    const T b22 = c22 * invDet;

    const T itx = (-(b00 * tx) - (b01 * ty) - (b02 * tz));
    const T ity = (-(b10 * tx) - (b11 * ty) - (b12 * tz));
    const T itz = (-(b20 * tx) - (b21 * ty) - (b22 * tz));

    return Mat44{
        Vec4<T>(b00, b10, b20, 0),
        Vec4<T>(b01, b11, b21, 0),
        Vec4<T>(b02, b12, b22, 0),
        Vec4<T>(itx, ity, itz, 1),
    };
}

template<typename T>
Mat44<T>
Mat44<T>::Transpose() const noexcept
{
    return Mat44 //
        {
            Vec4<T>(m[0].x, m[1].x, m[2].x, m[3].x),
            Vec4<T>(m[0].y, m[1].y, m[2].y, m[3].y),
            Vec4<T>(m[0].z, m[1].z, m[2].z, m[3].z),
            Vec4<T>(m[0].w, m[1].w, m[2].w, m[3].w),
        };
}

template<typename T>
void
Mat44<T>::Decompose(Vec3<T>& translation, UnitQuat<T>& rotation, Vec3<T>& scale) const noexcept
{
    const auto& mm = *this;

    // Extract translation (column 3)
    translation = Vec3<T>(mm[3][0], mm[3][1], mm[3][2]);

    // Extract scale (length of basis columns)
    scale.x = std::sqrt((mm[0][0] * mm[0][0]) + (mm[0][1] * mm[0][1]) + (mm[0][2] * mm[0][2]));
    scale.y = std::sqrt((mm[1][0] * mm[1][0]) + (mm[1][1] * mm[1][1]) + (mm[1][2] * mm[1][2]));
    scale.z = std::sqrt((mm[2][0] * mm[2][0]) + (mm[2][1] * mm[2][1]) + (mm[2][2] * mm[2][2]));

    // Build rotation matrix (row-major) from normalized columns
    const T invX = scale.x != 0 ? 1 / scale.x : 0;
    const T invY = scale.y != 0 ? 1 / scale.y : 0;
    const T invZ = scale.z != 0 ? 1 / scale.z : 0;

    const T r00 = mm[0][0] * invX;
    const T r01 = mm[1][0] * invY;
    const T r02 = mm[2][0] * invZ;
    const T r10 = mm[0][1] * invX;
    const T r11 = mm[1][1] * invY;
    const T r12 = mm[2][1] * invZ;
    const T r20 = mm[0][2] * invX;
    const T r21 = mm[1][2] * invY;
    const T r22 = mm[2][2] * invZ;

    // Convert rotation matrix to quaternion
    const T trace = r00 + r11 + r22;
    T qw;
    T qx;
    T qy;
    T qz;
    if(trace > 0)
    {
        const T s = std::sqrt(trace + 1) * 2;
        qw = T{ 0.25 } * s;
        qx = (r21 - r12) / s;
        qy = (r02 - r20) / s;
        qz = (r10 - r01) / s;
    }
    else if(r00 > r11 && r00 > r22)
    {
        const T s = std::sqrt(1 + r00 - r11 - r22) * 2;
        qw = (r21 - r12) / s;
        qx = T{ 0.25 } * s;
        qy = (r01 + r10) / s;
        qz = (r02 + r20) / s;
    }
    else if(r11 > r22)
    {
        const T s = std::sqrt(1 + r11 - r00 - r22) * 2;
        qw = (r02 - r20) / s;
        qx = (r01 + r10) / s;
        qy = T{ 0.25 } * s;
        qz = (r12 + r21) / s;
    }
    else
    {
        const T s = std::sqrt(1 + r22 - r00 - r11) * 2;
        qw = (r10 - r01) / s;
        qx = (r02 + r20) / s;
        qy = (r12 + r21) / s;
        qz = T{ 0.25 } * s;
    }

    rotation = UnitQuat<T>(qx, qy, qz, qw);
}

template<typename T>
Mat44<T>
Mat44<T>::PerspectiveLH(
    const Radians<T> fov, const T aspectRatio, const T nearClip, const T farClip) noexcept
{
    const T t = (fov * T{ 0.5 }).Tan();

    Mat44 result(0);
    result[0][0] = static_cast<T>(1.0 / (t * aspectRatio));
    result[1][1] = static_cast<T>(1.0 / t);
    result[2][2] = farClip / (farClip - nearClip);
    result[2][3] = 1;
    result[3][2] = -(farClip * nearClip) / (farClip - nearClip);
    return result;
}

template<typename NumType>
Mat44<NumType>
TrTransform<NumType>::ToMatrix() const noexcept
{
    Mat44<NumType> M = R.ToMatrix();
    M[3] = Vec4<NumType>(T, 1);
    return M;
}

template<typename NumType>
TrTransform<NumType>
TrTransform<NumType>::Inverse() const noexcept
{
    const UnitQuat<NumType> r = R.Inverse();

    const TrTransform result{ .T = -(r * T), .R = r };

    return result;
}

template<typename NumType>
Mat44<NumType>
TrsTransform<NumType>::ToMatrix() const noexcept
{
    Mat44<NumType> M = R.ToMatrix();
    M[0] *= S.x;
    M[1] *= S.y;
    M[2] *= S.z;
    M[3] = Vec4<NumType>(T, 1);
    return M;
}

template<typename NumType>
TrsTransform<NumType>
TrsTransform<NumType>::FromMatrix(const Mat44<NumType>& mat) noexcept
{
    TrsTransform result;
    mat.Decompose(result.T, result.R, result.S);
    return result;
}

Rect::Rect(const RectParams& params) noexcept
    : m_X(params.X),
      m_Y(params.Y),
      m_Width(params.Width),
      m_Height(params.Height)
{
    if(!MLG_VERIFY(m_Width > 0, "Width must be non-zero"))
    {
        m_Width = 1;
    }

    if(!MLG_VERIFY(m_Height > 0, "Height must be non-zero"))
    {
        m_Height = 1;
    }

#ifndef NDEBUG
    constexpr int kMax = std::numeric_limits<int>::max();
#endif

    MLG_ASSERT(m_Width <= static_cast<unsigned>(kMax) && m_X <= kMax - static_cast<int>(m_Width),
        "Right edge exceeds the int range");

    MLG_ASSERT(m_Height <= static_cast<unsigned>(kMax) && m_Y <= kMax - static_cast<int>(m_Height),
        "Bottom edge exceeds the int range");
}

bool
Rect::Contains(const Point2& point) const noexcept
{
    return point.GetX() >= m_X
        && point.GetX() < m_X + static_cast<int>(m_Width)
        && point.GetY() >= m_Y
        && point.GetY() < m_Y + static_cast<int>(m_Height);
}

bool
Rect::Contains(const Rect& other) const noexcept
{
    return other.m_X >= m_X
        && other.m_X + static_cast<int>(other.m_Width) <= m_X + static_cast<int>(m_Width)
        && other.m_Y >= m_Y
        && other.m_Y + static_cast<int>(other.m_Height) <= m_Y + static_cast<int>(m_Height);
}

bool
Rect::Intersects(const Rect& other) const noexcept
{
    return other.m_X < m_X + static_cast<int>(m_Width)
        && other.m_X + static_cast<int>(other.m_Width) > m_X
        && other.m_Y < m_Y + static_cast<int>(m_Height)
        && other.m_Y + static_cast<int>(other.m_Height) > m_Y;
}

Rect
Rect::Intersect(const Rect& other) const noexcept
{
    const int x0 = std::max(m_X, other.m_X);
    const int y0 = std::max(m_Y, other.m_Y);
    const int x1 =
        std::min(m_X + static_cast<int>(m_Width), other.m_X + static_cast<int>(other.m_Width));
    const int y1 =
        std::min(m_Y + static_cast<int>(m_Height), other.m_Y + static_cast<int>(other.m_Height));

    if(x1 <= x0 || y1 <= y0)
    {
        return Rect({ .X = 0, .Y = 0, .Width = 0, .Height = 0 });
    }

    return Rect({ .X = x0,
        .Y = y0,
        .Width = static_cast<unsigned>(x1 - x0),
        .Height = static_cast<unsigned>(y1 - y0) });
}

template UnitQuat<float> UnitQuat<float>::Identity;
template Mat44<float> Mat44<float>::Identity;
template TrTransform<float> TrTransform<float>::Identity;
template TrsTransform<float> TrsTransform<float>::Identity;

template Mat44<float> UnitQuat<float>::ToMatrix() const noexcept;

template Mat44<float> Mat44<float>::Inverse() const noexcept;
template Mat44<float> Mat44<float>::InverseAffine() const noexcept;
template Mat44<float> Mat44<float>::Transpose() const noexcept;
template void Mat44<float>::Decompose(
    Vec3<float>& translation, UnitQuat<float>& rotation, Vec3<float>& scale) const noexcept;
template Mat44<float> Mat44<float>::PerspectiveLH(const Radians<float> fov,
    const float aspectRatio,
    const float nearClip,
    const float farClip) noexcept;

template Mat44<float> TrTransform<float>::ToMatrix() const noexcept;
template TrTransform<float> TrTransform<float>::Inverse() const noexcept;
template Mat44<float> TrsTransform<float>::ToMatrix() const noexcept;
template TrsTransform<float> TrsTransform<float>::FromMatrix(const Mat44<float>& mat) noexcept;
