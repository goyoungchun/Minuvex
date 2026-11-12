#include <minuvex/Asset.h>
#include <minuvex/Optimization.h>

#include "ImportCommon.h"

#include <atomic>
#include <algorithm>
#include <cctype>

namespace lv {
namespace {
std::atomic<AssetId> nextAssetId{1};
std::string Extension(const std::filesystem::path& path) {
    auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) {return static_cast<char>(std::tolower(c));});return extension;
}
ImportSettings NormalizeSettings(ImportSettings settings) {if (!settings.TextureDirectory.empty()) {settings.TextureDirectory=std::filesystem::absolute(settings.TextureDirectory).lexically_normal();}return settings;}
}

ImporterRegistry::ImporterRegistry() {
    const auto gltf=[](const auto& path,const auto& settings) {return std::move(*detail::ImportGltf(path,settings));};
    Register(".gltf",gltf,AssetFormat::Gltf);Register(".glb",gltf,AssetFormat::Gltf);
    Register(".obj",[](const auto& path,const auto& settings) {return std::move(*detail::ImportObj(path,settings));},AssetFormat::Obj);
    Register(".fbx",[](const auto& path,const auto& settings) {return std::move(*detail::ImportFbx(path,settings));},AssetFormat::Fbx);
}
void ImporterRegistry::Register(std::string extension,ModelImporter importer,AssetFormat format) {
    if (extension.size()<2 || extension.front()!='.' || extension.find_first_of("/\\: \t\n",1)!=std::string::npos || !importer) {throw std::invalid_argument("Importer requires a file extension and callback.");}
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) {return static_cast<char>(std::tolower(c));});
    for (const auto& entry:entries_) {if (entry.Extension == extension) {throw std::invalid_argument("An importer is already registered for this extension.");}}
    entries_.push_back({std::move(extension),std::move(importer),format});
}
bool ImporterRegistry::Supports(const std::filesystem::path& path) const {const auto ext=Extension(path);return std::any_of(entries_.begin(),entries_.end(),[&](const auto& entry) {return entry.Extension == ext;});}
std::vector<std::string> ImporterRegistry::Extensions() const {std::vector<std::string> result;for (const auto& entry:entries_) {result.push_back(entry.Extension);}return result;}
ModelHandle ImporterRegistry::Import(const std::filesystem::path& path,const ImportSettings& settings) const {
    detail::ValidateImportSettings(settings);
    const auto ext=Extension(path);const Entry* selected=nullptr;for (const auto& entry:entries_) {if (entry.Extension == ext) {selected=&entry;break;}}
    if (!selected) {throw ImportError(ImportErrorCode::UnsupportedFeature,"No importer for extension: "+ext);}
    std::error_code ec;const auto canonical=std::filesystem::canonical(path,ec);if (ec) {throw ImportError(ImportErrorCode::Io,"Cannot resolve model path: "+detail::PathText(path));}
    try {
        const auto normalized=NormalizeSettings(settings);
        auto model=std::make_shared<ModelAsset>(selected->Importer(canonical,normalized));model->SourcePath=canonical;model->Settings=normalized;model->Format=selected->Format;
        if (std::find(model->Dependencies.begin(),model->Dependencies.end(),canonical) == model->Dependencies.end()) {model->Dependencies.insert(model->Dependencies.begin(),canonical);}
        ValidateModel(*model);
        auto info=InspectModel(*model);std::size_t remaining=normalized.MaxResourceBytes-info.GeometryBytes-info.ImageBytes;
        for (auto& mesh:model->Meshes) {for (auto& primitive:mesh.Primitives) {if (primitive.HasSkinning || !primitive.Lods.empty()) {continue;}auto levels=GenerateMeshLods(primitive.Geometry);std::size_t bytes=0;for (const auto& level:levels) {bytes+=level.Indices.size()*sizeof(std::uint32_t);}if (bytes<=remaining) {remaining-=bytes;primitive.Lods=std::move(levels);}else {model->Warnings.push_back("LOD generation exceeds the remaining resource budget; retained LOD0.");}}}
        ValidateModel(*model);model->Status=model->Warnings.empty() ? AssetImportStatus::Ready:AssetImportStatus::ReadyWithWarnings;
        model->Id=nextAssetId.fetch_add(1);return model;
    } catch (const std::invalid_argument& e) {throw ImportError(ImportErrorCode::InvalidData,e.what());}
}
ModelHandle AssetRegistry::PrepareModel(const std::filesystem::path& path,const ImportSettings& settings) const {return importers_.Import(path,settings);}
ModelHandle AssetRegistry::AddPrepared(ModelHandle model) {
    if (!model || !model->Id || model->SourcePath.empty()) {throw std::invalid_argument("Expected a prepared model with a valid ID/source path.");}ValidateModel(*model);
    for (const auto& existing:models_) {
        std::error_code ec;
        if (existing == model || (existing->Settings == model->Settings && (existing->SourcePath == model->SourcePath || (std::filesystem::equivalent(existing->SourcePath,model->SourcePath,ec) && !ec)))) {
            if (existing->ContentHash!=model->ContentHash) {throw ImportError(ImportErrorCode::InvalidData,"This source/settings are registered with different content; use Reimport to replace existing instances.");}return existing;
        }
        if (existing->Id == model->Id) {throw std::invalid_argument("Prepared Asset ID is already used by different source/settings.");}
    }
    models_.push_back(model);return model;
}

ModelHandle AssetRegistry::LoadModel(const std::filesystem::path& path, const ImportSettings& settings) {
    const auto normalized=NormalizeSettings(settings);
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) { throw ImportError(ImportErrorCode::Io, "Cannot resolve model path: " + detail::PathText(path)); }
    for (const auto& model : models_) {
        if (model->Settings == normalized && std::filesystem::equivalent(model->SourcePath, canonical, error) && !error) {
            return model;
        }
    }
    auto model = importers_.Import(canonical, normalized);
    models_.push_back(model); // Failed imports never enter the registry.
    return model;
}

ModelHandle AssetRegistry::Find(AssetId id) const noexcept {
    for (const auto& model : models_) { if (model->Id == id) { return model; } }
    return {};
}
ModelHandle AssetRegistry::ReplacePrepared(AssetId id,ModelHandle candidate) {
    if (!candidate) {throw std::invalid_argument("Missing replacement candidate.");}ValidateModel(*candidate);
    for (auto& current:models_) {if (current->Id != id) {continue;}std::error_code error;if (current->Settings != candidate->Settings || (current->SourcePath != candidate->SourcePath && (!std::filesystem::equivalent(current->SourcePath,candidate->SourcePath,error) || error))) {throw std::invalid_argument("Replacement source/settings do not match the original asset.");}auto replacement=std::make_shared<ModelAsset>(*candidate);replacement->Id=id;current=std::move(replacement);return current;}throw std::invalid_argument("Unknown replacement asset.");
}

ModelHandle AssetRegistry::ReimportModel(AssetId id) {
    for (auto& current : models_) {
        if (current->Id != id) { continue; }
        auto replacement = std::make_shared<ModelAsset>(*importers_.Import(current->SourcePath, current->Settings));
        const auto before=InspectModel(*current),after=InspectModel(*replacement);
        bool changed=before.Meshes!=after.Meshes || before.Primitives!=after.Primitives || before.Vertices!=after.Vertices || before.Indices!=after.Indices || before.Materials!=after.Materials || before.Skins!=after.Skins || before.Animations!=after.Animations || current->Skeleton.Signature!=replacement->Skeleton.Signature;
        if (!changed) {for (std::size_t i=0;i<current->Materials.size();++i) {changed|=current->Materials[i].Name!=replacement->Materials[i].Name;}for (std::size_t i=0;i<current->Meshes.size();++i) {changed|=current->Meshes[i].Name!=replacement->Meshes[i].Name;}}
        if (changed) {replacement->Warnings.push_back("Reimport changed mesh/material/hierarchy/skeleton/animation structure; review references before replacing Scene instances.");replacement->Status=AssetImportStatus::ReadyWithWarnings;}
        replacement->Id = id;
        current = std::move(replacement);
        return current;
    }
    throw std::invalid_argument("Asset ID does not belong to this registry.");
}

} // namespace lv
