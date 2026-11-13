// Native Win32 Host: no Sokol App/GFX or ImGui headers, no private SDK headers.
#include <minuvex/EmbeddedRenderer.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <windows.h>
#undef GetObject
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
void Require(bool ok,const char* text) {if (!ok) {throw std::runtime_error(text);}}
void Require(HRESULT hr,const char* text) {Require(SUCCEEDED(hr),text);}
template<class T> void Require(const lv::Result<T>& r) {if (!r) {throw std::runtime_error(r.Error().Message+" "+r.Error().Hint);}}
struct Targets {
    ComPtr<ID3D11Texture2D> Color,Depth;
    ComPtr<ID3D11RenderTargetView> Rtv;
    ComPtr<ID3D11DepthStencilView> Dsv;
    int Width=0,Height=0;
    void DepthTarget(ID3D11Device* device,int width,int height,DXGI_FORMAT format=DXGI_FORMAT_D32_FLOAT) {
        Width=width;Height=height;D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Require(device->CreateTexture2D(&d,nullptr,&Depth),"Cannot create Host depth texture.");Require(device->CreateRenderTargetView(Color.Get(),nullptr,&Rtv),"Cannot create Host RTV.");Require(device->CreateDepthStencilView(Depth.Get(),nullptr,&Dsv),"Cannot create Host DSV.");
    }
    void Offscreen(ID3D11Device* device,int width,int height,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT depth=DXGI_FORMAT_D32_FLOAT,bool withDepth=true) {
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        Require(device->CreateTexture2D(&d,nullptr,&Color),"Cannot create Host color texture.");
        if (withDepth) {DepthTarget(device,width,height,depth);}else {Width=width;Height=height;Require(device->CreateRenderTargetView(Color.Get(),nullptr,&Rtv),"Cannot create color-only Host RTV.");}
    }
    lv::HostRenderTarget Target() const {return {Color.Get(),Depth.Get(),Width,Height};}
};
std::vector<std::uint32_t> Pixels(ID3D11Device* device,ID3D11DeviceContext* context,const Targets& t,bool depth=false) {
    auto* source=depth ? t.Depth.Get():t.Color.Get();D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;Require(device->CreateTexture2D(&d,nullptr,&staging),"Cannot create Host readback texture.");context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE m{};Require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Cannot map Host output.");const std::size_t words=d.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? 2:1;std::vector<std::uint32_t> pixels(static_cast<std::size_t>(t.Width)*t.Height*words);
    for (int y=0;y<t.Height;++y) {std::memcpy(pixels.data()+static_cast<std::size_t>(y)*t.Width*words,static_cast<const char*>(m.pData)+static_cast<std::size_t>(y)*m.RowPitch,static_cast<std::size_t>(t.Width)*4*words);}context->Unmap(staging.Get(),0);
    if (words == 2) {for (std::size_t i=1;i<pixels.size();i+=2) {pixels[i]&=255;}} // X24 padding is undefined.
    return pixels;
}
ULONG References(IUnknown* object) {object->AddRef();return object->Release();}
void Compatibility(ID3D11Device* device,ID3D11DeviceContext* context) {
    const DXGI_FORMAT colors[]{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
    int cases=0;
    for (int color=0;color<4;++color) {for (int depth=0;depth<3;++depth) {
        lv::EmbeddedRenderer renderer;lv::Scene scene;lv::EmbeddedOptions options;options.Quality=lv::MakeQualityProfile(lv::QualityPreset::Low);options.Quality.Overrides.ResolutionScale=1;options.Quality.Overrides.TargetFps=0;options.Quality.Overrides.OnDemand=lv::OnDemandMode::On;
        const auto depthFormat=depth == 1 ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT:DXGI_FORMAT_D32_FLOAT;
        lv::HostGraphicsContext host{lv::GraphicsBackend::D3D11,device,context,static_cast<lv::EmbeddedColorFormat>(color),depth == 1 ? lv::EmbeddedDepthFormat::DepthStencil:lv::EmbeddedDepthFormat::Depth32Float};
        Require(renderer.Initialize(host,options));Targets target;target.Offscreen(device,96,64,colors[color],depthFormat,depth != 2);
        const float red[]{1,0,0,1};context->ClearRenderTargetView(target.Rtv.Get(),red);
        if (target.Dsv) {context->ClearDepthStencilView(target.Dsv.Get(),D3D11_CLEAR_DEPTH|(depth == 1 ? D3D11_CLEAR_STENCIL:0),.375f,0x5A);}
        const auto before=Pixels(device,context,target);const auto depthBefore=target.Depth ? Pixels(device,context,target,true):std::vector<std::uint32_t>{};
        lv::EmbeddedFrameOptions frame;frame.ClearColor={.25f,.5f,.75f,1};frame.Viewport={13,9,48,32};frame.TargetLoad=lv::EmbeddedTargetLoad::Preserve;
        const auto first=renderer.Render(scene,target.Target(),frame);Require(first);Require(first.Value().SceneRendered && first.Value().Presented && first.Value().RenderWidth == 48 && first.Value().RenderHeight == 32,"Embedded viewport did not set the internal render extent.");
        const auto after=Pixels(device,context,target);
        const auto sample=after[10*96+14];int r=sample&255,g=(sample>>8)&255,b=(sample>>16)&255;if (color >= 2) {std::swap(r,b);}
        const bool srgb=(color&1) != 0;Require(std::abs(r-(srgb ? 137:64))<=1 && std::abs(g-(srgb ? 188:128))<=1 && std::abs(b-(srgb ? 225:191))<=1,"Host color channels or linear/sRGB encoding are incorrect.");
        for (int y=0;y<64;++y) {for (int x=0;x<96;++x) {const auto i=static_cast<std::size_t>(y)*96+x;const bool inside=x>=13 && x<61 && y>=9 && y<41;Require(inside ? after[i] != before[i]:after[i] == before[i],"Viewport/scissor overwrote Host pixels outside its area or left holes inside.");}}
        if (target.Depth) {Require(Pixels(device,context,target,true) == depthBefore,"Preserve changed Host depth/stencil contents.");}
        // Position changes reuse the same Scene image and still composite into the new area.
        frame.Viewport.X=20;const auto moved=renderer.Render(scene,target.Target(),frame);Require(moved);Require(!moved.Value().SceneRendered && moved.Value().Presented,"Moving an unchanged viewport redrew the Scene.");
        const auto movedPixels=Pixels(device,context,target);Require(movedPixels[9*96+67] != before[9*96+67] && movedPixels[9*96+68] == before[9*96+68],"Cached image did not reach the moved viewport boundary.");
        if (color == 0 && depth == 0) {
            auto scaled=options.Quality;scaled.Overrides.ResolutionScale=.5f;Require(renderer.SetQuality(scaled,scene));const auto quality=renderer.GetQuality();Require(quality);Require(quality.Value().RenderWidth == 24 && quality.Value().RenderHeight == 16,"SetQuality used the full target instead of the active viewport.");Require(renderer.SetQuality(options.Quality,scene));
            const auto stable=renderer.GetQuality().Value();
            auto rejected=frame;rejected.Viewport.X=std::numeric_limits<int>::max();Require(!renderer.Render(scene,target.Target(),rejected),"Overflowing viewport was accepted.");
            rejected.Viewport={0,0,0,32};Require(!renderer.Render(scene,target.Target(),rejected),"Half-zero viewport was accepted.");
            rejected.Viewport={-1,0,48,32};Require(!renderer.Render(scene,target.Target(),rejected),"Negative viewport was accepted.");
            rejected.Viewport={0,33,48,32};Require(!renderer.Render(scene,target.Target(),rejected),"Out-of-bounds viewport was accepted.");
            rejected=frame;rejected.TargetLoad=static_cast<lv::EmbeddedTargetLoad>(99);Require(!renderer.Render(scene,target.Target(),rejected),"Invalid target load action was accepted.");
            Require(renderer.GetQuality().Value().RenderWidth == stable.RenderWidth && Pixels(device,context,target) == movedPixels,"Rejected frame changed the prior extent or Host pixels.");
            const auto queue=renderer.GetMetalCommandQueue();Require(!queue && queue.Error().Code == lv::ErrorCode::UnsupportedFeature,"D3D11 exposed a Metal queue.");
            bool wrong=false;std::thread thread([&]{auto r=renderer.GetMetalCommandQueue();wrong=!r && r.Error().Code == lv::ErrorCode::WrongThread;});thread.join();Require(wrong,"Metal queue query bypassed the owner thread contract.");
        }
        if (color == 0 && depth == 1) {Targets unsupported;unsupported.Offscreen(device,96,64,colors[color],DXGI_FORMAT_D24_UNORM_S8_UINT);const auto rejected=renderer.Render(scene,unsupported.Target(),frame);Require(!rejected && rejected.Error().Code == lv::ErrorCode::InvalidArgument,"Unsupported D24 Host target was accepted.");}
        frame.TargetLoad=lv::EmbeddedTargetLoad::Clear;Require(renderer.Render(scene,target.Target(),frame));const auto cleared=Pixels(device,context,target);Require(cleared[0] != before[0] && cleared.back() != before.back(),"Clear did not clear the entire Host color target.");
        if (target.Depth) {const auto values=Pixels(device,context,target,true);for (std::size_t i=0;i<values.size();i+=depth == 1 ? 2:1) {Require(values[i] == 0x3F800000 && (depth != 1 || values[i+1] == 0),"Clear did not store cleared Host depth/stencil.");}}
        // Suspended frames ignore viewport fit, then detach/release all native references.
        Require(renderer.Render(scene,{},frame));Require(!renderer.GetQuality().Value().Applied,"Sub-viewport suspension retained active extent.");Require(renderer.Shutdown());++cases;
    }}
    std::printf("Embedded compatibility: %d color/depth combinations, preserved viewport pixels/depth, cached composition and frame rejection passed.\n",cases);
}
struct Application {
    HWND Window=nullptr;int PendingWidth=800,PendingHeight=600;bool Closing=false;
    ComPtr<ID3D11Device> Device;ComPtr<ID3D11DeviceContext> Context;ComPtr<IDXGISwapChain> Swapchain;
    Targets Target;
    lv::AssetRegistry Assets;lv::Scene Scene;lv::EmbeddedRenderer Renderer;
    lv::ObjectId Object=lv::NoObject;
    static LRESULT CALLBACK Events(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message == WM_NCCREATE) {app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
        if (app && message == WM_SIZE) {app->PendingWidth=LOWORD(l);app->PendingHeight=HIWORD(l);return 0;}
        if (app && message == WM_DPICHANGED) {const auto* bounds=reinterpret_cast<const RECT*>(l);SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        if (app && message == WM_CLOSE) {app->Closing=true;return 0;}return DefWindowProcW(window,message,w,l);
    }
    ~Application() {(void)Renderer.Shutdown();if (Context) {Context->ClearState();}if (Window) {DestroyWindow(Window);}}
    lv::HostGraphicsContext Host() const {return {lv::GraphicsBackend::D3D11,Device.Get(),Context.Get(),lv::EmbeddedColorFormat::Rgba8Srgb,lv::EmbeddedDepthFormat::Depth32Float};}
    void Initialize(const std::filesystem::path& model) {
        // The Host controls DPI policy; texture/viewport dimensions are physical client pixels.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW wc{};wc.lpfnWndProc=Events;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"MinuvexNativeHost";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
        RECT bounds{0,0,800,600};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
        Window=CreateWindowExW(0,wc.lpszClassName,L"Minuvex Embedded Viewer - native D3D11 Host",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,wc.hInstance,this);Require(Window != nullptr,"Cannot create Host window.");
        RECT client{};GetClientRect(Window,&client);PendingWidth=client.right;PendingHeight=client.bottom;
        DXGI_SWAP_CHAIN_DESC swap{};swap.BufferDesc.Width=PendingWidth;swap.BufferDesc.Height=PendingHeight;swap.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swap.BufferCount=2;swap.OutputWindow=Window;swap.Windowed=TRUE;swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        Require(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&swap,&Swapchain,&Device,nullptr,&Context),"Cannot create native Host device/swapchain.");
        Require(Swapchain->GetBuffer(0,IID_PPV_ARGS(&Target.Color)),"Cannot acquire Host backbuffer.");Target.DepthTarget(Device.Get(),PendingWidth,PendingHeight);
        Object=Scene.AddModel(Assets.LoadModel(model));Scene.MainCamera.Position={0,1,5};Scene.MainCamera.Target={0,1,0};
        if (!Scene.GetObject(Object).Model->Animations.empty()) {Scene.GetAnimation(Object).Play(0);}
        lv::EmbeddedOptions options;options.Quality.Overrides.TargetFps=0;Require(Renderer.Initialize(Host(),options));ShowWindow(Window,SW_SHOW);
    }
    void Resize(int width,int height) {
        Require(Renderer.DetachTarget());Context->OMSetRenderTargets(0,nullptr,nullptr);Target={};
        if (!width || !height) {return;}
        Require(Swapchain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),"Host ResizeBuffers failed; target references must be detached first.");
        Require(Swapchain->GetBuffer(0,IID_PPV_ARGS(&Target.Color)),"Cannot reacquire Host backbuffer.");Target.DepthTarget(Device.Get(),width,height);
    }
    void Smoke() {
        auto original=Target.Target();lv::EmbeddedFrameOptions draw;draw.ForceRedraw=true;draw.ClearColor={0,0,0,1};
        ID3D11RenderTargetView* rtv=Target.Rtv.Get();Context->OMSetRenderTargets(1,&rtv,Target.Dsv.Get());Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        D3D11_VIEWPORT viewport{7,8,59,60,0,1};Context->RSSetViewports(1,&viewport);const float blend[]{.1f,.2f,.3f,.4f};Context->OMSetBlendState(nullptr,blend,0x13579);
        const double before=Scene.GetAnimation(Object).Time();Require(Renderer.Update(Scene,1.0/30));Require(Scene.GetAnimation(Object).Time() > before,"External Update did not advance animation.");const double advanced=Scene.GetAnimation(Object).Time();
        auto first=Renderer.Render(Scene,original,draw);Require(first);Require(first.Value().SceneRendered && first.Value().Presented,"Embedded frame did not draw/present.");Require(Scene.GetAnimation(Object).Time() == advanced,"Render unexpectedly advanced animation.");
        D3D11_PRIMITIVE_TOPOLOGY topology{};Context->IAGetPrimitiveTopology(&topology);D3D11_VIEWPORT restored{};UINT count=1;Context->RSGetViewports(&count,&restored);ComPtr<ID3D11RenderTargetView> restoredRtv;ComPtr<ID3D11DepthStencilView> restoredDsv;Context->OMGetRenderTargets(1,&restoredRtv,&restoredDsv);UINT mask=0;float restoredBlend[4]{};Context->OMGetBlendState(nullptr,restoredBlend,&mask);
        Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST && restored.TopLeftX == 7 && restored.Width == 59 && restoredRtv.Get() == Target.Rtv.Get() && restoredDsv.Get() == Target.Dsv.Get() && mask == 0x13579 && restoredBlend[2] == blend[2],"Minuvex changed Host pipeline state.");
        auto pixels=Pixels(Device.Get(),Context.Get(),Target);Require(std::count_if(pixels.begin(),pixels.end(),[](auto p){return (p&0x00FFFFFF) != 0;}) > 50,"External Host target contains no 3D geometry pixels.");
        auto caps=Renderer.GetQualityCapabilities();Require(caps);Require(caps.Value().Backend == lv::GraphicsBackend::D3D11,"Wrong active Embedded backend.");
        lv::DebugOptions diagnostics;diagnostics.View=lv::DebugView::Normal;diagnostics.ShowSkeleton=true;diagnostics.DepthTest=false;Require(Renderer.SetDebugOptions(diagnostics));
        lv::DebugDraw debug;debug.DrawAxis(lv::Mat4::Identity());Require(Renderer.SetDebugDraw(debug));Require(Renderer.Render(Scene,original,draw));const auto inspection=Renderer.GetRenderDebugInfo();Require(inspection);
        Require(inspection.Value().Live && inspection.Value().LineCount>0 && !inspection.Value().Resources.empty() && Scene.GetAnimation(Object).Time() == advanced,"Embedded debugging changed animation or failed to inspect/draw resources.");
        Context->IAGetPrimitiveTopology(&topology);Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Embedded debug rendering changed Host topology.");Require(Renderer.SetDebugOptions({}));Require(Renderer.SetDebugDraw({}));
        lv::EmbeddedRenderer competing;auto busy=competing.Initialize(Host());Require(!busy && busy.Error().Code == lv::ErrorCode::InvalidState,"Concurrent Embedded session was allowed.");
        bool wrong=false;std::thread other([&]{auto r=Renderer.Shutdown();wrong=!r && r.Error().Code == lv::ErrorCode::WrongThread;});other.join();Require(wrong,"Embedded owner thread was not enforced.");
        auto mismatch=original;++mismatch.Width;Require(!Renderer.Render(Scene,mismatch),"Mismatched Host target extent was accepted.");Targets bad;bad.Offscreen(Device.Get(),800,600,DXGI_FORMAT_R8G8B8A8_UNORM);Require(!Renderer.Render(Scene,bad.Target()),"Mismatched Host target format was accepted.");
        ComPtr<ID3D11Device> otherDevice;ComPtr<ID3D11DeviceContext> otherContext;Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&otherDevice,nullptr,&otherContext),"Cannot create second Host test device.");Targets foreign;foreign.Offscreen(otherDevice.Get(),800,600);Require(!Renderer.Render(Scene,foreign.Target()),"Foreign-device texture was accepted.");
        const auto quality=Renderer.GetQuality().Value();auto invalid=quality.Requested;invalid.Overrides.Shadow=lv::ShadowMode::Pcss;
        Require(!Renderer.SetQuality(invalid,Scene) && Renderer.GetQuality().Value().Requested == quality.Requested,"Rejected quality changed the active Embedded profile.");
        auto paced=quality.Requested;paced.Overrides.TargetFps=1;Require(Renderer.SetQuality(paced,Scene));
        Require(Renderer.Render(Scene,original,draw).Value().SceneRendered && Renderer.Render(Scene,original,draw).Value().SceneRendered,"ForceRedraw did not bypass the FPS limiter.");
        auto normal=draw;normal.ForceRedraw=false;Require(!Renderer.Render(Scene,original,normal).Value().SceneRendered,"Normal rendering bypassed the FPS limiter.");
        auto demand=quality.Requested;demand.Overrides.OnDemand=lv::OnDemandMode::On;Scene.GetAnimation(Object).Pause();Require(Renderer.SetQuality(demand,Scene));draw.ForceRedraw=false;
        auto changed=Renderer.Render(Scene,original,draw);Require(changed);Targets alternate;alternate.Offscreen(Device.Get(),800,600);auto reused=Renderer.Render(Scene,alternate.Target(),draw);Require(reused);
        Require(changed.Value().SceneRendered && !reused.Value().SceneRendered && reused.Value().Presented && Pixels(Device.Get(),Context.Get(),alternate) == Pixels(Device.Get(),Context.Get(),Target),"On-demand cached scene did not reach the replacement Host target.");
        Require(Renderer.RequestRedraw());Require(Renderer.Render(Scene,original,draw).Value().SceneRendered,"Explicit redraw did not invalidate Embedded cache.");
        Require(Renderer.Render(Scene,{}));Require(!Renderer.GetQuality().Value().Applied,"Suspension retained active render targets.");Require(Renderer.Render(Scene,original,draw));
        Require(Renderer.WaitIdle());const auto telemetry=Renderer.GetTelemetry();Require(telemetry);Require(telemetry.Value().Live && telemetry.Value().Frames.DrawCalls > 0 && telemetry.Value().Hardware.ActiveGraphicsDevice,"Embedded telemetry is missing native scene statistics.");
        restoredRtv.Reset();restoredDsv.Reset();Context->ClearState();Require(Renderer.DetachTarget());const auto references=References(Target.Color.Get());Require(Renderer.Shutdown());Require(Renderer.Shutdown());Require(References(Target.Color.Get()) == references,"Shutdown released or retained a Host texture reference.");
        Require(!Renderer.GetTelemetry().Value().Live,"Shutdown snapshot was not marked historical.");const float red[]{1,0,0,1};Context->ClearRenderTargetView(Target.Rtv.Get(),red);Require((Pixels(Device.Get(),Context.Get(),Target)[0]&255) == 255,"Host resources stopped working after Embedded shutdown.");
        lv::EmbeddedOptions options;options.Quality=demand;Require(Renderer.Initialize(Host(),options));Require(Renderer.Render(Scene,original,draw));
        Resize(640,360);Require(Renderer.Render(Scene,Target.Target(),draw));Require(Renderer.WaitIdle());
        Require(Renderer.Shutdown());Compatibility(Device.Get(),Context.Get());Require(Renderer.Initialize(Host(),options));
        std::puts("Embedded D3D11 Host: native pixels, external animation, preserved state, target validation/cache/resize, ownership/session/thread contracts and telemetry passed.");
    }
};
}
int main(int argc,char** argv) {
    try {
        const bool smoke=argc > 1 && std::strcmp(argv[1],"--smoke-test") == 0;
        Application app;app.Initialize(!smoke && argc > 1 ? std::filesystem::path(argv[1]):std::filesystem::path(MINUVEX_EMBEDDED_SAMPLE));if (smoke) {app.Smoke();}
        auto previous=std::chrono::steady_clock::now();std::uint64_t frames=0;
        while (!app.Closing && (!smoke || frames < 3)) {
            MSG message{};while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            if (app.Closing) {break;}if (!smoke && (app.PendingWidth != app.Target.Width || app.PendingHeight != app.Target.Height)) {app.Resize(app.PendingWidth,app.PendingHeight);}
            if (!app.Target.Width) {std::this_thread::sleep_for(std::chrono::milliseconds(8));continue;}
            const auto now=std::chrono::steady_clock::now();Require(app.Renderer.Update(app.Scene,std::chrono::duration<double>(now-previous).count()));previous=now;
            Require(app.Renderer.Render(app.Scene,app.Target.Target()));Require(app.Swapchain->Present(1,0),"Host Present failed.");++frames;
        }
        Require(app.Renderer.Shutdown());return 0;
    }catch (const std::exception& e) {std::fprintf(stderr,"Embedded Host: %s\n",e.what());return 1;}
}
