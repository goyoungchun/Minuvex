#pragma once

#include <minvex/SceneIO.h>
#include <minvex/PoseIO.h>

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
    bool CanUndo() const noexcept { return !pendingEdit_ && !undo_.empty(); }
    bool CanRedo() const noexcept { return !pendingEdit_ && !redo_.empty(); }
    bool IsEditing() const noexcept { return bool(pendingEdit_); }
    Result<void> Undo();
    Result<void> Redo();
    Result<void> ClearUndoHistory(); // Release retained snapshots; preserve Scene/savepoint/dirty.
    Result<void> BeginEdit(); // Group multiple successful mutations into one history step.
    Result<void> CommitEdit();
    Result<void> CancelEdit(); // Restore the complete pre-edit CPU state.
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
    Result<void> SetWorldTransform(ObjectId object, const Mat4& world); // Reject local shear.
    Result<void> SetVisible(ObjectId object, bool visible);
    Result<void> SetMaterial(ObjectId object, std::size_t index, const MaterialAsset& material);
    Result<void> ResetMaterial(ObjectId object, std::size_t index);
    Result<void> SetMorphWeights(ObjectId object, JointId node, std::span<const float> weights);
    Result<void> ClearMorphWeights(ObjectId object, JointId node);
    Result<void> SetCamera(const Camera& camera);
    Result<void> SetLight(const DirectionalLight& light);
    Result<void> SetLocalLights(std::vector<PointLight> points,std::vector<SpotLight> spots);
    Result<void> PlayAnimation(ObjectId object, std::size_t clip);
    Result<void> CrossFadeAnimation(ObjectId object, std::size_t clip, double durationSeconds = .3);
    Result<void> PlayBlendTree(ObjectId object, BlendTreeHandle tree, bool restart = true);
    Result<void> SetBlendParameter(ObjectId object, std::string_view name, float value);
    Result<void> SeekBlendTree(ObjectId object, double phase, AnimationSeekEvents events = AnimationSeekEvents::Silent);
    Result<void> SetAnimationEvents(ObjectId object, std::size_t clip, std::span<const AnimationEvent> events);
    Result<void> ResetAnimationEvents(ObjectId object, std::size_t clip);
    Result<std::vector<SceneAnimationEvent>> PollAnimationEvents();
    Result<std::size_t> DispatchAnimationEvents(std::function<void(const SceneAnimationEvent&)> callback);
    Result<void> SetAdditiveAnimation(ObjectId object, std::size_t clip, float weight = 1, const PoseAsset& reference = {}, std::span<const float> jointMask = {});
    Result<void> RestoreAdditiveAnimation(ObjectId object, const AdditiveAnimationState& state);
    Result<void> SetAdditiveWeight(ObjectId object, float weight);
    Result<void> SetAdditiveMask(ObjectId object, std::span<const float> jointMask);
    Result<void> SeekAdditive(ObjectId object, double seconds);
    Result<void> ClearAdditiveAnimation(ObjectId object);
    Result<void> PauseAnimation(ObjectId object);
    Result<void> ResumeAnimation(ObjectId object);
    Result<void> StopAnimation(ObjectId object);
    Result<void> SeekAnimation(ObjectId object, double seconds, AnimationSeekEvents events = AnimationSeekEvents::Silent);
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
    struct Snapshot {
        AssetRegistry Registry;
        Scene World;
        std::vector<ModelHandle> Library;
        ObjectId Selection=NoObject;
        std::uint64_t Revision=0;
    };
    std::vector<std::unique_ptr<Snapshot>> undo_,redo_;
    std::unique_ptr<Snapshot> pendingEdit_;
    std::uint64_t revision_=0,savedRevision_=0,nextRevision_=0;
    std::unique_ptr<Snapshot> Capture() const;
    void Restore(std::unique_ptr<Snapshot> snapshot) noexcept;
    void MarkChanged() noexcept {revision_=++nextRevision_;dirty_=true;}
    void KeepUndo(std::unique_ptr<Snapshot> snapshot) noexcept;
    void ClearHistory() noexcept;
    template<class Function> auto Change(Function function) -> Result<decltype(function())>;
    template<class Function> Result<void> ChangeAnimation(ObjectId object,Function function);
};

} // namespace lv
