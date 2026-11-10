#include "GraphicsTelemetry.h"
#include "GraphicsQuality.h"
#include <sokol_gfx.h>
#include <array>
#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#undef GetObject
#elif defined(__APPLE__)
#import <Metal/Metal.h>
#endif

namespace lv::detail {
struct GraphicsTelemetry::Impl {
    HardwareInfo Hardware=SystemInfo::QueryHardware();
    SystemMonitor Monitor;
    SystemSample Gpu;
    double LastPoll=-1;
    Metric<double> Time{std::nullopt,MetricStatus::Unsupported,"GPU timestamps unavailable on this backend"};
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IDXGIAdapter3> Adapter;
    struct Query {
        Microsoft::WRL::ComPtr<ID3D11Query> Disjoint,Start,End;
        bool Pending=false;
        std::uint64_t Generation=0,Serial=0;
    };
    std::array<Query,4> Queries;
    ID3D11DeviceContext* Context=nullptr;
    Query* Active=nullptr;
    std::uint64_t Generation=0,Serial=0,LatestSerial=0;
    bool Timing=false;
#endif
    Impl() {
        Hardware.Capabilities=QueryQualityCapabilities();Hardware.Backend=Hardware.Capabilities.Backend;Hardware.ActiveGraphicsDevice=Hardware.Capabilities.GraphicsAvailable;
        struct FormatEntry {sg_pixel_format Format;const char* Name;};
        constexpr FormatEntry formats[]={{SG_PIXELFORMAT_RGBA8,"RGBA8"},{SG_PIXELFORMAT_SRGB8A8,"SRGB8A8"},{SG_PIXELFORMAT_BGRA8,"BGRA8"},{SG_PIXELFORMAT_SBGR8A8,"SBGR8A8"},{SG_PIXELFORMAT_DEPTH,"DEPTH"},{SG_PIXELFORMAT_DEPTH_STENCIL,"DEPTH_STENCIL"},{SG_PIXELFORMAT_RGBA16F,"RGBA16F"},{SG_PIXELFORMAT_R32F,"R32F"}};
        for (const auto& entry:formats) {
            const auto f=sg_query_pixelformat(entry.Format);Hardware.Formats.push_back({entry.Name,f.sample,f.filter,f.render,f.msaa});
        }
#if defined(_WIN32)
        auto* device=static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
        Context=static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC d{};if (SUCCEEDED(adapter->GetDesc(&d))) {
                const int n=WideCharToMultiByte(CP_UTF8,0,d.Description,-1,nullptr,0,nullptr,nullptr);
                if (n > 1) {std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,d.Description,-1,text.data(),n,nullptr,nullptr);text.pop_back();Hardware.GpuName=std::move(text);}
                Hardware.DedicatedVideoMemory={d.DedicatedVideoMemory,MetricStatus::Measured,"Active D3D11 adapter dedicated capacity"};
                Hardware.SharedSystemMemory={d.SharedSystemMemory,MetricStatus::Measured,"Active D3D11 adapter shared system limit"};
                Hardware.MemoryKind=d.DedicatedVideoMemory ? GpuMemoryKind::Dedicated:GpuMemoryKind::Shared;
                Hardware.GpuVendorId=d.VendorId;Hardware.GpuDeviceId=d.DeviceId;Hardware.GpuVendor=d.VendorId == 0x10DE ? "NVIDIA":d.VendorId == 0x1002 ? "AMD":d.VendorId == 0x8086 ? "Intel":"Unknown";
            }
            (void)adapter.As(&Adapter);
        }
        Timing=device && Context;
        for (auto& q:Queries) {
            if (!Timing) {break;}
            D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};Timing=SUCCEEDED(device->CreateQuery(&d,&q.Disjoint));d.Query=D3D11_QUERY_TIMESTAMP;
            Timing=Timing && SUCCEEDED(device->CreateQuery(&d,&q.Start)) && SUCCEEDED(device->CreateQuery(&d,&q.End));
        }
        if (Timing) {Time={std::nullopt,MetricStatus::Unavailable,"D3D11 scene GPU timestamp pending (asynchronous)"};}
#elif defined(__APPLE__)
        id<MTLDevice> device=(__bridge id<MTLDevice>)sg_mtl_device();
        if (device) {Hardware.GpuName=device.name.UTF8String;Hardware.MemoryKind=device.hasUnifiedMemory ? GpuMemoryKind::Unified:GpuMemoryKind::Dedicated;}
#endif
    }
    void PollGpu(double now) {
        if (LastPoll >= 0 && now >= LastPoll && now-LastPoll < 1) {return;}LastPoll=now;Gpu={};
#if defined(_WIN32)
        if (Adapter) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(Adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))) {
                Gpu.GpuMemoryUsage={info.CurrentUsage,MetricStatus::Measured,"DXGI process local segment CurrentUsage"};Gpu.GpuMemoryBudget={info.Budget,MetricStatus::Measured,"DXGI OS-assigned process local segment Budget"};
            }
            if (SUCCEEDED(Adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&info))) {
                Gpu.GpuNonLocalUsage={info.CurrentUsage,MetricStatus::Measured,"DXGI process non-local segment CurrentUsage"};Gpu.GpuNonLocalBudget={info.Budget,MetricStatus::Measured,"DXGI process non-local segment Budget"};
            }
        } else {Gpu.GpuMemoryUsage={std::nullopt,MetricStatus::Unsupported,"IDXGIAdapter3 unavailable"};Gpu.GpuMemoryBudget={std::nullopt,MetricStatus::Unsupported,"IDXGIAdapter3 unavailable"};}
#elif defined(__APPLE__)
        id<MTLDevice> device=(__bridge id<MTLDevice>)sg_mtl_device();
        if (device) {
            Gpu.GpuMemoryUsage={static_cast<std::uint64_t>(device.currentAllocatedSize),MetricStatus::Measured,"Metal device currentAllocatedSize (all device resources)"};
            Gpu.GpuMemoryBudget={device.recommendedMaxWorkingSetSize,MetricStatus::Estimated,"Metal recommendedMaxWorkingSetSize; advisory threshold, not dedicated VRAM"};
            Gpu.GpuNonLocalUsage={std::nullopt,MetricStatus::Unsupported,"Metal memory is not split into DXGI local/non-local segments"};Gpu.GpuNonLocalBudget=Gpu.GpuNonLocalUsage;
        }
#endif
    }
};
GraphicsTelemetry::GraphicsTelemetry():impl_(std::make_unique<Impl>()) {}
GraphicsTelemetry::~GraphicsTelemetry()=default;
const HardwareInfo& GraphicsTelemetry::Hardware() const {return impl_->Hardware;}
SystemSample GraphicsTelemetry::Sample(double now) {
    CollectGpu();
    auto result=impl_->Monitor.Sample(now);impl_->PollGpu(now);const auto& g=impl_->Gpu;
    result.GpuMemoryUsage=g.GpuMemoryUsage;result.GpuMemoryBudget=g.GpuMemoryBudget;result.GpuNonLocalUsage=g.GpuNonLocalUsage;result.GpuNonLocalBudget=g.GpuNonLocalBudget;return result;
}
void GraphicsTelemetry::CollectGpu() {
#if defined(_WIN32)
    auto& p=*impl_;if (!p.Timing || p.Active) {return;}
    for (auto& q:p.Queries) {
        if (!q.Pending) {continue;}
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};UINT64 start=0,end=0;
        const auto disjoint=p.Context->GetData(q.Disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (disjoint == S_FALSE) {continue;}
        if (FAILED(disjoint)) {q.Pending=false;p.Time={std::nullopt,MetricStatus::Unavailable,"GPU timestamp read failed"};continue;}
        const auto a=p.Context->GetData(q.Start.Get(),&start,sizeof(start),D3D11_ASYNC_GETDATA_DONOTFLUSH),b=p.Context->GetData(q.End.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (a == S_FALSE || b == S_FALSE) {continue;}q.Pending=false;
        if (q.Generation == p.Generation && q.Serial > p.LatestSerial) {
            p.LatestSerial=q.Serial;
            if (a == S_OK && b == S_OK && !d.Disjoint && d.Frequency && end >= start) {p.Time={double(end-start)/double(d.Frequency)*1000,MetricStatus::Measured,"Asynchronous D3D11 scene GPU timestamps; excludes GUI/presentation"};}
            else {p.Time={std::nullopt,MetricStatus::Unavailable,"Disjoint or invalid GPU timestamp sample discarded"};}
        }
    }
#endif
}
void GraphicsTelemetry::BeginGpu() {
    CollectGpu();
#if defined(_WIN32)
    auto& p=*impl_;if (!p.Timing || p.Active) {return;}
    for (auto& q:p.Queries) {if (!q.Pending) {q.Generation=p.Generation;q.Serial=++p.Serial;p.Active=&q;p.Context->Begin(q.Disjoint.Get());p.Context->End(q.Start.Get());break;}}
#endif
}
void GraphicsTelemetry::EndGpu() noexcept {
#if defined(_WIN32)
    auto& p=*impl_;if (p.Active) {p.Context->End(p.Active->End.Get());p.Context->End(p.Active->Disjoint.Get());p.Active->Pending=true;p.Active=nullptr;}
#endif
}
void GraphicsTelemetry::ResetGpu() {
#if defined(_WIN32)
    auto& p=*impl_;++p.Generation;if (p.Timing) {p.Time={std::nullopt,MetricStatus::Unavailable,"D3D11 scene GPU timestamp pending after quality change"};}
#endif
}
Metric<double> GraphicsTelemetry::GpuTime() const {return impl_->Time;}
}
