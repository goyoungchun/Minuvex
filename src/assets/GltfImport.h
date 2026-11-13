#pragma once

#include <minvex/Asset.h>

namespace lv::detail {
inline std::string PathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
std::shared_ptr<ModelAsset> ImportGltf(const std::filesystem::path& path, const ImportSettings& settings);
}
