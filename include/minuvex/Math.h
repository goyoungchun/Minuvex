#pragma once

#include <array>

namespace lv {

struct Vec3 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

// Column-major storage, column vectors, right-handed coordinates.
struct alignas(16) Mat4 {
    std::array<float, 16> Elements{};

    static Mat4 Identity() noexcept;
    static Mat4 RotationY(float radians) noexcept;
    static Mat4 RotationX(float radians) noexcept;

    // Depth after perspective divide is [0, 1] (D3D11 and Metal).
    // Invalid or non-finite arguments throw std::invalid_argument.
    static Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);
};

Mat4 operator*(const Mat4& left, const Mat4& right) noexcept;

} // namespace lv
