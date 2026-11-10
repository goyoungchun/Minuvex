// Backend-specific readback belongs to this test, never to the public SDK.
#include "TestHarness.h"
#include "renderer/SceneRenderer.h"
#include "renderer/RenderSurface.h"
#include "renderer/TextureData.h"

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
    changed.Overrides.FrustumCulling=false;Require(bool(surface.ApplyQuality(changed,scene,Size,Size)),"Cannot disable culling.");surface.Render(scene,now+=1,true,{0,0,0,1});sg_commit();Check(surface.Stats().LastDrawCalls == 2,"disabling culling restores draw submissions");
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
    Require(bool(surface.Resize(textured,0,0)),"Cannot suspend quality surface.");Check(!surface.Texture().id && !surface.Quality().Applied,"zero-sized viewport releases targets");Require(bool(surface.Resize(textured,Size,Size)),"Cannot resume quality surface.");
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
    try { Require(sg_isvalid(), "Sokol initialization failed."); if (qualityTests) {RunQuality();RunEnhancements();} else if (animationTests) { RunAnimation(); } else { Run(); } }
    catch (const std::exception& error) { Check(false, error.what()); }
    sapp_request_quit();
}
void Cleanup() { if (sg_isvalid()) { sg_shutdown(); } }
} // namespace

int main(int argc, char** argv) {
    animationTests = argc > 1 && std::strcmp(argv[1],"--animation") == 0;
    qualityTests=argc > 1 && std::strcmp(argv[1],"--quality") == 0;
    sapp_desc app{};
    app.width = app.height = 64; app.window_title = "Minuvex D3D11 pixel tests";
    app.init_cb = Initialize; app.frame_cb = Frame; app.cleanup_cb = Cleanup; app.logger.func = Log;
    sapp_run(app);
    Check(!failed, "Sokol reported no rendering errors");
    return Finish();
}
