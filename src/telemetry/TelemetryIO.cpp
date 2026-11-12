#include <minuvex/Telemetry.h>
#include "core/JsonFile.h"
#include <cmath>

namespace lv {
namespace {
using Json=nlohmann::json;
std::string Csv(const std::string& text) {std::string result="\"";for (const char c:text) {if (c == '"') {result+='"';}result+=c;}return result+'"';}
}
Result<void> SaveTelemetry(const TelemetrySnapshot& s,const std::filesystem::path& path,TelemetryFormat format) {
    try {
        if (format != TelemetryFormat::Json && format != TelemetryFormat::Csv) {throw std::invalid_argument("Unknown telemetry export format.");}
        Json metrics=Json::array();
        auto add=[&]<class T>(const char* section,const char* name,const Metric<T>& m,const char* unit) {
            if (m.Status != MetricStatus::Measured && m.Status != MetricStatus::Estimated && m.Status != MetricStatus::Unsupported && m.Status != MetricStatus::Unavailable) {throw std::invalid_argument("Unknown metric status.");}
            if ((m.Status == MetricStatus::Unsupported || m.Status == MetricStatus::Unavailable) && m.Value) {throw std::invalid_argument("Unavailable/unsupported metrics cannot contain a value.");}
            if ((m.Status == MetricStatus::Measured || m.Status == MetricStatus::Estimated) && !m.Value) {throw std::invalid_argument("Measured/estimated metrics require a value.");}
            Json value=nullptr;if (m.Value) {if constexpr(std::is_floating_point_v<T>) {if (!std::isfinite(*m.Value) || *m.Value < 0) {throw std::invalid_argument("Telemetry measurements must be finite and nonnegative.");}}value=*m.Value;}
            metrics.push_back({{"section",section},{"name",name},{"value",value},{"status",MetricStatusName(m.Status)},{"unit",unit},{"source",m.Source}});
        };
        const auto& h=s.Hardware;const auto& m=s.System;const auto& f=s.Frames;
        add("hardware","totalSystemMemory",h.TotalSystemMemory,"bytes");add("hardware","dedicatedVideoMemory",h.DedicatedVideoMemory,"bytes");add("hardware","sharedSystemMemory",h.SharedSystemMemory,"bytes");
        add("system","availableSystemMemory",m.AvailableSystemMemory,"bytes");add("system","processResidentMemory",m.ProcessResidentMemory,"bytes");add("system","processPrivateMemory",m.ProcessPrivateMemory,"bytes");
        add("gpu","memoryUsage",m.GpuMemoryUsage,"bytes");add("gpu","memoryBudget",m.GpuMemoryBudget,"bytes");add("gpu","nonLocalUsage",m.GpuNonLocalUsage,"bytes");add("gpu","nonLocalBudget",m.GpuNonLocalBudget,"bytes");
        add("system","processCpu",m.ProcessCpuPercent,"percent");add("gpu","utilization",m.GpuUtilizationPercent,"percent");
        add("frame","fps",f.Fps,"fps");add("frame","averageFps",f.AverageFps,"fps");add("frame","averageFrame",f.AverageFrameMs,"ms");add("frame","p95Frame",f.P95FrameMs,"ms");
        add("frame","cpuFrame",f.CpuFrameMs,"ms");add("frame","averageCpu",f.AverageCpuMs,"ms");add("frame","p95Cpu",f.P95CpuMs,"ms");add("frame","gpuFrame",f.GpuFrameMs,"ms");
        add("frame","averageGpu",f.AverageGpuMs,"ms");add("frame","p95Gpu",f.P95GpuMs,"ms");
        add("frame","cpuSampleCount",Metric<std::uint64_t>{f.SampleCount,MetricStatus::Measured,"CPU rolling window"},"count");
        add("frame","gpuSampleCount",Metric<std::uint64_t>{f.GpuSampleCount,MetricStatus::Measured,"Unique valid asynchronous GPU results"},"count");
        if (!std::isfinite(f.CapturedAtSeconds) || f.CapturedAtSeconds < 0) {throw std::invalid_argument("Invalid frame snapshot timestamp.");}
        for (const auto t:{f.LastCpuSampleAtSeconds,f.LastGpuSampleAtSeconds}) {if (t && (!std::isfinite(*t) || *t < 0 || *t > f.CapturedAtSeconds)) {throw std::invalid_argument("Invalid frame sample timestamp.");}}
        add("frame","drawCalls",Metric<std::uint64_t>{f.DrawCalls,f.SampleCount ? MetricStatus::Measured:MetricStatus::Estimated,"Latest submitted scene draw count"},"count");
        add("frame","triangles",Metric<std::uint64_t>{f.Triangles,f.SampleCount ? MetricStatus::Measured:MetricStatus::Estimated,"Latest submitted indexed triangles"},"count");
        const char* logical="Engine logical allocation estimate; excludes GUI/swapchain/driver overhead and deferred deletion";
        add("resident","renderTargets",Metric<std::uint64_t>{s.Resident.RenderTargetBytes,MetricStatus::Estimated,logical},"bytes");
        add("resident","textures",Metric<std::uint64_t>{s.Resident.TextureBytes,MetricStatus::Estimated,logical},"bytes");
        add("resident","buffers",Metric<std::uint64_t>{s.Resident.BufferBytes,MetricStatus::Estimated,logical},"bytes");
        add("resident","total",Metric<std::uint64_t>{s.Resident.TotalBytes,MetricStatus::Estimated,logical},"bytes");
        if (!std::isfinite(m.TimestampSeconds) || m.TimestampSeconds < 0) {throw std::invalid_argument("Invalid sample timestamp.");}
        if (!std::isfinite(s.CapturedAtSeconds) || s.CapturedAtSeconds < 0) {throw std::invalid_argument("Invalid snapshot timestamp.");}
        const Json hardware{{"cpu",h.CpuName},{"gpu",h.GpuName},{"os",h.OperatingSystem},{"osVersion",h.OsVersion},{"architecture",h.Architecture},{"logicalProcessors",h.LogicalProcessors},{"physicalCores",h.PhysicalCores},{"activeGraphicsDevice",h.ActiveGraphicsDevice},{"backend",static_cast<int>(h.Backend)},{"memoryKind",static_cast<int>(h.MemoryKind)}};
        Json root{{"format","MinuvexTelemetry"},{"version",1},{"timestampSeconds",m.TimestampSeconds},{"hardware",hardware},{"cpuScope",s.CpuScope},{"quality",{{"name",s.Quality.Requested.Name},{"basePreset",static_cast<int>(s.Quality.Requested.Base)},{"applied",s.Quality.Applied},{"width",s.Quality.RenderWidth},{"height",s.Quality.RenderHeight}}},{"sampleCount",f.SampleCount},{"totalRenderedFrames",f.TotalRenderedFrames},{"metrics",metrics}};
        root["capturedAtSeconds"]=s.CapturedAtSeconds;root["live"]=s.Live;
        root["frameWindow"]={{"capturedAtSeconds",f.CapturedAtSeconds},{"gpuSampleCount",f.GpuSampleCount},{"lastCpuSampleAtSeconds",f.LastCpuSampleAtSeconds ? Json(*f.LastCpuSampleAtSeconds):Json(nullptr)},{"lastGpuSampleAtSeconds",f.LastGpuSampleAtSeconds ? Json(*f.LastGpuSampleAtSeconds):Json(nullptr)}};
        root["hardware"]["gpuVendor"]=h.GpuVendor;root["hardware"]["gpuVendorId"]=h.GpuVendorId;root["hardware"]["gpuDeviceId"]=h.GpuDeviceId;
        root["hardware"]["capabilities"]={{"graphicsAvailable",h.Capabilities.GraphicsAvailable},{"sampleCounts",h.Capabilities.SampleCounts},{"maxRenderSize",h.Capabilities.MaxRenderSize},{"maxTextureSize",h.Capabilities.MaxTextureSize},{"maxAnisotropy",h.Capabilities.MaxAnisotropy}};
        root["hardware"]["capabilities"]["shadowMapping"]=h.Capabilities.ShadowMapping;root["hardware"]["capabilities"]["hdrPostProcessing"]=h.Capabilities.HdrPostProcessing;root["hardware"]["capabilities"]["hdrSampleCounts"]=h.Capabilities.HdrSampleCounts;
        root["hardware"]["formats"]=Json::array();for (const auto& formatInfo:h.Formats) {root["hardware"]["formats"].push_back({{"name",formatInfo.Name},{"sampled",formatInfo.Sampled},{"filtered",formatInfo.Filtered},{"renderTarget",formatInfo.RenderTarget},{"msaa",formatInfo.Msaa}});}
        root["quality"]["effective"]=nullptr;
        const auto settings=[](const RenderSettings& e) {if (!std::isfinite(e.ResolutionScale) || !std::isfinite(e.LodBias)) {throw std::invalid_argument("Quality telemetry settings must be finite.");}return Json{{"resolution",static_cast<int>(e.Resolution)},{"resolutionScale",e.ResolutionScale},{"fixedWidth",e.FixedWidth},{"fixedHeight",e.FixedHeight},{"maxRenderHeight",e.MaxRenderHeight},{"msaaSamples",e.MsaaSamples},{"textures",static_cast<int>(e.Textures)},{"maxTextureSize",e.MaxTextureSize},{"anisotropy",e.Anisotropy},{"targetFps",e.TargetFps},{"onDemand",static_cast<int>(e.OnDemand)},{"lighting",static_cast<int>(e.Lighting)},{"frustumCulling",e.FrustumCulling},{"shadow",static_cast<int>(e.Shadow)},{"shadowResolution",e.ShadowResolution},{"lod",e.Lod},{"lodBias",e.LodBias},{"postProcessing",e.PostProcessing}};};
        root["quality"]["requested"]=settings(s.Quality.RequestedSettings);
        if (s.Quality.Effective) {root["quality"]["effective"]=settings(*s.Quality.Effective);}
        root["quality"]["custom"]=s.Quality.Requested.IsCustom();root["quality"]["allowFallback"]=s.Quality.Requested.AllowFallback;
        root["quality"]["adjustments"]=Json::array();for (const auto& a:s.Quality.Adjustments) {root["quality"]["adjustments"].push_back({{"field",a.Field},{"reason",a.Reason}});}
        const auto validated=root.dump(2); // Validate UTF-8 before any destination modification.
        if (format == TelemetryFormat::Json) {detail::WriteTextFile(validated+'\n',path,1024*1024);}
        else {
            std::string csv="section,name,value,status,unit,source\r\n";
            for (const auto& [key,value]:root["hardware"].items()) {csv+="hardware,"+Csv(key)+","+Csv(value.is_string() ? value.get<std::string>():value.dump())+",,,\r\n";}
            csv+="quality,name,"+Csv(s.Quality.Requested.Name)+",,,\r\n";
            for (const auto& [key,value]:root["quality"].items()) {if (key != "name" && key != "effective") {csv+="quality,"+Csv(key)+","+Csv(value.dump())+",,,\r\n";}}
            if (!root["quality"]["effective"].is_null()) {for (const auto& [key,value]:root["quality"]["effective"].items()) {csv+="effective,"+Csv(key)+","+Csv(value.dump())+",,,\r\n";}}
            csv+="snapshot,capturedAtSeconds,"+root["capturedAtSeconds"].dump()+",,seconds,steady clock\r\n";
            csv+="snapshot,live,"+root["live"].dump()+",,,\r\n";
            csv+="snapshot,cpuScope,"+Csv(s.CpuScope)+",,,\r\n";
            for (const auto& [key,value]:root["frameWindow"].items()) {csv+="frameWindow,"+Csv(key)+","+Csv(value.dump())+",,,\r\n";}
            for (const auto& row:metrics) {csv+=Csv(row["section"].get<std::string>())+","+Csv(row["name"].get<std::string>())+","+Csv(row["value"].is_null() ? "":row["value"].dump())+","+Csv(row["status"].get<std::string>())+","+Csv(row["unit"].get<std::string>())+","+Csv(row["source"].get<std::string>())+"\r\n";}
            detail::WriteTextFile(csv,path,1024*1024);
        }
        return Result<void>::Success();
    } catch (const std::bad_alloc& e) {return Result<void>::Failure({ErrorCode::InsufficientResource,e.what(),"Reduce telemetry metadata size.",path});}
    catch (const std::invalid_argument& e) {return Result<void>::Failure({ErrorCode::InvalidArgument,e.what(),"Correct telemetry values before exporting.",path});}
    catch (const std::exception& e) {return Result<void>::Failure({ErrorCode::TelemetryIo,e.what(),"Check the export path and storage permissions.",path});}
}
} // namespace lv
