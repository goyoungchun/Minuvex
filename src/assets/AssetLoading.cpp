#include <minvex/AssetLoading.h>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <algorithm>
namespace lv {
namespace {
Error ImportFailure(const std::filesystem::path& path) {try {throw;}catch (const ImportError& e) {return {e.Code == ImportErrorCode::Io ? ErrorCode::AssetLoad:e.Code == ImportErrorCode::ResourceLimit ? ErrorCode::InsufficientResource:e.Code == ImportErrorCode::UnsupportedFeature ? ErrorCode::UnsupportedFeature:ErrorCode::InvalidData,e.what(),"Correct the source and retry; the existing Scene was not changed.",path};}catch (const std::bad_alloc& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce import size.",path};}catch (const std::exception& e) {return {ErrorCode::InvalidData,e.what(),"Check the importer and source data.",path};}catch (...) {return {ErrorCode::AssetLoad,"Importer threw an unknown exception.","Check the custom importer.",path};}}
Error Unknown() {return {ErrorCode::InvalidHandle,"Unknown asset load ID.","Use an ID retained by this loader.",{}};}
std::uint64_t Stamp(const ModelAsset& model) {
    std::uint64_t h=14695981039346656037ull;const auto add=[&](std::uint64_t v) {h^=v;h*=1099511628211ull;};
    auto paths=model.Dependencies;paths.insert(paths.end(),model.MissingDependencies.begin(),model.MissingDependencies.end());paths.push_back(model.SourcePath);
    for (const auto& path:paths) {std::error_code error;const auto t=std::filesystem::last_write_time(path,error);add(error ? UINT64_MAX:static_cast<std::uint64_t>(t.time_since_epoch().count()));const auto bytes=std::filesystem::file_size(path,error);add(error ? UINT64_MAX:bytes);}return h;
}
}
struct AssetLoader::Impl {
    struct Job {std::filesystem::path Path;ImportSettings Settings;ImporterRegistry Importers;AssetLoadSnapshot Snapshot;};
    mutable std::mutex Mutex;std::condition_variable_any Wake;std::deque<std::shared_ptr<Job>> Queue;std::unordered_map<AssetLoadId,std::shared_ptr<Job>> Jobs;AssetLoadId Next=1;
    std::jthread Worker;
    Impl():Worker([this](std::stop_token stop) {while (!stop.stop_requested()) {
        std::shared_ptr<Job> job;{std::unique_lock lock(Mutex);if (!Wake.wait(lock,stop,[&]{return !Queue.empty();})) {break;}job=Queue.front();Queue.pop_front();if (job->Snapshot.State == AssetLoadState::Cancelled) {continue;}job->Snapshot.State=AssetLoadState::Importing;}
        ModelHandle model;std::optional<Error> failure;try {model=job->Importers.Import(job->Path,job->Settings);}catch (...) {try {failure=ImportFailure(job->Path);}catch (...) {failure=Error{ErrorCode::InsufficientResource,"Import failure metadata allocation failed.","Reduce memory pressure.",{}};}}
        std::lock_guard lock(Mutex);if (job->Snapshot.State != AssetLoadState::Cancelled && !stop.stop_requested()) {job->Snapshot.Model=std::move(model);job->Snapshot.Failure=std::move(failure);job->Snapshot.State=job->Snapshot.Model ? AssetLoadState::Ready:AssetLoadState::Failed;}
    }}) {}
    ~Impl() {Worker.request_stop();Wake.notify_all();} // jthread joins before the other members are destroyed.
};
AssetLoader::AssetLoader():impl_(std::make_unique<Impl>()) {}AssetLoader::~AssetLoader()=default;
Result<AssetLoadId> AssetLoader::Submit(const std::filesystem::path& path,const ImportSettings& settings,ImporterRegistry importers) {
    try {if (!importers.Supports(path)) {return Result<AssetLoadId>::Failure({ErrorCode::UnsupportedFeature,"Unsupported import extension.","Choose a registered source format.",path});}auto job=std::make_shared<Impl::Job>();job->Path=std::filesystem::absolute(path).lexically_normal();job->Settings=settings;if (!job->Settings.TextureDirectory.empty()) {job->Settings.TextureDirectory=std::filesystem::absolute(job->Settings.TextureDirectory).lexically_normal();}job->Importers=std::move(importers);std::lock_guard lock(impl_->Mutex);if (impl_->Jobs.size()>=64) {return Result<AssetLoadId>::Failure({ErrorCode::InsufficientResource,"At most 64 retained async jobs are supported.","Forget finished jobs before submitting more.",path});}const auto id=impl_->Next++;impl_->Queue.push_back(job);try {impl_->Jobs.emplace(id,job);}catch (...) {impl_->Queue.pop_back();throw;}impl_->Wake.notify_one();return Result<AssetLoadId>::Success(id);}catch (...) {return Result<AssetLoadId>::Failure(ImportFailure(path));}
}
Result<AssetLoadSnapshot> AssetLoader::Poll(AssetLoadId id) const {std::lock_guard lock(impl_->Mutex);const auto it=impl_->Jobs.find(id);if (it == impl_->Jobs.end()) {return Result<AssetLoadSnapshot>::Failure(Unknown());}return Result<AssetLoadSnapshot>::Success(it->second->Snapshot);}
Result<void> AssetLoader::Cancel(AssetLoadId id) {std::lock_guard lock(impl_->Mutex);const auto it=impl_->Jobs.find(id);if (it == impl_->Jobs.end()) {return Result<void>::Failure(Unknown());}it->second->Snapshot={AssetLoadState::Cancelled,{},{}};return Result<void>::Success();}
Result<void> AssetLoader::Forget(AssetLoadId id) {std::lock_guard lock(impl_->Mutex);const auto it=impl_->Jobs.find(id);if (it == impl_->Jobs.end()) {return Result<void>::Failure(Unknown());}if (it->second->Snapshot.State == AssetLoadState::Queued || it->second->Snapshot.State == AssetLoadState::Importing) {return Result<void>::Failure({ErrorCode::InvalidState,"Job is still importing.","Cancel it before forgetting.",{}});}std::erase(impl_->Queue,it->second);impl_->Jobs.erase(it);return Result<void>::Success();}
void AssetHotReload::Watch(ModelHandle model) {if (!model || !model->Id || model->SourcePath.empty()) {throw std::invalid_argument("Watch requires a prepared source asset.");}for (auto& e:entries_) {if (e.Model->Id == model->Id) {e.Model=model;return;}}entries_.push_back({model,Stamp(*model)});}
void AssetHotReload::Synchronize(std::span<const ModelHandle> models) {for (const auto& model:models) {Watch(model);}std::erase_if(entries_,[&](const auto& entry) {return std::none_of(models.begin(),models.end(),[&](const auto& model) {return model->Id == entry.Model->Id;});});}
std::vector<AssetId> AssetHotReload::Poll(double now) {if (!std::isfinite(now) || now<0) {throw std::invalid_argument("Invalid hot-reload clock.");}std::vector<AssetId> changed;if (lastPoll_>=0 && now>=lastPoll_ && now-lastPoll_<1) {return changed;}lastPoll_=now;for (auto& entry:entries_) {const auto stamp=Stamp(*entry.Model);if (stamp != entry.Stamp) {entry.Stamp=stamp;changed.push_back(entry.Model->Id);}}return changed;}
}
