#include <minvex/Math.h>
#include <minvex/Mesh.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {

int checks = 0;
int failures = 0;

void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

bool Near(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}

template<typename Function>
void Rejects(Function function, const char* description) {
    try {
        function();
        Check(false, description);
    } catch (const std::invalid_argument&) {
        Check(true, description);
    }
}

std::array<float, 4> Transform(const lv::Mat4& matrix, std::array<float, 4> vector) {
    std::array<float, 4> result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result[row] += matrix.Elements[column * 4 + row] * vector[column];
        }
    }
    return result;
}

void TestMatrices() {
    const auto identity = lv::Mat4::Identity();
    const std::array<float, 4> point{2, -3, 4, 1};
    Check(Transform(identity, point) == point, "identity preserves a point");

    const auto rotation = lv::Mat4::RotationY(std::numbers::pi_v<float> / 2.0f);
    const auto rotated = Transform(rotation, {1, 0, 0, 1});
    Check(Near(rotated[0], 0) && Near(rotated[2], -1), "right-handed Y rotation");
    const auto rotationX = lv::Mat4::RotationX(std::numbers::pi_v<float> / 2.0f);
    const auto rotatedX = Transform(rotationX, {0, 1, 0, 1});
    Check(Near(rotatedX[1], 0) && Near(rotatedX[2], 1), "right-handed X rotation");

    const auto composed = Transform(rotationX * rotation, point);
    const auto sequential = Transform(rotationX, Transform(rotation, point));
    bool same = true;
    for (int i = 0; i < 4; ++i) { same = same && Near(composed[i], sequential[i]); }
    Check(same, "matrix composition applies the rightmost transform first");

    const auto view = lv::Mat4::LookAt({2, 3, 4}, {0, 0, 0}, {0, 1, 0});
    const auto eyeInView = Transform(view, {2, 3, 4, 1});
    Check(Near(eyeInView[0], 0) && Near(eyeInView[1], 0) && Near(eyeInView[2], 0), "view maps the eye to the origin");
    const auto originInView = Transform(view, {0, 0, 0, 1});
    Check(Near(originInView[0], 0) && Near(originInView[1], 0) && originInView[2] < 0, "view target lies on the negative Z axis");

    const auto projection = lv::Mat4::Perspective(std::numbers::pi_v<float> / 2.0f, 2, 0.1f, 100);
    const auto nearPoint = Transform(projection, {0, 0, -0.1f, 1});
    const auto farPoint = Transform(projection, {0, 0, -100, 1});
    Check(Near(nearPoint[2] / nearPoint[3], 0), "D3D11/Metal near plane maps to depth zero");
    Check(Near(farPoint[2] / farPoint[3], 1), "D3D11/Metal far plane maps to depth one");
    const auto screenCorner = Transform(projection, {2, 1, -1, 1});
    Check(Near(screenCorner[0] / screenCorner[3], 1) && Near(screenCorner[1] / screenCorner[3], 1), "perspective respects field of view and aspect ratio");

    Rejects([] { (void)lv::Mat4::Perspective(1, 0, 0.1f, 100); }, "reject zero aspect (minimized window)");
    Rejects([] { (void)lv::Mat4::Perspective(0, 1, 0.1f, 100); }, "reject zero FOV");
    Rejects([] { (void)lv::Mat4::Perspective(std::numbers::pi_v<float>, 1, 0.1f, 100); }, "reject FOV >= pi");
    Rejects([] { (void)lv::Mat4::Perspective(1, 1, 0, 100); }, "reject non-positive near plane");
    Rejects([] { (void)lv::Mat4::Perspective(1, 1, 100, 0.1f); }, "reject reversed depth range");
    Rejects([] { (void)lv::Mat4::Perspective(1, 1, 1, 1); }, "reject identical depth planes");
    Rejects([] { (void)lv::Mat4::Perspective(1, std::numeric_limits<float>::quiet_NaN(), 1, 100); }, "reject NaN projection input");
    Rejects([] { (void)lv::Mat4::LookAt({0, 0, 0}, {0, 0, 0}, {0, 1, 0}); }, "reject identical eye and target");
    Rejects([] { (void)lv::Mat4::LookAt({0, 0, 1}, {0, 0, 0}, {0, 0, 1}); }, "reject up parallel to viewing direction");
    Rejects([] { (void)lv::Mat4::LookAt({0, 0, 1}, {0, 0, 0}, {0, 0, 0}); }, "reject zero up vector");
    Rejects([] { (void)lv::Mat4::LookAt({std::numeric_limits<float>::infinity(), 0, 1}, {0, 0, 0}, {0, 1, 0}); }, "reject infinite camera input");
}

void TestCube() {
    const auto mesh = lv::MakeCube();
    lv::ValidateMesh(mesh);
    Check(mesh.Vertices.size() == 24 && mesh.Indices.size() == 36, "cube has 24 face vertices and 12 triangles");
    bool bounds = true;
    for (const auto& vertex : mesh.Vertices) {
        for (const auto value : vertex.Position) { bounds = bounds && Near(std::abs(value), 0.5f); }
    }
    Check(bounds, "cube is centered with unit dimensions");

    bool winding = true;
    float area = 0;
    for (std::size_t i = 0; i < mesh.Indices.size(); i += 3) {
        const auto& a = mesh.Vertices.at(mesh.Indices[i]);
        const auto& b = mesh.Vertices.at(mesh.Indices[i + 1]);
        const auto& c = mesh.Vertices.at(mesh.Indices[i + 2]);
        std::array<float, 3> ab{}, ac{};
        for (int k = 0; k < 3; ++k) {
            ab[k] = b.Position[k] - a.Position[k];
            ac[k] = c.Position[k] - a.Position[k];
        }
        const std::array<float, 3> cross{
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0]
        };
        float facing = 0;
        float outward = 0;
        for (int k = 0; k < 3; ++k) {
            facing += cross[k] * a.Normal[k];
            outward += a.Position[k] * a.Normal[k];
        }
        winding = winding && facing > 0 && outward > 0;
        area += std::hypot(cross[0], cross[1], cross[2]) * 0.5f;
    }
    Check(winding, "every cube triangle is CCW with an outward normal");
    Check(Near(area, 6), "cube surface area is six square units");
}

void TestMeshValidation() {
    Rejects([] { lv::ValidateMesh({}); }, "reject empty mesh");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Indices.clear(); lv::ValidateMesh(mesh); }, "reject empty indices");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Indices.pop_back(); lv::ValidateMesh(mesh); }, "reject incomplete triangle");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Indices[0] = 24; lv::ValidateMesh(mesh); }, "reject out-of-range index");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Vertices[0].Position[0] = std::numeric_limits<float>::quiet_NaN(); lv::ValidateMesh(mesh); }, "reject NaN positions");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Vertices[0].Color[0] = std::numeric_limits<float>::infinity(); lv::ValidateMesh(mesh); }, "reject infinite colors");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Vertices[0].Normal = {0, 0, 0}; lv::ValidateMesh(mesh); }, "reject zero normals");
    auto boundary = lv::MakeCube();
    boundary.Vertices.resize(65537, boundary.Vertices[0]);
    boundary.Indices = {0, 1, 65536};
    lv::ValidateMesh(boundary);
    Check(true, "accept vertices and indices beyond the 16-bit range");
    Rejects([] { auto mesh = lv::MakeCube(); mesh.Vertices[0].TexCoord[0] = std::numeric_limits<float>::quiet_NaN(); lv::ValidateMesh(mesh); }, "reject non-finite UV coordinates");
}

} // namespace

int main() {
    try {
        TestMatrices();
        TestCube();
        TestMeshValidation();
    } catch (const std::exception& error) {
        Check(false, error.what());
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
