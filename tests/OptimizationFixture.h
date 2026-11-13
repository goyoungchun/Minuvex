#pragma once
#include <minvex/Optimization.h>
#include <algorithm>
#include <random>
namespace test {
inline lv::MeshData OptimizationGrid(bool shuffled=false) {
    constexpr unsigned n=32;lv::MeshData mesh;
    for (unsigned y=0;y<=n;++y) {for (unsigned x=0;x<=n;++x) {lv::Vertex v{};v.Position={2.0f*x/n-1,2.0f*y/n-1,0};v.Normal={0,0,1};v.Color={1,1,1,1};v.TexCoord={float(x)/n,float(y)/n};mesh.Vertices.push_back(v);}}
    std::vector<std::array<std::uint32_t,3>> triangles;for (unsigned y=0;y<n;++y) {for (unsigned x=0;x<n;++x) {auto a=y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;triangles.push_back({a,b,d});triangles.push_back({a,d,c});}}
    if (shuffled) {std::mt19937 random(19);std::shuffle(triangles.begin(),triangles.end(),random);}for (auto t:triangles) {mesh.Indices.insert(mesh.Indices.end(),t.begin(),t.end());}return mesh;
}
}
