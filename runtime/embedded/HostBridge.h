#pragma once
#include <minvex/EmbeddedRenderer.h>
#include <sokol_gfx.h>

namespace lv::detail {
sg_pixel_format EmbeddedColor(EmbeddedColorFormat format);
sg_pixel_format EmbeddedDepth(EmbeddedDepthFormat format);
class HostStateScope {
public:
    virtual ~HostStateScope() = default;
};
class HostBridge {
public:
    explicit HostBridge(const HostGraphicsContext& context);
    ~HostBridge();
    std::unique_ptr<HostStateScope> Enter();
    Result<void> ValidateTarget(const HostRenderTarget& target) const;
    Result<void> WaitIdle(std::uint32_t timeoutMilliseconds);
    sg_environment Environment() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lv::detail
