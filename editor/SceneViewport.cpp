#include "SceneViewport.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lv::editor {
void SceneViewport::ClearTarget() noexcept {
    if (attachment_.id) { sg_destroy_view(attachment_); }
    if (depthAttachment_.id) { sg_destroy_view(depthAttachment_); }
    if (texture_.id) { sg_destroy_view(texture_); }
    if (color_.id) { sg_destroy_image(color_); }
    if (depth_.id) { sg_destroy_image(depth_); }
    attachment_ = {}; depthAttachment_ = {}; texture_ = {}; color_ = {}; depth_ = {}; width_ = height_ = 0;
}
void SceneViewport::Clear() noexcept { renderer_.Clear(); ClearTarget(); }
void SceneViewport::Resize(int width, int height) {
    width = std::clamp(width,0,4096); height = std::clamp(height,0,4096);
    if (width == width_ && height == height_) { return; }
    if (!width || !height) { ClearTarget(); return; }
    SceneViewport next;
    const auto defaults = sg_query_desc().environment.defaults;
    sg_image_desc color{}; color.width = width; color.height = height;
    color.pixel_format = defaults.color_format; color.sample_count = 1;
    color.usage.color_attachment = true; color.label = "editor-viewport-color";
    next.color_ = sg_make_image(color);
    sg_image_desc depth{}; depth.width = width; depth.height = height;
    depth.pixel_format = defaults.depth_format; depth.sample_count = 1;
    depth.usage.depth_stencil_attachment = true; depth.label = "editor-viewport-depth";
    next.depth_ = sg_make_image(depth);
    if (sg_query_image_state(next.color_) != SG_RESOURCESTATE_VALID || sg_query_image_state(next.depth_) != SG_RESOURCESTATE_VALID) {
        throw std::runtime_error("Cannot create Editor viewport attachments.");
    }
    sg_view_desc attachment{}; attachment.color_attachment.image = next.color_;
    sg_view_desc depthAttachment{}; depthAttachment.depth_stencil_attachment.image = next.depth_;
    sg_view_desc texture{}; texture.texture.image = next.color_;
    next.attachment_ = sg_make_view(attachment); next.depthAttachment_ = sg_make_view(depthAttachment); next.texture_ = sg_make_view(texture);
    if (sg_query_view_state(next.attachment_) != SG_RESOURCESTATE_VALID || sg_query_view_state(next.depthAttachment_) != SG_RESOURCESTATE_VALID || sg_query_view_state(next.texture_) != SG_RESOURCESTATE_VALID) {
        throw std::runtime_error("Cannot create Editor viewport views.");
    }
    next.width_ = width; next.height_ = height;
    std::swap(color_,next.color_); std::swap(depth_,next.depth_);
    std::swap(attachment_,next.attachment_); std::swap(depthAttachment_,next.depthAttachment_); std::swap(texture_,next.texture_);
    std::swap(width_,next.width_); std::swap(height_,next.height_);
}
void SceneViewport::Render(const Scene& scene) {
    if (!width_ || !height_) { return; }
    renderer_.Prepare(scene);
    sg_pass pass{}; pass.attachments.colors[0] = attachment_; pass.attachments.depth_stencil = depthAttachment_;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR; pass.action.colors[0].clear_value = {0.025f,0.035f,0.055f,1};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR; pass.action.depth.clear_value = 1;
    sg_begin_pass(pass);
    try { renderer_.Draw(scene,static_cast<float>(width_)/static_cast<float>(height_)); }
    catch (...) { sg_end_pass(); throw; }
    sg_end_pass();
}
} // namespace lv::editor
