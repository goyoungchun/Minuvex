#pragma once
#include <minuvex/SystemInfo.h>
#include <minuvex/Scene.h>

namespace lv {
struct FrameStatistics {
    std::uint64_t TotalRenderedFrames = 0;
    std::size_t SampleCount = 0;
    Metric<double> Fps, AverageFps, AverageFrameMs, P95FrameMs, CpuFrameMs, AverageCpuMs, P95CpuMs;
    Metric<double> GpuFrameMs{std::nullopt, MetricStatus::Unsupported, "GPU timestamp collection unavailable"};
    std::uint64_t DrawCalls = 0, Triangles = 0;
};
// Bounded rolling window (240 submissions). Cadence includes pacing/idle; CPU time excludes both.
class FrameMonitor {
public:
    void Record(double now, double cpuMs, std::uint64_t drawCalls, std::uint64_t triangles);
    FrameStatistics Snapshot(double now) const;
    void Reset() noexcept;
private:
    struct Sample { double Time, Cpu; std::uint64_t Draws, Triangles; };
    std::vector<Sample> samples_;
    std::uint64_t total_ = 0;
};
struct ResourceCost {
    std::uint64_t RenderTargetBytes = 0, TextureBytes = 0, BufferBytes = 0, TotalBytes = 0;
};
struct QualityEstimate {
    QualityState Quality;
    ResourceCost Cost;
    std::uint64_t AssetSignature = 0; // Process-local immutable asset identity; not a persistent hash.
    // Additional allocation and logical transient peak while old resources remain alive.
    std::uint64_t ReusedBytes = 0, AdditionalBytes = 0, TemporaryPeakBytes = 0;
};
// Logical sizes include LDR/HDR, depth, MSAA, shadow/post targets, role-specific
// full mip chains and immutable vertex/index buffers (see RenderTargetBytes).
// Unique visible assets/meshes/images match renderer sharing. No driver alignment or OS usage claims.
Result<QualityEstimate> EstimateQualityResources(const QualityProfile& profile, const QualityCapabilities& caps,
    const Scene& scene, int width, int height, const QualityEstimate* current = nullptr);
enum class ProfileSuitability { Supported, PerformanceRisk, Unsupported };
struct ProfileAssessment {
    QualityPreset Preset = QualityPreset::Medium;
    ProfileSuitability Suitability = ProfileSuitability::Unsupported;
    std::optional<QualityEstimate> Estimate;
    std::string Reason;
};
struct QualityRecommendation {
    std::optional<QualityPreset> Preset;
    bool PerformanceValidated = false; // Initial policy/CPU pressure cannot prove GPU performance.
    std::string Reason;
    std::vector<ProfileAssessment> Profiles;
};
QualityRecommendation RecommendQuality(const QualityCapabilities& caps, const SystemSample& system,
    const Scene& scene, int width, int height, const QualityEstimate* current = nullptr,
    const FrameStatistics* frames = nullptr);
struct TelemetrySnapshot {
    double CapturedAtSeconds = 0;
    bool Live = false;
    HardwareInfo Hardware;
    SystemSample System;
    FrameStatistics Frames;
    QualityState Quality;
    ResourceCost Resident; // Engine logical allocations; excludes GUI, swapchain and deferred deletion.
    std::uint64_t ResidentAssetSignature = 0;
    std::optional<FrameStatistics> PreviousQualityFrames;
    std::string PreviousQualityName;
    std::string CpuScope = "Scene prepare/upload/draw submission; excludes pacing and presentation";
};
enum class TelemetryFormat { Json, Csv };
Result<void> SaveTelemetry(const TelemetrySnapshot& snapshot, const std::filesystem::path& path,
    TelemetryFormat format = TelemetryFormat::Json);
} // namespace lv
