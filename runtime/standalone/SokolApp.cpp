#define SOKOL_APP_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_LOG_IMPL
#include <sokol_gfx.h>
#include <sokol_app.h>
#include <sokol_glue.h>
#include <sokol_log.h>

#include "RuntimePlatform.h"
#include "WindowIcon.h"

#if defined(__APPLE__)
// The stock Sokol macOS loop terminates the process on window close. A
// standalone SDK must return to its caller so Run() can report errors and the
// application's stack can unwind. Keep this adapter next to the pinned Sokol
// implementation; the cleanup calls below depend on that exact version.
@interface MinvexStandaloneApplication : NSApplication
@end

@implementation MinvexStandaloneApplication
- (void)terminate:(id)sender {
    sapp_quit();
    [self stop:sender];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
        location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
        context:nil subtype:0 data1:0 data2:0];
    [self postEvent:wake atStart:YES];
}
@end
#endif

namespace lv::detail {

bool StandaloneMainThread() noexcept {
#if defined(__APPLE__)
    return [NSThread isMainThread];
#else
    return true;
#endif
}

bool StandaloneApplicationAvailable() noexcept {
#if defined(__APPLE__)
    return NSApp == nil || [NSApp isKindOfClass:[MinvexStandaloneApplication class]];
#else
    return true;
#endif
}

void RunStandaloneApplication(const sapp_desc& description) {
    auto branded = description;
    if (!branded.icon.sokol_default && !branded.icon.images[0].pixels.ptr) {
        branded.icon = WindowIcon();
    }
#if defined(__APPLE__)
    [MinvexStandaloneApplication sharedApplication];
#endif
    SetApplicationIcon();
    sapp_run(branded);
#if defined(__APPLE__)
    // stop: returns from NSApp.run without applicationWillTerminate. Perform
    // its Sokol cleanup here, before discarding the native device/window.
    _sapp_call_cleanup();
    NSApp.delegate = nil;
    _sapp.macos.window.delegate = nil;
    [_sapp.macos.window close];
    _sapp_macos_discard_state();
    _sapp_discard_state();
#endif
}

} // namespace lv::detail
