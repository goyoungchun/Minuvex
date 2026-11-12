#include "HostBridge.h"
#include <chrono>
#include <thread>
#if defined(_WIN32)
#include <d3d11_1.h>
#include <wrl/client.h>
#undef GetObject
#elif defined(__APPLE__)
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>
#endif

namespace lv::detail {
namespace {
void Require(bool condition,const char* message) {if (!condition) {throw std::invalid_argument(message);}}
Result<void> Invalid(const char* message) {return Result<void>::Failure({ErrorCode::InvalidArgument,message,"Use matching typed single-sample 2D targets on the initialized Host device.",{}});}
#if defined(_WIN32)
template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
DXGI_FORMAT NativeColor(EmbeddedColorFormat f) {
    switch (f) {case EmbeddedColorFormat::Rgba8:return DXGI_FORMAT_R8G8B8A8_UNORM;case EmbeddedColorFormat::Rgba8Srgb:return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;case EmbeddedColorFormat::Bgra8:return DXGI_FORMAT_B8G8R8A8_UNORM;case EmbeddedColorFormat::Bgra8Srgb:return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;}
    throw std::invalid_argument("Invalid embedded color format.");
}
class D3DState final:public HostStateScope {
    ID3D11DeviceContext1* context_;
    ComPtr<ID3DDeviceContextState> previous_;
public:
    D3DState(ID3D11DeviceContext1* context,ID3DDeviceContextState* isolated):context_(context) {context_->SwapDeviceContextState(isolated,&previous_);}
    ~D3DState() override {context_->ClearState();context_->SwapDeviceContextState(previous_.Get(),nullptr);}
};
#endif
}
sg_pixel_format EmbeddedColor(EmbeddedColorFormat f) {
    switch (f) {case EmbeddedColorFormat::Rgba8:return SG_PIXELFORMAT_RGBA8;case EmbeddedColorFormat::Rgba8Srgb:return SG_PIXELFORMAT_SRGB8A8;case EmbeddedColorFormat::Bgra8:return SG_PIXELFORMAT_BGRA8;case EmbeddedColorFormat::Bgra8Srgb:return SG_PIXELFORMAT_SBGR8A8;}
    throw std::invalid_argument("Invalid embedded color format.");
}
sg_pixel_format EmbeddedDepth(EmbeddedDepthFormat f) {
    switch (f) {case EmbeddedDepthFormat::Depth32Float:return SG_PIXELFORMAT_DEPTH;case EmbeddedDepthFormat::DepthStencil:return SG_PIXELFORMAT_DEPTH_STENCIL;}
    throw std::invalid_argument("Invalid embedded depth format.");
}
struct HostBridge::Impl {
    HostGraphicsContext Host;
#if defined(_WIN32)
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext1> Context;
    ComPtr<ID3DDeviceContextState> Isolated;
#elif defined(__APPLE__)
    id<MTLDevice> Device;
#endif
    explicit Impl(const HostGraphicsContext& host):Host(host) {
        (void)EmbeddedColor(host.ColorFormat);(void)EmbeddedDepth(host.DepthFormat);
#if defined(_WIN32)
        Require(host.Backend == GraphicsBackend::D3D11 && host.Device && host.ImmediateContext,"Windows Embedded requires a D3D11 device and immediate context.");
        Device=static_cast<ID3D11Device*>(const_cast<void*>(host.Device));auto* context=static_cast<ID3D11DeviceContext*>(const_cast<void*>(host.ImmediateContext));
        ComPtr<ID3D11Device> actual;context->GetDevice(&actual);
        Require(actual.Get() == Device.Get() && context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE,"Host context must be the same device's immediate context.");
        if (Device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {throw std::runtime_error("Embedded HLSL5 rendering requires D3D feature level 11.0 or higher.");}
        ComPtr<ID3D11Device1> device1;
        if (FAILED(Device.As(&device1)) || FAILED(context->QueryInterface(IID_PPV_ARGS(&Context)))) {throw std::runtime_error("D3D11.1 context-state isolation is required.");}
        const D3D_FEATURE_LEVEL levels[]={Device->GetFeatureLevel()};D3D_FEATURE_LEVEL selected{};
        if (FAILED(device1->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&Isolated))) {throw std::runtime_error("Cannot create isolated D3D11 Host context state.");}
#elif defined(__APPLE__)
        Require(host.Backend == GraphicsBackend::Metal && host.Device && !host.ImmediateContext,"macOS Embedded requires an MTLDevice and no immediate context.");
        Device=(__bridge id<MTLDevice>)host.Device;
#else
        throw std::runtime_error("Embedded graphics is unavailable on this platform.");
#endif
    }
};
HostBridge::HostBridge(const HostGraphicsContext& context):impl_(std::make_unique<Impl>(context)) {}
HostBridge::~HostBridge()=default;
std::unique_ptr<HostStateScope> HostBridge::Enter() {
#if defined(_WIN32)
    return std::make_unique<D3DState>(impl_->Context.Get(),impl_->Isolated.Get());
#else
    return std::make_unique<HostStateScope>();
#endif
}
sg_environment HostBridge::Environment() const {
    sg_environment e{};e.defaults.color_format=EmbeddedColor(impl_->Host.ColorFormat);e.defaults.depth_format=EmbeddedDepth(impl_->Host.DepthFormat);e.defaults.sample_count=1;
#if defined(_WIN32)
    e.d3d11.device=impl_->Device.Get();e.d3d11.device_context=impl_->Context.Get();
#elif defined(__APPLE__)
    e.metal.device=(__bridge const void*)impl_->Device;
#endif
    return e;
}
Result<void> HostBridge::ValidateTarget(const HostRenderTarget& t) const {
#if defined(_WIN32)
    auto check=[&](const void* pointer,bool depth) {
        auto* texture=static_cast<ID3D11Texture2D*>(const_cast<void*>(pointer));ComPtr<ID3D11Device> device;texture->GetDevice(&device);D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        const auto format=depth ? (impl_->Host.DepthFormat == EmbeddedDepthFormat::Depth32Float ? DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_D32_FLOAT_S8X24_UINT):NativeColor(impl_->Host.ColorFormat);
        return device.Get() == impl_->Device.Get() && d.Width == static_cast<UINT>(t.Width) && d.Height == static_cast<UINT>(t.Height) && d.Format == format && d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0 && d.ArraySize == 1 && d.MipLevels == 1 && d.Usage == D3D11_USAGE_DEFAULT && (d.BindFlags&(depth ? D3D11_BIND_DEPTH_STENCIL:D3D11_BIND_RENDER_TARGET));
    };
    if (!check(t.ColorTexture,false) || (t.DepthTexture && !check(t.DepthTexture,true))) {return Invalid("Host texture device, extent, format, usage or sample count does not match.");}
#elif defined(__APPLE__)
    const MTLPixelFormat colors[]={MTLPixelFormatRGBA8Unorm,MTLPixelFormatRGBA8Unorm_sRGB,MTLPixelFormatBGRA8Unorm,MTLPixelFormatBGRA8Unorm_sRGB};
    auto check=[&](const void* pointer,bool depth) {
        id<MTLTexture> texture=(__bridge id<MTLTexture>)pointer;const auto format=depth ? (impl_->Host.DepthFormat == EmbeddedDepthFormat::Depth32Float ? MTLPixelFormatDepth32Float:MTLPixelFormatDepth32Float_Stencil8):colors[static_cast<int>(impl_->Host.ColorFormat)];
        return texture.device == impl_->Device && texture.width == static_cast<NSUInteger>(t.Width) && texture.height == static_cast<NSUInteger>(t.Height) && texture.pixelFormat == format && texture.textureType == MTLTextureType2D && texture.sampleCount == 1 && texture.arrayLength == 1 && texture.mipmapLevelCount == 1 && texture.storageMode != MTLStorageModeMemoryless && (texture.usage&MTLTextureUsageRenderTarget) && texture.hazardTrackingMode != MTLHazardTrackingModeUntracked;
    };
    if (!check(t.ColorTexture,false) || (t.DepthTexture && !check(t.DepthTexture,true))) {return Invalid("Metal target must match device/extent/format and use tracked, persistent 2D render-target storage.");}
#endif
    return Result<void>::Success();
}
Result<void> HostBridge::WaitIdle(std::uint32_t timeout) {
#if defined(_WIN32)
    ComPtr<ID3D11Query> query;D3D11_QUERY_DESC d{D3D11_QUERY_EVENT,0};
    if (FAILED(impl_->Device->CreateQuery(&d,&query))) {return Result<void>::Failure({ErrorCode::Rendering,"Cannot create GPU completion query.","Check device removal and resource availability.",{}});}
    impl_->Context->End(query.Get());impl_->Context->Flush();const auto start=std::chrono::steady_clock::now();
    for (;;) {const auto hr=impl_->Context->GetData(query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);if (hr == S_OK) {return Result<void>::Success();}if (FAILED(hr)) {break;}
        if (std::chrono::steady_clock::now()-start >= std::chrono::milliseconds(timeout)) {break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
#elif defined(__APPLE__)
    id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)sg_mtl_command_queue();id<MTLCommandBuffer> command=[queue commandBuffer];
    if (command) {dispatch_semaphore_t done=dispatch_semaphore_create(0);[command addCompletedHandler:^(id<MTLCommandBuffer>){dispatch_semaphore_signal(done);}];[command commit];
        if (dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,static_cast<int64_t>(timeout)*NSEC_PER_MSEC)) == 0 && command.status == MTLCommandBufferStatusCompleted) {return Result<void>::Success();}}
#endif
    return Result<void>::Failure({ErrorCode::Rendering,"GPU completion timed out or the device failed.","Work may remain in flight; retain Host resources and retry or recover the device.",{}});
}
} // namespace lv::detail
