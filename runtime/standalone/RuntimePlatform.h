#pragma once

#if !defined(SOKOL_APP_INCLUDED)
#include <sokol_app.h>
#endif

namespace lv::detail {
bool StandaloneMainThread() noexcept;
bool StandaloneApplicationAvailable() noexcept;
void RunStandaloneApplication(const sapp_desc& description);
}
