#pragma once
#include "SceneRenderer.h"
#include "GraphicsQuality.h"
#include <sokol_gfx.h>

namespace lv::detail {
class RenderSurface final {
public:
    RenderSurface();
    ~RenderSurface();
    Result<void> ApplyQuality(const QualityProfile& profile,const Scene& scene,int width,int height);
    Result<void> Resize(const Scene& scene,int width,int height);
    bool Render(const Scene& scene,double now,bool force=false,const std::array<float,4>& clear={.025f,.035f,.055f,1});
    void Present() const; // In the native single-sample swapchain pass.
    void Invalidate() noexcept {scheduler_.Invalidate();}
    bool Due(double now) const;
    bool Applying() const noexcept {return applying_;}
    void Clear() noexcept;
    int Width() const noexcept {return state_.RenderWidth;}
    int Height() const noexcept {return state_.RenderHeight;}
    sg_view Texture() const noexcept;
    sg_image ColorImage() const noexcept;
    const QualityState& Quality() const noexcept {return state_;}
    SceneRenderStats Stats() const noexcept {return renderer_ ? renderer_->GetStats():SceneRenderStats{};}
    std::uint64_t RenderedFrames() const noexcept {return frames_;}
private:
    struct Targets;
    struct Presenter;
    std::unique_ptr<Targets> targets_;
    std::unique_ptr<Presenter> presenter_;
    std::unique_ptr<SceneRenderer> renderer_;
    QualityState state_;
    RenderScheduler scheduler_;
    int outputWidth_=0,outputHeight_=0,failedWidth_=-1,failedHeight_=-1;
    bool applying_=false;
    std::uint64_t frames_=0;
    Result<void> Fail(lv::Error error);
};
double RenderClockSeconds() noexcept;
}
