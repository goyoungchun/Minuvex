#include <minvex/SystemInfo.h>
#include <algorithm>
#include <cmath>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <winternl.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <mach/mach.h>
#import <Metal/Metal.h>
#endif

namespace lv {
namespace {
template<class T> Metric<T> Measured(T value, const char* source) {return {value,MetricStatus::Measured,source};}
#if defined(_WIN32)
std::string Utf8(const wchar_t* text) {
    const int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if (n <= 1) {return {};}
    std::string result(static_cast<std::size_t>(n), '\0');WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;
}
double CpuSeconds() {
    FILETIME created{},exit{},kernel{},user{};
    if (!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) {return -1;}
    auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return static_cast<double>(ticks(kernel)+ticks(user))*1e-7;
}
#elif defined(__APPLE__)
std::string SysText(const char* key) {
    std::size_t n=0;if (sysctlbyname(key,nullptr,&n,nullptr,0) || !n) {return {};}
    std::string value(n,'\0');if (sysctlbyname(key,value.data(),&n,nullptr,0)) {return {};}
    while (!value.empty() && !value.back()) {value.pop_back();}return value;
}
std::uint64_t SysNumber(const char* key) {std::uint64_t v=0;std::size_t n=sizeof(v);return sysctlbyname(key,&v,&n,nullptr,0) ? 0:v;}
double CpuSeconds() {rusage r{};if (getrusage(RUSAGE_SELF,&r)) {return -1;}return r.ru_utime.tv_sec+r.ru_stime.tv_sec+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)*1e-6;}
#else
double CpuSeconds() {return -1;}
#endif
}
const char* MetricStatusName(MetricStatus status) noexcept {
    switch (status) {case MetricStatus::Measured:return "Measured";case MetricStatus::Estimated:return "Estimated";case MetricStatus::Unsupported:return "Unsupported";default:return "Unavailable";}
}
HardwareInfo SystemInfo::QueryHardware() {
    HardwareInfo h;h.LogicalProcessors=std::thread::hardware_concurrency();
#if defined(_M_ARM64) || defined(__aarch64__)
    h.Architecture="arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    h.Architecture="x64";
#else
    h.Architecture="unknown";
#endif
#if defined(_WIN32)
    h.OperatingSystem="Windows";h.LogicalProcessors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    using VersionFn=LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto fn=reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    RTL_OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
    if (fn && !fn(&version)) {h.OsVersion=std::to_string(version.dwMajorVersion)+"."+std::to_string(version.dwMinorVersion)+"."+std::to_string(version.dwBuildNumber);}
    wchar_t cpu[256]{};DWORD bytes=sizeof(cpu);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,cpu,&bytes) == ERROR_SUCCESS) {h.CpuName=Utf8(cpu);}
    DWORD n=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&n);
    if (n) {std::vector<unsigned char> storage(n);if (GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()),&n)) {
        for (DWORD pos=0;pos<n;) {const auto* p=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()+pos);if (!p->Size) {break;}if (p->Relationship == RelationProcessorCore) {++h.PhysicalCores;}pos+=p->Size;}
    }}
    MEMORYSTATUSEX m{};m.dwLength=sizeof(m);if (GlobalMemoryStatusEx(&m)) {h.TotalSystemMemory=Measured<std::uint64_t>(m.ullTotalPhys,"GlobalMemoryStatusEx physical RAM");}
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0,&adapter))) {
        DXGI_ADAPTER_DESC1 d{};if (SUCCEEDED(adapter->GetDesc1(&d))) {h.GpuName=Utf8(d.Description);h.GpuVendorId=d.VendorId;h.GpuDeviceId=d.DeviceId;h.GpuVendor=d.VendorId == 0x10DE ? "NVIDIA":d.VendorId == 0x1002 ? "AMD":d.VendorId == 0x8086 ? "Intel":"Unknown";h.DedicatedVideoMemory=Measured<std::uint64_t>(d.DedicatedVideoMemory,"DXGI enumerated adapter descriptor");h.SharedSystemMemory=Measured<std::uint64_t>(d.SharedSystemMemory,"DXGI shared system memory limit");h.MemoryKind=d.DedicatedVideoMemory ? GpuMemoryKind::Dedicated:GpuMemoryKind::Shared;}
    }
#elif defined(__APPLE__)
    h.OperatingSystem="macOS";h.OsVersion=SysText("kern.osproductversion");h.CpuName=SysText("machdep.cpu.brand_string");
    h.PhysicalCores=static_cast<unsigned>(SysNumber("hw.physicalcpu"));h.TotalSystemMemory=Measured(SysNumber("hw.memsize"),"sysctl hw.memsize");
    @autoreleasepool {id<MTLDevice> device=MTLCreateSystemDefaultDevice();if (device) {h.GpuName=device.name.UTF8String;h.MemoryKind=device.hasUnifiedMemory ? GpuMemoryKind::Unified:GpuMemoryKind::Dedicated;h.DedicatedVideoMemory={std::nullopt,MetricStatus::Unsupported,"Metal exposes a recommended working set, not dedicated VRAM capacity"};}}
#else
    h.OperatingSystem="Unsupported platform";
#endif
    return h;
}
const SystemSample& SystemMonitor::Sample(double now) {
    if (!std::isfinite(now) || now < 0) {throw std::invalid_argument("System sample time must be finite and nonnegative.");}
    if (initialized_ && now >= sample_.TimestampSeconds && now-sample_.TimestampSeconds < 1) {return sample_;}
    SystemSample next;next.TimestampSeconds=now;
    next.GpuUtilizationPercent={std::nullopt,MetricStatus::Unsupported,"No reliable portable GPU utilization counter"};
#if defined(_WIN32)
    MEMORYSTATUSEX m{};m.dwLength=sizeof(m);if (GlobalMemoryStatusEx(&m)) {next.AvailableSystemMemory=Measured<std::uint64_t>(m.ullAvailPhys,"GlobalMemoryStatusEx available physical RAM");}
    PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p))) {
        next.ProcessResidentMemory=Measured<std::uint64_t>(p.WorkingSetSize,"GetProcessMemoryInfo working set");next.ProcessPrivateMemory=Measured<std::uint64_t>(p.PrivateUsage,"GetProcessMemoryInfo private commit (not resident RAM)");
    }
#elif defined(__APPLE__)
    vm_statistics64_data_t vm{};mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;vm_size_t page=0;const auto host=mach_host_self();
    if (host_page_size(host,&page) == KERN_SUCCESS && host_statistics64(host,HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&vm),&count) == KERN_SUCCESS) {next.AvailableSystemMemory={std::uint64_t(vm.free_count+vm.inactive_count)*page,MetricStatus::Estimated,"Mach free + inactive pages; reclaimability estimate"};}mach_port_deallocate(mach_task_self(),host);
    mach_task_basic_info_data_t task{};count=MACH_TASK_BASIC_INFO_COUNT;if (task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&task),&count) == KERN_SUCCESS) {next.ProcessResidentMemory=Measured<std::uint64_t>(task.resident_size,"Mach task resident_size");}
    next.ProcessPrivateMemory={std::nullopt,MetricStatus::Unsupported,"Private commit equivalent not collected on macOS"};
#endif
    const auto cpu=CpuSeconds();
    if (initialized_ && now > sample_.TimestampSeconds && cpu >= previousCpu_ && previousCpu_ >= 0) {
        const auto logical=std::max(1u,
#if defined(_WIN32)
            static_cast<unsigned>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))
#else
            std::thread::hardware_concurrency()
#endif
        );
        next.ProcessCpuPercent=Measured(std::clamp((cpu-previousCpu_)/(now-sample_.TimestampSeconds)/logical*100,0.0,100.0),"Process CPU time / wall time / logical processors");
    }
    previousCpu_=cpu;sample_=std::move(next);initialized_=true;return sample_;
}
} // namespace lv
