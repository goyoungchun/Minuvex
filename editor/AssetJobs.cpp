#include "EditorUI.h"
#include <algorithm>
namespace lv::editor {
void EditorUI::PollAssetJobs() {
    if (previewJob_ && loader_) {const auto job=loader_->Poll(*previewJob_);if (job && job.Value().State != AssetLoadState::Queued && job.Value().State != AssetLoadState::Importing) {importCandidate_=job.Value().Model;importError_=job.Value().Failure;if (importError_) {Record(ConsoleLevel::Error,importError_->Message,importError_->SourcePath);}(void)loader_->Forget(*previewJob_);previewJob_.reset();}}
    watcher_.Synchronize(Document.Assets());
    const auto submit=[&](ModelHandle model) {if (!loader_) {loader_=std::make_unique<AssetLoader>();}const auto job=loader_->Submit(model->SourcePath,model->Settings);if (job) {reloads_.push_back({job.Value(),std::move(model)});}else {Record(ConsoleLevel::Error,job.Error().Message,job.Error().SourcePath);}};
    if (hotReload_) {for (auto id:watcher_.Poll(detail::RenderClockSeconds())) {auto pending=std::find_if(reloads_.begin(),reloads_.end(),[&](const auto& r) {return r.Before->Id == id;});if (pending != reloads_.end()) {pending->Again=true;}else {for (const auto& model:Document.Assets()) {if (model->Id == id) {submit(model);break;}}}}}
    std::vector<ModelHandle> repeat;
    for (std::size_t i=0;i<reloads_.size();) {const auto r=reloads_[i];const auto job=loader_->Poll(r.Job);if (job && (job.Value().State == AssetLoadState::Queued || job.Value().State == AssetLoadState::Importing)) {if (!hotReload_) {(void)loader_->Cancel(r.Job);}else {++i;continue;}}
        const auto current=std::find(Document.Assets().begin(),Document.Assets().end(),r.Before);
        if (job && hotReload_ && current != Document.Assets().end()) {if (job.Value().State == AssetLoadState::Ready) {const auto result=Document.CommitReimport(r.Before->Id,job.Value().Model);if (result) {Record(ConsoleLevel::Info,"Hot Reload applied",r.Before->SourcePath);}else {Record(ConsoleLevel::Error,"Hot Reload retained previous asset: "+result.Error().Message,r.Before->SourcePath);}}else if (job.Value().Failure) {Record(ConsoleLevel::Error,"Hot Reload retained previous asset: "+job.Value().Failure->Message,r.Before->SourcePath);}}
        (void)loader_->Forget(r.Job);reloads_.erase(reloads_.begin()+i);if (hotReload_ && r.Again) {for (const auto& model:Document.Assets()) {if (model->Id == r.Before->Id) {repeat.push_back(model);break;}}}
    }for (auto& model:repeat) {submit(model);}
}
}
