#pragma once
#include <minuvex/Quality.h>

namespace lv {
enum class MetricStatus { Measured, Estimated, Unsupported, Unavailable };
template<class T> struct Metric {
    std::optional<T> Value;
    MetricStatus Status = MetricStatus::Unavailable;
    std::string Source;
};
const char* MetricStatusName(MetricStatus status) noexcept;
enum class GpuMemoryKind { Unknown, Dedicated, Shared, Unified };
struct GraphicsFormatSupport {
    std::string Name;
    bool Sampled = false, Filtered = false, RenderTarget = false, Msaa = false;
};
struct HardwareInfo {
    std::string CpuName, OperatingSystem, OsVersion, Architecture, GpuName, GpuVendor;
    std::uint32_t GpuVendorId = 0, GpuDeviceId = 0; // Zero means unavailable.
    unsigned LogicalProcessors = 0, PhysicalCores = 0;
    Metric<std::uint64_t> TotalSystemMemory, DedicatedVideoMemory, SharedSystemMemory;
    GraphicsBackend Backend = GraphicsBackend::None;
    GpuMemoryKind MemoryKind = GpuMemoryKind::Unknown;
    bool ActiveGraphicsDevice = false; // QueryHardware enumerates; runtime selects its actual device.
    QualityCapabilities Capabilities;
    std::vector<GraphicsFormatSupport> Formats; // Renderer-relevant formats, queried only with active graphics.
};
struct SystemSample {
    double TimestampSeconds = 0;
    Metric<std::uint64_t> AvailableSystemMemory, ProcessResidentMemory, ProcessPrivateMemory;
    Metric<std::uint64_t> GpuMemoryUsage, GpuMemoryBudget, GpuNonLocalUsage, GpuNonLocalBudget;
    Metric<double> ProcessCpuPercent, GpuUtilizationPercent;
};
class SystemInfo {
public:
    static HardwareInfo QueryHardware(); // No window/context required. Missing fields stay unknown.
};
// Single-threaded sampler. Caller supplies monotonic seconds; expensive OS queries at most once/sec.
class SystemMonitor {
public:
    const SystemSample& Sample(double now);
private:
    SystemSample sample_;
    bool initialized_ = false;
    double previousCpu_ = 0;
};
} // namespace lv
