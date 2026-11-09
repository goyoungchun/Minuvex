#include "EditorUI.h"
#include "RuntimePlatform.h"
#include "core/Utf8Path.h"

#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <memory>
#if defined(_WIN32)
#include <d3d11.h>
#include <wrl/client.h>
#undef GetObject
#endif

namespace {
struct Application {
    std::unique_ptr<lv::editor::EditorUI> UI;
    std::filesystem::path Initial, Directory = std::filesystem::current_path();
    std::uint64_t Frames = 0, Limit = 0;
    bool Smoke = false, AnimationSmoke = false, QualitySmoke = false, Gui = false, Failed = false;
    std::optional<lv::QualityProfile> InitialQuality;
    std::uint64_t CachedFrames=0;
    double PreviousTime = 0;
    char Message[2048]{};
    std::uint32_t FirstTarget = 0;
    int FirstWidth = 0;
    void Fail(const char* message) noexcept { Failed = true; std::snprintf(Message,sizeof(Message),"%s",message); sapp_quit(); }
};
void Require(bool valid, const char* message) { if (!valid) { throw std::runtime_error(message); } }
template<typename T> void Require(const lv::Result<T>& result) { if (!result) { throw std::runtime_error(result.Error().Message); } }
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
    if (level <= 1 && !(app.UI && app.UI->Viewport.ApplyingQuality())) { app.Fail(message ? message : "Editor graphics backend failure."); }
    slog_func(tag,level,item,message,line,file,nullptr);
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
        io.IniFilename = app.Smoke ? nullptr : ".minuvex-editor.ini";
#if defined(_WIN32)
        const char* font = "C:/Windows/Fonts/malgun.ttf";
#else
        const char* font = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
#endif
        if (std::filesystem::exists(font)) { io.FontDefault = io.Fonts->AddFontFromFileTTF(font,17); }
        ImGui::StyleColorsDark(); ImGui::GetStyle().WindowRounding = 3; ImGui::GetStyle().FrameRounding = 3;
        app.UI = std::make_unique<lv::editor::EditorUI>(); app.UI->SetDirectory(app.Directory);
        if (!app.Initial.empty()) {
            if (app.Initial.extension() == ".json") { app.UI->RequestOpen(app.Initial); }
            else { app.UI->Import(app.Initial,true); }
        }
        if (app.Smoke) {
            auto& doc = app.UI->Document;
            const auto asset = doc.ImportModel(app.AnimationSmoke ? MINUVEX_EDITOR_ANIMATION_SAMPLE : MINUVEX_EDITOR_SAMPLE); Require(asset);
            auto group = doc.CreateObject("Editor test group"); Require(group);
            lv::Transform t; t.Position = {-1.5f,0,0}; auto left = doc.PlaceModel(asset.Value(),t,group.Value()); Require(left);
            t.Position.X = 1.5f; Require(doc.PlaceModel(asset.Value(),t,group.Value()));
            if (app.AnimationSmoke) {
                Require(doc.Select(left.Value())); Require(doc.PlayAnimation(left.Value(),0));
                auto local = doc.GetScene().GetObject(left.Value()).Model->Skeleton.Joints[1].ReferenceLocal; local.Position.X = .25f;
                Require(doc.SetJointPose(left.Value(),1,local));
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
        for (const auto* name : {"Viewport","Hierarchy","Inspector","Asset Browser"}) {
            const auto* window = ImGui::FindWindowByName(name);
            Require(window && window->Active && window->DockId,"Editor panel is missing from the docking layout.");
        }
        Require(ui.Viewport.Width() > 0 && sg_query_view_state(ui.Viewport.Texture()) == SG_RESOURCESTATE_VALID,"Editor viewport texture is unavailable.");
        Require(ui.Viewport.Stats().ResidentModels == 1 && ui.Viewport.Stats().LastDrawCalls == (app.AnimationSmoke ? 2u : 4u),"Editor instances did not share the GPU model.");
        if (app.AnimationSmoke) {
            for (const auto* name : {"Bone Inspector","Animation Controls"}) { const auto* window = ImGui::FindWindowByName(name); Require(window && window->DockId,"Animation panel was not docked."); }
            const auto id = doc.Selection(); const auto& player = doc.GetScene().GetObject(id).Animation;
            Require(player.Time() > app.PreviousTime && player.Overrides().Joints.size() == 1,"Editor tick did not advance animation while preserving overrides.");
            const auto folder = std::filesystem::path(MINUVEX_EDITOR_TEST_OUTPUT); std::filesystem::create_directories(folder);
            Require(doc.SavePoseFile(id,folder/"full.mvxpose.json")); Require(doc.SavePoseFile(id,folder/"partial.mvxpose.json",true));
            Require(doc.ClearPoseOverrides(id)); Require(doc.LoadPoseFile(id,folder/"partial.mvxpose.json"));
            Require(doc.GetScene().GetObject(id).Animation.Overrides().Joints.size() == 1,"Editor partial pose reload failed.");
        }
#if defined(_WIN32)
        CheckViewportPixels(ui.Viewport);
#endif
        app.FirstTarget = ui.Viewport.ColorImage().id; app.FirstWidth = ui.Viewport.Width();
        const auto output = std::filesystem::path(MINUVEX_EDITOR_TEST_OUTPUT)/(app.QualitySmoke ? "quality-smoke.mvxscene.json":app.AnimationSmoke ? "animation-smoke.mvxscene.json" : "smoke.mvxscene.json");
        std::filesystem::create_directories(output.parent_path()); Require(doc.Save(output));
        ui.RequestNew(); Require(doc.GetScene().Size() == 0,"New Scene did not reset the document.");
        ui.RequestOpen(output); Require(doc.GetScene().Size() == 3 && doc.Assets().size() == 1 && !doc.IsDirty(),"Editor saved Scene did not reopen.");
        if (app.AnimationSmoke) {
            const auto id = doc.GetScene().ObjectIds()[1]; Require(doc.Select(id));
            Require(doc.GetScene().GetObject(id).Animation.State() == lv::PlaybackState::Playing && doc.GetScene().GetObject(id).Animation.Overrides().Joints.size() == 1,"Editor Scene did not restore animation and pose.");
        }
    }
    if (app.AnimationSmoke && app.Frames == 0) { app.PreviousTime = doc.GetScene().GetObject(doc.Selection()).Animation.Time(); }
    if (app.QualitySmoke && app.Frames == 2) {Require(ui.Viewport.Quality().Requested.Base == lv::QualityPreset::High && ui.Viewport.Quality().Applied,"Editor High quality did not commit.");Require(!doc.IsDirty(),"Quality application dirtied Scene data.");}
    if (app.QualitySmoke && app.Frames == 4) {app.CachedFrames=ui.Viewport.RenderedFrames();}
    if (app.QualitySmoke && app.Frames == 5) {Require(ui.Viewport.RenderedFrames() == app.CachedFrames,"Static Editor viewport did not reuse its on-demand image.");}
    if (app.QualitySmoke && app.Frames == 6) {Require(ui.Viewport.RenderedFrames() > app.CachedFrames,"Scene edit failed to wake Editor viewport.");}
    if (app.Frames == 7) {
        Require(ui.Viewport.Texture().id != 0,"Viewport did not resume after suspension.");
        // New/Open/Quit guard is also exercised in the real ImGui frame.
        Require(doc.CreateObject("Unsaved object")); ui.RequestQuit();
    }
    if (app.Frames == 8) {
        Require(!ui.QuitRequested() && ImGui::IsPopupOpen("Unsaved changes"),"Unsaved document quit was not guarded.");
        std::printf("Editor %s smoke: docked panels, shared offscreen rendering, resize/suspend/resume, save/reopen and unsaved guard passed.\n",app.AnimationSmoke ? "animation/pose" : "scene");
    }
}
void Frame(void* user) noexcept {
    auto& app = *static_cast<Application*>(user);
    if (app.Failed || !app.UI) { sapp_quit(); return; }
    if (sapp_width() <= 0 || sapp_height() <= 0) { return; }
    bool inPass = false;
    try {
        simgui_frame_desc_t frame{}; frame.width = sapp_width(); frame.height = sapp_height();
        frame.delta_time = std::clamp(sapp_frame_duration(),0.00001,0.25); frame.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(frame);
        const auto advanced = app.UI->Document.UpdateAnimations(frame.delta_time);
        if (!advanced) { throw std::runtime_error(advanced.Error().Message); }
        if (app.QualitySmoke && app.Frames == 2) {
            auto high=lv::MakeQualityProfile(lv::QualityPreset::High);high.AllowFallback=true;high.Overrides.TargetFps=0;Require(app.UI->ApplyQualityProfile(high));
            const auto file=std::filesystem::path(MINUVEX_EDITOR_TEST_OUTPUT)/"profile.mvxquality.json";Require(lv::SaveQuality(high,file));app.UI->LoadQualityProfile(file);
        }
        if (app.QualitySmoke && app.Frames == 4) {
            Require(app.UI->Document.PauseAnimation(app.UI->Document.Selection()));auto p=app.UI->Viewport.Quality().Requested;p.Overrides.OnDemand=lv::OnDemandMode::On;Require(app.UI->ApplyQualityProfile(p));
        }
        if (app.QualitySmoke && app.Frames == 6) {
            const auto prior=app.UI->Viewport.ColorImage().id;auto p=app.UI->Viewport.Quality().Requested;p.AllowFallback=false;p.Overrides.Shadow=lv::ShadowMode::Pcf;
            Require(!app.UI->ApplyQualityProfile(p) && prior == app.UI->Viewport.ColorImage().id,"Editor quality rejection did not retain GPU resources.");
            lv::Transform moved;moved.Position.X=-1.2f;Require(app.UI->Document.SetTransform(app.UI->Document.Selection(),moved));
        }
        if (app.Smoke && app.Frames == 3) {
            // Resize/suspend before building Image commands that reference views.
            app.UI->Viewport.Resize(128,64);
            Require(app.UI->Viewport.ColorImage().id != app.FirstTarget && app.UI->Viewport.Width() != app.FirstWidth,"Viewport resize did not replace the target.");
            app.UI->Viewport.Render(app.UI->Document.GetScene());
            app.UI->Viewport.Resize(0,0); Require(!app.UI->Viewport.Texture().id,"Suspended viewport retained its target.");
        }
        app.UI->Draw();
        app.UI->Viewport.Render(app.UI->Document.GetScene());
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
            for (int i = 0; i < sapp_get_num_dropped_files(); ++i) {
                const auto path = lv::detail::PathFromUtf8(sapp_get_dropped_file_path(i));
                if (path.filename().generic_u8string().ends_with(u8".mvxquality.json")) {app.UI->LoadQualityProfile(path);break;}
                if (path.filename().generic_u8string().ends_with(u8".mvxpose.json")) {
                    const auto loaded = app.UI->Document.LoadPoseFile(app.UI->Document.Selection(),path);
                    if (!loaded) { app.UI->ShowError(loaded.Error()); } break;
                }
                if (path.extension() == ".json") { app.UI->RequestOpen(path); break; }
                app.UI->Import(path,true);
            }
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
            else if (args[i] == "--animation-smoke-test") { app.Smoke = app.AnimationSmoke = true; app.Limit = 10; }
            else if (args[i] == "--quality-smoke-test") {app.Smoke=app.AnimationSmoke=app.QualitySmoke=true;app.Limit=10;}
            else if (args[i] == "--quality" && i+1 < args.size()) {auto profile=lv::LoadQuality(lv::detail::PathFromUtf8(args[++i]));Require(profile);app.InitialQuality=std::move(profile).Value();}
            else if (args[i] == "--frames" && i+1 < args.size()) {
                const auto& value = args[++i]; auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),app.Limit);
                Require(error == std::errc{} && end == value.data()+value.size() && app.Limit > 0,"--frames requires a positive integer.");
            } else if (args[i] == "--assets" && i+1 < args.size()) { app.Directory = std::filesystem::absolute(lv::detail::PathFromUtf8(args[++i])); }
            else if (!args[i].starts_with("--") && app.Initial.empty()) { app.Initial = lv::detail::PathFromUtf8(args[i]); }
            else { throw std::invalid_argument("Usage: minuvex_editor [model.glb | scene.mvxscene.json] [--assets directory] [--frames N] [--smoke-test]"); }
        }
        Require(lv::detail::StandaloneMainThread() && lv::detail::StandaloneApplicationAvailable(),"Editor requires a standalone main-thread application.");
        sapp_desc desc{}; desc.user_data = &app; desc.init_userdata_cb = Init; desc.frame_userdata_cb = Frame;
        desc.cleanup_userdata_cb = Cleanup; desc.event_userdata_cb = Event;
        desc.width = 1440; desc.height = 900; desc.window_title = "Minuvex Editor"; desc.sample_count = 1;
        desc.high_dpi = true; desc.srgb = true; desc.enable_clipboard = true; desc.clipboard_size = 65536;
        desc.enable_dragndrop = true; desc.max_dropped_files = 32; desc.max_dropped_file_path_length = 16384;
        desc.logger.func = Log; desc.logger.user_data = &app;
        lv::detail::RunStandaloneApplication(desc);
        if (app.Failed) { std::fprintf(stderr,"Editor failed: %s\n",app.Message); return 1; }
        if (app.Smoke && app.Frames < 9) { std::fprintf(stderr,"Editor smoke ended too early.\n"); return 1; }
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
