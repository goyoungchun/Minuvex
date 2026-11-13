#include "WindowIcon.h"
#include "WindowIconData.h"

#include <stb_image.h>
#include <iterator>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

namespace lv::detail {
namespace {
struct IconImages {
    sapp_icon_desc Description{};
    IconImages() noexcept {
        int slot = 0;
        for (const auto& source : branding::Icons) {
            int width = 0, height = 0, channels = 0;
            auto* pixels = stbi_load_from_memory(source.Data, static_cast<int>(source.Size), &width, &height, &channels, 4);
            if (pixels) {
                auto& image = Description.images[slot++];
                image.width = width;
                image.height = height;
                image.pixels = {pixels, static_cast<std::size_t>(width) * height * 4};
            }
        }
    }
    ~IconImages() {
        for (const auto& image : Description.images) {
            stbi_image_free(const_cast<void*>(image.pixels.ptr));
        }
    }
};
static_assert(std::size(branding::Icons) <= SAPP_MAX_ICONIMAGES);
}

const sapp_icon_desc& WindowIcon() noexcept {
    static const IconImages icons;
    return icons.Description;
}

void SetApplicationIcon() {
#if defined(__APPLE__)
    // Sokol's window icon API does not cover macOS; set the Dock/app icon here.
    const auto& source = branding::Icons[std::size(branding::Icons) - 1];
    NSData* data = [NSData dataWithBytes:source.Data length:source.Size];
    NSImage* image = [[NSImage alloc] initWithData:data];
    if (image) { [NSApp setApplicationIconImage:image]; }
#endif
}
}
