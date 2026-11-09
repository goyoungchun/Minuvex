#pragma once
#include <json.hpp>
#include <filesystem>

namespace lv::detail {
nlohmann::json ReadJsonFile(const std::filesystem::path& path, std::size_t limit);
void WriteJsonFile(const nlohmann::json& json, const std::filesystem::path& path, std::size_t limit);
}
