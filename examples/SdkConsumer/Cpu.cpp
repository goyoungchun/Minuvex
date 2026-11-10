#include <minuvex/Version.h>
#include <minuvex/EditorDocument.h>
#include <minuvex/Viewer.h>
#include <minuvex/EmbeddedRenderer.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) {std::puts("Usage: minuvex_sdk_cpu model.glb output-directory");return 2;}
    try {
        const auto& info=lv::GetBuildInfo();
        if (info.Version != lv::Version || info.PointerBits != sizeof(void*)*8) {return 1;}
        const auto output=std::filesystem::path(argv[2]);std::filesystem::create_directories(output);
        lv::EditorDocument doc;const auto asset=doc.ImportModel(argv[1]);
        if (!asset) {std::fprintf(stderr,"%s\n",asset.Error().Message.c_str());return 1;}
        if (!doc.PlaceModel(asset.Value()) || !doc.SetLocalLights({{{0,2,0},{1,1,1},1,5}}, {})) {return 1;}
        const auto file=output/"Example.mvxscene.json";
        if (!doc.Save(file)) {return 1;}
        lv::EditorDocument reopened;if (!reopened.Open(file) || reopened.GetScene().Size() != 1 || reopened.GetScene().PointLights.size() != 1) {return 1;}
        auto low=lv::MakeQualityProfile(lv::QualityPreset::Low);
        if (!lv::SaveQuality(low,output/"Low.mvxquality.json") || !lv::LoadQuality(output/"Low.mvxquality.json")) {return 1;}
        lv::QualityCapabilities caps;caps.GraphicsAvailable=true;
        const auto estimate=lv::EstimateQualityResources(low,caps,reopened.GetScene(),1280,720);
        if (!estimate || !estimate.Value().Cost.BufferBytes) {return 1;}
        lv::Viewer viewer;if (!viewer.AddModel(argv[1]) || !viewer.SetQuality(low)) {return 1;}
        lv::EmbeddedRenderer embedded;if (embedded.IsInitialized()) {return 1;}
        const auto hardware=lv::SystemInfo::QueryHardware();
        std::printf("Minuvex %.*s | %.*s | %.*s | %d bit | graphics=%d | %s\n",
            int(info.Version.size()),info.Version.data(),int(info.Compiler.size()),info.Compiler.data(),
            int(info.Configuration.size()),info.Configuration.data(),info.PointerBits,info.GraphicsAvailable,hardware.OperatingSystem.c_str());
        return 0;
    }catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
