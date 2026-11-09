#pragma once

#include <minuvex/Scene.h>

#include <memory>
#include <unordered_map>

namespace lv::detail {

struct SceneRenderStats {
    std::size_t ResidentModels = 0;
    std::size_t ResidentPrimitives = 0;
    std::size_t ResidentImages = 0;
    std::size_t LastDrawCalls = 0;
};

// Context/thread ownership stays with the caller. Prepare outside a render pass,
// Draw inside it, and destroy before sg_shutdown().
class SceneRenderer final {
public:
    SceneRenderer();
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void Prepare(const Scene& scene);
    void Draw(const Scene& scene, float aspect);
    SceneRenderStats GetStats() const noexcept { return stats_; }
    void Clear() noexcept;

private:
    struct GpuModel;
    std::unordered_map<const ModelAsset*, std::unique_ptr<GpuModel>> models_;
    SceneRenderStats stats_;
};

} // namespace lv::detail
