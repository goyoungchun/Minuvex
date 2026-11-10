#include <minuvex/Viewer.h>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace {
lv::ModelHandle Floor() {
    auto model=std::make_shared<lv::ModelAsset>();lv::MeshPrimitive primitive;
    primitive.Geometry.Vertices={{{-3,0,-3},{0,1,0},{1,1,1,1},{0,0}},{{-3,0,3},{0,1,0},{1,1,1,1},{0,4}},{{3,0,3},{0,1,0},{1,1,1,1},{4,4}},{{3,0,-3},{0,1,0},{1,1,1,1},{4,0}}};primitive.Geometry.Indices={0,1,2,0,2,3};for (auto& v:primitive.Geometry.Vertices) {v.Tangent={1,0,0,-1};}
    model->Meshes.push_back({"Floor",{primitive}});model->Parts.push_back({0});model->Materials.push_back({});model->Materials[0].BaseColor={.65f,.65f,.65f,1};model->Materials[0].NormalTexture=0;
    lv::ImageAsset image;image.Width=image.Height=32;image.Rgba.resize(32*32*4);
    for (int y=0;y<32;++y) {for (int x=0;x<32;++x) {const float nx=.35f*std::sin(x*6.2831853f/16),ny=.35f*std::sin(y*6.2831853f/16),nz=std::sqrt(1-nx*nx-ny*ny);const auto i=(y*32+x)*4;image.Rgba[i]=static_cast<std::uint8_t>((nx*.5f+.5f)*255);image.Rgba[i+1]=static_cast<std::uint8_t>((ny*.5f+.5f)*255);image.Rgba[i+2]=static_cast<std::uint8_t>((nz*.5f+.5f)*255);image.Rgba[i+3]=255;}}
    model->Images.push_back(std::move(image));model->Textures.push_back({});model->ModelBounds={{-3,0,-3},{3,0,3}};return model;
}
}
int main(int argc,char** argv) {
    try {
        lv::QualityPreset preset=lv::QualityPreset::High;lv::ViewerRunOptions run;
        for (int i=1;i<argc;++i) {const std::string_view arg=argv[i];if (arg == "--preset" && i+1<argc) {const std::string_view p=argv[++i];if (p != "low" && p != "medium" && p != "high") {return 2;}preset=p == "low" ? lv::QualityPreset::Low:p == "medium" ? lv::QualityPreset::Medium:lv::QualityPreset::High;}
            else if (arg == "--frames" && i+1<argc) {const std::string_view n=argv[++i];const auto [end,error]=std::from_chars(n.data(),n.data()+n.size(),run.FrameLimit);if (error != std::errc{} || end != n.data()+n.size()) {return 2;}}
            else {std::puts("Usage: minuvex_rendering_enhancement [--preset low|medium|high] [--frames N]");return arg == "--help" ? 0:2;}}
        lv::ViewerOptions options;options.AutoFrame=false;options.Title="Minuvex Rendering Enhancement";options.Quality=lv::MakeQualityProfile(preset);options.Quality.AllowFallback=true;lv::Viewer viewer(options);auto& scene=viewer.GetScene();scene.AddModel(Floor());
        auto cube=std::make_shared<lv::ModelAsset>();cube->Meshes.push_back({"Cube",{{lv::MakeCube(),0,false}}});cube->Parts.push_back({0});cube->Materials.push_back({});cube->ModelBounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};lv::Transform t;t.Position.Y=.8f;const auto object=scene.AddModel(cube,t);
        scene.MainCamera.Position={4,3,5};scene.MainCamera.Target={0,.5f,0};scene.Sun.Direction={-.4f,-.8f,-.6f};scene.Sun.Intensity=2;scene.Sun.Ambient=.12f;
        scene.PointLights={{{-1,1,1},{1,.15f,.1f},5,5}};scene.SpotLights={{{1,3,1},{-.25f,-1,-.25f},{.1f,.3f,1},10,8,.3f,.7f}};
        double time=0;run.OnFrame=[&](lv::Viewer& v,const lv::ViewerFrameInfo& info) {time+=info.DeltaTimeSeconds;lv::Transform transform;transform.Position.Y=.8f;transform.Rotation={0,static_cast<float>(std::sin(time*.5)),0,static_cast<float>(std::cos(time*.5))};v.GetScene().SetTransform(object,transform);};
        const auto result=viewer.Run(run);if (!result) {std::fprintf(stderr,"%s\n",result.Error().Message.c_str());return 1;}return 0;
    }catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
