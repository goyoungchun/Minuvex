// Backend-specific readback belongs to this test, never to the public SDK.
#include "TestHarness.h"
#include "renderer/SceneRenderer.h"

#include <sokol_gfx.h>
#include <sokol_app.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace test;

namespace {
constexpr int Size = 128;
using Pixel = std::array<std::uint8_t, 4>;
bool failed = false;
bool ran = false;

void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message,
         std::uint32_t line, const char* file, void* user) {
    if (level <= 1) { failed = true; }
    slog_func(tag, level, item, message, line, file, user);
}
void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }

struct Target {
    sg_image Color{}, Depth{};
    sg_view ColorView{}, DepthView{};
    Target() {
        sg_image_desc color{};
        color.width = color.height = Size;
        color.pixel_format = SG_PIXELFORMAT_RGBA8;
        color.usage.color_attachment = true;
        Color = sg_make_image(color);
        Require(sg_query_image_state(Color) == SG_RESOURCESTATE_VALID, "Pixel test color target creation failed.");
        sg_image_desc depth{};
        depth.width = depth.height = Size;
        depth.pixel_format = SG_PIXELFORMAT_DEPTH;
        depth.usage.depth_stencil_attachment = true;
        Depth = sg_make_image(depth);
        Require(sg_query_image_state(Depth) == SG_RESOURCESTATE_VALID, "Pixel test depth target creation failed.");
        sg_view_desc colorView{}; colorView.color_attachment.image = Color;
        sg_view_desc depthView{}; depthView.depth_stencil_attachment.image = Depth;
        ColorView = sg_make_view(colorView); DepthView = sg_make_view(depthView);
        Require(sg_query_view_state(ColorView) == SG_RESOURCESTATE_VALID && sg_query_view_state(DepthView) == SG_RESOURCESTATE_VALID,
            "Pixel test target view creation failed.");
    }
    ~Target() {
        sg_destroy_view(ColorView); sg_destroy_view(DepthView);
        sg_destroy_image(Color); sg_destroy_image(Depth);
    }
    std::vector<Pixel> Render(lv::detail::SceneRenderer& renderer, const lv::Scene& scene) {
        renderer.Prepare(scene);
        sg_pass pass{};
        pass.attachments.colors[0] = ColorView; pass.attachments.depth_stencil = DepthView;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0,0,0,1};
        pass.action.depth.load_action = SG_LOADACTION_CLEAR; pass.action.depth.clear_value = 1;
        sg_begin_pass(pass);
        renderer.Draw(scene, 1);
        sg_end_pass(); sg_commit();

        auto* device = static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
        auto* context = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
        auto* texture = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(Color).tex2d));
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())), "Pixel staging texture creation failed.");
        context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "GPU pixel readback failed.");
        std::vector<Pixel> pixels(Size * Size);
        for (int y = 0; y < Size; ++y) {
            std::memcpy(pixels.data() + y * Size, static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, Size * sizeof(Pixel));
        }
        context->Unmap(staging.Get(), 0);
        return pixels;
    }
};

bool Lit(const std::vector<Pixel>& pixels, int x, int y) {
    const auto& pixel = pixels.at(y * Size + x);
    return pixel[0] > 100 || pixel[1] > 100 || pixel[2] > 100;
}

void Run() {
    lv::AssetRegistry registry;
    const auto triangle = registry.LoadModel(Fixture("Sparse.gltf"));
    Target target;
    lv::detail::SceneRenderer renderer;
    lv::Scene scene;
    scene.MainCamera.Position = {0,0,2}; scene.MainCamera.VerticalFovRadians = std::numbers::pi_v<float> / 2;
    scene.Sun.Intensity = 0; scene.Sun.Ambient = 1;
    lv::Transform left; left.Position.X = -0.7f;
    lv::Transform right; right.Position.X = 0.7f;
    const auto first = scene.AddModel(triangle, left);
    const auto second = scene.AddModel(triangle, right);
    auto pixels = target.Render(renderer, scene);
    Check(Lit(pixels,42,64) && Lit(pixels,86,64) && !Lit(pixels,64,64), "GPU pixels contain two separately positioned instances");
    Check(renderer.GetStats().ResidentModels == 1 && renderer.GetStats().ResidentPrimitives == 1, "two rendered instances share one GPU mesh");
    scene.SetVisible(first, false);
    pixels = target.Render(renderer, scene);
    Check(!Lit(pixels,42,64) && Lit(pixels,86,64), "visibility removes only the selected instance's pixels");
    right.Scale.X = -1; scene.SetTransform(second, right);
    pixels = target.Render(renderer, scene);
    Check(Lit(pixels,86,64), "mirrored model remains visible with corrected face winding");
    scene.SetVisible(first, true);
    const auto group = scene.CreateObject();
    lv::Transform parent; parent.Position.Y = 0.6f;
    scene.SetTransform(group, parent); scene.SetParent(first, group);
    pixels = target.Render(renderer, scene);
    Check(Lit(pixels,42,45) && !Lit(pixels,42,64), "parent transform moves child pixels in the render target");
    scene.SetParent(first, lv::NoObject); scene.MainCamera.Position.Z = 4;
    pixels = target.Render(renderer, scene);
    Check(Lit(pixels,53,64) && Lit(pixels,75,64) && !Lit(pixels,42,64), "camera changes affect projected instance positions");
    scene.Sun.Ambient = 0;
    pixels = target.Render(renderer, scene);
    Check(!Lit(pixels,53,64) && !Lit(pixels,75,64), "directional light settings affect GPU shading");

    lv::Scene depthScene;
    depthScene.MainCamera = scene.MainCamera; depthScene.MainCamera.Position.Z = 2;
    depthScene.Sun.Intensity = 0; depthScene.Sun.Ambient = 1;
    auto blue = std::make_shared<lv::ModelAsset>(*triangle), red = std::make_shared<lv::ModelAsset>(*triangle);
    blue->Materials[0].BaseColor = {0,0,1,1}; red->Materials[0].BaseColor = {1,0,0,1};
    lv::Transform foregroundTransform; foregroundTransform.Position.Z = 0.2f;
    depthScene.AddModel(blue, foregroundTransform); depthScene.AddModel(red);
    pixels = target.Render(renderer, depthScene);
    Check(pixels[64*Size+64][2] > 200 && pixels[64*Size+64][0] < 10, "depth test preserves the near model when the far model draws later");

    auto masked = std::make_shared<lv::ModelAsset>(*triangle);
    masked->Materials[0].BaseColor[3] = 0.2f; masked->Materials[0].AlphaCutoff = 0.5f;
    lv::Scene maskScene;
    maskScene.MainCamera = depthScene.MainCamera; maskScene.Sun = depthScene.Sun;
    maskScene.AddModel(masked);
    pixels = target.Render(renderer, maskScene);
    Check(!Lit(pixels,64,64), "MASK material discards pixels below alpha cutoff");

    lv::Scene textured;
    textured.MainCamera = depthScene.MainCamera; textured.MainCamera.Position.Z = 3;
    textured.Sun = depthScene.Sun;
    textured.AddModel(registry.LoadModel(Fixture("SharedBox.glb")));
    pixels = target.Render(renderer, textured);
    bool orange = false, cyan = false;
    for (const auto& pixel : pixels) {
        orange = orange || (pixel[0] > 120 && pixel[1] > 15 && pixel[1] < 100 && pixel[2] < 25);
        cyan = cyan || (pixel[0] < 25 && pixel[1] > 25 && pixel[2] > 35);
    }
    Check(orange && cyan, "PNG base-color texture produces distinct colored GPU pixels");
    Check(renderer.GetStats().ResidentImages == 1 && renderer.GetStats().ResidentPrimitives == 1, "two glTF mesh nodes share their GPU image and mesh");
    textured.RemoveObject(textured.CollectRenderItems()[0].Object);
    pixels = target.Render(renderer, textured);
    Check(!Lit(pixels,42,64) && renderer.GetStats().ResidentModels == 0 && renderer.GetStats().ResidentImages == 0, "removal clears pixels and releases unused GPU resources");
}

void Initialize() {
    sg_desc desc{}; desc.environment = sglue_environment(); desc.logger.func = Log;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    desc.environment.defaults.sample_count = 1;
    sg_setup(desc);
}
void Frame() {
    if (ran) { sapp_request_quit(); return; }
    ran = true;
    try { Require(sg_isvalid(), "Sokol initialization failed."); Run(); }
    catch (const std::exception& error) { Check(false, error.what()); }
    sapp_request_quit();
}
void Cleanup() { if (sg_isvalid()) { sg_shutdown(); } }
} // namespace

int main() {
    sapp_desc app{};
    app.width = app.height = 64; app.window_title = "Minuvex D3D11 pixel tests";
    app.init_cb = Initialize; app.frame_cb = Frame; app.cleanup_cb = Cleanup; app.logger.func = Log;
    sapp_run(app);
    Check(!failed, "Sokol reported no rendering errors");
    return Finish();
}
