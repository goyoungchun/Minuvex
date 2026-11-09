#pragma once
#include <filesystem>
#include <string_view>

namespace lv::detail {
inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
} // namespace lv::detail
