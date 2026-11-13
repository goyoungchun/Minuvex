#pragma once
#include <json.hpp>
#include <filesystem>
#include <string_view>

namespace lv::detail {
// New documents use Minvex; accept pre-rename format tags when reading.
bool MatchesDocumentFormat(const nlohmann::json& value, std::string_view expected);
nlohmann::json ReadJsonFile(const std::filesystem::path& path, std::size_t limit);
void WriteJsonFile(const nlohmann::json& json, const std::filesystem::path& path, std::size_t limit);
void WriteTextFile(std::string_view bytes, const std::filesystem::path& path, std::size_t limit);
}
