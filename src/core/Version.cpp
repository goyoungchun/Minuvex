#include <minvex/Version.h>

namespace lv {
const BuildInfo& GetBuildInfo() noexcept {
    static constexpr BuildInfo info{Version, MINVEX_BUILD_COMPILER,
        MINVEX_BUILD_CONFIGURATION, sizeof(void*) * 8, bool(MINVEX_BUILD_GRAPHICS)};
    return info;
}
} // namespace lv
