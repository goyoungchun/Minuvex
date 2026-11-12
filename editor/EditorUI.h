#pragma once
#include "SceneViewport.h"
#include "CameraController.h"
#include "NativeFiles.h"
#include "EditorPreferences.h"
#include <minuvex/EditorDocument.h>
#include <minuvex/AssetLoading.h>

namespace lv::editor {
enum class ConsoleLevel { Info, Warning, Error };
struct ConsoleEntry {ConsoleLevel Level;double Time;std::string Message;std::filesystem::path Source;ObjectId Object=NoObject;};
enum class EditorFileAction { OpenModel, OpenScene, ImportAssets, SaveAs, NewProject };
enum class UnsavedChoice { Save, Discard, Cancel };
class EditorUI final {
public:
    EditorDocument Document;
    SceneViewport Viewport;
    void Draw(); // Inside an ImGui frame, outside graphics passes.
    void Shutdown() noexcept;
    void InitializeSession(std::filesystem::path preferences,std::filesystem::path sample,std::filesystem::path documentation,bool persist=true);
    void SetFilePicker(FilePicker picker) {picker_=std::move(picker);}
    void ChooseFiles(EditorFileAction action);
    void ResolvePending(UnsavedChoice choice);
    void RequestQuickOpen(const std::filesystem::path& path);
    void DropFiles(const std::vector<std::filesystem::path>& paths,float x,float y);
    void Record(ConsoleLevel level,std::string message,std::filesystem::path source={},ObjectId object=NoObject);
    const std::vector<ConsoleEntry>& ConsoleEntries() const noexcept {return consoleEntries_;}
    void Import(const std::filesystem::path& path, bool place);
    bool HasImportPreview() const noexcept {return !importQueue_.empty();}
    ModelHandle ImportPreviewModel() const noexcept {return importCandidate_;}
    void RefreshImportPreview(const ImportSettings& settings);
    bool ConfirmImportPreview();
    void CancelImportPreview();
    void PollAssetJobs();
    bool ImportPreviewPending() const noexcept {return previewJob_.has_value();}
    void SetHotReload(bool enabled) noexcept {hotReload_=enabled;}
    void RequestOpen(const std::filesystem::path& path);
    void RequestNew();
    void RequestQuit();
    bool QuitRequested() const noexcept { return quit_; }
    void SetDirectory(std::filesystem::path path) { directory_ = std::move(path); }
    void ShowError(const Error& error,ObjectId object=NoObject);
    void LoadQualityProfile(const std::filesystem::path& path);
    Result<void> ApplyQualityProfile(const QualityProfile& profile);
    const QualityProfile& QualityDraft() const noexcept {return qualityDraft_;}
    void SetQualityDraft(QualityProfile profile) {qualityDraft_=std::move(profile);}
    void ResetQualityDraft() {qualityDraft_=Viewport.Quality().Requested;}
    const std::optional<QualityEstimate>& SelectedQualityEstimate() const noexcept {return selectedEstimate_;}
    void ShowDebuggers() noexcept {renderDebugger_=animationDebugger_=true;}
    std::array<float,4> ViewportArea() const noexcept {return viewportRect_;}
    bool GizmoVisible() const noexcept {return gizmoVisible_;}
    int TransformTool() const noexcept {return transformTool_;}
    // Queue from UI actions after Image commands exist; commit at the next Draw entry.
    void RequestQualityProfile(QualityProfile profile) {pendingQuality_=std::move(profile);}
private:
    enum class Action { None, New, Open, QuickOpen, Project, Quit } pending_ = Action::None;
    enum class Dialog { None, Import, Open, QuickOpen, Project, Save, PoseFull, PosePartial, PoseLoad, QualitySave, QualityLoad, TelemetryJson, TelemetryCsv } dialog_ = Dialog::None;
    std::filesystem::path directory_ = std::filesystem::current_path(), openPath_;
    std::string error_, status_ = "Open a model to begin.";
    NativeFiles nativeFiles_;
    FilePicker picker_;
    EditorPreferences preferences_;
    std::filesystem::path preferencesPath_,samplePath_,documentationPath_,projectRoot_;
    bool persistPreferences_=false,welcome_=true,console_=true;
    bool consoleInfo_=true,consoleWarnings_=true,consoleErrors_=true;
    std::vector<ConsoleEntry> consoleEntries_;
    std::string consoleFilter_;
    float assetRect_[4]{};
    ObjectId deleteObject_=NoObject;
    std::string filter_;
    std::string hierarchyFilter_;
    std::array<float,4> viewportRect_{};
    int transformTool_=0;
    float crossFadeSeconds_=.3f;
    bool grid_=true,gizmoVisible_=false,gizmoBlockInput_=false,gizmoEditing_=false,gizmoSuppressed_=false;
    bool cameraEditing_=false;
    ObjectId gizmoObject_=NoObject;
    AssetId selectedAsset_ = 0;
    bool quit_ = false, resetLayout_ = false, cameraDrag_ = false;
    bool hierarchy_ = true, inspector_ = true, assets_ = true, viewport_ = true;
    bool bones_ = true, animation_ = true;
    bool quality_ = true;
    bool performance_ = true;
    bool assetInspector_=true,copyImport_=false;
    bool renderDebugger_=false,animationDebugger_=false;
    int inspectedTarget_=0;
    DebugDraw editorDebugDraw_;
    struct PendingImport {std::filesystem::path Path;bool Place=false;};
    std::vector<PendingImport> importQueue_;
    ImportSettings importSettings_;
    ModelHandle importCandidate_;
    std::optional<Error> importError_;
    std::unique_ptr<AssetLoader> loader_;
    std::optional<AssetLoadId> previewJob_;
    bool backgroundImport_=true,hotReload_=false;
    AssetHotReload watcher_;
    struct PendingReload {AssetLoadId Job;ModelHandle Before;bool Again=false;};std::vector<PendingReload> reloads_;
    QualityProfile qualityDraft_;
    std::optional<QualityProfile> pendingQuality_;
    QualityProfile analyzedProfile_;
    std::optional<QualityEstimate> selectedEstimate_;
    QualityRecommendation recommendation_;
    std::string estimateError_;
    double lastAnalysis_ = -1;
    TelemetrySnapshot analysisSnapshot_;
    int analysisFrame_=-1,analysisWidth_=0,analysisHeight_=0;
    ObjectId boneObject_ = NoObject, poseObject_ = NoObject;
    JointId bone_ = NoJoint;
    detail::CameraController controls_;
    void ApplyPending();
    void Ask(Action action);
    void FileDialog(Dialog type);
    void PersistPreferences();
    void Remember(const std::filesystem::path& path,RecentKind kind);
    void Welcome();
    void Console();
    bool Save(const std::filesystem::path& path);
    bool SaveDialog(Dialog type, const std::filesystem::path& path);
    void FrameScene();
    void Dockspace();
    void Menu();
    void Hierarchy();
    void Inspector();
    void BoneInspector();
    void AnimationControls();
    void QualitySettings();
    void PerformancePanel();
    const TelemetrySnapshot& RefreshQualityAnalysis();
    void QualityOverview();
    void RenderDebugger();
    void AnimationDebugger();
    void AssetBrowser();
    void AssetInspector();
    void ImportPreview();
    void AdvanceImportPreview();
    void ReimportAsset(AssetId asset);
    void CancelPreviewJob();
    void ViewportPanel();
    void TransformTools(float x,float y,float width,float height,bool enabled);
    void StopCameraEdit(bool cancel);
    void Dialogs();
    template<typename T> bool Report(const Result<T>& result,ObjectId object=NoObject) {
        if (!result) { ShowError(result.Error(),object); return false; } return true;
    }
};
} // namespace lv::editor
