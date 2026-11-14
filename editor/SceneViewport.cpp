#include "SceneViewport.h"
#include <algorithm>
#include <sokol_app.h>
#include "renderer/UiRenderer.h"

namespace lv::editor {
void SceneViewport::Resize(int width,int height) {
    width_=std::clamp(width,0,65536);height_=std::clamp(height,0,65536);
    (void)surface_.Resize(scene_ ? *scene_:empty_,width_,height_);
}
void SceneViewport::Render(const Scene& scene) {
    scene_=&scene;(void)surface_.Resize(scene,width_,height_);
    const bool retry=!UiError||failedUiRevision_!=Ui.Revision();surface_.SetUi(retry ? &Ui:nullptr,sapp_dpi_scale());
    try {surface_.Render(scene,detail::RenderClockSeconds());if (retry) {UiError.reset();}}
    catch (const detail::UiRasterError& error) {UiError=error.Info;failedUiRevision_=Ui.Revision();surface_.SetUi(nullptr);surface_.Render(scene,detail::RenderClockSeconds(),true,{.025f,.035f,.055f,1},true);}
}
Result<void> SceneViewport::SetQuality(const QualityProfile& profile,const Scene& scene) {
    scene_=&scene;return surface_.ApplyQuality(profile,scene,width_,height_);
}
}
