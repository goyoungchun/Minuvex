#include "EditorUI.h"
#include "core/Utf8Path.h"

#include <imgui.h>
#include <imgui_internal.h> // Pinned docking release: initial layout builder only.
#include <misc/cpp/imgui_stdlib.h>
#include <sokol_app.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <numbers>
#include <unordered_map>
#include <unordered_set>
#include <ImGuizmo.h>

namespace lv::editor {
namespace {
std::string Text(const std::filesystem::path& path) { const auto s = path.generic_u8string(); return {s.begin(),s.end()}; }
bool ModelFile(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx";
}
void Id(ObjectId id) { ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id))); }
bool VecInput(const char* label, Vec3& value, float speed = 0.05f) {
    float xyz[]{value.X,value.Y,value.Z};
    if (!ImGui::DragFloat3(label,xyz,speed)) { return false; } value = {xyz[0],xyz[1],xyz[2]}; return true;
}
bool RotationInput(Quaternion& value) {
    const auto length = std::hypot(std::hypot(double(value.X),double(value.Y)),std::hypot(double(value.Z),double(value.W)));
    const double x = value.X/length, y = value.Y/length, z = value.Z/length, w = value.W/length;
    constexpr double degrees = 180/std::numbers::pi;
    float angles[]{static_cast<float>(std::atan2(2*(w*x+y*z),1-2*(x*x+y*y))*degrees),
        static_cast<float>(std::asin(std::clamp(2*(w*y-z*x),-1.0,1.0))*degrees),
        static_cast<float>(std::atan2(2*(w*z+x*y),1-2*(y*y+z*z))*degrees)};
    if (!ImGui::DragFloat3("Rotation (deg)",angles,1)) { return false; }
    const double ax = angles[0]/degrees/2, ay = angles[1]/degrees/2, az = angles[2]/degrees/2;
    const auto cx = std::cos(ax), cy = std::cos(ay), cz = std::cos(az), sx = std::sin(ax), sy = std::sin(ay), sz = std::sin(az);
    value = {static_cast<float>(sx*cy*cz-cx*sy*sz),static_cast<float>(cx*sy*cz+sx*cy*sz),
        static_cast<float>(cx*cy*sz-sx*sy*cz),static_cast<float>(cx*cy*cz+sx*sy*sz)};
    return true;
}
}
void EditorUI::Record(ConsoleLevel level,std::string message,std::filesystem::path source,ObjectId object) {
    if (message.size() > 8192) {std::size_t end=8192;while (end && (static_cast<unsigned char>(message[end])&0xC0) == 0x80) {--end;}message.resize(end);}
    if (consoleEntries_.size() == 256) {consoleEntries_.erase(consoleEntries_.begin());}
    consoleEntries_.push_back({level,detail::RenderClockSeconds(),std::move(message),std::move(source),object});
}
void EditorUI::ShowError(const Error& error,ObjectId object) { error_ = error.Message + "\n" + error.Hint + (error.SourcePath.empty() ? "" : "\n"+Text(error.SourcePath));Record(ConsoleLevel::Error,error.Message+"\n"+error.Hint,error.SourcePath,object); }
void EditorUI::InitializeSession(std::filesystem::path preferences,std::filesystem::path sample,std::filesystem::path documentation,bool persist) {
    preferencesPath_=std::move(preferences);samplePath_=std::move(sample);documentationPath_=std::move(documentation);persistPreferences_=persist;
    if (persist) {auto loaded=preferences_.Load(preferencesPath_);if (!loaded) {Record(ConsoleLevel::Warning,loaded.Error().Message,preferencesPath_);}else {std::error_code ec;if (std::filesystem::is_directory(preferences_.Directory,ec)) {directory_=preferences_.Directory;}}}
}
void EditorUI::PersistPreferences() {
    if (!persistPreferences_) {return;}preferences_.Directory=directory_;const auto saved=preferences_.Save(preferencesPath_);if (!saved) {Record(ConsoleLevel::Warning,saved.Error().Message,preferencesPath_);}
}
void EditorUI::Remember(const std::filesystem::path& path,RecentKind kind) {try {preferences_.Touch(path,kind);directory_=preferences_.Directory;PersistPreferences();}catch (const std::exception& e) {Record(ConsoleLevel::Warning,e.what(),preferencesPath_);}}
void EditorUI::Shutdown() noexcept {try {PersistPreferences();CancelImportPreview();loader_.reset();reloads_.clear();watcher_.Clear();}catch (...) {}Viewport.Clear();}
void EditorUI::Import(const std::filesystem::path& path, bool place) {
    if (importQueue_.size()>=32) {ShowError({ErrorCode::InsufficientResource,"At most 32 files can be queued for Import Preview.","Finish or cancel the current import first.",path});return;}
    importQueue_.push_back({path,place});if (importQueue_.size() == 1) {importSettings_={};copyImport_=!projectRoot_.empty();RefreshImportPreview(importSettings_);}
}
void EditorUI::RefreshImportPreview(const ImportSettings& settings) {
    if (importQueue_.empty()) {return;}
    CancelPreviewJob();importSettings_=settings;importCandidate_.reset();importError_.reset();
    if (backgroundImport_) {if (!loader_) {loader_=std::make_unique<AssetLoader>();}auto job=loader_->Submit(importQueue_.front().Path,settings);if (job) {previewJob_=job.Value();}else {importError_=job.Error();}return;}
    auto candidate=Document.PreviewModel(importQueue_.front().Path,settings);
    if (candidate) {importCandidate_=candidate.Value();}else {importError_=candidate.Error();Record(ConsoleLevel::Error,candidate.Error().Message,candidate.Error().SourcePath);}
}
void EditorUI::CancelPreviewJob() {if (previewJob_ && loader_) {(void)loader_->Cancel(*previewJob_);(void)loader_->Forget(*previewJob_);}previewJob_.reset();}
void EditorUI::AdvanceImportPreview() {CancelPreviewJob();importCandidate_.reset();importError_.reset();if (!importQueue_.empty()) {importQueue_.erase(importQueue_.begin());}if (!importQueue_.empty()) {RefreshImportPreview(importSettings_);}}
void EditorUI::CancelImportPreview() {CancelPreviewJob();importQueue_.clear();importCandidate_.reset();importError_.reset();status_="Import cancelled; Scene and assets retained.";}
bool EditorUI::ConfirmImportPreview() {
    if (!importCandidate_ || importQueue_.empty()) {return false;}
    auto model=importCandidate_;
    if (copyImport_ && !projectRoot_.empty()) {
        try {model=CopyModelToProject(model,projectRoot_/"assets");}
        catch (const std::exception& e) {importError_=Error{ErrorCode::AssetLoad,e.what(),"Refresh the preview, fix dependencies, or choose external references.",importQueue_.front().Path};Record(ConsoleLevel::Error,e.what(),importQueue_.front().Path);return false;}
    }
    const bool place=importQueue_.front().Place;auto committed=Document.CommitImport(model,place);
    if (!committed) {importError_=committed.Error();Record(ConsoleLevel::Error,committed.Error().Message,model->SourcePath);return false;}
    selectedAsset_=committed.Value();assetInspector_=true;welcome_=false;
    if (place) {FrameScene();}
    status_="Imported "+Text(model->SourcePath.filename());Remember(model->SourcePath,RecentKind::Model);Record(ConsoleLevel::Info,status_,model->SourcePath,place ? Document.Selection():NoObject);
    for (const auto& warning:model->Warnings) {Record(ConsoleLevel::Warning,warning,model->SourcePath);}
    AdvanceImportPreview();return true;
}
void EditorUI::FrameScene() {
    try {
        const float aspect = Viewport.Aspect();
        Report(Document.SetCamera(detail::FitSceneCamera(Document.GetScene(),aspect,Document.Selection())),Document.Selection());
    } catch (const std::exception& e) { ShowError({ErrorCode::InvalidState,e.what(),"Place a visible model in the Scene first.",{}}); }
}
void EditorUI::ReimportAsset(AssetId asset) {
    if (!Report(Document.ReimportModel(asset))) {return;}
    for (const auto& model:Document.Assets()) {if (model->Id == asset) {status_="Reimported "+Text(model->SourcePath.filename());Record(ConsoleLevel::Info,status_,model->SourcePath);for (const auto& warning:model->Warnings) {Record(ConsoleLevel::Warning,warning,model->SourcePath);}}}
}
void EditorUI::Ask(Action action) {if (HasImportPreview()) {Record(ConsoleLevel::Warning,"Finish or cancel Import Preview before changing the document.");return;}pending_ = action; if (!Document.IsDirty()) { ApplyPending(); } }
void EditorUI::RequestOpen(const std::filesystem::path& path) { openPath_ = path; Ask(Action::Open); }
void EditorUI::RequestQuickOpen(const std::filesystem::path& path) {openPath_=path;Ask(Action::QuickOpen);}
void EditorUI::RequestNew() { Ask(Action::New); }
void EditorUI::RequestQuit() { Ask(Action::Quit); }
void EditorUI::ApplyPending() {
    switch (pending_) {
    case Action::New: Document.New(); selectedAsset_ = 0; controls_.Reset();welcome_=false; status_ = "New Scene"; break;
    case Action::Open:
        if (Report(Document.Open(openPath_))) {
            selectedAsset_ = 0; controls_.Reset();welcome_=false;projectRoot_.clear();
            const auto scenes=Document.Path().parent_path();std::error_code ec;
            if (scenes.filename() == "scenes" && std::filesystem::is_directory(scenes.parent_path()/"assets",ec)) {projectRoot_=scenes.parent_path();}
            status_ = "Opened " + Text(openPath_);Remember(openPath_,RecentKind::Scene);Record(ConsoleLevel::Info,status_,openPath_);
        }
        break;
    case Action::QuickOpen: {
        if (!ModelFile(openPath_)) {ShowError({ErrorCode::UnsupportedFeature,"Choose a supported glTF, GLB, OBJ or FBX model.","Check the file extension.",openPath_});break;}
        const auto opened=Document.OpenModel(openPath_,{},Viewport.Aspect());
        if (Report(opened)) {selectedAsset_=Document.GetScene().GetObject(opened.Value()).Model->Id;controls_.Reset();welcome_=false;projectRoot_.clear();status_="Quick Open: "+Text(openPath_.filename());Remember(openPath_,RecentKind::Model);Record(ConsoleLevel::Info,status_,openPath_,opened.Value());for (const auto& warning:Document.Assets().front()->Warnings) {Record(ConsoleLevel::Warning,warning,openPath_);}}
        break;
    }
    case Action::Project: {
        std::filesystem::path root,assets;
        try {
            root=std::filesystem::absolute(openPath_).lexically_normal();assets=root/"assets";
            if (!std::filesystem::is_directory(root)) {throw std::invalid_argument("Choose an existing workspace folder.");}
            std::filesystem::create_directories(assets);std::filesystem::create_directories(root/"scenes");
        } catch (const std::exception& e) {
            ShowError({ErrorCode::SceneIo,e.what(),"Choose a writable project folder; the previous Scene was retained.",openPath_});break;
        }
        Document.New();selectedAsset_=0;controls_.Reset();welcome_=false;
        projectRoot_=std::move(root);directory_=std::move(assets);PersistPreferences();
        status_="Workspace: "+Text(projectRoot_);Record(ConsoleLevel::Info,status_,projectRoot_);
        break;
    }
    case Action::Quit: quit_ = true; break;
    default: break;
    }
    pending_ = Action::None;
}
void EditorUI::FileDialog(Dialog type) {
    controls_.Reset();cameraDrag_=false;dialog_=type;
    FilePickerRequest request;request.Directory=directory_;
    request.Title=type == Dialog::QuickOpen ? "Open Model":type == Dialog::Import ? "Import Assets":type == Dialog::Open ? "Open Scene":type == Dialog::Save ? "Save Scene As":type == Dialog::Project ? "Choose a project workspace folder":"Minvex";
    const bool save=type == Dialog::Save || type == Dialog::PoseFull || type == Dialog::PosePartial || type == Dialog::QualitySave || type == Dialog::TelemetryJson || type == Dialog::TelemetryCsv;
    request.Mode=save ? FilePickerMode::Save:type == Dialog::Import ? FilePickerMode::Multiple:type == Dialog::Project ? FilePickerMode::Folder:FilePickerMode::Open;
    request.Filter=(type == Dialog::QuickOpen || type == Dialog::Import) ? FilePickerFilter::Model:type == Dialog::Open || type == Dialog::Save ? FilePickerFilter::Scene:type == Dialog::PoseFull || type == Dialog::PosePartial || type == Dialog::PoseLoad ? FilePickerFilter::Pose:type == Dialog::QualitySave || type == Dialog::QualityLoad ? FilePickerFilter::Quality:type == Dialog::TelemetryCsv ? FilePickerFilter::Csv:FilePickerFilter::Json;
    request.Name=type == Dialog::Save ? (Document.Path().empty() ? "Scene.mvxscene.json":Text(Document.Path().filename())):type == Dialog::QualitySave ? "Profile.mvxquality.json":type == Dialog::TelemetryCsv ? "Telemetry.csv":type == Dialog::TelemetryJson ? "Telemetry.json":"Pose.mvxpose.json";
    if (type == Dialog::Save && !Document.Path().empty()) {request.Directory=Document.Path().parent_path();}
    else if (type == Dialog::Save && !projectRoot_.empty()) {request.Directory=projectRoot_/"scenes";}
    if (type == Dialog::PoseFull || type == Dialog::PosePartial || type == Dialog::PoseLoad) {poseObject_=Document.Selection();}
    const auto selected=picker_ ? picker_(request):nativeFiles_.Choose(request);dialog_=Dialog::None;controls_.Reset();
    if (!Report(selected)) {return;}
    if (!selected.Value()) {if (save) {pending_=Action::None;}return;}
    const auto paths=*selected.Value();if (paths.empty()) {return;}
    if (save) {if (SaveDialog(type,paths.front()) && type != Dialog::Save) {directory_=paths.front().parent_path();PersistPreferences();}return;}
    if (type == Dialog::QuickOpen) {RequestQuickOpen(paths.front());}
    else if (type == Dialog::Open) {RequestOpen(paths.front());}
    else if (type == Dialog::Project) {openPath_=paths.front();Ask(Action::Project);}
    else if (type == Dialog::PoseLoad) {Report(Document.LoadPoseFile(poseObject_,paths.front()),poseObject_);}
    else if (type == Dialog::QualityLoad) {LoadQualityProfile(paths.front());}
    else {for (const auto& path:paths) {Import(path,false);}}
}
void EditorUI::ChooseFiles(EditorFileAction action) {FileDialog(action == EditorFileAction::OpenModel ? Dialog::QuickOpen:action == EditorFileAction::OpenScene ? Dialog::Open:action == EditorFileAction::ImportAssets ? Dialog::Import:action == EditorFileAction::NewProject ? Dialog::Project:Dialog::Save);}
void EditorUI::ResolvePending(UnsavedChoice choice) {if (choice == UnsavedChoice::Cancel) {pending_=Action::None;}else if (choice == UnsavedChoice::Discard) {ApplyPending();}else if (Document.Path().empty()) {FileDialog(Dialog::Save);}else {Save(Document.Path());}}
bool EditorUI::SaveDialog(Dialog type, const std::filesystem::path& path) {
    if (type == Dialog::Save) { return Save(path); }
    if (type == Dialog::QualitySave) {return Report(SaveQuality(qualityDraft_,path));}
    if (type == Dialog::TelemetryJson || type == Dialog::TelemetryCsv) {return Report(SaveTelemetry(Viewport.Telemetry(),path,type == Dialog::TelemetryJson ? TelemetryFormat::Json:TelemetryFormat::Csv));}
    if (!Report(Document.SavePoseFile(poseObject_,path,type == Dialog::PosePartial))) { return false; }
    status_ = "Saved Pose " + Text(path); return true;
}
void EditorUI::LoadQualityProfile(const std::filesystem::path& path) {
    auto profile=LoadQuality(path);if (Report(profile)) {qualityDraft_=std::move(profile).Value();quality_=true;status_="Loaded quality profile; Apply to use it.";}
}
Result<void> EditorUI::ApplyQualityProfile(const QualityProfile& profile) {
    auto result=Viewport.SetQuality(profile,Document.GetScene());if (result) {qualityDraft_=profile;}return result;
}
bool EditorUI::Save(const std::filesystem::path& path) {
    if (!Report(Document.Save(path))) { return false; }
    status_ = "Saved " + Text(Document.Path());
    Remember(Document.Path(),RecentKind::Scene);Record(ConsoleLevel::Info,status_,Document.Path());
    if (pending_ != Action::None) { ApplyPending(); }
    return true;
}
void EditorUI::Menu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Model...","Ctrl+O")) { FileDialog(Dialog::QuickOpen); }
            if (ImGui::MenuItem("Open Scene...","Ctrl+Shift+O")) { FileDialog(Dialog::Open); }
            if (ImGui::MenuItem("Import Assets...")) { FileDialog(Dialog::Import); }
            ImGui::Separator();
            if (ImGui::MenuItem("New Scene","Ctrl+N")) { RequestNew(); }
            if (ImGui::MenuItem("New Project (workspace)...")) {FileDialog(Dialog::Project);}
            if (ImGui::MenuItem("Save","Ctrl+S")) { if (Document.Path().empty()) { FileDialog(Dialog::Save); } else { Save(Document.Path()); } }
            if (ImGui::MenuItem("Save As...","Ctrl+Shift+S")) { FileDialog(Dialog::Save); }
            ImGui::Separator();
            if (ImGui::BeginMenu("Recent Files",!preferences_.Recent.empty())) {
                std::optional<RecentFile> open;
                for (std::size_t i=0;i<preferences_.Recent.size();++i) {const auto& recent=preferences_.Recent[i];ImGui::PushID(static_cast<int>(i));if (ImGui::MenuItem(Text(recent.Path).c_str())) {open=recent;}ImGui::PopID();}
                ImGui::EndMenu();
                if (open) {if (open->Kind == RecentKind::Model) {RequestQuickOpen(open->Path);}else {RequestOpen(open->Path);}}
            }
            if (ImGui::MenuItem("Quit")) { RequestQuit(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Render Debugger",nullptr,&renderDebugger_);
            ImGui::MenuItem("Animation Debugger",nullptr,&animationDebugger_);
            if (ImGui::MenuItem("Reset debug display")) {Report(Viewport.SetDebugOptions({}));editorDebugDraw_.Clear();Report(Viewport.SetDebugDraw(editorDebugDraw_));}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo","Ctrl+Z",false,Document.CanUndo())) {Report(Document.Undo());}
            if (ImGui::MenuItem("Redo","Ctrl+Y / Ctrl+Shift+Z",false,Document.CanRedo())) {Report(Document.Redo());}
            if (ImGui::MenuItem("Clear Undo History",nullptr,false,Document.CanUndo() || Document.CanRedo())) {Report(Document.ClearUndoHistory());}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Viewport",nullptr,&viewport_); ImGui::MenuItem("Hierarchy",nullptr,&hierarchy_);
            ImGui::MenuItem("UI Designer",nullptr,&uiDesigner_);
            ImGui::MenuItem("Inspector",nullptr,&inspector_); ImGui::MenuItem("Asset Browser",nullptr,&assets_);
            ImGui::MenuItem("Bone Inspector",nullptr,&bones_); ImGui::MenuItem("Animation Controls",nullptr,&animation_);
            ImGui::MenuItem("Quality Settings",nullptr,&quality_);
            ImGui::MenuItem("Performance",nullptr,&performance_);
            ImGui::MenuItem("Console",nullptr,&console_);
            ImGui::MenuItem("Asset Inspector",nullptr,&assetInspector_);
            ImGui::MenuItem("Welcome",nullptr,&welcome_);
            if (ImGui::MenuItem("Reset layout")) { resetLayout_ = true; viewport_ = hierarchy_ = inspector_ = assets_ = bones_ = animation_ = quality_ = performance_ = console_ = assetInspector_ = true; }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::TextUnformatted(Document.Path().empty() ? "Untitled" : Text(Document.Path().filename()).c_str());
        if (Document.IsDirty()) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
        ImGui::EndMainMenuBar();
    }
    const auto& io = ImGui::GetIO();
    if ((io.KeyCtrl || io.KeySuper) && !io.WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z,false)) {if (io.KeyShift) {if (Document.CanRedo()) {Report(Document.Redo());}}else if (Document.CanUndo()) {Report(Document.Undo());}}
        if (ImGui::IsKeyPressed(ImGuiKey_Y,false) && Document.CanRedo()) {Report(Document.Redo());}
        if (ImGui::IsKeyPressed(ImGuiKey_S,false)) {
            if (io.KeyShift || Document.Path().empty()) { FileDialog(Dialog::Save); } else { Save(Document.Path()); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O,false)) { FileDialog(io.KeyShift ? Dialog::Open:Dialog::QuickOpen); }
        if (ImGui::IsKeyPressed(ImGuiKey_N,false)) { RequestNew(); }
    }
}
void EditorUI::Dockspace() {
    const auto dock = ImHashStr("MinvexMainDockspace");
    const bool existed = ImGui::DockBuilderGetNode(dock) != nullptr;
    ImGui::DockSpaceOverViewport(dock,ImGui::GetMainViewport());
    if (resetLayout_ || !existed) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock,ImGui::GetMainViewport()->WorkSize);
        auto center = dock;
        const auto left = ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.19f,nullptr,&center);
        const auto right = ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.29f,nullptr,&center);
        const auto bottom = ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,0.30f,nullptr,&center);
        ImGui::DockBuilderDockWindow("Hierarchy",left); ImGui::DockBuilderDockWindow("Inspector",right);
        ImGui::DockBuilderDockWindow("UI Designer",right);
        ImGui::DockBuilderDockWindow("Asset Browser",bottom); ImGui::DockBuilderDockWindow("Viewport",center);
        ImGui::DockBuilderDockWindow("Bone Inspector",right); ImGui::DockBuilderDockWindow("Animation Controls",bottom);
        ImGui::DockBuilderDockWindow("Console",bottom);
        ImGui::DockBuilderDockWindow("Asset Inspector",right);
        ImGui::DockBuilderDockWindow("Quality Settings",right);
        ImGui::DockBuilderDockWindow("Performance",right);
        ImGui::DockBuilderDockWindow("Render Debugger",right);ImGui::DockBuilderDockWindow("Animation Debugger",right);
        ImGui::DockBuilderFinish(dock);
    }
    // Add Phase 5 panels to a saved Phase 4 layout without resetting user splits.
    for (const auto& panel : {std::pair{"Bone Inspector","Inspector"},std::pair{"Animation Controls","Asset Browser"},std::pair{"Quality Settings","Inspector"},std::pair{"Performance","Inspector"},std::pair{"Console","Asset Browser"},std::pair{"Asset Inspector","Inspector"},std::pair{"Render Debugger","Inspector"},std::pair{"Animation Debugger","Inspector"}}) {
        if (!ImGui::FindWindowSettingsByID(ImHashStr(panel.first))) {
            const auto* anchor = ImGui::FindWindowSettingsByID(ImHashStr(panel.second));
            ImGui::DockBuilderDockWindow(panel.first,anchor && anchor->DockId ? anchor->DockId : dock);
        }
    }
}
void EditorUI::Hierarchy() {
    if (!hierarchy_) { return; }
    if (ImGui::Begin("Hierarchy",&hierarchy_)) {
        if (ImGui::Button("+ Empty")) { Report(Document.CreateObject()); }
        ImGui::SameLine(); if (ImGui::Button("Root")) { if (Document.Selection()) { Report(Document.SetParent(Document.Selection(),NoObject)); } }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* p = ImGui::AcceptDragDropPayload("MVX_OBJECT")) { Report(Document.SetParent(*static_cast<const ObjectId*>(p->Data),NoObject)); }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();
        std::unordered_map<ObjectId,std::vector<ObjectId>> children;
        for (auto id : Document.GetScene().ObjectIds()) { children[Document.GetScene().GetObject(id).Parent].push_back(id); }
        ImGui::InputTextWithHint("##hierarchy-search","Search object names...",&hierarchyFilter_);
        const auto lower=[](std::string value) {for (auto& c:value) {c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}return value;};
        std::unordered_set<ObjectId> matched;
        if (!hierarchyFilter_.empty()) {const auto query=lower(hierarchyFilter_);for (auto id:Document.GetScene().ObjectIds()) {if (lower(Document.GetScene().GetObject(id).Name).find(query) != std::string::npos) {for (auto parent=id;parent;parent=Document.GetScene().GetObject(parent).Parent) {matched.insert(parent);}}}}
        std::vector<ObjectId> stack(children[NoObject].rbegin(),children[NoObject].rend());
        while (!stack.empty()) {
            const auto id = stack.back(); stack.pop_back();
            if (!id) { ImGui::TreePop(); continue; }
            if (!hierarchyFilter_.empty() && !matched.contains(id)) {continue;}
            const auto& object = Document.GetScene().GetObject(id);
            Id(id);
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (Document.Selection() == id) { flags |= ImGuiTreeNodeFlags_Selected; }
            if (children[id].empty()) { flags |= ImGuiTreeNodeFlags_Leaf; }
            const std::string label = (object.Visible ? "" : "[hidden] ") + (object.Name.empty() ? "Object" : object.Name);
            if (!hierarchyFilter_.empty()) {ImGui::SetNextItemOpen(true,ImGuiCond_Always);}
            const bool open = ImGui::TreeNodeEx("node",flags,"%s",label.c_str());
            if (ImGui::IsItemClicked()) { Report(Document.Select(id)); }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("MVX_OBJECT",&id,sizeof(id)); ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_OBJECT")) { Report(Document.SetParent(*static_cast<const ObjectId*>(p->Data),id)); }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
            if (open) { stack.push_back(NoObject); for (auto it = children[id].rbegin(); it != children[id].rend(); ++it) { stack.push_back(*it); } }
        }
    }
    ImGui::End();
}
void EditorUI::Inspector() {
    if (!inspector_) { return; }
    if (ImGui::Begin("Inspector",&inspector_)) {
        const auto id = Document.Selection();
        if (id && Document.GetScene().Contains(id)) {
            const auto object = Document.GetScene().GetObject(id);
            auto name = object.Name; if (ImGui::InputText("Name",&name)) { Report(Document.RenameObject(id,std::move(name))); }
            bool visible = object.Visible; if (ImGui::Checkbox("Visible",&visible)) { Report(Document.SetVisible(id,visible)); }
            if (ImGui::BeginCombo("Parent",object.Parent ? Document.GetScene().GetObject(object.Parent).Name.c_str() : "<root>")) {
                if (ImGui::Selectable("<root>",!object.Parent)) { Report(Document.SetParent(id,NoObject)); }
                for (auto parent : Document.GetScene().ObjectIds()) { if (parent == id) { continue; } Id(parent);
                    if (ImGui::Selectable(Document.GetScene().GetObject(parent).Name.c_str(),object.Parent == parent)) { Report(Document.SetParent(id,parent)); } ImGui::PopID(); }
                ImGui::EndCombo();
            }
            auto t = object.LocalTransform;
            bool changed = VecInput("Position",t.Position); changed = RotationInput(t.Rotation) || changed; changed = VecInput("Scale",t.Scale,0.01f) || changed;
            if (changed) { Report(Document.SetTransform(id,t)); }
            ImGui::TextWrapped("%s",object.Model ? Text(object.Model->SourcePath.filename()).c_str() : "Empty object");
            EquipmentControls(id);
            if (ImGui::Button("Delete subtree...")) { deleteObject_=id; }
            if (object.Model && ImGui::CollapsingHeader("Materials")) {
                ImGui::TextUnformatted("PBR uses Advanced lighting + PBR quality. Direct lights; no IBL.");
                for (std::size_t index=0;index<object.Model->Materials.size();++index) {ImGui::PushID(static_cast<int>(index));auto material=Document.GetScene().GetMaterial(id,index);
                    const auto label=material.Name.empty() ? "Material "+std::to_string(index):material.Name;
                    if (ImGui::TreeNode("Material","%zu: %s",index,label.c_str())) {
                        bool materialChanged=false;int shading=material.Unlit ? 1:static_cast<int>(material.Shading);
                        if (ImGui::Combo("Shading",&shading,"Basic\0Unlit\0PBR\0")) {material.Shading=static_cast<ShadingModel>(shading);material.Unlit=shading == 1;materialChanged=true;}
                        materialChanged=ImGui::ColorEdit4("Base color",material.BaseColor.data()) || materialChanged;
                        materialChanged=ImGui::SliderFloat("Metallic",&material.MetallicFactor,0,1) || materialChanged;materialChanged=ImGui::SliderFloat("Roughness",&material.RoughnessFactor,0,1) || materialChanged;
                        materialChanged=ImGui::SliderFloat("Normal scale",&material.NormalScale,-16,16) || materialChanged;materialChanged=ImGui::Checkbox("Double sided",&material.DoubleSided) || materialChanged;
                        const auto texture=[&](const char* name,std::optional<std::size_t>& slot) {const auto preview=slot ? std::to_string(*slot):"<none>";bool edit=false;if (ImGui::BeginCombo(name,preview.c_str())) {if (ImGui::Selectable("<none>",!slot)) {slot.reset();edit=true;}for (std::size_t i=0;i<object.Model->Textures.size();++i) {const auto image=object.Model->Textures[i].ImageIndex;if (image>=object.Model->Images.size() || object.Model->Images[image].Rgba.empty()) {continue;}const auto text=std::to_string(i);if (ImGui::Selectable(text.c_str(),slot == i)) {slot=i;edit=true;}}ImGui::EndCombo();}return edit;};
                        materialChanged=texture("Base texture",material.BaseColorTexture) || materialChanged;materialChanged=texture("Metallic/Roughness texture",material.MetallicRoughnessTexture) || materialChanged;materialChanged=texture("Normal texture",material.NormalTexture) || materialChanged;
                        if (materialChanged) {Report(Document.SetMaterial(id,index,material));}if (ImGui::Button("Reset material")) {Report(Document.ResetMaterial(id,index));}ImGui::TreePop();
                    }ImGui::PopID();
                }
            }
            ImGui::Separator();
        } else { ImGui::TextUnformatted("Select an object in Hierarchy."); }
        if (id && Document.GetScene().Contains(id)) {const auto model=Document.GetScene().GetObject(id).Model;if (model && ImGui::CollapsingHeader("Morph Targets")) {
            bool found=false;for (std::size_t i=0;i<model->Nodes.size();++i) {const auto& node=model->Nodes[i];if (!node.MeshIndex || model->Meshes[*node.MeshIndex].MorphWeights.empty()) {continue;}found=true;const auto joint=static_cast<JointId>(i);auto weights=Document.GetScene().GetObject(id).Animation.EvaluateMorphWeights(joint);ImGui::PushID(static_cast<int>(i));ImGui::Text("Node %zu: %s",i,node.Name.c_str());bool edited=false;
                for (std::size_t t=0;t<weights.size();++t) {ImGui::PushID(static_cast<int>(t));const auto& name=model->Meshes[*node.MeshIndex].Primitives[0].MorphTargets[t].Name;edited=ImGui::SliderFloat(name.c_str(),&weights[t],-1,1,"%.3f") || edited;ImGui::PopID();}
                if (edited) {Report(Document.SetMorphWeights(id,joint,weights));}if (ImGui::Button("Reset morph override")) {Report(Document.ClearMorphWeights(id,joint));}ImGui::PopID();
            }ImGui::TextUnformatted(found ? "Overrides apply after animation. Reset resumes animated/default weights.":"This model has no morph targets.");
        }}
        if (ImGui::CollapsingHeader("Camera")) {
            auto camera = Document.GetScene().MainCamera;
            bool changed = VecInput("Eye",camera.Position); changed = VecInput("Target",camera.Target) || changed;
            int projection=static_cast<int>(camera.Projection);if (ImGui::Combo("Projection",&projection,"Perspective\0Orthographic\0")) {camera.Projection=static_cast<CameraProjection>(projection);changed=true;}
            if (camera.Projection == CameraProjection::Orthographic) {changed=ImGui::DragFloat("Orthographic height",&camera.OrthographicHeight,.05f,.0001f,1e8f) || changed;}
            changed = VecInput("Up",camera.Up) || changed;
            float fov = camera.VerticalFovRadians*180/std::numbers::pi_v<float>;
            if (ImGui::DragFloat("FOV (deg)",&fov,0.2f,1,175)) { camera.VerticalFovRadians = fov*std::numbers::pi_v<float>/180; changed = true; }
            changed = ImGui::DragFloat("Near",&camera.NearPlane,0.01f) || changed; changed = ImGui::DragFloat("Far",&camera.FarPlane,1) || changed;
            if (changed) { Report(Document.SetCamera(camera)); }
            if (ImGui::Button("Focus selected")) { FrameScene(); }
        }
        if (ImGui::CollapsingHeader("Directional Light")) {
            auto light = Document.GetScene().Sun;
            bool changed = VecInput("Direction",light.Direction);
            float color[]{light.Color.X,light.Color.Y,light.Color.Z};
            if (ImGui::ColorEdit3("Color",color)) { light.Color = {color[0],color[1],color[2]}; changed = true; }
            changed = ImGui::DragFloat("Intensity",&light.Intensity,0.01f,0,10) || changed;
            changed = ImGui::DragFloat("Ambient",&light.Ambient,0.01f,0,10) || changed;
            if (changed) { Report(Document.SetLight(light)); }
        }
        if (ImGui::CollapsingHeader("Point / Spot Lights (Advanced)")) {
            auto points=Document.GetScene().PointLights;auto spots=Document.GetScene().SpotLights;bool changed=false;
            if (points.size()+spots.size() < MaxLocalLights) {if (ImGui::Button("+ Point")) {points.push_back({});changed=true;}ImGui::SameLine();if (ImGui::Button("+ Spot")) {spots.push_back({});changed=true;}}
            const auto edit=[&](auto& l) {bool c=VecInput("Position",l.Position);float color[]{l.Color.X,l.Color.Y,l.Color.Z};if (ImGui::ColorEdit3("Color",color)) {l.Color={color[0],color[1],color[2]};c=true;}c=ImGui::DragFloat("Intensity",&l.Intensity,.05f,0,100) || c;c=ImGui::DragFloat("Range",&l.Range,.1f,.01f,10000) || c;return c;};
            for (std::size_t i=0;i<points.size();++i) {ImGui::PushID(static_cast<int>(i));if (ImGui::TreeNode("Point","Point %zu",i)) {changed=edit(points[i]) || changed;if (ImGui::Button("Remove")) {points.erase(points.begin()+static_cast<std::ptrdiff_t>(i));changed=true;ImGui::TreePop();ImGui::PopID();break;}ImGui::TreePop();}ImGui::PopID();}
            for (std::size_t i=0;i<spots.size();++i) {ImGui::PushID(static_cast<int>(i)+100);if (ImGui::TreeNode("Spot","Spot %zu",i)) {changed=edit(spots[i]) || changed;changed=VecInput("Direction",spots[i].Direction) || changed;changed=ImGui::SliderFloat("Inner cone",&spots[i].InnerConeRadians,0,1.55f) || changed;changed=ImGui::SliderFloat("Outer cone",&spots[i].OuterConeRadians,.01f,1.56f) || changed;if (ImGui::Button("Remove")) {spots.erase(spots.begin()+static_cast<std::ptrdiff_t>(i));changed=true;ImGui::TreePop();ImGui::PopID();break;}ImGui::TreePop();}ImGui::PopID();}
            if (changed) {Report(Document.SetLocalLights(std::move(points),std::move(spots)));}
        }
    }
    ImGui::End();
}
void EditorUI::BoneInspector() {
    if (!bones_) { return; }
    if (ImGui::Begin("Bone Inspector",&bones_)) {
        const auto object = Document.Selection();
        if (!object || !Document.GetScene().GetObject(object).Model) { ImGui::TextUnformatted("Select a model instance in Hierarchy."); }
        else {
            const auto& instance = Document.GetScene().GetObject(object); const auto& skeleton = instance.Model->Skeleton;
            if (boneObject_ != object) { boneObject_ = object; bone_ = NoJoint; }
            if (bone_ >= skeleton.Joints.size()) {
                for (std::size_t i=0;i<skeleton.Joints.size();++i) { if (skeleton.Joints[i].IsJoint) { bone_ = static_cast<JointId>(i); break; } }
                if (bone_ == NoJoint && !skeleton.Joints.empty()) { bone_ = 0; }
            }
            ImGui::Text("%zu skins | %zu transform nodes",instance.Model->Skins.size(),skeleton.Joints.size());
            if (instance.LeaderPose) { ImGui::TextWrapped("Following Leader Pose. Bone edits affect the dormant independent player; edit the leader for the displayed pose."); }
            if (bone_ != NoJoint && ImGui::BeginCombo("Joint / Node",skeleton.Joints[bone_].Name.c_str())) {
                for (std::size_t i=0;i<skeleton.Joints.size();++i) { ImGui::PushID(static_cast<int>(i));
                    const auto& joint = skeleton.Joints[i]; const auto label = (joint.IsJoint ? "[bone] " : "[node] ") + (joint.Name.empty() ? std::to_string(i) : joint.Name);
                    if (ImGui::Selectable(label.c_str(),bone_ == i)) { bone_ = static_cast<JointId>(i); } ImGui::PopID(); }
                ImGui::EndCombo();
            }
            if (bone_ != NoJoint) {
                const auto& joint = skeleton.Joints[bone_];
                ImGui::Text("Joint ID: %u",bone_);
                ImGui::TextWrapped("Parent: %s",joint.Parent == NoJoint ? "<root>" : skeleton.Joints[joint.Parent].Name.c_str());
                if (!joint.Editable) { ImGui::TextWrapped("Matrix contains shear; TRS editing is unavailable."); }
                else {
                    const auto pose = instance.Animation.CapturePose();
                    auto local = std::find_if(pose.Joints.begin(),pose.Joints.end(),[&](const auto& p) { return p.Joint == bone_; })->Local;
                    ImGui::TextWrapped("Local TRS overrides replace animation on this node.");
                    bool edited = VecInput("Position",local.Position); edited = RotationInput(local.Rotation) || edited; edited = VecInput("Scale",local.Scale,0.01f) || edited;
                    if (edited) { Report(Document.SetJointPose(object,bone_,local)); }
                    const auto& overrides = instance.Animation.Overrides().Joints;
                    ImGui::Text("Override: %s",std::any_of(overrides.begin(),overrides.end(),[&](const auto& p) { return p.Joint == bone_; }) ? "enabled" : "none");
                    if (ImGui::Button("Clear joint override")) { Report(Document.ClearJointPose(object,bone_)); }
                }
            }
            if (ImGui::Button("Clear all overrides")) { Report(Document.ClearPoseOverrides(object)); }
            if (ImGui::Button("Reference Pose")) { Report(Document.ResetPose(object)); }
            ImGui::Separator();
            if (ImGui::Button("Save full Pose...")) { FileDialog(Dialog::PoseFull); }
            if (ImGui::Button("Save overrides...")) { FileDialog(Dialog::PosePartial); }
            if (ImGui::Button("Load Pose...")) { FileDialog(Dialog::PoseLoad); }
        }
    } ImGui::End();
}
void EditorUI::AnimationControls() {
    if (!animation_) { return; }
    if (ImGui::Begin("Animation Controls",&animation_)) {
        const auto object = Document.Selection();
        if (!object || !Document.GetScene().GetObject(object).Model) { ImGui::TextUnformatted("Select a model instance in Hierarchy."); }
        else {
            // Failed Document edits restore snapshots, invalidating Scene references.
            const auto model=Document.GetScene().GetObject(object).Model;const auto& clips=model->Animations;
            const auto& player=Document.GetScene().GetObject(object).Animation;
            const auto clip=player.Clip();const auto layer=player.Additive();const auto playback=layer && layer->State==PlaybackState::Playing ? PlaybackState::Playing:player.State()==PlaybackState::Stopped && layer ? layer->State:player.State();const auto overrides=player.Overrides().Joints.size();
            const auto tree=player.Tree();
            double time=player.Time();float speed=static_cast<float>(player.Speed());bool loop=player.Loop();
            const auto blending=player.Blend().has_value();const auto weight=player.BlendWeight();const auto elapsed=blending ? player.Blend()->Elapsed:0,durationBlend=blending ? player.Blend()->Duration:0;
            if (clips.empty()) { ImGui::TextUnformatted("This model has no animation clips. Pose editing remains available."); }
            else {
                ImGui::SliderFloat("Cross-fade (s)",&crossFadeSeconds_,0,2,"%.2f");
                if (ImGui::BeginCombo("Clip",clip ? clips[*clip].Name.c_str() : tree ? "<Blend Tree>":"<none>")) {
                    for (std::size_t i=0;i<clips.size();++i) { ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(clips[i].Name.c_str(),clip == i)) { Report(Document.CrossFadeAnimation(object,i,crossFadeSeconds_)); } ImGui::PopID(); }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Play / Restart")) {if (tree) {Report(Document.PlayBlendTree(object,tree->Tree));}else { Report(Document.PlayAnimation(object,clip.value_or(0))); }}
                ImGui::SameLine(); if (ImGui::Button(playback == PlaybackState::Playing ? "Pause" : "Resume")) {
                    if (playback == PlaybackState::Playing) { Report(Document.PauseAnimation(object)); }
                    else if (clip || tree || layer) { Report(Document.ResumeAnimation(object)); } else { Report(Document.PlayAnimation(object,0)); }
                }
                ImGui::SameLine(); if (ImGui::Button("Stop")) { Report(Document.StopAnimation(object)); }
                if (clip) {
                    double zero = 0, duration = clips[*clip].Duration;
                    if (ImGui::SliderScalar("Time",ImGuiDataType_Double,&time,&zero,&duration,"%.3f s")) { Report(Document.SeekAnimation(object,time)); }
                }
                if (tree) {double phase=tree->Phase,zero=0,one=1;if (ImGui::SliderScalar("Tree phase",ImGuiDataType_Double,&phase,&zero,&one,"%.3f")) {Report(Document.SeekBlendTree(object,phase));}}
                if (ImGui::DragFloat("Speed",&speed,0.05f,-100,100)) { Report(Document.SetAnimationSpeed(object,speed)); }
                if (ImGui::Checkbox("Loop",&loop)) { Report(Document.SetAnimationLoop(object,loop)); }
                ImGui::Text("%s | %zu joint overrides",playback == PlaybackState::Playing ? "Playing" : playback == PlaybackState::Paused ? "Paused" : "Stopped",overrides);
                if (blending) {ImGui::ProgressBar(weight);ImGui::Text("Cross-fade: %.2f / %.2f s",elapsed,durationBlend);}
                BlendTreeControls(object,model);
                AdditiveControls(object,model);
                EventControls(object,model);
            }
        }
    } ImGui::End();
}
void EditorUI::BlendTreeControls(ObjectId object,ModelHandle model) {
    if (!ImGui::CollapsingHeader("Blend Tree")) {return;}const auto active=Document.GetScene().GetObject(object).Animation.Tree();const auto source=active ? active->Tree:BlendTreeHandle{};
    if (treeObject_!=object || treeModel_!=model || treeDraftSource_!=source) {treeObject_=object;treeModel_=model;treeDraftSource_=source;treeDraft_=source ? source->Description():BlendTreeDesc{};if (!source) {for (std::size_t i=0;i<std::min<std::size_t>(model->Animations.size(),2);++i) {treeDraft_.Samples.push_back({i,static_cast<float>(i),0});}}}
    const auto text=[](const char* label,std::string& value) {char buffer[257]{};std::copy_n(value.c_str(),std::min<std::size_t>(value.size(),256),buffer);if (ImGui::InputText(label,buffer,sizeof(buffer))) {value=buffer;}};
    text("Tree name",treeDraft_.Name);int kind=treeDraft_.Kind==BlendTreeKind::Blend1D ? 0:1;
    if (ImGui::Combo("Tree type",&kind,"Blend1D\0Blend2D\0")) {treeDraft_.Kind=kind==0 ? BlendTreeKind::Blend1D:BlendTreeKind::Blend2D;treeDraft_.Samples.clear();for (std::size_t i=0;i<(kind==0 ? 2u:3u);++i) {treeDraft_.Samples.push_back({std::min(i,model->Animations.size()-1),i==1 ? 1.0f:0.0f,i==2 ? 1.0f:0.0f});}}
    text("Parameter X",treeDraft_.ParameterX);const bool two=treeDraft_.Kind==BlendTreeKind::Blend2D;if (two) {text("Parameter Y",treeDraft_.ParameterY);}
    std::optional<std::size_t> remove;
    for (std::size_t i=0;i<treeDraft_.Samples.size();++i) {auto& sample=treeDraft_.Samples[i];ImGui::PushID(static_cast<int>(i));ImGui::Text("Sample %zu",i);
        if (ImGui::BeginCombo("Animation",model->Animations.at(sample.Clip).Name.c_str())) {for (std::size_t clip=0;clip<model->Animations.size();++clip) {ImGui::PushID(static_cast<int>(clip));if (ImGui::Selectable(model->Animations[clip].Name.c_str(),sample.Clip==clip)) {sample.Clip=clip;}ImGui::PopID();}ImGui::EndCombo();}
        ImGui::DragFloat(two ? "X":"Threshold",&sample.X,.02f);if (two) {ImGui::DragFloat("Y",&sample.Y,.02f);}if (treeDraft_.Samples.size()>(two ? 3u:1u) && ImGui::Button("Remove sample")) {remove=i;}ImGui::PopID();
    }
    if (remove) {treeDraft_.Samples.erase(treeDraft_.Samples.begin()+static_cast<std::ptrdiff_t>(*remove));}
    if (treeDraft_.Samples.size()<MaxBlendSamples && ImGui::Button("Add sample")) {const auto x=treeDraft_.Samples.back().X+1;treeDraft_.Samples.push_back({0,x,two ? .5f:0.0f});}
    if (ImGui::Button("Apply / Play tree")) {try {auto tree=CreateBlendTree(*model,treeDraft_);Report(Document.PlayBlendTree(object,std::move(tree)));}catch (const std::exception& e) {Record(ConsoleLevel::Error,e.what());}}
    if (!active) {return;}const auto& desc=active->Tree->Description();float x=active->X,y=active->Y;
    ImGui::PushID("Tree parameters");if (ImGui::DragFloat(desc.ParameterX.c_str(),&x,.02f)) {Report(Document.SetBlendParameter(object,desc.ParameterX,x));}if (desc.Kind==BlendTreeKind::Blend2D && ImGui::DragFloat(desc.ParameterY.c_str(),&y,.02f)) {Report(Document.SetBlendParameter(object,desc.ParameterY,y));}ImGui::PopID();
    const auto weights=EvaluateBlendWeights(*active->Tree,active->X,active->Y);ImGui::TextUnformatted("Active sample weights");
    for (std::size_t i=0;i<weights.size();++i) {ImGui::Text("%zu: %s  %.3f",i,model->Animations[desc.Samples[i].Clip].Name.c_str(),weights[i]);}
    if (desc.Kind!=BlendTreeKind::Blend2D) {return;}
    const auto origin=ImGui::GetCursorScreenPos();const ImVec2 size{std::max(120.0f,ImGui::GetContentRegionAvail().x),170};ImGui::InvisibleButton("Triangulation",size);auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(25,29,35,255));
    double minX=desc.Samples[0].X,maxX=minX,minY=desc.Samples[0].Y,maxY=minY;for (const auto& s:desc.Samples) {minX=std::min(minX,double(s.X));maxX=std::max(maxX,double(s.X));minY=std::min(minY,double(s.Y));maxY=std::max(maxY,double(s.Y));}
    const auto point=[&](float px,float py){return ImVec2{origin.x+10+static_cast<float>(std::clamp((double(px)-minX)/(maxX-minX),0.0,1.0))*(size.x-20),origin.y+size.y-10-static_cast<float>(std::clamp((double(py)-minY)/(maxY-minY),0.0,1.0))*(size.y-20)};};
    for (const auto& t:active->Tree->Triangles()) {for (int k=0;k<3;++k) {const auto& a=desc.Samples[t[k]];const auto& b=desc.Samples[t[(k+1)%3]];draw->AddLine(point(a.X,a.Y),point(b.X,b.Y),IM_COL32(130,145,160,255));}}
    for (std::size_t i=0;i<desc.Samples.size();++i) {const auto p=point(desc.Samples[i].X,desc.Samples[i].Y);draw->AddCircleFilled(p,3+weights[i]*4,IM_COL32(90,180,240,255));draw->AddText(p,IM_COL32_WHITE,std::to_string(i).c_str());}draw->AddCircleFilled(point(active->X,active->Y),4,IM_COL32(255,135,90,255));
}
void EditorUI::QualitySettings() {
    if (!quality_) {return;}
    if (ImGui::Begin("Quality Settings",&quality_)) {
        ImGui::InputText("Profile name",&qualityDraft_.Name);
        int mode=qualityDraft_.IsCustom() ? 3:static_cast<int>(qualityDraft_.Base);
        if (ImGui::Combo("Profile",&mode,"Low\0Medium\0High\0Custom\0")) {
            if (mode < 3) {qualityDraft_=MakeQualityProfile(static_cast<QualityPreset>(mode));}
            else {const auto s=ExpandQuality(qualityDraft_);if (s) {qualityDraft_.Name="Custom";qualityDraft_.Overrides.TargetFps=s.Value().TargetFps;}}
        }
        if (qualityDraft_.IsCustom()) {int base=static_cast<int>(qualityDraft_.Base);if (ImGui::Combo("Inherit preset",&base,"Low\0Medium\0High\0")) {qualityDraft_.Base=static_cast<QualityPreset>(base);}}
        const auto expanded=ExpandQuality(qualityDraft_);
        if (expanded) {
            auto s=expanded.Value();auto& o=qualityDraft_.Overrides;
            int resolution=static_cast<int>(s.Resolution);if (ImGui::Combo("Resolution",&resolution,"Relative\0Fixed\0")) {o.Resolution=static_cast<ResolutionMode>(resolution);}
            if (s.Resolution == ResolutionMode::Fixed) {
                if (ImGui::DragInt("Width",&s.FixedWidth,8,1,16384)) {o.FixedWidth=s.FixedWidth;}
                if (ImGui::DragInt("Height",&s.FixedHeight,8,1,16384)) {o.FixedHeight=s.FixedHeight;}
            } else {
                if (ImGui::SliderFloat("Scale",&s.ResolutionScale,.5f,1,"%.2f")) {o.ResolutionScale=s.ResolutionScale;}
                if (ImGui::DragInt("Height cap",&s.MaxRenderHeight,8,1,16384)) {o.MaxRenderHeight=s.MaxRenderHeight;}
            }
            if (ImGui::DragInt("Target FPS (0 = unlimited)",&s.TargetFps,1,0,240)) {o.TargetFps=s.TargetFps;}
            int samples=s.MsaaSamples == 1 ? 0:s.MsaaSamples == 2 ? 1:s.MsaaSamples == 4 ? 2:3;
            if (ImGui::Combo("MSAA",&samples,"Off\0x2\0x4\0x8\0")) {o.MsaaSamples=1<<samples;}
            int texture=static_cast<int>(s.Textures);if (ImGui::Combo("Textures",&texture,"Low (512)\0Medium (2048)\0High (8192)\0")) {o.Textures=static_cast<TextureQuality>(texture);}
            if (ImGui::SliderInt("Anisotropy",&s.Anisotropy,1,16)) {o.Anisotropy=s.Anisotropy;}
            int lighting=static_cast<int>(s.Lighting);if (ImGui::Combo("Lighting",&lighting,"Unlit\0Basic\0Advanced (normal maps + local lights)\0")) {o.Lighting=static_cast<LightingMode>(lighting);}
            if (ImGui::Checkbox("Frustum culling",&s.FrustumCulling)) {o.FrustumCulling=s.FrustumCulling;}
            int demand=static_cast<int>(s.OnDemand);if (ImGui::Combo("On demand",&demand,"Off\0On\0Auto\0")) {o.OnDemand=static_cast<OnDemandMode>(demand);}
            int shadow=static_cast<int>(s.Shadow);if (ImGui::Combo("Sun shadow",&shadow,"Off\0Basic\0PCF (3x3)\0PCSS (contact softening)\0")) {o.Shadow=static_cast<ShadowMode>(shadow);}
            int shadowSize=s.ShadowResolution == 512 ? 0:s.ShadowResolution == 1024 ? 1:s.ShadowResolution == 2048 ? 2:3;if (ImGui::Combo("Shadow size",&shadowSize,"512\0 1024\0 2048\0 4096\0")) {o.ShadowResolution=512<<shadowSize;}
            if (s.Shadow != ShadowMode::Off) {
                if (ImGui::SliderFloat("Shadow depth bias",&s.ShadowDepthBias,0,.1f,"%.5f")) {o.ShadowDepthBias=s.ShadowDepthBias;}
                if (ImGui::SliderFloat("Shadow slope bias",&s.ShadowSlopeBias,0,.1f,"%.5f")) {o.ShadowSlopeBias=s.ShadowSlopeBias;}
            }
            if (s.Shadow == ShadowMode::Pcss) {
                if (ImGui::SliderFloat("Sun source angle (degrees)",&s.ShadowSourceAngle,0,20,"%.3f")) {o.ShadowSourceAngle=s.ShadowSourceAngle;}
                if (ImGui::SliderInt("Blocker samples",&s.ShadowBlockerSamples,1,64)) {o.ShadowBlockerSamples=s.ShadowBlockerSamples;}
                if (ImGui::SliderInt("Filter samples",&s.ShadowFilterSamples,1,64)) {o.ShadowFilterSamples=s.ShadowFilterSamples;}
                if (ImGui::SliderFloat("Max shadow radius (texels)",&s.ShadowMaxFilterRadius,1,64,"%.1f")) {o.ShadowMaxFilterRadius=s.ShadowMaxFilterRadius;}
            }
            if (ImGui::Checkbox("HDR + FXAA + Reinhard",&s.PostProcessing)) {o.PostProcessing=s.PostProcessing;}
            if (ImGui::Checkbox("Automatic rigid LOD",&s.Lod)) {o.Lod=s.Lod;}
            if (ImGui::SliderFloat("LOD bias",&s.LodBias,-4,4,"%.2f")) {o.LodBias=s.LodBias;}
            if (ImGui::Checkbox("Static GPU instancing",&s.Instancing)) {o.Instancing=s.Instancing;}
            if (ImGui::Checkbox("PBR materials (Advanced)",&s.Pbr)) {o.Pbr=s.Pbr;}
            int compression=static_cast<int>(s.Compression);if (ImGui::Combo("Texture compression",&compression,"None\0Auto\0BC3\0")) {o.Compression=static_cast<TextureCompression>(compression);}
            ImGui::TextWrapped("PCSS softens the directional Sun shadow with receiver distance. Low disables shadows by default.");
        } else {ImGui::TextWrapped("%s",expanded.Error().Message.c_str());}
        ImGui::Checkbox("Allow supported fallback",&qualityDraft_.AllowFallback);
        ImGui::TextUnformatted(qualityDraft_.IsCustom() ? "Custom overrides":"Preset defaults");
        if (ImGui::Button("Clear overrides")) {qualityDraft_.Overrides={};}
        ImGui::SameLine();if (ImGui::Button("Apply")) {if (Report(Viewport.SetQuality(qualityDraft_,Document.GetScene()))) {status_="Quality applied";}}
        ImGui::SameLine();if (ImGui::Button("Reset to applied")) {ResetQualityDraft();}
        if (ImGui::Button("Save profile...")) {FileDialog(Dialog::QualitySave);}ImGui::SameLine();if (ImGui::Button("Load profile...")) {FileDialog(Dialog::QualityLoad);}
        const auto& q=Viewport.Quality();ImGui::Separator();
        ImGui::Text("Applied: %s",q.Applied ? "yes":"pending visible viewport");
        if (q.Effective) {
            const auto& effective=*q.Effective;
            ImGui::Text("Render: %d x %d",q.RenderWidth,q.RenderHeight);
            ImGui::Text("MSAA requested / effective: %d / %d",q.RequestedSettings.MsaaSamples,effective.MsaaSamples);
            ImGui::Text("Texture limit: %d / %d",q.RequestedSettings.MaxTextureSize,effective.MaxTextureSize);
            ImGui::Text("Anisotropy: %d / %d",q.RequestedSettings.Anisotropy,effective.Anisotropy);
            ImGui::Text("Shadow mode / size requested: %d / %d | effective: %d / %d",static_cast<int>(q.RequestedSettings.Shadow),q.RequestedSettings.ShadowResolution,static_cast<int>(effective.Shadow),effective.ShadowResolution);
            ImGui::Text("Post requested / effective: %s / %s",q.RequestedSettings.PostProcessing ? "on":"off",effective.PostProcessing ? "on":"off");
            ImGui::Text("LOD bias requested / effective: %.2f / %.2f",q.RequestedSettings.LodBias,effective.LodBias);
            ImGui::Text("FPS target: %d | culling: %s",effective.TargetFps,effective.FrustumCulling ? "on":"off");
            const auto mask=effective.PostProcessing ? q.Capabilities.HdrSampleCounts:q.Capabilities.SampleCounts;
            ImGui::Text("Supported %s MSAA: 1%s%s%s",effective.PostProcessing ? "HDR":"LDR",mask&(1u<<2)?", 2":"",mask&(1u<<4)?", 4":"",mask&(1u<<8)?", 8":"");
            ImGui::Text("Shadow format: %s | HDR: %s",q.Capabilities.ShadowMapping ? "supported":"unavailable",q.Capabilities.HdrPostProcessing ? "supported":"unavailable");
            for (const auto& a:q.Adjustments) {ImGui::TextWrapped("%s: %s",a.Field.c_str(),a.Reason.c_str());}
        }
        if (q.LastError) {ImGui::TextWrapped("Previous quality retained: %s",q.LastError->Message.c_str());}
        QualityOverview();
    }ImGui::End();
}
void EditorUI::AssetBrowser() {
    std::fill(std::begin(assetRect_),std::end(assetRect_),0.0f);
    if (!assets_) { return; }
    if (ImGui::Begin("Asset Browser",&assets_)) {
        const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();assetRect_[0]=pos.x;assetRect_[1]=pos.y;assetRect_[2]=pos.x+size.x;assetRect_[3]=pos.y+size.y;
        if (ImGui::Button("Import Assets...")) { FileDialog(Dialog::Import); }
        ImGui::SameLine(); if (ImGui::Button("Up")) { directory_ = directory_.parent_path(); }
        ImGui::SameLine(); ImGui::TextUnformatted(Text(directory_).c_str());
        ImGui::SetNextItemWidth(200); ImGui::InputTextWithHint("##filter","Filter files",&filter_);
        if (ImGui::BeginTabBar("Assets")) {
            if (ImGui::BeginTabItem("Imported")) {
                ImGui::TextUnformatted("Drag an asset to Viewport, or select and Place.");
                AssetId reimport = 0;
                for (const auto& model : Document.Assets()) {
                    Id(model->Id);
                    if (ImGui::Selectable(Text(model->SourcePath.filename()).c_str(),selectedAsset_ == model->Id,ImGuiSelectableFlags_AllowDoubleClick)) {
                        selectedAsset_ = model->Id;
                        if (ImGui::IsMouseDoubleClicked(0)) { Report(Document.PlaceModel(selectedAsset_)); }
                    }
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("MVX_ASSET",&model->Id,sizeof(AssetId)); ImGui::TextUnformatted(Text(model->SourcePath.filename()).c_str()); ImGui::EndDragDropSource();
                    }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s",Text(model->SourcePath).c_str()); }
                    if (selectedAsset_ == model->Id) {
                        if (ImGui::SmallButton("Place instance")) { if (Report(Document.PlaceModel(selectedAsset_))) { FrameScene(); } }
                        ImGui::SameLine();if (ImGui::SmallButton("Reimport")) {reimport = model->Id;}
                        for (const auto& warning : model->Warnings) { ImGui::TextWrapped("%s",warning.c_str()); }
                    }
                    ImGui::PopID();
                }
                if (reimport) {ReimportAsset(reimport);}
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                try {
                    std::vector<std::filesystem::directory_entry> files;
                    for (const auto& entry : std::filesystem::directory_iterator(directory_,std::filesystem::directory_options::skip_permission_denied)) {
                        const auto name = Text(entry.path().filename());
                        if ((entry.is_directory() || ModelFile(entry.path())) && (filter_.empty() || name.find(filter_) != std::string::npos)) { files.push_back(entry); }
                    }
                    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) { return a.path() < b.path(); });
                    for (const auto& entry : files) {
                        const bool folder = entry.is_directory(); const auto path = Text(entry.path());
                        const auto label = (folder ? "[dir] " : "") + Text(entry.path().filename());
                        if (ImGui::Selectable(label.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
                            if (folder) { directory_ = entry.path(); } else { Import(entry.path(),false); }
                        }
                        if (!folder && ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("MVX_FILE",path.c_str(),path.size()+1); ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource(); }
                    }
                } catch (const std::exception& e) { ImGui::TextWrapped("%s",e.what()); }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
void EditorUI::ViewportPanel() {
    gizmoVisible_=false;viewportRect_={};
    if (!viewport_) {StopCameraEdit(true);if (gizmoEditing_) {if (Document.IsEditing()) {(void)Document.CancelEdit();}ImGuizmo::Enable(false);gizmoEditing_=false;}Viewport.Resize(0,0);return;}
    if (ImGui::Begin("Viewport",&viewport_)) {
        if (ImGui::Button("Focus selected (F)")) { FrameScene(); }
        ImGui::SameLine();ImGui::BeginDisabled(Document.IsEditing());ImGui::SetNextItemWidth(115);ImGui::Combo("##transform-tool",&transformTool_,"Move (W)\0Rotate (E)\0Scale (R)\0");ImGui::EndDisabled();
        ImGui::SameLine();ImGui::Checkbox("Grid / Axis",&grid_);
        ImGui::TextUnformatted("Left: Select / Gizmo | Right: Orbit | Middle: Pan | Wheel: Zoom | Esc: Cancel");
        const auto available = ImGui::GetContentRegionAvail(); const auto& io = ImGui::GetIO();
        if (available.x > 1 && available.y > 1) {
            Viewport.Resize(static_cast<int>(std::min(65536.0f,available.x*sapp_dpi_scale())),static_cast<int>(std::min(65536.0f,available.y*sapp_dpi_scale())));
            // Publish replacement overlay views before ImGui records this Image.
            Viewport.Render(Document.GetScene());
            ImGui::Image(simgui_imtextureid(Viewport.Texture()),available);
            const auto imageMin=ImGui::GetItemRectMin();
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_ASSET")) { if (Report(Document.PlaceModel(*static_cast<const AssetId*>(p->Data)))) { FrameScene(); } }
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_FILE")) { Import(detail::PathFromUtf8(static_cast<const char*>(p->Data)),true); }
                ImGui::EndDragDropTarget();
            }
            const bool modal = ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) || dialog_ != Dialog::None || pending_ != Action::None;
            const bool uiBlocked=UiViewportInput(imageMin.x,imageMin.y,available.x,available.y,hovered,modal);
            if (uiBlocked) {StopCameraEdit(false);controls_.Reset();cameraDrag_=false;}
            TransformTools(imageMin.x,imageMin.y,available.x,available.y,!modal&&!uiBlocked);
            if (io.AppFocusLost || modal) {StopCameraEdit(true);}
            else if (!uiBlocked) {
                auto camera = Document.GetScene().MainCamera;
                ViewerInputEvent event; event.Shift = io.KeyShift;
                bool changed = false;
                for (int button = 1; button < 3; ++button) {
                    event.Button = button == 1 ? ViewerMouseButton::Left : ViewerMouseButton::Middle;
                    if (hovered && !gizmoEditing_ && !gizmoSuppressed_ && ImGui::IsMouseClicked(button)) {if (!Document.IsEditing() && Report(Document.BeginEdit())) {cameraEditing_=true;}event.Type = ViewerInputType::MouseDown; controls_.Process(camera,event,{},static_cast<int>(available.y)); cameraDrag_ = true; }
                    if (ImGui::IsMouseReleased(button)) { event.Type = ViewerInputType::MouseUp; controls_.Process(camera,event,{},static_cast<int>(available.y)); }
                }
                if (cameraDrag_) { event.Type = ViewerInputType::MouseMove; event.DeltaX = io.MouseDelta.x; event.DeltaY = io.MouseDelta.y; changed = controls_.Process(camera,event,{},static_cast<int>(available.y)); }
                if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2]) { cameraDrag_ = false; }
                if (hovered && !gizmoEditing_ && !gizmoSuppressed_ && io.MouseWheel != 0) { event.Type = ViewerInputType::Scroll; event.ScrollY = io.MouseWheel; changed = controls_.Process(camera,event,{},static_cast<int>(available.y)) || changed; }
                if (changed) { Report(Document.SetCamera(camera)); }
                if (!cameraDrag_ && cameraEditing_) {StopCameraEdit(false);}
                if (hovered && !gizmoBlockInput_ && !io.WantTextInput && ImGui::IsMouseClicked(0)) {try {Report(Document.Select(detail::PickSceneObject(Document.GetScene(),Viewport.Aspect(),(io.MousePos.x-imageMin.x)/available.x*2-1,1-(io.MousePos.y-imageMin.y)/available.y*2)));ImGui::SetWindowFocus();}catch (const std::exception& e) {ShowError({ErrorCode::InvalidArgument,e.what(),"Check the camera and visible model bounds.",{}});}}
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !gizmoEditing_ && !gizmoSuppressed_) {
                    if (cameraEditing_) {if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {StopCameraEdit(true);}}
                    else {
                        if (!io.KeyCtrl && !io.KeySuper) {if (ImGui::IsKeyPressed(ImGuiKey_W,false)) {transformTool_=0;}if (ImGui::IsKeyPressed(ImGuiKey_E,false)) {transformTool_=1;}if (ImGui::IsKeyPressed(ImGuiKey_R,false)) {transformTool_=2;}}
                        if (ImGui::IsKeyPressed(ImGuiKey_F,false)) {FrameScene();}
                        if (ImGui::IsKeyPressed(ImGuiKey_Delete,false) && Document.Selection()) {deleteObject_=Document.Selection();}
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {Report(Document.Select(NoObject));controls_.Reset();cameraDrag_=false;}
                    }
                }
            }
        } else {StopCameraEdit(true);if (gizmoEditing_) {if (Document.IsEditing()) {(void)Document.CancelEdit();}ImGuizmo::Enable(false);gizmoEditing_=false;}Viewport.Resize(0,0);}
    } else {StopCameraEdit(true);if (gizmoEditing_) {if (Document.IsEditing()) {(void)Document.CancelEdit();}ImGuizmo::Enable(false);gizmoEditing_=false;}Viewport.Resize(0,0);}
    ImGui::End();
}
void EditorUI::DropFiles(const std::vector<std::filesystem::path>& paths,float x,float y) {
    if (dialog_ != Dialog::None || pending_ != Action::None || HasImportPreview()) {Record(ConsoleLevel::Warning,"Finish the current operation before dropping files.");return;}
    const bool library=assets_ && x >= assetRect_[0] && y >= assetRect_[1] && x < assetRect_[2] && y < assetRect_[3];
    for (const auto& path:paths) {
        auto name=Text(path.filename());std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c) {return static_cast<char>(std::tolower(c));});
        if (name.ends_with(".mvxquality.json")) {LoadQualityProfile(path);break;}
        if (name.ends_with(".mvxpose.json")) {Report(Document.LoadPoseFile(Document.Selection(),path),Document.Selection());break;}
        if (name.ends_with(".json")) {RequestOpen(path);break;}
        Import(path,!library);
    }
}
void EditorUI::Welcome() {
    if (!welcome_ || Document.GetScene().Size() || !Document.Assets().empty()) {return;}
    const auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos({viewport->WorkPos.x+viewport->WorkSize.x*.5f,viewport->WorkPos.y+viewport->WorkSize.y*.45f},ImGuiCond_Appearing,{.5f,.5f});
    ImGui::SetNextWindowSize({560,0},ImGuiCond_Always);
    if (ImGui::Begin("Welcome to Minvex",&welcome_,ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("MINVEX");ImGui::TextWrapped("Open a model and start inspecting. No project is required.");ImGui::Spacing();
        if (ImGui::Button("Open Model",{520,40})) {FileDialog(Dialog::QuickOpen);}
        if (ImGui::Button("Open Scene",{255,32})) {FileDialog(Dialog::Open);}ImGui::SameLine();if (ImGui::Button("New Project",{255,32})) {FileDialog(Dialog::Project);}
        ImGui::TextDisabled("Supported: glTF / GLB / OBJ / FBX");ImGui::Separator();ImGui::TextUnformatted("Recent Files");
        std::optional<RecentFile> open;
        if (preferences_.Recent.empty()) {ImGui::TextDisabled("Your opened models and saved Scenes will appear here.");}
        for (std::size_t i=0;i<preferences_.Recent.size();++i) {const auto& entry=preferences_.Recent[i];std::error_code ec;const bool exists=std::filesystem::is_regular_file(entry.Path,ec);ImGui::PushID(static_cast<int>(i));const auto label=(exists ? "":"[missing] ")+Text(entry.Path.filename());if (ImGui::Selectable(label.c_str())) {open=entry;}if (ImGui::IsItemHovered()) {ImGui::SetTooltip("%s",Text(entry.Path).c_str());}ImGui::PopID();}
        if (open) {if (open->Kind == RecentKind::Model) {RequestQuickOpen(open->Path);}else {RequestOpen(open->Path);}}
        ImGui::Separator();if (ImGui::Button("Sample Scene")) {RequestQuickOpen(samplePath_);}ImGui::SameLine();if (ImGui::Button("Documentation")) {Report(NativeFiles::OpenPath(documentationPath_));}ImGui::SameLine();if (ImGui::Button("Clear Recent")) {preferences_.Recent.clear();PersistPreferences();}
    }ImGui::End();
}
void EditorUI::Console() {
    if (!console_) {return;}
    if (ImGui::Begin("Console",&console_)) {
        ImGui::Checkbox("Info",&consoleInfo_);ImGui::SameLine();ImGui::Checkbox("Warnings",&consoleWarnings_);ImGui::SameLine();ImGui::Checkbox("Errors",&consoleErrors_);ImGui::SameLine();if (ImGui::Button("Clear")) {consoleEntries_.clear();}
        ImGui::InputTextWithHint("##console-filter","Filter messages",&consoleFilter_);ImGui::Separator();
        std::filesystem::path reveal;ObjectId select=NoObject;
        for (std::size_t i=0;i<consoleEntries_.size();++i) {
            const auto& entry=consoleEntries_[i];if ((entry.Level == ConsoleLevel::Info && !consoleInfo_) || (entry.Level == ConsoleLevel::Warning && !consoleWarnings_) || (entry.Level == ConsoleLevel::Error && !consoleErrors_) || (!consoleFilter_.empty() && entry.Message.find(consoleFilter_) == std::string::npos)) {continue;}
            ImGui::PushID(static_cast<int>(i));const auto color=entry.Level == ConsoleLevel::Error ? ImVec4{1,.4f,.35f,1}:entry.Level == ConsoleLevel::Warning ? ImVec4{1,.78f,.3f,1}:ImVec4{.75f,.82f,.9f,1};
            ImGui::TextColored(color,"%s",entry.Level == ConsoleLevel::Error ? "ERROR":entry.Level == ConsoleLevel::Warning ? "WARNING":"INFO");ImGui::SameLine();ImGui::TextWrapped("%s",entry.Message.c_str());
            if (!entry.Source.empty()) {ImGui::TextDisabled("%s",Text(entry.Source).c_str());if (ImGui::SmallButton("Open folder")) {reveal=entry.Source.parent_path();}}
            if (entry.Object && Document.GetScene().Contains(entry.Object)) {ImGui::SameLine();if (ImGui::SmallButton("Select object")) {select=entry.Object;}}
            ImGui::Separator();ImGui::PopID();
        }
        if (select) {Report(Document.Select(select));}if (!reveal.empty()) {Report(NativeFiles::OpenPath(reveal));}
    }ImGui::End();
}
void EditorUI::Dialogs() {
    if (pending_ != Action::None && Document.IsDirty() && dialog_ == Dialog::None && error_.empty()) { ImGui::OpenPopup("Unsaved changes"); }
    if (ImGui::BeginPopupModal("Unsaved changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        if (ImGui::Button("Save")) {
            ResolvePending(UnsavedChoice::Save);ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Discard")) { ResolvePending(UnsavedChoice::Discard); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { ResolvePending(UnsavedChoice::Cancel); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (deleteObject_ && !Document.GetScene().Contains(deleteObject_)) {deleteObject_=NoObject;}
    if (deleteObject_) {ImGui::OpenPopup("Delete subtree?");}
    if (ImGui::BeginPopupModal("Delete subtree?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {ImGui::TextUnformatted("Delete the selected object and its children?");ImGui::TextUnformatted("Imported assets remain in the library.");if (ImGui::Button("Delete")) {Report(Document.RemoveObject(deleteObject_),deleteObject_);deleteObject_=NoObject;ImGui::CloseCurrentPopup();}ImGui::SameLine();if (ImGui::Button("Cancel")) {deleteObject_=NoObject;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    if (!error_.empty()) { ImGui::OpenPopup("Operation failed"); }
    if (ImGui::BeginPopupModal("Operation failed",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*65); ImGui::TextUnformatted(error_.c_str()); ImGui::PopTextWrapPos();
        if (ImGui::Button("Close")) { error_.clear(); ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
    }
}
void EditorUI::Draw() {
    ImGuizmo::BeginFrame();
    PollAssetJobs();
    PollAnimationEvents();
    if (pendingQuality_) {const auto profile=std::move(*pendingQuality_);pendingQuality_.reset();Report(ApplyQualityProfile(profile));lastAnalysis_=-1;}
    Menu(); Dockspace(); Hierarchy(); Inspector(); BoneInspector(); AssetBrowser();AssetInspector(); AnimationControls(); QualitySettings(); UiDesigner();ViewportPanel(); PerformancePanel();RenderDebugger();AnimationDebugger();Console(); Welcome();ImportPreview(); Dialogs();
    if (ImGui::BeginViewportSideBar("Status",ImGui::GetMainViewport(),ImGuiDir_Down,ImGui::GetFrameHeight(),ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings)) {
        const auto stats = Viewport.Stats();
        ImGui::Text("%zu objects | %zu assets | %zu draw calls | %s",Document.GetScene().Size(),Document.Assets().size(),stats.LastDrawCalls,status_.c_str());
    } ImGui::End();
}
} // namespace lv::editor
