#pragma once
#include <minuvex/Debug.h>
#include <minuvex/Telemetry.h>
#include <memory>

namespace lv {
enum class EmbeddedColorFormat { Rgba8, Rgba8Srgb, Bgra8, Bgra8Srgb };
enum class EmbeddedDepthFormat { Depth32Float, DepthStencil }; // D32_FLOAT / D32_FLOAT_S8X24_UINT on D3D11.
struct HostGraphicsContext {
    GraphicsBackend Backend = GraphicsBackend::None;
    const void* Device = nullptr; // ID3D11Device* or (__bridge const void*) id<MTLDevice>.
    const void* ImmediateContext = nullptr; // D3D11 immediate context; null for Metal.
    EmbeddedColorFormat ColorFormat = EmbeddedColorFormat::Rgba8Srgb;
    EmbeddedDepthFormat DepthFormat = EmbeddedDepthFormat::Depth32Float;
};
struct HostRenderTarget {
    // Borrowed typed 2D, one mip/slice/sample, same device as Initialize.
    const void* ColorTexture = nullptr; // ID3D11Texture2D* or id<MTLTexture>.
    const void* DepthTexture = nullptr; // Optional; Scene depth stays internal.
    int Width = 0, Height = 0; // Both zero suspends and detaches; otherwise 1..8192.
};
struct EmbeddedOptions {
    QualityProfile Quality;
};
struct EmbeddedViewport {
    int X = 0, Y = 0, Width = 0, Height = 0; // Top-left framebuffer pixels; all zero = full target.
};
enum class EmbeddedTargetLoad { Clear, Preserve };
struct EmbeddedFrameOptions {
    std::array<float,4> ClearColor{.035f,.045f,.065f,1};
    bool ForceRedraw = false; // Bypass cached/on-demand/FPS scheduling for this call.
    EmbeddedViewport Viewport;
    EmbeddedTargetLoad TargetLoad = EmbeddedTargetLoad::Clear; // Clear entire target, or load Host contents.
};
struct EmbeddedFrameResult {
    bool SceneRendered = false, Presented = false;
    int RenderWidth = 0, RenderHeight = 0;
};
// Host owns the window, event loop, device and target. Minuvex owns its internal
// Sokol session/resources, never presents a window. One active graphics session/process.
// Construct/use/shutdown/destroy on the owning render thread; no concurrent native use.
class EmbeddedRenderer final {
public:
    EmbeddedRenderer();
    ~EmbeddedRenderer();
    EmbeddedRenderer(const EmbeddedRenderer&) = delete;
    EmbeddedRenderer& operator=(const EmbeddedRenderer&) = delete;
    EmbeddedRenderer(EmbeddedRenderer&&) = delete;
    EmbeddedRenderer& operator=(EmbeddedRenderer&&) = delete;
    Result<void> Initialize(const HostGraphicsContext& context, const EmbeddedOptions& options = {});
    Result<void> Update(Scene& scene, double deltaSeconds); // Advance exactly when the Host calls it.
    Result<EmbeddedFrameResult> Render(const Scene& scene, const HostRenderTarget& target,
                                       const EmbeddedFrameOptions& options = {});
    Result<void> SetQuality(const QualityProfile& profile, const Scene& scene);
    Result<QualityState> GetQuality() const;
    Result<QualityCapabilities> GetQualityCapabilities() const;
    Result<TelemetrySnapshot> GetTelemetry() const;
    Result<void> RequestRedraw();
    Result<void> SetDebugOptions(const DebugOptions& options);
    Result<void> SetDebugDraw(const DebugDraw& commands);
    Result<RenderDebugInfo> GetRenderDebugInfo() const;
    Result<void> DetachTarget(); // Release Minuvex's target references before Host resize/replacement.
    Result<void> WaitIdle(std::uint32_t timeoutMilliseconds = 5000); // Explicit GPU completion boundary.
    Result<const void*> GetMetalCommandQueue() const; // Borrowed id<MTLCommandQueue>; valid until Shutdown.
    Result<void> Shutdown(); // Idempotent; releases only Minuvex's resources/references.
    bool IsInitialized() const noexcept; // Thread-safe query; other calls require the owner thread.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lv
