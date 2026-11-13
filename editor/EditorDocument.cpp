#include <minuvex/EditorDocument.h>
#include "CameraController.h"

#include <algorithm>

namespace lv {
namespace {
template<typename Function>
auto Edit(Function function) -> Result<decltype(function())> {
    using T = decltype(function());
    try {
        if constexpr (std::is_void_v<T>) { function(); return Result<void>::Success(); }
        else { return Result<T>::Success(function()); }
    } catch (const std::bad_alloc& e) {
        return Result<T>::Failure({ErrorCode::InsufficientResource,e.what(),"Reduce the document size and import budgets.",{}});
    } catch (const ImportError& e) {
        const auto code = e.Code == ImportErrorCode::UnsupportedFeature ? ErrorCode::UnsupportedFeature :
            e.Code == ImportErrorCode::ResourceLimit ? ErrorCode::InsufficientResource :
            e.Code == ImportErrorCode::InvalidData ? ErrorCode::InvalidData : ErrorCode::AssetLoad;
        return Result<T>::Failure({code,e.what(),"Check the model path, dependencies and supported import features.",{}});
    } catch (const std::invalid_argument& e) {
        return Result<T>::Failure({ErrorCode::InvalidArgument,e.what(),"Check the object/asset handle, parent and numeric values.",{}});
    } catch (const std::exception& e) {
        return Result<T>::Failure({ErrorCode::InvalidState,e.what(),"Check the document state and available resources.",{}});
    }
}
}
std::unique_ptr<EditorDocument::Snapshot> EditorDocument::Capture() const {
    auto s=std::make_unique<Snapshot>();s->Registry=registry_;s->Library=library_;s->Selection=selection_;s->Revision=revision_;
    s->World.objects_=world_.objects_;s->World.order_=world_.order_;s->World.MainCamera=world_.MainCamera;s->World.Sun=world_.Sun;s->World.PointLights=world_.PointLights;s->World.SpotLights=world_.SpotLights;return s;
}
void EditorDocument::Restore(std::unique_ptr<Snapshot> s) noexcept {
    registry_=std::move(s->Registry);world_=std::move(s->World);library_=std::move(s->Library);selection_=s->Selection;revision_=s->Revision;dirty_=revision_ != savedRevision_;
}
void EditorDocument::KeepUndo(std::unique_ptr<Snapshot> s) noexcept {
    undo_.push_back(std::move(s));if (undo_.size()>32) {undo_.erase(undo_.begin());}redo_.clear();
}
void EditorDocument::ClearHistory() noexcept {undo_.clear();redo_.clear();pendingEdit_.reset();revision_=savedRevision_=nextRevision_=0;}
template<class Function> auto EditorDocument::Change(Function function) -> Result<decltype(function())> {
    using T=decltype(function());
    if (pendingEdit_) {auto result=Edit(function);if (!result) {(void)CancelEdit();}return result;}
    return Edit([&]() -> T {
        auto before=Capture();undo_.reserve(undo_.size()+1);
        try {
            if constexpr (std::is_void_v<T>) {function();if (revision_ != before->Revision) {KeepUndo(std::move(before));}}
            else {auto result=function();if (revision_ != before->Revision) {KeepUndo(std::move(before));}return result;}
        }catch (...) {Restore(std::move(before));throw;}
    });
}
template<class Function> Result<void> EditorDocument::ChangeAnimation(ObjectId object,Function function) {return Change([&] {function(world_.GetAnimation(object));MarkChanged();});}
Result<void> EditorDocument::BeginEdit() {return Edit([&] {if (pendingEdit_) {throw std::logic_error("An edit group is already active.");}auto before=Capture();undo_.reserve(undo_.size()+1);pendingEdit_=std::move(before);});}
Result<void> EditorDocument::CommitEdit() {return Edit([&] {if (!pendingEdit_) {throw std::logic_error("No edit group is active.");}if (revision_ != pendingEdit_->Revision) {KeepUndo(std::move(pendingEdit_));}pendingEdit_.reset();});}
Result<void> EditorDocument::CancelEdit() {return Edit([&] {if (!pendingEdit_) {throw std::logic_error("No edit group is active.");}Restore(std::move(pendingEdit_));});}
Result<void> EditorDocument::Undo() {return Edit([&] {if (!CanUndo()) {throw std::logic_error("No committed edit is available to undo.");}auto current=Capture();redo_.reserve(redo_.size()+1);redo_.push_back(std::move(current));auto before=std::move(undo_.back());undo_.pop_back();Restore(std::move(before));});}
Result<void> EditorDocument::Redo() {return Edit([&] {if (!CanRedo()) {throw std::logic_error("No committed edit is available to redo.");}auto current=Capture();undo_.reserve(undo_.size()+1);undo_.push_back(std::move(current));auto after=std::move(redo_.back());redo_.pop_back();Restore(std::move(after));});}
Result<void> EditorDocument::ClearUndoHistory() {return Edit([&] {if (pendingEdit_) {throw std::logic_error("Finish the active edit before clearing history.");}undo_.clear();redo_.clear();});}
void EditorDocument::New() {
    world_ = Scene{}; library_.clear(); registry_.Clear(); path_.clear(); selection_ = NoObject; dirty_ = false;
    ClearHistory();
}
Result<void> EditorDocument::Open(const std::filesystem::path& path) {
    auto absolute = Edit([&] { return std::filesystem::absolute(path).lexically_normal(); });
    if (!absolute) { return Result<void>::Failure(absolute.Error()); }
    AssetRegistry registry;
    auto loaded = LoadScene(path,registry);
    if (!loaded) { return Result<void>::Failure(loaded.Error()); }
    world_ = std::move(loaded.Value().World); library_ = std::move(loaded.Value().Assets);
    registry_ = std::move(registry); path_ = std::move(absolute).Value(); selection_ = NoObject; dirty_ = false;
    ClearHistory();
    return Result<void>::Success();
}
Result<void> EditorDocument::Save(const std::filesystem::path& path) {
    if (pendingEdit_) {return Result<void>::Failure({ErrorCode::InvalidState,"Finish or cancel the active edit before saving.","Release the gizmo or press Escape.",path});}
    auto absolute = Edit([&] { return std::filesystem::absolute(path).lexically_normal(); });
    if (!absolute) { return Result<void>::Failure(absolute.Error()); }
    for (const auto& model:library_) {
        for (const auto& source:model->Dependencies) {
            std::error_code error;
            if (absolute.Value() == source.lexically_normal() || std::filesystem::equivalent(absolute.Value(),source,error)) {
                return Result<void>::Failure({ErrorCode::InvalidArgument,"A Scene cannot overwrite one of its source assets/resources.","Choose a different Scene file name.",absolute.Value()});
            }
        }
    }
    auto saved = SaveScene(world_,path,library_);
    if (saved) { path_ = std::move(absolute).Value(); savedRevision_=revision_;dirty_ = false; }
    return saved;
}
Result<ObjectId> EditorDocument::OpenModel(const std::filesystem::path& path,const ImportSettings& settings,float aspect) {
    auto preview=PreviewModel(path,settings);if (!preview) {return Result<ObjectId>::Failure(preview.Error());}return OpenPreparedModel(preview.Value(),aspect);
}
Result<ModelHandle> EditorDocument::PreviewModel(const std::filesystem::path& path,const ImportSettings& settings) const {
    auto result=Edit([&] {return registry_.PrepareModel(path,settings);});
    if (!result) {auto error=result.Error();error.SourcePath=path;return Result<ModelHandle>::Failure(std::move(error));}return result;
}
Result<ObjectId> EditorDocument::OpenPreparedModel(ModelHandle model,float aspect) {
    static_assert(std::is_nothrow_move_assignable_v<EditorDocument>);
    auto result = Edit([&] {
        EditorDocument next;
        const auto registered=next.registry_.AddPrepared(model);
        next.library_.push_back(registered);
        const auto id = next.world_.AddModel(registered);
        next.world_.MainCamera = detail::FitSceneCamera(next.world_,aspect);
        next.selection_ = id; next.MarkChanged();
        *this = std::move(next);
        return id;
    });
    if (!result) {auto error=result.Error();if (model) {error.SourcePath=model->SourcePath;}return Result<ObjectId>::Failure(std::move(error));}
    return result;
}
Result<AssetId> EditorDocument::CommitImport(ModelHandle model,bool place) {
    auto result=Change([&] {
        auto registry=registry_;const auto imported=registry.AddPrepared(model);auto library=library_;
        const bool added=std::find(library.begin(),library.end(),imported) == library.end();if (added) {library.push_back(imported);}
        ObjectId instance=NoObject;if (place) {instance=world_.AddModel(imported);}
        registry_=std::move(registry);library_=std::move(library);if (place) {selection_=instance;}if (added || place) {MarkChanged();}return imported->Id;
    });
    if (!result) {auto error=result.Error();if (model) {error.SourcePath=model->SourcePath;}return Result<AssetId>::Failure(std::move(error));}return result;
}
Result<AssetId> EditorDocument::ImportModel(const std::filesystem::path& path, const ImportSettings& settings) {
    auto result = Change([&] {
        auto model = registry_.LoadModel(path,settings);
        if (std::find(library_.begin(),library_.end(),model) == library_.end()) { library_.push_back(model); MarkChanged(); }
        return model->Id;
    });
    if (!result) { auto error = result.Error(); error.SourcePath = path; return Result<AssetId>::Failure(std::move(error)); }
    return result;
}
Result<ObjectId> EditorDocument::PlaceModel(AssetId asset, const Transform& transform, ObjectId parent) {
    return Change([&] {
        auto model = registry_.Find(asset);
        if (!model) { throw std::invalid_argument("Asset is not imported in this document."); }
        const auto id = world_.AddModel(std::move(model),transform,parent); selection_ = id; MarkChanged(); return id;
    });
}
Result<void> EditorDocument::ReimportModel(AssetId asset) {
    auto previous = registry_.Find(asset);
    if (!previous) {return Result<void>::Failure({ErrorCode::InvalidHandle,"Asset is not imported in this document.","Use an Asset ID from the imported library.",{}});}
    auto result = Change([&] {
        auto registry = registry_;
        auto replacement = registry.ReimportModel(asset);
        auto library = library_;
        for (auto& model : library) {if (model == previous) {model = replacement;}}
        (void)world_.ReplaceModel(previous,replacement);
        registry_ = std::move(registry); library_.swap(library); MarkChanged();
    });
    if (!result) {auto error = result.Error(); error.SourcePath = previous->SourcePath;return Result<void>::Failure(std::move(error));}
    return result;
}
Result<ObjectId> EditorDocument::CreateObject(std::string name, ObjectId parent) {
    return Change([&] { const auto id = world_.CreateObject(std::move(name),parent); selection_ = id; MarkChanged(); return id; });
}
Result<void> EditorDocument::CommitReimport(AssetId asset,ModelHandle candidate) {
    const auto previous=registry_.Find(asset);if (!previous) {return Result<void>::Failure({ErrorCode::InvalidHandle,"Unknown imported asset.","Select an imported asset.",{}});}
    return Change([&] {auto registry=registry_;auto replacement=registry.ReplacePrepared(asset,std::move(candidate));auto library=library_;for (auto& model:library) {if (model == previous) {model=replacement;}}(void)world_.ReplaceModel(previous,replacement);registry_=std::move(registry);library_.swap(library);MarkChanged();});
}
Result<void> EditorDocument::Select(ObjectId object) {
    return Edit([&] { if (object) { (void)world_.GetObject(object); } selection_ = object; });
}
Result<void> EditorDocument::RemoveObject(ObjectId object) {
    return Change([&] { world_.RemoveObject(object); if (!world_.Contains(selection_)) { selection_ = NoObject; } MarkChanged(); });
}
Result<void> EditorDocument::RenameObject(ObjectId object, std::string name) {
    return Change([&] { world_.SetName(object,std::move(name)); MarkChanged(); });
}
Result<void> EditorDocument::SetParent(ObjectId object, ObjectId parent) {
    return Change([&] { world_.SetParent(object,parent); MarkChanged(); });
}
Result<void> EditorDocument::SetTransform(ObjectId object, const Transform& transform) {
    return Change([&] { world_.SetTransform(object,transform); MarkChanged(); });
}
Result<void> EditorDocument::SetWorldTransform(ObjectId object,const Mat4& world) {
    return Change([&] {const auto parent=world_.GetObject(object).Parent;const auto local=DecomposeTRS(parent ? world_.GetWorldTransform(parent).InverseAffine()*world:world);world_.SetTransform(object,local);MarkChanged();});
}
Result<void> EditorDocument::SetVisible(ObjectId object, bool visible) {
    return Change([&] { world_.SetVisible(object,visible); MarkChanged(); });
}
Result<void> EditorDocument::SetCamera(const Camera& camera) {
    return Change([&] { (void)camera.ViewProjection(1); world_.MainCamera = camera; MarkChanged(); });
}
Result<void> EditorDocument::SetLight(const DirectionalLight& light) {
    return Change([&] { light.Validate(); world_.Sun = light; MarkChanged(); });
}
Result<void> EditorDocument::SetLocalLights(std::vector<PointLight> points,std::vector<SpotLight> spots) {
    return Change([&] {if (points.size()+spots.size() > MaxLocalLights) {throw std::invalid_argument("At most 8 local lights are supported.");}for (const auto& p:points) {p.Validate();}for (const auto& s:spots) {s.Validate();}world_.PointLights=std::move(points);world_.SpotLights=std::move(spots);MarkChanged();});
}
Result<void> EditorDocument::PlayAnimation(ObjectId object, std::size_t clip) { return ChangeAnimation(object,[&](auto& p) { p.Play(clip); }); }
Result<void> EditorDocument::CrossFadeAnimation(ObjectId object,std::size_t clip,double duration) {return ChangeAnimation(object,[&](auto& p) {p.CrossFade(clip,duration);});}
Result<void> EditorDocument::SetMaterial(ObjectId object,std::size_t index,const MaterialAsset& material) {return Change([&] {world_.SetMaterial(object,index,material);MarkChanged();});}
Result<void> EditorDocument::ResetMaterial(ObjectId object,std::size_t index) {return Change([&] {world_.ResetMaterial(object,index);MarkChanged();});}
Result<void> EditorDocument::SetMorphWeights(ObjectId object,JointId node,std::span<const float> weights) {return ChangeAnimation(object,[&](auto& player){player.SetMorphWeights(node,weights);});}
Result<void> EditorDocument::ClearMorphWeights(ObjectId object,JointId node) {return ChangeAnimation(object,[&](auto& player){player.ClearMorphWeights(node);});}
Result<void> EditorDocument::PauseAnimation(ObjectId object) { return ChangeAnimation(object,[](auto& p) { p.Pause(); }); }
Result<void> EditorDocument::ResumeAnimation(ObjectId object) { return ChangeAnimation(object,[](auto& p) { p.Resume(); }); }
Result<void> EditorDocument::StopAnimation(ObjectId object) { return ChangeAnimation(object,[](auto& p) { p.Stop(); }); }
Result<void> EditorDocument::SeekAnimation(ObjectId object, double seconds) { return ChangeAnimation(object,[&](auto& p) { p.Seek(seconds); }); }
Result<void> EditorDocument::SetAnimationSpeed(ObjectId object, double speed) { return ChangeAnimation(object,[&](auto& p) { p.SetSpeed(speed); }); }
Result<void> EditorDocument::SetAnimationLoop(ObjectId object, bool loop) { return ChangeAnimation(object,[&](auto& p) { p.SetLoop(loop); }); }
Result<void> EditorDocument::SetJointPose(ObjectId object, JointId joint, const Transform& local) { return ChangeAnimation(object,[&](auto& p) { p.SetJointOverride(joint,local); }); }
Result<void> EditorDocument::ClearJointPose(ObjectId object, JointId joint) { return ChangeAnimation(object,[&](auto& p) { p.ClearJointOverride(joint); }); }
Result<void> EditorDocument::ClearPoseOverrides(ObjectId object) { return ChangeAnimation(object,[](auto& p) { p.ClearOverrides(); }); }
Result<void> EditorDocument::ResetPose(ObjectId object) { return ChangeAnimation(object,[](auto& p) { p.ResetPose(); }); }
Result<void> EditorDocument::ApplyPose(ObjectId object, const PoseAsset& pose) { return ChangeAnimation(object,[&](auto& p) { p.ApplyPose(pose); }); }
Result<void> EditorDocument::UpdateAnimations(double deltaSeconds) { return Edit([&] { world_.UpdateAnimations(deltaSeconds); }); }
Result<void> EditorDocument::SavePoseFile(ObjectId object, const std::filesystem::path& path, bool overridesOnly) const {
    auto captured = Edit([&] {
        const auto& instance = world_.GetObject(object);
        if (!instance.Model) { throw std::invalid_argument("Select a model instance to save a pose."); }
        return overridesOnly ? instance.Animation.Overrides() : instance.Animation.CapturePose("Saved Pose");
    });
    if (!captured) { return Result<void>::Failure(captured.Error()); }
    return lv::SavePose(captured.Value(),world_.GetObject(object).Model->Skeleton,path);
}
Result<void> EditorDocument::LoadPoseFile(ObjectId object, const std::filesystem::path& path) {
    auto selected = Edit([&] {
        const auto& instance = world_.GetObject(object);
        if (!instance.Model) { throw std::invalid_argument("Select a model instance to load a pose."); } return instance.Model;
    });
    if (!selected) { return Result<void>::Failure(selected.Error()); }
    auto loaded = lv::LoadPose(path,selected.Value()->Skeleton);
    if (!loaded) { return Result<void>::Failure(loaded.Error()); }
    return ApplyPose(object,loaded.Value());
}
} // namespace lv
