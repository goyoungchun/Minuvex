#include <minuvex/Version.h>

namespace lv {
const BuildInfo& GetBuildInfo() noexcept {
    static constexpr BuildInfo info{Version, MINUVEX_BUILD_COMPILER,
        MINUVEX_BUILD_CONFIGURATION, sizeof(void*) * 8, bool(MINUVEX_BUILD_GRAPHICS)};
    return info;
}
} // namespace lv
