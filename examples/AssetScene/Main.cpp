#include <minuvex/Asset.h>
#include <minuvex/Scene.h>

#include "renderer/SceneRenderer.h"

#include <sokol_gfx.h>
#include <sokol_app.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string_view>

namespace {
struct Application {
    lv::AssetRegistry Assets;
    lv::Scene Scene;
    std::unique_ptr<lv::detail::SceneRenderer> Renderer;
    std::filesystem::path ModelPath;
    lv::ModelHandle Model;
    lv::ObjectId Group = 0, Second = 0;
    lv::Transform FirstTransform, SecondTransform;
    std::size_t Primitives = 0;
    double Elapsed = 0;
    int Frames = 0;
    bool Smoke = false, Paused = false, Failed = false;
} app;

void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message,
         std::uint32_t line, const char* file, void* user) {
    if (level <= 1) { app.Failed = true; }
    slog_func(tag, level, item, message, line, file, user);
}
void Fail(const char* message) {
    std::fprintf(stderr, "Minuvex AssetScene: %s\n", message);
    app.Failed = true;
    sapp_request_quit();
}
void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

void Initialize() {
    try {
        sg_desc graphics{};
        graphics.environment = sglue_environment(); graphics.logger.func = Log;
        sg_setup(graphics);
        Require(sg_isvalid(), "Sokol GFX initialization failed.");
        app.Model = app.Assets.LoadModel(app.ModelPath);
        Require(app.Assets.LoadModel(app.ModelPath) == app.Model, "Registry did not share a repeated load.");
        const auto& bounds = app.Model->ModelBounds;
        const float extent = std::max({bounds.Max.X-bounds.Min.X, bounds.Max.Y-bounds.Min.Y, bounds.Max.Z-bounds.Min.Z});
        Require(extent > 0, "Model bounds are degenerate.");
        const float scale = 1.8f / extent;
        const lv::Vec3 center{(bounds.Min.X+bounds.Max.X)*0.5f, (bounds.Min.Y+bounds.Max.Y)*0.5f, (bounds.Min.Z+bounds.Max.Z)*0.5f};
        app.FirstTransform.Position = {-1.2f-center.X*scale, -center.Y*scale, -center.Z*scale};
        app.FirstTransform.Scale = {scale, scale, scale};
        app.SecondTransform = app.FirstTransform;
        app.SecondTransform.Position.X += 2.4f;
        app.Group = app.Scene.CreateObject("Shared model instances");
        app.Scene.AddModel(app.Model, app.FirstTransform, app.Group);
        app.Second = app.Scene.AddModel(app.Model, app.SecondTransform, app.Group);
        app.Scene.MainCamera.Position = {2.8f, 2.0f, 6.3f};
        app.Scene.MainCamera.FarPlane = 100;
        std::set<std::size_t> meshes;
        for (const auto& part : app.Model->Parts) { meshes.insert(part.MeshIndex); }
        for (auto index : meshes) { app.Primitives += app.Model->Meshes[index].Primitives.size(); }
        app.Renderer = std::make_unique<lv::detail::SceneRenderer>();
        app.Renderer->Prepare(app.Scene);
        const auto stats = app.Renderer->GetStats();
        Require(stats.ResidentModels == 1 && stats.ResidentPrimitives == app.Primitives, "GPU resources were duplicated between instances.");
        std::printf("Minuvex AssetScene | 1 shared asset, 2 instances, %zu GPU primitives\n", stats.ResidentPrimitives);
        for (const auto& warning : app.Model->Warnings) { std::printf("Import note: %s\n", warning.c_str()); }
    } catch (const std::exception& error) { Fail(error.what()); }
}

void Frame() {
    if (app.Failed || !app.Renderer) { sapp_request_quit(); return; }
    if (sapp_width() <= 0 || sapp_height() <= 0) { return; }
    bool inPass = false;
    try {
        if (!app.Paused) { app.Elapsed += std::clamp(sapp_frame_duration(), 0.0, 0.1); }
        lv::Transform group;
        const auto angle = static_cast<float>(app.Elapsed * 0.15);
        group.Rotation = {0, std::sin(angle), 0, std::cos(angle)};
        app.Scene.SetTransform(app.Group, group);
        if (app.Smoke) {
            if (app.Frames == 2) { app.Scene.SetVisible(app.Group, false); }
            if (app.Frames == 3) { app.Scene.SetVisible(app.Group, true); }
            if (app.Frames == 4) {
                auto mirrored = app.SecondTransform;
                mirrored.Scale.X *= -1;
                app.Scene.SetTransform(app.Second, mirrored);
            }
            if (app.Frames == 5) { app.Scene.RemoveObject(app.Second); }
            if (app.Frames == 6) { app.Second = app.Scene.AddModel(app.Model, app.SecondTransform, app.Group); }
        }
        app.Renderer->Prepare(app.Scene);
        auto stats = app.Renderer->GetStats();
        if (app.Smoke) {
            const bool hidden = app.Frames == 2;
            Require(stats.ResidentModels == (hidden ? 0u : 1u), "Visibility/cache lifetime check failed.");
            Require(stats.ResidentPrimitives == (hidden ? 0u : app.Primitives), "GPU mesh sharing check failed.");
        }
        sg_pass pass{};
        pass.swapchain = sglue_swapchain();
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.035f, 0.045f, 0.065f, 1};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1;
        sg_begin_pass(pass); inPass = true;
        app.Renderer->Draw(app.Scene, static_cast<float>(sapp_width()) / static_cast<float>(sapp_height()));
        sg_end_pass(); inPass = false;
        sg_commit();
        if (app.Smoke) {
            std::size_t expected = 0;
            for (const auto& item : app.Scene.CollectRenderItems()) {
                for (const auto& part : item.Model->Parts) { expected += item.Model->Meshes[part.MeshIndex].Primitives.size(); }
            }
            Require(app.Renderer->GetStats().LastDrawCalls == expected, "Scene draw submission count is incorrect.");
        }
        ++app.Frames;
        if (app.Smoke && app.Frames >= 8) { sapp_request_quit(); }
    } catch (const std::exception& error) { if (inPass) { sg_end_pass(); } Fail(error.what()); }
}

void Cleanup() {
    app.Renderer.reset();
    if (sg_isvalid()) { sg_shutdown(); }
    if (app.Failed || (app.Smoke && app.Frames < 8)) { std::exit(EXIT_FAILURE); }
    if (app.Smoke) { std::puts("Minuvex Phase 2 smoke test passed: shared asset/GPU buffers, hierarchy, visibility, mirrored scale, removal and re-upload (8 frames)."); }
}
void Event(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && !event->key_repeat) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) { sapp_request_quit(); }
        if (event->key_code == SAPP_KEYCODE_SPACE) { app.Paused = !app.Paused; }
    }
}
} // namespace

int main(int argc, char* argv[]) {
    app.ModelPath = std::filesystem::absolute(argv[0]).parent_path() / "assets" / "SharedBox.glb";
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--smoke-test") { app.Smoke = true; }
        else if (argument == "--model" && i + 1 < argc) { app.ModelPath = argv[++i]; }
        else if (argument == "--help") {
            std::puts("Usage: minuvex_asset_scene [--model model.gltf|model.glb] [--smoke-test]\nTwo instances share one asset. Space pauses rotation; Esc exits.");
            return 0;
        } else { std::fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]); return 2; }
    }
    sapp_desc window{};
    window.init_cb = Initialize; window.frame_cb = Frame; window.cleanup_cb = Cleanup; window.event_cb = Event;
    window.width = 1280; window.height = 720; window.sample_count = 1; window.high_dpi = true; window.srgb = true;
    window.window_title = "Minuvex | Phase 2 - Shared Asset & Scene"; window.logger.func = Log;
    sapp_run(window);
    return app.Failed || (app.Smoke && app.Frames < 8) ? 1 : 0;
}
