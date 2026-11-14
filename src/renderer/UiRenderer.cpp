#include "UiRenderer.h"
#include "ui.glsl.h"
#include <stdexcept>
namespace lv::detail {
UiRenderer::UiRenderer() {
    shader_=sg_make_shader(ui_shader_desc(sg_query_backend()));sg_pipeline_desc p{};p.shader=shader_;p.colors[0].pixel_format=SG_PIXELFORMAT_RGBA8;p.depth.pixel_format=SG_PIXELFORMAT_NONE;p.sample_count=1;p.label="minvex-ui-composite";pipeline_=sg_make_pipeline(p);
    sg_sampler_desc s{};s.min_filter=s.mag_filter=SG_FILTER_LINEAR;s.wrap_u=s.wrap_v=SG_WRAP_CLAMP_TO_EDGE;sampler_=sg_make_sampler(s);
    if (sg_query_shader_state(shader_)!=SG_RESOURCESTATE_VALID||sg_query_pipeline_state(pipeline_)!=SG_RESOURCESTATE_VALID||sg_query_sampler_state(sampler_)!=SG_RESOURCESTATE_VALID) {sg_destroy_sampler(sampler_);sg_destroy_pipeline(pipeline_);sg_destroy_shader(shader_);throw std::runtime_error("Cannot create UI compositor.");}
}
void UiRenderer::ClearImages() noexcept {sg_destroy_view(overlayView_);sg_destroy_view(outputView_);sg_destroy_view(attachment_);sg_destroy_image(overlay_);sg_destroy_image(output_);overlayView_=outputView_=attachment_={};overlay_=output_={};}
UiRenderer::~UiRenderer() {ClearImages();sg_destroy_sampler(sampler_);sg_destroy_pipeline(pipeline_);sg_destroy_shader(shader_);}
void UiRenderer::Composite(const UiCanvas& canvas,sg_view scene,int width,int height,float scale) {
    const bool resize=width_!=width||height_!=height;
    if (resize||revision_!=canvas.Revision()||scale_!=scale) {
        auto raster=canvas.Rasterize(width,height,scale);if (!raster) {throw UiRasterError(raster.Error());}
        if (resize) {ClearImages();width_=width;height_=height;sg_image_desc d{};d.width=width;d.height=height;d.pixel_format=SG_PIXELFORMAT_RGBA8;d.usage.color_attachment=true;d.label="minvex-ui-output";output_=sg_make_image(d);sg_view_desc v{};v.color_attachment.image=output_;attachment_=sg_make_view(v);v={};v.texture.image=output_;outputView_=sg_make_view(v);}
        sg_destroy_view(overlayView_);sg_destroy_image(overlay_);overlayView_={};overlay_={};sg_image_desc d{};d.width=width;d.height=height;d.pixel_format=SG_PIXELFORMAT_RGBA8;d.label="minvex-ui-bitmap";d.data={raster.Value().data(),raster.Value().size()};overlay_=sg_make_image(d);sg_view_desc v{};v.texture.image=overlay_;overlayView_=sg_make_view(v);
        if (sg_query_image_state(output_)!=SG_RESOURCESTATE_VALID||sg_query_image_state(overlay_)!=SG_RESOURCESTATE_VALID||sg_query_view_state(attachment_)!=SG_RESOURCESTATE_VALID||sg_query_view_state(outputView_)!=SG_RESOURCESTATE_VALID||sg_query_view_state(overlayView_)!=SG_RESOURCESTATE_VALID) {throw std::runtime_error("Cannot create UI overlay textures.");}
        revision_=canvas.Revision();scale_=scale;
    }
    sg_pass pass{};pass.attachments.colors[0]=attachment_;pass.action.colors[0].load_action=SG_LOADACTION_DONTCARE;sg_begin_pass(pass);sg_bindings b{};b.views[VIEW_ui_scene]=scene;b.views[VIEW_ui_overlay]=overlayView_;b.samplers[SMP_ui_sampler]=sampler_;sg_apply_pipeline(pipeline_);sg_apply_bindings(b);sg_draw(0,3,1);sg_end_pass();
}
}
