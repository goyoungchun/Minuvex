// A source-level SDK consumer: public headers and one CMake target only.
#include <minvex/Viewer.h>
#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc < 2) { return 2; }
    lv::Viewer viewer;
    const auto model = viewer.AddModel(argv[1]);
    if (!model) { std::fprintf(stderr,"%s\n",model.Error().Message.c_str()); return 1; }
    lv::ViewerRunOptions options;
    if (argc > 2) { options.FrameLimit = 3; }
    const auto result = viewer.Run(options);
    if (!result) { std::fprintf(stderr,"%s\n",result.Error().Message.c_str()); return 1; }
    return 0;
}
