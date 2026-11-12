#include "RenderSurface.h"
#include "GraphicsTelemetry.h"
#include "DebugRenderer.h"
#include "DebugResources.h"
#include "debug.glsl.h"
#include "present.glsl.h"
#include <algorithm>
#include <chrono>
#include <bit>

namespace lv::detail {
namespace {void Require(bool value,const char* message) {if (!value) {throw std::runtime_error(message);}}}
struct RenderSurface::Targets {
    sg_image Color{},Msaa{},Depth{},PostColor{};
    sg_view Attachment{},Resolve{},DepthAttachment{},Texture{},PostAttachment{},PostTexture{};
    Targets(int width,int height,int samples,bool post) {
        try {
            const auto defaults=sg_query_desc().environment.defaults;
            sg_image_desc color{};color.width=width;color.height=height;color.pixel_format=post ? SG_PIXELFORMAT_RGBA16F:defaults.color_format;color.sample_count=1;
            color.usage.color_attachment=samples == 1;color.usage.resolve_attachment=samples > 1;color.label="quality-resolved-color";Color=sg_make_image(color);
            if (samples > 1) {color.sample_count=samples;color.usage.resolve_attachment=false;color.usage.color_attachment=true;color.label="quality-msaa-color";Msaa=sg_make_image(color);}
            sg_image_desc depth{};depth.width=width;depth.height=height;depth.pixel_format=defaults.depth_format;depth.sample_count=samples;
            depth.usage.depth_stencil_attachment=true;depth.label="quality-depth";Depth=sg_make_image(depth);
            Require(sg_query_image_state(Color) == SG_RESOURCESTATE_VALID && sg_query_image_state(Depth) == SG_RESOURCESTATE_VALID && (samples == 1 || sg_query_image_state(Msaa) == SG_RESOURCESTATE_VALID),"Cannot create quality render target images.");
            sg_view_desc view{};view.color_attachment.image=samples > 1 ? Msaa:Color;Attachment=sg_make_view(view);
            view={};view.depth_stencil_attachment.image=Depth;DepthAttachment=sg_make_view(view);
            if (samples > 1) {view={};view.resolve_attachment.image=Color;Resolve=sg_make_view(view);}
            view={};view.texture.image=Color;Texture=sg_make_view(view);
            if (post) {color.sample_count=1;color.usage={};color.usage.color_attachment=true;color.pixel_format=defaults.color_format;PostColor=sg_make_image(color);view={};view.color_attachment.image=PostColor;PostAttachment=sg_make_view(view);view={};view.texture.image=PostColor;PostTexture=sg_make_view(view);Require(sg_query_image_state(PostColor) == SG_RESOURCESTATE_VALID && sg_query_view_state(PostAttachment) == SG_RESOURCESTATE_VALID && sg_query_view_state(PostTexture) == SG_RESOURCESTATE_VALID,"Cannot create post-processing output.");}
            Require(sg_query_view_state(Attachment) == SG_RESOURCESTATE_VALID && sg_query_view_state(DepthAttachment) == SG_RESOURCESTATE_VALID && sg_query_view_state(Texture) == SG_RESOURCESTATE_VALID && (samples == 1 || sg_query_view_state(Resolve) == SG_RESOURCESTATE_VALID),"Cannot create quality render target views.");
        } catch (...) {Destroy();throw;}
    }
    void Destroy() noexcept {
        for (auto view:{Texture,Attachment,Resolve,DepthAttachment,PostTexture,PostAttachment}) {if (view.id) {sg_destroy_view(view);}}
        for (auto image:{Color,Msaa,Depth,PostColor}) {if (image.id) {sg_destroy_image(image);}}
        Color=Msaa=Depth=PostColor={};Texture=Attachment=Resolve=DepthAttachment=PostTexture=PostAttachment={};
    }
    ~Targets() {Destroy();}
};
struct RenderSurface::ShadowTargets {
    sg_image Color{},Depth{};sg_view ColorView{},DepthView{},Texture{};sg_sampler Sampler{};
    explicit ShadowTargets(int size) {
        try {sg_image_desc d{};d.width=d.height=size;d.sample_count=1;d.pixel_format=SG_PIXELFORMAT_R32F;d.usage.color_attachment=true;Color=sg_make_image(d);d.pixel_format=SG_PIXELFORMAT_DEPTH;d.usage={};d.usage.depth_stencil_attachment=true;Depth=sg_make_image(d);
            sg_view_desc v{};v.color_attachment.image=Color;ColorView=sg_make_view(v);v={};v.depth_stencil_attachment.image=Depth;DepthView=sg_make_view(v);v={};v.texture.image=Color;Texture=sg_make_view(v);sg_sampler_desc s{};s.min_filter=s.mag_filter=SG_FILTER_NEAREST;s.wrap_u=s.wrap_v=SG_WRAP_CLAMP_TO_EDGE;Sampler=sg_make_sampler(s);
            Require(sg_query_image_state(Color) == SG_RESOURCESTATE_VALID && sg_query_image_state(Depth) == SG_RESOURCESTATE_VALID && sg_query_view_state(ColorView) == SG_RESOURCESTATE_VALID && sg_query_view_state(DepthView) == SG_RESOURCESTATE_VALID && sg_query_view_state(Texture) == SG_RESOURCESTATE_VALID && sg_query_sampler_state(Sampler) == SG_RESOURCESTATE_VALID,"Cannot create directional shadow resources.");
        }catch (...) {Destroy();throw;}
    }
    void Destroy() noexcept {for (auto v:{ColorView,DepthView,Texture}) {if (v.id) {sg_destroy_view(v);}}for (auto i:{Color,Depth}) {if (i.id) {sg_destroy_image(i);}}if (Sampler.id) {sg_destroy_sampler(Sampler);}ColorView=DepthView=Texture={};Color=Depth={};Sampler={};}
    ~ShadowTargets() {Destroy();}
};
struct RenderSurface::Presenter {
    sg_shader Shader{};sg_pipeline Pipeline{},ColorOnlyPipeline{};sg_sampler Sampler{};
    sg_shader PostShader{};sg_pipeline PostPipeline{};
    Presenter() {
        try {
            Shader=sg_make_shader(present_shader_desc(sg_query_backend()));Require(sg_query_shader_state(Shader) == SG_RESOURCESTATE_VALID,"Cannot create presentation shader.");
            sg_pipeline_desc pipeline{};pipeline.shader=Shader;pipeline.sample_count=1;pipeline.label="quality-present";Pipeline=sg_make_pipeline(pipeline);
            pipeline.depth.pixel_format=SG_PIXELFORMAT_NONE;pipeline.label="quality-present-color-only";ColorOnlyPipeline=sg_make_pipeline(pipeline);
            sg_sampler_desc sampler{};sampler.min_filter=sampler.mag_filter=SG_FILTER_LINEAR;sampler.wrap_u=sampler.wrap_v=SG_WRAP_CLAMP_TO_EDGE;Sampler=sg_make_sampler(sampler);
            PostShader=sg_make_shader(post_shader_desc(sg_query_backend()));pipeline.shader=PostShader;pipeline.depth.pixel_format=SG_PIXELFORMAT_NONE;PostPipeline=sg_make_pipeline(pipeline);
            Require(sg_query_shader_state(PostShader) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(PostPipeline) == SG_RESOURCESTATE_VALID,"Cannot create HDR post-processing pipeline.");
            Require(sg_query_pipeline_state(Pipeline) == SG_RESOURCESTATE_VALID && sg_query_pipeline_state(ColorOnlyPipeline) == SG_RESOURCESTATE_VALID && sg_query_sampler_state(Sampler) == SG_RESOURCESTATE_VALID,"Cannot create quality presentation resources.");
        } catch (...) {Destroy();throw;}
    }
    void Destroy() noexcept {if (PostPipeline.id) {sg_destroy_pipeline(PostPipeline);}if (PostShader.id) {sg_destroy_shader(PostShader);}if (Pipeline.id) {sg_destroy_pipeline(Pipeline);}if (ColorOnlyPipeline.id) {sg_destroy_pipeline(ColorOnlyPipeline);}if (Shader.id) {sg_destroy_shader(Shader);}if (Sampler.id) {sg_destroy_sampler(Sampler);}Pipeline=ColorOnlyPipeline=PostPipeline={};Shader=PostShader={};Sampler={};}
    ~Presenter() {Destroy();}
};
struct RenderSurface::DebugPreview {
    sg_shader Shader{};sg_pipeline Pipeline{};
    explicit DebugPreview(const RenderSettings& settings) {
        Shader=sg_make_shader(debug_preview_shader_desc(sg_query_backend()));sg_pipeline_desc p{};p.shader=Shader;p.sample_count=settings.MsaaSamples;p.colors[0].pixel_format=settings.PostProcessing ? SG_PIXELFORMAT_RGBA16F:sg_query_desc().environment.defaults.color_format;p.depth.compare=SG_COMPAREFUNC_ALWAYS;p.label="debug-shadow-preview";Pipeline=sg_make_pipeline(p);
        if (sg_query_shader_state(Shader) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(Pipeline) != SG_RESOURCESTATE_VALID) {if (Pipeline.id) {sg_destroy_pipeline(Pipeline);}if (Shader.id) {sg_destroy_shader(Shader);}throw std::runtime_error("Cannot create shadow preview.");}
    }
    ~DebugPreview() {if (Pipeline.id) {sg_destroy_pipeline(Pipeline);}if (Shader.id) {sg_destroy_shader(Shader);}}
};
double RenderClockSeconds() noexcept {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
RenderSurface::RenderSurface()=default;
RenderSurface::~RenderSurface()=default;
sg_view RenderSurface::Texture() const noexcept {return targets_ ? (targets_->PostTexture.id ? targets_->PostTexture:targets_->Texture):sg_view{};}
sg_image RenderSurface::ColorImage() const noexcept {return targets_ ? (targets_->PostColor.id ? targets_->PostColor:targets_->Color):sg_image{};}
Result<void> RenderSurface::Fail(lv::Error error) {state_.LastError=error;scheduler_.Invalidate();return Result<void>::Failure(std::move(error));}
Result<void> RenderSurface::ApplyQuality(const QualityProfile& profile,const Scene& scene,int width,int height) {
    if (!sg_isvalid() || sg_query_pass_state() != SG_PASSSTATE_NONE || applying_) {return Fail({ErrorCode::InvalidState,"Apply quality outside render passes on the graphics thread.","Apply from OnFrame or between frames.",{}});}
    const auto caps=QueryQualityCapabilities();auto resolved=ResolveQuality(profile,caps,std::max(1,width),std::max(1,height));
    if (!resolved) {return Fail(resolved.Error());}
    if (debug_.View == DebugView::ShadowMap && resolved.Value().Effective->Shadow == ShadowMode::Off) {return Fail({ErrorCode::UnsupportedFeature,"Shadow Map view requires active shadow mapping.","Select Lit before disabling shadows.",{}});}
    applying_=true;struct Reset {bool& Flag;~Reset(){Flag=false;}}reset{applying_};
    try {
        auto next=std::move(resolved).Value();const auto& settings=*next.Effective;
        const bool rebuild=!renderer_ || !state_.Effective || settings.MsaaSamples != state_.Effective->MsaaSamples || settings.MaxTextureSize != state_.Effective->MaxTextureSize || settings.Anisotropy != state_.Effective->Anisotropy || settings.PostProcessing != state_.Effective->PostProcessing || settings.Lod != state_.Effective->Lod || settings.Compression != state_.Effective->Compression || (settings.Shadow != ShadowMode::Off) != (state_.Effective->Shadow != ShadowMode::Off) || (settings.Lighting == LightingMode::Advanced) != (state_.Effective->Lighting == LightingMode::Advanced);
        std::unique_ptr<Targets> targets;
        const bool resize=!targets_ || next.RenderWidth != state_.RenderWidth || next.RenderHeight != state_.RenderHeight || !state_.Effective || settings.MsaaSamples != state_.Effective->MsaaSamples || settings.PostProcessing != state_.Effective->PostProcessing;
        if (resize && width > 0 && height > 0) {targets=std::make_unique<Targets>(next.RenderWidth,next.RenderHeight,settings.MsaaSamples,settings.PostProcessing);}
        std::unique_ptr<ShadowTargets> shadow;const bool replaceShadow=!shadow_ || !state_.Effective || settings.ShadowResolution != state_.Effective->ShadowResolution || settings.Shadow == ShadowMode::Off || width <= 0 || height <= 0;
        if (replaceShadow && settings.Shadow != ShadowMode::Off && width > 0 && height > 0) {shadow=std::make_unique<ShadowTargets>(settings.ShadowResolution);}
        std::unique_ptr<SceneRenderer> renderer;
        if (rebuild) {renderer=std::make_unique<SceneRenderer>(settings);renderer->Prepare(scene);}
        std::unique_ptr<Presenter> presenter;if (!presenter_) {presenter=std::make_unique<Presenter>();}
        std::unique_ptr<GraphicsTelemetry> telemetry;if (!telemetry_) {telemetry=std::make_unique<GraphicsTelemetry>();}
        const bool changed=state_.Effective && (state_.Requested != next.Requested || *state_.Effective != settings || next.RenderWidth != state_.RenderWidth || next.RenderHeight != state_.RenderHeight);
        if (changed && frameMonitor_.Snapshot(RenderClockSeconds()).SampleCount) {previousFrames_=frameMonitor_.Snapshot(RenderClockSeconds());previousFrames_->DebugRendering=debug_ != DebugOptions{} || !debugDraw_.Lines().empty();if (telemetry_) {previousFrames_->GpuFrameMs=telemetry_->GpuTime();}previousName_=state_.Requested.Name;}
        if (resize) {targets_.swap(targets);}if (rebuild) {renderer_.swap(renderer);}if (presenter) {presenter_.swap(presenter);}
        if (rebuild || resize) {debugRenderer_.reset();debugPreview_.reset();}
        if (replaceShadow) {shadow_.swap(shadow);}
        if (telemetry) {telemetry_.swap(telemetry);}if (changed) {frameMonitor_.Reset();telemetry_->ResetGpu();}
        renderer_->SetDrawSettings(settings);next.Applied=bool(targets_);
        if (!targets_) {next.RenderWidth=next.RenderHeight=0;}
        state_=std::move(next);outputWidth_=width;outputHeight_=height;failedWidth_=failedHeight_=-1;scheduler_.Invalidate();return Result<void>::Success();
    } catch (const std::bad_alloc& e) {return Fail({ErrorCode::InsufficientResource,e.what(),"Reduce quality and retry; previous resources are retained.",{}});}
    catch (const std::exception& e) {return Fail({ErrorCode::Rendering,e.what(),"Quality application failed; previous settings/resources are retained.",{}});}
}
Result<void> RenderSurface::Resize(const Scene& scene,int width,int height) {
    if (width <= 0 || height <= 0) {targets_.reset();shadow_.reset();state_.RenderWidth=state_.RenderHeight=0;state_.Applied=false;outputWidth_=width;outputHeight_=height;scheduler_.Invalidate();return Result<void>::Success();}
    if (targets_ && width == outputWidth_ && height == outputHeight_) {return Result<void>::Success();}
    if (width == failedWidth_ && height == failedHeight_ && state_.LastError) {return Result<void>::Failure(*state_.LastError);}
    auto result=ApplyQuality(state_.Requested,scene,width,height);if (!result) {failedWidth_=width;failedHeight_=height;outputWidth_=width;outputHeight_=height;scheduler_.Invalidate();}return result;
}
bool RenderSurface::Due(double now) const {return scheduler_.Due(now,state_.Effective ? state_.Effective->TargetFps:0);}
bool RenderSurface::Render(const Scene& scene,double now,bool force,const std::array<float,4>& clear,bool bypassPacing) {
    if (!targets_ || !renderer_ || !state_.Effective || (!bypassPacing && !Due(now))) {return false;}
    auto signature=SceneRenderSignature(scene);for (float value:clear) {signature^=std::bit_cast<std::uint32_t>(value);signature*=1099511628211ull;}
    if (!scheduler_.NeedsRender(state_.Effective->OnDemand,signature,force || bypassPacing || SceneHasActiveAnimation(scene))) {return false;}
    const bool overlay=debug_.View != DebugView::ShadowMap && (debug_.ShowSkeleton || debug_.ShowBounds || debug_.ShowReferencePose || debug_.View == DebugView::Skeleton || debug_.View == DebugView::BoundingBox || !debugDraw_.Lines().empty());
    if (overlay && !debugRenderer_) {debugRenderer_=std::make_unique<DebugRenderer>(*state_.Effective);}
    if (!overlay) {debugRenderer_.reset();}
    if (debug_.View == DebugView::ShadowMap && !debugPreview_) {debugPreview_=std::make_unique<DebugPreview>(*state_.Effective);}
    const auto cpuStart=RenderClockSeconds();telemetry_->BeginGpu(cpuStart,frameMonitor_);
    struct EndGpu {GraphicsTelemetry& Telemetry;~EndGpu() {Telemetry.EndGpu();}} gpuScope{*telemetry_};
    renderer_->SetDebugOptions(debug_);renderer_->SetViewport(static_cast<float>(std::max(1,outputWidth_))/std::max(1,outputHeight_),state_.RenderHeight);renderer_->Prepare(scene);
    const auto assets=renderer_->GetStats().ResidentAssetSignature;
    if (monitoredAssets_ && *monitoredAssets_ != assets) {frameMonitor_.Reset();telemetry_->ResetGpu();}
    monitoredAssets_=assets;ShadowSampling shadow;
    if (debugRenderer_) {debugRenderer_->Prepare(scene,debug_,debugDraw_);}
    debugLines_=debugRenderer_ ? debugRenderer_->LineCount():0;debugCalls_=debugLines_ ? 1:0;
    if (shadow_) {shadow.ViewProjection=renderer_->ShadowViewProjection(scene);shadow.Texture=shadow_->Texture;shadow.Sampler=shadow_->Sampler;shadow.Mode=state_.Effective->Shadow;shadow.Resolution=state_.Effective->ShadowResolution;sg_pass p{};p.attachments.colors[0]=shadow_->ColorView;p.attachments.depth_stencil=shadow_->DepthView;p.action.colors[0].load_action=SG_LOADACTION_CLEAR;p.action.colors[0].clear_value={1,1,1,1};p.action.depth.load_action=SG_LOADACTION_CLEAR;p.action.depth.clear_value=1;sg_begin_pass(p);try {renderer_->DrawShadow(scene,shadow.ViewProjection);}catch (...) {sg_end_pass();throw;}sg_end_pass();}
    sg_pass pass{};pass.attachments.colors[0]=targets_->Attachment;pass.attachments.resolves[0]=targets_->Resolve;pass.attachments.depth_stencil=targets_->DepthAttachment;
    pass.action.colors[0].load_action=SG_LOADACTION_CLEAR;pass.action.colors[0].clear_value={clear[0],clear[1],clear[2],clear[3]};pass.action.depth.load_action=SG_LOADACTION_CLEAR;pass.action.depth.clear_value=1;
    const auto aspect=static_cast<float>(std::max(1,outputWidth_))/static_cast<float>(std::max(1,outputHeight_));
    sg_begin_pass(pass);try {
        if (debug_.View == DebugView::ShadowMap) {sg_bindings b{};b.views[VIEW_debug_texture]=shadow_->Texture;b.samplers[SMP_debug_sampler]=shadow_->Sampler;sg_apply_pipeline(debugPreview_->Pipeline);sg_apply_bindings(b);sg_draw(0,3,1);}
        else {renderer_->Draw(scene,aspect,shadow);if (debugRenderer_) {debugRenderer_->Draw(scene.MainCamera.ViewProjection(aspect),debug_.DepthTest);}}
    }catch (...) {sg_end_pass();throw;}sg_end_pass();
    if (targets_->PostAttachment.id) {sg_pass p{};p.attachments.colors[0]=targets_->PostAttachment;p.action.colors[0].load_action=SG_LOADACTION_DONTCARE;sg_begin_pass(p);sg_bindings bindings{};bindings.views[VIEW_rendered_texture]=targets_->Texture;bindings.samplers[SMP_rendered_sampler]=presenter_->Sampler;post_params_t post{};post.post_options={1.0f/state_.RenderWidth,1.0f/state_.RenderHeight,debug_.View == DebugView::Lit ? 0.0f:1.0f,0};sg_apply_pipeline(presenter_->PostPipeline);sg_apply_bindings(bindings);sg_apply_uniforms(UB_post_params,SG_RANGE(post));sg_draw(0,3,1);sg_end_pass();}
    const auto cpuEnd=RenderClockSeconds();const auto stats=renderer_->GetStats();
    frameMonitor_.Record(cpuEnd,(cpuEnd-cpuStart)*1000,(debug_.View == DebugView::ShadowMap ? 1:stats.LastDrawCalls+debugCalls_)+stats.LastShadowDrawCalls+(state_.Effective->PostProcessing ? 1:0),(debug_.View == DebugView::ShadowMap ? 1:stats.LastTriangles)+stats.LastShadowTriangles+(state_.Effective->PostProcessing ? 1:0));
    scheduler_.Submitted(now,signature);++frames_;return true;
}
void RenderSurface::Present(bool hasDepth) const {
    if (!targets_ || !presenter_) {return;}sg_bindings bindings{};bindings.views[VIEW_rendered_texture]=Texture();bindings.samplers[SMP_rendered_sampler]=presenter_->Sampler;
    sg_apply_pipeline(hasDepth ? presenter_->Pipeline:presenter_->ColorOnlyPipeline);sg_apply_bindings(bindings);sg_draw(0,3,1);
}
TelemetrySnapshot RenderSurface::Telemetry(double now) {
    TelemetrySnapshot result;result.CapturedAtSeconds=now;result.Live=sg_isvalid();result.Quality=state_;
    if (telemetry_) {result.Hardware=telemetry_->Hardware();result.System=telemetry_->Sample(now,frameMonitor_);}
    result.Frames=frameMonitor_.Snapshot(now);
    result.Frames.DebugRendering=debug_ != DebugOptions{} || !debugDraw_.Lines().empty();
    if (telemetry_) {result.Frames.GpuFrameMs=telemetry_->GpuTime();if (!result.Frames.GpuSampleCount) {result.Frames.AverageGpuMs=result.Frames.P95GpuMs=result.Frames.GpuFrameMs;}}
    const auto stats=Stats();result.ResidentAssetSignature=stats.ResidentAssetSignature;result.Resident.TextureBytes=stats.LogicalTextureBytes;result.Resident.BufferBytes=stats.LogicalBufferBytes;
    result.Optimization=stats.Optimization;
    if (targets_ && state_.Effective) {result.Resident.RenderTargetBytes=RenderTargetBytes(*state_.Effective,state_.RenderWidth,state_.RenderHeight);}
    result.Resident.TotalBytes=result.Resident.TextureBytes+result.Resident.BufferBytes+result.Resident.RenderTargetBytes;
    result.PreviousQualityFrames=previousFrames_;result.PreviousQualityName=previousName_;return result;
}
void RenderSurface::Clear() noexcept {debugRenderer_.reset();debugPreview_.reset();debugDraw_.Clear();debug_={};debugLines_=debugCalls_=0;targets_.reset();shadow_.reset();renderer_.reset();presenter_.reset();telemetry_.reset();frameMonitor_.Reset();monitoredAssets_.reset();previousFrames_.reset();previousName_.clear();state_.Effective.reset();state_.Applied=false;state_.RenderWidth=state_.RenderHeight=0;scheduler_.Invalidate();}

Result<void> RenderSurface::SetDebugOptions(const DebugOptions& options) {
    if (!sg_isvalid() || sg_query_pass_state() != SG_PASSSTATE_NONE) {return Result<void>::Failure({ErrorCode::InvalidState,"Change debug options outside passes on the graphics thread.","Use OnFrame or between renders.",{}});}
    try {ValidateDebugOptions(options);}catch (const std::exception& e) {return Result<void>::Failure({ErrorCode::InvalidArgument,e.what(),"Select a valid view and palette index.",{}});}
    if (options.View == DebugView::ShadowMap && (!state_.Effective || state_.Effective->Shadow == ShadowMode::Off)) {return Result<void>::Failure({ErrorCode::UnsupportedFeature,"Shadow Map view is unavailable while shadows are off.","Apply a profile with supported shadows first.",{}});}
    if (debug_ != options) {debug_=options;frameMonitor_.Reset();if (telemetry_) {telemetry_->ResetGpu();}scheduler_.Invalidate();}return Result<void>::Success();
}
Result<void> RenderSurface::SetDebugDraw(const DebugDraw& commands) {
    if (!sg_isvalid() || sg_query_pass_state() != SG_PASSSTATE_NONE) {return Result<void>::Failure({ErrorCode::InvalidState,"Change debug commands outside passes.","Submit before rendering.",{}});}
    try {debugDraw_=commands;frameMonitor_.Reset();if (telemetry_) {telemetry_->ResetGpu();}scheduler_.Invalidate();return Result<void>::Success();}catch (const std::bad_alloc& e) {return Result<void>::Failure({ErrorCode::InsufficientResource,e.what(),"Reduce debug line count.",{}});}
}
sg_view RenderSurface::InspectionTexture(int target) const noexcept {return target == 2 ? (shadow_ ? shadow_->Texture:sg_view{}):target == 1 ? (targets_ ? targets_->Texture:sg_view{}):Texture();}
sg_sampler RenderSurface::InspectionSampler(int target) const noexcept {return target == 2 ? (shadow_ ? shadow_->Sampler:sg_sampler{}):(presenter_ ? presenter_->Sampler:sg_sampler{});}
RenderDebugInfo RenderSurface::Inspect() const {
    RenderDebugInfo r;r.Live=sg_isvalid();r.Options=debug_;r.RenderedFrames=frames_;r.LineCount=debugLines_+(debug_.View == DebugView::Wireframe ? Stats().LastWireLines:0);r.LineDrawCalls=debugCalls_;
    if (!r.Live) {return r;}const auto stats=Stats();r.Optimization=stats.Optimization;
    if (shadow_) {r.Passes.push_back({"Sun shadow","shadow_mesh / shadow_skin","R32F + depth, masked triangles",stats.LastShadowDrawCalls,stats.LastShadowTriangles});InspectImage(r.Resources,shadow_->Color,"Shadow depth color",true);InspectImage(r.Resources,shadow_->Depth,"Shadow depth attachment",true);}
    if (targets_) {r.Passes.push_back({"Scene",debug_.View == DebugView::ShadowMap ? "debug_preview":"mesh / skin",DebugViewName(debug_.View),debug_.View == DebugView::ShadowMap ? 1:stats.LastDrawCalls,debug_.View == DebugView::ShadowMap ? 1:stats.LastTriangles});InspectImage(r.Resources,targets_->Color,"Resolved scene color",true);InspectImage(r.Resources,targets_->Msaa,"MSAA scene color",true);InspectImage(r.Resources,targets_->Depth,"Scene depth attachment",true);InspectImage(r.Resources,targets_->PostColor,"Final post output",true);}
    if (debugCalls_) {r.Passes.push_back({"Debug overlay","debug_lines",debug_.DepthTest ? "Depth-tested lines":"Always-visible lines",debugCalls_,0});}
    if (targets_ && targets_->PostColor.id) {r.Passes.push_back({"Post", "post",debug_.View == DebugView::Lit ? "FXAA + Reinhard":"Diagnostic color copy",1,1});}
    if (renderer_) {renderer_->Inspect(r.Resources);}if (debugRenderer_) {debugRenderer_->Inspect(r.Resources);}if (debugPreview_) {InspectShader(r.Resources,debugPreview_->Shader,"Shadow preview");InspectPipeline(r.Resources,debugPreview_->Pipeline,"Shadow preview");}
    for (const auto& resource:r.Resources) {if (resource.Kind == DebugResourceKind::Buffer && (resource.Name == "Wireframe edge indices" || resource.Name == "Debug line stream")) {r.DebugBufferBytes+=resource.Bytes;}}
    if (presenter_) {InspectShader(r.Resources,presenter_->Shader,"present");InspectPipeline(r.Resources,presenter_->Pipeline,"Host/swapchain present");InspectPipeline(r.Resources,presenter_->ColorOnlyPipeline,"Host color-only present");InspectShader(r.Resources,presenter_->PostShader,"post");InspectPipeline(r.Resources,presenter_->PostPipeline,"Post output");}return r;
}
}
