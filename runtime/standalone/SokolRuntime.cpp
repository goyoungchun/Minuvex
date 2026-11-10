#include "ViewerState.h"
#include "RuntimePlatform.h"
#include "renderer/RenderSurface.h"

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace lv::detail {
namespace {
std::atomic_bool standaloneUsed{false};

struct Runtime {
    Viewer& Facade;
    ViewerState& State;
    const ViewerRunOptions& Options;
    RenderSurface Surface;
    double LastUpdate = 0;
    std::uint64_t Frames = 0;
    bool Initialized = false, Failed = false, InPass = false;
    ErrorCode Code = ErrorCode::GraphicsInitialization;
    char Message[2048]{};
    char Hint[512]{};

    void Fail(ErrorCode code, const char* message, const char* hint) noexcept {
        if (!Failed) {
            Failed = true; Code = code;
            std::snprintf(Message,sizeof(Message),"%s",message);
            std::snprintf(Hint,sizeof(Hint),"%s",hint);
        }
        State.CloseRequested.store(true);
    }
    void Capture(ErrorCode code, const char* hint) noexcept {
        try { throw; }
        catch (const std::bad_alloc&) { Fail(ErrorCode::InsufficientResource,"Runtime allocation failed.","Reduce model/resource sizes and retry."); }
        catch (const std::exception& error) { Fail(code,error.what(),hint); }
        catch (...) { Fail(code,"Callback/runtime threw a non-standard exception.",hint); }
    }
    bool Stop() const noexcept { return Failed || State.CloseRequested.load(); }
};

void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message,
         std::uint32_t line, const char* file, void* user) noexcept {
    auto& runtime = *static_cast<Runtime*>(user);
    if (runtime.Surface.Applying() && level <= 1) {slog_func(tag,level,item,message,line,file,nullptr);return;}
    if (level <= 1) {
        char description[2048];
        std::snprintf(description,sizeof(description),"%s error %u: %s",tag,item,message ? message : "See the native graphics diagnostics.");
        runtime.Fail(runtime.Initialized ? ErrorCode::Rendering : ErrorCode::GraphicsInitialization,
            description,"Check graphics-driver/backend compatibility and GPU resource limits.");
    }
    // Sokol PANIC is fatal by contract; never resume native initialization from
    // a state the backend declares unrecoverable. Ordinary errors return Result.
    slog_func(tag,level,item,message,line,file,nullptr);
}

void Initialize(void* user) noexcept {
    auto& runtime = *static_cast<Runtime*>(user);
    try {
        sg_desc graphics{};
        graphics.environment = sglue_environment();
        graphics.logger.func = Log; graphics.logger.user_data = &runtime;
        sg_setup(graphics);
        if (!sg_isvalid()) {
            runtime.Fail(ErrorCode::GraphicsInitialization,"Sokol GFX setup failed.","Use a compatible D3D11/Metal device and current graphics driver.");
            return;
        }
        runtime.State.Width = std::max(1,static_cast<int>(std::lround(sapp_widthf()/sapp_dpi_scale())));
        runtime.State.Height = std::max(1,static_cast<int>(std::lround(sapp_heightf()/sapp_dpi_scale())));
        if (runtime.State.AutoFrame && !runtime.State.World.CollectRenderItems().empty()) {
            const auto fit = runtime.Facade.FrameScene();
            if (!fit) { runtime.Fail(fit.Error().Code,fit.Error().Message.c_str(),fit.Error().Hint.c_str()); return; }
        }
        const auto quality=runtime.Surface.ApplyQuality(runtime.State.Quality.Requested,runtime.State.World,sapp_width(),sapp_height());
        if (!quality) {runtime.Fail(quality.Error().Code,quality.Error().Message.c_str(),quality.Error().Hint.c_str());return;}
        runtime.State.Quality=runtime.Surface.Quality();runtime.LastUpdate=RenderClockSeconds();
        runtime.Initialized = !runtime.Failed;
    } catch (...) { runtime.Capture(ErrorCode::GraphicsInitialization,"Check the Scene and GPU resource availability."); }
}

void Frame(void* user) noexcept {
    auto& runtime = *static_cast<Runtime*>(user);
    if (runtime.Stop()) { sapp_quit(); return; }
    const int width = sapp_width(), height = sapp_height();
    if (width <= 0 || height <= 0) { return; }
    try {
        const auto dpi = sapp_dpi_scale();
        runtime.State.Width = std::max(1,static_cast<int>(std::lround(static_cast<float>(width)/dpi)));
        runtime.State.Height = std::max(1,static_cast<int>(std::lround(static_cast<float>(height)/dpi)));
        (void)runtime.Surface.Resize(runtime.State.World,width,height);runtime.State.Quality=runtime.Surface.Quality();
        const auto now=RenderClockSeconds();
        if (!runtime.Surface.Due(now)) {std::this_thread::sleep_for(std::chrono::milliseconds(1));return;}
        const auto elapsed=now-runtime.LastUpdate;runtime.LastUpdate=now;
        const ViewerFrameInfo info{runtime.Frames,std::isfinite(elapsed) ? std::clamp(elapsed,0.0,0.25) : 0.0,width,height};
        if (runtime.Options.AdvanceAnimations) { runtime.State.World.UpdateAnimations(info.DeltaTimeSeconds); }
        if (runtime.Options.OnFrame) {
            try { runtime.Options.OnFrame(runtime.Facade,info); }
            catch (...) { runtime.Capture(ErrorCode::CallbackFailure,"Handle exceptions inside OnFrame or return control without throwing."); }
        }
        if (runtime.Stop()) { sapp_quit(); return; }
        const bool submitted=runtime.Surface.Render(runtime.State.World,now,runtime.Options.FrameLimit != 0 || bool(runtime.Options.OnFrame),runtime.State.Options.ClearColor);
        if (!submitted) {std::this_thread::sleep_for(std::chrono::milliseconds(8));return;}
        sg_pass pass{};
        pass.swapchain = sglue_swapchain();
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        const auto& color = runtime.State.Options.ClearColor;
        pass.action.colors[0].clear_value = {color[0],color[1],color[2],color[3]};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR; pass.action.depth.clear_value = 1;
        sg_begin_pass(pass); runtime.InPass = true;
        runtime.Surface.Present();
        sg_end_pass(); runtime.InPass = false;
        sg_commit();
        ++runtime.Frames;
        if (runtime.Options.FrameLimit && runtime.Frames >= runtime.Options.FrameLimit) { runtime.State.CloseRequested.store(true); }
        if (runtime.Stop()) { sapp_quit(); }
    } catch (...) {
        if (runtime.InPass) { sg_end_pass(); runtime.InPass = false; }
        runtime.Capture(ErrorCode::Rendering,"Check Scene transforms, camera/light values and available GPU resources.");
        sapp_quit();
    }
}

ViewerMouseButton MouseButton(sapp_mousebutton button) noexcept {
    switch (button) {
    case SAPP_MOUSEBUTTON_LEFT: return ViewerMouseButton::Left;
    case SAPP_MOUSEBUTTON_RIGHT: return ViewerMouseButton::Right;
    case SAPP_MOUSEBUTTON_MIDDLE: return ViewerMouseButton::Middle;
    default: return ViewerMouseButton::None;
    }
}

void Event(const sapp_event* source, void* user) noexcept {
    auto& runtime = *static_cast<Runtime*>(user);
    if (source->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        runtime.State.CloseRequested.store(true);
        return;
    }
    if (runtime.Stop()) { return; }
    try {
        ViewerInputEvent event;
        switch (source->type) {
        case SAPP_EVENTTYPE_MOUSE_DOWN: event.Type = ViewerInputType::MouseDown; break;
        case SAPP_EVENTTYPE_MOUSE_UP: event.Type = ViewerInputType::MouseUp; break;
        case SAPP_EVENTTYPE_MOUSE_MOVE: event.Type = ViewerInputType::MouseMove; break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL: event.Type = ViewerInputType::Scroll; break;
        case SAPP_EVENTTYPE_KEY_DOWN: event.Type = ViewerInputType::KeyDown; break;
        case SAPP_EVENTTYPE_RESIZED: event.Type = ViewerInputType::Resized; break;
        case SAPP_EVENTTYPE_UNFOCUSED:
        case SAPP_EVENTTYPE_ICONIFIED:
        case SAPP_EVENTTYPE_MOUSE_LEAVE: event.Type = ViewerInputType::FocusLost; break;
        default: return;
        }
        const float dpi = sapp_dpi_scale() > 0 ? sapp_dpi_scale() : 1;
        event.Button = MouseButton(source->mouse_button);
        event.DeltaX = source->mouse_dx/dpi; event.DeltaY = source->mouse_dy/dpi;
        event.ScrollY = source->scroll_y;
        event.Width = std::max(0,static_cast<int>(std::lround(sapp_widthf()/dpi)));
        event.Height = std::max(0,static_cast<int>(std::lround(sapp_heightf()/dpi)));
        event.Shift = (source->modifiers & SAPP_MODIFIER_SHIFT) != 0;
        event.Repeat = source->key_repeat;
        switch (source->key_code) {
        case SAPP_KEYCODE_ESCAPE: event.Key = ViewerKey::Escape; break;
        case SAPP_KEYCODE_F: event.Key = ViewerKey::F; break;
        case SAPP_KEYCODE_SPACE: event.Key = ViewerKey::Space; break;
        default: break;
        }
        const auto handled = runtime.Facade.ProcessInput(event);
        if (!handled && !(handled.Error().Code == ErrorCode::InvalidState && event.Key == ViewerKey::F)) {
            runtime.Fail(handled.Error().Code,handled.Error().Message.c_str(),handled.Error().Hint.c_str());
        }
    } catch (...) { runtime.Capture(ErrorCode::Rendering,"Check the input callback and camera state."); }
}

void Cleanup(void* user) noexcept {
    auto& runtime = *static_cast<Runtime*>(user);
    try {if (runtime.Initialized) {runtime.State.LastTelemetry=runtime.Surface.Telemetry(RenderClockSeconds());runtime.State.LastTelemetry->Live=false;}}catch (...) { /* Diagnostic collection must not prevent native cleanup. */ }
    runtime.Surface.Clear();runtime.State.Quality.Applied=false;runtime.State.Quality.Effective.reset();runtime.State.Quality.RenderWidth=runtime.State.Quality.RenderHeight=0;
    runtime.State.Controls.Reset();
    if (sg_isvalid()) { sg_shutdown(); }
}
} // namespace

bool GraphicsRuntimeAvailable() noexcept {return true;}

Result<void> RunStandalone(Viewer& viewer, ViewerState& state, const ViewerRunOptions& options) {
    if (!StandaloneMainThread()) {
        return Result<void>::Failure({ErrorCode::WrongThread,"macOS standalone windows must run on the main thread.","Construct and run the Viewer on the application's main thread.",{}});
    }
    bool expected = false;
    if (!standaloneUsed.compare_exchange_strong(expected,true)) {
        return Result<void>::Failure({ErrorCode::InvalidState,"This process has already used its standalone event-loop session.","Create a new process for another standalone Viewer session.",{}});
    }
    // Claim the session before reading Sokol's non-thread-safe global state.
    // Another Viewer on another owner thread must fail without touching it.
    if (!StandaloneApplicationAvailable() || sg_isvalid() || sapp_isvalid()) {
        standaloneUsed.store(false);
        return Result<void>::Failure({ErrorCode::InvalidState,"An existing application/graphics context already owns the standalone runtime.","Use Viewer in a standalone process; embedded integration is a later feature.",{}});
    }
    Runtime runtime{viewer,state,options};
    state.ApplyQuality=[&](const QualityProfile& profile) {
        if (!sg_isvalid()) {QualityState next;next.Requested=profile;next.RequestedSettings=ExpandQuality(profile).Value();state.Quality=std::move(next);return Result<void>::Success();}
        const auto result=runtime.Surface.ApplyQuality(profile,state.World,std::max(1,sapp_width()),std::max(1,sapp_height()));state.Quality=runtime.Surface.Quality();return result;
    };
    state.Invalidate=[&]{runtime.Surface.Invalidate();};
    state.ReadTelemetry=[&]{return runtime.Surface.Telemetry(RenderClockSeconds());};
    state.CloseRequested.store(false);
    state.HasRun = true;
    state.ActiveRun = &options;
    state.Running.store(true);
    struct ResetRunning { ViewerState& State; ~ResetRunning() { State.ApplyQuality={};State.Invalidate={};State.ReadTelemetry={};State.ActiveRun = nullptr; State.Running.store(false); } } reset{state};
    sapp_desc app{};
    app.user_data = &runtime;
    app.init_userdata_cb = Initialize; app.frame_userdata_cb = Frame;
    app.cleanup_userdata_cb = Cleanup; app.event_userdata_cb = Event;
    app.width = state.Options.Width; app.height = state.Options.Height;
    app.window_title = state.Options.Title.c_str(); app.sample_count = 1;
    app.high_dpi = state.Options.HighDpi; app.srgb = true;
    app.disable_vsync = !state.Options.VSync;
    app.logger.func = Log; app.logger.user_data = &runtime;
    RunStandaloneApplication(app);
    if (runtime.Failed) { return Result<void>::Failure({runtime.Code,runtime.Message,runtime.Hint,{}}); }
    return Result<void>::Success();
}

} // namespace lv::detail
