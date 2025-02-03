#pragma once

#define USE_C_MATH 0

#if USE_C_MATH
#include <cmath>
#endif

//#include "Core.h"

// Mostly imported from glm library to reduce compile times
namespace bkm {
    inline constexpr f32 PI = 3.1415927f;
    inline constexpr f32 TwoPI = 2.0f * PI;
    inline constexpr f32 PI_HALF = PI / 2.0f;
    inline constexpr f32 EPSILON = 1e-5f;
    inline f32 NotANumber = 0.0f / 0.0f;

    // General
    inline f32 Floor(f32 x) noexcept
    {
#if USE_C_MATH
        return floorf(x);
#else
        i32 i = (i32)x;
        return static_cast<f32>(i - (i > x));
#endif
    }

    inline f32 Abs(f32 scalar) noexcept
    {
#if USE_C_MATH
        return std::abs(scalar);
#else
        // Convert the float to an integer representation
        u32* pScalar = (u32*)&scalar;

        // Clear the sign bit (most significant bit)
        *pScalar &= 0x7FFFFFFF;

        return scalar;
#endif
    }

    inline f32 Sqrt(f32 x) noexcept
    {
#if USE_C_MATH
        return std::sqrt(x);
#else
        f32 result;

        i32 i = *(i32*)&x;

        i = 0x1FC00000 + (i >> 1);

        result = *(f32*)&i;

        result = result / 2 + x / (2 * result); /* 1st iteration */
        result = result / 2 + x / (2 * result); /* 2nd iteration, this can be removed */

        return result;
#endif
    }

    inline f32 CopySign(f32 x, f32 y) noexcept
    {
#if USE_C_MATH
        return std::copysign(x, y);
#else

        // Treat the float as a 32-bit integer to manipulate its bits
        u32* px = (u32*)&x;
        u32* py = (u32*)&y;

        // Mask out the sign bit (most significant bit)
        u32 sign_mask = 0x80000000;  // The sign bit in a 32-bit float
        *px &= ~sign_mask;  // Clear the sign bit of x

        // Copy the sign bit from y to x
        *px |= (*py & sign_mask);  // Set the sign bit of x to be the same as y

        return x;  // Return the modified x
#endif
    }

    // Goniometric functions
    inline f32 Cos(f32 x) noexcept
    {
#if USE_C_MATH
        return std::cos(x);
#else
        constexpr f32 tp = 1.0f / (2.0f * PI);
        x *= tp;
        x -= 0.25f + Floor(x + 0.25f);
        x *= 16.0f * (Abs(x) - 0.5f);

        // Extra precision
        x += 0.225f * x * (Abs(x) - 1.0f);

        return x;
#endif
    }

    inline f32 Sin(f32 x) noexcept
    {
#if USE_C_MATH
        return std::sin(x);
#else
        return Cos(x - PI_HALF);
#endif
    }

    inline f32 Tan(f32 x) noexcept
    {
        return Sin(x) / Cos(x);
    }

    inline f32 Atan(f32 x) noexcept
    {
#if USE_C_MATH
        return std::atan(x);
#else
        return 8.430893743524f * x / (3.2105332277903100f + Sqrt(27.2515970979709f + 29.3591908371266f * x * x));
#endif
    }

    inline f32 Atan2(f32 y, f32 x) noexcept
    {
#if USE_C_MATH
        return std::atan2(y, x);
#else
        //http://pubs.opengroup.org/onlinepubs/009695399/functions/atan2.html
        f32 abs_y = Abs(y) + 1e-10f;      // kludge to prevent 0/0 condition
        f32 r = (x - CopySign(abs_y, x)) / (abs_y + Abs(x));
        f32 angle = PI / 2.0f - CopySign(PI / 4.0f, x);

        angle += (0.1963f * r * r - 0.9817f) * r;
        return CopySign(angle, y);
#endif
    }
}

#include "BKM_Types.h"

namespace bkm {
    inline m4 Inverse(m4 m)
    {
        f32 coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
        f32 coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
        f32 coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3];

        f32 coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
        f32 coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
        f32 coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3];

        f32 coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
        f32 coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
        f32 coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2];

        f32 coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
        f32 coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
        f32 coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3];

        f32 coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
        f32 coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
        f32 coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2];

        f32 coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
        f32 coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
        f32 coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1];

        v4 fac0(coef00, coef00, coef02, coef03);
        v4 fac1(coef04, coef04, coef06, coef07);
        v4 fac2(coef08, coef08, coef10, coef11);
        v4 fac3(coef12, coef12, coef14, coef15);
        v4 fac4(coef16, coef16, coef18, coef19);
        v4 fac5(coef20, coef20, coef22, coef23);

        v4 vec0(m[1][0], m[0][0], m[0][0], m[0][0]);
        v4 vec1(m[1][1], m[0][1], m[0][1], m[0][1]);
        v4 vec2(m[1][2], m[0][2], m[0][2], m[0][2]);
        v4 vec3(m[1][3], m[0][3], m[0][3], m[0][3]);

        v4 inv0(vec1 * fac0 - vec2 * fac1 + vec3 * fac2);
        v4 inv1(vec0 * fac0 - vec2 * fac3 + vec3 * fac4);
        v4 inv2(vec0 * fac1 - vec1 * fac3 + vec3 * fac5);
        v4 inv3(vec0 * fac2 - vec1 * fac4 + vec2 * fac5);

        v4 signA(+1, -1, +1, -1);
        v4 signB(-1, +1, -1, +1);
        m4 inverse(inv0 * signA, inv1 * signB, inv2 * signA, inv3 * signB);

        v4 row0(inverse[0][0], inverse[1][0], inverse[2][0], inverse[3][0]);

        v4 dot0(m[0] * row0);
        f32 dot1 = (dot0.x + dot0.y) + (dot0.z + dot0.w);

        f32 oneOverDeterminant = 1.0f / dot1;

        return inverse * oneOverDeterminant;
    }

    inline m4 Perspective(f32 fovy, f32 aspect, f32 zNear, f32 zFar)
    {
        m4 result(0.0f);

        f32 tanHalfFovy = Tan(fovy / 2.0f);
        result[0][0] = 1.0f / (aspect * tanHalfFovy);
        result[1][1] = 1.0f / (tanHalfFovy);
        result[2][2] = zFar / (zNear - zFar);
        result[2][3] = -1.0f;
        result[3][2] = -(zFar * zNear) / (zFar - zNear);
        return result;
    }

    inline m4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
    {
        m4 result(1.0f);
        result[0][0] = 2.0f / (right - left);
        result[1][1] = 2.0f / (top - bottom);
        result[2][2] = -1.0f / (zFar - zNear);
        result[3][0] = -(right + left) / (right - left);
        result[3][1] = -(top + bottom) / (top - bottom);
        result[3][2] = -zNear / (zFar - zNear);

        return result;
    }

    inline m3 Translate(m3 m, V2 v)
    {
        m3 result(m);
        result[2] = m[0] * v[0] + m[1] * v[1] + m[2];
        return result;
    }

    inline m4 Translate(m4 m, v3 v)
    {
        m4 result(m);
        result[3] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
        return result;
    }

    inline m3 Scale(m3 m, V2 scale)
    {
        m3 result;
        result[0] = m[0] * scale[0];
        result[1] = m[1] * scale[1];
        result[2] = m[2];
        return result;
    }

    inline m4 Scale(m4 m, v3 scale)
    {
        m4 result;
        result[0] = m[0] * scale[0];
        result[1] = m[1] * scale[1];
        result[2] = m[2] * scale[2];
        result[3] = m[3];
        return result;
    }

    inline f32 Dot(const V2& v0, const V2& v1)
    {
        return v0.x * v1.x + v0.y * v1.y;
    }

    inline f32 Dot(const v3& v0, const v3& v1)
    {
        return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
    }

    inline f32 Dot(const v4& v0, const v4& v1)
    {
        return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
    }

    inline bool NonZero(const V2& v)
    {
        // TODO: Is there faster way?
        return Dot(v, v) > 0.0f;
    }

    inline f32 InverseSqrt(f32 x)
    {
        return 1.0f / Sqrt(x);
    }

    inline f32 Mix(f32 start, f32 end, f32 t)
    {
        return start * (1.0f - t) + end * t;
    }

    inline f32 Normalize(f32 value, f32 min, f32 max) 
    { 
        return (value - min) / (max - min); 
    }

    inline V2 Normalize(const V2& v)
    {
        return v * InverseSqrt(Dot(v, v));
    }

    inline v3 Normalize(const v3& v)
    {
        return v * InverseSqrt(Dot(v, v));
    }

    inline v4 Normalize(const v4& v)
    {
        return v * InverseSqrt(Dot(v, v));
    }

    inline v3 Cross(const v3& a, const v3& b)
    {
        return v3(
            a.y * b.z - b.y * a.z,
            a.z * b.x - b.z * a.x,
            a.x * b.y - b.x * a.y);
    }

    inline f32 Length(const V2& v)
    {
        return Sqrt(Dot(v, v));
    }

    inline f32 Length(const v3& v)
    {
        return Sqrt(Dot(v, v));
    }

    inline f32 Length(const v4& v)
    {
        return Sqrt(Dot(v, v));
    }

    inline m4 ToM4(const qtn& q)
    {
        m4 result(1.0f);

        f32 qxx = q.x * q.x;
        f32 qyy = q.y * q.y;
        f32 qzz = q.z * q.z;
        f32 qxy = q.x * q.y;

        f32 qxz = q.x * q.z;
        f32 qyz = q.y * q.z;
        f32 qwx = q.w * q.x;
        f32 qwy = q.w * q.y;
        f32 qwz = q.w * q.z;

        result[0][0] = 1.0f - 2.0f * (qyy + qzz);
        result[0][1] = 2.0f * (qxy + qwz);
        result[0][2] = 2.0f * (qxz - qwy);

        result[1][0] = 2.0f * (qxy - qwz);
        result[1][1] = 1.0f - 2.0f * (qxx + qzz);
        result[1][2] = 2.0f * (qyz + qwx);

        result[2][0] = 2.0f * (qxz + qwy);
        result[2][1] = 2.0f * (qyz - qwx);
        result[2][2] = 1.0f - 2.0f * (qxx + qyy);

        return result;
    }

    inline f32 Radians(f32 degrees)
    {
        return degrees * PI / 180.0f;
    }

    inline f32 Degrees(f32 radians)
    {
        return radians * 180.0f / PI;
    }

    inline m4 LookAt(v3 eye, v3 center, v3 up)
    {
        m4 result(1.0f);

        const v3 f(Normalize(center - eye));
        const v3 s(Normalize(Cross(f, up)));
        const v3 u(Cross(s, f));

        result[0][0] = s.x;
        result[1][0] = s.y;
        result[2][0] = s.z;
        result[0][1] = u.x;
        result[1][1] = u.y;
        result[2][1] = u.z;
        result[0][2] = -f.x;
        result[1][2] = -f.y;
        result[2][2] = -f.z;
        result[3][0] = -Dot(s, eye);
        result[3][1] = -Dot(u, eye);
        result[3][2] = Dot(f, eye);

        return result;
    }

    inline V2 Rotate(const V2& v, f32 angle)
    {
        V2 result;
        result.x = v.x * Cos(angle) + v.y * -Sin(angle);
        result.y = v.x * Sin(angle) + v.y * Cos(angle);
        return result;
    }

    inline v3 Rotate(const qtn& q, const v3& v)
    {
        return q * v;
    }

    inline V2 Abs(const V2& v)
    {
        return V2(Abs(v.x), Abs(v.y));
    }

    inline v3 Abs(const v3& v)
    {
        return v3(Abs(v.x), Abs(v.y), Abs(v.z));
    }

    inline f32 Lerp(f32 start, f32 end, f32 maxDistanceDelta)
    {
        return start + (end - start) * maxDistanceDelta;
    }

    inline V2 Lerp(V2 v1, V2 v2, f32 maxDistanceDelta)
    {
        return V2(Lerp(v1.x, v2.x, maxDistanceDelta), Lerp(v1.y, v2.y, maxDistanceDelta));
    }

    inline f32 Dot(qtn q1, qtn q2)
    {
        return q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
    }

    inline qtn Normalize(qtn q)
    {
        f32 magnitude = Sqrt(Dot(q, q));
        q.w /= magnitude;
        q.x /= magnitude;
        q.y /= magnitude;
        q.z /= magnitude;
        return q;
    }

    inline qtn Lerp(qtn start, qtn end, f32 maxRotationDelta)
    {
        qtn result(
            bkm::Mix(start.w, end.w, maxRotationDelta),
            bkm::Mix(start.x, end.x, maxRotationDelta),
            bkm::Mix(start.y, end.y, maxRotationDelta),
            bkm::Mix(start.z, end.z, maxRotationDelta)
        );

        return bkm::Normalize(result);
    }
#if 0
    inline QTN Slerp(QTN start, QTN end, f32 maxRotationDelta)
    {
        QTN result(0.0f, 0.0f, 0.0f, 0.0f);

        start = Meth::Normalize(start);
        end = Meth::Normalize(end);

        // Calculate the angle between the quaternions
        f32 dot = Dot(start, end);

        // if dot < 0, the interpolation will take the long way around the spehere
        // One quat must be negated
        if (dot < 0.0f)
        {
            end = -end;
            dot = -dot;
        }

        // Quaternions are very close, perform linear interpolation
        if (1.0f - dot > maxRotationDelta)
        {
            f32 theta = std::acos(dot); // TODO: Implement Acos
            // Interpolate using spherical linear interpolation formula
            f32 sinTheta = Sin(theta);
            f32 weightStart = Sin((1.0f - maxRotationDelta) * theta) / sinTheta;
            f32 weightEnd = Sin(maxRotationDelta * theta) / sinTheta;

            result.w = weightStart * start.w + weightEnd * end.w;
            result.x = weightStart * start.x + weightEnd * end.x;
            result.y = weightStart * start.y + weightEnd * end.y;
            result.z = weightStart * start.z + weightEnd * end.z;
        }
        else // Linear interpolation
        {
            result = Lerp(start, end, maxRotationDelta);
        }

        // Normalize the result
        return Meth::Normalize(result);
    }
#endif


    inline v3 EulerAngles(const qtn& q)
    {
        f32 x = Atan2(2 * (q.x * q.y + q.w * q.z), q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z);
        f32 y = Atan(-2 * (q.x * q.z - q.w * q.y));
        f32 z = Atan2(2 * (q.y * q.z + q.w * q.x), q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z);

        return v3(x, y, z);
    }

    template<typename T>
    inline T Min(T a, T b)
    {
        return a < b ? a : b;
    }

    template<typename T>
    inline T Max(T a, T b)
    {
        return a > b ? a : b;
    }

    template<typename T>
    inline T Clamp(T value, T minimum, T maximum)
    {
        return Max(minimum, Min(value, maximum));
    }

    inline v3 ScreenToRaycastDirection(V2i position, v4 viewport, m4 viewProjection)
    {
        m4 inverse = Inverse(viewProjection);
        v4 worldNear(0.0f);
        v4 worldFar(0.0f);
        {
            // Convert to [-1, 1] space
            v4 nearClip = v4(0.0f, 0.0f, -1.0f, 1.0f);
            nearClip.x = ((2.0f * (position.x - viewport[0])) / viewport[2]) - 1.0f;
            nearClip.y = 1.0f - ((2.0f * (position.y - viewport[1])) / viewport[3]);

            // Unprojecting
            worldNear = inverse * nearClip;

            // Magic
            worldNear /= worldNear.w;
        }

        {
            // Convert to [-1, 1] space
            v4 farClip = v4(0.0f, 0.0f, 1.0f, 1.0f);
            farClip.x = ((2.0f * (position.x - viewport[0])) / viewport[2]) - 1.0f;
            farClip.y = 1.0f - ((2.0f * (position.y - viewport[1])) / viewport[3]);

            // Unprojecting
            worldFar = inverse * farClip;

            // Magic
            worldFar /= worldFar.w;
        }

        return v3(Normalize(worldFar - worldNear));
    }

    // Converts 2D screen coordinates to world position
    inline V2 OrthoScreenToWorld(V2i screenPos, V2i screenSize, m4 viewProjection)
    {
        // Convert screen space coordinates to normalized device coordinates (NDC)
        f32 normalizedX = (2.0f * screenPos.x) / screenSize.x - 1.0f;
        f32 normalizedY = 1.0f - (2.0f * screenPos.y) / screenSize.y; // Y is flipped in NDC

        // NDC position (with depth set to 0 since it's a 2D position)
        v4 ndcPosition = v4(normalizedX, normalizedY, 0.0f, 1.0f);

        // Invert the view-projection matrix
        m4 inverseViewProjMatrix = bkm::Inverse(viewProjection);

        // Unproject the NDC position back to world coordinates
        v4 worldPosition = inverseViewProjMatrix * ndcPosition;

        // Normalize by w component (homogeneous coordinates)
        if (worldPosition.w != 0.0f)
        {
            worldPosition /= worldPosition.w;
        }

        // Return the world position (as a Vector3)
        return V2(worldPosition.x, worldPosition.y);
    }
}

#include "BKM_Operators.h"
