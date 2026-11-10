#include <minuvex/EmbeddedRenderer.h>
#include <atomic>
#include <cmath>
#include <thread>
#if MINUVEX_EMBEDDED_GRAPHICS
#include "HostBridge.h"
#include "platform/GraphicsSession.h"
#include "renderer/RenderSurface.h"
#include <cstdio>
#include <cstdlib>
#endif

namespace lv {
namespace {
template<class T=void> Result<T> Failure(ErrorCode code,const char* text,const char* hint="Use the owner render thread and valid Host resources.") {return Result<T>::Failure({code,text,hint,{}});}
Error Exception(ErrorCode fallback) {
    try {throw;}catch (const std::bad_alloc& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce the Scene or render target size.",{}};}
    catch (const std::invalid_argument& e) {return {ErrorCode::InvalidArgument,e.what(),"Check the Host contract and Scene parameters.",{}};}
    catch (const std::exception& e) {return {fallback,e.what(),"Check Host compatibility, resource limits and device status.",{}};}
}
#if MINUVEX_EMBEDDED_GRAPHICS
struct FrameCommit {
    bool Pending=true;
    ~FrameCommit() {if (Pending && sg_isvalid() && sg_query_pass_state() == SG_PASSSTATE_NONE) {sg_commit();}}
    void Finish() {sg_commit();Pending=false;}
};
struct TargetBinding {
    HostRenderTarget Host;
    sg_image Color{},Depth{};sg_view ColorView{},DepthView{};
    TargetBinding(const HostRenderTarget& host,const sg_environment& environment):Host(host) {
        try {
            sg_image_desc d{};d.width=host.Width;d.height=host.Height;d.sample_count=1;d.pixel_format=environment.defaults.color_format;d.usage.color_attachment=true;d.label="embedded-borrowed-color";
#if defined(_WIN32)
            d.d3d11_texture=host.ColorTexture;
#else
            d.mtl_texture=host.ColorTexture;
#endif
            Color=sg_make_image(d);d.usage={};d.usage.depth_stencil_attachment=true;d.pixel_format=environment.defaults.depth_format;d.label="embedded-borrowed-depth";
#if defined(_WIN32)
            d.d3d11_texture=host.DepthTexture;
#else
            d.mtl_texture=host.DepthTexture;
#endif
            Depth=sg_make_image(d);
            if (sg_query_image_state(Color) != SG_RESOURCESTATE_VALID || sg_query_image_state(Depth) != SG_RESOURCESTATE_VALID) {throw std::runtime_error("Cannot wrap Host render target images.");}
            sg_view_desc v{};v.color_attachment.image=Color;ColorView=sg_make_view(v);v={};v.depth_stencil_attachment.image=Depth;DepthView=sg_make_view(v);
            if (sg_query_view_state(ColorView) != SG_RESOURCESTATE_VALID || sg_query_view_state(DepthView) != SG_RESOURCESTATE_VALID) {throw std::runtime_error("Cannot create Host target attachment views.");}
        }catch (...) {Clear();throw;}
    }
    ~TargetBinding() {Clear();}
    void Clear() noexcept {if (ColorView.id) {sg_destroy_view(ColorView);}if (DepthView.id) {sg_destroy_view(DepthView);}if (Color.id) {sg_destroy_image(Color);}if (Depth.id) {sg_destroy_image(Depth);}ColorView=DepthView={};Color=Depth={};}
    bool Matches(const HostRenderTarget& t) const noexcept {return Host.ColorTexture == t.ColorTexture && Host.DepthTexture == t.DepthTexture && Host.Width == t.Width && Host.Height == t.Height;}
};
#endif
}
struct EmbeddedRenderer::Impl {
    std::thread::id Owner=std::this_thread::get_id();
    std::atomic_bool Initialized{false};
#if MINUVEX_EMBEDDED_GRAPHICS
    detail::GraphicsLease Lease{this};
    std::unique_ptr<detail::HostBridge> Bridge;
    std::unique_ptr<detail::RenderSurface> Surface;
    std::unique_ptr<TargetBinding> Target;
    std::optional<TelemetrySnapshot> LastTelemetry;
    char Diagnostics[1024]{};
    static void Log(const char* tag,std::uint32_t level,std::uint32_t item,const char* text,std::uint32_t,const char*,void* user) noexcept {
        auto& p=*static_cast<Impl*>(user);
        if (level <= 1) {std::snprintf(p.Diagnostics,sizeof(p.Diagnostics),"%s %u: %s",tag,item,text ? text:"Graphics resource failure");}
        if (level == 0) {std::fprintf(stderr,"%s\n",p.Diagnostics);std::abort();} // Native PANIC cannot be recovered.
    }
#endif
    Result<void> Ready(bool initialized=true) const {
        if (std::this_thread::get_id() != Owner) {return Failure(ErrorCode::WrongThread,"EmbeddedRenderer operation called on another thread.");}
        if (initialized && !Initialized.load()) {return Failure(ErrorCode::InvalidState,"Initialize EmbeddedRenderer before using graphics or updating its Scene.");}
        return Result<void>::Success();
    }
};
EmbeddedRenderer::EmbeddedRenderer():impl_(std::make_unique<Impl>()) {}
EmbeddedRenderer::~EmbeddedRenderer() {
    if (impl_->Initialized && std::this_thread::get_id() != impl_->Owner) {std::terminate();}
    if (impl_->Initialized) {if (!Shutdown()) {std::terminate();}}
}
bool EmbeddedRenderer::IsInitialized() const noexcept {return impl_->Initialized.load();}
Result<void> EmbeddedRenderer::Initialize(const HostGraphicsContext& context,const EmbeddedOptions& options) {
    auto& p=*impl_;auto ready=p.Ready(false);if (!ready) {return ready;}
    if (p.Initialized) {return Failure(ErrorCode::InvalidState,"This EmbeddedRenderer is already initialized.");}
    const auto quality=ExpandQuality(options.Quality);if (!quality) {return Result<void>::Failure(quality.Error());}
#if MINUVEX_EMBEDDED_GRAPHICS
    if (!p.Lease.Acquire()) {return Failure(ErrorCode::InvalidState,"Another Minuvex graphics session owns this process.");}
    if (sg_isvalid()) {p.Lease.Release();return Failure(ErrorCode::InvalidState,"An external Sokol GFX context is already active.","Use a native Host device without a separate Sokol implementation/session.");}
    try {
        p.Bridge=std::make_unique<detail::HostBridge>(context);auto hostState=p.Bridge->Enter();
        sg_desc d{};d.environment=p.Bridge->Environment();d.logger.func=Impl::Log;d.logger.user_data=&p;
        d.metal.use_command_buffer_with_retained_references=true;sg_setup(d);
        if (!sg_isvalid()) {throw std::runtime_error("Cannot initialize Embedded graphics on the Host device.");}
        p.Surface=std::make_unique<detail::RenderSurface>();Scene empty;
        auto applied=p.Surface->ApplyQuality(options.Quality,empty,0,0);
        if (!applied) {const auto error=applied.Error();p.Surface.reset();sg_shutdown();hostState.reset();p.Bridge.reset();p.Lease.Release();return Result<void>::Failure(error);}
        sg_commit();p.LastTelemetry.reset();p.Initialized=true;return Result<void>::Success();
    } catch (...) {
        const auto error=Exception(ErrorCode::GraphicsInitialization);
        // Scope restoration already ran on exception. Shutdown inside isolation as well.
        if (p.Bridge) {auto scope=p.Bridge->Enter();p.Target.reset();p.Surface.reset();if (sg_isvalid()) {sg_shutdown();}}
        p.Bridge.reset();p.Lease.Release();return Result<void>::Failure(error);
    }
#else
    (void)context;return Failure(ErrorCode::UnsupportedFeature,"Embedded graphics was disabled at build time.","Build with MINUVEX_BUILD_RENDERER=ON on Windows or macOS.");
#endif
}
Result<void> EmbeddedRenderer::Update(Scene& scene,double delta) {
    auto ready=impl_->Ready();if (!ready) {return ready;}
    try {scene.UpdateAnimations(delta);return Result<void>::Success();}catch (...) {return Result<void>::Failure(Exception(ErrorCode::InvalidArgument));}
}
Result<EmbeddedFrameResult> EmbeddedRenderer::Render(const Scene& scene,const HostRenderTarget& target,const EmbeddedFrameOptions& options) {
    auto& p=*impl_;auto ready=p.Ready();if (!ready) {return Result<EmbeddedFrameResult>::Failure(ready.Error());}
    for (const float c:options.ClearColor) {if (!std::isfinite(c) || c < 0 || c > 1) {return Failure<EmbeddedFrameResult>(ErrorCode::InvalidArgument,"Clear color must be finite and in [0,1].");}}
    if (target.Width < 0 || target.Height < 0 || target.Width > 8192 || target.Height > 8192 || (target.Width == 0) != (target.Height == 0) || (target.Width > 0 && (!target.ColorTexture || !target.DepthTexture || target.ColorTexture == target.DepthTexture))) {return Failure<EmbeddedFrameResult>(ErrorCode::InvalidArgument,"Provide two matching Host textures and dimensions, or a zero-size suspended target.");}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {
        auto hostState=p.Bridge->Enter();FrameCommit frame;sg_reset_state_cache();p.Diagnostics[0]=0;
        if (!target.Width) {p.Target.reset();(void)p.Surface->Resize(scene,0,0);frame.Finish();return Result<EmbeddedFrameResult>::Success({});}
        const auto valid=p.Bridge->ValidateTarget(target);if (!valid) {return Result<EmbeddedFrameResult>::Failure(valid.Error());}
        std::unique_ptr<TargetBinding> candidate;
        if (!p.Target || !p.Target->Matches(target)) {candidate=std::make_unique<TargetBinding>(target,p.Bridge->Environment());}
        const auto resized=p.Surface->Resize(scene,target.Width,target.Height);if (!resized) {return Result<EmbeddedFrameResult>::Failure(resized.Error());}
        if (candidate) {p.Target.swap(candidate);candidate.reset();}
        if (options.ForceRedraw) {p.Surface->Invalidate();}
        const bool rendered=p.Surface->Render(scene,detail::RenderClockSeconds(),false,options.ClearColor);
        sg_pass pass{};pass.attachments.colors[0]=p.Target->ColorView;pass.attachments.depth_stencil=p.Target->DepthView;
        pass.action.colors[0].load_action=SG_LOADACTION_CLEAR;pass.action.colors[0].clear_value={options.ClearColor[0],options.ClearColor[1],options.ClearColor[2],options.ClearColor[3]};pass.action.depth.load_action=SG_LOADACTION_CLEAR;pass.action.depth.clear_value=1;
        sg_begin_pass(pass);p.Surface->Present();sg_end_pass();frame.Finish();
        if (p.Diagnostics[0]) {return Failure<EmbeddedFrameResult>(ErrorCode::Rendering,p.Diagnostics);}
        return Result<EmbeddedFrameResult>::Success({rendered,true,p.Surface->Width(),p.Surface->Height()});
    }catch (...) {return Result<EmbeddedFrameResult>::Failure(Exception(ErrorCode::Rendering));}
#else
    (void)scene;return Failure<EmbeddedFrameResult>(ErrorCode::UnsupportedFeature,"Embedded graphics is unavailable.");
#endif
}
Result<void> EmbeddedRenderer::SetQuality(const QualityProfile& profile,const Scene& scene) {
    auto& p=*impl_;auto ready=p.Ready();if (!ready) {return ready;}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {auto scope=p.Bridge->Enter();FrameCommit frame;sg_reset_state_cache();auto result=p.Surface->ApplyQuality(profile,scene,p.Target ? p.Target->Host.Width:0,p.Target ? p.Target->Host.Height:0);frame.Finish();return result;}
    catch (...) {return Result<void>::Failure(Exception(ErrorCode::Rendering));}
#else
    (void)profile;(void)scene;return Failure(ErrorCode::UnsupportedFeature,"Embedded graphics is unavailable.");
#endif
}
Result<QualityState> EmbeddedRenderer::GetQuality() const {
    const auto& p=*impl_;auto ready=p.Ready();if (!ready) {return Result<QualityState>::Failure(ready.Error());}
#if MINUVEX_EMBEDDED_GRAPHICS
    return Result<QualityState>::Success(p.Surface->Quality());
#else
    return Failure<QualityState>(ErrorCode::UnsupportedFeature,"Embedded graphics is unavailable.");
#endif
}
Result<QualityCapabilities> EmbeddedRenderer::GetQualityCapabilities() const {
    auto quality=GetQuality();if (!quality) {return Result<QualityCapabilities>::Failure(quality.Error());}return Result<QualityCapabilities>::Success(quality.Value().Capabilities);
}
Result<TelemetrySnapshot> EmbeddedRenderer::GetTelemetry() const {
    auto& p=*impl_;auto ready=p.Ready(false);if (!ready) {return Result<TelemetrySnapshot>::Failure(ready.Error());}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {if (p.Initialized) {auto scope=p.Bridge->Enter();return Result<TelemetrySnapshot>::Success(p.Surface->Telemetry(detail::RenderClockSeconds()));}
        if (p.LastTelemetry) {return Result<TelemetrySnapshot>::Success(*p.LastTelemetry);}
    }catch (...) {return Result<TelemetrySnapshot>::Failure(Exception(ErrorCode::Rendering));}
#endif
    return Failure<TelemetrySnapshot>(ErrorCode::InvalidState,"No Embedded telemetry snapshot is available.");
}
Result<void> EmbeddedRenderer::RequestRedraw() {
    auto ready=impl_->Ready();if (!ready) {return ready;}
#if MINUVEX_EMBEDDED_GRAPHICS
    impl_->Surface->Invalidate();
#endif
    return Result<void>::Success();
}
Result<void> EmbeddedRenderer::DetachTarget() {
    auto& p=*impl_;auto ready=p.Ready();if (!ready) {return ready;}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {auto scope=p.Bridge->Enter();sg_reset_state_cache();p.Target.reset();Scene empty;(void)p.Surface->Resize(empty,0,0);sg_commit();return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(Exception(ErrorCode::Rendering));}
#else
    return Failure(ErrorCode::UnsupportedFeature,"Embedded graphics is unavailable.");
#endif
}
Result<void> EmbeddedRenderer::WaitIdle(std::uint32_t timeout) {
    auto& p=*impl_;auto ready=p.Ready();if (!ready) {return ready;}
    if (!timeout || timeout > 60000) {return Failure(ErrorCode::InvalidArgument,"GPU wait timeout must be 1..60000 milliseconds.");}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {auto scope=p.Bridge->Enter();return p.Bridge->WaitIdle(timeout);}catch (...) {return Result<void>::Failure(Exception(ErrorCode::Rendering));}
#else
    return Failure(ErrorCode::UnsupportedFeature,"Embedded graphics is unavailable.");
#endif
}
Result<void> EmbeddedRenderer::Shutdown() {
    auto& p=*impl_;auto ready=p.Ready(false);if (!ready) {return ready;}if (!p.Initialized) {return Result<void>::Success();}
#if MINUVEX_EMBEDDED_GRAPHICS
    try {
        auto scope=p.Bridge->Enter();try {p.LastTelemetry=p.Surface->Telemetry(detail::RenderClockSeconds());p.LastTelemetry->Live=false;}catch (...) {}
        p.Target.reset();p.Surface.reset();sg_shutdown();scope.reset();p.Bridge.reset();p.Initialized=false;p.Lease.Release();return Result<void>::Success();
    }catch (...) {return Result<void>::Failure(Exception(ErrorCode::Rendering));}
#else
    return Result<void>::Success();
#endif
}
} // namespace lv
