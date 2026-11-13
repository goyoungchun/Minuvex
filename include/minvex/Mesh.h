#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace lv {

struct Vertex {
    std::array<float, 3> Position;
    std::array<float, 3> Normal;
    std::array<float, 4> Color;
    std::array<float, 2> TexCoord{};
    std::array<std::uint16_t,4> Joints{};
    std::array<float,4> Weights{1,0,0,0};
    // W=0 means absent (derive a cotangent frame); authored glTF tangents use +/-1.
    std::array<float,4> Tangent{1,0,0,0};
};

// Indexed triangle list; 32-bit indices support imported glTF geometry.
struct MeshData {
    std::vector<Vertex> Vertices;
    std::vector<std::uint32_t> Indices;
};
struct MeshLod {
    std::vector<std::uint32_t> Indices; // References MeshData::Vertices; LOD0 stays in MeshData.
    float Error=0;
};

// Throws std::invalid_argument before any GPU upload if the mesh is malformed.
void ValidateMesh(const MeshData& mesh);

// Unit cube centered at the origin; outward normals and CCW front faces.
MeshData MakeCube();

} // namespace lv
