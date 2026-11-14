#pragma once
#include <minvex/Ui.h>
#include <sokol_gfx.h>
namespace lv::detail {
struct UiRasterError : std::runtime_error {
    lv::Error Info;
    explicit UiRasterError(lv::Error error):std::runtime_error(error.Message),Info(std::move(error)) {}
};
class UiRenderer final {
public:
    UiRenderer();
    ~UiRenderer();
    void Composite(const UiCanvas& canvas,sg_view scene,int width,int height,float scale);
    sg_view Texture() const noexcept {return outputView_;}
    sg_image Image() const noexcept {return output_;}
    sg_image OverlayImage() const noexcept {return overlay_;}
    std::uint64_t PixelBytes() const noexcept {return static_cast<std::uint64_t>(width_)*height_*4;}
private:
    sg_shader shader_{};sg_pipeline pipeline_{};sg_sampler sampler_{};
    sg_image overlay_{},output_{};sg_view overlayView_{},outputView_{},attachment_{};
    std::uint64_t revision_=0;int width_=0,height_=0;float scale_=0;
    void ClearImages() noexcept;
};
}
