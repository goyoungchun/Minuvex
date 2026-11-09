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
    lv::Viewer viewer;
    lv::Viewer nested;
    lv::ObjectId modelId = 0;
    if (mode != "empty") {
        const auto model = viewer.AddModel(Fixture("SharedBox.glb"));
        if (!model) { Check(false,model.Error().Message.c_str()); return Finish(); }
        modelId = model.Value();
    }
    std::uint64_t callbacks = 0;
    bool consume = false, throwInput = false, recursive = false;
    lv::ViewerRunOptions options;
    options.FrameLimit = 3;
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
        if (mode != "close") { Check(callbacks == 3,"frame limit submits exactly three frame callbacks"); }
    }
    const auto again = viewer.Run();
    Check(!again && again.Error().Code == lv::ErrorCode::InvalidState,"Viewer cannot start a second native session");
    const auto other = nested.Run();
    Check(!other && other.Error().Code == lv::ErrorCode::InvalidState,"process cannot start another standalone event loop after cleanup");
    return Finish();
}
