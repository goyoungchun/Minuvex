#include <minvex/Quality.h>

#include <algorithm>
#include <cmath>

namespace lv {
namespace {
void Require(bool condition, const char* message) { if (!condition) { throw std::invalid_argument(message); } }
bool Utf8(const std::string& text) {
    for (std::size_t i=0;i<text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]); if (c < 128) { if (!c) { return false; } continue; }
        unsigned remaining=0, code=0, min=0;
        if (c >= 0xC2 && c <= 0xDF) { remaining=1; code=c&31; min=128; }
        else if (c >= 0xE0 && c <= 0xEF) { remaining=2; code=c&15; min=2048; }
        else if (c >= 0xF0 && c <= 0xF4) { remaining=3; code=c&7; min=65536; } else { return false; }
        while (remaining--) { if (i == text.size()) { return false; } const auto next=static_cast<unsigned char>(text[i++]); if ((next&0xC0) != 0x80) { return false; } code=(code<<6)|(next&63); }
        if (code < min || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return false; }
    } return true;
}
int TextureLimit(TextureQuality q) {
    switch (q) { case TextureQuality::Low:return 512; case TextureQuality::Medium:return 2048; case TextureQuality::High:return 8192; }
    throw std::invalid_argument("Invalid texture quality.");
}
}
bool QualityOverrides::Empty() const noexcept {
    return !Resolution && !ResolutionScale && !FixedWidth && !FixedHeight && !MaxRenderHeight && !TargetFps && !MsaaSamples && !Textures && !Anisotropy && !Lighting && !FrustumCulling && !OnDemand && !Shadow && !ShadowResolution && !ShadowSourceAngle && !ShadowBlockerSamples && !ShadowFilterSamples && !ShadowMaxFilterRadius && !ShadowDepthBias && !ShadowSlopeBias && !Lod && !LodBias && !PostProcessing && !Instancing && !Compression && !Pbr;
}
RenderSettings QualityPresetSettings(QualityPreset preset) {
    RenderSettings s;
    switch (preset) {
    case QualityPreset::Low: s.MaxRenderHeight=720; s.TargetFps=30; s.Textures=TextureQuality::Low; s.OnDemand=OnDemandMode::Auto;s.Lod=true;break;
    case QualityPreset::Medium: s.Lighting=LightingMode::Advanced;s.Shadow=ShadowMode::Basic;s.Lod=true;break;
    case QualityPreset::High: s.MaxRenderHeight=2160; s.MsaaSamples=4; s.Textures=TextureQuality::High; s.Anisotropy=8;s.Lighting=LightingMode::Advanced;s.Shadow=ShadowMode::Pcf;s.ShadowResolution=2048;s.PostProcessing=true;s.Pbr=true;break;
    default: throw std::invalid_argument("Invalid base quality preset.");
    }
    s.MaxTextureSize=TextureLimit(s.Textures); return s;
}
QualityProfile MakeQualityProfile(QualityPreset preset) {
    (void)QualityPresetSettings(preset); QualityProfile p; p.Base=preset;
    p.Name=preset == QualityPreset::Low ? "Low" : preset == QualityPreset::High ? "High" : "Medium"; return p;
}
Result<RenderSettings> ExpandQuality(const QualityProfile& p) {
    try {
        Require(!p.Name.empty() && p.Name.size() <= 256 && Utf8(p.Name),"Quality name requires 1..256 UTF-8 bytes without NUL.");
        auto s=QualityPresetSettings(p.Base); const auto& o=p.Overrides;
#define LV_OVERRIDE(field) if (o.field) { s.field=*o.field; }
        LV_OVERRIDE(Resolution) LV_OVERRIDE(ResolutionScale) LV_OVERRIDE(FixedWidth) LV_OVERRIDE(FixedHeight) LV_OVERRIDE(MaxRenderHeight)
        LV_OVERRIDE(TargetFps) LV_OVERRIDE(MsaaSamples) LV_OVERRIDE(Textures) LV_OVERRIDE(Anisotropy) LV_OVERRIDE(Lighting) LV_OVERRIDE(FrustumCulling)
        LV_OVERRIDE(OnDemand) LV_OVERRIDE(Shadow) LV_OVERRIDE(ShadowResolution) LV_OVERRIDE(Lod) LV_OVERRIDE(LodBias) LV_OVERRIDE(PostProcessing)
        LV_OVERRIDE(Instancing) LV_OVERRIDE(Compression) LV_OVERRIDE(Pbr)
        LV_OVERRIDE(ShadowSourceAngle) LV_OVERRIDE(ShadowBlockerSamples) LV_OVERRIDE(ShadowFilterSamples)
        LV_OVERRIDE(ShadowMaxFilterRadius) LV_OVERRIDE(ShadowDepthBias) LV_OVERRIDE(ShadowSlopeBias)
#undef LV_OVERRIDE
        Require(s.Resolution == ResolutionMode::Relative || s.Resolution == ResolutionMode::Fixed,"Invalid resolution mode.");
        Require(std::isfinite(s.ResolutionScale) && s.ResolutionScale >= .5f && s.ResolutionScale <= 1,"Resolution scale must be in [0.5,1].");
        Require(s.FixedWidth > 0 && s.FixedWidth <= 16384 && s.FixedHeight > 0 && s.FixedHeight <= 16384 && s.MaxRenderHeight > 0 && s.MaxRenderHeight <= 16384,"Render dimensions must be in [1,16384].");
        Require(s.TargetFps >= 0 && s.TargetFps <= 240,"Target FPS must be 0 (unlimited) or 1..240.");
        Require(s.MsaaSamples == 1 || s.MsaaSamples == 2 || s.MsaaSamples == 4 || s.MsaaSamples == 8,"MSAA samples must be 1,2,4,8.");
        Require(s.Anisotropy >= 1 && s.Anisotropy <= 16,"Anisotropy must be in [1,16].");
        Require(s.Lighting == LightingMode::Unlit || s.Lighting == LightingMode::Basic || s.Lighting == LightingMode::Advanced,"Invalid lighting mode.");
        Require(s.OnDemand == OnDemandMode::Off || s.OnDemand == OnDemandMode::On || s.OnDemand == OnDemandMode::Auto,"Invalid on-demand mode.");
        Require(s.Shadow == ShadowMode::Off || s.Shadow == ShadowMode::Basic || s.Shadow == ShadowMode::Pcf || s.Shadow == ShadowMode::Pcss,"Invalid shadow mode.");
        Require(s.ShadowResolution == 512 || s.ShadowResolution == 1024 || s.ShadowResolution == 2048 || s.ShadowResolution == 4096,"Shadow resolution must be 512/1024/2048/4096.");
        Require(std::isfinite(s.ShadowSourceAngle) && s.ShadowSourceAngle >= 0 && s.ShadowSourceAngle <= 20,"Shadow source angle must be in [0,20] degrees.");
        Require(s.ShadowBlockerSamples >= 1 && s.ShadowBlockerSamples <= 64 && s.ShadowFilterSamples >= 1 && s.ShadowFilterSamples <= 64,"PCSS sample counts must be in [1,64].");
        Require(std::isfinite(s.ShadowMaxFilterRadius) && s.ShadowMaxFilterRadius >= 1 && s.ShadowMaxFilterRadius <= 64,"Shadow filter radius must be in [1,64] texels.");
        Require(std::isfinite(s.ShadowDepthBias) && s.ShadowDepthBias >= 0 && s.ShadowDepthBias <= .1f && std::isfinite(s.ShadowSlopeBias) && s.ShadowSlopeBias >= 0 && s.ShadowSlopeBias <= .1f,"Shadow depth/slope bias must be in [0,0.1].");
        Require(std::isfinite(s.LodBias) && std::abs(s.LodBias) <= 4,"LOD bias must be finite and in [-4,4].");
        Require(s.Compression == TextureCompression::None || s.Compression == TextureCompression::Auto || s.Compression == TextureCompression::Bc3,"Invalid texture compression.");
        s.MaxTextureSize=TextureLimit(s.Textures); return Result<RenderSettings>::Success(s);
    } catch (const std::invalid_argument& e) { return Result<RenderSettings>::Failure({ErrorCode::InvalidArgument,e.what(),"Correct the quality profile values.",{}}); }
}
Result<QualityState> ResolveQuality(const QualityProfile& p,const QualityCapabilities& caps,int width,int height) {
    auto expanded=ExpandQuality(p); if (!expanded) { return Result<QualityState>::Failure(expanded.Error()); }
    try {
        Require(width > 0 && width <= 65536 && height > 0 && height <= 65536,"Output dimensions must be in [1,65536].");
        Require(caps.MaxRenderSize > 0 && caps.MaxRenderSize <= 16384 && caps.MaxTextureSize > 0 && caps.MaxTextureSize <= 32768 && caps.MaxAnisotropy >= 1 && caps.MaxAnisotropy <= 16 && (caps.SampleCounts&(1u<<1)) && (!caps.HdrPostProcessing || (caps.HdrSampleCounts&(1u<<1))) && caps.RenderTargetBudgetBytes >= 8,"Invalid graphics capabilities or attachment budget.");
        if (!caps.GraphicsAvailable) { return Result<QualityState>::Failure({ErrorCode::UnsupportedFeature,"No graphics context is available.","Resolve against a graphics device, or stage the profile before Viewer::Run.",{}}); }
        QualityState state; state.Requested=p; state.RequestedSettings=expanded.Value(); state.Capabilities=caps; auto s=expanded.Value();
        const auto adjust=[&](bool condition,const char* field,const char* reason,auto change) {
            if (!condition) { return true; }
            if (!p.AllowFallback) { return false; }
            change(); state.Adjustments.push_back({field,reason}); return true;
        };
        const auto unsupported=[&](const char* message) { return Result<QualityState>::Failure({ErrorCode::UnsupportedFeature,message,"Choose supported settings or explicitly allow fallback.",{}}); };
        if (!adjust(s.Pbr && (!caps.PbrLighting || s.Lighting!=LightingMode::Advanced),"pbr","PBR requires supported Advanced lighting; using Basic material shading.",[&]{s.Pbr=false;})) {return unsupported("PBR requires supported Advanced lighting.");}
        if (!adjust(s.Shadow != ShadowMode::Off && !caps.ShadowMapping,"shadow","Shadow format support is unavailable; disabled.",[&]{s.Shadow=ShadowMode::Off;})) {return unsupported("Shadow render/sample formats are unavailable.");}
        if (!adjust(s.Shadow != ShadowMode::Off && s.ShadowResolution > caps.MaxRenderSize,"shadowResolution","Shadow size clamped to supported power of two.",[&]{while (s.ShadowResolution > 512 && s.ShadowResolution > caps.MaxRenderSize) {s.ShadowResolution/=2;}if (s.ShadowResolution > caps.MaxRenderSize) {s.Shadow=ShadowMode::Off;}})) {return unsupported("Shadow map exceeds the device extent limit.");}
        if (!adjust(s.PostProcessing && !caps.HdrPostProcessing,"postProcessing","HDR render/filter formats are unavailable; disabled.",[&]{s.PostProcessing=false;})) {return unsupported("HDR post processing is unavailable.");}
        if (s.Compression == TextureCompression::Auto) {s.Compression=caps.Bc3Textures ? TextureCompression::Bc3:TextureCompression::None;}
        if (!adjust(s.Compression == TextureCompression::Bc3 && !caps.Bc3Textures,"textureCompression","BC3 sampling unavailable; using RGBA8.",[&]{s.Compression=TextureCompression::None;})) {return unsupported("BC3 texture sampling is unavailable.");}
        const auto sampleMask=[&]{return s.PostProcessing ? caps.HdrSampleCounts:caps.SampleCounts;};
        if (!adjust(!(sampleMask()&(1u<<s.MsaaSamples)),"msaaSamples","Requested MSAA is unavailable; using the highest supported lower count.",[&]{while (s.MsaaSamples > 1 && !(sampleMask()&(1u<<s.MsaaSamples))) {s.MsaaSamples/=2;}})) { return unsupported("Requested MSAA count is not supported for the render formats."); }
        if (!adjust(s.Anisotropy > caps.MaxAnisotropy,"anisotropy","Clamped to the device's anisotropy limit.",[&]{s.Anisotropy=caps.MaxAnisotropy;})) { return unsupported("Requested anisotropy exceeds the device limit."); }
        if (!adjust(s.MaxTextureSize > caps.MaxTextureSize,"maxTextureSize","Texture size capped by the device.",[&]{s.MaxTextureSize=caps.MaxTextureSize;})) { return unsupported("Texture quality exceeds the device limit."); }
        double w=s.FixedWidth,h=s.FixedHeight;
        if (s.Resolution == ResolutionMode::Relative) { const double scale=std::min(1.0,double(s.MaxRenderHeight)/height)*s.ResolutionScale; w=width*scale;h=height*scale; }
        auto dimensions=[&]{state.RenderWidth=std::max(1,static_cast<int>(std::floor(w)));state.RenderHeight=std::max(1,static_cast<int>(std::floor(h)));}; dimensions();
        if (!adjust(w > caps.MaxRenderSize || h > caps.MaxRenderSize,"renderSize","Render extent reduced to the device limit.",[&]{const double scale=caps.MaxRenderSize/std::max(w,h);w*=scale;h*=scale;dimensions();})) { return unsupported("Render extent exceeds the device limit."); }
        const auto bytes=[&]{return RenderTargetBytes(s,state.RenderWidth,state.RenderHeight);};
        if (bytes() > caps.RenderTargetBudgetBytes) {
            if (!p.AllowFallback) { return Result<QualityState>::Failure({ErrorCode::InsufficientResource,"Render targets exceed the configured attachment budget.","Reduce resolution/MSAA or explicitly allow fallback.",{}}); }
            while (s.MsaaSamples > 1 && bytes() > caps.RenderTargetBudgetBytes) { s.MsaaSamples/=2; while (s.MsaaSamples > 1 && !(sampleMask()&(1u<<s.MsaaSamples))) {s.MsaaSamples/=2;} }
            while (s.Shadow != ShadowMode::Off && s.ShadowResolution > 512 && bytes() > caps.RenderTargetBudgetBytes) {s.ShadowResolution/=2;}
            if (s.Shadow != ShadowMode::Off && bytes() > caps.RenderTargetBudgetBytes) {s.Shadow=ShadowMode::Off;}
            if (s.PostProcessing && RenderTargetBytes(s,1,1) > caps.RenderTargetBudgetBytes) {s.PostProcessing=false;}
            if (bytes() > caps.RenderTargetBudgetBytes) {const double scale=std::sqrt(double(caps.RenderTargetBudgetBytes)/double(bytes()));w=state.RenderWidth*scale;h=state.RenderHeight*scale;dimensions();}
            while (bytes() > caps.RenderTargetBudgetBytes) {if (state.RenderWidth >= state.RenderHeight && state.RenderWidth > 1) {--state.RenderWidth;}else if (state.RenderHeight > 1) {--state.RenderHeight;}else {break;}}
            state.Adjustments.push_back({"renderBudget","MSAA/shadow/post-processing/extent reduced to the configured attachment budget."});
        }
        if (s.Resolution == ResolutionMode::Fixed) { s.FixedWidth=state.RenderWidth;s.FixedHeight=state.RenderHeight; }
        state.Effective=s; return Result<QualityState>::Success(std::move(state));
    } catch (const std::invalid_argument& e) { return Result<QualityState>::Failure({ErrorCode::InvalidArgument,e.what(),"Check output dimensions and supplied graphics capabilities.",{}}); }
}
std::uint64_t RenderTargetBytes(const RenderSettings& s,int width,int height) {
    Require(width > 0 && height > 0 && width <= 65536 && height <= 65536 && (s.MsaaSamples == 1 || s.MsaaSamples == 2 || s.MsaaSamples == 4 || s.MsaaSamples == 8),"Invalid render target extent/sample count.");
    Require(s.Shadow == ShadowMode::Off || ((s.Shadow == ShadowMode::Basic || s.Shadow == ShadowMode::Pcf || s.Shadow == ShadowMode::Pcss) && (s.ShadowResolution == 512 || s.ShadowResolution == 1024 || s.ShadowResolution == 2048 || s.ShadowResolution == 4096)),"Invalid shadow target settings.");
    const std::uint64_t color=s.PostProcessing ? 8:4;
    return std::uint64_t(width)*height*((color+4)*s.MsaaSamples+(s.MsaaSamples > 1 ? color:0)+(s.PostProcessing ? 4:0))+(s.Shadow != ShadowMode::Off ? std::uint64_t(s.ShadowResolution)*s.ShadowResolution*8:0);
}
bool IntersectsFrustum(const Bounds& b,const Mat4& world,const Mat4& vp) {
    Require(std::isfinite(b.Min.X) && std::isfinite(b.Min.Y) && std::isfinite(b.Min.Z) && std::isfinite(b.Max.X) && std::isfinite(b.Max.Y) && std::isfinite(b.Max.Z) && b.Min.X <= b.Max.X && b.Min.Y <= b.Max.Y && b.Min.Z <= b.Max.Z,"Invalid culling bounds.");
    const auto m=vp*world; unsigned outside=63;
    for (int mask=0;mask<8;++mask) {
        const double p[]{mask&1?b.Max.X:b.Min.X,mask&2?b.Max.Y:b.Min.Y,mask&4?b.Max.Z:b.Min.Z,1}; double c[4]{};
        for (int row=0;row<4;++row) {for (int col=0;col<4;++col) {c[row]+=m.Elements[col*4+row]*p[col];} Require(std::isfinite(c[row]),"Culling transform exceeds numeric range.");}
        const unsigned planes=(c[0]<-c[3]?1u:0u)|(c[0]>c[3]?2u:0u)|(c[1]<-c[3]?4u:0u)|(c[1]>c[3]?8u:0u)|(c[2]<0?16u:0u)|(c[2]>c[3]?32u:0u); outside&=planes;
    } return outside == 0;
}
bool RenderScheduler::Due(double now,int fps) const {
    Require(std::isfinite(now) && now >= 0 && fps >= 0 && fps <= 240,"Invalid scheduler time/FPS.");
    return !valid_ || fps == 0 || now < last_ || now-last_+1e-9 >= 1.0/fps;
}
bool RenderScheduler::NeedsRender(OnDemandMode mode,std::uint64_t signature,bool dynamic) const noexcept {return !valid_ || mode == OnDemandMode::Off || dynamic || signature != signature_;}
void RenderScheduler::Submitted(double now,std::uint64_t signature) noexcept {valid_=true;last_=now;signature_=signature;}
} // namespace lv
