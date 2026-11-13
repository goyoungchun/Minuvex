#include "ImportCommon.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <sstream>
#if defined(_WIN32)
#include <thread>
#endif

namespace lv {
namespace {
std::atomic<std::uint64_t> packageId{1};
bool Within(const std::filesystem::path& parent,const std::filesystem::path& file) {
    const auto relative=file.lexically_relative(parent);return !relative.empty() && !relative.is_absolute() && *relative.begin()!="..";
}
void Publish(const std::filesystem::path& staging,const std::filesystem::path& destination) {
    if (std::filesystem::exists(destination)) {throw ImportError(ImportErrorCode::Io,"Refusing to overwrite an existing imported package.");}
#if defined(_WIN32)
    std::error_code error;
    for (unsigned attempt=0;attempt<6;++attempt) {
        std::filesystem::rename(staging,destination,error);if (!error) {return;}
        // Antivirus/indexer opens can briefly deny a directory rename after copying.
        // Permanent permission errors still surface within 310 ms; never delete the destination.
        std::error_code probe;
        if (attempt == 5 || (error.value()!=5 && error.value()!=32 && error.value()!=33) || std::filesystem::exists(destination,probe)) {break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(10u<<attempt));
    }
    throw std::filesystem::filesystem_error("Cannot publish imported package",staging,destination,error);
#else
    std::filesystem::rename(staging,destination);
#endif
}
struct Staging {
    std::filesystem::path Parent,Path;
    ~Staging() {
        // Only remove the fresh immediate staging child created by this call.
        if (!Path.empty() && Path.parent_path() == Parent && Path.filename().string().starts_with(".minvex-import-")) {std::error_code ec;std::filesystem::remove_all(Path,ec);}
    }
};
}
ModelHandle CopyModelToProject(ModelHandle model,const std::filesystem::path& assetsDirectory) {
    if (!model) {throw std::invalid_argument("Select a prepared model before copying resources.");}ValidateModel(*model);
    if (model->Format == AssetFormat::Custom) {throw ImportError(ImportErrorCode::UnsupportedFeature,"Custom importers must provide their own resource packaging.");}
    if (assetsDirectory.empty()) {throw std::invalid_argument("Project Asset directory is empty.");}
    try {
        const auto root=std::filesystem::absolute(assetsDirectory).lexically_normal();std::filesystem::create_directories(root);
        auto common=model->SourcePath.parent_path();auto files=model->Dependencies;
        if (std::find(files.begin(),files.end(),model->SourcePath) == files.end()) {files.insert(files.begin(),model->SourcePath);}
        for (const auto& file:files) {
            if (file.root_path()!=common.root_path()) {throw ImportError(ImportErrorCode::UnsupportedFeature,"Project copy cannot preserve references across filesystem roots; use external references.");}
            while (!Within(common,file)) {const auto parent=common.parent_path();if (parent == common) {throw ImportError(ImportErrorCode::UnsupportedFeature,"Cannot preserve dependency layout.");}common=parent;}
        }
        if (!model->Settings.TextureDirectory.empty()) {while (!Within(common,model->Settings.TextureDirectory) && common!=model->Settings.TextureDirectory) {const auto parent=common.parent_path();if (parent == common) {throw ImportError(ImportErrorCode::UnsupportedFeature,"Texture directory uses a different filesystem root.");}common=parent;}}
        std::ostringstream suffix;suffix<<std::hex<<model->ContentHash<<'-'<<std::chrono::steady_clock::now().time_since_epoch().count()<<'-'<<packageId.fetch_add(1);
        Staging stage{root,root/(".minvex-import-"+suffix.str())};const auto published=root/("Import-"+suffix.str());
        if (!std::filesystem::create_directory(stage.Path)) {stage.Path.clear();throw ImportError(ImportErrorCode::Io,"A package staging directory already exists; retry the import.");}
        for (const auto& source:files) {const auto destination=stage.Path/source.lexically_relative(common);std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(source,destination,std::filesystem::copy_options::none);}
        auto settings=model->Settings;if (!settings.TextureDirectory.empty()) {settings.TextureDirectory=stage.Path/settings.TextureDirectory.lexically_relative(common);}
        const auto source=stage.Path/model->SourcePath.lexically_relative(common);
        auto copied=std::make_shared<ModelAsset>(*ImporterRegistry{}.Import(source,settings));
        if (copied->ContentHash!=model->ContentHash || copied->MissingDependencies.size()>model->MissingDependencies.size()) {throw ImportError(ImportErrorCode::InvalidData,"Source/dependencies changed or copied resources cannot be resolved; refresh the Import Preview.");}
        copied->SourcePath=published/source.lexically_relative(stage.Path);
        for (auto& dependency:copied->Dependencies) {dependency=published/dependency.lexically_relative(stage.Path);}
        for (auto& missing:copied->MissingDependencies) {if (missing.is_absolute() && Within(stage.Path,missing)) {missing=published/missing.lexically_relative(stage.Path);}}
        if (!copied->Settings.TextureDirectory.empty()) {copied->Settings.TextureDirectory=published/copied->Settings.TextureDirectory.lexically_relative(stage.Path);}
        Publish(stage.Path,published);stage.Path.clear();return copied;
    } catch (const std::filesystem::filesystem_error& e) {throw ImportError(ImportErrorCode::Io,e.what());}
}
} // namespace lv
