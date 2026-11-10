#pragma once
#include <json.hpp>
#include <filesystem>
#include <string_view>

namespace lv::detail {
nlohmann::json ReadJsonFile(const std::filesystem::path& path, std::size_t limit);
void WriteJsonFile(const nlohmann::json& json, const std::filesystem::path& path, std::size_t limit);
void WriteTextFile(std::string_view bytes, const std::filesystem::path& path, std::size_t limit);
}
