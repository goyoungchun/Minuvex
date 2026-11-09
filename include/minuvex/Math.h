#pragma once

#include <array>

namespace lv {

struct Vec3 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

struct Quaternion {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
};

// Column-major storage, column vectors, right-handed coordinates.
struct Mat4 {
    std::array<float, 16> Elements{};

    static Mat4 Identity() noexcept;
    static Mat4 RotationY(float radians) noexcept;
    static Mat4 RotationX(float radians) noexcept;
    static Mat4 Translation(Vec3 position);
    static Mat4 Scale(Vec3 scale);
    static Mat4 Rotation(Quaternion rotation);

    // Depth after perspective divide is [0, 1] (D3D11 and Metal).
    // Invalid or non-finite arguments throw std::invalid_argument.
    static Mat4 Perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);

    // Rejects non-affine, non-finite and singular matrices.
    Mat4 NormalMatrix() const;
    double LinearDeterminant() const noexcept;
    Vec3 TransformPoint(Vec3 point) const noexcept;
};

struct Transform {
    Vec3 Position{};
    Quaternion Rotation{};
    Vec3 Scale{1.0f, 1.0f, 1.0f};

    // T * R * S; finite components, nonzero scale and quaternion required.
    Mat4 Matrix() const;
};

Mat4 operator*(const Mat4& left, const Mat4& right) noexcept;

} // namespace lv
