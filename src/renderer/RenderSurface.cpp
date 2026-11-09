#include "RenderSurface.h"
#include "present.glsl.h"
#include <algorithm>
#include <chrono>
#include <bit>

namespace lv::detail {
namespace {void Require(bool value,const char* message) {if (!value) {throw std::runtime_error(message);}}}
struct RenderSurface::Targets {
    sg_image Color{},Msaa{},Depth{};
    sg_view Attachment{},Resolve{},DepthAttachment{},Texture{};
    Targets(int width,int height,int samples) {
        try {
            const auto defaults=sg_query_desc().environment.defaults;
            sg_image_desc color{};color.width=width;color.height=height;color.pixel_format=defaults.color_format;color.sample_count=1;
            color.usage.color_attachment=samples == 1;color.usage.resolve_attachment=samples > 1;color.label="quality-resolved-color";Color=sg_make_image(color);
            if (samples > 1) {color.sample_count=samples;color.usage.resolve_attachment=false;color.usage.color_attachment=true;color.label="quality-msaa-color";Msaa=sg_make_image(color);}
            sg_image_desc depth{};depth.width=width;depth.height=height;depth.pixel_format=defaults.depth_format;depth.sample_count=samples;
            depth.usage.depth_stencil_attachment=true;depth.label="quality-depth";Depth=sg_make_image(depth);
            Require(sg_query_image_state(Color) == SG_RESOURCESTATE_VALID && sg_query_image_state(Depth) == SG_RESOURCESTATE_VALID && (samples == 1 || sg_query_image_state(Msaa) == SG_RESOURCESTATE_VALID),"Cannot create quality render target images.");
            sg_view_desc view{};view.color_attachment.image=samples > 1 ? Msaa:Color;Attachment=sg_make_view(view);
            view={};view.depth_stencil_attachment.image=Depth;DepthAttachment=sg_make_view(view);
            if (samples > 1) {view={};view.resolve_attachment.image=Color;Resolve=sg_make_view(view);}
            view={};view.texture.image=Color;Texture=sg_make_view(view);
            Require(sg_query_view_state(Attachment) == SG_RESOURCESTATE_VALID && sg_query_view_state(DepthAttachment) == SG_RESOURCESTATE_VALID && sg_query_view_state(Texture) == SG_RESOURCESTATE_VALID && (samples == 1 || sg_query_view_state(Resolve) == SG_RESOURCESTATE_VALID),"Cannot create quality render target views.");
        } catch (...) {Destroy();throw;}
    }
    void Destroy() noexcept {
        for (auto view:{Texture,Attachment,Resolve,DepthAttachment}) {if (view.id) {sg_destroy_view(view);}}
        for (auto image:{Color,Msaa,Depth}) {if (image.id) {sg_destroy_image(image);}}
        Color=Msaa=Depth={};Texture=Attachment=Resolve=DepthAttachment={};
    }
    ~Targets() {Destroy();}
};
struct RenderSurface::Presenter {
    sg_shader Shader{};sg_pipeline Pipeline{};sg_sampler Sampler{};
    Presenter() {
        try {
            Shader=sg_make_shader(present_shader_desc(sg_query_backend()));Require(sg_query_shader_state(Shader) == SG_RESOURCESTATE_VALID,"Cannot create presentation shader.");
            sg_pipeline_desc pipeline{};pipeline.shader=Shader;pipeline.sample_count=1;pipeline.label="quality-present";Pipeline=sg_make_pipeline(pipeline);
            sg_sampler_desc sampler{};sampler.min_filter=sampler.mag_filter=SG_FILTER_LINEAR;sampler.wrap_u=sampler.wrap_v=SG_WRAP_CLAMP_TO_EDGE;Sampler=sg_make_sampler(sampler);
            Require(sg_query_pipeline_state(Pipeline) == SG_RESOURCESTATE_VALID && sg_query_sampler_state(Sampler) == SG_RESOURCESTATE_VALID,"Cannot create quality presentation resources.");
        } catch (...) {Destroy();throw;}
    }
    void Destroy() noexcept {if (Pipeline.id) {sg_destroy_pipeline(Pipeline);}if (Shader.id) {sg_destroy_shader(Shader);}if (Sampler.id) {sg_destroy_sampler(Sampler);}Pipeline={};Shader={};Sampler={};}
    ~Presenter() {Destroy();}
};
double RenderClockSeconds() noexcept {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
RenderSurface::RenderSurface()=default;
RenderSurface::~RenderSurface()=default;
sg_view RenderSurface::Texture() const noexcept {return targets_ ? targets_->Texture:sg_view{};}
sg_image RenderSurface::ColorImage() const noexcept {return targets_ ? targets_->Color:sg_image{};}
Result<void> RenderSurface::Fail(lv::Error error) {state_.LastError=error;scheduler_.Invalidate();return Result<void>::Failure(std::move(error));}
Result<void> RenderSurface::ApplyQuality(const QualityProfile& profile,const Scene& scene,int width,int height) {
    if (!sg_isvalid() || sg_query_pass_state() != SG_PASSSTATE_NONE || applying_) {return Fail({ErrorCode::InvalidState,"Apply quality outside render passes on the graphics thread.","Apply from OnFrame or between frames.",{}});}
    const auto caps=QueryQualityCapabilities();auto resolved=ResolveQuality(profile,caps,std::max(1,width),std::max(1,height));
    if (!resolved) {return Fail(resolved.Error());}
    applying_=true;struct Reset {bool& Flag;~Reset(){Flag=false;}}reset{applying_};
    try {
        auto next=std::move(resolved).Value();const auto& settings=*next.Effective;
        const bool rebuild=!renderer_ || !state_.Effective || settings.MsaaSamples != state_.Effective->MsaaSamples || settings.MaxTextureSize != state_.Effective->MaxTextureSize || settings.Anisotropy != state_.Effective->Anisotropy;
        std::unique_ptr<Targets> targets;
        const bool resize=!targets_ || next.RenderWidth != state_.RenderWidth || next.RenderHeight != state_.RenderHeight || !state_.Effective || settings.MsaaSamples != state_.Effective->MsaaSamples;
        if (resize && width > 0 && height > 0) {targets=std::make_unique<Targets>(next.RenderWidth,next.RenderHeight,settings.MsaaSamples);}
        std::unique_ptr<SceneRenderer> renderer;
        if (rebuild) {renderer=std::make_unique<SceneRenderer>(settings);renderer->Prepare(scene);}
        std::unique_ptr<Presenter> presenter;if (!presenter_) {presenter=std::make_unique<Presenter>();}
        if (resize) {targets_.swap(targets);}if (rebuild) {renderer_.swap(renderer);}if (presenter) {presenter_.swap(presenter);}
        renderer_->SetDrawSettings(settings);next.Applied=bool(targets_);
        if (!targets_) {next.RenderWidth=next.RenderHeight=0;}
        state_=std::move(next);outputWidth_=width;outputHeight_=height;failedWidth_=failedHeight_=-1;scheduler_.Invalidate();return Result<void>::Success();
    } catch (const std::bad_alloc& e) {return Fail({ErrorCode::InsufficientResource,e.what(),"Reduce quality and retry; previous resources are retained.",{}});}
    catch (const std::exception& e) {return Fail({ErrorCode::Rendering,e.what(),"Quality application failed; previous settings/resources are retained.",{}});}
}
Result<void> RenderSurface::Resize(const Scene& scene,int width,int height) {
    if (width <= 0 || height <= 0) {targets_.reset();state_.RenderWidth=state_.RenderHeight=0;state_.Applied=false;outputWidth_=width;outputHeight_=height;scheduler_.Invalidate();return Result<void>::Success();}
    if (targets_ && width == outputWidth_ && height == outputHeight_) {return Result<void>::Success();}
    if (width == failedWidth_ && height == failedHeight_ && state_.LastError) {return Result<void>::Failure(*state_.LastError);}
    auto result=ApplyQuality(state_.Requested,scene,width,height);if (!result) {failedWidth_=width;failedHeight_=height;outputWidth_=width;outputHeight_=height;scheduler_.Invalidate();}return result;
}
bool RenderSurface::Due(double now) const {return scheduler_.Due(now,state_.Effective ? state_.Effective->TargetFps:0);}
bool RenderSurface::Render(const Scene& scene,double now,bool force,const std::array<float,4>& clear) {
    if (!targets_ || !renderer_ || !state_.Effective || !Due(now)) {return false;}
    auto signature=SceneRenderSignature(scene);for (float value:clear) {signature^=std::bit_cast<std::uint32_t>(value);signature*=1099511628211ull;}
    if (!scheduler_.NeedsRender(state_.Effective->OnDemand,signature,force || SceneHasActiveAnimation(scene))) {return false;}
    renderer_->Prepare(scene);sg_pass pass{};pass.attachments.colors[0]=targets_->Attachment;pass.attachments.resolves[0]=targets_->Resolve;pass.attachments.depth_stencil=targets_->DepthAttachment;
    pass.action.colors[0].load_action=SG_LOADACTION_CLEAR;pass.action.colors[0].clear_value={clear[0],clear[1],clear[2],clear[3]};pass.action.depth.load_action=SG_LOADACTION_CLEAR;pass.action.depth.clear_value=1;
    sg_begin_pass(pass);try {renderer_->Draw(scene,static_cast<float>(std::max(1,outputWidth_))/static_cast<float>(std::max(1,outputHeight_)));}catch (...) {sg_end_pass();throw;}sg_end_pass();
    scheduler_.Submitted(now,signature);++frames_;return true;
}
void RenderSurface::Present() const {
    if (!targets_ || !presenter_) {return;}sg_bindings bindings{};bindings.views[VIEW_rendered_texture]=targets_->Texture;bindings.samplers[SMP_rendered_sampler]=presenter_->Sampler;
    sg_apply_pipeline(presenter_->Pipeline);sg_apply_bindings(bindings);sg_draw(0,3,1);
}
void RenderSurface::Clear() noexcept {targets_.reset();renderer_.reset();presenter_.reset();state_.Effective.reset();state_.Applied=false;state_.RenderWidth=state_.RenderHeight=0;scheduler_.Invalidate();}
}
