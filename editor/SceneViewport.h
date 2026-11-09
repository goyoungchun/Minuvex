#pragma once
#include "renderer/RenderSurface.h"
#include <algorithm>

namespace lv::editor {
class SceneViewport final {
public:
    void Resize(int width,int height);
    void Render(const Scene& scene);
    Result<void> SetQuality(const QualityProfile& profile,const Scene& scene);
    void Clear() noexcept {surface_.Clear();}
    int Width() const noexcept {return surface_.Width();}
    int Height() const noexcept {return surface_.Height();}
    float Aspect() const noexcept {return static_cast<float>(std::max(1,width_))/static_cast<float>(std::max(1,height_));}
    sg_view Texture() const noexcept {return surface_.Texture();}
    sg_image ColorImage() const noexcept {return surface_.ColorImage();}
    detail::SceneRenderStats Stats() const noexcept {return surface_.Stats();}
    const QualityState& Quality() const noexcept {return surface_.Quality();}
    bool ApplyingQuality() const noexcept {return surface_.Applying();}
    std::uint64_t RenderedFrames() const noexcept {return surface_.RenderedFrames();}
private:
    detail::RenderSurface surface_;
    Scene empty_;
    const Scene* scene_=nullptr;
    int width_=0,height_=0;
};
}
