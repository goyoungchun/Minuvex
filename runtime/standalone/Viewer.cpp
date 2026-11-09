#include "ViewerState.h"

#include <cmath>
#include <exception>

namespace lv::detail {

Error CurrentException(ErrorCode fallback, const char* hint) {
    try { throw; }
    catch (const ImportError& error) {
        switch (error.Code) {
        case ImportErrorCode::Io: return {ErrorCode::AssetLoad, error.what(), "Check the model path and referenced resource files.", {}};
        case ImportErrorCode::InvalidData: return {ErrorCode::InvalidData, error.what(), "Validate the glTF/GLB file and its buffer/image data.", {}};
        case ImportErrorCode::UnsupportedFeature: return {ErrorCode::UnsupportedFeature, error.what(), "Export a static TRIANGLES glTF 2.0 model with supported materials.", {}};
        case ImportErrorCode::ResourceLimit: return {ErrorCode::InsufficientResource, error.what(), "Reduce the asset size or adjust ImportSettings limits.", {}};
        }
        return {fallback, error.what(), hint, {}};
    }
    catch (const std::bad_alloc& error) { return {ErrorCode::InsufficientResource, error.what(), "Reduce memory usage and retry.", {}}; }
    catch (const std::exception& error) { return {fallback, error.what(), hint, {}}; }
    catch (...) { return {fallback, "Operation failed with a non-standard exception.", hint, {}}; }
}

} // namespace lv::detail

namespace lv {
namespace {
Result<void> Ready(const detail::ViewerState& state) {
    if (std::this_thread::get_id() != state.Owner) {
        return Result<void>::Failure({ErrorCode::WrongThread, "Viewer operation called from a different thread.", "Use the Viewer on its construction thread; RequestClose/IsRunning may be used from another thread.", {}});
    }
    try { detail::ValidateViewerOptions(state.Options); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Correct ViewerOptions before constructing the Viewer.")); }
    return Result<void>::Success();
}
Result<void> ObjectReady(const detail::ViewerState& state, ObjectId id) {
    auto ready = Ready(state);
    if (!ready) { return ready; }
    if (!state.World.Contains(id)) { return Result<void>::Failure({ErrorCode::InvalidHandle, "Model/object ID is not in this Viewer.", "Use a live ID returned by this Viewer or its Scene.", {}}); }
    return Result<void>::Success();
}
}

Viewer::Viewer(ViewerOptions options) : state_(std::make_unique<detail::ViewerState>(std::move(options))) {}
Viewer::~Viewer() = default;
Scene& Viewer::GetScene() noexcept { return state_->World; }
const Scene& Viewer::GetScene() const noexcept { return state_->World; }

Result<ObjectId> Viewer::AddModel(const std::filesystem::path& path, const ModelOptions& options) {
    auto ready = Ready(*state_);
    if (!ready) { return Result<ObjectId>::Failure(ready.Error()); }
    if (options.Parent && !state_->World.Contains(options.Parent)) {
        return Result<ObjectId>::Failure({ErrorCode::InvalidHandle, "Model parent does not belong to this Viewer.", "Create the parent in GetScene() or use NoObject.", path});
    }
    try {
        (void)options.Transform.Matrix().NormalMatrix();
        const auto model = state_->Assets.LoadModel(path, options.Import);
        const auto id = state_->World.AddModel(model, options.Transform, options.Parent);
        if (state_->AutoFrame) {
            auto framed = FrameScene();
            if (!framed) {
                state_->World.RemoveObject(id);
                auto error = framed.Error(); error.SourcePath = path;
                return Result<ObjectId>::Failure(std::move(error));
            }
        }
        return Result<ObjectId>::Success(id);
    } catch (...) {
        auto error = detail::CurrentException(ErrorCode::InvalidArgument, "Check the model transform and import settings.");
        error.SourcePath = path;
        return Result<ObjectId>::Failure(std::move(error));
    }
}

Result<void> Viewer::RemoveModel(ObjectId id) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    if (!state_->World.GetObject(id).Model) { return Result<void>::Failure({ErrorCode::InvalidHandle, "Object has no model attached.", "Remove empty hierarchy groups through GetScene().RemoveObject().", {}}); }
    try { state_->World.RemoveObject(id); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Use a live model ID.")); }
}

Result<void> Viewer::SetTransform(ObjectId id, const Transform& transform) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.SetTransform(id,transform); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Use finite TRS values, nonzero scale and a nonzero quaternion.")); }
}

Result<void> Viewer::SetVisible(ObjectId id, bool visible) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    state_->World.SetVisible(id,visible);
    return Result<void>::Success();
}

Result<void> Viewer::SetCamera(const Camera& camera) {
    auto ready = Ready(*state_); if (!ready) { return ready; }
    try {
        (void)camera.ViewProjection(state_->Aspect());
        state_->World.MainCamera = camera;
        state_->AutoFrame = false;
        state_->Controls.Reset();
        return Result<void>::Success();
    } catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Use a valid perspective camera and nonparallel view/up directions.")); }
}

Result<void> Viewer::SetLight(const DirectionalLight& light) {
    auto ready = Ready(*state_); if (!ready) { return ready; }
    try { light.Validate(); state_->World.Sun = light; return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Use a nonzero direction and finite, nonnegative light parameters.")); }
}

Result<void> Viewer::FrameScene() {
    auto ready = Ready(*state_); if (!ready) { return ready; }
    if (state_->World.CollectRenderItems().empty()) {
        return Result<void>::Failure({ErrorCode::InvalidState, "There are no visible models to frame.", "Add a model and make its parent hierarchy visible.", {}});
    }
    try {
        state_->World.MainCamera = detail::FitSceneCamera(state_->World,state_->Aspect());
        state_->AutoFrame = state_->Options.AutoFrame;
        state_->Controls.Reset();
        return Result<void>::Success();
    } catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Check visible Scene transforms and camera projection settings.")); }
}

Result<void> Viewer::ProcessInput(const ViewerInputEvent& event) {
    auto ready = Ready(*state_); if (!ready) { return ready; }
    if (state_->DispatchingInput) { return Result<void>::Failure({ErrorCode::InvalidState,"Recursive input dispatch is not supported.","Change the camera or Scene directly inside OnInput instead of redispatching input.",{}}); }
    if (!std::isfinite(event.DeltaX) || !std::isfinite(event.DeltaY) || !std::isfinite(event.ScrollY)) {
        return Result<void>::Failure({ErrorCode::InvalidArgument, "Input deltas must be finite.", "Send logical pixel deltas and a finite wheel delta.", {}});
    }
    if (event.Type == ViewerInputType::Resized) {
        if (event.Width < 0 || event.Height < 0) { return Result<void>::Failure({ErrorCode::InvalidArgument, "Viewport dimensions cannot be negative.", "Use logical window dimensions; zero is allowed for a minimized window.", {}}); }
        state_->Width = event.Width; state_->Height = event.Height;
    }
    if (event.Type == ViewerInputType::FocusLost) { state_->Controls.Reset(); }
    if (state_->ActiveRun && state_->ActiveRun->OnInput) {
        struct ResetDispatch { bool& Flag; ~ResetDispatch() { Flag = false; } } reset{state_->DispatchingInput};
        state_->DispatchingInput = true;
        try { if (state_->ActiveRun->OnInput(*this,event)) { return Result<void>::Success(); } }
        catch (const std::exception& error) { return Result<void>::Failure({ErrorCode::CallbackFailure,error.what(),"Handle exceptions inside OnInput.",{}}); }
        catch (...) { return Result<void>::Failure({ErrorCode::CallbackFailure,"OnInput threw a non-standard exception.","Handle exceptions inside OnInput.",{}}); }
    }
    if (event.Type == ViewerInputType::Resized) {
        if (event.Width > 0 && event.Height > 0 && state_->AutoFrame && !state_->World.CollectRenderItems().empty()) { return FrameScene(); }
        return Result<void>::Success();
    }
    if (event.Type == ViewerInputType::FocusLost) { return Result<void>::Success(); }
    if (!state_->Options.EnableDefaultInput) { return Result<void>::Success(); }
    if (event.Type == ViewerInputType::KeyDown && !event.Repeat) {
        if (event.Key == ViewerKey::Escape) { RequestClose(); }
        if (event.Key == ViewerKey::F) { return FrameScene(); }
    }
    try {
        if (state_->Controls.Process(state_->World.MainCamera,event,state_->Options.CameraControls,
            state_->Height > 0 ? state_->Height : state_->Options.Height)) { state_->AutoFrame = false; }
        return Result<void>::Success();
    } catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Check the input viewport size and current camera.")); }
}

Result<void> Viewer::Run(const ViewerRunOptions& options) {
    auto ready = Ready(*state_); if (!ready) { return ready; }
    if (state_->Running || state_->HasRun) {
        return Result<void>::Failure({ErrorCode::InvalidState, "This Viewer has already entered its standalone event loop.", "Use one standalone Run() session per process.", {}});
    }
    try {
        (void)state_->World.MainCamera.ViewProjection(state_->Aspect());
        state_->World.Sun.Validate();
        // Keep callbacks and limits alive/stable even if the caller edits its
        // original parameter object while the blocking session is running.
        const auto snapshot = options;
        return detail::RunStandalone(*this,*state_,snapshot);
    } catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument, "Check the Scene camera, light and ViewerOptions.")); }
}

void Viewer::RequestClose() noexcept { if (state_->Running.load()) { state_->CloseRequested.store(true); } }
bool Viewer::IsRunning() const noexcept { return state_->Running.load(); }

} // namespace lv
