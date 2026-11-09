#include "SceneViewport.h"
#include <algorithm>

namespace lv::editor {
void SceneViewport::Resize(int width,int height) {
    width_=std::clamp(width,0,65536);height_=std::clamp(height,0,65536);
    (void)surface_.Resize(scene_ ? *scene_:empty_,width_,height_);
}
void SceneViewport::Render(const Scene& scene) {
    scene_=&scene;(void)surface_.Resize(scene,width_,height_);surface_.Render(scene,detail::RenderClockSeconds());
}
Result<void> SceneViewport::SetQuality(const QualityProfile& profile,const Scene& scene) {
    scene_=&scene;return surface_.ApplyQuality(profile,scene,width_,height_);
}
}
