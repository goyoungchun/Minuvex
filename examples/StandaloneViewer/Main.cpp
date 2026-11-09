#include <minuvex/Viewer.h>

#include <charconv>
#include <cstdio>
#include <string_view>

namespace {
void PrintError(const lv::Error& error) {
    std::fprintf(stderr,"Minuvex: %s\n%s\n",error.Message.c_str(),error.Hint.c_str());
}
}

int main(int argc, char* argv[]) {
    auto path = std::filesystem::absolute(argv[0]).parent_path() / "assets" / "SharedBox.glb";
    lv::ViewerRunOptions run;
    bool hasPath = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--help") {
            std::puts("Usage: minuvex_standalone [model.glb|model.gltf] [--frames N]\nLeft drag: orbit | Right/middle/Shift+left drag: pan | Wheel: zoom | F: frame | Esc: close");
            return 0;
        }
        if (argument == "--frames" && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto [end,error] = std::from_chars(value.data(),value.data()+value.size(),run.FrameLimit);
            if (error != std::errc{} || end != value.data()+value.size() || value.empty()) {
                std::fputs("--frames requires a nonnegative integer (0 = unlimited).\n",stderr); return 2;
            }
        } else if (!argument.starts_with("--") && !hasPath) { path = argv[i]; hasPath = true; }
        else { std::fprintf(stderr,"Unknown or incomplete argument: %s\n",argv[i]); return 2; }
    }
    lv::Viewer viewer;
    const auto model = viewer.AddModel(path);
    if (!model) { PrintError(model.Error()); return 1; }
    const auto result = viewer.Run(run);
    if (!result) { PrintError(result.Error()); return 1; }
    return 0;
}
