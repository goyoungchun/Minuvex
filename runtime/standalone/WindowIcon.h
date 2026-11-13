#pragma once

#if !defined(SOKOL_APP_INCLUDED)
#include <sokol_app.h>
#endif

namespace lv::detail {
// Image storage remains valid for the lifetime of the process.
const sapp_icon_desc& WindowIcon() noexcept;
void SetApplicationIcon();
}
