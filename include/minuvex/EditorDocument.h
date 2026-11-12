#pragma once

#include <minuvex/SceneIO.h>
#include <minuvex/PoseIO.h>

namespace lv {

// CPU editing model used by the Editor UI; no window, ImGui or graphics dependency.
// Single caller thread, references remain valid until New/Open/OpenModel or mutation.
class EditorDocument final {
public:
    const Scene& GetScene() const noexcept { return world_; }
    const std::vector<ModelHandle>& Assets() const noexcept { return library_; }
    const std::filesystem::path& Path() const noexcept { return path_; }
    ObjectId Selection() const noexcept { return selection_; }
    bool IsDirty() const noexcept { return dirty_; }
    void New(); // Caller handles unsaved-change confirmation.
    Result<void> Open(const std::filesystem::path& path); // Transactional replacement.
    Result<ObjectId> OpenModel(const std::filesystem::path& path, const ImportSettings& settings = {}, float aspect = 1);
    Result<ModelHandle> PreviewModel(const std::filesystem::path& path,const ImportSettings& settings={}) const;
    Result<ObjectId> OpenPreparedModel(ModelHandle model,float aspect=1);
    Result<AssetId> CommitImport(ModelHandle model,bool place=false); // Commit only after Preview approval.
    // Quick Open: one model in an unsaved Scene with fitted camera. Failure preserves the document.
    Result<void> Save(const std::filesystem::path& path); // Save As; protects source resources, path/dirty change only on success.
    Result<AssetId> ImportModel(const std::filesystem::path& path, const ImportSettings& settings = {});
    Result<void> ReimportModel(AssetId asset); // Atomically replace the library and all compatible instances.
    Result<void> CommitReimport(AssetId asset,ModelHandle candidate); // Prepared same-source candidate; transactional Scene/library replacement.
    Result<ObjectId> PlaceModel(AssetId asset, const Transform& transform = {}, ObjectId parent = NoObject);
    Result<ObjectId> CreateObject(std::string name = "Object", ObjectId parent = NoObject);
    Result<void> Select(ObjectId object); // NoObject clears selection, does not mark dirty.
    Result<void> RemoveObject(ObjectId object); // Entire subtree; clears removed selection.
    Result<void> RenameObject(ObjectId object, std::string name);
    Result<void> SetParent(ObjectId object, ObjectId parent); // Keeps local TRS.
    Result<void> SetTransform(ObjectId object, const Transform& transform);
    Result<void> SetVisible(ObjectId object, bool visible);
    Result<void> SetCamera(const Camera& camera);
    Result<void> SetLight(const DirectionalLight& light);
    Result<void> SetLocalLights(std::vector<PointLight> points,std::vector<SpotLight> spots);
    Result<void> PlayAnimation(ObjectId object, std::size_t clip);
    Result<void> PauseAnimation(ObjectId object);
    Result<void> ResumeAnimation(ObjectId object);
    Result<void> StopAnimation(ObjectId object);
    Result<void> SeekAnimation(ObjectId object, double seconds);
    Result<void> SetAnimationSpeed(ObjectId object, double speed);
    Result<void> SetAnimationLoop(ObjectId object, bool loop);
    Result<void> SetJointPose(ObjectId object, JointId joint, const Transform& local);
    Result<void> ClearJointPose(ObjectId object, JointId joint);
    Result<void> ClearPoseOverrides(ObjectId object);
    Result<void> ResetPose(ObjectId object);
    Result<void> ApplyPose(ObjectId object, const PoseAsset& pose);
    Result<void> SavePoseFile(ObjectId object, const std::filesystem::path& path, bool overridesOnly = false) const;
    Result<void> LoadPoseFile(ObjectId object, const std::filesystem::path& path);
    Result<void> UpdateAnimations(double deltaSeconds); // Preview tick; does not mark dirty.

private:
    AssetRegistry registry_;
    Scene world_;
    std::vector<ModelHandle> library_;
    std::filesystem::path path_;
    ObjectId selection_ = NoObject;
    bool dirty_ = false;
};

} // namespace lv
