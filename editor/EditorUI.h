#pragma once
#include "SceneViewport.h"
#include "CameraController.h"
#include <minuvex/EditorDocument.h>

namespace lv::editor {
class EditorUI final {
public:
    EditorDocument Document;
    SceneViewport Viewport;
    void Draw(); // Inside an ImGui frame, outside graphics passes.
    void Shutdown() noexcept { Viewport.Clear(); }
    void Import(const std::filesystem::path& path, bool place);
    void RequestOpen(const std::filesystem::path& path);
    void RequestNew();
    void RequestQuit();
    bool QuitRequested() const noexcept { return quit_; }
    void SetDirectory(std::filesystem::path path) { directory_ = std::move(path); }
    void ShowError(const Error& error);
private:
    enum class Action { None, New, Open, Quit } pending_ = Action::None;
    enum class Dialog { None, Import, Open, Save } dialog_ = Dialog::None;
    std::filesystem::path directory_ = std::filesystem::current_path(), openPath_, overwritePath_;
    std::string pathInput_, error_, status_ = "Import a glTF/GLB to begin.";
    std::string filter_;
    AssetId selectedAsset_ = 0;
    bool quit_ = false, resetLayout_ = false, cameraDrag_ = false;
    bool hierarchy_ = true, inspector_ = true, assets_ = true, viewport_ = true;
    detail::CameraController controls_;
    void ApplyPending();
    void Ask(Action action);
    void FileDialog(Dialog type);
    bool Save(const std::filesystem::path& path);
    void FrameScene();
    void Dockspace();
    void Menu();
    void Hierarchy();
    void Inspector();
    void AssetBrowser();
    void ViewportPanel();
    void Dialogs();
    template<typename T> bool Report(const Result<T>& result) {
        if (!result) { ShowError(result.Error()); return false; } return true;
    }
};
} // namespace lv::editor
