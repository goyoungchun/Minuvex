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
template<typename Function> Result<void> AnimationEdit(Scene& world, bool& dirty, ObjectId object, Function function) {
    return Edit([&] { function(world.GetAnimation(object)); dirty = true; });
}
}
void EditorDocument::New() {
    world_ = Scene{}; library_.clear(); registry_.Clear(); path_.clear(); selection_ = NoObject; dirty_ = false;
}
Result<void> EditorDocument::Open(const std::filesystem::path& path) {
    auto absolute = Edit([&] { return std::filesystem::absolute(path).lexically_normal(); });
    if (!absolute) { return Result<void>::Failure(absolute.Error()); }
    AssetRegistry registry;
    auto loaded = LoadScene(path,registry);
    if (!loaded) { return Result<void>::Failure(loaded.Error()); }
    world_ = std::move(loaded.Value().World); library_ = std::move(loaded.Value().Assets);
    registry_ = std::move(registry); path_ = std::move(absolute).Value(); selection_ = NoObject; dirty_ = false;
    return Result<void>::Success();
}
Result<void> EditorDocument::Save(const std::filesystem::path& path) {
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
    if (saved) { path_ = std::move(absolute).Value(); dirty_ = false; }
    return saved;
}
Result<ObjectId> EditorDocument::OpenModel(const std::filesystem::path& path,const ImportSettings& settings,float aspect) {
    static_assert(std::is_nothrow_move_assignable_v<EditorDocument>);
    auto result = Edit([&] {
        EditorDocument next;
        const auto model = next.registry_.LoadModel(path,settings);
        next.library_.push_back(model);
        const auto id = next.world_.AddModel(model);
        next.world_.MainCamera = detail::FitSceneCamera(next.world_,aspect);
        next.selection_ = id; next.dirty_ = true;
        *this = std::move(next);
        return id;
    });
    if (!result) {auto error=result.Error();error.SourcePath=path;return Result<ObjectId>::Failure(std::move(error));}
    return result;
}
Result<AssetId> EditorDocument::ImportModel(const std::filesystem::path& path, const ImportSettings& settings) {
    auto result = Edit([&] {
        auto model = registry_.LoadModel(path,settings);
        if (std::find(library_.begin(),library_.end(),model) == library_.end()) { library_.push_back(model); dirty_ = true; }
        return model->Id;
    });
    if (!result) { auto error = result.Error(); error.SourcePath = path; return Result<AssetId>::Failure(std::move(error)); }
    return result;
}
Result<ObjectId> EditorDocument::PlaceModel(AssetId asset, const Transform& transform, ObjectId parent) {
    return Edit([&] {
        auto model = registry_.Find(asset);
        if (!model) { throw std::invalid_argument("Asset is not imported in this document."); }
        const auto id = world_.AddModel(std::move(model),transform,parent); selection_ = id; dirty_ = true; return id;
    });
}
Result<void> EditorDocument::ReimportModel(AssetId asset) {
    auto previous = registry_.Find(asset);
    if (!previous) {return Result<void>::Failure({ErrorCode::InvalidHandle,"Asset is not imported in this document.","Use an Asset ID from the imported library.",{}});}
    auto result = Edit([&] {
        auto registry = registry_;
        auto replacement = registry.ReimportModel(asset);
        auto library = library_;
        for (auto& model : library) {if (model == previous) {model = replacement;}}
        (void)world_.ReplaceModel(previous,replacement);
        registry_ = std::move(registry); library_.swap(library); dirty_ = true;
    });
    if (!result) {auto error = result.Error(); error.SourcePath = previous->SourcePath;return Result<void>::Failure(std::move(error));}
    return result;
}
Result<ObjectId> EditorDocument::CreateObject(std::string name, ObjectId parent) {
    return Edit([&] { const auto id = world_.CreateObject(std::move(name),parent); selection_ = id; dirty_ = true; return id; });
}
Result<void> EditorDocument::Select(ObjectId object) {
    return Edit([&] { if (object) { (void)world_.GetObject(object); } selection_ = object; });
}
Result<void> EditorDocument::RemoveObject(ObjectId object) {
    return Edit([&] { world_.RemoveObject(object); if (!world_.Contains(selection_)) { selection_ = NoObject; } dirty_ = true; });
}
Result<void> EditorDocument::RenameObject(ObjectId object, std::string name) {
    return Edit([&] { world_.SetName(object,std::move(name)); dirty_ = true; });
}
Result<void> EditorDocument::SetParent(ObjectId object, ObjectId parent) {
    return Edit([&] { world_.SetParent(object,parent); dirty_ = true; });
}
Result<void> EditorDocument::SetTransform(ObjectId object, const Transform& transform) {
    return Edit([&] { world_.SetTransform(object,transform); dirty_ = true; });
}
Result<void> EditorDocument::SetVisible(ObjectId object, bool visible) {
    return Edit([&] { world_.SetVisible(object,visible); dirty_ = true; });
}
Result<void> EditorDocument::SetCamera(const Camera& camera) {
    return Edit([&] { (void)camera.ViewProjection(1); world_.MainCamera = camera; dirty_ = true; });
}
Result<void> EditorDocument::SetLight(const DirectionalLight& light) {
    return Edit([&] { light.Validate(); world_.Sun = light; dirty_ = true; });
}
Result<void> EditorDocument::SetLocalLights(std::vector<PointLight> points,std::vector<SpotLight> spots) {
    return Edit([&] {if (points.size()+spots.size() > MaxLocalLights) {throw std::invalid_argument("At most 8 local lights are supported.");}for (const auto& p:points) {p.Validate();}for (const auto& s:spots) {s.Validate();}world_.PointLights=std::move(points);world_.SpotLights=std::move(spots);dirty_=true;});
}
Result<void> EditorDocument::PlayAnimation(ObjectId object, std::size_t clip) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.Play(clip); }); }
Result<void> EditorDocument::PauseAnimation(ObjectId object) { return AnimationEdit(world_,dirty_,object,[](auto& p) { p.Pause(); }); }
Result<void> EditorDocument::ResumeAnimation(ObjectId object) { return AnimationEdit(world_,dirty_,object,[](auto& p) { p.Resume(); }); }
Result<void> EditorDocument::StopAnimation(ObjectId object) { return AnimationEdit(world_,dirty_,object,[](auto& p) { p.Stop(); }); }
Result<void> EditorDocument::SeekAnimation(ObjectId object, double seconds) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.Seek(seconds); }); }
Result<void> EditorDocument::SetAnimationSpeed(ObjectId object, double speed) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.SetSpeed(speed); }); }
Result<void> EditorDocument::SetAnimationLoop(ObjectId object, bool loop) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.SetLoop(loop); }); }
Result<void> EditorDocument::SetJointPose(ObjectId object, JointId joint, const Transform& local) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.SetJointOverride(joint,local); }); }
Result<void> EditorDocument::ClearJointPose(ObjectId object, JointId joint) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.ClearJointOverride(joint); }); }
Result<void> EditorDocument::ClearPoseOverrides(ObjectId object) { return AnimationEdit(world_,dirty_,object,[](auto& p) { p.ClearOverrides(); }); }
Result<void> EditorDocument::ResetPose(ObjectId object) { return AnimationEdit(world_,dirty_,object,[](auto& p) { p.ResetPose(); }); }
Result<void> EditorDocument::ApplyPose(ObjectId object, const PoseAsset& pose) { return AnimationEdit(world_,dirty_,object,[&](auto& p) { p.ApplyPose(pose); }); }
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
