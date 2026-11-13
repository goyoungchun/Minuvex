// Backend-specific readback belongs to this test, never to the public SDK.
#include "TestHarness.h"
#include "renderer/SceneRenderer.h"
#include "renderer/RenderSurface.h"
#include "renderer/TextureData.h"
#include "OptimizationFixture.h"

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
bool animationTests = false;
bool qualityTests = false;
bool compatibilityTests = false;
bool pbrTests = false;
bool morphTests = false;

void Log(const char* tag, std::uint32_t level, std::uint32_t item, const char* message,
         std::uint32_t line, const char* file, void* user) {
    if (level <= 1) { failed = true; }
    slog_func(tag, level, item, message, line, file, user);
}
void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
std::vector<Pixel> ReadImage(sg_image image,int width,int height) {
    auto* device=static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));auto* context=static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    auto* texture=static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    Require(desc.SampleDesc.Count == 1,"Readback must use the resolved single-sample image.");desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,staging.GetAddressOf())),"Cannot create quality readback texture.");context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Cannot map quality pixels.");
    std::vector<Pixel> pixels(static_cast<std::size_t>(width)*height);for (int y=0;y<height;++y) {std::memcpy(pixels.data()+y*width,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,static_cast<std::size_t>(width)*4);}context->Unmap(staging.Get(),0);return pixels;
}

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

void RunAnimation() {
    lv::AssetRegistry registry; const auto ribbon = registry.LoadModel(Fixture("AnimatedRibbon.gltf"));
    Target target; lv::detail::SceneRenderer renderer; lv::Scene scene;
    scene.MainCamera.Position = {0,1,5}; scene.MainCamera.Target = {0,1,0};
    scene.Sun.Intensity = 0; scene.Sun.Ambient = 1;
    const auto first = scene.AddModel(ribbon);
    auto reference = target.Render(renderer,scene);
    Check(Lit(reference,64,43) && Lit(reference,64,82) && !Lit(reference,86,43),"reference skin pixels ignore mesh-node translation and include inverse binds");
    scene.GetAnimation(first).Play(3); scene.GetAnimation(first).Seek(2);
    auto moved = target.Render(renderer,scene);
    Check(!Lit(moved,64,43) && Lit(moved,86,43),"animated non-joint ancestor moves skin pixels");
    lv::Transform offset; offset.Position.X = -1; const auto second = scene.AddModel(ribbon,offset);
    auto instances = target.Render(renderer,scene);
    Check(Lit(instances,42,43) && Lit(instances,86,43) && !Lit(instances,64,43),"instances have independent animation poses on shared GPU geometry");
    Check(renderer.GetStats().ResidentModels == 1 && renderer.GetStats().ResidentPrimitives == 1 && renderer.GetStats().LastDrawCalls == 2,"skinned instances share a single resident GPU mesh");
    auto local = ribbon->Skeleton.Joints[1].ReferenceLocal; local.Position.X = .5f;
    scene.GetAnimation(second).SetJointOverride(1,local);
    auto overridden = target.Render(renderer,scene);
    Check(Lit(overridden,53,43) && !Lit(overridden,42,43) && Lit(overridden,86,43),"manual joint override deforms only its own instance");
    scene.SetVisible(second,false); scene.GetAnimation(first).Play(0); scene.GetAnimation(first).Seek(1);
    auto bent = target.Render(renderer,scene);
    const auto box = [](const auto& pixels) {
        std::array<int,4> bounds{Size,Size,-1,-1};
        for (int y=0;y<Size;++y) { for (int x=0;x<Size;++x) { if (Lit(pixels,x,y)) { bounds[0]=std::min(bounds[0],x); bounds[1]=std::min(bounds[1],y); bounds[2]=std::max(bounds[2],x); bounds[3]=std::max(bounds[3],y); } } } return bounds;
    };
    const auto original = box(reference), deformed = box(bent);
    Check(deformed[1] > original[1]+10 && deformed[2]-deformed[0] > original[2]-original[0]+10,"GPU LBS rotates and blends the upper ribbon instead of translating a rigid mesh");
    scene.GetAnimation(first).ResetPose();
    auto reset = target.Render(renderer,scene); Check(reset == reference,"reference reset restores exact original GPU pixels");
    scene.RemoveObject(second);
    auto litModel = std::make_shared<lv::ModelAsset>(*ribbon); litModel->Materials[0].Unlit = false; litModel->Materials[0].BaseColor = {1,1,1,1};
    scene.SetModel(first,litModel); scene.Sun.Direction = {0,0,-1}; scene.Sun.Intensity = 1; scene.Sun.Ambient = 0;
    auto bright = target.Render(renderer,scene);
    local = litModel->Skeleton.Joints[1].ReferenceLocal; local.Rotation = {0,.5f,0,.8660254f};
    scene.GetAnimation(first).SetJointOverride(1,local);
    auto tilted = target.Render(renderer,scene);
    const auto mean = [](const auto& pixels) { double sum=0, count=0; for (const auto& p : pixels) { if (p[0] > 10) { sum+=p[0]; ++count; } } return count ? sum/count : 0; };
    Check(mean(tilted) > mean(bright)*.3 && mean(tilted) < mean(bright)*.7,"skinned normals change directional lighting");
    scene.SetModel(first,registry.LoadModel(Fixture("AnimatedRigid.gltf"))); scene.Sun.Intensity = 0; scene.Sun.Ambient = 1;
    scene.GetAnimation(first).Play(0); scene.GetAnimation(first).Seek(1);
    auto rigid = target.Render(renderer,scene); Check(Lit(rigid,90,43) && !Lit(rigid,68,43),"rigid animation updates node matrices on GPU without a skin");
    scene.RemoveObject(first); (void)target.Render(renderer,scene);
    Check(renderer.GetStats().ResidentModels == 0,"removing skinned instances releases GPU model cache");
}

void RunQuality() {
    lv::AssetRegistry assets;const auto triangle=assets.LoadModel(Fixture("Sparse.gltf"));lv::Scene scene;
    scene.MainCamera.Position={0,0,2};scene.MainCamera.Target={0,0,0};scene.MainCamera.VerticalFovRadians=std::numbers::pi_v<float>/2;scene.Sun.Intensity=0;scene.Sun.Ambient=0;
    const auto object=scene.AddModel(triangle);lv::detail::RenderSurface surface;
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Medium);profile.Overrides.Resolution=lv::ResolutionMode::Fixed;profile.Overrides.FixedWidth=Size;profile.Overrides.FixedHeight=Size;profile.Overrides.TargetFps=0;profile.Overrides.FrustumCulling=false;
    auto applied=surface.ApplyQuality(profile,scene,Size,Size);Require(bool(applied),"Cannot apply initial quality.");
    double now=1;surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();auto dark=ReadImage(surface.ColorImage(),Size,Size);
    Check(!Lit(dark,64,64),"Basic lighting obeys the Scene's zero illumination");
    profile.Overrides.Lighting=lv::LightingMode::Unlit;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot apply Unlit quality.");
    surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();auto bright=ReadImage(surface.ColorImage(),Size,Size);Check(Lit(bright,64,64),"quality Unlit changes actual material shading");
    Target destination;sg_pass pass{};pass.attachments.colors[0]=destination.ColorView;pass.attachments.depth_stencil=destination.DepthView;
    sg_begin_pass(pass);surface.Present();sg_end_pass();sg_commit();
    Check(ReadImage(destination.Color,Size,Size) == bright,"fullscreen presentation preserves image orientation and pixels");
    const auto originalImage=surface.ColorImage().id;const auto originalRequest=surface.Quality().Requested;
    auto invalid=profile;invalid.Overrides.Shadow=lv::ShadowMode::Pcss;
    Check(!surface.ApplyQuality(invalid,scene,Size,Size) && surface.ColorImage().id == originalImage && surface.Quality().Requested == originalRequest,"capability rejection preserves the previous GPU target and request");
    auto bad=std::make_shared<lv::ModelAsset>(*triangle);bad->Meshes[0].Primitives[0].Geometry.Vertices[0].Normal={0,0,0};lv::Scene badScene;badScene.AddModel(bad);
    auto changed=profile;changed.Overrides.Textures=lv::TextureQuality::Low;changed.Overrides.FixedWidth=64;
    Check(!surface.ApplyQuality(changed,badScene,Size,Size) && surface.ColorImage().id == originalImage && surface.Quality().Requested == originalRequest,"failure after candidate target allocation rolls back GPU/settings");
    surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();Check(ReadImage(surface.ColorImage(),Size,Size) == bright,"previous resources still render correctly after rejected application");
    const auto capabilities=surface.Quality().Capabilities;Check(capabilities.GraphicsAvailable && capabilities.Backend == lv::GraphicsBackend::D3D11 && (capabilities.SampleCounts&(1u<<1)),"capabilities come from the active D3D11 device/formats");
    if (capabilities.SampleCounts&(1u<<4)) {
        changed=profile;changed.Overrides.MsaaSamples=4;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot apply supported 4x MSAA.");surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();
        auto antialiased=ReadImage(surface.ColorImage(),Size,Size);bool partial=false;for (const auto& p:antialiased) {partial|=p[0] > 0 && p[0] < 255;}
        Check(partial && surface.Quality().Effective->MsaaSamples == 4 && Lit(antialiased,64,64),"MSAA resolves partial edge coverage into a sampleable image");
    }
    changed=profile;changed.Overrides.FixedWidth=64;changed.Overrides.FixedHeight=32;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot change render resolution.");
    surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();Check(surface.Width() == 64 && surface.Height() == 32 && ReadImage(surface.ColorImage(),64,32)[16*64+32][0] > 100,"fixed render extent recreates target and still displays geometry");
    changed=profile;changed.Overrides.FrustumCulling=true;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot enable culling.");lv::Transform distant;distant.Position.X=100;scene.AddModel(triangle,distant);
    surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();Check(surface.Stats().LastDrawCalls == 1 && surface.Stats().LastCulledParts == 1,"Frustum culling removes offscreen draw submissions");
    changed.Overrides.FrustumCulling=false;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot disable culling.");surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();Check(surface.Stats().LastDrawCalls == 1 && surface.Stats().Optimization.InstancedObjects == 2 && surface.Stats().LastCulledParts == 0,"disabling culling restores both objects in one static instance batch");
    changed=profile;changed.Overrides.OnDemand=lv::OnDemandMode::On;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot enable on-demand rendering.");
    Check(surface.Render(scene,now+=1,false,{0,0,0,1}),"on-demand mode renders first frame");sg_commit();
    Check(!surface.Render(scene,now+=1,false,{0,0,0,1}),"static on-demand frame reuses previous texture");
    lv::Transform shift;shift.Position.X=.2f;scene.SetTransform(object,shift);Check(surface.Render(scene,now+=1,false,{0,0,0,1}),"Object edit wakes on-demand rendering");sg_commit();
    scene.MainCamera.Position.X=.1f;Check(surface.Render(scene,now+=1,false,{0,0,0,1}),"direct advanced Camera edit wakes on-demand rendering");sg_commit();
    surface.Invalidate();Check(surface.Render(scene,now+=1,false,{0,0,0,1}),"explicit redraw invalidates cached image");sg_commit();
    lv::Scene skinned;skinned.MainCamera.Position={0,1,5};skinned.MainCamera.Target={0,1,0};auto rig=assets.LoadModel(Fixture("AnimatedRibbon.gltf"));const auto skin=skinned.AddModel(rig);
    auto hip=rig->Skeleton.Joints[1].ReferenceLocal;hip.Position.X=10;auto tip=rig->Skeleton.Joints[2].ReferenceLocal;tip.Position.X=-20;
    skinned.GetAnimation(skin).SetJointOverride(1,hip);skinned.GetAnimation(skin).SetJointOverride(2,tip);changed.Overrides.FrustumCulling=true;
    Require(bool(surface.ApplyQuality(changed,skinned,Size,Size)),"Cannot configure skinned culling.");surface.Render(skinned,now+=1,false);sg_commit();
    Check(surface.Stats().LastDrawCalls == 1 && surface.Stats().LastCulledParts == 0,"conservative skin bound keeps blends between two offscreen joints");
    skinned.GetAnimation(skin).Play(0);Check(surface.Render(skinned,now+=1,false),"active animation keeps Low/on-demand skinning available");sg_commit();
    auto large=std::make_shared<lv::ModelAsset>(*assets.LoadModel(Fixture("SharedBox.glb")));large->Images[0].Width=large->Images[0].Height=1024;large->Images[0].Rgba.assign(1024*1024*4,255);lv::Scene textured;textured.AddModel(large);
    auto low=lv::MakeQualityProfile(lv::QualityPreset::Low);low.Overrides.FixedWidth=Size;low.Overrides.FixedHeight=Size;low.Overrides.Resolution=lv::ResolutionMode::Fixed;low.Overrides.TargetFps=0;
    Require(bool(surface.ApplyQuality(low,textured,Size,Size)),"Cannot apply Low texture quality.");surface.Render(textured,now+=1,true);sg_commit();
    Check(surface.Stats().MaxResidentTextureSize == 512 && large->Images[0].Width == 1024,"Low downsizes GPU texture while preserving immutable CPU asset");
    auto high=lv::MakeQualityProfile(lv::QualityPreset::High);high.AllowFallback=true;high.Overrides.MsaaSamples=1;high.Overrides.TargetFps=0;
    Require(bool(surface.ApplyQuality(high,textured,Size,Size)),"Cannot apply High sampling.");surface.Render(textured,now+=1,true);sg_commit();
    Check(surface.Stats().MaxResidentTextureSize == 1024 && surface.Quality().Effective->Anisotropy > 1,"High restores texture resolution and creates anisotropic sampler");
    lv::ImageAsset checker;checker.Width=checker.Height=2;checker.Rgba={0,0,0,255,255,255,255,255,0,0,0,255,255,255,255,255};const auto packed=lv::detail::PackTexture(checker,1);
    Check(packed.Width == 1 && packed.Height == 1 && packed.Levels == 1 && packed.Pixels[0] >= 187 && packed.Pixels[0] <= 188,"texture reduction averages in linear space before sRGB encoding");
    Require(bool(surface.Resize(textured,0,0)),"Cannot suspend quality surface.");Check(!surface.Texture().id && !surface.Quality().Applied,"zero-sized viewport releases targets");
    auto suspended=surface.Telemetry(lv::detail::RenderClockSeconds());lv::QualityEstimate current;current.Quality=suspended.Quality;current.Cost=suspended.Resident;current.AssetSignature=suspended.ResidentAssetSignature;
    auto resume=lv::EstimateQualityResources(high,suspended.Quality.Capabilities,textured,Size,Size,&current);
    Check(resume && resume.Value().AdditionalBytes == resume.Value().Cost.RenderTargetBytes,"real suspended HDR/shadow telemetry cannot reuse released attachments");
    Require(bool(surface.Resize(textured,Size,Size)),"Cannot resume quality surface.");
    Check(surface.Texture().id && surface.Quality().Applied,"viewport resumes with latest requested quality");
}

std::shared_ptr<lv::ModelAsset> EnhancementPlane(float half=1) {
    auto model=std::make_shared<lv::ModelAsset>();lv::MeshPrimitive primitive;
    primitive.Geometry.Vertices={{{-half,-half,0},{0,0,1},{1,1,1,1},{0,0}},{{half,-half,0},{0,0,1},{1,1,1,1},{1,0}},{{half,half,0},{0,0,1},{1,1,1,1},{1,1}},{{-half,half,0},{0,0,1},{1,1,1,1},{0,1}}};primitive.Geometry.Indices={0,1,2,0,2,3};model->Meshes.push_back({"Plane",{primitive}});model->Materials.push_back({});model->Materials[0].DoubleSided=true;model->Parts.push_back({0});model->ModelBounds={{-half,-half,0},{half,half,0}};return model;
}
void RunEnhancements() {
    lv::Scene scene;scene.MainCamera.Position={0,0,2};scene.MainCamera.Target={0,0,0};scene.MainCamera.VerticalFovRadians=std::numbers::pi_v<float>/2;scene.Sun.Direction={0,0,-1};scene.Sun.Intensity=1;scene.Sun.Ambient=0;
    auto model=EnhancementPlane();model->Images={{1,1,{128,128,255,255}}};model->Textures.push_back({});model->Materials[0].NormalTexture=0;for (auto& vertex:model->Meshes[0].Primitives[0].Geometry.Vertices) {vertex.Tangent={1,0,0,1};}
    const auto object=scene.AddModel(model);lv::detail::RenderSurface surface;auto p=lv::MakeQualityProfile(lv::QualityPreset::Low);p.Overrides.TargetFps=0;p.Overrides.Lighting=lv::LightingMode::Advanced;p.Overrides.OnDemand=lv::OnDemandMode::On;
    double time=100;const auto apply=[&] {const auto r=surface.ApplyQuality(p,scene,Size,Size);Require(bool(r),r ? "":r.Error().Message.c_str());};
    const auto render=[&](bool force=true) {surface.Render(scene,time+=1,force,{0,0,0,1});sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    apply();auto flat=render();auto tilted=std::make_shared<lv::ModelAsset>(*model);tilted->Images[0].Rgba={255,128,128,255};scene.SetModel(object,tilted);auto normal=render();
    Check(flat[64*Size+64][0] > 240 && normal[64*Size+64][0] < 20,"linear glTF normal texture and authored tangent change actual lighting");
    auto derived=std::make_shared<lv::ModelAsset>(*tilted);for (auto& vertex:derived->Meshes[0].Primitives[0].Geometry.Vertices) {vertex.Tangent[3]=0;}scene.SetModel(object,derived);auto cotangent=render();Check(cotangent[64*Size+64][0] < 20,"UV derivatives provide normal mapping when glTF tangents are absent");
    p.Overrides.Lighting=lv::LightingMode::Basic;apply();auto basic=render();Check(basic[64*Size+64][0] > 240 && surface.Stats().ResidentImages == 0,"Basic quality bypasses normal-map upload and shading");
    p.Overrides.Lighting=lv::LightingMode::Advanced;scene.SetModel(object,EnhancementPlane());scene.Sun.Intensity=0;scene.PointLights={{{0,0,2},{1,0,0},8,10}};apply();auto point=render();
    Check(point[64*Size+64][0] > 180 && point[64*Size+64][1] < 5,"Advanced point light contributes range-attenuated colored illumination");
    scene.PointLights.clear();scene.SpotLights={{{0,0,2},{0,0,-1},{0,1,0},8,10,.2f,.4f}};auto spot=render(false);scene.SpotLights[0].Direction={1,0,0};auto away=render(false);
    Check(spot[64*Size+64][1] > 180 && away[64*Size+64][1] < 5,"spot cone affects pixels and direct light edits wake On-Demand cache");
    scene.SpotLights.clear();scene.Sun.Intensity=1;scene.Sun.Direction={.6f,0,-1};auto caster=EnhancementPlane(.25f);lv::Transform placed;placed.Position={-.4f,0,.7f};const auto casterId=scene.AddModel(caster,placed);
    auto unshadowed=render();p.Overrides.Shadow=lv::ShadowMode::Basic;p.Overrides.ShadowResolution=512;apply();auto hard=render();p.Overrides.Shadow=lv::ShadowMode::Pcf;apply();auto soft=render();
    std::size_t occluded=0,filtered=0;for (int y=38;y<90;++y) {for (int x=60;x<95;++x) {const auto i=y*Size+x;if (unshadowed[i][0] > 180 && hard[i][0] < 50) {++occluded;}if (hard[i][0] != soft[i][0] && soft[i][0] > 10 && soft[i][0] < 210) {++filtered;}}}
    Check(occluded > 5 && surface.Stats().LastShadowDrawCalls == 2,"directional shadow pass darkens receiver pixels behind an occluder");
    Check(filtered > 0,"3x3 PCF produces intermediate edge visibility compared with Basic shadow");
    auto masked=std::make_shared<lv::ModelAsset>(*caster);masked->Materials[0].AlphaCutoff=.5f;masked->Materials[0].BaseColor[3]=0;scene.SetModel(casterId,masked);auto transparent=render();std::size_t lit=0;for (std::size_t i=0;i<hard.size();++i) {if (hard[i][0] < 50 && transparent[i][0] > 180) {++lit;}}
    Check(lit > 10,"alpha-masked geometry is discarded in shadow and color passes");
    scene.RemoveObject(casterId);scene.Sun.Intensity=0;scene.Sun.Ambient=3;p.Overrides.Shadow=lv::ShadowMode::Off;p.Overrides.PostProcessing=false;apply();auto clipped=render();p.Overrides.PostProcessing=true;apply();auto hdr=render();
    Check(clipped[64*Size+64][0] == 255 && hdr[64*Size+64][0] > 175 && hdr[64*Size+64][0] < 210,"RGBA16F scene and Reinhard post processing preserve and tone-map HDR light energy");
    auto shared=std::make_shared<lv::ModelAsset>(*model);shared->Materials[0].BaseColorTexture=0;scene.SetModel(object,shared);p.Overrides.Shadow=lv::ShadowMode::Pcf;apply();(void)render();
    auto estimate=lv::EstimateQualityResources(p,surface.Quality().Capabilities,scene,Size,Size);auto measured=surface.Telemetry(lv::detail::RenderClockSeconds());Check(estimate && estimate.Value().Cost.TotalBytes == measured.Resident.TotalBytes && surface.Stats().ResidentImages == 2 && measured.Resident.TextureBytes == 12,"combined shadow/HDR and shared sRGB/linear image accounting agrees with renderer allocations");
}
void RunDebug() {
    lv::AssetRegistry assets;auto model=std::make_shared<lv::ModelAsset>(*assets.LoadModel(Fixture("Sparse.gltf")));
    auto& geometry=model->Meshes[0].Primitives[0].Geometry;geometry.Vertices.resize(3);geometry.Indices={0,1,2};const std::array<std::array<float,3>,3> positions={{{-.8f,-.8f,0},{.8f,-.8f,0},{0,.8f,0}}};
    for (int i=0;i<3;++i) {geometry.Vertices[i].Position=positions[i];geometry.Vertices[i].Normal={1,0,0};geometry.Vertices[i].Color={1,1,1,1};geometry.Vertices[i].TexCoord={i == 0 ? 0.0f:i == 1 ? 1.0f:.5f,i == 2 ? 1.0f:0.0f};}
    model->Materials={lv::MaterialAsset{}};model->Materials[0].BaseColor={.25f,.5f,.75f,1};model->Meshes[0].Primitives[0].MaterialIndex=0;
    lv::Scene scene;const auto object=scene.AddModel(model);scene.MainCamera.Position={0,0,2};scene.MainCamera.Target={};scene.MainCamera.VerticalFovRadians=std::numbers::pi_v<float>/2;scene.MainCamera.FarPlane=10;scene.Sun.Intensity=scene.Sun.Ambient=0;
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);profile.Overrides.FixedWidth=profile.Overrides.FixedHeight=Size;profile.Overrides.Resolution=lv::ResolutionMode::Fixed;profile.Overrides.TargetFps=0;profile.Overrides.OnDemand=lv::OnDemandMode::On;profile.Overrides.FrustumCulling=false;
    lv::detail::RenderSurface surface;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare diagnostic test surface.");double clock=1;
    const auto render=[&] {Require(surface.Render(scene,clock++,false,{0,0,0,1}),"Debug change failed to invalidate On-Demand rendering.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    const auto set=[&](lv::DebugView view) {lv::DebugOptions o;o.View=view;Require(bool(surface.SetDebugOptions(o)),"Cannot select diagnostic view.");};
    const auto lit=render();Check(lit[64*Size+64][0] == 0,"Lit respects the original dark lighting");
    set(lv::DebugView::Unlit);auto unlit=render();const auto center=unlit[64*Size+64];Check(center[0]>55 && center[0]<75 && center[1]>115 && center[1]<140 && center[2]>180,"Unlit displays material color without editing the material");
    set(lv::DebugView::Normal);auto normals=render();auto normal=normals[64*Size+64];Check(normal[0]>245 && normal[1]>120 && normal[1]<135 && normal[2]>120 && normal[2]<135,"Normal view maps world geometric normals to RGB");
    set(lv::DebugView::UV);auto uv=render();const auto coord=uv[64*Size+64];Check(coord[0]>100 && coord[0]<155 && coord[1]>90 && coord[1]<160 && coord[2]==0,"UV view visualizes interpolated UV0 independently of lighting");
    set(lv::DebugView::Depth);auto depth=render();Check(depth[64*Size+64][0]>45 && depth[64*Size+64][0]<60 && depth[64*Size+64][0] == depth[64*Size+64][1],"Depth view reconstructs linear view depth divided by far plane");
    set(lv::DebugView::Wireframe);auto wire=render();std::size_t edgePixels=0;for (const auto& p:wire) {edgePixels+=p[0]>240;}Check(edgePixels>50 && wire[64*Size+64][0]==0 && surface.Stats().LastTriangles==0,"Wireframe draws triangle edges rather than filled interiors");
    const auto inspection=surface.Inspect();bool buffers=false,targets=false,valid=true;for (const auto& r:inspection.Resources) {buffers|=r.Name == "Wireframe edge indices" && r.Bytes>0;targets|=r.Kind == lv::DebugResourceKind::RenderTarget && r.Width == Size;valid&=r.State == lv::DebugResourceState::Valid;}Check(buffers && targets && valid && inspection.DebugBufferBytes>0 && !inspection.Passes.empty(),"inspection snapshots expose valid targets, extra debug bytes, edge buffers, shaders and pipelines");
    const auto image=surface.ColorImage().id;Check(!surface.SetDebugOptions({static_cast<lv::DebugView>(99)}) && !surface.SetDebugOptions({lv::DebugView::ShadowMap}) && surface.ColorImage().id == image && surface.Debug().View == lv::DebugView::Wireframe,"invalid and unavailable views retain prior rendering settings/resources");
    set(lv::DebugView::BoneWeight);const auto rigid=render();Check(rigid[64*Size+64][2]>245 && rigid[64*Size+64][0]==0,"rigid vertices show zero influence instead of fabricated skin weights");
    set(lv::DebugView::BoundingBox);(void)render();Check(surface.Inspect().LineCount == 12 && surface.Inspect().LineDrawCalls == 1,"bounding box overlay is submitted independently of mesh triangles");
    set(lv::DebugView::Unlit);lv::DebugDraw commands;commands.DrawLine({-.5f,0,-1},{.5f,0,-1},{1,0,0,1});Require(bool(surface.SetDebugDraw(commands)),"Cannot submit public debug geometry.");const auto hidden=render();auto options=surface.Debug();options.DepthTest=false;Require(bool(surface.SetDebugOptions(options)),"Cannot select x-ray overlay.");const auto shown=render();std::size_t hiddenRed=0,shownRed=0;for (std::size_t i=0;i<shown.size();++i) {hiddenRed+=hidden[i][0]>240 && hidden[i][1]==0;shownRed+=shown[i][0]>240 && shown[i][1]==0;}Check(shownRed>hiddenRed+15,"debug line depth-test option controls occlusion behind a mesh");
    Require(bool(surface.SetDebugDraw({})),"Cannot clear diagnostic commands.");set(lv::DebugView::Normal);(void)render();profile.Base=lv::QualityPreset::High;profile.Overrides.MsaaSamples=1;profile.Overrides.ShadowResolution=512;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot enable shadow/HDR debug target.");
    const auto history=surface.Telemetry(lv::detail::RenderClockSeconds());Check(history.PreviousQualityFrames && history.PreviousQualityFrames->DebugRendering,"quality history preserves the diagnostic measurement flag");
    set(lv::DebugView::Normal);auto hdr=render();Check(hdr[64*Size+64] == normal,"HDR diagnostic output bypasses FXAA and tone mapping");
    set(lv::DebugView::ShadowMap);auto shadow=render();std::size_t written=0;bool gray=true;for (const auto& p:shadow) {written+=p[0]<250;gray&=p[0]==p[1] && p[1]==p[2];}Check(gray && written>20 && surface.InspectionTexture(2).id,"Shadow Map view visualizes actual directional depth across the target");
    auto noShadow=profile;noShadow.Overrides.Shadow=lv::ShadowMode::Off;Check(!surface.ApplyQuality(noShadow,scene,Size,Size) && surface.Quality().Requested == profile,"disabling shadows during Shadow Map inspection is explicitly rejected");
    set(lv::DebugView::Lit);Require(bool(surface.ApplyQuality(noShadow,scene,Size,Size)),"Cannot restore no-shadow profile.");
    scene.SetModel(object,assets.LoadModel(Fixture("AnimatedRibbon.gltf")));scene.GetAnimation(object).Play(0);scene.GetAnimation(object).Seek(.5);scene.MainCamera.Position={.2f,.5f,3};scene.MainCamera.Target={.2f,.5f,0};
    options={};options.View=lv::DebugView::Skeleton;options.DepthTest=false;options.ShowReferencePose=true;Require(bool(surface.SetDebugOptions(options)),"Cannot select skeleton/reference overlay.");(void)render();const auto bones=surface.Inspect();Check(bones.LineCount>6 && scene.GetAnimation(object).Time() == .5 && surface.Telemetry(lv::detail::RenderClockSeconds()).Frames.DebugRendering,"animated/reference skeleton drawing preserves playback and marks diagnostic timing");
    set(lv::DebugView::BoneWeight);auto weights=render();std::size_t weighted=0;for (const auto& p:weights) {weighted+=p[0]>20 && (p[1]>20 || p[0]>240);}Check(weighted>20,"Bone Weight heatmap uses actual skinned vertex influences");
    set(lv::DebugView::Lit);const auto normalFrames=render();(void)normalFrames;Check(!surface.Telemetry(lv::detail::RenderClockSeconds()).Frames.DebugRendering,"returning to Lit starts normal performance evidence again");
}
void RunOptimization() {
    lv::AssetRegistry assets;auto model=std::make_shared<lv::ModelAsset>(*assets.LoadModel(Fixture("Sparse.gltf")));auto& primitive=model->Meshes[0].Primitives[0];primitive.Geometry=OptimizationGrid();primitive.Lods=lv::GenerateMeshLods(primitive.Geometry);model->ModelBounds={{-1,-1,0},{1,1,0}};model->Materials={lv::MaterialAsset{}};primitive.MaterialIndex=0;
    lv::Scene scene;scene.MainCamera.Position={0,0,2};scene.MainCamera.Target={};scene.MainCamera.VerticalFovRadians=std::numbers::pi_v<float>/2;scene.MainCamera.FarPlane=100;scene.Sun.Intensity=0;scene.Sun.Ambient=1;
    for (int i=0;i<1000;++i) {lv::Transform t;t.Position={((i%40)-19.5f)*.045f,((i/40)-12)*.045f,0};t.Scale={.02f,.02f,.02f};scene.AddModel(model,t);}
    lv::detail::RenderSurface surface;auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);profile.Overrides.Lod=false;profile.Overrides.Instancing=false;profile.Overrides.Compression=lv::TextureCompression::None;profile.Overrides.FixedWidth=profile.Overrides.FixedHeight=Size;profile.Overrides.Resolution=lv::ResolutionMode::Fixed;profile.Overrides.TargetFps=0;
    const auto apply=[&](const lv::Scene& s) {const auto applied=surface.ApplyQuality(profile,s,Size,Size);Require(bool(applied),applied ? "":applied.Error().Message.c_str());};double clock=1;
    const auto render=[&](const lv::Scene& s) {Require(surface.Render(s,clock++,true,{0,0,0,1}),"Cannot render optimization fixture.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    apply(scene);const auto baseline=render(scene);const auto before=surface.Stats();profile.Overrides.Instancing=true;apply(scene);const auto instanced=render(scene);const auto after=surface.Stats();std::size_t difference=0;for (std::size_t i=0;i<baseline.size();++i) {difference+=baseline[i]!=instanced[i];}
    Check(before.LastDrawCalls==1000 && after.LastDrawCalls==1 && after.LastTriangles==before.LastTriangles && after.Optimization.SavedDrawCalls==999 && difference<Size*Size/100,"1000 static instances reduce main draws to one while preserving rasterized geometry");
    std::vector<lv::ObjectId> outside;for (int i=0;i<100;++i) {lv::Transform t;t.Position={1000.0f+i,0,0};outside.push_back(scene.AddModel(model,t));}(void)render(scene);Check(surface.Stats().LastDrawCalls==1 && surface.Stats().LastCulledParts==100,"frustum culling excludes offscreen instances before batching");for (auto id:outside) {scene.RemoveObject(id);}
    profile.Overrides.Shadow=lv::ShadowMode::Basic;profile.Overrides.ShadowResolution=512;apply(scene);(void)render(scene);Check(surface.Stats().LastDrawCalls==1 && surface.Stats().LastShadowDrawCalls==1 && surface.Stats().Optimization.SavedDrawCalls==1998,"static instancing also reduces directional shadow submissions");
    lv::Transform mirror;mirror.Scale={-.02f,.02f,.02f};scene.AddModel(model,mirror);(void)render(scene);Check(surface.Stats().LastDrawCalls==2 && surface.Stats().LastShadowDrawCalls==2,"mirrored static transforms retain separate winding groups");
    lv::Scene lodScene;lodScene.MainCamera=scene.MainCamera;lodScene.MainCamera.Position.Z=30;lodScene.Sun=scene.Sun;lodScene.AddModel(model);profile.Overrides.Shadow=lv::ShadowMode::Off;profile.Overrides.Instancing=false;profile.Overrides.Lod=true;apply(lodScene);(void)render(lodScene);const auto lod=surface.Stats();Check(lod.Optimization.LodDraws[3]==1 && lod.LastTriangles<primitive.Geometry.Indices.size()/3/2,"distant grid selects actual reduced GPU LOD indices");
    const auto lodEstimate=lv::EstimateQualityResources(profile,surface.Quality().Capabilities,lodScene,Size,Size);Check(lodEstimate && lodEstimate.Value().Cost.BufferBytes==surface.Stats().LogicalBufferBytes,"LOD index allocations are included in public resource costs");
    lodScene.MainCamera.Position.Z=2;profile.Overrides.LodBias=-4;apply(lodScene);(void)render(lodScene);Check(surface.Stats().LastTriangles==primitive.Geometry.Indices.size()/3,"negative bias restores full detail without removing the generated LOD cache");
    auto textured=std::make_shared<lv::ModelAsset>(*model);lv::ImageAsset image;image.Width=image.Height=256;image.Rgba.resize(256*256*4);for (std::size_t i=0;i<image.Rgba.size();i+=4) {image.Rgba[i]=128;image.Rgba[i+1]=64;image.Rgba[i+2]=192;image.Rgba[i+3]=255;}textured->Images={image};textured->Textures={lv::TextureAsset{}};textured->Materials[0].BaseColorTexture=0;lodScene.SetModel(lodScene.ObjectIds()[0],textured);profile.Overrides.Lod=false;apply(lodScene);const auto raw=render(lodScene);const auto rawBytes=surface.Stats().LogicalTextureBytes;
    if (surface.Quality().Capabilities.Bc3Textures) {profile.Overrides.Compression=lv::TextureCompression::Bc3;apply(lodScene);const auto compressed=render(lodScene);const auto compressedBytes=surface.Stats().LogicalTextureBytes;unsigned maxError=0;for (std::size_t i=0;i<raw.size();++i) {for (int c=0;c<3;++c) {maxError=std::max(maxError,static_cast<unsigned>(std::abs(int(raw[i][c])-int(compressed[i][c]))));}}
        const auto estimated=lv::EstimateQualityResources(profile,surface.Quality().Capabilities,lodScene,Size,Size);Check(compressedBytes<rawBytes/3 && maxError<=8 && estimated && estimated.Value().Cost.TextureBytes==compressedBytes && textured->Images[0].Rgba==image.Rgba,"BC3 keeps source pixels intact, reduces actual texture bytes and matches estimated cost");
        std::printf("Optimization observed: main draws %zu -> %zu; distant triangles %zu -> %llu; texture bytes %llu -> %llu. Timing is not a controlled benchmark.\n",before.LastDrawCalls,after.LastDrawCalls,primitive.Geometry.Indices.size()/3,static_cast<unsigned long long>(lod.LastTriangles),static_cast<unsigned long long>(rawBytes),static_cast<unsigned long long>(compressedBytes));
    }
}
void RunCamera() {
    auto model=std::make_shared<lv::ModelAsset>();model->Meshes.push_back({"Cube",{{lv::MakeCube(),0,false}}});model->Materials.push_back({});model->Materials[0].Unlit=true;model->Parts.push_back({0});model->ModelBounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};
    lv::Scene scene;const auto object=scene.AddModel(model);auto& camera=scene.MainCamera;camera.Position={0,0,4};camera.Target={};camera.Projection=lv::CameraProjection::Orthographic;camera.OrthographicHeight=4;camera.FarPlane=20;
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);profile.Overrides.TargetFps=0;profile.Overrides.OnDemand=lv::OnDemandMode::On;profile.Overrides.Lod=false;
    lv::detail::RenderSurface surface;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare camera test.");double clock=1;
    const auto render=[&] {Require(surface.Render(scene,clock++,false,{0,0,0,1}),"Camera change failed to wake On-Demand.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    const auto count=[](const auto& pixels) {return std::count_if(pixels.begin(),pixels.end(),[](const auto& p){return p[0]+p[1]+p[2]>20;});};
    const auto nearPixels=render();camera.Position.Z=8;const auto farPixels=render();Check(nearPixels == farPixels && count(nearPixels)>500,"orthographic pixels retain apparent size across camera distance");
    camera.Projection=lv::CameraProjection::Perspective;const auto perspective=render();Check(count(perspective)<count(nearPixels)/2,"switching projection invalidates the cache and changes real geometry size");
    camera.Projection=lv::CameraProjection::Orthographic;camera.OrthographicHeight=8;const auto zoom=render();Check(count(zoom)<count(nearPixels)/2,"orthographic span changes invalidate cached output");
    Require(bool(surface.SetDebugOptions({lv::DebugView::Depth})),"Cannot select orthographic depth view.");const auto depth=render();Check(std::abs(int(depth[64*Size+64][0])-96)<=2 && depth[64*Size+64][0] == depth[64*Size+64][1],"orthographic Depth View reconstructs linear view depth");
    auto grid=std::make_shared<lv::ModelAsset>(*model);auto& primitive=grid->Meshes[0].Primitives[0];primitive.Geometry=OptimizationGrid();primitive.Lods=lv::GenerateMeshLods(primitive.Geometry);grid->ModelBounds={{-1,-1,0},{1,1,0}};scene.SetModel(object,grid);profile.Overrides.Lod=true;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare orthographic LOD.");
    (void)render();const auto triangles=surface.Stats().LastTriangles;camera.Position.Z=12;(void)render();Check(triangles<primitive.Geometry.Indices.size()/3 && triangles == surface.Stats().LastTriangles,"orthographic LOD depends on projected span rather than perspective distance");
}
void RunBlend() {
    lv::AssetRegistry registry;auto model=std::make_shared<lv::ModelAsset>(*registry.LoadModel(Fixture("AnimatedRibbon.gltf")));
    auto clip=model->Animations[0];clip.Name="Constant bent pose";clip.Duration=0;for (auto& track:clip.Tracks) {track.Times={0};track.Values={track.Values[1]};}model->Animations.push_back(clip);
    lv::Scene scene;scene.MainCamera.Position={0,1,5};scene.MainCamera.Target={0,1,0};scene.Sun.Intensity=0;scene.Sun.Ambient=1;const auto id=scene.AddModel(model);
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);profile.Overrides.TargetFps=0;profile.Overrides.OnDemand=lv::OnDemandMode::On;
    lv::detail::RenderSurface surface;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare blend rendering.");double clock=1;
    const auto render=[&] {Require(surface.Render(scene,clock++,false,{0,0,0,1}),"Blend progress failed to wake On-Demand.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    const auto reference=render();auto& player=scene.GetAnimation(id);player.CrossFade(model->Animations.size()-1,1);(void)render();scene.UpdateAnimations(.5);const auto blended=render();
    Check(player.Time() == 0 && player.Blend() && reference != blended,"fade progress changes real GPU skinning pixels even with stationary target playhead");
    player.Pause();scene.UpdateAnimations(.25);Check(!surface.Render(scene,clock++,false,{0,0,0,1}),"paused blend reuses cached output");
    lv::Scene expected;expected.MainCamera=scene.MainCamera;expected.Sun=scene.Sun;const auto expectedId=expected.AddModel(model);auto pose=lv::ReferencePose(model->Skeleton);pose.Joints[1].Local.Position.X=.25f;pose.Joints[2].Local.Rotation={0,0,std::sin(std::numbers::pi_v<float>/8),std::cos(std::numbers::pi_v<float>/8)};expected.GetAnimation(expectedId).ApplyPose(pose);
    Require(surface.Render(expected,clock++,false,{0,0,0,1}),"Cannot render expected midpoint.");sg_commit();const auto midpoint=ReadImage(surface.ColorImage(),Size,Size);std::size_t changed=0;for (std::size_t i=0;i<midpoint.size();++i) {changed+=midpoint[i] != blended[i];}
    Check(changed<10,"GPU blend midpoint matches independently authored half-translation and half-rotation pose");
    player.Resume();scene.UpdateAnimations(.5);const auto finished=render();Check(!player.Blend() && finished != blended,"completed fade releases source and produces final GPU pose");
}
void RunPbr() {
    auto model=std::make_shared<lv::ModelAsset>();lv::MeshPrimitive primitive;primitive.Geometry.Vertices={{{-2,-2,0},{0,0,1},{1,1,1,1},{0,0}},{{2,-2,0},{0,0,1},{1,1,1,1},{1,0}},{{0,2,0},{0,0,1},{1,1,1,1},{.5f,1}}};primitive.Geometry.Indices={0,1,2};model->Meshes.push_back({"PBR triangle",{primitive}});model->Parts.push_back({0});model->ModelBounds={{-2,-2,0},{2,2,0}};model->Materials.push_back({});auto& material=model->Materials[0];material.Shading=lv::ShadingModel::Pbr;material.BaseColor={.8f,.4f,.2f,1};material.MetallicFactor=0;material.RoughnessFactor=.6f;
    lv::Scene scene;const auto id=scene.AddModel(model);scene.MainCamera.Position={0,0,4};scene.MainCamera.Target={};scene.MainCamera.Projection=lv::CameraProjection::Orthographic;scene.MainCamera.OrthographicHeight=4;scene.Sun.Direction={-.6f,0,-.8f};scene.Sun.Intensity=2;scene.Sun.Ambient=0;
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Medium);profile.Overrides.Pbr=true;profile.Overrides.Shadow=lv::ShadowMode::Off;profile.Overrides.Lod=false;profile.Overrides.Compression=lv::TextureCompression::None;profile.Overrides.TargetFps=0;profile.Overrides.OnDemand=lv::OnDemandMode::On;
    lv::detail::RenderSurface surface;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare PBR surface.");double clock=1;
    const auto render=[&] {Require(surface.Render(scene,clock++,false,{0,0,0,1}),"PBR change did not wake On-Demand.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    // Independent fixed-angle CPU GGX/Smith/Schlick reference, NdotL=.8, NdotV=1.
    const auto expected=[](double base,double metallic,double roughness) {const double nl=.8,nh=std::sqrt(.9),alpha2=std::pow(std::max(.045,roughness),4);const double f0=.04*(1-metallic)+base*metallic,f=f0+(1-f0)*std::pow(1-nh,5);const double d=alpha2/(std::numbers::pi*std::pow(.9*(alpha2-1)+1,2));const double g=2*nl/(nl+std::sqrt(alpha2+(1-alpha2)*nl*nl));return static_cast<int>(std::lround(std::clamp(((1-f)*(1-metallic)*base/std::numbers::pi+f*d*g/(4*nl))*nl*2,0.0,1.0)*255));};
    auto dielectric=render();const auto center=64*Size+64;Check(std::abs(int(dielectric[center][0])-expected(.8,0,.6))<=3,"PBR dielectric direct light matches independent CPU BRDF");
    scene.Sun.Intensity=0;scene.PointLights={{{0,0,2},{1,1,1},10,8}};const auto point=render();Check(point[center][0]>10,"PBR Point Light contributes actual local specular/diffuse pixels");scene.PointLights.clear();scene.SpotLights={{{0,0,2},{0,0,-1},{1,1,1},10,8,.2f,.5f}};const auto spot=render();scene.SpotLights[0].Direction={0,0,1};const auto outside=render();Check(spot[center][0]>10 && outside[center][0] == 0,"PBR Spot Light respects cone direction");scene.SpotLights.clear();scene.Sun.Intensity=2;
    auto override=material;override.MetallicFactor=1;override.RoughnessFactor=.2f;scene.SetMaterial(id,0,override);auto metal=render();Check(std::abs(int(metal[center][0])-expected(.8,1,.2))<=3 && metal!=dielectric,"metallic/roughness overrides change actual GGX pixels");
    Check(!surface.Render(scene,clock++,false,{0,0,0,1}),"unchanged PBR Scene reuses cached pixels");
    auto textured=std::make_shared<lv::ModelAsset>(*model);textured->Images={{1,1,{128,128,128,255}},{1,1,{32,128,64,255}},{1,1,{128,255,128,255}}};textured->Textures.resize(3);for (std::size_t i=0;i<3;++i) {textured->Textures[i].ImageIndex=i;}auto& m=textured->Materials[0];m.BaseColor={1,1,1,1};m.BaseColorTexture=0;m.MetallicRoughnessTexture=1;m.MetallicFactor=.8f;m.RoughnessFactor=.7f;scene.SetModel(id,textured);const auto mapped=render();const double linear=std::pow((128.0/255+.055)/1.055,2.4);
    Check(std::abs(int(mapped[center][0])-expected(linear,.8*64/255,.7*128/255))<=3,"base texture is sRGB while MR G/B channels remain linear");
    auto changed=std::make_shared<lv::ModelAsset>(*textured);changed->Images[1].Rgba[0]=255;scene.SetModel(id,changed);Check(render() == mapped,"MR red channel does not affect shading");
    auto shared=changed->Materials[0];shared.NormalTexture=1;scene.SetMaterial(id,0,shared);(void)render();const auto estimate=lv::EstimateQualityResources(profile,surface.Quality().Capabilities,scene,Size,Size);
    Check(estimate && surface.Stats().ResidentImages == 2 && estimate.Value().Cost.TextureBytes == surface.Stats().LogicalTextureBytes,"MR and Normal reuse one linear allocation and match resource costs");
    shared.NormalTexture=2;scene.SetMaterial(id,0,shared);const auto normal=render();Check(normal[center][0]<mapped[center][0]/2,"normal map modifies PBR direct lighting");
    scene.ResetMaterial(id,0);Check(render() == mapped && surface.Stats().ResidentImages == 2,"ResetMaterial restores pixels and releases unused override texture");
    scene.MainCamera.Position.Z=-4;scene.Sun.Direction={-.6f,0,.8f};const auto back=render();shared=changed->Materials[0];shared.DoubleSided=true;scene.SetMaterial(id,0,shared);const auto twoSided=render();Check(back[center][0] == 0 && twoSided[center][0]>5,"DoubleSided override changes actual backface rasterization");
    scene.MainCamera.Position.Z=4;scene.Sun.Direction={-.6f,0,-.8f};scene.ResetMaterial(id,0);lv::Transform offset;offset.Position.X=.2f;const auto other=scene.AddModel(changed,offset);(void)render();Check(surface.Stats().LastDrawCalls == 1 && surface.Stats().Optimization.InstancedObjects == 2,"PBR materials support static GPU instancing");shared=changed->Materials[0];shared.MetallicFactor=1;scene.SetMaterial(other,0,shared);(void)render();Check(surface.Stats().LastDrawCalls == 2,"different instance materials cannot merge into one GPU batch");scene.RemoveObject(other);
    lv::AssetRegistry assets;scene.SetModel(id,assets.LoadModel(Fixture("AnimatedRibbon.gltf")));scene.MainCamera.Position={0,1,5};scene.MainCamera.Target={0,1,0};scene.MainCamera.Projection=lv::CameraProjection::Perspective;const auto skin=render();scene.GetAnimation(id).Play(0);scene.GetAnimation(id).Seek(1);Check(render()!=skin && surface.Stats().LastDrawCalls == 1,"PBR and GPU skinning produce animated pixels");
}
void RunMorph() {
    lv::AssetRegistry assets;auto model=std::make_shared<lv::ModelAsset>(*assets.LoadModel(Fixture("MorphRibbon.gltf")));model->Parts.resize(1);
    model->Images={{1,1,{200,230,255,255}}};model->Textures.resize(1);model->Materials[0].Unlit=false;model->Materials[0].Shading=lv::ShadingModel::Basic;model->Materials[0].NormalTexture=0;
    lv::Scene scene;const auto id=scene.AddModel(model);scene.MainCamera.Position={0,1,5};scene.MainCamera.Target={0,1,0};scene.MainCamera.Projection=lv::CameraProjection::Orthographic;scene.MainCamera.OrthographicHeight=3.5f;scene.Sun.Intensity=1;scene.Sun.Ambient=.1f;auto& player=scene.GetAnimation(id);const std::array<float,2> weights{.8f,.2f};player.SetMorphWeights(3,weights);player.Play(0);player.Seek(.5);
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Medium);profile.Overrides.Shadow=lv::ShadowMode::Off;profile.Overrides.TargetFps=0;profile.Overrides.OnDemand=lv::OnDemandMode::On;profile.Overrides.Compression=lv::TextureCompression::None;
    lv::detail::RenderSurface surface;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot prepare morph rendering.");double clock=1;
    const auto render=[&](const lv::Scene& world) {Require(surface.Render(world,clock++,false,{0,0,0,1}),"Morph state failed to wake rendering.");sg_commit();return ReadImage(surface.ColorImage(),Size,Size);};
    auto reference=std::make_shared<lv::ModelAsset>(*model);reference->Meshes[0].Primitives[0].Geometry=lv::EvaluateMorph(model->Meshes[0].Primitives[0],weights);reference->Meshes[0].Primitives[0].MorphTargets.clear();reference->Meshes[0].MorphWeights.clear();for (auto& node:reference->Nodes) {node.MorphWeights.clear();}for (auto& clip:reference->Animations) {clip.MorphTracks.clear();}
    lv::Scene cpu;const auto ref=cpu.AddModel(reference);cpu.MainCamera=scene.MainCamera;cpu.Sun=scene.Sun;cpu.GetAnimation(ref).Play(0);cpu.GetAnimation(ref).Seek(.5);
    const auto compare=[](const auto& a,const auto& b) {std::size_t differences=0;for (std::size_t i=0;i<a.size();++i) {for (int c=0;c<3;++c) {if (std::abs(int(a[i][c])-int(b[i][c]))>3) {++differences;break;}}}return differences;};
    const auto gpu=render(scene),expected=render(cpu);Check(compare(gpu,expected)<12,"GPU Morph-before-Skinning matches CPU-deformed geometry including tangent-space normal mapping");
    Require(bool(surface.SetDebugOptions({lv::DebugView::Normal})),"Cannot select normal view.");Check(compare(render(scene),render(cpu))<12,"GPU NORMAL morph deltas match normalized CPU normals");Require(bool(surface.SetDebugOptions({})),"Cannot restore lit view.");(void)render(scene);
    const auto estimate=lv::EstimateQualityResources(profile,surface.Quality().Capabilities,scene,Size,Size);Check(estimate && estimate.Value().Cost.BufferBytes == surface.Stats().LogicalBufferBytes,"packed GPU morph storage is included in public memory costs");
    player.Pause();Check(!surface.Render(scene,clock++,false,{0,0,0,1}),"paused unchanged morph weights retain cached rendering");player.SetMorphWeights(3,std::array<float,2>{0,0});const auto reset=render(scene);Check(reset!=gpu,"manual morph edits invalidate cached pixels");
    player.ClearMorphWeights(3);player.Play(4);player.Seek(.5);const auto halfway=render(scene);player.Seek(1);Check(render(scene)!=halfway,"weight animation updates real GPU geometry");
    player.ResetPose();player.SetMorphWeights(3,std::array<float,2>{50,0});scene.MainCamera.Position.X=scene.MainCamera.Target.X=25;const auto outsideBase=render(scene);Check(std::count_if(outsideBase.begin(),outsideBase.end(),[](const auto& p){return p[0]+p[1]+p[2]>20;})>30 && surface.Stats().LastDrawCalls == 1,"morphed bounds prevent base-AABB false culling");
    profile.Overrides.Shadow=lv::ShadowMode::Pcf;Require(bool(surface.ApplyQuality(profile,scene,Size,Size)),"Cannot enable morph shadows.");(void)render(scene);Check(surface.Stats().LastShadowDrawCalls == 1,"Morph+Skinning participates in the actual shadow pass");
    scene.MainCamera.Position.X=scene.MainCamera.Target.X=0;player.SetMorphWeights(3,weights);(void)render(scene);const auto bytes=surface.Stats().LogicalBufferBytes;lv::Transform offset;offset.Position.X=-1.3f;const auto second=scene.AddModel(model,offset);scene.GetAnimation(second).SetMorphWeights(3,std::array<float,2>{0,0});const auto both=render(scene);scene.GetAnimation(second).SetMorphWeights(3,std::array<float,2>{1,0});const auto changed=render(scene);bool independent=true;for (int y=0;y<Size;++y) {for (int x=Size/2;x<Size;++x) {independent &= both[y*Size+x] == changed[y*Size+x];}}
    Check(independent && both!=changed && surface.Stats().ResidentModels == 1 && surface.Stats().LogicalBufferBytes == bytes && surface.Stats().LastDrawCalls == 2,"instances share morph GPU data while retaining independent weights");
}
void Initialize() {
    sg_desc desc{}; desc.environment = sglue_environment(); desc.logger.func = Log;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH;
    desc.environment.defaults.sample_count = 1;
    sg_setup(desc);
}
void RunCompatibility() {
    lv::AssetRegistry registry;Target target;lv::detail::SceneRenderer renderer;
    for (const auto* file:{"Compatibility.obj","Compatibility.fbx","CompatibilityBinary.fbx"}) {
        const auto model=registry.LoadModel(Fixture(file));lv::Scene scene;const auto object=scene.AddModel(model);
        const auto center=lv::Vec3{(model->ModelBounds.Min.X+model->ModelBounds.Max.X)*.5f,0,0};
        scene.MainCamera.Target=center;scene.MainCamera.Position={center.X,0,3};scene.Sun.Intensity=0;scene.Sun.Ambient=1;
        const auto before=target.Render(renderer,scene);std::size_t lit=0;for (int y=0;y<Size;++y) {for (int x=0;x<Size;++x) {lit+=Lit(before,x,y);}}
        Check(lit>150,"OBJ/ASCII FBX/binary FBX imported triangles produce actual GPU pixels");
        Check(renderer.GetStats().ResidentModels == 1,"format change releases the previous GPU asset");
        if (!model->Animations.empty()) {scene.GetAnimation(object).Play(0);scene.GetAnimation(object).Seek(1);const auto after=target.Render(renderer,scene);std::size_t changed=0;for (std::size_t i=0;i<after.size();++i) {changed+=before[i]!=after[i];}Check(changed>100,"FBX animation changes real GPU skinning pixels");}
    }
    renderer.Clear();
}
void Frame() {
    if (ran) { sapp_request_quit(); return; }
    ran = true;
    try { Require(sg_isvalid(), "Sokol initialization failed."); if (qualityTests) {RunQuality();RunEnhancements();RunCompatibility();RunDebug();RunOptimization();RunCamera();RunBlend();RunPbr();RunMorph();} else if (morphTests) {RunMorph();}else if (pbrTests) {RunPbr();}else if (animationTests) { RunAnimation(); } else if (compatibilityTests) {RunCompatibility();}else { Run(); } }
    catch (const std::exception& error) { Check(false, error.what()); }
    sapp_request_quit();
}
void Cleanup() { if (sg_isvalid()) { sg_shutdown(); } }
} // namespace

int main(int argc, char** argv) {
    animationTests = argc > 1 && std::strcmp(argv[1],"--animation") == 0;
    qualityTests=argc > 1 && std::strcmp(argv[1],"--quality") == 0;
    compatibilityTests=argc > 1 && std::strcmp(argv[1],"--compatibility") == 0;
    pbrTests=argc > 1 && std::strcmp(argv[1],"--pbr") == 0;
    morphTests=argc > 1 && std::strcmp(argv[1],"--morph") == 0;
    sapp_desc app{};
    app.width = app.height = 64; app.window_title = "Minuvex D3D11 pixel tests";
    app.init_cb = Initialize; app.frame_cb = Frame; app.cleanup_cb = Cleanup; app.logger.func = Log;
    sapp_run(app);
    Check(!failed, "Sokol reported no rendering errors");
    return Finish();
}
