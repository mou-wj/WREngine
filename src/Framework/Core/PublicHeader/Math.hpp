// Vector.hpp
#pragma once
#include <array>
#include <cmath>
#include "Matrix.hpp"

namespace Core {

    template<typename T>
    struct Vec2
    {
        union
        {
            std::array<T, 2> Data;
            struct { T X, Y; };
        };

        Vec2() : Data{ T(0), T(0) } {}
        Vec2(T x, T y) : X(x), Y(y) {}

        T& operator[](size_t i) { return Data[i]; }
        const T& operator[](size_t i) const { return Data[i]; }

        Vec2 operator+(const Vec2& rhs) const
        {
            return { X + rhs.X, Y + rhs.Y };
        }

        Vec2 operator-(const Vec2& rhs) const
        {
            return { X - rhs.X, Y - rhs.Y };
        }

        Vec2 operator*(T scalar) const
        {
            return { X * scalar, Y * scalar };
        }

        Vec2 operator/(T scalar) const
        {
            return { X / scalar, Y / scalar };
        }

        Vec2& operator+=(const Vec2& rhs)
        {
            X += rhs.X;
            Y += rhs.Y;
            return *this;
        }

        Vec2& operator-=(const Vec2& rhs)
        {
            X -= rhs.X;
            Y -= rhs.Y;
            return *this;
        }

        Vec2& operator*=(T scalar)
        {
            X *= scalar;
            Y *= scalar;
            return *this;
        }

        Vec2& operator/=(T scalar)
        {
            X /= scalar;
            Y /= scalar;
            return *this;
        }

        Vec2 operator-() const
        {
            return { -X, -Y };
        }

        bool operator==(const Vec2& rhs) const
        {
            return X == rhs.X &&
                Y == rhs.Y;
        }

        bool operator!=(const Vec2& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator<(const Vec2& rhs) const
        {
            return X < rhs.X &&
                Y < rhs.Y;
        }

        bool operator>(const Vec2& rhs) const
        {
            return X > rhs.X &&
                Y > rhs.Y;
        }

        bool operator<=(const Vec2& rhs) const
        {
            return X <= rhs.X &&
                Y <= rhs.Y;
        }

        bool operator>=(const Vec2& rhs) const
        {
            return X >= rhs.X &&
                Y >= rhs.Y;
        }

        T LengthSquared() const
        {
            return X * X + Y * Y;
        }

        T Length() const
        {
            return static_cast<T>(
                std::sqrt(LengthSquared()));
        }
    };

    template<typename T>
    struct Vec3
    {
        union
        {
            std::array<T, 3> Data;
            struct { T X, Y, Z; };
        };

        Vec3() : Data{ T(0), T(0), T(0) } {}
        Vec3(T x, T y, T z)
            : X(x), Y(y), Z(z)
        {
        }

        T& operator[](size_t i) { return Data[i]; }
        const T& operator[](size_t i) const { return Data[i]; }

        Vec3 operator+(const Vec3& rhs) const
        {
            return {
                X + rhs.X,
                Y + rhs.Y,
                Z + rhs.Z
            };
        }

        Vec3 operator-(const Vec3& rhs) const
        {
            return {
                X - rhs.X,
                Y - rhs.Y,
                Z - rhs.Z
            };
        }

        Vec3 operator*(T scalar) const
        {
            return {
                X * scalar,
                Y * scalar,
                Z * scalar
            };
        }

        Vec3 operator/(T scalar) const
        {
            return {
                X / scalar,
                Y / scalar,
                Z / scalar
            };
        }

        Vec3 operator-() const
        {
            return { -X, -Y, -Z };
        }

        Vec3& operator+=(const Vec3& rhs)
        {
            X += rhs.X;
            Y += rhs.Y;
            Z += rhs.Z;
            return *this;
        }

        Vec3& operator-=(const Vec3& rhs)
        {
            X -= rhs.X;
            Y -= rhs.Y;
            Z -= rhs.Z;
            return *this;
        }

        Vec3& operator*=(T scalar)
        {
            X *= scalar;
            Y *= scalar;
            Z *= scalar;
            return *this;
        }

        Vec3& operator/=(T scalar)
        {
            X /= scalar;
            Y /= scalar;
            Z /= scalar;
            return *this;
        }

        bool operator==(const Vec3& rhs) const
        {
            return X == rhs.X &&
                Y == rhs.Y &&
                Z == rhs.Z;
        }

        bool operator!=(const Vec3& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator<(const Vec3& rhs) const
        {
            return X < rhs.X &&
                Y < rhs.Y &&
                Z < rhs.Z;
        }

        bool operator>(const Vec3& rhs) const
        {
            return X > rhs.X &&
                Y > rhs.Y &&
                Z > rhs.Z;
        }

        bool operator<=(const Vec3& rhs) const
        {
            return X <= rhs.X &&
                Y <= rhs.Y &&
                Z <= rhs.Z;
        }

        bool operator>=(const Vec3& rhs) const
        {
            return X >= rhs.X &&
                Y >= rhs.Y &&
                Z >= rhs.Z;
        }

        T LengthSquared() const
        {
            return X * X +
                Y * Y +
                Z * Z;
        }

        T Length() const
        {
            return static_cast<T>(
                std::sqrt(
                    LengthSquared()));
        }
    };

    template<typename T>
    struct alignas(16) Vec4
    {
        union
        {
            std::array<T, 4> Data;
            struct { T X, Y, Z, W; };
        };

        Vec4() : Data{ T(0), T(0), T(0), T(0) } {}

        Vec4(T ix, T iy, T iz, T iw)
            : X(ix), Y(iy), Z(iz), W(iw)
        {
        }
        Vec4(const std::array<T, 4>& data)
            : Data(data)
        {
        }
        Vec4(const Vec3<T>& v3,T iw)
            : X(v3.X), Y(v3.Y), Z(v3.Z), W(iw)
        {
        }
        T& operator[](size_t i) { return Data[i]; }
        const T& operator[](size_t i) const { return Data[i]; }

        Vec4 operator+(const Vec4& rhs) const
        {
            return { X + rhs.X, Y + rhs.Y, Z + rhs.Z, W + rhs.W };
        }

        Vec4 operator-(const Vec4& rhs) const
        {
            return { X - rhs.X, Y - rhs.Y, Z - rhs.Z, W - rhs.W };
        }

        Vec4 operator*(T scalar) const
        {
            return { X * scalar, Y * scalar, Z * scalar, W * scalar };
        }

        Vec4 operator/(T scalar) const
        {
            return { X / scalar, Y / scalar, Z / scalar, W / scalar };
        }

        Vec4 operator-() const
        {
            return { -X, -Y, -Z, -W };
        }

        Vec4& operator+=(const Vec4& rhs)
        {
            X += rhs.X;
            Y += rhs.Y;
            Z += rhs.Z;
            W += rhs.W;
            return *this;
        }

        Vec4& operator-=(const Vec4& rhs)
        {
            X -= rhs.X;
            Y -= rhs.Y;
            Z -= rhs.Z;
            W -= rhs.W;
            return *this;
        }

        Vec4& operator*=(T scalar)
        {
            X *= scalar;
            Y *= scalar;
            Z *= scalar;
            W *= scalar;
            return *this;
        }

        Vec4& operator/=(T scalar)
        {
            X /= scalar;
            Y /= scalar;
            Z /= scalar;
            W /= scalar;
            return *this;
        }

        bool operator==(const Vec4& rhs) const
        {
            return X == rhs.X &&
                Y == rhs.Y &&
                Z == rhs.Z &&
                W == rhs.W;
        }

        bool operator!=(const Vec4& rhs) const
        {
            return !(*this == rhs);
        }

        bool operator<(const Vec4& rhs) const
        {
            return X < rhs.X &&
                Y < rhs.Y &&
                Z < rhs.Z &&
                W < rhs.W;
        }

        bool operator>(const Vec4& rhs) const
        {
            return X > rhs.X &&
                Y > rhs.Y &&
                Z > rhs.Z &&
                W > rhs.W;
        }

        bool operator<=(const Vec4& rhs) const
        {
            return X <= rhs.X &&
                Y <= rhs.Y &&
                Z <= rhs.Z &&
                W <= rhs.W;
        }

        bool operator>=(const Vec4& rhs) const
        {
            return X >= rhs.X &&
                Y >= rhs.Y &&
                Z >= rhs.Z &&
                W >= rhs.W;
        }

        T LengthSquared() const
        {
            return X * X +
                Y * Y +
                Z * Z +
                W * W;
        }

        T Length() const
        {
            return static_cast<T>(
                std::sqrt(LengthSquared()));
        }
    };

    // ---------------------------------
    // Utility Functions
    // ---------------------------------

    template<typename T>
    T Dot(const Vec2<T>& a,
        const Vec2<T>& b)
    {
        return a.X * b.X +
            a.Y * b.Y;
    }

    template<typename T>
    T Dot(const Vec3<T>& a,
        const Vec3<T>& b)
    {
        return a.X * b.X +
            a.Y * b.Y +
            a.Z * b.Z;
    }

    template<typename T>
    Vec3<T> Cross(
        const Vec3<T>& a,
        const Vec3<T>& b)
    {
        return {
            a.Y * b.Z -
            a.Z * b.Y,

            a.Z * b.X -
            a.X * b.Z,

            a.X * b.Y -
            a.Y * b.X
        };
    }

    template<typename V>
    V Normalize(const V& v)
    {
        auto len = v.Length();

        if (len <= 0)
        {
            return v;
        }

        return v / len;
    }

    template<typename V>
    auto Distance(
        const V& a,
        const V& b)
    {
        return (a - b).Length();
    }

    template<typename V, typename T>
    V Lerp(
        const V& a,
        const V& b,
        T t)
    {
        return a * (T(1) - t)
            + b * t;
    }

    // ---------------------------------
    // Aliases
    // ---------------------------------

    using Int2 = Vec2<int>;
    using Int3 = Vec3<int>;
    using Int4 = Vec4<int>;

    using UInt2 = Vec2<unsigned int>;
    using UInt3 = Vec3<unsigned int>;
    using UInt4 = Vec4<unsigned int>;

    using Float2 = Vec2<float>;
    using Float3 = Vec3<float>;
    using Float4 = Vec4<float>;

    using Double2 = Vec2<double>;
    using Double3 = Vec3<double>;
    using Double4 = Vec4<double>;
    using Float4x4 = Mat4;


    template<typename T>
    constexpr T Pi()
    {
        static_assert(
            std::is_floating_point_v<T>,
            "Pi only supports floating point types");

        return static_cast<T>(
            3.1415926535897932384626433832795);
    }

    template<typename T>
    constexpr T DegToRad(T degree)
    {
        static_assert(
            std::is_floating_point_v<T>,
            "DegToRad only supports floating point types");

        return degree *
            (Pi<T>() / static_cast<T>(180.0));
    }

    template<typename T>
    constexpr T RadToDeg(T radian)
    {
        static_assert(
            std::is_floating_point_v<T>,
            "RadToDeg only supports floating point types");

        return radian *
            (static_cast<T>(180.0) / Pi<T>());
    }

} // namespace Core