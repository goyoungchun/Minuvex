#include "ViewerState.h"

namespace lv::detail {
bool GraphicsRuntimeAvailable() noexcept {return false;}
Result<void> RunStandalone(Viewer&, ViewerState&, const ViewerRunOptions&) {
    return Result<void>::Failure({ErrorCode::UnsupportedFeature, "This Minvex build has no graphics runtime.", "Configure with MINVEX_BUILD_RENDERER=ON on Windows or macOS.", {}});
}
}
