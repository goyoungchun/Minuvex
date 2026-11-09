#include <minuvex/Mesh.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lv {

void ValidateMesh(const MeshData& mesh) {
    constexpr auto maxVertices = static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1;
    if (mesh.Vertices.empty() || mesh.Vertices.size() > maxVertices) {
        throw std::invalid_argument("A mesh requires 1..65,536 vertices for 16-bit indices.");
    }
    if (mesh.Indices.empty() || mesh.Indices.size() % 3 != 0 ||
        mesh.Indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("A mesh requires a non-empty triangle index list that fits a draw call.");
    }
    for (const auto index : mesh.Indices) {
        if (index >= mesh.Vertices.size()) {
            throw std::invalid_argument("A mesh index references a missing vertex.");
        }
    }
    const auto finite = [](float value) { return std::isfinite(value); };
    for (const auto& vertex : mesh.Vertices) {
        if (!std::all_of(vertex.Position.begin(), vertex.Position.end(), finite) ||
            !std::all_of(vertex.Normal.begin(), vertex.Normal.end(), finite) ||
            !std::all_of(vertex.Color.begin(), vertex.Color.end(), finite) ||
            std::hypot(vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]) <= 0.0f) {
            throw std::invalid_argument("Mesh attributes must be finite and normals must be non-zero.");
        }
    }
}

MeshData MakeCube() {
    MeshData mesh;
    mesh.Vertices.reserve(24);
    mesh.Indices.reserve(36);
    const auto face = [&mesh](std::array<std::array<float, 3>, 4> positions,
                             std::array<float, 3> normal, std::array<float, 4> color) {
        const auto base = static_cast<std::uint16_t>(mesh.Vertices.size());
        for (const auto& position : positions) {
            mesh.Vertices.push_back({position, normal, color});
        }
        for (const int offset : {0, 1, 2, 0, 2, 3}) {
            mesh.Indices.push_back(static_cast<std::uint16_t>(base + offset));
        }
    };

    face({{{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}}, {0, 0, 1}, {0.20f, 0.72f, 0.90f, 1});
    face({{{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}}}, {0, 0, -1}, {0.22f, 0.40f, 0.75f, 1});
    face({{{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}}, {1, 0, 0}, {0.95f, 0.57f, 0.25f, 1});
    face({{{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}}}, {-1, 0, 0}, {0.75f, 0.30f, 0.32f, 1});
    face({{{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}}, {0, 1, 0}, {0.38f, 0.84f, 0.60f, 1});
    face({{{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}}}, {0, -1, 0}, {0.54f, 0.36f, 0.76f, 1});
    return mesh;
}

} // namespace lv
