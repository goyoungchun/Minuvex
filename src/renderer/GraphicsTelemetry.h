#pragma once
#include <minvex/Telemetry.h>
#include <memory>

namespace lv::detail {
// Owns native timestamp queries, never waits/flushes. Destroy before graphics shutdown.
class GraphicsTelemetry {
public:
    GraphicsTelemetry();
    ~GraphicsTelemetry();
    const HardwareInfo& Hardware() const;
    SystemSample Sample(double now, FrameMonitor& frames);
    void BeginGpu(double now, FrameMonitor& frames);
    void EndGpu() noexcept;
    void ResetGpu();
    Metric<double> GpuTime() const;
private:
    void CollectGpu(FrameMonitor& frames);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
