#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace lv {

struct Vertex {
    std::array<float, 3> Position;
    std::array<float, 3> Normal;
    std::array<float, 4> Color;
};

// Phase 1: an indexed triangle list with at most 65,536 vertices.
struct MeshData {
    std::vector<Vertex> Vertices;
    std::vector<std::uint16_t> Indices;
};

// Throws std::invalid_argument before any GPU upload if the mesh is malformed.
void ValidateMesh(const MeshData& mesh);

// Unit cube centered at the origin; outward normals and CCW front faces.
MeshData MakeCube();

} // namespace lv
