#include <minvex/Math.h>
#include <minvex/Mesh.h>

#include "renderer/MeshRenderer.h"
#include "RuntimePlatform.h"

#include <sokol_app.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <numbers>
#include <string_view>

namespace {

struct Application {
    std::unique_ptr<lv::detail::MeshRenderer> Renderer;
    double Elapsed = 0.0;
    int Frames = 0;
    bool Paused = false;
    bool SmokeTest = false;
    bool Failed = false;
} app;

void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message,
         std::uint32_t line, const char* file, void* userData) {
    if (level <= 1) {
        app.Failed = true;
    }
    slog_func(tag, level, item, message, line, file, userData);
}

void Fail(const char* message) {
    std::fprintf(stderr, "Minvex: %s\n", message);
    app.Failed = true;
    sapp_request_quit();
}

void Initialize() {
    try {
        sg_desc graphics{};
        graphics.environment = sglue_environment();
        graphics.logger.func = Log;
        sg_setup(graphics);
        if (!sg_isvalid()) {
            Fail("Sokol GFX initialization failed.");
            return;
        }
        app.Renderer = std::make_unique<lv::detail::MeshRenderer>();
        app.Renderer->Initialize(lv::MakeCube());
        std::puts("Minvex Foundation | Space: pause/resume | Esc: quit");
    } catch (const std::exception& error) {
        Fail(error.what());
    }
}

void Frame() {
    if (app.Failed || !app.Renderer) {
        sapp_request_quit();
        return;
    }
    const int width = sapp_width();
    const int height = sapp_height();
    if (width <= 0 || height <= 0) {
        return;
    }
    if (!app.Paused) {
        app.Elapsed += std::clamp(sapp_frame_duration(), 0.0, 0.1);
    }
    try {
        const auto projection = lv::Mat4::Perspective(std::numbers::pi_v<float> / 3.0f,
            static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.0f);
        const auto view = lv::Mat4::LookAt({1.8f, 1.3f, 2.5f}, {0, 0, 0}, {0, 1, 0});
        const auto model = lv::Mat4::RotationY(static_cast<float>(app.Elapsed * 0.6)) *
            lv::Mat4::RotationX(-0.18f);

        sg_pass pass{};
        pass.swapchain = sglue_swapchain();
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0.035f, 0.045f, 0.065f, 1.0f};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
        sg_begin_pass(pass);
        app.Renderer->Draw(model, projection * view);
        sg_end_pass();
        sg_commit();
        ++app.Frames;
        if (app.SmokeTest && app.Frames >= 8) {
            sapp_request_quit();
        }
    } catch (const std::exception& error) {
        Fail(error.what());
    }
}

void Cleanup() {
    app.Renderer.reset(); // Destroy GPU objects before the device/context.
    if (sg_isvalid()) {
        sg_shutdown();
    }
    if (app.SmokeTest && !app.Failed && app.Frames >= 8) {
        std::puts("Minvex render smoke test passed (8 frames).");
    }
    // Cocoa's application loop terminates the process instead of returning to
    // main. Preserve failure exit codes on macOS as well as Windows.
    if (app.Failed || (app.SmokeTest && app.Frames < 8)) {
        std::exit(EXIT_FAILURE);
    }
}

void Event(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && !event->key_repeat) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) { sapp_request_quit(); }
        if (event->key_code == SAPP_KEYCODE_SPACE) { app.Paused = !app.Paused; }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--smoke-test") {
            app.SmokeTest = true;
        } else if (argument == "--help") {
            std::puts("Usage: minvex_foundation [--smoke-test]\nRenders a lit cube. Space pauses rotation; Esc exits.");
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    sapp_desc window{};
    window.init_cb = Initialize;
    window.frame_cb = Frame;
    window.cleanup_cb = Cleanup;
    window.event_cb = Event;
    window.width = 1280;
    window.height = 720;
    window.sample_count = 1;
    window.high_dpi = true;
    window.window_title = "Minvex | Phase 1 - Foundation Mesh";
    window.logger.func = Log;
    lv::detail::RunStandaloneApplication(window);
    return (app.Failed || (app.SmokeTest && app.Frames < 8)) ? 1 : 0;
}
