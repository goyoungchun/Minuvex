#pragma once
#include <minvex/Debug.h>
#include <sokol_gfx.h>
namespace lv::detail {
class DebugRenderer final {
public:
    explicit DebugRenderer(const RenderSettings& settings);
    ~DebugRenderer();
    void Prepare(const Scene& scene,const DebugOptions& options,const DebugDraw& commands);
    void Draw(const Mat4& viewProjection,bool depthTest);
    void Inspect(std::vector<DebugResource>& resources) const;
    std::size_t LineCount() const noexcept {return lines_;}
private:
    sg_shader shader_{};sg_pipeline pipelines_[2]{};sg_buffer vertices_{};
    std::size_t lines_=0;int offset_=0;
    void Destroy() noexcept;
};
}
