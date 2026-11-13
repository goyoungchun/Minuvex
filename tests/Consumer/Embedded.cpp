#include <minvex/EmbeddedRenderer.h>
// Link only minvex::embedded: public lifecycle requires no Host window.
int main() {
    lv::EmbeddedRenderer renderer;lv::Scene scene;
    const auto before=renderer.Render(scene,{});
    if (renderer.IsInitialized() || before || before.Error().Code != lv::ErrorCode::InvalidState) {return 1;}
    return renderer.Shutdown() ? 0:1;
}
