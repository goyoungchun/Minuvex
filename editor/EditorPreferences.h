#pragma once
#include <minuvex/Result.h>
#include <vector>

namespace lv::editor {
enum class RecentKind { Model, Scene };
struct RecentFile {std::filesystem::path Path;RecentKind Kind=RecentKind::Model;};
class EditorPreferences {
public:
    std::filesystem::path Directory=std::filesystem::current_path();
    std::vector<RecentFile> Recent;
    void Touch(const std::filesystem::path& path,RecentKind kind);
    Result<void> Load(const std::filesystem::path& path);
    Result<void> Save(const std::filesystem::path& path) const;
};
} // namespace lv::editor
