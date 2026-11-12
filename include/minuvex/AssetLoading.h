#pragma once
#include <minuvex/Asset.h>
#include <minuvex/Result.h>
#include <span>
namespace lv {
using AssetLoadId=std::uint64_t;
enum class AssetLoadState { Queued, Importing, Ready, Failed, Cancelled };
struct AssetLoadSnapshot {AssetLoadState State=AssetLoadState::Queued;ModelHandle Model;std::optional<Error> Failure;};
// One bounded CPU worker. Submit/Poll/Cancel/Forget are thread safe; never modifies a Registry/Scene/GPU.
// Cancel is immediate publication cancellation; a running third-party parser finishes privately.
// Destruction requests stop and joins the current import. Custom importer captures must be thread safe.
class AssetLoader final {
public:
    AssetLoader();~AssetLoader();
    AssetLoader(const AssetLoader&)=delete;AssetLoader& operator=(const AssetLoader&)=delete;
    Result<AssetLoadId> Submit(const std::filesystem::path& path,const ImportSettings& settings={},ImporterRegistry importers={});
    Result<AssetLoadSnapshot> Poll(AssetLoadId id) const;
    Result<void> Cancel(AssetLoadId id);
    Result<void> Forget(AssetLoadId id); // Terminal jobs only; releases retained Model/Error.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Metadata polling of source/loaded/missing dependencies, at most once per second. Owner thread only.
// Poll consumes the change event even if the caller's reload fails, preventing retry storms.
class AssetHotReload {
public:
    void Watch(ModelHandle model);
    void Synchronize(std::span<const ModelHandle> models);
    void Clear() noexcept {entries_.clear();lastPoll_=-1;}
    std::vector<AssetId> Poll(double now);
private:
    struct Entry {ModelHandle Model;std::uint64_t Stamp;};std::vector<Entry> entries_;double lastPoll_=-1;
};
}
