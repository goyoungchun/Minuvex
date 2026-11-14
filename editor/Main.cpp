#include "EditorUI.h"
#include "RuntimePlatform.h"
#include "core/Utf8Path.h"

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#undef GetObject
#endif

namespace {
struct Application {
    std::unique_ptr<lv::editor::EditorUI> UI;
    std::filesystem::path Initial, Directory = std::filesystem::current_path();
    std::filesystem::path Preferences = std::filesystem::current_path()/".minvex-editor.json";
    bool DirectoryExplicit = false;
    std::uint64_t Frames = 0, Limit = 0;
    bool Smoke = false, AnimationSmoke = false, QualitySmoke = false, TelemetrySmoke = false, Gui = false, Failed = false;
    bool UiSmoke=false;
    lv::UiId UiButton=lv::NoUi;
    std::optional<lv::QualityProfile> InitialQuality;
    std::optional<lv::QualityPreset> RecommendedQuality;
    std::uint64_t CachedFrames=0;
    double PreviousTime = 0;
    char Message[2048]{};
    std::uint32_t FirstTarget = 0;
    int FirstWidth = 0;
    lv::ObjectId GizmoObject=lv::NoObject;
    ImVec2 GizmoCenter{};
    lv::Transform GizmoMoved;
    void Fail(const char* message) noexcept { Failed = true; std::snprintf(Message,sizeof(Message),"%s",message); sapp_quit(); }
};
void Require(bool valid, const char* message) { if (!valid) { throw std::runtime_error(message); } }
template<typename T> void Require(const lv::Result<T>& result) { if (!result) { throw std::runtime_error(result.Error().Message); } }
ImVec2 GizmoScreenPosition(Application& app) {
    const auto& ui=*app.UI;const auto area=ui.ViewportArea();
    const auto point=ui.Document.GetScene().GetWorldTransform(app.GizmoObject).TransformPoint({});
    const auto vp=ui.Document.GetScene().MainCamera.ViewProjection(ui.Viewport.Aspect());const auto clip=vp.TransformPoint(point);const auto& m=vp.Elements;
    const float w=m[3]*point.X+m[7]*point.Y+m[11]*point.Z+m[15];
    Require(std::isfinite(w) && std::abs(w)>1e-6f,"Gizmo test point is outside the camera projection.");
    return {area[0]+(.5f+clip.X/w*.5f)*area[2],area[1]+(.5f-clip.Y/w*.5f)*area[3]};
}
void AwaitPreview(lv::editor::EditorUI& ui) {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while (ui.ImportPreviewPending() && std::chrono::steady_clock::now()<deadline) {ui.PollAssetJobs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}Require(!ui.ImportPreviewPending(),"Background Import Preview timed out.");}
bool ConfirmPreview(lv::editor::EditorUI& ui) {AwaitPreview(ui);return ui.ConfirmImportPreview();}
#if defined(_WIN32)
void CheckViewportPixels(const lv::editor::SceneViewport& viewport) {
    auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    auto* context = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(viewport.ColorImage()).tex2d));
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,staging.GetAddressOf())),"Cannot create viewport readback target.");
    context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Cannot read Editor viewport pixels.");
    std::size_t left = 0, right = 0;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            if (std::max({row[x*4],row[x*4+1],row[x*4+2]}) > 100) { if (x < desc.Width/2) { ++left; } else { ++right; } }
        }
    }
    context->Unmap(staging.Get(),0);
    Require(left > 50 && right > 50,"Editor offscreen texture is missing one of its model instances.");
}
#endif
void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message, std::uint32_t line, const char* file, void* user) noexcept {
    auto& app=*static_cast<Application*>(user);
    if (app.UI && level <= 2) {try {app.UI->Record(level <= 1 ? lv::editor::ConsoleLevel::Error:lv::editor::ConsoleLevel::Warning,message ? message:tag);}catch (...) {}}
    if (level <= 1 && !(app.UI && app.UI->Viewport.ApplyingQuality())) { app.Fail(message ? message : "Editor graphics backend failure."); }
    slog_func(tag,level,item,message,line,file,nullptr);
}
void CheckEditorFileWorkflows() {
    using namespace lv::editor;
    const auto folder=std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT)/"file-workflows";
    std::filesystem::create_directories(folder);
    const auto source=folder/std::filesystem::path(L"한글 sample.glb");
    std::filesystem::copy_file(MINVEX_EDITOR_SAMPLE,source,std::filesystem::copy_options::overwrite_existing);
    EditorUI ui;ui.InitializeSession({},MINVEX_EDITOR_SAMPLE,MINVEX_EDITOR_DOCUMENTATION,false);
    ui.SetFilePicker([](const FilePickerRequest& request) {Require(request.Mode == FilePickerMode::Open && request.Filter == FilePickerFilter::Model,"Open Model picker request is incorrect.");return FilePickerResult::Success(std::nullopt);});
    ui.ChooseFiles(EditorFileAction::OpenModel);
    Require(ui.Document.GetScene().Size() == 0 && ui.ConsoleEntries().empty(),"File chooser cancellation changed the Scene or logged an error.");
    ui.SetFilePicker([&](const FilePickerRequest&) {return FilePickerResult::Success(std::vector<std::filesystem::path>{source});});
    ui.ChooseFiles(EditorFileAction::OpenModel);
    Require(ui.Document.GetScene().Size() == 1 && ui.Document.Selection() && ui.Document.Path().empty() && ui.Document.IsDirty(),"Native picker selection did not Quick Open an unsaved model.");
    const auto selected=ui.Document.Selection();
    ui.RequestQuickOpen(folder/"missing.glb");ui.ResolvePending(UnsavedChoice::Cancel);
    Require(ui.Document.Selection() == selected,"Cancelled dirty-document switch changed the model.");
    ui.RequestQuickOpen(folder/"missing.glb");ui.ResolvePending(UnsavedChoice::Discard);
    Require(ui.Document.Selection() == selected && ui.ConsoleEntries().back().Level == ConsoleLevel::Error && !ui.ConsoleEntries().back().Source.empty(),"Failed model replacement lost the Scene or error context.");
    ui.SetFilePicker([](const FilePickerRequest& request) {Require(request.Mode == FilePickerMode::Save && request.Name == "Scene.mvxscene.json","Unsaved Scene picker defaults are incorrect.");return FilePickerResult::Success(std::nullopt);});
    ui.RequestQuit();ui.ResolvePending(UnsavedChoice::Save);
    Require(!ui.QuitRequested() && ui.Document.IsDirty(),"Cancelling Save before Exit did not cancel Exit.");
    const auto saved=folder/"saved.mvxscene.json";
    ui.SetFilePicker([&](const FilePickerRequest&) {return FilePickerResult::Success(std::vector<std::filesystem::path>{saved});});
    ui.ChooseFiles(EditorFileAction::SaveAs);
    Require(!ui.Document.IsDirty() && ui.Document.Path() == saved,"Save As failed or resumed a cancelled Exit.");
    Require(!ui.QuitRequested(),"Cancelled Exit was unexpectedly resumed by Save As.");
    ui.RequestNew();ui.RequestOpen(saved);
    Require(ui.Document.GetScene().Size() == 1 && !ui.Document.IsDirty(),"Open Scene did not restore the native Save As document.");
    ui.SetFilePicker([&](const FilePickerRequest& request) {Require(request.Mode == FilePickerMode::Multiple,"Import Assets did not request a multiselect dialog.");return FilePickerResult::Success(std::vector<std::filesystem::path>{source,MINVEX_EDITOR_ANIMATION_SAMPLE});});
    ui.ChooseFiles(EditorFileAction::ImportAssets);
    AwaitPreview(ui);
    Require(ui.HasImportPreview() && ui.ImportPreviewModel() && ui.Document.Assets().size() == 1 && !ui.Document.IsDirty(),"Import Preview mutated the current document before approval.");
    ui.CancelImportPreview();Require(ui.Document.Assets().size() == 1 && !ui.Document.IsDirty(),"Cancelling Import Preview changed existing assets.");
    ui.ChooseFiles(EditorFileAction::ImportAssets);
    while (ui.HasImportPreview()) {Require(ConfirmPreview(ui),"Failed to confirm a file in the multi-file Import Preview.");}
    Require(ui.Document.Assets().size() == 2 && ui.Document.GetScene().Size() == 1,"Multi-file import placed instances instead of registering assets.");
    ui.DropFiles({source},-10,-10);
    Require(ui.HasImportPreview() && ui.Document.GetScene().Size() == 1,"Dropped model bypassed Import Preview.");Require(ConfirmPreview(ui),"Dropped model preview could not commit.");
    Require(ui.Document.GetScene().Size() == 2,"Dropped model did not create a Scene instance.");
    ui.SetFilePicker([&](const FilePickerRequest&) {return FilePickerResult::Success(std::vector<std::filesystem::path>{source});});
    ui.ChooseFiles(EditorFileAction::NewProject);ui.ResolvePending(UnsavedChoice::Discard);
    Require(ui.Document.GetScene().Size() == 2,"Failed workspace creation lost the previous Scene.");
    ui.SetFilePicker([&](const FilePickerRequest& request) {Require(request.Mode == FilePickerMode::Folder,"New Project did not request a folder chooser.");return FilePickerResult::Success(std::vector<std::filesystem::path>{folder});});
    ui.ChooseFiles(EditorFileAction::NewProject);ui.ResolvePending(UnsavedChoice::Discard);
    Require(ui.Document.GetScene().Size() == 0 && std::filesystem::is_directory(folder/"assets") && std::filesystem::is_directory(folder/"scenes"),"Workspace creation failed.");
    ui.Import(source,false);Require(ConfirmPreview(ui),"Project resource copy did not commit.");
    Require(ui.Document.Assets().front()->SourcePath!=source && ui.Document.GetScene().Size() == 0,"Project Import retained an external source or placed an unwanted instance.");
    for (int i=0;i<300;++i) {ui.Record(ConsoleLevel::Info,"bounded Console entry");}
    Require(ui.ConsoleEntries().size() == 256,"Console history was not bounded.");
    ui.Shutdown();
}
void Init(void* user) noexcept {
    auto& app = *static_cast<Application*>(user);
    try {
        sg_desc desc{}; desc.environment = sglue_environment(); desc.logger.func = Log; desc.logger.user_data = &app;
        sg_setup(desc); Require(sg_isvalid(),"Cannot initialize Editor graphics.");
        simgui_desc_t gui{}; gui.logger.func = Log; gui.logger.user_data = &app;
        simgui_setup(gui); app.Gui = true;
        auto& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = app.Smoke||app.UiSmoke ? nullptr : ".minvex-editor.ini";
#if defined(_WIN32)
        const char* font = "C:/Windows/Fonts/malgun.ttf";
#else
        const char* font = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
#endif
        if (std::filesystem::exists(font)) { io.FontDefault = io.Fonts->AddFontFromFileTTF(font,17); }
        ImGui::StyleColorsDark(); ImGui::GetStyle().WindowRounding = 3; ImGui::GetStyle().FrameRounding = 3;
        app.UI = std::make_unique<lv::editor::EditorUI>();
        app.UI->InitializeSession(app.Preferences,MINVEX_EDITOR_SAMPLE,MINVEX_EDITOR_DOCUMENTATION,!app.Smoke&&!app.UiSmoke);
        if (app.Smoke) {CheckEditorFileWorkflows();}
        if (app.DirectoryExplicit) {app.UI->SetDirectory(app.Directory);}
        if (!app.Initial.empty()) {
            if (app.Initial.extension() == ".json") { app.UI->RequestOpen(app.Initial); }
            else { app.UI->RequestQuickOpen(app.Initial); }
        }
        if (app.Smoke) {
            auto& doc = app.UI->Document;
            const auto asset = doc.ImportModel(app.AnimationSmoke ? MINVEX_EDITOR_ANIMATION_SAMPLE : MINVEX_EDITOR_SAMPLE); Require(asset);
            auto group = doc.CreateObject("Editor test group"); Require(group);
            lv::Transform t; t.Position = {-1.5f,0,0}; auto left = doc.PlaceModel(asset.Value(),t,group.Value()); Require(left);
            t.Position.X = 1.5f; Require(doc.PlaceModel(asset.Value(),t,group.Value()));
            if (app.AnimationSmoke) {
                Require(doc.Select(left.Value())); Require(doc.PlayAnimation(left.Value(),0));
                auto local = doc.GetScene().GetObject(left.Value()).Model->Skeleton.Joints[1].ReferenceLocal; local.Position.X = .25f;
                Require(doc.SetJointPose(left.Value(),1,local));
                Require(doc.SetAdditiveAnimation(left.Value(),2,.25f));
                const std::array<lv::AnimationEvent,1> events{{{.00001,"EditorEvent","smoke"}}};Require(doc.SetAnimationEvents(left.Value(),0,events));
            }
            Require(doc.SetCamera(lv::detail::FitSceneCamera(doc.GetScene(),1.5f)));
        }
        if (app.InitialQuality) {Require(app.UI->ApplyQualityProfile(*app.InitialQuality));}
        if (app.QualitySmoke) {auto low=lv::MakeQualityProfile(lv::QualityPreset::Low);low.Overrides.TargetFps=0;Require(app.UI->ApplyQualityProfile(low));}
    } catch (const std::exception& e) { app.Fail(e.what()); }
    catch (...) { app.Fail("Editor initialization threw an unknown exception."); }
}
void SmokeAfterDraw(Application& app) {
    auto& ui = *app.UI; auto& doc = ui.Document;
    if (app.Frames == 1) {
        if (app.QualitySmoke) {const auto* panel=ImGui::FindWindowByName("Quality Settings");Require(panel && panel->DockId,"Quality Settings panel was not docked.");Require(ui.Viewport.Quality().Requested.Base == lv::QualityPreset::Low && ui.Viewport.Quality().Applied,"Initial Low quality was not applied.");}
        for (const auto* name : {"Viewport","Hierarchy","Inspector","Asset Browser","Console","Asset Inspector"}) {
            const auto* window = ImGui::FindWindowByName(name);
            Require(window && window->Active && window->DockId,"Editor panel is missing from the docking layout.");
        }
        Require(ui.Viewport.Width() > 0 && sg_query_view_state(ui.Viewport.Texture()) == SG_RESOURCESTATE_VALID,"Editor viewport texture is unavailable.");
        const auto& stats = ui.Viewport.Stats();
        const auto& quality = ui.Viewport.Quality();
        Require(stats.ResidentModels == 1,"Editor instances did not share the GPU model.");
        Require(quality.Applied && quality.Effective.has_value(),"Editor viewport quality was not applied.");
        // SharedBox has two parts with opposite winding, forming two static batches.
        const bool staticInstancing = !app.AnimationSmoke && quality.Effective->Instancing;
        const auto expectedDraws = app.AnimationSmoke || staticInstancing ? 2u : 4u;
        Require(stats.LastDrawCalls == expectedDraws,"Editor draw calls did not match the active instancing configuration.");
        Require(stats.Optimization.InstancedObjects == (staticInstancing ? 4u : 0u),"Editor static instances were not batched as configured.");
        if (app.AnimationSmoke) {
            for (const auto* name : {"Bone Inspector","Animation Controls"}) { const auto* window = ImGui::FindWindowByName(name); Require(window && window->DockId,"Animation panel was not docked."); }
            const auto id = doc.Selection(); const auto& player = doc.GetScene().GetObject(id).Animation;
            Require(player.Time() > app.PreviousTime && player.Overrides().Joints.size() == 1,"Editor tick did not advance animation while preserving overrides.");
            const auto folder = std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT); std::filesystem::create_directories(folder);
            Require(doc.SavePoseFile(id,folder/"full.mvxpose.json")); Require(doc.SavePoseFile(id,folder/"partial.mvxpose.json",true));
            Require(doc.ClearPoseOverrides(id)); Require(doc.LoadPoseFile(id,folder/"partial.mvxpose.json"));
            Require(doc.GetScene().GetObject(id).Animation.Overrides().Joints.size() == 1,"Editor partial pose reload failed.");
        }
#if defined(_WIN32)
        CheckViewportPixels(ui.Viewport);
#endif
        const auto* browser=ImGui::FindWindowByName("Asset Browser");
        const auto previousSelection=doc.Selection();const auto previousCount=doc.GetScene().Size();
        const auto source=doc.Assets().front()->SourcePath;
        ui.DropFiles({source},browser->Pos.x+20,browser->Pos.y+40);
        Require(ui.HasImportPreview(),"Asset Browser drop did not show an Import Preview.");Require(ConfirmPreview(ui),"Asset Browser import could not commit.");
        Require(doc.GetScene().Size() == previousCount && doc.Assets().size() == 1,"Dropping on Asset Browser unexpectedly placed an instance.");
        const auto* viewport=ImGui::FindWindowByName("Viewport");
        ui.DropFiles({source},viewport->Pos.x+viewport->Size.x*.5f,viewport->Pos.y+viewport->Size.y*.5f);
        Require(ConfirmPreview(ui),"Viewport drop could not commit.");
        Require(doc.GetScene().Size() == previousCount+1,"Dropping on Viewport failed to place an instance.");
        Require(doc.RemoveObject(doc.Selection()));Require(doc.Select(previousSelection));
        app.FirstTarget = ui.Viewport.ColorImage().id; app.FirstWidth = ui.Viewport.Width();
        const auto output = std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT)/(app.TelemetrySmoke ? "telemetry-smoke.mvxscene.json":app.QualitySmoke ? "quality-smoke.mvxscene.json":app.AnimationSmoke ? "animation-smoke.mvxscene.json" : "smoke.mvxscene.json");
        std::filesystem::create_directories(output.parent_path()); Require(doc.Save(output));
        ui.RequestNew(); Require(doc.GetScene().Size() == 0,"New Scene did not reset the document.");
        ui.RequestOpen(output); Require(doc.GetScene().Size() == 3 && doc.Assets().size() == 1 && !doc.IsDirty(),"Editor saved Scene did not reopen.");
        if (app.AnimationSmoke) {
            const auto id = doc.GetScene().ObjectIds()[1]; Require(doc.Select(id));
            Require(doc.GetScene().GetObject(id).Animation.State() == lv::PlaybackState::Playing && doc.GetScene().GetObject(id).Animation.Overrides().Joints.size() == 1,"Editor Scene did not restore animation and pose.");
            Require(doc.GetScene().GetObject(id).Animation.Additive() && doc.GetScene().GetObject(id).Animation.Additive()->Clip==2,"Editor Scene did not restore its additive preview layer.");
            Require(doc.GetScene().GetObject(id).Animation.AnimationEvents(0).size()==1,"Editor Scene did not restore its event markers.");
        }
    }
    if (app.AnimationSmoke && app.Frames == 0) { app.PreviousTime = doc.GetScene().GetObject(doc.Selection()).Animation.Time(); }
    if (app.TelemetrySmoke && app.Frames == 2) {
        const auto* panel=ImGui::FindWindowByName("Performance");Require(panel && panel->DockId && !panel->Hidden,"Performance panel was not visible and docked.");
        auto t=ui.Viewport.Telemetry();Require(t.Hardware.ActiveGraphicsDevice && t.Frames.SampleCount > 0 && t.Resident.TotalBytes > 0,"Editor did not collect real device, frame and resource statistics.");
        lv::QualityEstimate current;current.Quality=t.Quality;current.Cost=t.Resident;current.AssetSignature=t.ResidentAssetSignature;
        auto r=lv::RecommendQuality(t.Quality.Capabilities,t.System,doc.GetScene(),ui.Viewport.OutputWidth(),ui.Viewport.OutputHeight(),&current,&t.Frames);
        Require(r.Profiles.size() == 3 && !r.PerformanceValidated,"Editor quality comparison lost initial recommendation assessments.");
        app.RecommendedQuality=r.Preset;Require(!doc.IsDirty(),"Telemetry collection dirtied scene.");
        if (r.Preset) {auto recommended=lv::MakeQualityProfile(*r.Preset);if (app.QualitySmoke) {recommended.Overrides.TargetFps=0;}ui.RequestQualityProfile(std::move(recommended));}
        const auto dir=std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT);Require(lv::SaveTelemetry(t,dir/"telemetry-smoke.json"));Require(lv::SaveTelemetry(t,dir/"telemetry-smoke.csv",lv::TelemetryFormat::Csv));
        std::puts("Editor telemetry: visible Performance panel, active device/counters, profile comparison/application and JSON/CSV exports passed.");
    }
    if (app.QualitySmoke && app.Frames == 2) {Require(ui.Viewport.Quality().Requested.Base == lv::QualityPreset::High && ui.Viewport.Quality().Applied,"Editor High quality did not commit.");Require(!doc.IsDirty(),"Quality application dirtied Scene data.");}
    if (app.TelemetrySmoke && app.Frames == 3 && app.RecommendedQuality) {Require(ui.Viewport.Quality().Requested.Base == *app.RecommendedQuality && !doc.IsDirty(),"Queued telemetry recommendation did not apply safely before UI commands.");}
    if (app.Frames == 3) {Require(ui.HasImportPreview() && ImGui::IsPopupOpen("Import Preview") && ui.ImportPreviewModel()->Format == lv::AssetFormat::Obj && doc.Assets().size() == 1,"Actual Import Preview modal did not display OBJ without changing the document.");}
    if (app.Frames == 4) {Require(!ui.HasImportPreview() && doc.Assets().size() == 1,"Escape did not cancel the actual Import Preview.");ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);}
    if (app.Frames == 5) {const auto* inspector=ImGui::FindWindowByName("Asset Inspector");Require(inspector && inspector->Active && !inspector->Hidden,"Asset Inspector did not render its selected model information.");}
    if (app.QualitySmoke && app.Frames == 4) {app.CachedFrames=ui.Viewport.RenderedFrames();}
    if (app.QualitySmoke && app.Frames == 5) {Require(ui.Viewport.RenderedFrames() == app.CachedFrames,"Static Editor viewport did not reuse its on-demand image.");}
    if (app.QualitySmoke && app.Frames == 6) {
        Require(ui.Viewport.RenderedFrames() > app.CachedFrames,"Scene edit failed to wake Editor viewport.");
        const auto* panel=ImGui::FindWindowByName("Quality Settings");const auto* performance=ImGui::FindWindowByName("Performance");
        Require(panel && !panel->Hidden && performance && performance->Hidden && ui.SelectedQualityEstimate().has_value(),"Quality Settings did not analyze selected cost independently of the hidden Performance panel.");
    }
    if (app.Frames == 7) {
        Require(ui.Viewport.Texture().id != 0,"Viewport did not resume after suspension.");
        const auto* debug=ImGui::FindWindowByName("Render Debugger");const auto* animation=ImGui::FindWindowByName("Animation Debugger");const auto info=ui.Viewport.Inspect();
        Require(debug && debug->DockId && animation && animation->DockId && info.LineCount>0 && info.LineDrawCalls==1,"Editor debug panels or actual skeleton/bounds overlay were not connected.");
        // New/Open/Quit guard is also exercised in the real ImGui frame.
        Require(doc.CreateObject("Unsaved object")); ui.RequestQuit();
    }
    if (app.Frames == 8) {
        if (app.AnimationSmoke) {Require(std::any_of(ui.ConsoleEntries().begin(),ui.ConsoleEntries().end(),[](const auto& entry){return entry.Message.find("EditorEvent")!=std::string::npos;}),"Editor did not consume animation events into its log.");}
        Require(!ui.QuitRequested() && ImGui::IsPopupOpen("Unsaved changes"),"Unsaved document quit was not guarded.");
        std::printf("Editor %s smoke: docked panels, shared offscreen rendering, resize/suspend/resume, save/reopen and unsaved guard passed.\n",app.AnimationSmoke ? "animation/pose" : "scene");
    }
    if (app.Frames == 9) {const auto area=ui.ViewportArea();Require(ui.GizmoVisible() && area[2]>10 && area[3]>10,"Transform Gizmo is not connected to the Viewport.");app.GizmoCenter={area[0]+area[2]*.5f,area[1]+area[3]*.5f};ImGui::GetIO().AddMousePosEvent(app.GizmoCenter.x,app.GizmoCenter.y);ImGui::GetIO().AddMouseButtonEvent(0,true);}
    if (app.Frames == 10) {Require(doc.IsEditing() && ImGuizmo::IsUsing(),"Gizmo click did not begin a grouped edit.");ImGui::GetIO().AddMousePosEvent(app.GizmoCenter.x+40,app.GizmoCenter.y+20);}
    if (app.Frames == 11) {app.GizmoMoved=doc.GetScene().GetObject(app.GizmoObject).LocalTransform;Require(std::abs(app.GizmoMoved.Position.X)+std::abs(app.GizmoMoved.Position.Y)>.01f,"Gizmo drag did not update the selected transform.");ImGui::GetIO().AddMouseButtonEvent(0,false);}
    if (app.Frames == 12) {Require(!doc.IsEditing() && doc.CanUndo(),"Gizmo release did not commit its group.");Require(doc.Undo());Require(std::abs(doc.GetScene().GetObject(app.GizmoObject).LocalTransform.Position.X)<1e-5f,"Gizmo undo did not restore the original transform.");Require(doc.Redo());app.GizmoCenter=GizmoScreenPosition(app);ImGui::GetIO().AddMousePosEvent(app.GizmoCenter.x,app.GizmoCenter.y);}
    // Draw one released/hovered frame after Undo/Redo, then hit the actual
    // projected origin. The previous drag delta is not a stable screen target.
    if (app.Frames == 13) {app.GizmoCenter=GizmoScreenPosition(app);ImGui::GetIO().AddMousePosEvent(app.GizmoCenter.x,app.GizmoCenter.y);ImGui::GetIO().AddMouseButtonEvent(0,true);}
    if (app.Frames == 14) {Require(doc.IsEditing() && ImGuizmo::IsUsing(),"Second Gizmo drag did not begin.");ImGui::GetIO().AddMousePosEvent(app.GizmoCenter.x+20,app.GizmoCenter.y+10);}
    if (app.Frames == 15) {const auto moved=doc.GetScene().GetObject(app.GizmoObject).LocalTransform.Position;Require(std::abs(moved.X-app.GizmoMoved.Position.X)+std::abs(moved.Y-app.GizmoMoved.Position.Y)>.01f,"Second Gizmo drag did not update the transform.");ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);}
    if (app.Frames == 16) {Require(!doc.IsEditing() && doc.GetScene().GetObject(app.GizmoObject).LocalTransform.Matrix().Elements == app.GizmoMoved.Matrix().Elements && doc.Selection() == app.GizmoObject,"Escape did not cancel the Gizmo without clearing selection.");ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ImGui::GetIO().AddMouseButtonEvent(0,false);}
    if (app.Frames == 17) {Require(ui.GizmoVisible(),"Perspective Gizmo is unavailable.");const auto point=GizmoScreenPosition(app);ImGui::GetIO().AddMousePosEvent(point.x,point.y);ImGui::GetIO().AddKeyEvent(ImGuiKey_E,true);}
    if (app.Frames == 18) {Require(ui.TransformTool() == 1,"Viewport tool shortcut was blocked by Gizmo hover.");ImGui::GetIO().AddKeyEvent(ImGuiKey_E,false);std::puts("Editor transform tools: orthographic drag, grouped undo/redo, Escape rollback, perspective overlay and hovered tool shortcut passed.");}
}
void Frame(void* user) noexcept {
    auto& app = *static_cast<Application*>(user);
    if (app.Failed || !app.UI) { sapp_quit(); return; }
    if (sapp_width() <= 0 || sapp_height() <= 0) { return; }
    bool inPass = false;
    try {
        simgui_frame_desc_t frame{}; frame.width = sapp_width(); frame.height = sapp_height();
        frame.delta_time = std::clamp(sapp_frame_duration(),0.00001,0.25); frame.dpi_scale = sapp_dpi_scale();
        if (app.Smoke && app.Frames == 4) {ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);}
        simgui_new_frame(frame);
        const auto advanced = app.UI->Document.UpdateAnimations(frame.delta_time);
        if (!advanced) {if (advanced.Error().Code==lv::ErrorCode::InsufficientResource) {app.UI->Record(lv::editor::ConsoleLevel::Error,advanced.Error().Message);}else {throw std::runtime_error(advanced.Error().Message);}}
        if (app.QualitySmoke && app.Frames == 2) {
            auto high=lv::MakeQualityProfile(lv::QualityPreset::High);high.AllowFallback=true;high.Overrides.TargetFps=0;Require(app.UI->ApplyQualityProfile(high));
            const auto file=std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT)/"profile.mvxquality.json";Require(lv::SaveQuality(high,file));app.UI->LoadQualityProfile(file);
        }
        if (app.QualitySmoke && app.Frames == 4) {
            Require(app.UI->Document.PauseAnimation(app.UI->Document.Selection()));auto p=app.UI->Viewport.Quality().Requested;p.Overrides.OnDemand=lv::OnDemandMode::On;p.Overrides.Shadow=lv::ShadowMode::Pcss;p.Overrides.ShadowSourceAngle=2.f;Require(app.UI->ApplyQualityProfile(p));
        }
        if (app.QualitySmoke && app.Frames == 6) {
            const auto prior=app.UI->Viewport.ColorImage().id;auto p=app.UI->Viewport.Quality().Requested;p.AllowFallback=false;p.Overrides.ShadowBlockerSamples=0;
            Require(!app.UI->ApplyQualityProfile(p) && prior == app.UI->Viewport.ColorImage().id,"Editor quality rejection did not retain GPU resources.");
            const auto applied=app.UI->Viewport.Quality().Requested;const auto dirty=app.UI->Document.IsDirty();
            auto draft=applied;draft.Overrides.TargetFps=120;app.UI->SetQualityDraft(draft);
            Require(app.UI->QualityDraft() == draft && app.UI->Viewport.Quality().Requested == applied,"Editing Custom draft applied quality prematurely.");
            app.UI->ResetQualityDraft();Require(app.UI->QualityDraft() == applied && app.UI->Document.IsDirty() == dirty && prior == app.UI->Viewport.ColorImage().id,"Reset to applied changed Scene dirty state or GPU resources.");
            lv::Transform moved;moved.Position.X=-1.2f;Require(app.UI->Document.SetTransform(app.UI->Document.Selection(),moved));
        }
        if (app.Smoke && app.Frames == 3) {
            // Resize/suspend before building Image commands that reference views.
            app.UI->Viewport.Resize(128,64);
            Require(app.UI->Viewport.ColorImage().id != app.FirstTarget && app.UI->Viewport.Width() != app.FirstWidth,"Viewport resize did not replace the target.");
            app.UI->Viewport.Render(app.UI->Document.GetScene());
            app.UI->Viewport.Resize(0,0); Require(!app.UI->Viewport.Texture().id,"Suspended viewport retained its target.");
            app.UI->Import(std::filesystem::path(MINVEX_EDITOR_ASSET_FIXTURES)/"Compatibility.obj",false);
            AwaitPreview(*app.UI);
        }
        if (app.Smoke && app.Frames == 5) {ImGui::SetWindowFocus("Asset Inspector");}
        if (app.TelemetrySmoke && app.Frames == 1) {ImGui::SetWindowFocus("Performance");}
        if (app.QualitySmoke && app.Frames == 6) {ImGui::SetWindowFocus("Quality Settings");}
        if (app.Smoke && app.Frames == 7) {const auto dirty=app.UI->Document.IsDirty();app.UI->ShowDebuggers();lv::DebugOptions debug;debug.View=lv::DebugView::Normal;debug.ShowSkeleton=debug.ShowBounds=debug.ShowReferencePose=true;debug.DepthTest=false;Require(app.UI->Viewport.SetDebugOptions(debug));Require(app.UI->Document.IsDirty() == dirty,"Debug display dirtied the Scene.");}
        if (app.Smoke && app.Frames == 9) {auto& doc=app.UI->Document;app.UI->ResolvePending(lv::editor::UnsavedChoice::Cancel);ImGui::ClosePopupToLevel(0,true);doc.New();app.GizmoObject=doc.CreateObject("Gizmo test").Value();lv::Camera camera;camera.Position={0,0,10};camera.Target={};camera.Projection=lv::CameraProjection::Orthographic;camera.OrthographicHeight=6;Require(doc.SetCamera(camera));ImGui::SetWindowFocus("Viewport");}
        if (app.Smoke && app.Frames == 17) {auto camera=app.UI->Document.GetScene().MainCamera;camera.Projection=lv::CameraProjection::Perspective;Require(app.UI->Document.SetCamera(camera));}
        if (app.UiSmoke) {
            auto& ui=app.UI->Viewport.Ui;
            if (app.Frames==0) {lv::UiElement button;button.Kind=lv::UiKind::Button;button.Name="UI smoke";button.Text="Minvex 한글";button.Normal.Fill.fill({.3f,.4f,.8f,1});app.UiButton=ui.Add(button).Value();const auto directory=std::filesystem::path(MINVEX_EDITOR_TEST_OUTPUT)/"ui";Require(ui.SetFontDirectory(directory/"Font",true));Require(ui.Save(directory/"screen.mvxui.json"));Require(ui.Load(directory/"screen.mvxui.json"));app.UiButton=ui.Elements().Value().front().Id;}
            if (app.Frames==2) {auto button=ui.Get(app.UiButton).Value();button.Typography.FontFile="missing.ttf";Require(ui.Set(app.UiButton,button));}
            if (app.Frames==3) {auto button=ui.Get(app.UiButton).Value();button.Typography.FontFile.clear();Require(ui.Set(app.UiButton,button));}
            if (app.Frames==4) {Require(ui.Clear());}
            if (app.Frames==5) {lv::UiElement text;text.Text="Recreated overlay";app.UiButton=ui.Add(text).Value();app.UI->Viewport.Resize(640,360);}
        }
        app.UI->Draw();
        app.UI->Viewport.Render(app.UI->Document.GetScene());
        if (app.UiSmoke) {if (app.Frames>0) {Require(app.UI->Viewport.Texture().id!=0,"UI smoke lost its viewport texture.");}Require(app.Frames==2 ? app.UI->Viewport.UiError.has_value():!app.UI->Viewport.UiError,"UI preview font failure did not recover.");if (app.Frames==7) {std::puts("Editor UI: save/load, Unicode overlay, missing font recovery, clear and resize passed.");}}
        if (app.Smoke) { SmokeAfterDraw(app); }
        simgui_flush();
        sg_pass pass{}; pass.swapchain = sglue_swapchain();
        pass.action.colors[0].clear_value = {0.04f,0.045f,0.055f,1};
        sg_begin_pass(pass); inPass = true; simgui_draw(); sg_end_pass(); inPass = false; sg_commit();
        ++app.Frames;
        if (app.UI->QuitRequested() || (app.Limit && app.Frames >= app.Limit)) { sapp_quit(); }
    } catch (const std::exception& e) { if (inPass) { sg_end_pass(); } app.Fail(e.what()); }
    catch (...) { if (inPass) { sg_end_pass(); } app.Fail("Editor frame threw an unknown exception."); }
}
void Event(const sapp_event* event, void* user) noexcept {
    auto& app = *static_cast<Application*>(user);
    if (!app.UI || app.Failed) { return; }
    try {
        if (event->type == SAPP_EVENTTYPE_QUIT_REQUESTED) { sapp_cancel_quit(); app.UI->RequestQuit(); return; }
        if (app.Gui) { simgui_handle_event(event); }
        if (event->type == SAPP_EVENTTYPE_FILES_DROPPED) {
            std::vector<std::filesystem::path> paths;
            for (int i = 0; i < sapp_get_num_dropped_files(); ++i) {
                paths.push_back(lv::detail::PathFromUtf8(sapp_get_dropped_file_path(i)));
            }
            app.UI->DropFiles(paths,event->mouse_x/std::max(1.0f,sapp_dpi_scale()),event->mouse_y/std::max(1.0f,sapp_dpi_scale()));
        }
    } catch (const std::exception& e) { app.Fail(e.what()); }
    catch (...) { app.Fail("Editor input threw an unknown exception."); }
}
void Cleanup(void* user) noexcept {
    auto& app = *static_cast<Application*>(user);
    if (app.UI) { app.UI->Shutdown(); app.UI.reset(); }
    if (app.Gui) { simgui_shutdown(); app.Gui = false; }
    if (sg_isvalid()) { sg_shutdown(); }
}
int Run(const std::vector<std::string>& args) {
    Application app;
    try {
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--smoke-test") { app.Smoke = true; app.Limit = 10; }
            else if (args[i] == "--ui-smoke-test") {app.UiSmoke=true;app.Limit=8;}
            else if (args[i] == "--animation-smoke-test") { app.Smoke = app.AnimationSmoke = true; app.Limit = 10; }
            else if (args[i] == "--quality-smoke-test") {app.Smoke=app.AnimationSmoke=app.QualitySmoke=true;app.Limit=10;}
            else if (args[i] == "--telemetry-smoke-test") {app.Smoke=app.AnimationSmoke=app.TelemetrySmoke=true;app.Limit=10;}
            else if (args[i] == "--integration-smoke-test") {app.Smoke=app.AnimationSmoke=app.QualitySmoke=app.TelemetrySmoke=true;app.Limit=10;}
            else if (args[i] == "--quality" && i+1 < args.size()) {auto profile=lv::LoadQuality(lv::detail::PathFromUtf8(args[++i]));Require(profile);app.InitialQuality=std::move(profile).Value();}
            else if (args[i] == "--frames" && i+1 < args.size()) {
                const auto& value = args[++i]; auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),app.Limit);
                Require(error == std::errc{} && end == value.data()+value.size() && app.Limit > 0,"--frames requires a positive integer.");
            } else if (args[i] == "--assets" && i+1 < args.size()) { app.Directory = std::filesystem::absolute(lv::detail::PathFromUtf8(args[++i]));app.DirectoryExplicit=true; }
            else if (args[i] == "--preferences" && i+1 < args.size()) {app.Preferences=std::filesystem::absolute(lv::detail::PathFromUtf8(args[++i]));}
            else if (!args[i].starts_with("--") && app.Initial.empty()) { app.Initial = lv::detail::PathFromUtf8(args[i]); }
            else { throw std::invalid_argument("Usage: minvex_editor [model.glb | scene.mvxscene.json] [--assets directory] [--preferences file.json] [--frames N] [--smoke-test]"); }
        }
        if (app.Smoke) {app.Limit=std::max<std::uint64_t>(19,app.Limit);}
        Require(lv::detail::StandaloneMainThread() && lv::detail::StandaloneApplicationAvailable(),"Editor requires a standalone main-thread application.");
        sapp_desc desc{}; desc.user_data = &app; desc.init_userdata_cb = Init; desc.frame_userdata_cb = Frame;
        desc.cleanup_userdata_cb = Cleanup; desc.event_userdata_cb = Event;
        desc.width = 1440; desc.height = 900; desc.window_title = "Minvex Editor"; desc.sample_count = 1;
        desc.high_dpi = true; desc.srgb = true; desc.enable_clipboard = true; desc.clipboard_size = 65536;
        desc.enable_dragndrop = true; desc.max_dropped_files = 32; desc.max_dropped_file_path_length = 16384;
        desc.logger.func = Log; desc.logger.user_data = &app;
        lv::detail::RunStandaloneApplication(desc);
        if (app.Failed) { std::fprintf(stderr,"Editor failed: %s\n",app.Message); return 1; }
        if (app.Smoke && app.Frames < 19) { std::fprintf(stderr,"Editor smoke ended too early.\n"); return 1; }
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
} // namespace
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) { const auto text = std::filesystem::path(argv[i]).u8string(); args.emplace_back(text.begin(),text.end()); }
    return Run(args);
}
#else
int main(int argc, char** argv) { return Run({argv,argv+argc}); }
#endif
