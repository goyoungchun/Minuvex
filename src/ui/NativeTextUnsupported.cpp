#include "NativeText.h"
namespace lv::detail {
std::vector<UiFont> NativeFonts(const std::filesystem::path&) {throw std::runtime_error("Native fonts require Windows or macOS.");}
std::array<float,2> NativeMeasure(const UiElement&,const std::filesystem::path&,float) {throw NativeTextUnavailable("Native text requires Windows or macOS.");}
std::vector<std::uint8_t> NativeText(const UiElement&,const std::filesystem::path&,int,int,float,const UiColor&) {throw std::runtime_error("Native text requires Windows or macOS.");}
}
