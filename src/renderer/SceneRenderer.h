#pragma once

#include <minuvex/Scene.h>
#include <minuvex/Quality.h>
#include "MeshRenderer.h"

#include <memory>
#include <unordered_map>

namespace lv::detail {

struct SceneRenderStats {
    std::size_t ResidentModels = 0;
    std::size_t ResidentPrimitives = 0;
    std::size_t ResidentImages = 0;
    std::size_t LastDrawCalls = 0;
    std::size_t LastCulledParts = 0;
    std::uint64_t LastTriangles = 0, LogicalTextureBytes = 0, LogicalBufferBytes = 0;
    std::uint64_t ResidentAssetSignature = 0;
    int MaxResidentTextureSize = 0;
    std::uint64_t LastShadowDrawCalls=0,LastShadowTriangles=0;
    std::uint64_t LastWireLines=0;
    OptimizationStatistics Optimization;
};

// Context/thread ownership stays with the caller. Prepare outside a render pass,
// Draw inside it, and destroy before sg_shutdown().
class SceneRenderer final {
public:
    explicit SceneRenderer(RenderSettings settings = {});
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void Prepare(const Scene& scene);
    void Draw(const Scene& scene, float aspect,const ShadowSampling& shadow={});
    void DrawShadow(const Scene& scene,const Mat4& lightViewProjection);
    Mat4 ShadowViewProjection(const Scene& scene) const;
    SceneRenderStats GetStats() const noexcept { return stats_; }
    void Clear() noexcept;
    void SetDrawSettings(const RenderSettings& settings) noexcept {settings_=settings;}
    void SetDebugOptions(const DebugOptions& options) noexcept {debug_=options;}
    void Inspect(std::vector<DebugResource>& resources) const;
    void SetViewport(float aspect,int height) noexcept {aspect_=aspect;height_=height;viewportKnown_=true;}

private:
    struct GpuModel;
    std::unordered_map<const ModelAsset*, std::unique_ptr<GpuModel>> models_;
    SceneRenderStats stats_;
    RenderSettings settings_;
    DebugOptions debug_;
    struct BatchKey {MeshRenderer* Mesh;unsigned Lod;bool Mirrored,Shadow;bool operator==(const BatchKey&) const = default;};
    struct BatchHash {std::size_t operator()(const BatchKey& k) const noexcept {return reinterpret_cast<std::uintptr_t>(k.Mesh)^(k.Lod<<2)^(k.Mirrored ? 1:0)^(k.Shadow ? 2:0);}};
    struct Batch;
    std::unordered_map<BatchKey,std::unique_ptr<Batch>,BatchHash> batches_;
    float aspect_=1;int height_=720;bool viewportKnown_=false;
    void PrepareBatches(const Scene& scene);
    unsigned Lod(const Scene& scene,const Bounds& bounds,const Mat4& world,std::size_t levels) const;
    void DrawImpl(const Scene& scene,const Mat4& viewProjection,bool shadowPass,const ShadowSampling& shadow);
};

} // namespace lv::detail
