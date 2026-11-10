#pragma once
#include <minuvex/Result.h>
#include <minuvex/Math.h>

#include <optional>
#include <cstdint>
#include <vector>

namespace lv {
enum class QualityPreset { Low, Medium, High };
enum class ResolutionMode { Relative, Fixed };
enum class TextureQuality { Low, Medium, High };
enum class LightingMode { Unlit, Basic, Advanced };
enum class ShadowMode { Off, Basic, Pcf, Pcss };
enum class OnDemandMode { Off, On, Auto };
enum class GraphicsBackend { None, D3D11, Metal };

struct RenderSettings {
    ResolutionMode Resolution = ResolutionMode::Relative;
    float ResolutionScale = 1;
    int FixedWidth = 1280, FixedHeight = 720, MaxRenderHeight = 1080;
    int TargetFps = 60; // 0 = unlimited; 1..240 otherwise.
    int MsaaSamples = 1;
    TextureQuality Textures = TextureQuality::Medium;
    int MaxTextureSize = 2048; // Derived from texture quality and device limit.
    int Anisotropy = 1;
    LightingMode Lighting = LightingMode::Basic;
    bool FrustumCulling = true;
    OnDemandMode OnDemand = OnDemandMode::Off;
    ShadowMode Shadow = ShadowMode::Off;
    int ShadowResolution = 1024;
    bool Lod = false;
    float LodBias = 0;
    bool PostProcessing = false;
    bool operator==(const RenderSettings&) const = default;
};

// Optional fields retain which values were explicitly overridden, including false/0.
struct QualityOverrides {
    std::optional<ResolutionMode> Resolution;
    std::optional<float> ResolutionScale;
    std::optional<int> FixedWidth, FixedHeight, MaxRenderHeight, TargetFps, MsaaSamples;
    std::optional<TextureQuality> Textures;
    std::optional<int> Anisotropy;
    std::optional<LightingMode> Lighting;
    std::optional<bool> FrustumCulling;
    std::optional<OnDemandMode> OnDemand;
    std::optional<ShadowMode> Shadow;
    std::optional<int> ShadowResolution;
    std::optional<bool> Lod;
    std::optional<float> LodBias;
    std::optional<bool> PostProcessing;
    bool Empty() const noexcept;
    bool operator==(const QualityOverrides&) const = default;
};
struct QualityProfile {
    std::string Name = "Default";
    QualityPreset Base = QualityPreset::Medium;
    QualityOverrides Overrides;
    bool AllowFallback = false; // Unsupported options are rejected unless explicitly enabled.
    bool IsCustom() const noexcept { return !Overrides.Empty(); }
    bool operator==(const QualityProfile&) const = default;
};
struct QualityCapabilities {
    GraphicsBackend Backend = GraphicsBackend::None;
    bool GraphicsAvailable = false;
    int MaxRenderSize = 8192, MaxTextureSize = 8192, MaxAnisotropy = 16;
    std::uint32_t SampleCounts = 1u << 1; // Bit N denotes support for N samples (1/2/4/8).
    bool ShadowMapping = false, HdrPostProcessing = false;
    std::uint32_t HdrSampleCounts = 1u << 1;
    // Host-configured attachment budget, not measured free VRAM (Phase 7).
    std::uint64_t RenderTargetBudgetBytes = 256ull * 1024 * 1024;
};
struct QualityAdjustment { std::string Field, Reason; };
struct QualityState {
    QualityProfile Requested;
    RenderSettings RequestedSettings;
    std::optional<RenderSettings> Effective;
    QualityCapabilities Capabilities;
    int RenderWidth = 0, RenderHeight = 0;
    bool Applied = false; // GPU resources committed; false for CPU resolution/pre-Run staging.
    std::vector<QualityAdjustment> Adjustments;
    std::optional<lv::Error> LastError;
};

QualityProfile MakeQualityProfile(QualityPreset preset);
RenderSettings QualityPresetSettings(QualityPreset preset);
Result<RenderSettings> ExpandQuality(const QualityProfile& profile); // Syntactic validation only.
Result<QualityState> ResolveQuality(const QualityProfile& profile, const QualityCapabilities& capabilities,
                                   int outputWidth, int outputHeight);
std::uint64_t RenderTargetBytes(const RenderSettings& settings, int width, int height);
// CPU-only profile IO, independent of Scene and Editor (.mvxquality.json v1).
Result<void> SaveQuality(const QualityProfile& profile, const std::filesystem::path& path);
Result<QualityProfile> LoadQuality(const std::filesystem::path& path);

// Conservative AABB test, RH clip space with depth [0,1]. Invalid values throw.
bool IntersectsFrustum(const Bounds& bounds, const Mat4& world, const Mat4& viewProjection);

// CPU policy shared by Viewer and viewport; explicit clocks make pacing testable.
class RenderScheduler {
public:
    bool Due(double now, int targetFps) const;
    bool NeedsRender(OnDemandMode mode, std::uint64_t signature, bool dynamic) const noexcept;
    void Submitted(double now, std::uint64_t signature) noexcept;
    void Invalidate() noexcept { valid_ = false; }
private:
    bool valid_ = false;
    double last_ = 0;
    std::uint64_t signature_ = 0;
};
} // namespace lv
