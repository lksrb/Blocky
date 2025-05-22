#pragma once

struct v3;
struct v4;
struct m4;

// Base
template<typename T>
struct v2b
{
    T x, y;

    v2b() = default;

    constexpr v2b(T x, T y)
    {
        this->x = x;
        this->y = y;
    }

    explicit v2b(const v3& v);

    constexpr explicit v2b(T scalar)
    {
        x = y = scalar;
    }

    // Access operators
    T& operator[](u32 index)
    {
        Assert(index < 3, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
        }
    }

    const T& operator[](u32 index) const
    {
        Assert(index < 3, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
        }
    }

    // Unary arithmetic operators
    v2b& operator+=(const v2b& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    v2b& operator-=(const v2b& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    v2b& operator*=(T scalar)
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    v2b& operator/=(T scalar)
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    v2b operator-() const
    {
        return v2b(-x, -y);
    }

    // Equality operators
    bool operator==(const v2b& other) const
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const v2b& other) const
    {
        return !(*this == other);
    }
};

struct v3
{
    f32 x, y, z;

    v3() = default;

    constexpr v3(f32 x, f32 y, f32 z)
    {
        this->x = x;
        this->y = y;
        this->z = z;
    }

    constexpr explicit v3(f32 scalar)
    {
        this->x = this->y = this->z = scalar;
    }

    template<typename T>
    constexpr explicit v3(const v2b<T>& v, f32 z);

    explicit v3(const v4& v);

    // Access operators
    f32& operator[](u32 index)
    {
        Assert(index < 3, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
        }
    }

    const f32& operator[](u32 index) const
    {
        Assert(index < 3, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
        }
    }

    // Unary arithmetic operators
    v3& operator+=(const v3& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    v3& operator-=(const v3& other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    v3& operator*=(f32 scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    v3& operator/=(f32 scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    v3 operator-() const
    {
        return v3(-x, -y, -z);
    }

    // Equality operators
    bool operator==(const v3& other) const
    {
        return bkm::Equals(x, other.x) && bkm::Equals(y, other.y) && bkm::Equals(z, other.z);
    }

    bool operator!=(const v3& other) const
    {
        return !(*this == other);
    }
};

struct v4
{
    f32 x, y, z, w;

    v4() = default;

    constexpr v4(f32 x, f32 y, f32 z, f32 w)
    {
        this->x = x;
        this->y = y;
        this->z = z;
        this->w = w;
    }

    constexpr explicit v4(v3 v, f32 w)
    {
        this->x = v.x;
        this->y = v.y;
        this->z = v.z;
        this->w = w;
    }

    explicit v4(f32 scalar)
    {
        x = y = z = w = scalar;
    }

    // Access operators
    f32& operator[](u32 index)
    {
        Assert(index < 4, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            case 3:
                return w;
        }
    }

    const f32& operator[](u32 index) const
    {
        Assert(index < 4, "Indexing out of bounds.");
        switch (index)
        {
            default:
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            case 3:
                return w;
        }
    }

    // Unary arithmetic operators
    v4& operator+=(const v4& other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    v4& operator-=(const v4& other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    v4& operator*=(f32 scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    v4& operator/=(f32 scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    v4 operator-() const
    {
        return v4(-x, -y, -z, -w);
    }

    // Equality operators
    bool operator==(const v4& other) const
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    bool operator!=(const v4& other) const
    {
        return !(*this == other);
    }
};

struct m4;

// Column major matrix
struct m3
{
    v3 columns[3];

    m3() = default;
    explicit m3(f32 scalar)
    {
        columns[0] = v3(scalar, 0.0f, 0.0f);
        columns[1] = v3(0.0f, scalar, 0.0f);
        columns[2] = v3(0.0f, 0.0f, scalar);
    }

    m3(f32 x0, f32 y0, f32 z0,
        f32 x1, f32 y1, f32 z1,
        f32 x2, f32 y2, f32 z2)
    {
        columns[0] = v3(x0, y0, z0);
        columns[1] = v3(x1, y1, z1);
        columns[2] = v3(x2, y2, z2);
    }

    m3(const v3& vec0, const v3& vec1, const v3& vec2)
    {
        columns[0] = vec0;
        columns[1] = vec1;
        columns[2] = vec2;
    }

    explicit m3(const m4& m);

    // Access operators
    v3& operator[](u32 index)
    {
        Assert(index < 3, "Indexing out of bounds.");
        return columns[index];
    }

    const v3& operator[](u32 index) const
    {
        Assert(index < 3, "Indexing out of bounds.");
        return columns[index];
    }
};

// Column major matrix
struct m4
{
    v4 columns[4];

    m4() = default;
    explicit m4(f32 scalar)
    {
        columns[0] = v4(scalar, 0.0f, 0.0f, 0.0f);
        columns[1] = v4(0.0f, scalar, 0.0f, 0.0f);
        columns[2] = v4(0.0f, 0.0f, scalar, 0.0f);
        columns[3] = v4(0.0f, 0.0f, 0.0f, scalar);
    }

    m4(f32 x0, f32 y0, f32 z0, f32 w0,
        f32 x1, f32 y1, f32 z1, f32 w1,
        f32 x2, f32 y2, f32 z2, f32 w2,
        f32 x3, f32 y3, f32 z3, f32 w3)
    {
        columns[0] = v4(x0, y0, z0, w0);
        columns[1] = v4(x1, y1, z1, w1);
        columns[2] = v4(x2, y2, z2, w2);
        columns[3] = v4(x3, y3, z3, w3);
    }

    m4(const v4& vec0, const v4& vec1, const v4& vec2, const v4& vec3)
    {
        columns[0] = vec0;
        columns[1] = vec1;
        columns[2] = vec2;
        columns[3] = vec3;
    }

    explicit m4(const m3& m)
    {
        columns[0] = v4(m[0], 0.0f);
        columns[1] = v4(m[1], 0.0f);
        columns[2] = v4(m[2], 0.0f);
        columns[3] = v4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Access operators
    v4& operator[](u32 index)
    {
        Assert(index < 4, "Indexing out of bounds.");
        return columns[index];
    }

    const v4& operator[](u32 index) const
    {
        Assert(index < 4, "Indexing out of bounds.");
        return columns[index];
    }
};

// Quaternion
struct qtn
{
    f32 w, x, y, z;

    explicit qtn(f32 w, f32 x, f32 y, f32 z)
    {
        this->w = w;
        this->x = x;
        this->y = y;
        this->z = z;
    }

    explicit qtn(const v3& eulerAngle)
    {
        v3 c = v3(bkm::Cos(eulerAngle.x * 0.5f), bkm::Cos(eulerAngle.y * 0.5f), bkm::Cos(eulerAngle.z * 0.5f));
        v3 s = v3(bkm::Sin(eulerAngle.x * 0.5f), bkm::Sin(eulerAngle.y * 0.5f), bkm::Sin(eulerAngle.z * 0.5f));

        w = c.x * c.y * c.z + s.x * s.y * s.z;
        x = s.x * c.y * c.z - c.x * s.y * s.z;
        y = c.x * s.y * c.z + s.x * c.y * s.z;
        z = c.x * c.y * s.z - s.x * s.y * c.z;
    }

    qtn operator-() const
    {
        return qtn(-w, -x, -y, -z);
    }
};

using v2 = v2b<f32>;
using v2i = v2b<i32>;

// TODO: Should this be here? 
// === Constructors ===

template<typename T>
v2b<T>::v2b(const v3& v)
{
    x = v.x;
    y = v.y;
}

inline v3::v3(const v4& v)
{
    x = v.x;
    y = v.y;
    z = v.z;
}

template<typename T>
constexpr v3::v3(const v2b<T>& v, f32 z)
{
    this->x = v.x;
    this->y = v.y;
    this->z = z;
}

inline m3::m3(const m4& m)
{
    columns[0] = v3(m[0]);
    columns[1] = v3(m[1]);
    columns[2] = v3(m[2]);
}

// === Forward declarations ===

// Vector2
template<typename T> inline v2b<T> operator+(const v2b<T>& v0, const v2b<T>& v1);
template<typename T> inline v2b<T> operator-(const v2b<T>& v0, const v2b<T>& v1);
template<typename T> inline v2b<T> operator*(const v2b<T>& v0, const v2b<T>& v1);
template<typename T> inline v2b<T> operator/(const v2b<T>& v0, const v2b<T>& v1);
template<typename T> inline v2b<T> operator*(const v2b<T>& v0, f32 scalar);
template<typename T> inline v2b<T> operator*(f32 scalar, const v2b<T>& v0);

// Vector3
inline v3 operator+(const v3& v0, const v3& v1);
inline v3 operator-(const v3& v0, const v3& v1);
inline v3 operator*(const v3& v0, const v3& v1);
inline v3 operator/(const v3& v0, const v3& v1);
inline v3 operator*(const v3& v0, f32 scalar);
inline v3 operator*(const m3& m, const v3& v);

// Vector4
inline v4 operator+(const v4& v0, const v4& v1);
inline v4 operator-(const v4& v0, const v4& v1);
inline v4 operator*(const v4& v0, const v4& v1);
inline v4 operator/(const v4& v0, const v4& v1);
inline v4 operator*(const v4& v0, f32 scalar);

// Matrix4
inline m4 operator*(const m4& m, f32 scalar);
inline m4 operator*(const m4& m1, const m4& m2);
inline v4 operator*(const m4& m, const v4& v);
inline v4 operator*(const v4& v, const m4& m);

// Quaternion
inline v3 operator*(const qtn& q, const v3& v);
