#include <minuvex/Math.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace lv {
namespace {

Vec3 Subtract(Vec3 a, Vec3 b) noexcept {
    return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
}

float Dot(Vec3 a, Vec3 b) noexcept {
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

Vec3 Cross(Vec3 a, Vec3 b) noexcept {
    return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
}

Vec3 Normalize(Vec3 v) {
    const float length = std::hypot(v.X, v.Y, v.Z);
    if (!std::isfinite(length) || length <= 0.0f) {
        throw std::invalid_argument("LookAt requires finite, non-degenerate directions.");
    }
    return {v.X / length, v.Y / length, v.Z / length};
}

} // namespace

Mat4 Mat4::Identity() noexcept {
    Mat4 result;
    result.Elements = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return result;
}

Mat4 Mat4::RotationY(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result;
    result.Elements = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    return result;
}

Mat4 Mat4::RotationX(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result;
    result.Elements = {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
    return result;
}

Mat4 Mat4::Perspective(float fov, float aspect, float nearPlane, float farPlane) {
    if (!std::isfinite(fov) || !std::isfinite(aspect) || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane) || fov <= 0 || fov >= std::numbers::pi_v<float> ||
        aspect <= 0 || nearPlane <= 0 || farPlane <= nearPlane) {
        throw std::invalid_argument("Perspective requires 0 < FOV < pi, aspect > 0 and 0 < near < far.");
    }
    const float focal = 1.0f / std::tan(fov * 0.5f);
    Mat4 result;
    result.Elements[0] = focal / aspect;
    result.Elements[5] = focal;
    result.Elements[10] = farPlane / (nearPlane - farPlane);
    result.Elements[11] = -1.0f;
    result.Elements[14] = nearPlane * result.Elements[10];
    return result;
}

Mat4 Mat4::LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = Normalize(Subtract(target, eye));
    const Vec3 right = Normalize(Cross(forward, Normalize(up)));
    const Vec3 cameraUp = Cross(right, forward);
    Mat4 result;
    result.Elements = {
        right.X, cameraUp.X, -forward.X, 0,
        right.Y, cameraUp.Y, -forward.Y, 0,
        right.Z, cameraUp.Z, -forward.Z, 0,
        -Dot(right, eye), -Dot(cameraUp, eye), Dot(forward, eye), 1
    };
    return result;
}

Mat4 operator*(const Mat4& left, const Mat4& right) noexcept {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.Elements[column * 4 + row] += left.Elements[k * 4 + row] * right.Elements[column * 4 + k];
            }
        }
    }
    return result;
}

} // namespace lv
