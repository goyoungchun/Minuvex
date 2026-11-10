#include "TestHarness.h"
#include <minuvex/Telemetry.h>
#include <minuvex/Viewer.h>
#include <json.hpp>
#include <limits>
#include <thread>

using namespace test;
namespace {
lv::QualityCapabilities Caps() {lv::QualityCapabilities c;c.GraphicsAvailable=true;c.SampleCounts=(1u<<1)|(1u<<2)|(1u<<4);return c;}
void Frames() {
    lv::FrameMonitor m;Check(!m.Snapshot(0).Fps.Value && m.Snapshot(0).SampleCount == 0,"no fabricated FPS before a frame");
    m.Record(1,2,5,12);Check(!m.Snapshot(1).Fps.Value && *m.Snapshot(1).CpuFrameMs.Value == 2,"first frame exposes CPU work without invented cadence");
    m.Record(1.02,4,6,24);m.Record(1.04,6,7,36);auto s=m.Snapshot(1.04);
    Check(std::abs(*s.AverageFps.Value-50) < .001 && std::abs(*s.AverageFrameMs.Value-20) < .001,"FPS uses elapsed submission cadence");
    Check(*s.AverageCpuMs.Value == 4 && *s.P95CpuMs.Value == 6 && s.DrawCalls == 7 && s.Triangles == 36,"CPU mean, P95 and latest rendered counters");
    Check(*m.Snapshot(4).Fps.Value == 0 && *m.Snapshot(4).AverageFps.Value > 0,"idle live FPS drops to zero while history remains");
    Rejects([&]{m.Record(1.03,1,0,0);},"backwards frame time rejected");Rejects([&]{m.Record(1.04,1,0,0);},"duplicate frame time rejected");
    Rejects([&]{m.Record(2,-1,0,0);},"negative CPU time rejected");Rejects([&]{m.Snapshot(std::numeric_limits<double>::quiet_NaN());},"nonfinite sample clock rejected");
    for (int i=0;i<300;++i) {m.Record(2+i*.01,1,0,0);}s=m.Snapshot(5);
    Check(s.SampleCount == 240 && s.TotalRenderedFrames == 303,"bounded rolling history retains session total");m.Reset();Check(m.Snapshot(0).SampleCount == 0 && m.Snapshot(0).TotalRenderedFrames == 0,"quality history reset separates measurement windows");
}
void Resources() {
    const auto caps=Caps();lv::Scene scene;auto p=lv::MakeQualityProfile(lv::QualityPreset::Medium);auto e=lv::EstimateQualityResources(p,caps,scene,100,50);
    Check(e && e.Value().Cost.RenderTargetBytes == 100*50*8 && e.Value().Cost.TotalBytes == e.Value().Cost.RenderTargetBytes,"empty scene accounts color/depth attachments");
    lv::AssetRegistry assets;const auto model=assets.LoadModel(Fixture("SharedBox.glb"));const auto first=scene.AddModel(model);
    auto one=lv::EstimateQualityResources(p,caps,scene,100,50);scene.AddModel(model);auto shared=lv::EstimateQualityResources(p,caps,scene,100,50);
    Check(one && shared && one.Value().Cost.TotalBytes == shared.Value().Cost.TotalBytes,"instances share logical GPU asset allocations");
    std::uint64_t expected=0;for (const auto& mesh:model->Meshes) {for (const auto& primitive:mesh.Primitives) {expected+=primitive.Geometry.Vertices.size()*sizeof(lv::Vertex)+primitive.Geometry.Indices.size()*sizeof(std::uint32_t);}}
    Check(one.Value().Cost.BufferBytes == expected && expected > 0,"vertex and uint32 index buffers included");
    const auto reuse=lv::EstimateQualityResources(p,caps,scene,100,50,&shared.Value());Check(reuse && reuse.Value().AdditionalBytes == 0 && reuse.Value().ReusedBytes == shared.Value().Cost.TotalBytes,"identical quality reuses targets and GPU cache");
    p.Overrides.TargetFps=30;const auto fps=lv::EstimateQualityResources(p,caps,scene,100,50,&shared.Value());Check(fps && fps.Value().AdditionalBytes == 0,"pacing-only changes have no new GPU allocation");
    auto resized=lv::EstimateQualityResources(p,caps,scene,200,100,&shared.Value());Check(resized && resized.Value().AdditionalBytes == resized.Value().Cost.RenderTargetBytes && resized.Value().TemporaryPeakBytes == shared.Value().Cost.TotalBytes+resized.Value().AdditionalBytes,"resize accounts transient old-plus-new attachments");
    p.Overrides.MsaaSamples=4;const auto msaa=lv::EstimateQualityResources(p,caps,scene,100,50,&shared.Value());Check(msaa && msaa.Value().Cost.RenderTargetBytes == 100*50*36 && msaa.Value().ReusedBytes == 0,"MSAA includes resolve and pipeline/cache recreation");
    scene.SetVisible(first,false);scene.SetVisible(scene.ObjectIds()[1],false);const auto hidden=lv::EstimateQualityResources(p,caps,scene,100,50);Check(hidden && hidden.Value().Cost.BufferBytes == 0 && hidden.Value().Cost.TextureBytes == 0,"hidden assets are not resident in renderer cache");
    auto imageModel=std::make_shared<lv::ModelAsset>(*model);imageModel->Images={{1024,512,std::vector<std::uint8_t>(1024*512*4,255)}};imageModel->Textures={lv::TextureAsset{}};for (auto& mat:imageModel->Materials) {mat.BaseColorTexture=0;}
    lv::Scene imageScene;imageScene.AddModel(imageModel);auto low=lv::EstimateQualityResources(lv::MakeQualityProfile(lv::QualityPreset::Low),caps,imageScene,100,50);auto high=lv::EstimateQualityResources(lv::MakeQualityProfile(lv::QualityPreset::High),caps,imageScene,100,50);
    std::uint64_t lowMip=0;for (int w=512,h=256;;w=std::max(1,w/2),h=std::max(1,h/2)) {lowMip+=std::uint64_t(w)*h*4;if (w == 1 && h == 1) {break;}}
    Check(low && high && low.Value().Cost.TextureBytes == lowMip+model->Meshes[0].Primitives.size()*4 && high.Value().Cost.TextureBytes > low.Value().Cost.TextureBytes,"texture reduction and complete NPOT mip chain with per-primitive fallback");
    Check(imageModel->Images[0].Width == 1024 && imageModel->Images[0].Rgba.size() == 1024*512*4,"estimation does not modify or resample CPU assets");
    auto replacement=std::make_shared<lv::ModelAsset>(*imageModel);lv::Scene replacedScene;replacedScene.AddModel(replacement);
    auto replaced=lv::EstimateQualityResources(lv::MakeQualityProfile(lv::QualityPreset::Low),caps,replacedScene,100,50,&low.Value());
    Check(replaced && replaced.Value().AssetSignature != low.Value().AssetSignature && replaced.Value().AdditionalBytes == replaced.Value().Cost.BufferBytes+replaced.Value().Cost.TextureBytes,"different immutable assets do not reuse a same-sized resident cache");
    p.Overrides.Shadow=lv::ShadowMode::Pcf;const auto unsupported=lv::EstimateQualityResources(p,caps,scene,100,50);Check(!unsupported && unsupported.Error().Code == lv::ErrorCode::UnsupportedFeature,"estimate applies the same strict capability policy as rendering");
    Check(!lv::EstimateQualityResources(p,caps,scene,0,0),"suspended extent does not invent cost");
    imageModel->Images[0].Width=0;Check(!lv::EstimateQualityResources(lv::QualityProfile{},caps,imageScene,100,50),"invalid image data rejected without large allocation");
}
void Recommendations() {
    lv::Scene scene;auto caps=Caps();lv::SystemSample system;
    auto r=lv::RecommendQuality(caps,system,scene,1280,720);Check(r.Preset == lv::QualityPreset::Medium && !r.PerformanceValidated,"missing budget yields conservative initial Medium, never proven performance");
    system.GpuMemoryBudget={1024ull*1024*1024,lv::MetricStatus::Measured,"test budget"};system.GpuMemoryUsage={0,lv::MetricStatus::Measured,"test usage"};
    r=lv::RecommendQuality(caps,system,scene,1280,720);Check(r.Preset == lv::QualityPreset::High,"supported High may be suggested when transient memory fits headroom");
    system.GpuMemoryUsage.Value=*system.GpuMemoryBudget.Value;r=lv::RecommendQuality(caps,system,scene,1280,720);Check(!r.Preset && r.Profiles[0].Suitability == lv::ProfileSuitability::PerformanceRisk,"exhausted OS budget yields risk, not imaginary available VRAM");
    system={};caps.SampleCounts=1u<<1;r=lv::RecommendQuality(caps,system,scene,1280,720);Check(r.Preset == lv::QualityPreset::Medium && r.Profiles[2].Suitability == lv::ProfileSuitability::Unsupported,"unsupported High never silently recommended with fallback");
    system.AvailableSystemMemory={100ull*1024*1024,lv::MetricStatus::Measured,"test RAM"};r=lv::RecommendQuality(caps,system,scene,1280,720);Check(r.Preset == lv::QualityPreset::Low,"low available RAM marks higher presets as risk");
    system={};auto current=lv::EstimateQualityResources(lv::QualityProfile{},caps,scene,1280,720);lv::FrameStatistics f;f.SampleCount=60;f.P95CpuMs={25,lv::MetricStatus::Measured,"test CPU P95"};
    r=lv::RecommendQuality(caps,system,scene,1280,720,&current.Value(),&f);Check(r.Preset == lv::QualityPreset::Low && !r.PerformanceValidated,"sustained measured CPU cost over target suggests lower preset without GPU guarantee");
    f.SampleCount=2;r=lv::RecommendQuality(caps,system,scene,1280,720,&current.Value(),&f);Check(r.Preset == lv::QualityPreset::Medium,"insufficient timing samples do not trigger CPU pressure policy");
    f.SampleCount=60;f.P95CpuMs={};f.GpuFrameMs={25,lv::MetricStatus::Measured,"test GPU timestamp"};r=lv::RecommendQuality(caps,system,scene,1280,720,&current.Value(),&f);Check(r.Preset == lv::QualityPreset::Low && !r.PerformanceValidated,"measured GPU cost above target flags current/higher presets for testing");
    f.GpuFrameMs.Status=lv::MetricStatus::Unsupported;r=lv::RecommendQuality(caps,system,scene,1280,720,&current.Value(),&f);Check(r.Preset == lv::QualityPreset::Medium,"unsupported GPU counter is excluded from recommendation evidence");
    caps.GraphicsAvailable=false;r=lv::RecommendQuality(caps,system,scene,1280,720);Check(!r.Preset && r.Profiles.size() == 3,"no graphics yields explicit unsupported assessments");
}
void SystemAndExport() {
    const auto hardware=lv::SystemInfo::QueryHardware();Check(!hardware.OperatingSystem.empty() && !hardware.Architecture.empty(),"headless hardware query reports platform and architecture");
    Check(hardware.TotalSystemMemory.Value && *hardware.TotalSystemMemory.Value > 0,"OS-reported system RAM is available without a renderer");
    Check(!hardware.ActiveGraphicsDevice && !hardware.Capabilities.GraphicsAvailable,"enumeration is not mistaken for an active renderer");
    lv::SystemMonitor monitor;auto first=monitor.Sample(1);auto same=monitor.Sample(1.5);auto next=monitor.Sample(2);
    Check(first.TimestampSeconds == same.TimestampSeconds && next.TimestampSeconds == 2,"OS metrics sampled at most once per second");
    Check(!first.ProcessCpuPercent.Value && next.ProcessCpuPercent.Value && *next.ProcessCpuPercent.Value >= 0 && *next.ProcessCpuPercent.Value <= 100,"CPU percentage needs an interval and is normalized across logical processors");
    Check(next.ProcessResidentMemory.Value && *next.ProcessResidentMemory.Value > 0,"process resident memory collected from OS");
    Check(!next.GpuUtilizationPercent.Value && next.GpuUtilizationPercent.Status == lv::MetricStatus::Unsupported,"unsupported utilization remains empty, not zero");
    Check(!monitor.Sample(.5).ProcessCpuPercent.Value,"backward clock rebases the process sampler");Rejects([&]{monitor.Sample(-1);},"negative OS sample clock rejected");
    lv::Viewer viewer;Check(!viewer.GetTelemetry(),"Viewer telemetry is unavailable before its runtime starts");bool wrong=false;std::thread thread([&]{auto r=viewer.GetTelemetry();wrong=!r && r.Error().Code == lv::ErrorCode::WrongThread;});thread.join();Check(wrong,"Viewer telemetry enforces owner thread");
    TemporaryDirectory temp;lv::TelemetrySnapshot s;s.Hardware=hardware;s.System=next;s.Hardware.CpuName="테스트,\"CPU\"\nsecond line";
    const auto json=temp.File("telemetry.json"),csv=temp.File("telemetry.csv");Check(bool(lv::SaveTelemetry(s,json)),"telemetry JSON export succeeds without Editor");
    const auto root=nlohmann::json::parse(Read(json));Check(root["format"] == "MinuvexTelemetry" && root["hardware"]["cpu"] == s.Hardware.CpuName,"export preserves UTF-8 metadata");
    Check(root["metrics"][0].contains("status") && root["metrics"][0].contains("source") && root["metrics"][0].contains("unit"),"export retains metric reliability, source and units");
    Check(bool(lv::SaveTelemetry(s,csv,lv::TelemetryFormat::Csv)) && Read(csv).find("\"테스트,\"\"CPU\"\"\nsecond line\"") != std::string::npos,"CSV quotes commas, quotes and newlines");
    const auto before=Read(json);s.Frames.Fps={std::numeric_limits<double>::infinity(),lv::MetricStatus::Measured,"bad"};Check(!lv::SaveTelemetry(s,json) && Read(json) == before,"invalid values leave previous export intact");
    s.Frames.Fps={0,lv::MetricStatus::Unsupported,"bad"};Check(!lv::SaveTelemetry(s,json),"unsupported numeric zero cannot masquerade as a measurement");
    s.Frames.Fps={};const auto fail=lv::SaveTelemetry(s,temp.File("missing/telemetry.json"));Check(!fail && fail.Error().Code == lv::ErrorCode::TelemetryIo,"export IO failure carries distinct error and path");
    std::size_t files=0;for (const auto& entry:std::filesystem::directory_iterator(temp.Path)) {(void)entry;++files;}Check(files == 2,"failed atomic export leaves no temporary files");
}
}
int main() {try {Frames();Resources();Recommendations();SystemAndExport();}catch (const std::exception& e) {Check(false,e.what());}return Finish();}
