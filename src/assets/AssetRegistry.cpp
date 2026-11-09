#include <minuvex/Asset.h>

#include "GltfImport.h"

#include <atomic>

namespace lv {
namespace {
std::atomic<AssetId> nextAssetId{1};
}

ModelHandle AssetRegistry::LoadModel(const std::filesystem::path& path, const ImportSettings& settings) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) { throw ImportError(ImportErrorCode::Io, "Cannot resolve model path: " + detail::PathText(path)); }
    for (const auto& model : models_) {
        if (model->Settings == settings && std::filesystem::equivalent(model->SourcePath, canonical, error) && !error) {
            return model;
        }
    }
    auto model = detail::ImportGltf(canonical, settings);
    model->Id = nextAssetId.fetch_add(1);
    models_.push_back(model); // Failed imports never enter the registry.
    return model;
}

ModelHandle AssetRegistry::Find(AssetId id) const noexcept {
    for (const auto& model : models_) { if (model->Id == id) { return model; } }
    return {};
}

} // namespace lv
