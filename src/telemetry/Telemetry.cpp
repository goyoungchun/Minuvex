#include <minuvex/Telemetry.h>
#include "ResourceSignature.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace lv {
namespace {
void Time(double t) {if (!std::isfinite(t) || t < 0) {throw std::invalid_argument("Telemetry clocks must be finite and nonnegative.");}}
Metric<double> Measured(double v, const char* source) {return {v,MetricStatus::Measured,source};}
double Percentile(std::vector<double> values) {std::sort(values.begin(),values.end());return values[static_cast<std::size_t>(std::ceil(values.size()*.95))-1];}
std::uint64_t Add(std::uint64_t a,std::uint64_t b) {if (b > UINT64_MAX-a) {throw std::invalid_argument("Resource estimate overflow.");}return a+b;}
std::uint64_t Multiply(std::uint64_t a,std::uint64_t b) {if (a && b > UINT64_MAX/a) {throw std::invalid_argument("Resource estimate overflow.");}return a*b;}
ResourceCost Cost(const Scene& scene,const QualityState& quality,std::uint64_t& signature) {
    signature=14695981039346656037ull;
    ResourceCost cost;const auto& settings=*quality.Effective;
    cost.RenderTargetBytes=std::uint64_t(quality.RenderWidth)*quality.RenderHeight*(8u*settings.MsaaSamples+(settings.MsaaSamples > 1 ? 4u:0u));
    std::unordered_set<const ModelAsset*> assets;
    for (const auto& item:scene.CollectRenderItems()) {
        if (!assets.insert(item.Model.get()).second) {continue;}
        detail::HashResourceIdentity(signature,item.Model.get());
        const auto& asset=*item.Model;std::unordered_set<std::size_t> meshes,images;
        for (const auto& part:asset.Parts) {
            if (!meshes.insert(part.MeshIndex).second) {continue;}
            for (const auto& primitive:asset.Meshes.at(part.MeshIndex).Primitives) {
                const auto& geometry=primitive.Geometry;
                cost.BufferBytes=Add(cost.BufferBytes,Add(Multiply(geometry.Vertices.size(),sizeof(Vertex)),Multiply(geometry.Indices.size(),sizeof(std::uint32_t))));
                cost.TextureBytes=Add(cost.TextureBytes,4); // Renderer owns a white fallback per primitive.
                const auto& material=asset.Materials.at(primitive.MaterialIndex);
                if (!material.BaseColorTexture) {continue;}
                const auto imageIndex=asset.Textures.at(*material.BaseColorTexture).ImageIndex;
                if (!images.insert(imageIndex).second) {continue;}
                const auto& image=asset.Images.at(imageIndex);int w=image.Width,h=image.Height;
                if (w <= 0 || h <= 0 || w > 32768 || h > 32768 || Multiply(Multiply(w,h),4) != image.Rgba.size()) {throw std::invalid_argument("Invalid source image in resource estimate.");}
                while (w > settings.MaxTextureSize || h > settings.MaxTextureSize) {w=std::max(1,w/2);h=std::max(1,h/2);}
                for (;;) {cost.TextureBytes=Add(cost.TextureBytes,std::uint64_t(w)*h*4);if (w == 1 && h == 1) {break;}w=std::max(1,w/2);h=std::max(1,h/2);}
            }
        }
    }
    cost.TotalBytes=Add(cost.RenderTargetBytes,Add(cost.TextureBytes,cost.BufferBytes));return cost;
}
}
void FrameMonitor::Record(double now,double cpuMs,std::uint64_t draws,std::uint64_t triangles) {
    Time(now);Time(cpuMs);
    if (!samples_.empty() && now <= samples_.back().Time) {throw std::invalid_argument("Frame timestamps must strictly increase.");}
    if (samples_.size() == 240) {samples_.erase(samples_.begin());}
    samples_.push_back({now,cpuMs,draws,triangles});++total_;
}
FrameStatistics FrameMonitor::Snapshot(double now) const {
    Time(now);FrameStatistics result;result.TotalRenderedFrames=total_;result.SampleCount=samples_.size();
    if (samples_.empty()) {return result;}
    if (now < samples_.back().Time) {throw std::invalid_argument("Snapshot time precedes the latest frame.");}
    const auto& last=samples_.back();result.DrawCalls=last.Draws;result.Triangles=last.Triangles;
    result.CpuFrameMs=Measured(last.Cpu,"CPU scene submission wall clock");
    double total=0;std::vector<double> cpu,periods;cpu.reserve(samples_.size());
    for (std::size_t i=0;i<samples_.size();++i) {const auto& s=samples_[i];total+=s.Cpu;cpu.push_back(s.Cpu);if (i) {periods.push_back((s.Time-samples_[i-1].Time)*1000);}}
    result.AverageCpuMs=Measured(total/samples_.size(),"Rolling mean scene submission CPU ms");result.P95CpuMs=Measured(Percentile(cpu),"Nearest-rank rolling P95 CPU ms");
    if (!periods.empty()) {const auto interval=samples_.back().Time-samples_.front().Time;
        result.AverageFps=Measured((samples_.size()-1)/interval,"Rolling submissions / elapsed wall time");
        result.AverageFrameMs=Measured(interval*1000/(samples_.size()-1),"Submission cadence including pacing and idle");
        result.P95FrameMs=Measured(Percentile(periods),"Nearest-rank rolling P95 submission interval");
        result.Fps=Measured(now-last.Time > 2 ? 0:1000/periods.back(),"Latest submission cadence; zero after 2 seconds idle");
    } else if (now-last.Time > 2) {result.Fps=Measured(0,"No submissions for 2 seconds");}
    return result;
}
void FrameMonitor::Reset() noexcept {samples_.clear();total_=0;}
Result<QualityEstimate> EstimateQualityResources(const QualityProfile& p,const QualityCapabilities& caps,const Scene& scene,int w,int h,const QualityEstimate* current) {
    auto resolved=ResolveQuality(p,caps,w,h);if (!resolved) {return Result<QualityEstimate>::Failure(resolved.Error());}
    try {
        QualityEstimate e;e.Quality=std::move(resolved).Value();e.Cost=Cost(scene,e.Quality,e.AssetSignature);
        if (current && current->Quality.Effective) {
            const auto& old=*current->Quality.Effective;const auto& next=*e.Quality.Effective;
            if (old.MsaaSamples == next.MsaaSamples && current->Quality.RenderWidth == e.Quality.RenderWidth && current->Quality.RenderHeight == e.Quality.RenderHeight) {e.ReusedBytes=std::min(e.Cost.RenderTargetBytes,current->Cost.RenderTargetBytes);}
            if (current->AssetSignature == e.AssetSignature && old.MsaaSamples == next.MsaaSamples && old.MaxTextureSize == next.MaxTextureSize && old.Anisotropy == next.Anisotropy) {e.ReusedBytes=Add(e.ReusedBytes,std::min(e.Cost.TextureBytes,current->Cost.TextureBytes));e.ReusedBytes=Add(e.ReusedBytes,std::min(e.Cost.BufferBytes,current->Cost.BufferBytes));}
        }
        e.AdditionalBytes=e.Cost.TotalBytes-e.ReusedBytes;e.TemporaryPeakBytes=Add(current ? current->Cost.TotalBytes:0,e.AdditionalBytes);
        return Result<QualityEstimate>::Success(std::move(e));
    } catch (const std::bad_alloc& e) {return Result<QualityEstimate>::Failure({ErrorCode::InsufficientResource,e.what(),"Reduce the scene size.",{}});}
    catch (const std::exception& e) {return Result<QualityEstimate>::Failure({ErrorCode::InvalidData,e.what(),"Check scene references and image sizes.",{}});}
}
QualityRecommendation RecommendQuality(const QualityCapabilities& caps,const SystemSample& system,const Scene& scene,int w,int h,const QualityEstimate* current,const FrameStatistics* frames) {
    QualityRecommendation result;
    const bool budget=system.GpuMemoryBudget.Value && system.GpuMemoryUsage.Value &&
        (system.GpuMemoryBudget.Status == MetricStatus::Measured || system.GpuMemoryBudget.Status == MetricStatus::Estimated) && system.GpuMemoryUsage.Status == MetricStatus::Measured;
    const auto headroom=budget && *system.GpuMemoryBudget.Value > *system.GpuMemoryUsage.Value ? *system.GpuMemoryBudget.Value-*system.GpuMemoryUsage.Value:0;
    const bool lowRam=system.AvailableSystemMemory.Value && (system.AvailableSystemMemory.Status == MetricStatus::Measured || system.AvailableSystemMemory.Status == MetricStatus::Estimated) && *system.AvailableSystemMemory.Value < 512ull*1024*1024;
    const bool cpuPressure=current && current->Quality.Effective && frames && frames->SampleCount >= 30 && frames->P95CpuMs.Value && frames->P95CpuMs.Status == MetricStatus::Measured && std::isfinite(*frames->P95CpuMs.Value) && current->Quality.Effective->TargetFps > 0 &&
        *frames->P95CpuMs.Value > 1000.0/current->Quality.Effective->TargetFps;
    const bool gpuPressure=current && current->Quality.Effective && frames && frames->SampleCount >= 30 && frames->GpuFrameMs.Value && frames->GpuFrameMs.Status == MetricStatus::Measured && std::isfinite(*frames->GpuFrameMs.Value) && current->Quality.Effective->TargetFps > 0 && *frames->GpuFrameMs.Value > 1000.0/current->Quality.Effective->TargetFps;
    for (const auto preset:{QualityPreset::Low,QualityPreset::Medium,QualityPreset::High}) {
        ProfileAssessment a;a.Preset=preset;
        auto estimate=EstimateQualityResources(MakeQualityProfile(preset),caps,scene,w,h,current);
        if (!estimate) {a.Reason=estimate.Error().Message;}
        else {
            a.Estimate=std::move(estimate).Value();a.Suitability=ProfileSuitability::Supported;
            const bool memoryRisk=budget && a.Estimate->AdditionalBytes > headroom/2;
            const bool frameRisk=(cpuPressure || gpuPressure) && preset >= current->Quality.Requested.Base;
            if (memoryRisk || frameRisk || (lowRam && preset != QualityPreset::Low)) {a.Suitability=ProfileSuitability::PerformanceRisk;a.Reason=memoryRisk ? "Transient allocations consume more than half the reported memory headroom.":frameRisk ? (cpuPressure ? "Measured CPU P95 exceeds the current target interval; test the lower preset.":"Latest measured GPU scene time exceeds the current target interval; test the lower preset."):"Available system RAM is below 512 MiB.";}
            else {a.Reason="Capabilities supported. Runtime performance testing is still required.";}
            if (a.Suitability == ProfileSuitability::Supported && (budget || preset != QualityPreset::High)) {result.Preset=preset;}
        }
        result.Profiles.push_back(std::move(a));
    }
    result.Reason=result.Preset ? (budget ? "Initial capability and memory-headroom policy; not a benchmark guarantee.":"Memory budget unavailable: conservative supported Medium/Low; runtime testing required."):"No preset fits the supported capabilities and current memory/CPU policy. Review the profile assessments.";
    if (cpuPressure) {result.Reason+=" Current CPU submission pressure was observed; lower quality may help but needs testing.";}
    if (gpuPressure) {result.Reason+=" Latest GPU scene time exceeded the current target; this observation is not a benchmark of other presets.";}
    return result;
}
} // namespace lv
