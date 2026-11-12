#include <minuvex/Telemetry.h>
#include <minuvex/Optimization.h>
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
    scene.ValidateLighting();
    signature=14695981039346656037ull;
    ResourceCost cost;const auto& settings=*quality.Effective;
    cost.RenderTargetBytes=RenderTargetBytes(settings,quality.RenderWidth,quality.RenderHeight);
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
                if (settings.Lod) {for (const auto& lod:primitive.Lods) {cost.BufferBytes=Add(cost.BufferBytes,Multiply(lod.Indices.size(),sizeof(std::uint32_t)));}}
                cost.TextureBytes=Add(cost.TextureBytes,4); // Renderer owns a white fallback per primitive.
                const auto& material=asset.Materials.at(primitive.MaterialIndex);
                for (int role=0;role<2;++role) {
                const auto texture=role == 0 ? material.BaseColorTexture:material.NormalTexture;
                if (!texture || (role == 1 && settings.Lighting != LightingMode::Advanced)) {continue;}
                const auto imageIndex=asset.Textures.at(*texture).ImageIndex;
                if (!images.insert(imageIndex+role*asset.Images.size()).second) {continue;}
                const auto& image=asset.Images.at(imageIndex);int w=image.Width,h=image.Height;
                if (w <= 0 || h <= 0 || w > 32768 || h > 32768 || Multiply(Multiply(w,h),4) != image.Rgba.size()) {throw std::invalid_argument("Invalid source image in resource estimate.");}
                while (w > settings.MaxTextureSize || h > settings.MaxTextureSize) {w=std::max(1,w/2);h=std::max(1,h/2);}
                cost.TextureBytes=Add(cost.TextureBytes,TextureMipBytes(w,h,settings.Compression == TextureCompression::Bc3 && w>=4 && h>=4 && w%4==0 && h%4==0));
                }
            }
        }
    }
    cost.TotalBytes=Add(cost.RenderTargetBytes,Add(cost.TextureBytes,cost.BufferBytes));return cost;
}
}
void FrameMonitor::Record(double now,double cpuMs,std::uint64_t draws,std::uint64_t triangles) {
    Time(now);Time(cpuMs);
    if (!samples_.empty() && now <= samples_.back().Time) {throw std::invalid_argument("Frame timestamps must strictly increase.");}
    if (!samples_.empty() && now-samples_.back().Time > 2) {samples_.clear();}
    if (samples_.size() == 240) {samples_.erase(samples_.begin());}
    samples_.push_back({now,cpuMs,draws,triangles});++total_;
}
void FrameMonitor::RecordGpu(double now,double ms) {
    Time(now);Time(ms);
    if (!gpuSamples_.empty() && now <= gpuSamples_.back().Time) {throw std::invalid_argument("GPU submission timestamps must strictly increase.");}
    if (!gpuSamples_.empty() && now-gpuSamples_.back().Time > 2) {gpuSamples_.clear();}
    if (gpuSamples_.size() == 240) {gpuSamples_.erase(gpuSamples_.begin());}
    gpuSamples_.push_back({now,ms});
}
FrameStatistics FrameMonitor::Snapshot(double now) const {
    Time(now);FrameStatistics result;result.CapturedAtSeconds=now;result.TotalRenderedFrames=total_;result.SampleCount=samples_.size();
    if (!gpuSamples_.empty()) {
        if (now < gpuSamples_.back().Time) {throw std::invalid_argument("Snapshot time precedes the latest GPU submission.");}
        result.GpuSampleCount=gpuSamples_.size();result.LastGpuSampleAtSeconds=gpuSamples_.back().Time;
        double total=0;std::vector<double> values;values.reserve(gpuSamples_.size());
        for (const auto& sample:gpuSamples_) {total+=sample.Ms;values.push_back(sample.Ms);}
        result.GpuFrameMs=Measured(gpuSamples_.back().Ms,"Latest asynchronous scene GPU timestamp");
        result.AverageGpuMs=Measured(total/gpuSamples_.size(),"Rolling mean asynchronous scene GPU ms");
        result.P95GpuMs=Measured(Percentile(values),"Nearest-rank rolling P95 asynchronous scene GPU ms");
    }
    if (samples_.empty()) {return result;}
    if (now < samples_.back().Time) {throw std::invalid_argument("Snapshot time precedes the latest frame.");}
    const auto& last=samples_.back();result.LastCpuSampleAtSeconds=last.Time;result.DrawCalls=last.Draws;result.Triangles=last.Triangles;
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
void FrameMonitor::Reset() noexcept {samples_.clear();gpuSamples_.clear();total_=0;}
Result<QualityEstimate> EstimateQualityResources(const QualityProfile& p,const QualityCapabilities& caps,const Scene& scene,int w,int h,const QualityEstimate* current) {
    auto resolved=ResolveQuality(p,caps,w,h);if (!resolved) {return Result<QualityEstimate>::Failure(resolved.Error());}
    try {
        QualityEstimate e;e.Quality=std::move(resolved).Value();e.Cost=Cost(scene,e.Quality,e.AssetSignature);
        if (current && current->Quality.Effective) {
            const auto& old=*current->Quality.Effective;const auto& next=*e.Quality.Effective;
            const auto& cost = current->Cost;
            const bool suspended = current->Quality.RenderWidth == 0 && current->Quality.RenderHeight == 0;
            const auto oldTargets = suspended ? 0 : RenderTargetBytes(old,current->Quality.RenderWidth,current->Quality.RenderHeight);
            if (cost.RenderTargetBytes != oldTargets || cost.TotalBytes != Add(cost.RenderTargetBytes,Add(cost.TextureBytes,cost.BufferBytes))) {
                throw std::invalid_argument("Current resource cost does not match its extent/settings or total.");
            }
            const auto shadowBytes=[](const RenderSettings& s) {return s.Shadow != ShadowMode::Off ? std::uint64_t(s.ShadowResolution)*s.ShadowResolution*8:0;};
            if (!suspended && old.Shadow != ShadowMode::Off && next.Shadow != ShadowMode::Off && old.ShadowResolution == next.ShadowResolution) {e.ReusedBytes=shadowBytes(next);}
            if (!suspended && old.MsaaSamples == next.MsaaSamples && old.PostProcessing == next.PostProcessing && current->Quality.RenderWidth == e.Quality.RenderWidth && current->Quality.RenderHeight == e.Quality.RenderHeight) {e.ReusedBytes=Add(e.ReusedBytes,std::min(e.Cost.RenderTargetBytes-shadowBytes(next),current->Cost.RenderTargetBytes-shadowBytes(old)));}
            if (current->AssetSignature == e.AssetSignature && old.MsaaSamples == next.MsaaSamples && old.MaxTextureSize == next.MaxTextureSize && old.Anisotropy == next.Anisotropy && old.PostProcessing == next.PostProcessing && old.Lod == next.Lod && old.Compression == next.Compression && (old.Shadow != ShadowMode::Off) == (next.Shadow != ShadowMode::Off) && (old.Lighting == LightingMode::Advanced) == (next.Lighting == LightingMode::Advanced)) {e.ReusedBytes=Add(e.ReusedBytes,std::min(e.Cost.TextureBytes,current->Cost.TextureBytes));e.ReusedBytes=Add(e.ReusedBytes,std::min(e.Cost.BufferBytes,current->Cost.BufferBytes));}
        }
        e.AdditionalBytes=e.Cost.TotalBytes-e.ReusedBytes;e.TemporaryPeakBytes=Add(current ? current->Cost.TotalBytes:0,e.AdditionalBytes);
        return Result<QualityEstimate>::Success(std::move(e));
    } catch (const std::bad_alloc& e) {return Result<QualityEstimate>::Failure({ErrorCode::InsufficientResource,e.what(),"Reduce the scene size.",{}});}
    catch (const std::exception& e) {return Result<QualityEstimate>::Failure({ErrorCode::InvalidData,e.what(),"Check scene references, image sizes and the current resource-cost snapshot.",{}});}
}
QualityRecommendation RecommendQuality(const QualityCapabilities& caps,const SystemSample& system,const Scene& scene,int w,int h,const QualityEstimate* current,const FrameStatistics* frames) {
    QualityRecommendation result;
    const bool budget=system.GpuMemoryBudget.Value && system.GpuMemoryUsage.Value &&
        (system.GpuMemoryBudget.Status == MetricStatus::Measured || system.GpuMemoryBudget.Status == MetricStatus::Estimated) && system.GpuMemoryUsage.Status == MetricStatus::Measured;
    const auto headroom=budget && *system.GpuMemoryBudget.Value > *system.GpuMemoryUsage.Value ? *system.GpuMemoryBudget.Value-*system.GpuMemoryUsage.Value:0;
    const bool lowRam=system.AvailableSystemMemory.Value && (system.AvailableSystemMemory.Status == MetricStatus::Measured || system.AvailableSystemMemory.Status == MetricStatus::Estimated) && *system.AvailableSystemMemory.Value < 512ull*1024*1024;
    const auto recent=[&](std::optional<double> time) {return frames && time && std::isfinite(*time) && *time >= 0 && std::isfinite(frames->CapturedAtSeconds) && frames->CapturedAtSeconds >= *time && frames->CapturedAtSeconds-*time <= 2;};
    const auto measured=[](const Metric<double>& m) {return m.Status == MetricStatus::Measured && m.Value && std::isfinite(*m.Value) && *m.Value >= 0;};
    const bool cpuEvidence=frames && !frames->DebugRendering && frames->SampleCount >= 30 && recent(frames->LastCpuSampleAtSeconds) && measured(frames->P95CpuMs);
    const bool gpuEvidence=frames && !frames->DebugRendering && frames->GpuSampleCount >= 30 && recent(frames->LastGpuSampleAtSeconds) && measured(frames->P95GpuMs);
    const double target=current && current->Quality.Effective && current->Quality.Effective->TargetFps > 0 ? 1000.0/current->Quality.Effective->TargetFps:0;
    const bool cpuPressure=target > 0 && cpuEvidence && *frames->P95CpuMs.Value > target;
    const bool gpuPressure=target > 0 && gpuEvidence && *frames->P95GpuMs.Value > target;
    std::optional<QualityPreset> observedPreset;
    for (const auto preset:{QualityPreset::Low,QualityPreset::Medium,QualityPreset::High}) {
        ProfileAssessment a;a.Preset=preset;
        auto estimate=EstimateQualityResources(MakeQualityProfile(preset),caps,scene,w,h,current);
        if (!estimate) {a.Reason=estimate.Error().Message;}
        else {
            a.Estimate=std::move(estimate).Value();a.Suitability=ProfileSuitability::Supported;
            const bool memoryRisk=budget && a.Estimate->AdditionalBytes > headroom/2;
            const bool frameRisk=(cpuPressure || gpuPressure) && preset >= current->Quality.Requested.Base;
            if (memoryRisk || frameRisk || (lowRam && preset != QualityPreset::Low)) {a.Suitability=ProfileSuitability::PerformanceRisk;a.Reason=memoryRisk ? "Transient allocations consume more than half the reported memory headroom.":frameRisk ? (cpuPressure ? "Measured CPU P95 exceeds the current target interval; test the lower preset.":"Measured GPU P95 exceeds the current target interval; test the lower preset."):"Available system RAM is below 512 MiB.";}
            else {a.Reason="Capabilities supported. Runtime performance testing is still required.";}
            if (a.Suitability == ProfileSuitability::Supported && target > 0 && cpuEvidence && gpuEvidence && !cpuPressure && !gpuPressure &&
                *current->Quality.Effective == *a.Estimate->Quality.Effective && current->Quality.Applied &&
                current->Quality.RenderWidth == a.Estimate->Quality.RenderWidth && current->Quality.RenderHeight == a.Estimate->Quality.RenderHeight && current->AssetSignature == a.Estimate->AssetSignature) {
                a.PerformanceValidated=true;observedPreset=preset;
                a.Reason="Recent CPU/GPU scene P95 fits this exact effective preset's target. GUI/presentation and future workloads still need validation.";
            }
            if (a.Suitability == ProfileSuitability::Supported && (budget || preset != QualityPreset::High)) {result.Preset=preset;}
        }
        result.Profiles.push_back(std::move(a));
    }
    if (observedPreset) {result.Preset=observedPreset;result.PerformanceValidated=true;}
    result.Reason=result.PerformanceValidated ? "Retain the preset observed within the CPU/GPU scene target in the recent window; this is not an end-to-end FPS guarantee.":result.Preset ? (budget ? "Initial capability and memory-headroom policy; runtime validation required.":"Memory budget unavailable: conservative supported Medium/Low; runtime testing required."):"No preset fits the supported capabilities and current memory/frame policy. Review the profile assessments.";
    if (cpuPressure) {result.Reason+=" Current CPU submission pressure was observed; lower quality may help but needs testing.";}
    if (gpuPressure) {result.Reason+=" Recent GPU P95 exceeded the current target; this observation is not a benchmark of other presets.";}
    return result;
}
} // namespace lv
