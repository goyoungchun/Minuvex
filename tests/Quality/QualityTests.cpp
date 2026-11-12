#include <minuvex/Quality.h>
#include <minuvex/Viewer.h>
#include "TestHarness.h"
#include <json.hpp>
#include <limits>

using namespace test;
using Json=nlohmann::json;
namespace {
lv::QualityCapabilities Caps() {lv::QualityCapabilities c;c.GraphicsAvailable=true;c.ShadowMapping=c.HdrPostProcessing=true;c.SampleCounts=c.HdrSampleCounts=(1u<<1)|(1u<<2)|(1u<<4)|(1u<<8);return c;}
void Policy() {
    lv::RenderSettings invalidShadow;invalidShadow.Shadow=lv::ShadowMode::Pcf;invalidShadow.ShadowResolution=513;Rejects([&]{(void)lv::RenderTargetBytes(invalidShadow,16,16);},"public target byte helper rejects unsupported shadow resolutions");
    auto low=lv::MakeQualityProfile(lv::QualityPreset::Low),medium=lv::MakeQualityProfile(lv::QualityPreset::Medium),high=lv::MakeQualityProfile(lv::QualityPreset::High);
    auto l=lv::ExpandQuality(low),m=lv::ExpandQuality(medium),h=lv::ExpandQuality(high);
    Check(l && l.Value().MaxRenderHeight == 720 && l.Value().TargetFps == 30 && l.Value().MaxTextureSize == 512 && l.Value().MsaaSamples == 1,"Low profile reduces render/texture load without disabling skinning");
    Check(m && m.Value().MaxRenderHeight == 1080 && m.Value().MaxTextureSize == 2048 && m.Value().TargetFps == 60 && m.Value().Lighting == lv::LightingMode::Advanced && m.Value().Shadow == lv::ShadowMode::Basic && !m.Value().PostProcessing,"Medium enables standard material lighting and Basic shadow without HDR");
    Check(h && h.Value().MsaaSamples == 4 && h.Value().Anisotropy == 8 && h.Value().MaxTextureSize == 8192 && h.Value().PostProcessing && h.Value().ShadowResolution == 2048,"High enables HDR, larger shadows and MSAA/texture/sampling features");
    Check(!low.IsCustom() && !medium.IsCustom() && !high.IsCustom(),"Custom is explicit overrides, not a fourth base preset");
    low.Overrides.TargetFps=0;low.Overrides.FrustumCulling=false;Check(low.IsCustom() && lv::ExpandQuality(low).Value().TargetFps == 0 && !lv::ExpandQuality(low).Value().FrustumCulling,"zero/false overrides are retained");
    auto caps=Caps();auto resolved=lv::ResolveQuality(medium,caps,3840,2160);
    Check(resolved && resolved.Value().RenderWidth == 1920 && resolved.Value().RenderHeight == 1080 && !resolved.Value().Applied,"CPU resolver computes extent but does not claim GPU application");
    medium.Overrides.ResolutionScale=.5f;resolved=lv::ResolveQuality(medium,caps,3840,2160);
    Check(resolved && resolved.Value().RenderWidth == 960 && resolved.Value().RenderHeight == 540,"resolution scale applies after preset height cap");
    medium.Overrides.Resolution=lv::ResolutionMode::Fixed;medium.Overrides.FixedWidth=333;medium.Overrides.FixedHeight=222;
    resolved=lv::ResolveQuality(medium,caps,100,80);Check(resolved && resolved.Value().RenderWidth == 333 && resolved.Value().RenderHeight == 222,"fixed extent is independent of output size");
    auto request=high;request.Overrides.MsaaSamples=8;caps.SampleCounts=caps.HdrSampleCounts=(1u<<1)|(1u<<4);
    auto strict=lv::ResolveQuality(request,caps,640,480);Check(!strict && strict.Error().Code == lv::ErrorCode::UnsupportedFeature,"unsupported MSAA is rejected by default");
    request.AllowFallback=true;resolved=lv::ResolveQuality(request,caps,640,480);
    Check(resolved && resolved.Value().RequestedSettings.MsaaSamples == 8 && resolved.Value().Effective->MsaaSamples == 4 && !resolved.Value().Adjustments.empty(),"requested/effective remain distinct after explicit fallback");
    caps=Caps();caps.MaxAnisotropy=2;caps.MaxTextureSize=1024;
    Check(!lv::ResolveQuality(high,caps,640,480),"strict texture/anisotropy capability validation");
    high.AllowFallback=true;resolved=lv::ResolveQuality(high,caps,640,480);
    Check(resolved && resolved.Value().Effective->Anisotropy == 2 && resolved.Value().Effective->MaxTextureSize == 1024,"texture and anisotropy fallback is reported");
    request=lv::MakeQualityProfile(lv::QualityPreset::Medium);request.Overrides.Lighting=lv::LightingMode::Advanced;request.Overrides.Shadow=lv::ShadowMode::Pcss;request.Overrides.PostProcessing=true;request.Overrides.Lod=true;request.Overrides.LodBias=1.0f;
    Check(!lv::ResolveQuality(request,Caps(),640,480),"unimplemented rendering enhancements cannot be advertised as effective");
    request.AllowFallback=true;resolved=lv::ResolveQuality(request,Caps(),640,480);
    Check(resolved && resolved.Value().Effective->Lighting == lv::LightingMode::Advanced && resolved.Value().Effective->Shadow == lv::ShadowMode::Pcf && resolved.Value().Effective->PostProcessing && resolved.Value().Effective->Lod && resolved.Value().Effective->LodBias == 1 && resolved.Value().Adjustments.size() == 1,"fallback preserves implemented LOD and adjusts only PCSS");
    auto compressed=lv::MakeQualityProfile(lv::QualityPreset::Low);compressed.Overrides.Compression=lv::TextureCompression::Bc3;Check(!lv::ResolveQuality(compressed,Caps(),640,480),"explicit BC3 requires capability support");compressed.AllowFallback=true;auto fallback=lv::ResolveQuality(compressed,Caps(),640,480);Check(fallback && fallback.Value().Effective->Compression == lv::TextureCompression::None && !fallback.Value().Adjustments.empty(),"BC3 fallback is explicit and reported");
    request=lv::MakeQualityProfile(lv::QualityPreset::High);caps=Caps();caps.ShadowMapping=caps.HdrPostProcessing=false;
    const auto unavailable=lv::ResolveQuality(request,caps,640,480);request.AllowFallback=true;resolved=lv::ResolveQuality(request,caps,640,480);
    Check(!unavailable && unavailable.Error().Code == lv::ErrorCode::UnsupportedFeature && resolved && resolved.Value().Effective->Shadow == lv::ShadowMode::Off && !resolved.Value().Effective->PostProcessing && resolved.Value().RequestedSettings.PostProcessing,"missing shadow/HDR formats require explicit fallback and preserve the request");
    caps=Caps();caps.HdrSampleCounts=1u<<1;resolved=lv::ResolveQuality(request,caps,640,480);
    Check(resolved && resolved.Value().Effective->MsaaSamples == 1 && resolved.Value().Effective->PostProcessing,"HDR uses its own sample mask even when LDR supports 4x");
    request=lv::MakeQualityProfile(lv::QualityPreset::High);caps=Caps();caps.RenderTargetBudgetBytes=1024*1024;
    strict=lv::ResolveQuality(request,caps,1920,1080);Check(!strict && strict.Error().Code == lv::ErrorCode::InsufficientResource,"attachment budget is checked before allocation");
    request.AllowFallback=true;resolved=lv::ResolveQuality(request,caps,1920,1080);
    Check(resolved && lv::RenderTargetBytes(*resolved.Value().Effective,resolved.Value().RenderWidth,resolved.Value().RenderHeight) <= caps.RenderTargetBudgetBytes,"fallback includes HDR/shadow/MSAA resources within configured budget");
    request.Overrides.Resolution=lv::ResolutionMode::Fixed;request.Overrides.FixedWidth=16384;request.Overrides.FixedHeight=1;caps.RenderTargetBudgetBytes=8;
    resolved=lv::ResolveQuality(request,caps,1,1);Check(resolved && resolved.Value().RenderWidth == 1 && resolved.Value().RenderHeight == 1,"one-pixel/thin extent cannot escape budget after rounding");
    request=lv::MakeQualityProfile(lv::QualityPreset::Medium);request.Overrides.Resolution=lv::ResolutionMode::Fixed;request.Overrides.FixedWidth=4000;caps=Caps();caps.MaxRenderSize=1024;
    Check(!lv::ResolveQuality(request,caps,100,100),"strict oversized target rejected");request.AllowFallback=true;
    resolved=lv::ResolveQuality(request,caps,100,100);Check(resolved && resolved.Value().RenderWidth == 1024 && resolved.Value().Effective->FixedWidth == 1024,"device extent cap becomes effective fixed width");
    caps=Caps();caps.GraphicsAvailable=false;Check(!lv::ResolveQuality(request,caps,100,100),"no-device resolution fails instead of inventing graphics support");
    caps=Caps();caps.SampleCounts=0;Check(!lv::ResolveQuality(request,caps,100,100),"invalid host capability mask rejected");
    const auto invalid=[&](lv::QualityProfile p,const char* label){Check(!lv::ExpandQuality(p),label);};
    request={};request.Name="";invalid(request,"empty name rejected");request.Name=std::string("bad\0name",8);invalid(request,"NUL name rejected");request.Name="\xFF";invalid(request,"invalid UTF-8 rejected");
    request={};request.Base=static_cast<lv::QualityPreset>(99);invalid(request,"invalid preset rejected");
    request={};request.Overrides.ResolutionScale=.49f;invalid(request,"resolution scale below supported range rejected");request.Overrides.ResolutionScale=std::numeric_limits<float>::quiet_NaN();invalid(request,"nonfinite scale rejected");
    request={};request.Overrides.FixedWidth=0;invalid(request,"zero dimensions rejected");request={};request.Overrides.TargetFps=241;invalid(request,"invalid target FPS rejected");
    request={};request.Overrides.MsaaSamples=3;invalid(request,"non-power-of-two sample count rejected");request={};request.Overrides.Anisotropy=17;invalid(request,"anisotropy limit enforced");
    request={};request.Overrides.ShadowResolution=123;invalid(request,"invalid shadow resolution rejected");request={};request.Overrides.LodBias=std::numeric_limits<float>::infinity();invalid(request,"nonfinite LOD bias rejected");
    lv::Viewer viewer;auto staged=viewer.GetQuality();Check(staged && !staged.Value().Effective && !staged.Value().Applied,"Viewer stages requested quality before Run");
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);Check(bool(viewer.SetQuality(profile)) && viewer.GetQuality().Value().Requested.Base == lv::QualityPreset::Low,"Viewer quality can be changed without graphics");
    profile.Overrides.MsaaSamples=3;Check(!viewer.SetQuality(profile) && viewer.GetQuality().Value().Requested.Base == lv::QualityPreset::Low,"failed staging preserves prior request");
    Check(!viewer.GetQualityCapabilities() && bool(viewer.RequestRedraw()),"capability query requires a context; pre-Run redraw is harmless");
}
void Persistence() {
    TemporaryDirectory temp;const auto file=temp.File("Profile.mvxquality.json");
    auto p=lv::MakeQualityProfile(lv::QualityPreset::Low);p.Name="사용자 profile";p.AllowFallback=true;p.Overrides.TargetFps=0;p.Overrides.FrustumCulling=false;p.Overrides.ResolutionScale=.75f;p.Overrides.Textures=lv::TextureQuality::High;p.Overrides.OnDemand=lv::OnDemandMode::Auto;
    Check(bool(lv::SaveQuality(p,file)),"quality profile saved independently of Scene");auto loaded=lv::LoadQuality(file);
    Check(loaded && loaded.Value() == p,"base and only explicit overrides round trip, including UTF-8/false/zero");
    const auto bytes=Read(file);auto json=Json::parse(bytes);Check(json["overrides"].size() == 5 && !json["overrides"].contains("msaaSamples"),"defaults are not flattened into Custom overrides");
    const auto rejects=[&](Json bad,const char* label){Write(file,bad.dump());auto result=lv::LoadQuality(file);Check(!result && result.Error().Code == lv::ErrorCode::InvalidData,label);};
    auto bad=json;bad["version"]=2;rejects(bad,"future quality schema rejected");bad=json;bad["version"]=1.0;rejects(bad,"fractional version rejected");bad=json;bad["baseProfile"]="custom";rejects(bad,"Custom is not a base preset");
    bad=json;bad["overrides"]["typo"]=1;rejects(bad,"unknown overrides cannot silently change quality");bad=json;bad["overrides"]["targetFps"]=1.5;rejects(bad,"fractional integer option rejected");bad=json;bad["overrides"]["frustumCulling"]=1;rejects(bad,"nonboolean flag rejected");bad=json;bad["overrides"]["resolutionScale"]=nullptr;rejects(bad,"null override is not treated as missing");
    bad=json;bad.erase("format");bad["overrides"]["onDemandRendering"]=true;Write(file,bad.dump());loaded=lv::LoadQuality(file);Check(loaded && loaded.Value().Overrides.OnDemand == lv::OnDemandMode::On,"planning-document v1 boolean onDemand schema remains readable");
    Write(file,"{\"version\":1,\"version\":1}");Check(!lv::LoadQuality(file),"duplicate JSON keys rejected");Write(file,std::string(1024*1024+1,' '));Check(!lv::LoadQuality(file),"oversized quality JSON rejected");
    Write(file,bytes);p.Overrides.MsaaSamples=3;Check(!lv::SaveQuality(p,file) && Read(file) == bytes,"invalid profile save preserves destination");
    Check(!lv::SaveQuality(lv::QualityProfile{},temp.File("missing")/"file.json"),"IO failure returns an Error");
    bool extra=false;for (const auto& entry:std::filesystem::directory_iterator(temp.Path)) {extra|=entry.path().filename().string().find(".tmp-") != std::string::npos;}Check(!extra,"failed profile write leaves no temporary file");
}
void CullingAndPacing() {
    const lv::Bounds b{{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};const auto vp=lv::Camera{}.ViewProjection(1);
    Check(lv::IntersectsFrustum(b,lv::Mat4::Identity(),vp),"visible AABB retained");
    Check(!lv::IntersectsFrustum(b,lv::Mat4::Translation({100,0,0}),vp),"side-plane rejection");
    Check(!lv::IntersectsFrustum(b,lv::Mat4::Translation({0,0,100}),vp),"behind-camera rejection");
    Check(lv::IntersectsFrustum(b,lv::Mat4::Scale({-2,1,1}),vp),"mirrored AABB retained");
    Rejects([&]{(void)lv::IntersectsFrustum({{1,0,0},{0,0,0}},lv::Mat4::Identity(),vp);},"invalid bounds rejected");
    lv::RenderScheduler scheduler;Check(scheduler.Due(1,30) && scheduler.NeedsRender(lv::OnDemandMode::Auto,10,false),"first static frame renders");scheduler.Submitted(1,10);
    Check(!scheduler.Due(1.01,30) && scheduler.Due(1+1.0/30,30),"target frame pacing uses elapsed time");
    Check(!scheduler.NeedsRender(lv::OnDemandMode::On,10,false) && scheduler.NeedsRender(lv::OnDemandMode::Auto,11,false),"static content skips while dirty content wakes");
    Check(scheduler.NeedsRender(lv::OnDemandMode::Auto,10,true) && scheduler.NeedsRender(lv::OnDemandMode::Off,10,false),"animation and continuous policy keep rendering");
    Check(scheduler.Due(1,0) && scheduler.Due(.5,60),"unlimited FPS and clock reset are supported");scheduler.Invalidate();Check(scheduler.NeedsRender(lv::OnDemandMode::On,10,false),"explicit invalidation wakes static rendering");
    Rejects([&]{(void)scheduler.Due(-1,30);},"invalid pacing clock rejected");
}
}
int main() {try {Policy();Persistence();CullingAndPacing();}catch (const std::exception& e) {Check(false,e.what());}return Finish();}
