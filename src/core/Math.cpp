#include <minuvex/Math.h>

#include <cmath>
#include <algorithm>
#include <numbers>
#include <stdexcept>

namespace lv {

Transform DecomposeTRS(const Mat4& matrix) {
    (void)matrix.NormalMatrix();
    const auto& m = matrix.Elements;
    Transform t; t.Position = {m[12],m[13],m[14]};
    double x = std::hypot(double(m[0]),double(m[1]),double(m[2]));
    const double y = std::hypot(double(m[4]),double(m[5]),double(m[6]));
    const double z = std::hypot(double(m[8]),double(m[9]),double(m[10]));
    if (matrix.LinearDeterminant() < 0) { x = -x; }
    t.Scale = {static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
    const double a = m[0]/x, b = m[4]/y, c = m[8]/z, d = m[1]/x, e = m[5]/y, f = m[9]/z, g = m[2]/x, h = m[6]/y, i = m[10]/z;
    double qx, qy, qz, qw;
    if (a+e+i > 0) { const auto s = std::sqrt(a+e+i+1)*2; qw=s/4; qx=(h-f)/s; qy=(c-g)/s; qz=(d-b)/s; }
    else if (a > e && a > i) { const auto s = std::sqrt(1+a-e-i)*2; qw=(h-f)/s; qx=s/4; qy=(b+d)/s; qz=(c+g)/s; }
    else if (e > i) { const auto s = std::sqrt(1+e-a-i)*2; qw=(c-g)/s; qx=(b+d)/s; qy=s/4; qz=(f+h)/s; }
    else { const auto s = std::sqrt(1+i-a-e)*2; qw=(d-b)/s; qx=(c+g)/s; qy=(f+h)/s; qz=s/4; }
    t.Rotation = {static_cast<float>(qx),static_cast<float>(qy),static_cast<float>(qz),static_cast<float>(qw)};
    const auto rebuilt = t.Matrix();
    for (std::size_t k = 0; k < 16; ++k) {
        if (!std::isfinite(rebuilt.Elements[k]) || std::abs(double(m[k])-rebuilt.Elements[k]) > 0.0001*std::max(1.0,std::abs(double(m[k])))) {
            throw std::invalid_argument("Matrix contains shear and cannot be represented by TRS.");
        }
    }
    return t;
}
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

Mat4 Mat4::Translation(Vec3 position) {
    if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z)) {
        throw std::invalid_argument("Translation must be finite.");
    }
    auto result = Identity();
    result.Elements[12] = position.X;
    result.Elements[13] = position.Y;
    result.Elements[14] = position.Z;
    return result;
}

Mat4 Mat4::Scale(Vec3 scale) {
    if (!std::isfinite(scale.X) || !std::isfinite(scale.Y) || !std::isfinite(scale.Z) ||
        scale.X == 0 || scale.Y == 0 || scale.Z == 0) {
        throw std::invalid_argument("Scale must be finite and nonzero on each axis.");
    }
    auto result = Identity();
    result.Elements[0] = scale.X;
    result.Elements[5] = scale.Y;
    result.Elements[10] = scale.Z;
    return result;
}

Mat4 Mat4::Rotation(Quaternion q) {
    const double length = std::sqrt(double(q.X) * q.X + double(q.Y) * q.Y + double(q.Z) * q.Z + double(q.W) * q.W);
    if (!std::isfinite(length) || length == 0) {
        throw std::invalid_argument("Rotation quaternion must be finite and nonzero.");
    }
    const float x = static_cast<float>(q.X / length);
    const float y = static_cast<float>(q.Y / length);
    const float z = static_cast<float>(q.Z / length);
    const float w = static_cast<float>(q.W / length);
    Mat4 result;
    result.Elements = {
        1 - 2 * (y*y + z*z), 2 * (x*y + z*w), 2 * (x*z - y*w), 0,
        2 * (x*y - z*w), 1 - 2 * (x*x + z*z), 2 * (y*z + x*w), 0,
        2 * (x*z + y*w), 2 * (y*z - x*w), 1 - 2 * (x*x + y*y), 0,
        0, 0, 0, 1
    };
    return result;
}

double Mat4::LinearDeterminant() const noexcept {
    const auto& m = Elements;
    return double(m[0]) * (double(m[5])*m[10] - double(m[9])*m[6]) -
        double(m[4]) * (double(m[1])*m[10] - double(m[9])*m[2]) +
        double(m[8]) * (double(m[1])*m[6] - double(m[5])*m[2]);
}

Mat4 Mat4::NormalMatrix() const {
    for (float value : Elements) {
        if (!std::isfinite(value)) { throw std::invalid_argument("Transform matrix must be finite."); }
    }
    if (Elements[3] != 0 || Elements[7] != 0 || Elements[11] != 0 || Elements[15] != 1) {
        throw std::invalid_argument("Model transform must be affine.");
    }
    const double det = LinearDeterminant();
    if (!std::isfinite(det) || det == 0) { throw std::invalid_argument("Model transform is singular."); }
    const auto& m = Elements;
    auto result = Identity();
    // Cofactor matrix / determinant is the inverse transpose.
    result.Elements[0] = static_cast<float>((double(m[5])*m[10] - double(m[9])*m[6]) / det);
    result.Elements[1] = static_cast<float>((double(m[8])*m[6] - double(m[4])*m[10]) / det);
    result.Elements[2] = static_cast<float>((double(m[4])*m[9] - double(m[8])*m[5]) / det);
    result.Elements[4] = static_cast<float>((double(m[9])*m[2] - double(m[1])*m[10]) / det);
    result.Elements[5] = static_cast<float>((double(m[0])*m[10] - double(m[8])*m[2]) / det);
    result.Elements[6] = static_cast<float>((double(m[8])*m[1] - double(m[0])*m[9]) / det);
    result.Elements[8] = static_cast<float>((double(m[1])*m[6] - double(m[5])*m[2]) / det);
    result.Elements[9] = static_cast<float>((double(m[4])*m[2] - double(m[0])*m[6]) / det);
    result.Elements[10] = static_cast<float>((double(m[0])*m[5] - double(m[4])*m[1]) / det);
    for (float value : result.Elements) {
        if (!std::isfinite(value)) { throw std::invalid_argument("Normal transform exceeds float range."); }
    }
    return result;
}

Vec3 Mat4::TransformPoint(Vec3 point) const noexcept {
    const auto& m = Elements;
    return {m[0]*point.X + m[4]*point.Y + m[8]*point.Z + m[12],
            m[1]*point.X + m[5]*point.Y + m[9]*point.Z + m[13],
            m[2]*point.X + m[6]*point.Y + m[10]*point.Z + m[14]};
}

Mat4 Transform::Matrix() const {
    return Mat4::Translation(Position) * Mat4::Rotation(Rotation) * Mat4::Scale(Scale);
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
