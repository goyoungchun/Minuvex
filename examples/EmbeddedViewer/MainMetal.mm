// Native Cocoa/Metal Host, independent of Sokol App and Dear ImGui.
#include <minvex/EmbeddedRenderer.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
void Require(bool condition,const char* text) {if (!condition) {throw std::runtime_error(text);}}
template<class T> void Require(const lv::Result<T>& r) {if (!r) {throw std::runtime_error(r.Error().Message);}}
struct Targets {
    id<MTLTexture> Color,Depth;
    int Width=0,Height=0;
    void CreateDepth(id<MTLDevice> device,int width,int height) {
        Width=width;Height=height;auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:width height:height mipmapped:NO];d.usage=MTLTextureUsageRenderTarget;d.storageMode=MTLStorageModePrivate;Depth=[device newTextureWithDescriptor:d];Require(Depth != nil,"Cannot create Host Metal depth texture.");
    }
    void Offscreen(id<MTLDevice> device,int width,int height) {
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB width:width height:height mipmapped:NO];d.usage=MTLTextureUsageRenderTarget;d.storageMode=MTLStorageModePrivate;Color=[device newTextureWithDescriptor:d];Require(Color != nil,"Cannot create Host Metal color texture.");CreateDepth(device,width,height);
    }
    lv::HostRenderTarget Target() const {return {(__bridge const void*)Color,(__bridge const void*)Depth,Width,Height};}
};
std::vector<std::uint32_t> Pixels(id<MTLDevice> device,id<MTLCommandQueue> queue,const Targets& t) {
    const NSUInteger row=(static_cast<NSUInteger>(t.Width)*4+255)&~NSUInteger(255);id<MTLBuffer> buffer=[device newBufferWithLength:row*t.Height options:MTLResourceStorageModeShared];Require(buffer != nil,"Cannot allocate Metal Host readback.");
    auto command=[queue commandBuffer];auto copy=[command blitCommandEncoder];[copy copyFromTexture:t.Color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(t.Width,t.Height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*t.Height];[copy endEncoding];[command commit];[command waitUntilCompleted];Require(command.status == MTLCommandBufferStatusCompleted,"Host Metal readback failed.");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(t.Width)*t.Height);for (int y=0;y<t.Height;++y) {std::memcpy(pixels.data()+static_cast<std::size_t>(y)*t.Width,static_cast<const char*>(buffer.contents)+row*y,t.Width*4);}return pixels;
}
void Smoke(id<MTLDevice> device,id<MTLCommandQueue> queue,lv::EmbeddedRenderer& renderer,lv::Scene& scene,lv::ObjectId object,const lv::HostGraphicsContext& host) {
    Targets target;target.Offscreen(device,256,256);lv::EmbeddedFrameOptions frame;frame.ClearColor={0,0,0,1};frame.ForceRedraw=true;
    const auto before=scene.GetAnimation(object).Time();Require(renderer.Update(scene,.1));Require(scene.GetAnimation(object).Time() > before,"External update failed.");const auto advanced=scene.GetAnimation(object).Time();Require(renderer.Render(scene,target.Target(),frame));Require(renderer.WaitIdle());
    auto pixels=Pixels(device,queue,target);Require(std::count_if(pixels.begin(),pixels.end(),[](auto p){return (p&0x00FFFFFF) != 0;}) > 50,"No geometry in Host Metal texture.");Require(scene.GetAnimation(object).Time() == advanced,"Render advanced Host animation.");
    // Host clear -> SDK -> Host readback all commit to the borrowed queue in order.
    Targets panel;panel.Offscreen(device,96,64);auto clear=[MTLRenderPassDescriptor renderPassDescriptor];clear.colorAttachments[0].texture=panel.Color;clear.colorAttachments[0].loadAction=MTLLoadActionClear;clear.colorAttachments[0].storeAction=MTLStoreActionStore;clear.colorAttachments[0].clearColor=MTLClearColorMake(1,0,0,1);
    clear.depthAttachment.texture=panel.Depth;clear.depthAttachment.loadAction=MTLLoadActionClear;clear.depthAttachment.storeAction=MTLStoreActionStore;clear.depthAttachment.clearDepth=.375;
    auto command=[queue commandBuffer];auto encoder=[command renderCommandEncoderWithDescriptor:clear];[encoder endEncoding];[command commit];const auto background=Pixels(device,queue,panel);
    lv::Scene empty;auto inset=frame;inset.ClearColor={.25f,.5f,.75f,1};inset.Viewport={13,9,48,32};inset.TargetLoad=lv::EmbeddedTargetLoad::Preserve;Require(renderer.Render(empty,panel.Target(),inset));const auto composed=Pixels(device,queue,panel);
    for (int y=0;y<64;++y) {for (int x=0;x<96;++x) {const auto i=static_cast<std::size_t>(y)*96+x;const bool inside=x>=13 && x<61 && y>=9 && y<41;Require(inside ? composed[i] != background[i]:composed[i] == background[i],"Metal viewport changed Host pixels outside its area or left holes.");}}
    panel.Depth=nil;const auto colorOnly=renderer.Render(empty,panel.Target(),inset);Require(colorOnly);Require(colorOnly.Value().RenderWidth == 48 && colorOnly.Value().RenderHeight == 32,"Metal color-only viewport extent failed.");
    lv::EmbeddedRenderer other;auto busy=other.Initialize(host);Require(!busy && busy.Error().Code == lv::ErrorCode::InvalidState,"Concurrent Metal session was accepted.");
    bool wrong=false;std::thread thread([&]{auto result=renderer.Shutdown();wrong=!result && result.Error().Code == lv::ErrorCode::WrongThread;});thread.join();Require(wrong,"Owner thread was not enforced.");
    auto mismatch=target.Target();++mismatch.Height;Require(!renderer.Render(scene,mismatch),"Mismatched Metal extent was accepted.");
    const auto quality=renderer.GetQuality().Value();auto bad=quality.Requested;bad.Overrides.Shadow=static_cast<lv::ShadowMode>(99);
    const auto rejected=renderer.SetQuality(bad,scene);Require(!rejected && rejected.Error().Code == lv::ErrorCode::InvalidArgument && renderer.GetQuality().Value().Requested == quality.Requested,"Rejected Metal quality changed the active profile.");
    auto pcss=lv::MakeQualityProfile(lv::QualityPreset::Low);pcss.Overrides.Shadow=lv::ShadowMode::Pcss;pcss.Overrides.TargetFps=0;
    const auto capabilities=renderer.GetQualityCapabilities();Require(capabilities);
    for (bool fallback:{false,true}) {
        pcss.AllowFallback=fallback;const auto expected=lv::ResolveQuality(pcss,capabilities.Value(),target.Width,target.Height);
        const auto applied=renderer.SetQuality(pcss,scene);Require(bool(applied) == bool(expected),"Metal PCSS application disagrees with device capabilities.");
        if (expected) {Require(renderer.GetQuality().Value().Effective == expected.Value().Effective,"Metal PCSS fallback changed the effective settings.");}
        else {Require(applied.Error().Code == expected.Error().Code && renderer.GetQuality().Value().Requested == quality.Requested,"Rejected Metal PCSS changed the active profile.");}
    }
    Require(renderer.SetQuality(quality.Requested,scene));
    auto paced=quality.Requested;paced.Overrides.TargetFps=1;Require(renderer.SetQuality(paced,scene));
    Require(renderer.Render(scene,target.Target(),frame).Value().SceneRendered && renderer.Render(scene,target.Target(),frame).Value().SceneRendered,"ForceRedraw did not bypass the Metal FPS limiter.");
    auto normal=frame;normal.ForceRedraw=false;Require(!renderer.Render(scene,target.Target(),normal).Value().SceneRendered,"Normal Metal rendering bypassed the FPS limiter.");
    auto demand=quality.Requested;demand.Overrides.TargetFps=0;demand.Overrides.OnDemand=lv::OnDemandMode::On;scene.GetAnimation(object).Pause();Require(renderer.SetQuality(demand,scene));frame.ForceRedraw=false;Require(renderer.Render(scene,target.Target(),frame));Require(renderer.WaitIdle());
    Targets replacement;replacement.Offscreen(device,256,256);auto cached=renderer.Render(scene,replacement.Target(),frame);Require(cached);Require(renderer.WaitIdle());Require(!cached.Value().SceneRendered && cached.Value().Presented && Pixels(device,queue,target) == Pixels(device,queue,replacement),"Metal cached output did not reach replacement Host texture.");
    Require(renderer.Render(scene,{}));Require(!renderer.GetQuality().Value().Applied,"Metal suspension failed.");replacement.Offscreen(device,320,180);Require(renderer.Render(scene,replacement.Target(),frame));Require(renderer.WaitIdle());Require(renderer.GetTelemetry());Require(renderer.DetachTarget());Require(renderer.Shutdown());Require(renderer.Shutdown());Require(!renderer.GetTelemetry().Value().Live,"Historical Metal snapshot missing.");
    Require(Pixels(device,queue,target) == pixels,"Host Metal texture did not survive shutdown.");lv::EmbeddedOptions options;options.Quality=demand;Require(renderer.Initialize(host,options));Require(renderer.Render(scene,target.Target(),frame));Require(renderer.WaitIdle());
    std::puts("Embedded Metal Host: native pixels, external animation, target validation/cache/resize, ownership/session/thread contracts and telemetry passed.");
}
}
int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            const bool smoke=argc > 1 && std::strcmp(argv[1],"--smoke-test") == 0;
            auto app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];[app finishLaunching];
            NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,800,600) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];[window setReleasedWhenClosed:NO];window.title=@"Minvex Embedded Viewer - native Metal Host";
            id<MTLDevice> device=MTLCreateSystemDefaultDevice();Require(device != nil,"No native Metal device.");
            CAMetalLayer* layer=[CAMetalLayer layer];layer.device=device;layer.pixelFormat=MTLPixelFormatBGRA8Unorm_sRGB;layer.framebufferOnly=YES;window.contentView.wantsLayer=YES;window.contentView.layer=layer;[window makeKeyAndOrderFront:nil];
            lv::AssetRegistry assets;lv::Scene scene;const auto object=scene.AddModel(assets.LoadModel(!smoke && argc > 1 ? argv[1]:MINVEX_EMBEDDED_SAMPLE));scene.MainCamera.Position={0,1,5};scene.MainCamera.Target={0,1,0};if (!scene.GetObject(object).Model->Animations.empty()) {scene.GetAnimation(object).Play(0);}
            lv::EmbeddedRenderer renderer;lv::HostGraphicsContext host{lv::GraphicsBackend::Metal,(__bridge const void*)device,nullptr,lv::EmbeddedColorFormat::Bgra8Srgb,lv::EmbeddedDepthFormat::Depth32Float};lv::EmbeddedOptions options;options.Quality.Overrides.TargetFps=0;Require(renderer.Initialize(host,options));
            auto borrowed=renderer.GetMetalCommandQueue();Require(borrowed);id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)borrowed.Value();if (smoke) {Smoke(device,queue,renderer,scene,object,host);borrowed=renderer.GetMetalCommandQueue();Require(borrowed);queue=(__bridge id<MTLCommandQueue>)borrowed.Value();}
            auto previous=std::chrono::steady_clock::now();std::uint64_t frames=0;
            while (window.visible && (!smoke || frames < 3)) {@autoreleasepool {
                for (;;) {auto event=[app nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];if (!event) {break;}[app sendEvent:event];}
                [app updateWindows];const auto bounds=window.contentView.bounds;const CGFloat scale=window.backingScaleFactor;layer.contentsScale=scale;layer.drawableSize=CGSizeMake(std::max(1.0,bounds.size.width*scale),std::max(1.0,bounds.size.height*scale));
                id<CAMetalDrawable> drawable=[layer nextDrawable];if (!drawable) {continue;}Targets target;target.Color=drawable.texture;target.CreateDepth(device,static_cast<int>(drawable.texture.width),static_cast<int>(drawable.texture.height));
                const auto now=std::chrono::steady_clock::now();Require(renderer.Update(scene,std::chrono::duration<double>(now-previous).count()));previous=now;Require(renderer.Render(scene,target.Target()));
                auto command=[queue commandBuffer];[command presentDrawable:drawable];[command commit];++frames;
            }}
            Require(renderer.Shutdown());[window close];return 0;
        }catch (const std::exception& e) {std::fprintf(stderr,"Embedded Metal Host: %s\n",e.what());return 1;}
    }
}
