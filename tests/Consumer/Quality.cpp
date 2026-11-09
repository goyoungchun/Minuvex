#include <minuvex/Quality.h>

int main(int argc,char** argv) {
    if (argc != 2) {return 2;}
    auto profile=lv::MakeQualityProfile(lv::QualityPreset::Low);profile.Name="Consumer";profile.Overrides.ResolutionScale=.75f;
    if (!lv::SaveQuality(profile,argv[1])) {return 1;}
    auto loaded=lv::LoadQuality(argv[1]);if (!loaded || loaded.Value() != profile) {return 1;}
    lv::QualityCapabilities caps;caps.GraphicsAvailable=true;auto resolved=lv::ResolveQuality(loaded.Value(),caps,1920,1080);
    return resolved && resolved.Value().RenderWidth == 960 && resolved.Value().RenderHeight == 540 && !resolved.Value().Applied ? 0:1;
}
