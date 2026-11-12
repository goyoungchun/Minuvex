#include "EditorPreferences.h"
#include "core/JsonFile.h"
#include "core/Utf8Path.h"
#include <json.hpp>
#include <algorithm>

namespace lv::editor {
namespace {
std::string Utf8(const std::filesystem::path& path) {const auto s=path.generic_u8string();return {s.begin(),s.end()};}
std::filesystem::path Path(const nlohmann::json& value) {
    if (!value.is_string()) {throw std::invalid_argument("Preference path must be a string.");}
    auto text=value.get<std::string>();if (text.empty() || text.size() > 16384 || text.find('\0') != std::string::npos) {throw std::invalid_argument("Preference path is empty or exceeds its limit.");}
    return detail::PathFromUtf8(text);
}
}
void EditorPreferences::Touch(const std::filesystem::path& path,RecentKind kind) {
    const auto absolute=std::filesystem::absolute(path).lexically_normal();
    std::erase_if(Recent,[&](const auto& entry) {std::error_code error;return entry.Path == absolute || std::filesystem::equivalent(entry.Path,absolute,error);});
    Recent.insert(Recent.begin(),{absolute,kind});if (Recent.size() > 12) {Recent.resize(12);}Directory=absolute.parent_path();
}
Result<void> EditorPreferences::Load(const std::filesystem::path& path) {
    try {
        std::error_code ec;if (!std::filesystem::exists(path,ec) && !ec) {return Result<void>::Success();}
        const auto json=detail::ReadJsonFile(path,256*1024);
        if (json.at("format") != "MinuvexEditor" || !json.at("version").is_number_integer() || json.at("version") != 1 || !json.at("recent").is_array() || json.at("recent").size() > 12) {throw std::invalid_argument("Invalid Editor preferences format/version or recent list.");}
        EditorPreferences next;next.Directory=Path(json.at("directory"));
        for (const auto& entry:json.at("recent")) {const auto kind=entry.at("kind");if (kind != "model" && kind != "scene") {throw std::invalid_argument("Invalid recent file kind.");}next.Recent.push_back({Path(entry.at("path")),kind == "model" ? RecentKind::Model:RecentKind::Scene});}
        *this=std::move(next);return Result<void>::Success();
    }catch (const std::exception& e) {return Result<void>::Failure({ErrorCode::InvalidData,e.what(),"Preferences were not applied; existing Editor state was retained.",path});}
}
Result<void> EditorPreferences::Save(const std::filesystem::path& path) const {
    try {
        if (Recent.size() > 12) {throw std::invalid_argument("Too many recent files.");}
        nlohmann::json json{{"format","MinuvexEditor"},{"version",1},{"directory",Utf8(Directory)},{"recent",nlohmann::json::array()}};
        (void)Path(json["directory"]);
        for (const auto& entry:Recent) {if (entry.Kind != RecentKind::Model && entry.Kind != RecentKind::Scene) {throw std::invalid_argument("Invalid recent file kind.");}auto text=Utf8(entry.Path);(void)Path(text);json["recent"].push_back({{"path",text},{"kind",entry.Kind == RecentKind::Model ? "model":"scene"}});}
        detail::WriteJsonFile(json,path,256*1024);return Result<void>::Success();
    }catch (const std::exception& e) {return Result<void>::Failure({ErrorCode::SceneIo,e.what(),"Check the preferences path and write permission.",path});}
}
} // namespace lv::editor
