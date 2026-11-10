#include "EditorUI.h"
#include <imgui.h>

namespace lv::editor {
namespace {
const char* Preset(QualityPreset p) {return p == QualityPreset::Low ? "Low":p == QualityPreset::High ? "High":"Medium";}
const char* Suitability(ProfileSuitability s) {return s == ProfileSuitability::Supported ? "Supported":s == ProfileSuitability::PerformanceRisk ? "Performance risk":"Unsupported";}
template<class T> void MetricRow(const char* label,const Metric<T>& m,const char* unit,double divisor=1) {
    if (m.Value) {ImGui::Text("%s: %.2f %s [%s]",label,static_cast<double>(*m.Value)/divisor,unit,MetricStatusName(m.Status));}
    else {ImGui::Text("%s: -- [%s]",label,MetricStatusName(m.Status));}
    if (ImGui::IsItemHovered() && !m.Source.empty()) {ImGui::SetTooltip("%s",m.Source.c_str());}
}
void CostRows(const ResourceCost& c) {
    constexpr double mib=1024*1024;
    ImGui::Text("Targets: %.2f MiB | textures: %.2f MiB",c.RenderTargetBytes/mib,c.TextureBytes/mib);
    ImGui::Text("Buffers: %.2f MiB | total: %.2f MiB",c.BufferBytes/mib,c.TotalBytes/mib);
}
}
void EditorUI::PerformancePanel() {
    if (!performance_) {return;}
    if (ImGui::Begin("Performance",&performance_)) {
        const auto now=detail::RenderClockSeconds();const auto snapshot=Viewport.Telemetry();const auto& h=snapshot.Hardware;const auto& sys=snapshot.System;const auto& f=snapshot.Frames;
        const auto& caps=Viewport.Quality().Capabilities;
        if (caps.GraphicsAvailable && (lastAnalysis_ < 0 || now-lastAnalysis_ >= 1 || analyzedProfile_ != qualityDraft_)) {
            lastAnalysis_=now;analyzedProfile_=qualityDraft_;selectedEstimate_.reset();estimateError_.clear();
            QualityEstimate current;current.Quality=snapshot.Quality;current.Cost=snapshot.Resident;current.AssetSignature=snapshot.ResidentAssetSignature;
            const auto estimate=EstimateQualityResources(qualityDraft_,caps,Document.GetScene(),Viewport.OutputWidth(),Viewport.OutputHeight(),&current);
            if (estimate) {selectedEstimate_=estimate.Value();}else {estimateError_=estimate.Error().Message;}
            recommendation_=RecommendQuality(caps,sys,Document.GetScene(),Viewport.OutputWidth(),Viewport.OutputHeight(),&current,&f);
        }
        if (ImGui::CollapsingHeader("Current device",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("CPU: %s",h.CpuName.empty() ? "Unavailable":h.CpuName.c_str());
            ImGui::Text("Cores: %u physical / %u logical",h.PhysicalCores,h.LogicalProcessors);
            ImGui::TextWrapped("GPU: %s",h.GpuName.empty() ? "Unavailable":h.GpuName.c_str());
            ImGui::Text("%s %s | %s | %s",h.OperatingSystem.c_str(),h.OsVersion.c_str(),h.Architecture.c_str(),h.Backend == GraphicsBackend::D3D11 ? "D3D11":h.Backend == GraphicsBackend::Metal ? "Metal":"No backend");
            ImGui::TextUnformatted(h.MemoryKind == GpuMemoryKind::Unified ? "Unified CPU/GPU memory":h.MemoryKind == GpuMemoryKind::Dedicated ? "Dedicated capacity reported (shared capacity may also exist)":"Shared / unknown GPU memory");
            MetricRow("System RAM",h.TotalSystemMemory,"MiB",1048576);MetricRow("Dedicated capacity",h.DedicatedVideoMemory,"MiB",1048576);
            if (ImGui::TreeNode("Graphics limits and formats")) {ImGui::Text("Render/texture limit: %d / %d | anisotropy: %d",h.Capabilities.MaxRenderSize,h.Capabilities.MaxTextureSize,h.Capabilities.MaxAnisotropy);for (const auto& format:h.Formats) {ImGui::Text("%s: sample %d, filter %d, target %d, MSAA %d",format.Name.c_str(),format.Sampled,format.Filtered,format.RenderTarget,format.Msaa);}ImGui::TreePop();}
        }
        if (ImGui::CollapsingHeader("Current usage and frames",ImGuiTreeNodeFlags_DefaultOpen)) {
            MetricRow("Rendered FPS",f.Fps,"fps");MetricRow("Rolling FPS",f.AverageFps,"fps");
            MetricRow("Frame cadence",f.AverageFrameMs,"ms");MetricRow("P95 cadence",f.P95FrameMs,"ms");
            MetricRow("CPU scene",f.CpuFrameMs,"ms");MetricRow("CPU average",f.AverageCpuMs,"ms");MetricRow("CPU P95",f.P95CpuMs,"ms");MetricRow("GPU scene",f.GpuFrameMs,"ms");
            ImGui::TextWrapped("CPU/GPU scene submission excludes GUI and presentation. Cadence includes pacing and On-Demand idle; this is not maximum GPU throughput.");
            ImGui::Text("Draw calls: %llu | triangles: %llu",static_cast<unsigned long long>(f.DrawCalls),static_cast<unsigned long long>(f.Triangles));
            MetricRow("Process CPU (all cores)",sys.ProcessCpuPercent,"%");MetricRow("Available RAM",sys.AvailableSystemMemory,"MiB",1048576);
            MetricRow("Process resident",sys.ProcessResidentMemory,"MiB",1048576);MetricRow("Private commit",sys.ProcessPrivateMemory,"MiB",1048576);
            MetricRow("GPU allocation/usage",sys.GpuMemoryUsage,"MiB",1048576);MetricRow("GPU budget / working set",sys.GpuMemoryBudget,"MiB",1048576);
            MetricRow("Non-local usage",sys.GpuNonLocalUsage,"MiB",1048576);MetricRow("Non-local budget",sys.GpuNonLocalBudget,"MiB",1048576);MetricRow("GPU utilization",sys.GpuUtilizationPercent,"%");
            ImGui::TextUnformatted("Engine resident logical sizes [Estimated]");CostRows(snapshot.Resident);
            if (snapshot.PreviousQualityFrames) {ImGui::TextWrapped("Before: %s",snapshot.PreviousQualityName.c_str());MetricRow("Before CPU average",snapshot.PreviousQualityFrames->AverageCpuMs,"ms");MetricRow("Before rolling FPS",snapshot.PreviousQualityFrames->AverageFps,"fps");}
        }
        if (ImGui::CollapsingHeader("Selected quality cost and recommendation",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Selected: %s | Applied: %s",qualityDraft_.Name.c_str(),snapshot.Quality.Requested.Name.c_str());
            if (selectedEstimate_) {
                const auto& e=*selectedEstimate_;CostRows(e.Cost);
                ImGui::Text("Reuse: %.2f MiB | new: %.2f MiB",e.ReusedBytes/1048576.0,e.AdditionalBytes/1048576.0);
                ImGui::Text("Transient peak: %.2f MiB [Estimated]",e.TemporaryPeakBytes/1048576.0);
                for (const auto& a:e.Quality.Adjustments) {ImGui::TextWrapped("Fallback %s: %s",a.Field.c_str(),a.Reason.c_str());}
            } else {ImGui::TextWrapped("%s",estimateError_.c_str());}
            ImGui::TextWrapped("Logical RGBA8/depth-4B + mip/buffer estimates exclude driver overhead, GUI, swapchain and deferred deletion. OS memory can include all of these.");
            for (const auto& p:recommendation_.Profiles) {ImGui::Text("%s: %s",Preset(p.Preset),Suitability(p.Suitability));if (ImGui::IsItemHovered()) {ImGui::SetTooltip("%s",p.Reason.c_str());}}
            ImGui::TextWrapped("%s",recommendation_.Reason.c_str());
            if (recommendation_.Preset) {ImGui::Text("Initial recommendation: %s (requires runtime testing)",Preset(*recommendation_.Preset));if (ImGui::Button("Apply recommended preset")) {RequestQualityProfile(MakeQualityProfile(*recommendation_.Preset));}}
        }
        if (ImGui::Button("Export JSON...")) {FileDialog(Dialog::TelemetryJson);}ImGui::SameLine();if (ImGui::Button("Export CSV...")) {FileDialog(Dialog::TelemetryCsv);}
        ImGui::TextWrapped("System/GPU memory sampled once per second. Hover metrics for source and scope.");
    }ImGui::End();
}
}
