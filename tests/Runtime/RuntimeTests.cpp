#include "TestHarness.h"
#include <minuvex/Viewer.h>

#include <thread>

using namespace test;
namespace {
void Require(const lv::Result<void>& result) { if (!result) { throw std::runtime_error(result.Error().Message); } }
bool Same(lv::Vec3 a, lv::Vec3 b) { return Near(a.X,b.X) && Near(a.Y,b.Y) && Near(a.Z,b.Z); }
}

int main(int argc, char* argv[]) {
    if (argc != 2) { return 2; }
    const std::string mode(argv[1]);
    const bool animated = mode == "animation" || mode == "animation-manual";
    lv::Viewer viewer;
    lv::Viewer nested;
    lv::ObjectId modelId = 0;
    if (mode != "empty") {
        const auto model = viewer.AddModel(Fixture(animated ? "AnimatedRibbon.gltf" : "SharedBox.glb"));
        if (!model) { Check(false,model.Error().Message.c_str()); return Finish(); }
        modelId = model.Value();
    }
    if (animated) { Require(viewer.PlayAnimation(modelId,0)); viewer.GetScene().GetAnimation(modelId).Seek(.5); }
    std::uint64_t callbacks = 0;
    bool consume = false, throwInput = false, recursive = false;
    lv::ViewerRunOptions options;
    options.FrameLimit = 3;
    if (mode == "telemetry") {options.FrameLimit=80;}
    options.AdvanceAnimations = mode != "animation-manual";
    double expectedTime = .5;
    options.OnInput = [&](lv::Viewer& v, const lv::ViewerInputEvent& event) {
        if (recursive && event.Key == lv::ViewerKey::Space) {
            const auto reentered = v.ProcessInput(event);
            Check(!reentered && reentered.Error().Code == lv::ErrorCode::InvalidState,"recursive input is rejected without stack overflow");
            return true;
        }
        if (throwInput && event.Type == lv::ViewerInputType::Scroll) { throw std::runtime_error("expected input failure"); }
        return consume;
    };
    options.OnFrame = [&](lv::Viewer& v, const lv::ViewerFrameInfo& info) {
        Check(v.IsRunning(),"OnFrame runs while Viewer owns the native loop");
        Check(info.FrameNumber == callbacks && info.FramebufferWidth > 0 && info.FramebufferHeight > 0 &&
            std::isfinite(info.DeltaTimeSeconds) && info.DeltaTimeSeconds >= 0,"frame callback receives valid framebuffer/timing information");
        ++callbacks;
        if (mode == "telemetry") {
            auto t=v.GetTelemetry();Check(t && t.Value().Live && t.Value().Hardware.ActiveGraphicsDevice && !t.Value().Hardware.GpuName.empty() && !t.Value().Hardware.Formats.empty(),"live telemetry identifies the actual graphics device and relevant format support");
            if (info.FrameNumber == 20) {
                const auto& s=t.Value();Check(s.Frames.SampleCount > 0 && s.Frames.DrawCalls > 0 && s.Frames.Triangles > 0 && s.Frames.AverageCpuMs.Value,"actual rendered frames expose timing and draw/triangle counters");
                auto estimate=lv::EstimateQualityResources(s.Quality.Requested,s.Quality.Capabilities,v.GetScene(),info.FramebufferWidth,info.FramebufferHeight);
                Check(estimate && estimate.Value().Cost.TotalBytes == s.Resident.TotalBytes && estimate.Value().Cost.TextureBytes == s.Resident.TextureBytes,"public estimate matches resident renderer logical allocation accounting");
                Check(!s.System.GpuMemoryUsage.Value || s.System.GpuMemoryUsage.Status == lv::MetricStatus::Measured,"native memory availability is explicit rather than fabricated");
            }
            if (info.FrameNumber == 40) {auto p=lv::MakeQualityProfile(lv::QualityPreset::Low);Require(v.SetQuality(p));auto changed=v.GetTelemetry();Check(changed && changed.Value().PreviousQualityFrames && changed.Value().Frames.SampleCount == 0,"successful quality change retains before history and resets current timing window");}
        }
        if (mode == "quality") {
            auto caps=v.GetQualityCapabilities();Check(caps && caps.Value().GraphicsAvailable,"Viewer exposes active graphics capabilities");
            if (info.FrameNumber == 0) {
                auto low=lv::MakeQualityProfile(lv::QualityPreset::Low);low.Overrides.ResolutionScale=.5f;Require(v.SetQuality(low));
                auto state=v.GetQuality();Check(state && state.Value().Applied && state.Value().RenderWidth <= info.FramebufferWidth/2 && state.Value().Effective->TargetFps == 30,"SetQuality applies Low resolution and pacing inside OnFrame");
                auto invalid=low;invalid.Overrides.Shadow=lv::ShadowMode::Pcf;Check(!v.SetQuality(invalid) && v.GetQuality().Value().Requested == low,"runtime quality failure retains prior requested/effective settings");
            }
            if (info.FrameNumber == 1) {
                auto high=lv::MakeQualityProfile(lv::QualityPreset::High);high.AllowFallback=true;Require(v.SetQuality(high));
                Check(v.GetQuality().Value().Requested.Base == lv::QualityPreset::High && v.GetQuality().Value().Applied,"runtime can raise quality without reopening window");Require(v.RequestRedraw());
            }
        }
        if (animated) {
            if (mode == "animation") { expectedTime += info.DeltaTimeSeconds; }
            Check(std::abs(v.GetScene().GetAnimation(modelId).Time()-expectedTime) < 0.000001,"runtime advances animation before OnFrame, or leaves it to manual updates");
            if (mode == "animation-manual") { v.GetScene().UpdateAnimations(.125); expectedTime += .125; }
        }
        if (mode == "callback-error") { throw std::runtime_error("expected frame failure"); }
        if (mode == "render-error") { v.GetScene().MainCamera.Target = v.GetScene().MainCamera.Position; return; }
        if (mode == "normal" && info.FrameNumber == 0) {
            options.FrameLimit = 1; // Run must use its original options snapshot.
            const auto self = v.Run();
            Check(!self && self.Error().Code == lv::ErrorCode::InvalidState,"reentrant Run returns InvalidState");
            const auto second = nested.Run();
            Check(!second && second.Error().Code == lv::ErrorCode::InvalidState,"another Viewer cannot take over the active Sokol context");
        }
        if (mode == "normal" && info.FrameNumber == 1) {
            lv::ModelOptions placed; placed.Transform.Position.X = 2;
            const auto model = v.AddModel(Fixture("SharedBox.glb"),placed);
            Check(model.HasValue() && v.GetScene().Size() == 2,"OnFrame may add a shared model before resource upload");
            Require(v.SetVisible(modelId,false));
            Require(v.FrameScene());
        }
        if (mode == "input" && info.FrameNumber == 0) {
            lv::ViewerInputEvent event;
            event.Type = lv::ViewerInputType::MouseDown; event.Button = lv::ViewerMouseButton::Left;
            Require(v.ProcessInput(event));
            event.Type = lv::ViewerInputType::MouseMove; event.DeltaX = 40;
            const auto camera = v.GetScene().MainCamera;
            consume = true; Require(v.ProcessInput(event)); consume = false;
            Check(Same(camera.Position,v.GetScene().MainCamera.Position),"OnInput can consume an event before default orbit");
            Require(v.ProcessInput(event));
            Check(!Same(camera.Position,v.GetScene().MainCamera.Position),"unconsumed input reaches default camera controls");
            consume = true; event.Type = lv::ViewerInputType::FocusLost; Require(v.ProcessInput(event)); consume = false;
            const auto unfocused = v.GetScene().MainCamera;
            event.Type = lv::ViewerInputType::MouseMove; Require(v.ProcessInput(event));
            Check(Same(unfocused.Position,v.GetScene().MainCamera.Position),"consumed focus-loss still resets drag state");
            throwInput = true; event.Type = lv::ViewerInputType::Scroll; event.ScrollY = 1;
            const auto rejected = v.ProcessInput(event);
            Check(!rejected && rejected.Error().Code == lv::ErrorCode::CallbackFailure,"input callback exception is returned as Result");
            throwInput = false;
            Require(v.ProcessInput(event));
            recursive = true; event = {}; event.Type = lv::ViewerInputType::KeyDown; event.Key = lv::ViewerKey::Space;
            Require(v.ProcessInput(event)); recursive = false;
        }
    };
    std::jthread closer;
    if (mode == "close") {
        options.FrameLimit = 0;
        closer = std::jthread([&](std::stop_token token) {
            while (!viewer.IsRunning() && !token.stop_requested()) { std::this_thread::yield(); }
            if (!token.stop_requested()) { viewer.RequestClose(); }
        });
    }
    const auto result = viewer.Run(options);
    if (closer.joinable()) { closer.request_stop(); closer.join(); }
    Check(!viewer.IsRunning(),"Run returns with running state cleared");
    if (mode == "callback-error" || mode == "render-error") {
        const auto code = mode == "callback-error" ? lv::ErrorCode::CallbackFailure : lv::ErrorCode::Rendering;
        Check(!result && result.Error().Code == code && !result.Error().Hint.empty(),"runtime failure is returned after graphics cleanup");
        Check(callbacks == 1,"a failed callback/render does not execute another user frame");
        Require(viewer.SetCamera(lv::Camera{}));
    } else {
        Check(result.HasValue(),result ? "standalone run succeeds" : result.Error().Message.c_str());
        if (mode != "close") { Check(callbacks == (mode == "telemetry" ? 80u:3u),"frame limit uses the Run entry snapshot"); }
    }
    if (mode == "telemetry") {
        const auto t=viewer.GetTelemetry();Check(t && !t.Value().Live && t.Value().Frames.SampleCount > 0,"last telemetry snapshot survives graphics cleanup and is marked historical");
        if (t) {
            Check(t.Value().Frames.GpuFrameMs.Status == lv::MetricStatus::Unsupported || (t.Value().Frames.GpuFrameMs.Value && *t.Value().Frames.GpuFrameMs.Value >= 0),"supported GPU timestamps resolve asynchronously without blocking");
            Check(t.Value().System.ProcessCpuPercent.Value.has_value(),"native runtime refreshes process counters after one second");
            TemporaryDirectory temp;Check(bool(lv::SaveTelemetry(t.Value(),temp.File("native.json"))),"native telemetry snapshot exports after shutdown");
        }
    }
    const auto again = viewer.Run();
    Check(!again && again.Error().Code == lv::ErrorCode::InvalidState,"Viewer cannot start a second native session");
    const auto other = nested.Run();
    Check(!other && other.Error().Code == lv::ErrorCode::InvalidState,"process cannot start another standalone event loop after cleanup");
    return Finish();
}
