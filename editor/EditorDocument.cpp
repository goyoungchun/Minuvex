#include <minuvex/EditorDocument.h>

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
    auto saved = SaveScene(world_,path,library_);
    if (saved) { path_ = std::move(absolute).Value(); dirty_ = false; }
    return saved;
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
} // namespace lv
