#pragma once

#include <minuvex/Result.h>
#include <minuvex/Scene.h>
#include <minuvex/Quality.h>
#include <minuvex/Telemetry.h>
#include <minuvex/Debug.h>
#include <minuvex/AssetLoading.h>

#include <functional>
#include <memory>

namespace lv {

namespace detail { struct ViewerState; }
class Viewer;

enum class ViewerInputType { MouseDown, MouseUp, MouseMove, Scroll, KeyDown, FocusLost, Resized };
enum class ViewerMouseButton { None, Left, Right, Middle };
enum class ViewerKey { Unknown, Escape, F, Space };

// Mouse deltas and viewport dimensions are in logical window pixels (HiDPI
// independent). FrameInfo uses actual framebuffer pixels instead.
struct ViewerInputEvent {
    ViewerInputType Type = ViewerInputType::MouseMove;
    ViewerMouseButton Button = ViewerMouseButton::None;
    ViewerKey Key = ViewerKey::Unknown;
    float DeltaX = 0, DeltaY = 0, ScrollY = 0;
    int Width = 0, Height = 0;
    bool Shift = false;
    bool Repeat = false;
};

struct CameraControlOptions {
    float OrbitSpeed = 0.005f; // Radians per logical pixel.
    float PanSpeed = 1.0f;
    float ZoomSpeed = 0.15f;
    float MinimumDistance = 0.0001f;
    float MaximumDistance = 100'000'000.0f;
};

struct ViewerOptions {
    int Width = 1280, Height = 720;
    std::string Title = "Minuvex Viewer"; // UTF-8, at most 126 bytes.
    bool HighDpi = true;
    bool VSync = true;
    bool EnableDefaultInput = true;
    bool AutoFrame = true;
    std::array<float, 4> ClearColor{0.035f, 0.045f, 0.065f, 1.0f};
    CameraControlOptions CameraControls;
    QualityProfile Quality;
};

struct ModelOptions {
    lv::Transform Transform;
    ObjectId Parent = NoObject;
    ImportSettings Import;
};

struct ViewerFrameInfo {
    std::uint64_t FrameNumber = 0; // Zero-based index of the upcoming frame.
    double DeltaTimeSeconds = 0;
    int FramebufferWidth = 0, FramebufferHeight = 0;
};

struct ViewerRunOptions {
    // Copied once at Run entry; callbacks may still capture mutable user state.
    // Zero runs until the window closes or RequestClose() is called.
    std::uint64_t FrameLimit = 0;
    // Runs before upload/draw. May mutate this Viewer's Scene or request exit.
    std::function<void(Viewer&, const ViewerFrameInfo&)> OnFrame;
    // Return true to consume a normalized event before the default controls.
    std::function<bool(Viewer&, const ViewerInputEvent&)> OnInput;
    bool AdvanceAnimations = true; // Advance before OnFrame; disable for manual Scene updates.
};

// Synchronous standalone facade. Construct/use on the owning thread, run on the
// application's main thread on macOS. RequestClose/IsRunning are thread-safe.
// One standalone event-loop session is supported per process; no ImGui required.
class Viewer final {
public:
    explicit Viewer(ViewerOptions options = {});
    ~Viewer();
    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;
    Viewer(Viewer&&) = delete;
    Viewer& operator=(Viewer&&) = delete;

    Result<ObjectId> AddModel(const std::filesystem::path& path, const ModelOptions& options = {});
    Result<void> RemoveModel(ObjectId id);
    Result<void> ReimportModel(ObjectId id); // Refresh the shared source asset across its Scene instances.
    Result<AssetLoadId> AddModelAsync(const std::filesystem::path& path,const ModelOptions& options={});
    Result<AssetLoadSnapshot> GetAssetLoad(AssetLoadId id) const;
    Result<ObjectId> CommitModelLoad(AssetLoadId id); // Ready candidate only; owner-thread Scene publication/GPU upload next render.
    Result<void> CancelModelLoad(AssetLoadId id); // Cancels/forgets this Viewer's pending AddModel request.
    Result<void> SetHotReload(bool enabled);
    Result<std::size_t> PollHotReload(double now); // Also pumped by the native loop before FPS/On-Demand checks.
    Result<std::optional<Error>> GetAssetLoadError() const;
    Result<void> SetTransform(ObjectId id, const Transform& transform);
    Result<void> SetVisible(ObjectId id, bool visible);
    Result<void> SetCamera(const Camera& camera); // Disables automatic framing.
    Result<void> SetLight(const DirectionalLight& light);
    Result<void> FrameScene();
    Result<void> PlayAnimation(ObjectId id, std::size_t clip);
    Result<void> PauseAnimation(ObjectId id);
    Result<void> StopAnimation(ObjectId id);
    Result<void> SetPose(ObjectId id, const PoseAsset& pose); // Manual override after animation.
    Result<void> ResetPose(ObjectId id);
    Result<void> ProcessInput(const ViewerInputEvent& event);
    Result<void> SetQuality(const QualityProfile& profile);
    Result<QualityState> GetQuality() const;
    Result<QualityCapabilities> GetQualityCapabilities() const;
    Result<TelemetrySnapshot> GetTelemetry() const; // Live on owner thread; last snapshot retained after Run.
    Result<void> RequestRedraw(); // Invalidate on-demand rendering on the owner thread.
    Result<void> SetDebugOptions(const DebugOptions& options);
    Result<DebugOptions> GetDebugOptions() const;
    Result<void> SetDebugView(DebugView view);
    Result<void> SetShowSkeleton(bool show);
    Result<void> SetDebugDraw(const DebugDraw& commands); // Copies persistent world-space lines; empty clears.
    Result<RenderDebugInfo> GetRenderDebugInfo() const; // Active graphics session only; diagnostic values, no GPU ownership.

    // Advanced API: owner thread only, retaining Phase 2's exception contracts.
    Scene& GetScene() noexcept;
    const Scene& GetScene() const noexcept;

    Result<void> Run(const ViewerRunOptions& options = {});
    void RequestClose() noexcept; // No effect outside an active Run().
    bool IsRunning() const noexcept;

private:
    std::unique_ptr<detail::ViewerState> state_;
};

} // namespace lv
