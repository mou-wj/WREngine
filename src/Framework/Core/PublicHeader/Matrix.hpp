#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <iostream>

namespace Core
{



    // ============================================================
    // Matrix Template
    // Row-major storage (like glm by default)
    // ============================================================

    template <typename T, std::size_t Rows, std::size_t Cols>
    class Matrix
    {
        static_assert(Rows > 0 && Cols > 0, "Matrix dimensions must be > 0");
        static_assert(std::is_arithmetic_v<T>, "Matrix scalar type must be arithmetic");

    public:
        using ScalarType = T;
        static constexpr std::size_t RowCount = Rows;
        static constexpr std::size_t ColCount = Cols;

        // Row-major storage: Elements[row][col]
        std::array<T, Rows* Cols> Elements{};

    public:
        // ------------------------------------------------------------
        // Constructors
        // ------------------------------------------------------------

        constexpr Matrix() : Matrix(0) {}

        constexpr explicit Matrix(T value)
        {
            for (std::size_t i = 0; i < Rows * Cols; ++i)
                Elements[i] = value;
        }

        // Construct from initializer list (row-major)
        constexpr Matrix(std::initializer_list<T> init)
        {
            std::size_t i = 0;
            for (T v : init)
            {
                if (i < Rows * Cols)
                    Elements[i++] = v;
            }
        }
        void Print(
            std::ostream& os = std::cout,
            int precision = 4) const
        {
            auto oldFlags = os.flags();
            auto oldPrecision = os.precision();

            for (std::size_t row = 0;
                row < Rows;
                ++row)
            {
                os << "[ ";

                for (std::size_t col = 0;
                    col < Cols;
                    ++col)
                {
                    os << operator()(row, col);

                    if (col < Cols - 1)
                    {
                        os << ", ";
                    }
                }

                os << " ]\n";
            }

            os.flags(oldFlags);
            os.precision(oldPrecision);
        }
   
        // ------------------------------------------------------------
        // Element Access
        // ------------------------------------------------------------

        constexpr T& operator()(std::size_t row, std::size_t col)
        {
            return Elements[row * Cols + col];
        }

        constexpr const T& operator()(std::size_t row, std::size_t col) const
        {
            return Elements[row * Cols + col];
        }

        constexpr T* Data() { return Elements.data(); }
        constexpr const T* Data() const { return Elements.data(); }

        constexpr T* operator[](std::size_t row)
        {
            return Elements.data() + row * Cols;
        }

        constexpr const T* operator[](std::size_t row) const
        {
            return Elements.data() + row * Cols;
        }

        // ------------------------------------------------------------
        // Identity (only for square matrices)
        // ------------------------------------------------------------

        static constexpr Matrix Identity()
        {
            static_assert(Rows == Cols, "Identity matrix must be square");

            Matrix result{};
            for (std::size_t i = 0; i < Rows; ++i)
                result(i, i) = static_cast<T>(1);
            return result;
        }
    };

    // ============================================================
    // Common Type Aliases (glm-like naming)
    // ============================================================

    // float matrices
    using Mat3 = Matrix<float, 3, 3>;
    using Mat3x4 = Matrix<float, 3, 4>;
    using Mat4 = Matrix<float, 4, 4>;

    // explicit-sized aliases (engine friendly)
    using Mat3f = Matrix<float, 3, 3>;
    using Mat3x4f = Matrix<float, 3, 4>;
    using Mat4f = Matrix<float, 4, 4>;

    using Mat3d = Matrix<double, 3, 3>;
    using Mat3x4d = Matrix<double, 3, 4>;
    using Mat4d = Matrix<double, 4, 4>;

    // ================= Matrix Operations =================

// Matrix + Matrix
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator+(
        const Matrix<T, Rows, Cols>& a, const Matrix<T, Rows, Cols>& b) {
        Matrix<T, Rows, Cols> result;
        for (std::size_t i = 0; i < Rows * Cols; ++i)
            result.Elements[i] = a.Elements[i] + b.Elements[i];
        return result;
    }

    // Matrix - Matrix
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator-(
        const Matrix<T, Rows, Cols>& a, const Matrix<T, Rows, Cols>& b) {
        Matrix<T, Rows, Cols> result;
        for (std::size_t i = 0; i < Rows * Cols; ++i)
            result.Elements[i] = a.Elements[i] - b.Elements[i];
        return result;
    }

    // Matrix * scalar
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator*(const Matrix<T, Rows, Cols>& m, T scalar) {
        Matrix<T, Rows, Cols> result;
        for (std::size_t i = 0; i < Rows * Cols; ++i)
            result.Elements[i] = m.Elements[i] * scalar;
        return result;
    }

    // scalar * Matrix
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator*(T scalar, const Matrix<T, Rows, Cols>& m) {
        return m * scalar;
    }

    // Matrix / scalar
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator/(const Matrix<T, Rows, Cols>& m, T scalar) {
        Matrix<T, Rows, Cols> result;
        for (std::size_t i = 0; i < Rows * Cols; ++i)
            result.Elements[i] = m.Elements[i] / scalar;
        return result;
    }

    // Matrix * Matrix (general, for compatible sizes)
    template<typename T, std::size_t Rows, std::size_t K, std::size_t Cols>
    constexpr Matrix<T, Rows, Cols> operator*(const Matrix<T, Rows, K>& a, const Matrix<T, K, Cols>& b) {
        Matrix<T, Rows, Cols> result{};
        for (std::size_t row = 0; row < Rows; ++row) {
            for (std::size_t col = 0; col < Cols; ++col) {
                T sum = T{};
                for (std::size_t k = 0; k < K; ++k)
                    sum += a(row, k) * b(k, col);
                result(row, col) = sum;
            }
        }
        return result;
    }

    // Matrix * Vector (std::array)
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr std::array<T, Rows> operator*(const Matrix<T, Rows, Cols>& m, const std::array<T, Cols>& v) {
        std::array<T, Rows> result{};
        for (std::size_t row = 0; row < Rows; ++row) {
            T sum = T{};
            for (std::size_t col = 0; col < Cols; ++col)
                sum += m(row, col) * v[col];
            result[row] = sum;
        }
        return result;
    }

    // Matrix transpose
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr Matrix<T, Cols, Rows> Transpose(const Matrix<T, Rows, Cols>& m) {
        Matrix<T, Cols, Rows> result;
        for (std::size_t row = 0; row < Rows; ++row)
            for (std::size_t col = 0; col < Cols; ++col)
                result(col, row) = m(row, col);
        return result;
    }

    // Matrix equality
    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr bool operator==(const Matrix<T, Rows, Cols>& a, const Matrix<T, Rows, Cols>& b) {
        for (std::size_t i = 0; i < Rows * Cols; ++i)
            if (a.Elements[i] != b.Elements[i])
                return false;
        return true;
    }

    template<typename T, std::size_t Rows, std::size_t Cols>
    constexpr bool operator!=(const Matrix<T, Rows, Cols>& a, const Matrix<T, Rows, Cols>& b) {
        return !(a == b);
    }

} // namespace math

// =================== General Determinant and Inverse Templates ===================
namespace Core {

// 3x3 Determinant

// 1x1 Determinant
template<typename T>
constexpr T Determinant(const Matrix<T, 1, 1>& m) {
    return m(0, 0);
}

// 2x2 Determinant
template<typename T>
constexpr T Determinant(const Matrix<T, 2, 2>& m) {
    return m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
}

// 3x3 Determinant
template<typename T>
constexpr T Determinant(const Matrix<T, 3, 3>& m) {
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1))
         - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0))
         + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

// 4x4 Determinant
template<typename T>
constexpr T Determinant(const Matrix<T, 4, 4>& m) {
    const T* a = m.Elements.data();
    T det =
        a[3] * a[6] * a[9] * a[12] - a[2] * a[7] * a[9] * a[12] - a[3] * a[5] * a[10] * a[12] + a[1] * a[7] * a[10] * a[12]
      + a[2] * a[5] * a[11] * a[12] - a[1] * a[6] * a[11] * a[12] - a[3] * a[6] * a[8] * a[13] + a[2] * a[7] * a[8] * a[13]
      + a[3] * a[4] * a[10] * a[13] - a[0] * a[7] * a[10] * a[13] - a[2] * a[4] * a[11] * a[13] + a[0] * a[6] * a[11] * a[13]
      + a[3] * a[5] * a[8] * a[14] - a[1] * a[7] * a[8] * a[14] - a[3] * a[4] * a[9] * a[14] + a[0] * a[7] * a[9] * a[14]
      + a[1] * a[4] * a[11] * a[14] - a[0] * a[5] * a[11] * a[14] - a[2] * a[5] * a[8] * a[15] + a[1] * a[6] * a[8] * a[15]
      + a[2] * a[4] * a[9] * a[15] - a[0] * a[6] * a[9] * a[15] - a[1] * a[4] * a[10] * a[15] + a[0] * a[5] * a[10] * a[15];
    return det;
}


// =================== Adjugate (伴随矩阵) ===================
// 1x1 Adjugate
template<typename T>
constexpr Matrix<T, 1, 1> Adjugate(const Matrix<T, 1, 1>& m) {
    return Matrix<T, 1, 1>{1};
}

// 2x2 Adjugate
template<typename T>
constexpr Matrix<T, 2, 2> Adjugate(const Matrix<T, 2, 2>& m) {
    Matrix<T, 2, 2> adj;
    adj(0,0) =  m(1,1);
    adj(0,1) = -m(0,1);
    adj(1,0) = -m(1,0);
    adj(1,1) =  m(0,0);
    return adj;
}

// 3x3 Adjugate
template<typename T>
constexpr Matrix<T, 3, 3> Adjugate(const Matrix<T, 3, 3>& m) {
    Matrix<T, 3, 3> adj;
    adj(0,0) =  (m(1,1) * m(2,2) - m(1,2) * m(2,1));
    adj(0,1) = -(m(1,0) * m(2,2) - m(1,2) * m(2,0));
    adj(0,2) =  (m(1,0) * m(2,1) - m(1,1) * m(2,0));
    adj(1,0) = -(m(0,1) * m(2,2) - m(0,2) * m(2,1));
    adj(1,1) =  (m(0,0) * m(2,2) - m(0,2) * m(2,0));
    adj(1,2) = -(m(0,0) * m(2,1) - m(0,1) * m(2,0));
    adj(2,0) =  (m(0,1) * m(1,2) - m(0,2) * m(1,1));
    adj(2,1) = -(m(0,0) * m(1,2) - m(0,2) * m(1,0));
    adj(2,2) =  (m(0,0) * m(1,1) - m(0,1) * m(1,0));
    return Transpose(adj);
}

// 4x4 Adjugate
template<typename T>
constexpr Matrix<T, 4, 4> Adjugate(const Matrix<T, 4, 4>& m) {
    Matrix<T, 4, 4> adj;
    // 按照代数余子式展开，直接写出每一项
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            Matrix<T, 3, 3> minor;
            int mi = 0;
            for (int row = 0; row < 4; ++row) {
                if (row == i) continue;
                int mj = 0;
                for (int col = 0; col < 4; ++col) {
                    if (col == j) continue;
                    minor(mi, mj) = m(row, col);
                    ++mj;
                }
                ++mi;
            }
            T cofactor = Determinant(minor);
            if ((i + j) % 2) cofactor = -cofactor;
            adj(j, i) = cofactor; // 注意转置
        }
    }
    return adj;
}



// 1x1~4x4 Inverse: Inverse = Adjugate / Determinant
template<typename T>
constexpr Matrix<T, 1, 1> Inverse(const Matrix<T, 1, 1>& m) {
    return Adjugate(m) / Determinant(m);
}

template<typename T>
constexpr Matrix<T, 2, 2> Inverse(const Matrix<T, 2, 2>& m) {
    return Adjugate(m) / Determinant(m);
}

template<typename T>
constexpr Matrix<T, 3, 3> Inverse(const Matrix<T, 3, 3>& m) {
    return Adjugate(m) / Determinant(m);
}

template<typename T>
constexpr Matrix<T, 4, 4> Inverse(const Matrix<T, 4, 4>& m) {
    return Adjugate(m) / Determinant(m);
}





} // namespace Core