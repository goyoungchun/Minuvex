#pragma once

#include <minuvex/SceneIO.h>

namespace lv {

// CPU editing model used by the Editor UI; no window, ImGui or graphics dependency.
// Single caller thread, references remain valid until New/Open or mutation.
class EditorDocument final {
public:
    const Scene& GetScene() const noexcept { return world_; }
    const std::vector<ModelHandle>& Assets() const noexcept { return library_; }
    const std::filesystem::path& Path() const noexcept { return path_; }
    ObjectId Selection() const noexcept { return selection_; }
    bool IsDirty() const noexcept { return dirty_; }
    void New(); // Caller handles unsaved-change confirmation.
    Result<void> Open(const std::filesystem::path& path); // Transactional replacement.
    Result<void> Save(const std::filesystem::path& path); // Save As; path/dirty change only on success.
    Result<AssetId> ImportModel(const std::filesystem::path& path, const ImportSettings& settings = {});
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

private:
    AssetRegistry registry_;
    Scene world_;
    std::vector<ModelHandle> library_;
    std::filesystem::path path_;
    ObjectId selection_ = NoObject;
    bool dirty_ = false;
};

} // namespace lv
