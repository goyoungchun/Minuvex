#include <minvex/EditorDocument.h>

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) { return 2; }
    lv::EditorDocument document;
    const auto model = document.ImportModel(argv[1]);
    if (!model) { std::fprintf(stderr,"%s\n",model.Error().Message.c_str()); return 1; }
    if (!document.PlaceModel(model.Value()) || !document.Save(argv[2])) { return 1; }
    lv::EditorDocument reopened;
    return reopened.Open(argv[2]) && reopened.GetScene().Size() == 1 && reopened.Assets().size() == 1 ? 0 : 1;
}
