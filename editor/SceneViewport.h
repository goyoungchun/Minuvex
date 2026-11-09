#pragma once
#include "renderer/SceneRenderer.h"
#include <sokol_gfx.h>

namespace lv::editor {
// Main-window offscreen target. Uses the environment's color/depth formats and
// sample count 1, matching the Editor renderer and sampled ImGui image.
class SceneViewport final {
public:
    ~SceneViewport() { Clear(); }
    void Resize(int width, int height); // Outside passes; 0 suspends rendering.
    void Render(const Scene& scene); // Outside passes, before the ImGui pass.
    void Clear() noexcept;
    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }
    sg_view Texture() const noexcept { return texture_; }
    sg_image ColorImage() const noexcept { return color_; }
    detail::SceneRenderStats Stats() const noexcept { return renderer_.GetStats(); }
private:
    void ClearTarget() noexcept;
    detail::SceneRenderer renderer_;
    sg_image color_{}, depth_{};
    sg_view attachment_{}, depthAttachment_{}, texture_{};
    int width_ = 0, height_ = 0;
};
} // namespace lv::editor
