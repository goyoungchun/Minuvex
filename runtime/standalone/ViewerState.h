#pragma once

#include "CameraController.h"

#include <atomic>
#include <thread>

namespace lv::detail {

struct ViewerState {
    explicit ViewerState(ViewerOptions options) : Options(std::move(options)), AutoFrame(Options.AutoFrame) {}
    ViewerOptions Options;
    AssetRegistry Assets;
    Scene World;
    CameraController Controls;
    std::thread::id Owner = std::this_thread::get_id();
    std::atomic_bool Running{false}, CloseRequested{false};
    bool HasRun = false;
    bool AutoFrame;
    bool DispatchingInput = false;
    const ViewerRunOptions* ActiveRun = nullptr;
    int Width = 0, Height = 0;
    float Aspect() const {
        return static_cast<float>(Width > 0 ? Width : Options.Width) / static_cast<float>(Height > 0 ? Height : Options.Height);
    }
};

Result<void> RunStandalone(Viewer& viewer, ViewerState& state, const ViewerRunOptions& options);
Error CurrentException(ErrorCode fallback, const char* hint);

} // namespace lv::detail
