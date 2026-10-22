#pragma once
#include "Matrix.hpp"
#include "Math.hpp"
namespace Core{

    // LookAt (right-handed) 看向z轴负方向
    inline Mat4 LookAtRH(const Float3& eye, const Float3& center, const Float3& up) {
        Float3 f =
            Normalize(center - eye);

        Float3 s =
            Normalize(Cross(f, up));

        Float3 u =
            Cross(s, f);

        Mat4 m =
            Mat4::Identity();

        m(0, 0) = s.X;
        m(0, 1) = s.Y;
        m(0, 2) = s.Z;
        m(0, 3) = -Dot(s, eye);

        m(1, 0) = u.X;
        m(1, 1) = u.Y;
        m(1, 2) = u.Z;
        m(1, 3) = -Dot(u, eye);

        m(2, 0) = -f.X;
        m(2, 1) = -f.Y;
        m(2, 2) = -f.Z;
        m(2, 3) = Dot(f, eye);

        return m;
    }

    // Perspective projection (right-handed)
    inline Mat4 PerspectiveRhNo(float fovy, float aspect, float zNear, float zFar) {
        float tanHalfFovy = std::tan(fovy / 2.0f);
        Mat4 result{};
        result(0, 0) = 1.0f / (aspect * tanHalfFovy);
        result(1, 1) = 1.0f / tanHalfFovy;
        result(2, 2) = -(zFar + zNear) / (zFar - zNear);
        result(2, 3) = -2.0f * zFar * zNear / (zFar - zNear);
        result(3, 2) = -1.0f;
        return result;
    }
    inline Mat4 PerspectiveRhZo(
        float fovy,
        float aspect,
        float zNear,
        float zFar)
    {
        float tanHalfFovy = std::tan(fovy / 2.0f);

        Mat4 result{};

        result(0, 0) = 1.0f / (aspect * tanHalfFovy);
        result(1, 1) = 1.0f / tanHalfFovy;

        result(2, 2) = zFar / (zNear - zFar);
        result(2, 3) = zFar * zNear / (zNear - zFar);

        result(3, 2) = -1.0f;

        return result;
    }
    // Orthographic projection (right-handed)
    inline Mat4 OrthoRhNo(float left, float right, float bottom, float top, float zNear, float zFar) {
        Mat4 result = Mat4::Identity();
        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = -2.0f / (zFar - zNear);
        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -(zFar + zNear) / (zFar - zNear);
        return result;
    }
    inline Mat4 OrthoRhBoundsNo(
        float left,
        float right,
        float bottom,
        float top,
        float minZ,
        float maxZ) {
        Mat4 result = Mat4::Identity();

        result(0, 0) = 2 / (right - left);
        result(1, 1) = 2 / (top - bottom);
        result(2, 2) = 2 / (maxZ - minZ);

        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -(maxZ + minZ) / (maxZ - minZ);
        return result;
    }

    inline Mat4 OrthoRhZo(
        float left,
        float right,
        float bottom,
        float top,
        float zNear,
        float zFar)
    {
        Mat4 result = Mat4::Identity();

        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = -1.0f / (zFar - zNear);

        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -zNear / (zFar - zNear);

        return result;
    }
    inline Mat4 OrthoRhBoundsZo(
        float left,
        float right,
        float bottom,
        float top,
        float minZ,
        float maxZ)
    {
        Mat4 result = Mat4::Identity();

        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = 1.0f / (maxZ - minZ);

        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -minZ / (maxZ - minZ);

        return result;
    }

    template<typename T>
    constexpr Matrix<T, 4, 4> MakeTranslationMatrix(
        const Vec3<T>& translation)
    {
        Matrix<T, 4, 4> result =
            Matrix<T, 4, 4>::Identity();

        result(0, 3) = translation.X;
        result(1, 3) = translation.Y;
        result(2, 3) = translation.Z;

        return result;
    }

    template<typename T>
    constexpr Matrix<T, 4, 4> MakeScaleMatrix(
        const Vec3<T>& scale)
    {
        Matrix<T, 4, 4> result =
            Matrix<T, 4, 4>::Identity();

        result(0, 0) = scale.X;
        result(1, 1) = scale.Y;
        result(2, 2) = scale.Z;

        return result;
    }

    template<typename T>
    Matrix<T, 4, 4> MakeRotationMatrix(
        const Vec3<T>& rotation)
    {
        constexpr T degToRad =
            static_cast<T>(
                0.017453292519943295769f);

        T pitch =
            rotation.X * degToRad;

        T yaw =
            rotation.Z * degToRad;

        T roll =
            rotation.Y * degToRad;

        T cp = std::cos(pitch);
        T sp = std::sin(pitch);

        T cy = std::cos(yaw);
        T sy = std::sin(yaw);

        T cr = std::cos(roll);
        T sr = std::sin(roll);

        Matrix<T, 4, 4> rx =
            Matrix<T, 4, 4>::Identity();

        rx(1, 1) = cp;
        rx(1, 2) = -sp;
        rx(2, 1) = sp;
        rx(2, 2) = cp;

        Matrix<T, 4, 4> ry =
            Matrix<T, 4, 4>::Identity();

        ry(0, 0) = cr;
        ry(0, 2) = sr;
        ry(2, 0) = -sr;
        ry(2, 2) = cr;

        Matrix<T, 4, 4> rz =
            Matrix<T, 4, 4>::Identity();

        rz(0, 0) = cy;
        rz(0, 1) = -sy;
        rz(1, 0) = sy;
        rz(1, 1) = cy;

        return rz * ry * rx;
    }

    template<typename T>
    Matrix<T, 4, 4> MakeTransformMatrix(
        const Vec3<T>& location,
        const Vec3<T>& rotation,
        const Vec3<T>& scale)
    {
        return
            MakeTranslationMatrix(location)
            * MakeRotationMatrix(rotation)
            * MakeScaleMatrix(scale);
    }

    template<typename T>
    constexpr Vec3<T> GetTranslation(
        const Matrix<T, 4, 4>& matrix)
    {
        return Vec3<T>(
            matrix(0, 3),
            matrix(1, 3),
            matrix(2, 3));
    }

    template<typename T>
    Vec3<T> GetScale(
        const Matrix<T, 4, 4>& matrix)
    {
        Vec3<T> x(
            matrix(0, 0),
            matrix(1, 0),
            matrix(2, 0));

        Vec3<T> y(
            matrix(0, 1),
            matrix(1, 1),
            matrix(2, 1));

        Vec3<T> z(
            matrix(0, 2),
            matrix(1, 2),
            matrix(2, 2));

        return Vec3<T>(
            x.Length(),
            y.Length(),
            z.Length());
    }

    template<typename T>
    Vec3<T> GetEulerRotation(
        const Matrix<T, 4, 4>& matrix)
    {
        Vec3<T> scale =
            GetScale(matrix);

        T m00 =
            matrix(0, 0) / scale.X;

        T m10 =
            matrix(1, 0) / scale.X;

        T m20 =
            matrix(2, 0) / scale.X;

        T m21 =
            matrix(2, 1) / scale.Y;

        T m22 =
            matrix(2, 2) / scale.Z;

        T pitch =
            std::atan2(m21, m22);

        T yaw =
            std::atan2(
                -m20,
                std::sqrt(
                    m21 * m21 +
                    m22 * m22));

        T roll =
            std::atan2(m10, m00);

        constexpr T radToDeg =
            static_cast<T>(
                57.295779513082320876f);

        return Vec3<T>(
            pitch * radToDeg,
            roll * radToDeg,
            yaw * radToDeg);
    }

    template<typename T>
    constexpr Vec3<T> TransformPoint(
        const Matrix<T, 4, 4>& matrix,
        const Vec3<T>& point)
    {
        return Vec3<T>(
            matrix(0, 0) * point.X +
            matrix(0, 1) * point.Y +
            matrix(0, 2) * point.Z +
            matrix(0, 3),

            matrix(1, 0) * point.X +
            matrix(1, 1) * point.Y +
            matrix(1, 2) * point.Z +
            matrix(1, 3),

            matrix(2, 0) * point.X +
            matrix(2, 1) * point.Y +
            matrix(2, 2) * point.Z +
            matrix(2, 3));
    }

    template<typename T>
    constexpr Vec3<T> TransformVector(
        const Matrix<T, 4, 4>& matrix,
        const Vec3<T>& vector)
    {
        return Vec3<T>(
            matrix(0, 0) * vector.X +
            matrix(0, 1) * vector.Y +
            matrix(0, 2) * vector.Z,

            matrix(1, 0) * vector.X +
            matrix(1, 1) * vector.Y +
            matrix(1, 2) * vector.Z,

            matrix(2, 0) * vector.X +
            matrix(2, 1) * vector.Y +
            matrix(2, 2) * vector.Z);
    }


}