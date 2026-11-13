#include <minvex/Viewer.h>
#include <cstdio>

int main(int argc,char** argv) {
    if (argc < 2 || argc > 3) {std::puts("Usage: minvex_sdk_viewer model.glb [--interactive]");return 2;}
    lv::ViewerOptions options;options.Quality.AllowFallback=true;
    lv::Viewer viewer(options);const auto model=viewer.AddModel(argv[1]);
    if (!model) {std::fprintf(stderr,"%s\n",model.Error().Message.c_str());return 1;}
    lv::ViewerRunOptions run;run.FrameLimit=3;
    if (argc == 3) {if (std::string_view(argv[2]) != "--interactive") {return 2;}run.FrameLimit=0;}
    const auto result=viewer.Run(run);
    if (!result) {std::fprintf(stderr,"%s\n",result.Error().Message.c_str());return 1;}
    return 0;
}
