#pragma once
#include <minuvex/Mesh.h>
#include <minuvex/Math.h>
#include <span>
#include <cstddef>
namespace lv {
struct MeshOptimization {
    MeshData Geometry;
    std::vector<MeshLod> Lods;
    std::uint64_t CacheMissesBefore=0,CacheMissesAfter=0; // Simulated 16-entry LRU, not hardware measurements.
};
// Reorders triangles/vertices without changing attributes or source data. Retains unused vertices.
MeshOptimization OptimizeMesh(const MeshData& mesh,std::span<const MeshLod> lods={});
// Conservative attribute-aware index-only LOD1..3, target ratios .5/.25/.125; may stop earlier.
std::vector<MeshLod> GenerateMeshLods(const MeshData& mesh,float targetError=.02f);
unsigned SelectMeshLod(float projectedDiameterPixels,float bias,std::size_t reducedLevels);
std::uint64_t TextureMipBytes(int width,int height,bool bc3=false);
struct OptimizationStatistics {
    std::uint64_t InstancedObjects=0,InstanceDraws=0,SavedDrawCalls=0,InstanceBufferBytes=0;
    std::array<std::uint64_t,4> LodDraws{};
    std::uint64_t SourceTriangles=0,SubmittedTriangles=0,CulledParts=0;
};
}
