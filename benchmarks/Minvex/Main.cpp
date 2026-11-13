#include <minvex/Version.h>
#include <minvex/Viewer.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace {
using Clock=std::chrono::steady_clock;
double Milliseconds(Clock::duration d) {return std::chrono::duration<double,std::milli>(d).count();}
std::string Quote(std::string_view value) {
    std::string result="\"";
    for (const unsigned char c:value) {
        if (c == '"' || c == '\\') {result+='\\';result+=char(c);}
        else if (c < 32) {char escape[7];std::snprintf(escape,sizeof(escape),"\\u%04x",unsigned(c));result+=escape;}
        else {result+=char(c);}
    }
    return result+'"';
}
int Number(std::string_view text) {int n=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),n);if (error != std::errc{} || end != text.data()+text.size() || n < 1 || n > 100000) {throw std::invalid_argument("Counts must be in [1,100000].");}return n;}
double P95(std::vector<double> values) {std::sort(values.begin(),values.end());return values[std::size_t(std::ceil(values.size()*.95))-1];}
}
int main(int argc,char** argv) {
    try {
        int objects=1000,frames=240,warmup=60;std::filesystem::path output="benchmark.json";
        for (int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];if (arg == "--help") {std::puts("minvex_benchmark [--objects N] [--frames N] [--warmup N] [--output file.json]");return 0;}
            if (i+1 == argc) {throw std::invalid_argument("Missing argument value.");}
            const std::string_view value=argv[++i];
            if (arg == "--objects") {objects=Number(value);}else if (arg == "--frames") {frames=Number(value);}else if (arg == "--warmup") {warmup=Number(value);}else if (arg == "--output") {output=value;}else {throw std::invalid_argument("Unknown argument.");}
        }
        const auto start=Clock::now();lv::ViewerOptions options;options.Width=1280;options.Height=720;options.Title="Minvex SDK benchmark";
        options.VSync=false;options.HighDpi=false;options.AutoFrame=false;options.EnableDefaultInput=false;options.ClearColor={.035f,.045f,.065f,1};
        options.Quality=lv::MakeQualityProfile(lv::QualityPreset::Low);options.Quality.Overrides.TargetFps=0;options.Quality.Overrides.OnDemand=lv::OnDemandMode::Off;options.Quality.Overrides.FrustumCulling=true;
        options.Quality.Overrides.Lod=false;options.Quality.Overrides.Instancing=false;options.Quality.Overrides.Compression=lv::TextureCompression::None;
        lv::Viewer viewer(options);auto model=std::make_shared<lv::ModelAsset>();model->Meshes.push_back({"Shared cube",{{lv::MakeCube(),0,false}}});model->Materials.push_back({});model->Parts.push_back({0});model->ModelBounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};
        auto& scene=viewer.GetScene();const int side=int(std::ceil(std::cbrt(objects)));const float center=float(side-1);
        for (int i=0;i<objects;++i) {lv::Transform t;t.Position={float(i%side)*2-center,float((i/side)%side)*2-center,float(i/(side*side))*2-center};scene.AddModel(model,t);}
        scene.MainCamera.Position={side*2.5f,side*2.0f,side*2.5f};scene.MainCamera.Target={};scene.MainCamera.NearPlane=.1f;scene.MainCamera.FarPlane=1000;
        scene.Sun.Direction={-.4f,-.8f,-.6f};scene.Sun.Intensity=.75f;scene.Sun.Ambient=.25f;
        const auto built=Clock::now();double initMs=0;std::vector<double> intervals;intervals.reserve(frames);auto previous=Clock::now();
        lv::ViewerRunOptions run;run.FrameLimit=std::uint64_t(warmup)+frames+1;
        run.OnFrame=[&](lv::Viewer&,const lv::ViewerFrameInfo& info) {if (info.FramebufferWidth != 1280 || info.FramebufferHeight != 720) {throw std::runtime_error("Benchmark requires an unchanged 1280x720 framebuffer.");}const auto now=Clock::now();if (info.FrameNumber == 0) {initMs=Milliseconds(now-built);}if (info.FrameNumber > std::uint64_t(warmup)) {intervals.push_back(Milliseconds(now-previous));}previous=now;};
        const auto rendered=viewer.Run(run);if (!rendered) {throw std::runtime_error(rendered.Error().Message);}
        const auto telemetry=viewer.GetTelemetry();if (!telemetry || intervals.size() != std::size_t(frames)) {throw std::runtime_error("Incomplete benchmark samples.");}
        const auto& snapshot=telemetry.Value();const auto saved=lv::SaveTelemetry(snapshot,output.string()+".telemetry.json");if (!saved) {throw std::runtime_error(saved.Error().Message);}
        std::ofstream file(output,std::ios::binary);file << std::setprecision(12);
        file << "{\n  \"format\":\"MinvexBenchmark\",\"version\":2,\"engine\":\"Minvex\",\"engineVersion\":" << Quote(lv::Version)
             << ",\n  \"scenario\":\"shared-cubes-v2-unbatched\",\"objects\":" << objects << ",\"width\":1280,\"height\":720,\"msaa\":1,\"vsync\":false,\"shadows\":false,\"warmupFrames\":" << warmup << ",\"measuredFrames\":" << frames
             << ",\n  \"lod\":false,\"instancing\":false,\"frustumCulling\":true,\"postProcessing\":false,\"compression\":\"none\""
             << ",\n  \"backend\":" << Quote(snapshot.Hardware.Backend == lv::GraphicsBackend::D3D11 ? "d3d11":"metal") << ",\"gpu\":" << Quote(snapshot.Hardware.GpuName)
             << ",\n  \"sceneBuildMs\":" << Milliseconds(built-start) << ",\"runtimeInitMs\":" << initMs << ",\"cadenceMeanMs\":" << std::accumulate(intervals.begin(),intervals.end(),0.0)/intervals.size() << ",\"cadenceP95Ms\":" << P95(intervals)
             << ",\n  \"cpuSubmissionMeanMs\":";
        if (snapshot.Frames.AverageCpuMs.Value) {file << *snapshot.Frames.AverageCpuMs.Value;}else {file << "null";}
        file << ",\"gpuLatestMs\":";if (snapshot.Frames.GpuFrameMs.Value) {file << *snapshot.Frames.GpuFrameMs.Value;}else {file << "null";}
        file << ",\"processResidentBytes\":";if (snapshot.System.ProcessResidentMemory.Value) {file << *snapshot.System.ProcessResidentMemory.Value;}else {file << "null";}
        file << ",\"modelLoadMs\":null,\"executableBytes\":" << std::filesystem::file_size(argv[0]) << "\n}\n";
        file.close();if (!file) {throw std::runtime_error("Cannot write benchmark report.");}
        std::printf("%d objects, %d measured frames: %s\n",objects,frames,output.string().c_str());return 0;
    }catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
