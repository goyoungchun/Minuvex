#pragma once

#include <minuvex/Scene.h>
#include <minuvex/Quality.h>

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
    void Draw(const Scene& scene, float aspect);
    SceneRenderStats GetStats() const noexcept { return stats_; }
    void Clear() noexcept;
    void SetDrawSettings(const RenderSettings& settings) noexcept { settings_.Lighting=settings.Lighting;settings_.FrustumCulling=settings.FrustumCulling; }

private:
    struct GpuModel;
    std::unordered_map<const ModelAsset*, std::unique_ptr<GpuModel>> models_;
    SceneRenderStats stats_;
    RenderSettings settings_;
};

} // namespace lv::detail
