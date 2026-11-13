#include <minvex/Viewer.h>

#include <charconv>
#include <cstdio>
#include <string_view>

namespace {
void PrintError(const lv::Error& error) {
    std::fprintf(stderr,"Minvex: %s\n%s\n",error.Message.c_str(),error.Hint.c_str());
}
}

int main(int argc, char* argv[]) {
    auto path = std::filesystem::absolute(argv[0]).parent_path() / "assets" / "SharedBox.glb";
    lv::ViewerRunOptions run;
    bool hasPath = false;
    std::optional<std::size_t> animation;
    lv::QualityProfile quality;
    std::filesystem::path telemetryJson,telemetryCsv;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--help") {
            std::puts("Usage: minvex_standalone [model.glb|model.gltf] [--frames N] [--animation clip-index] [--preset low|medium|high] [--quality profile.mvxquality.json] [--allow-fallback] [--telemetry-json path] [--telemetry-csv path]\nLeft drag: orbit | Right/middle/Shift+left drag: pan | Wheel: zoom | F: frame | Esc: close");
            return 0;
        }
        if (argument == "--frames" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),run.FrameLimit);
            if (error != std::errc{} || end != value.data()+value.size() || value.empty()) {
                std::fputs("--frames requires a nonnegative integer (0 = unlimited).\n",stderr); return 2;
            }
        } else if (argument == "--animation" && i+1 < argc) {
            const std::string_view value(argv[++i]); std::size_t clip=0;
            const auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),clip);
            if (error != std::errc{} || end != value.data()+value.size()) { std::fputs("--animation requires a clip index.\n",stderr); return 2; } animation = clip;
        } else if (argument == "--quality" && i+1 < argc) {
            auto loaded=lv::LoadQuality(argv[++i]);if (!loaded) {PrintError(loaded.Error());return 1;}quality=std::move(loaded).Value();
        } else if (argument == "--preset" && i+1 < argc) {
            const std::string_view preset(argv[++i]);if (preset != "low" && preset != "medium" && preset != "high") {std::fputs("Unknown quality preset.\n",stderr);return 2;}
            quality=lv::MakeQualityProfile(preset == "low" ? lv::QualityPreset::Low:preset == "high" ? lv::QualityPreset::High:lv::QualityPreset::Medium);
        } else if (argument == "--allow-fallback") {quality.AllowFallback=true;}
        else if (argument == "--telemetry-json" && i+1 < argc) {telemetryJson=argv[++i];}
        else if (argument == "--telemetry-csv" && i+1 < argc) {telemetryCsv=argv[++i];}
        else if (!argument.starts_with("--") && !hasPath) { path = argv[i]; hasPath = true; }
        else { std::fprintf(stderr,"Unknown or incomplete argument: %s\n",argv[i]); return 2; }
    }
    lv::ViewerOptions options;options.Quality=quality;lv::Viewer viewer(options);
    const auto model = viewer.AddModel(path);
    if (!model) { PrintError(model.Error()); return 1; }
    if (animation) { const auto playing = viewer.PlayAnimation(model.Value(),*animation); if (!playing) { PrintError(playing.Error()); return 1; } }
    const auto result = viewer.Run(run);
    if (!result) { PrintError(result.Error()); return 1; }
    if (!telemetryJson.empty() || !telemetryCsv.empty()) {
        auto snapshot=viewer.GetTelemetry();if (!snapshot) {PrintError(snapshot.Error());return 1;}
        for (const auto& item:{std::pair{telemetryJson,lv::TelemetryFormat::Json},std::pair{telemetryCsv,lv::TelemetryFormat::Csv}}) {
            if (item.first.empty()) {continue;}const auto saved=lv::SaveTelemetry(snapshot.Value(),item.first,item.second);if (!saved) {PrintError(saved.Error());return 1;}
        }
    }
    return 0;
}
