#include <minvex/Optimization.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lv {
namespace {
std::uint64_t Misses(std::span<const std::uint32_t> indices) {std::vector<std::uint32_t> cache;std::uint64_t misses=0;for (auto index:indices) {auto it=std::find(cache.begin(),cache.end(),index);if (it == cache.end()) {++misses;}else {cache.erase(it);}cache.insert(cache.begin(),index);if (cache.size()>16) {cache.pop_back();}}return misses;}
void Lod(const MeshLod& lod,const MeshData& mesh) {if (lod.Indices.empty() || lod.Indices.size()%3 || lod.Indices.size()>=mesh.Indices.size() || !std::isfinite(lod.Error) || lod.Error<0) {throw std::invalid_argument("Invalid mesh LOD.");}for (auto i:lod.Indices) {if (i>=mesh.Vertices.size()) {throw std::invalid_argument("LOD vertex is out of range.");}}}
}
MeshOptimization OptimizeMesh(const MeshData& mesh,std::span<const MeshLod> lods) {
    ValidateMesh(mesh);if (lods.size()>3) {throw std::invalid_argument("At most three reduced LODs are supported.");}
    MeshOptimization result;result.Geometry=mesh;result.Lods.assign(lods.begin(),lods.end());result.CacheMissesBefore=Misses(mesh.Indices);
    auto& indices=result.Geometry.Indices;meshopt_optimizeVertexCache(indices.data(),indices.data(),indices.size(),mesh.Vertices.size());
    for (auto& level:result.Lods) {Lod(level,mesh);meshopt_optimizeVertexCache(level.Indices.data(),level.Indices.data(),level.Indices.size(),mesh.Vertices.size());}
    std::vector<unsigned> remap(mesh.Vertices.size());auto used=meshopt_optimizeVertexFetchRemap(remap.data(),indices.data(),indices.size(),mesh.Vertices.size());
    for (auto& index:remap) {if (index == ~0u) {index=static_cast<unsigned>(used++);}}
    for (std::size_t i=0;i<mesh.Vertices.size();++i) {result.Geometry.Vertices[remap[i]]=mesh.Vertices[i];}
    for (auto& i:indices) {i=remap[i];}for (auto& level:result.Lods) {for (auto& i:level.Indices) {i=remap[i];}}
    result.VertexRemap=std::move(remap);result.CacheMissesAfter=Misses(indices);return result;
}
std::vector<MeshLod> GenerateMeshLods(const MeshData& mesh,float error) {
    ValidateMesh(mesh);if (!std::isfinite(error) || error<0 || error>1) {throw std::invalid_argument("LOD error must be finite in [0,1].");}
    std::vector<MeshLod> result;if (mesh.Indices.size()<96) {return result;}
    std::vector<float> attributes;attributes.reserve(mesh.Vertices.size()*9);for (const auto& v:mesh.Vertices) {attributes.insert(attributes.end(),v.Normal.begin(),v.Normal.end());attributes.insert(attributes.end(),v.TexCoord.begin(),v.TexCoord.end());attributes.insert(attributes.end(),v.Color.begin(),v.Color.end());}
    const float weights[]={.5f,.5f,.5f,.1f,.1f,.1f,.1f,.1f,.1f};std::size_t previous=mesh.Indices.size();
    for (unsigned level=1;level<=3;++level) {MeshLod lod;lod.Indices.resize(mesh.Indices.size());const auto target=std::max(std::size_t(3),(mesh.Indices.size()>>level)/3*3);
        const auto count=meshopt_simplifyWithAttributes(lod.Indices.data(),mesh.Indices.data(),mesh.Indices.size(),mesh.Vertices[0].Position.data(),mesh.Vertices.size(),sizeof(Vertex),attributes.data(),9*sizeof(float),weights,9,nullptr,target,error,meshopt_SimplifyLockBorder,&lod.Error);
        if (count<3 || count>=previous) {break;}lod.Indices.resize(count);previous=count;result.push_back(std::move(lod));
    }return result;
}
unsigned SelectMeshLod(float diameter,float bias,std::size_t levels) {
    if (!std::isfinite(diameter) || diameter<0 || !std::isfinite(bias) || std::abs(bias)>4 || levels>3) {throw std::invalid_argument("Invalid LOD selection inputs.");}
    const float adjusted=diameter*std::exp2(-bias);const float base=adjusted>=256 ? 0:adjusted>=128 ? 1:adjusted>=64 ? 2:3;return static_cast<unsigned>(std::min(base,static_cast<float>(levels)));
}
std::uint64_t TextureMipBytes(int w,int h,bool bc3) {
    if (w<1 || h<1 || w>32768 || h>32768) {throw std::invalid_argument("Invalid texture extent.");}std::uint64_t bytes=0;
    for (;;) {bytes+=bc3 ? std::uint64_t((w+3)/4)*((h+3)/4)*16:std::uint64_t(w)*h*4;if (w==1 && h==1) {break;}w=std::max(1,w/2);h=std::max(1,h/2);}return bytes;
}
}
