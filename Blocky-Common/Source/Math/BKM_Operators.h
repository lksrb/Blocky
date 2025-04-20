#pragma once

// Vector2
template<typename T>
inline v2b<T> operator+(const v2b<T>& v0, const v2b<T>& v1)
{
    return v2b<T>(v0.x + v1.x, v0.y + v1.y);
}

template<typename T>
inline v2b<T> operator-(const v2b<T>& v0, const v2b<T>& v1)
{
    return v2b<T>(v0.x - v1.x, v0.y - v1.y);
}

template<typename T>
inline v2b<T> operator*(const v2b<T>& v0, const v2b<T>& v1)
{
    return v2b<T>(v0.x * v1.x, v0.y * v1.y);
}

template<typename T>
inline v2b<T> operator*(f32 scalar, const v2b<T>& v0)
{
    return v0 * scalar;
}

template<typename T>
inline v2b<T> operator/(const v2b<T>& v0, const v2b<T>& v1)
{
    return v2b<T>(v0.x / v1.x, v0.y / v1.y);
}

template<typename T>
inline v2b<T> operator*(const v2b<T>& v0, f32 scalar)
{
    return v2b<T>(v0.x * scalar, v0.y * scalar);
}

// Vector3
inline v3 operator+(const v3& v0, const v3& v1)
{
    return v3(v0.x + v1.x, v0.y + v1.y, v0.z + v1.z);
}

inline v3 operator-(const v3& v0, const v3& v1)
{
    return v3(v0.x - v1.x, v0.y - v1.y, v0.z - v1.z);
}

inline v3 operator*(const v3& v0, const v3& v1)
{
    return v3(v0.x * v1.x, v0.y * v1.y, v0.z * v1.z);
}

inline v3 operator/(const v3& v0, const v3& v1)
{
    return v3(v0.x / v1.x, v0.y / v1.y, v0.z / v1.z);
}

inline v3 operator*(const v3& v0, f32 scalar)
{
    return v3(v0.x * scalar, v0.y * scalar, v0.z * scalar);
}

inline v3 operator*(f32 scalar, const v3& v0)
{
    return v0 * scalar;
}

inline v3 operator*(const m3& m, const v3& v)
{
    return v3(m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
        m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
        m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z);
}

// Vector4
inline v4 operator+(const v4& v0, const v4& v1)
{
    return v4(v0.x + v1.x, v0.y + v1.y, v0.z + v1.z, v0.w + v1.w);
}

inline v4 operator-(const v4& v0, const v4& v1)
{
    return v4(v0.x - v1.x, v0.y - v1.y, v0.z - v1.z, v0.w - v1.w);
}

inline v4 operator*(const v4& v0, const v4& v1)
{
    return v4(v0.x * v1.x, v0.y * v1.y, v0.z * v1.z, v0.w * v1.w);
}

inline v4 operator/(const v4& v0, const v4& v1)
{
    return v4(v0.x / v1.x, v0.y / v1.y, v0.z / v1.z, v0.w / v1.w);
}

inline v4 operator*(const v4& v, f32 scalar)
{
    return v4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}

// Matrix4
inline m4 operator*(const m4& m, f32 scalar)
{
    return m4(m[0] * scalar, m[1] * scalar, m[2] * scalar, m[3] * scalar);
}

inline m4 operator*(const m4& m1, const m4& m2)
{
    v4 SrcA0 = m1[0];
    v4 SrcA1 = m1[1];
    v4 SrcA2 = m1[2];
    v4 SrcA3 = m1[3];

    v4 SrcB0 = m2[0];
    v4 SrcB1 = m2[1];
    v4 SrcB2 = m2[2];
    v4 SrcB3 = m2[3];

    m4 result;
    result[0] = SrcA0 * SrcB0[0] + SrcA1 * SrcB0[1] + SrcA2 * SrcB0[2] + SrcA3 * SrcB0[3];
    result[1] = SrcA0 * SrcB1[0] + SrcA1 * SrcB1[1] + SrcA2 * SrcB1[2] + SrcA3 * SrcB1[3];
    result[2] = SrcA0 * SrcB2[0] + SrcA1 * SrcB2[1] + SrcA2 * SrcB2[2] + SrcA3 * SrcB2[3];
    result[3] = SrcA0 * SrcB3[0] + SrcA1 * SrcB3[1] + SrcA2 * SrcB3[2] + SrcA3 * SrcB3[3];
    return result;
}

inline v4 operator*(const m4& m, const v4& v)
{
    return v4(
        m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2] + m[3][0] * v[3],
        m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2] + m[3][1] * v[3],
        m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2] + m[3][2] * v[3],
        m[0][3] * v[0] + m[1][3] * v[1] + m[2][3] * v[2] + m[3][3] * v[3]);
}

inline v4 operator*(const v4& v, const m4& m)
{
    return v4(
        m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2] + m[3][0] * v[3],
        m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2] + m[3][1] * v[3],
        m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2] + m[3][2] * v[3],
        m[0][3] * v[0] + m[1][3] * v[1] + m[2][3] * v[2] + m[3][3] * v[3]);
}

// Matrix3
inline m3 operator*(const m3& m, f32 scalar)
{
    return m3(m[0] * scalar, m[1] * scalar, m[2] * scalar);
}

inline m3 operator*(const m3& m1, const m3& m2)
{
    v3 SrcA0 = m1[0];
    v3 SrcA1 = m1[1];
    v3 SrcA2 = m1[2];

    v3 SrcB0 = m2[0];
    v3 SrcB1 = m2[1];
    v3 SrcB2 = m2[2];

    m3 result;
    result[0] = SrcA0 * SrcB0[0] + SrcA1 * SrcB0[1] + SrcA2 * SrcB0[2];
    result[1] = SrcA0 * SrcB1[0] + SrcA1 * SrcB1[1] + SrcA2 * SrcB1[2];
    result[2] = SrcA0 * SrcB2[0] + SrcA1 * SrcB2[1] + SrcA2 * SrcB2[2];
    return result;
}

// Quaternion
inline v3 operator*(const qtn& q, const v3& v)
{
    v3 QuatVector(q.x, q.y, q.z);
    v3 uv(bkm::Cross(QuatVector, v));
    v3 uuv(bkm::Cross(QuatVector, uv));

    return v + ((uv * q.w) + uuv) * 2.0f;
}
