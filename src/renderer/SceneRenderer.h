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

private:
    struct GpuModel;
    std::unordered_map<const ModelAsset*, std::unique_ptr<GpuModel>> models_;
    SceneRenderStats stats_;
    RenderSettings settings_;
    DebugOptions debug_;
    void DrawImpl(const Scene& scene,const Mat4& viewProjection,bool shadowPass,const ShadowSampling& shadow);
};

} // namespace lv::detail
