#pragma once
#include <minuvex/Result.h>
#include <functional>
#include <optional>
#include <vector>

namespace lv::editor {
enum class FilePickerMode { Open, Multiple, Save, Folder };
enum class FilePickerFilter { Model, Scene, Pose, Quality, Json, Csv };
struct FilePickerRequest {
    FilePickerMode Mode = FilePickerMode::Open;
    FilePickerFilter Filter = FilePickerFilter::Model;
    std::filesystem::path Directory;
    std::string Name, Title;
};
using FilePickerResult = Result<std::optional<std::vector<std::filesystem::path>>>;
using FilePicker = std::function<FilePickerResult(const FilePickerRequest&)>;
// Main/UI thread only. Successful nullopt means normal user cancellation.
class NativeFiles final {
public:
    NativeFiles();
    ~NativeFiles();
    NativeFiles(const NativeFiles&) = delete;
    NativeFiles& operator=(const NativeFiles&) = delete;
    FilePickerResult Choose(const FilePickerRequest& request) const;
    static Result<void> OpenPath(const std::filesystem::path& path);
private:
    bool initialized_ = false;
    std::string error_;
};
} // namespace lv::editor
