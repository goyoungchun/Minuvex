#include "ViewerState.h"

#include <cmath>
#include <exception>
#include <algorithm>

namespace lv::detail {

Error CurrentException(ErrorCode fallback, const char* hint) {
    try { throw; }
    catch (const ImportError& error) {
        switch (error.Code) {
        case ImportErrorCode::Io: return {ErrorCode::AssetLoad, error.what(), "Check the model path and referenced resource files.", {}};
        case ImportErrorCode::InvalidData: return {ErrorCode::InvalidData, error.what(), "Validate the GLB/glTF, OBJ or FBX source and its dependencies.", {}};
        case ImportErrorCode::UnsupportedFeature: return {ErrorCode::UnsupportedFeature, error.what(), "Use supported GLB/glTF, OBJ or FBX mesh/material/animation features; inspect the import warnings.", {}};
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
    const auto quality=ExpandQuality(state.Options.Quality);if (!quality) {return Result<void>::Failure(quality.Error());}
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
Result<void> Viewer::SetQuality(const QualityProfile& profile) {
    auto ready=Ready(*state_);if (!ready) {return ready;}const auto expanded=ExpandQuality(profile);if (!expanded) {return Result<void>::Failure(expanded.Error());}
    if (state_->ApplyQuality) {return state_->ApplyQuality(profile);}
    QualityState next;next.Requested=profile;next.RequestedSettings=expanded.Value();state_->Quality=std::move(next);state_->Options.Quality=profile;return Result<void>::Success();
}
Result<QualityState> Viewer::GetQuality() const {auto ready=Ready(*state_);if (!ready) {return Result<QualityState>::Failure(ready.Error());}return Result<QualityState>::Success(state_->Quality);}
Result<QualityCapabilities> Viewer::GetQualityCapabilities() const {
    auto ready=Ready(*state_);if (!ready) {return Result<QualityCapabilities>::Failure(ready.Error());}
    if (!detail::GraphicsRuntimeAvailable()) {return Result<QualityCapabilities>::Failure({ErrorCode::UnsupportedFeature,"This build has no graphics runtime.","Use the renderer build to query device quality capabilities.",{}});}
    if (!state_->Running.load() || !state_->Quality.Capabilities.GraphicsAvailable) {return Result<QualityCapabilities>::Failure({ErrorCode::InvalidState,"Quality capabilities require an active graphics context.","Query inside OnFrame or inspect the staged requested profile before Run.",{}});}
    return Result<QualityCapabilities>::Success(state_->Quality.Capabilities);
}
Result<void> Viewer::RequestRedraw() {auto ready=Ready(*state_);if (!ready) {return ready;}if (state_->Invalidate) {state_->Invalidate();}return Result<void>::Success();}
Result<void> Viewer::SetDebugOptions(const DebugOptions& options) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {ValidateDebugOptions(options);if (state_->ApplyDebug) {auto r=state_->ApplyDebug(options);if (!r) {return r;}}state_->Debug=options;return Result<void>::Success();}catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Check debug options."));}
}
Result<DebugOptions> Viewer::GetDebugOptions() const {auto ready=Ready(*state_);if (!ready) {return Result<DebugOptions>::Failure(ready.Error());}return Result<DebugOptions>::Success(state_->Debug);}
Result<void> Viewer::SetDebugView(DebugView view) {auto options=GetDebugOptions();if (!options) {return Result<void>::Failure(options.Error());}options.Value().View=view;return SetDebugOptions(options.Value());}
Result<void> Viewer::SetShowSkeleton(bool show) {auto options=GetDebugOptions();if (!options) {return Result<void>::Failure(options.Error());}options.Value().ShowSkeleton=show;return SetDebugOptions(options.Value());}
Result<void> Viewer::SetDebugDraw(const DebugDraw& commands) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {DebugDraw copy=commands;if (state_->ApplyDebugDraw) {auto r=state_->ApplyDebugDraw(copy);if (!r) {return r;}}state_->DebugCommands=std::move(copy);return Result<void>::Success();}catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Reduce debug command count."));}
}
Result<RenderDebugInfo> Viewer::GetRenderDebugInfo() const {
    auto ready=Ready(*state_);if (!ready) {return Result<RenderDebugInfo>::Failure(ready.Error());}try {if (state_->ReadDebug) {return Result<RenderDebugInfo>::Success(state_->ReadDebug());}return Result<RenderDebugInfo>::Failure({detail::GraphicsRuntimeAvailable() ? ErrorCode::InvalidState:ErrorCode::UnsupportedFeature,"Render inspection requires an active graphics session.","Query on the owner thread during OnFrame.",{}});}catch (...) {return Result<RenderDebugInfo>::Failure(detail::CurrentException(ErrorCode::Rendering,"Cannot inspect renderer resources."));}
}
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
Result<AssetLoadId> Viewer::AddModelAsync(const std::filesystem::path& path,const ModelOptions& options) {
    auto ready=Ready(*state_);if (!ready) {return Result<AssetLoadId>::Failure(ready.Error());}try {(void)options.Transform.Matrix().NormalMatrix();if (!state_->Loader) {state_->Loader=std::make_unique<AssetLoader>();}auto submitted=state_->Loader->Submit(path,options.Import,state_->Assets.Importers());if (submitted) {try {state_->PendingModels.emplace(submitted.Value(),options);}catch (...) {(void)state_->Loader->Cancel(submitted.Value());(void)state_->Loader->Forget(submitted.Value());throw;}}return submitted;}catch (...) {return Result<AssetLoadId>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Check async import options."));}
}
Result<AssetLoadSnapshot> Viewer::GetAssetLoad(AssetLoadId id) const {auto ready=Ready(*state_);if (!ready) {return Result<AssetLoadSnapshot>::Failure(ready.Error());}if (!state_->Loader || !state_->PendingModels.contains(id)) {return Result<AssetLoadSnapshot>::Failure({ErrorCode::InvalidHandle,"Unknown Viewer asset request.","Use a pending AddModelAsync ID.",{}});}return state_->Loader->Poll(id);}
Result<void> Viewer::CancelModelLoad(AssetLoadId id) {auto job=GetAssetLoad(id);if (!job) {return Result<void>::Failure(job.Error());}(void)state_->Loader->Cancel(id);(void)state_->Loader->Forget(id);state_->PendingModels.erase(id);return Result<void>::Success();}
Result<ObjectId> Viewer::CommitModelLoad(AssetLoadId job) {
    auto status=GetAssetLoad(job);if (!status) {return Result<ObjectId>::Failure(status.Error());}if (status.Value().Failure) {return Result<ObjectId>::Failure(*status.Value().Failure);}if (status.Value().State != AssetLoadState::Ready) {return Result<ObjectId>::Failure({ErrorCode::InvalidState,"Asset request is not ready.","Poll for Ready before committing.",{}});}
    try {const auto& options=state_->PendingModels.at(job);auto registry=state_->Assets;const auto model=registry.AddPrepared(status.Value().Model);const auto object=state_->World.AddModel(model,options.Transform,options.Parent);if (state_->AutoFrame) {const auto fit=FrameScene();if (!fit) {state_->World.RemoveObject(object);return Result<ObjectId>::Failure(fit.Error());}}state_->Assets=std::move(registry);(void)state_->Loader->Forget(job);state_->PendingModels.erase(job);if (state_->Invalidate) {state_->Invalidate();}return Result<ObjectId>::Success(object);}catch (...) {return Result<ObjectId>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Cannot publish the prepared model; the request remains available."));}
}
Result<void> Viewer::SetHotReload(bool enabled) {auto ready=Ready(*state_);if (!ready) {return ready;}if (state_->HotReload == enabled) {return Result<void>::Success();}state_->HotReload=enabled;state_->Watcher.Clear();if (!enabled && state_->Loader) {for (const auto& r:state_->Reloads) {(void)state_->Loader->Cancel(r.Job);(void)state_->Loader->Forget(r.Job);}state_->Reloads.clear();}if (enabled) {state_->Watcher.Synchronize(state_->Assets.Models());}return Result<void>::Success();}
Result<std::optional<Error>> Viewer::GetAssetLoadError() const {auto ready=Ready(*state_);if (!ready) {return Result<std::optional<Error>>::Failure(ready.Error());}return Result<std::optional<Error>>::Success(state_->LastAssetError);}
Result<std::size_t> Viewer::PollHotReload(double now) {
    auto ready=Ready(*state_);if (!ready) {return Result<std::size_t>::Failure(ready.Error());}std::size_t count=0;if (!state_->HotReload) {return Result<std::size_t>::Success(0);}
    try {state_->Watcher.Synchronize(state_->Assets.Models());std::vector<ModelHandle> resubmit;
        const auto submit=[&](ModelHandle model) {if (!state_->Loader) {state_->Loader=std::make_unique<AssetLoader>();}auto r=state_->Loader->Submit(model->SourcePath,model->Settings,state_->Assets.Importers());if (!r) {state_->LastAssetError=r.Error();return;}state_->Reloads.push_back({r.Value(),std::move(model)});};
        for (auto id:state_->Watcher.Poll(now)) {auto it=std::find_if(state_->Reloads.begin(),state_->Reloads.end(),[&](const auto& r) {return r.Before->Id == id;});if (it != state_->Reloads.end()) {it->Again=true;}else {submit(state_->Assets.Find(id));}}
        for (std::size_t i=0;i<state_->Reloads.size();) {auto r=state_->Reloads[i];const auto job=state_->Loader->Poll(r.Job);if (!job) {throw std::runtime_error(job.Error().Message);}if (job.Value().State == AssetLoadState::Queued || job.Value().State == AssetLoadState::Importing) {++i;continue;}
            if (state_->Assets.Find(r.Before->Id) == r.Before) {if (job.Value().State == AssetLoadState::Ready) {try {auto registry=state_->Assets;const auto model=registry.ReplacePrepared(r.Before->Id,job.Value().Model);(void)state_->World.ReplaceModel(r.Before,model);state_->Assets=std::move(registry);++count;state_->LastAssetError.reset();if (state_->Invalidate) {state_->Invalidate();}}catch (...) {state_->LastAssetError=detail::CurrentException(ErrorCode::InvalidData,"Hot Reload retained the previous compatible asset.");}}else if (job.Value().Failure) {state_->LastAssetError=job.Value().Failure;}}
            (void)state_->Loader->Forget(r.Job);state_->Reloads.erase(state_->Reloads.begin()+i);if (r.Again) {const auto current=state_->Assets.Find(r.Before->Id);if (current) {resubmit.push_back(current);}}
        }for (auto& model:resubmit) {submit(model);}return Result<std::size_t>::Success(count);
    }catch (...) {state_->LastAssetError=detail::CurrentException(ErrorCode::InvalidData,"Hot Reload retained previous assets.");return Result<std::size_t>::Failure(*state_->LastAssetError);}
}

Result<void> Viewer::ReimportModel(ObjectId id) {
    auto ready = ObjectReady(*state_,id); if (!ready) {return ready;}
    const auto previous = state_->World.GetObject(id).Model;
    if (!previous || state_->Assets.Find(previous->Id) != previous) {return Result<void>::Failure({ErrorCode::InvalidHandle,"Model is not managed by this Viewer's AssetRegistry.","Use an instance created with AddModel.",{}});}
    try {
        auto registry = state_->Assets;
        auto replacement = registry.ReimportModel(previous->Id);
        (void)state_->World.ReplaceModel(previous,replacement);
        state_->Assets = std::move(registry);
        return Result<void>::Success();
    }catch (...) {auto error = detail::CurrentException(ErrorCode::InvalidArgument,"Restore compatible skeleton/clip data or clear the affected animation and pose before reimporting.");error.SourcePath = previous->SourcePath;return Result<void>::Failure(std::move(error));}
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

Result<void> Viewer::PlayAnimation(ObjectId id, std::size_t clip) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.GetAnimation(id).Play(clip); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use an animation clip from this model.")); }
}
Result<void> Viewer::CrossFadeAnimation(ObjectId id,std::size_t clip,double duration) {
    auto ready=Ready(*state_);if (!ready) {return ready;}
    try {state_->World.GetAnimation(id).CrossFade(clip,duration);return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a valid animation clip, nonnegative duration and compatible scale signs."));}
}
Result<void> Viewer::PlayBlendTree(ObjectId id,BlendTreeHandle tree,bool restart) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.GetAnimation(id).PlayBlendTree(std::move(tree),restart);return Result<void>::Success();}catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a Blend Tree compatible with this model."));}
}
Result<void> Viewer::SetBlendParameter(ObjectId id,std::string_view name,float value) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.GetAnimation(id).SetBlendParameter(name,value);return Result<void>::Success();}catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a tree parameter name and finite value."));}
}
Result<void> Viewer::SeekBlendTree(ObjectId id,double phase) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.GetAnimation(id).SeekBlendTree(phase);return Result<void>::Success();}catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a normalized tree phase in [0,1]."));}
}
Result<void> Viewer::SetMaterial(ObjectId id,std::size_t index,const MaterialAsset& material) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.SetMaterial(id,index,material);return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a valid instance material and model-local textures."));}
}
Result<void> Viewer::ResetMaterial(ObjectId id,std::size_t index) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.ResetMaterial(id,index);return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a valid instance material index."));}
}
Result<void> Viewer::SetMorphWeights(ObjectId id,JointId node,std::span<const float> weights) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.GetAnimation(id).SetMorphWeights(node,weights);return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use the mesh node's target count and finite weights in [-100,100]."));}
}
Result<void> Viewer::ClearMorphWeights(ObjectId id,JointId node) {
    auto ready=Ready(*state_);if (!ready) {return ready;}try {state_->World.GetAnimation(id).ClearMorphWeights(node);return Result<void>::Success();}
    catch (...) {return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a morph mesh node in this instance."));}
}
Result<void> Viewer::PauseAnimation(ObjectId id) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.GetAnimation(id).Pause(); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a model instance.")); }
}
Result<void> Viewer::StopAnimation(ObjectId id) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.GetAnimation(id).Stop(); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a model instance.")); }
}
Result<void> Viewer::SetPose(ObjectId id, const PoseAsset& pose) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.GetAnimation(id).ApplyPose(pose); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a compatible pose and finite nonsingular TRS.")); }
}
Result<void> Viewer::ResetPose(ObjectId id) {
    auto ready = ObjectReady(*state_,id); if (!ready) { return ready; }
    try { state_->World.GetAnimation(id).ResetPose(); return Result<void>::Success(); }
    catch (...) { return Result<void>::Failure(detail::CurrentException(ErrorCode::InvalidArgument,"Use a model instance.")); }
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
Result<TelemetrySnapshot> Viewer::GetTelemetry() const {
    if (std::this_thread::get_id() != state_->Owner) {return Result<TelemetrySnapshot>::Failure({ErrorCode::WrongThread,"Read telemetry on the Viewer owner thread.","Copy a snapshot before sharing it with other threads.",{}});}
    try {
        if (state_->ReadTelemetry) {return Result<TelemetrySnapshot>::Success(state_->ReadTelemetry());}
        if (state_->LastTelemetry) {return Result<TelemetrySnapshot>::Success(*state_->LastTelemetry);}
        return Result<TelemetrySnapshot>::Failure({detail::GraphicsRuntimeAvailable() ? ErrorCode::InvalidState:ErrorCode::UnsupportedFeature,"No graphics telemetry session is available.","Use SystemInfo/SystemMonitor without graphics, or query during/after Viewer::Run.",{}});
    } catch (...) {return Result<TelemetrySnapshot>::Failure(detail::CurrentException(ErrorCode::Rendering,"Cannot collect the telemetry snapshot."));}
}
bool Viewer::IsRunning() const noexcept { return state_->Running.load(); }

} // namespace lv
