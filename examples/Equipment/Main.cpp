#include <minvex/Viewer.h>
#include "Diagnostics.h"
#include "EquipmentScene.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

using equipment::Character;
using equipment::Cube;
using equipment::Rigid;

int main(int argc, char** argv) {
    try {
        lv::ViewerRunOptions run; bool exercise = false, vsync = true;
        std::filesystem::path profilePath; std::optional<int> targetFps;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--frames" && i + 1 < argc) {
                const std::string_view number = argv[++i]; const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), run.FrameLimit);
                if (error != std::errc{} || end != number.data() + number.size()) { return 2; }
            } else if (argument == "--exercise") { exercise = true; }
            else if (argument == "--no-vsync") { vsync = false; }
            else if (argument == "--profile" && i + 1 < argc) { profilePath = argv[++i]; }
            else if (argument == "--target-fps" && i + 1 < argc) {
                const std::string_view number = argv[++i]; int value = 0;
                const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
                if (error != std::errc{} || end != number.data() + number.size() || value < 0 || value > 240) { return 2; } targetFps = value;
            }
            else { std::puts("Usage: minvex_equipment [--frames N] [--exercise] [--profile file.json] [--target-fps 0..240] [--no-vsync]\nSpace: cycle gear; Shift+Space: detach/attach weapon"); return argument == "--help" ? 0 : 2; }
        }
        lv::ViewerOptions options; options.AutoFrame = false; options.Title = "Minvex Equipment | Space: gear | Shift+Space: detach weapon";
        options.Quality = lv::MakeQualityProfile(lv::QualityPreset::Medium); options.Quality.AllowFallback = true;
        options.VSync = vsync;
        // VSync already paces this animation preview to the display. An explicit
        // --target-fps remains available for testing the software limiter.
        const int fps = targetFps.value_or(vsync ? 0 : 60);
        options.Quality.Overrides.TargetFps = fps;
        lv::Viewer viewer(options); auto& scene = viewer.GetScene();
        const auto bodyModel = Character(false), clothingModel = Character(true);
        lv::SkeletonSocket hand{"Weapon", "RightHand"}; hand.LocalTransform.Position = {0, -.25f, .1f};
        lv::SkeletonSocket head{"Hat", "Head"}; head.LocalTransform.Position.Y = .28f;
        const auto shared = lv::CreateSharedSkeleton(bodyModel->Skeleton, {hand, head});
        const auto body = scene.AddModel(bodyModel), clothes = scene.AddModel(clothingModel, {}, body);
        scene.SetSharedSkeleton(body, shared); scene.SetSharedSkeleton(clothes, shared); scene.SetLeaderPose(clothes, body); scene.GetAnimation(body).Play(0);
        lv::MeshData hatMesh; Cube(hatMesh, {}, {.65f,.08f,.6f}); Cube(hatMesh, {0,.15f,0}, {.45f,.25f,.45f});
        const auto hatNode = scene.CreateAttachmentNode(body, "Hat"); scene.AddModel(Rigid("Hat", std::move(hatMesh), {.95f,.65f,.08f,1}), {}, hatNode);
        lv::MeshData sword; Cube(sword, {0,-.35f,0}, {.08f,.85f,.08f}); Cube(sword, {0,.02f,0}, {.35f,.06f,.12f});
        const auto weaponNode = scene.CreateAttachmentNode(clothes, "Weapon"); scene.AddModel(Rigid("Weapon", std::move(sword), {.75f,.82f,.9f,1}), {}, weaponNode);
        lv::MeshData ground; Cube(ground, {0,-.07f,0}, {5,.1f,4}); scene.AddModel(Rigid("Ground", std::move(ground), {.15f,.18f,.23f,1}));
        scene.MainCamera.Position = {3, 2.5f, 5}; scene.MainCamera.Target = {0, 1.1f, 0};
        bool detached = false; unsigned gear = 0;
        const auto detach = [&] { if (detached) { scene.AttachToSocket(weaponNode, clothes, "Weapon"); } else { scene.DetachFromSocket(weaponNode); } detached = !detached; };
        const auto cycle = [&] { gear = (gear + 1) % 4; scene.SetVisible(clothes, gear != 1); scene.SetVisible(hatNode, gear != 2); scene.SetVisible(weaponNode, gear != 3); };
        run.OnInput = [&](lv::Viewer&, const lv::ViewerInputEvent& event) {
            if (event.Type != lv::ViewerInputType::KeyDown || event.Key != lv::ViewerKey::Space || event.Repeat) { return false; }
            if (event.Shift) { detach(); } else { cycle(); } return true;
        };
        equipment::Diagnostics diagnostics;
        if (exercise || !profilePath.empty()) { run.OnFrame = [&](lv::Viewer&, const lv::ViewerFrameInfo& frame) {
            if (exercise) { if (frame.FrameNumber == 1 || frame.FrameNumber == 3) { detach(); } if (frame.FrameNumber == 4 || frame.FrameNumber == 5) { cycle(); } }
            if (!profilePath.empty()) { diagnostics.Record(frame, scene, body, clothes); }
        }; }
        const auto result = viewer.Run(run); if (!result) { std::fprintf(stderr, "%s\n", result.Error().Message.c_str()); return 1; }
        if (!profilePath.empty()) { const auto telemetry = viewer.GetTelemetry(); if (!telemetry) { throw std::runtime_error(telemetry.Error().Message); } diagnostics.Save(profilePath, telemetry.Value(), vsync, fps); }
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
