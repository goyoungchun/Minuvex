#pragma once
#include "GltfImport.h"
#include <span>

namespace lv::detail {
[[noreturn]] inline void ImportInvalid(const std::string& message) {throw ImportError(ImportErrorCode::InvalidData,message);}
[[noreturn]] inline void ImportLimit(const std::string& message) {throw ImportError(ImportErrorCode::ResourceLimit,message);}
void ValidateImportSettings(const ImportSettings& settings);
struct ImportContext {
    ModelAsset& Model;
    std::size_t Bytes=0,Vertices=0,Indices=0;
    void Reserve(std::size_t bytes);
    void Geometry(std::size_t vertices,std::size_t indices);
    void Hash(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> Read(const std::filesystem::path& path);
    std::optional<std::size_t> Texture(const std::string& name,std::span<const std::uint8_t> embedded={},const std::filesystem::path& directory={});
};
void FinishImportedModel(ModelAsset& model);
std::shared_ptr<ModelAsset> ImportObj(const std::filesystem::path& path,const ImportSettings& settings);
std::shared_ptr<ModelAsset> ImportFbx(const std::filesystem::path& path,const ImportSettings& settings);
} // namespace lv::detail
