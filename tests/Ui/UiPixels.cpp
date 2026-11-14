#include "TestHarness.h"
#include <minvex/EmbeddedRenderer.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>
using namespace test;
using Microsoft::WRL::ComPtr;
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);if (FAILED(hr)) {throw std::runtime_error("D3D11 device unavailable.");}
        constexpr int W=640,H=360;D3D11_TEXTURE2D_DESC desc{};desc.Width=W;desc.Height=H;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> target;device->CreateTexture2D(&desc,nullptr,&target);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;device->CreateTexture2D(&desc,nullptr,&staging);
        lv::EmbeddedRenderer renderer;lv::HostGraphicsContext host;host.Backend=lv::GraphicsBackend::D3D11;host.Device=device.Get();host.ImmediateContext=context.Get();host.ColorFormat=lv::EmbeddedColorFormat::Rgba8;lv::EmbeddedOptions options;options.Quality=lv::MakeQualityProfile(lv::QualityPreset::Low);options.Quality.Overrides.TargetFps=0;options.Quality.Overrides.ResolutionScale=.5f;renderer.Initialize(host,options).Value();lv::Scene scene;lv::HostRenderTarget output{target.Get(),nullptr,W,H};lv::EmbeddedFrameOptions frame;frame.ClearColor={0,0,0,1};
        auto read=[&] {context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};if (FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) {throw std::runtime_error("Readback failed.");}std::vector<std::uint8_t> pixels(W*H*4);for (int y=0;y<H;++y) {std::memcpy(pixels.data()+y*W*4,static_cast<std::uint8_t*>(map.pData)+y*map.RowPitch,W*4);}context->Unmap(staging.Get(),0);return pixels;};
        lv::UiElement e;e.Kind=lv::UiKind::Button;e.Text="Hello 한글";e.Layout={20,20,220,70};e.Normal.Fill.fill({1,0,0,1});e.Normal.Radius.fill(12);const auto id=renderer.GetUi().Add(e).Value();const auto first=renderer.Render(scene,output,frame).Value();Check(first.SceneRendered&&first.RenderWidth==W/2,"UI composes at output resolution with half resolution scene");auto pixels=read();Check(pixels[(40*W+30)*4]>240&&pixels[(40*W+30)*4+1]<10,"UI lands at logical coordinates");Check(pixels[(20*W+20)*4]<10,"rounded corner clips fill");std::size_t white=0;for (int y=20;y<90;++y) {for (int x=20;x<240;++x) {if (pixels[(y*W+x)*4+1]>200) {++white;}}}Check(white>20,"native text survives GPU composite");
        Check(!renderer.Render(scene,output,frame).Value().SceneRendered,"unchanged UI uses on demand cache");e.Normal.Fill.fill({0,1,0,1});renderer.GetUi().Set(id,e).Value();Check(renderer.Render(scene,output,frame).Value().SceneRendered,"UI edit invalidates on demand");pixels=read();Check(pixels[(40*W+30)*4+1]>240&&pixels[(40*W+30)*4]<10,"edited UI replaces cached pixels");
        frame.UiScale=2;renderer.Render(scene,output,frame).Value();pixels=read();Check(pixels[(80*W+60)*4+1]>240&&pixels[(25*W+25)*4+1]<10,"DPI changes rendering without changing logical layout");renderer.GetUi().Clear().Value();renderer.Render(scene,output,frame).Value();pixels=read();Check(pixels[(80*W+60)*4+1]<10,"clearing canvas removes stale overlay");renderer.Shutdown().Value();Check(!renderer.IsInitialized(),"UI resources release with graphics session");
    }catch (const std::exception& e) {Check(false,e.what());}return Finish();
}
