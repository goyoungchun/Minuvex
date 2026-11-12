#include "NativeFiles.h"
#include "core/Utf8Path.h"
#include <nfd.h>
#include <sokol_app.h>
#include <memory>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#endif

namespace lv::editor {
namespace {
Error Failure(const char* message) {return {ErrorCode::InvalidState,message ? message:"Native file dialog failed.","Choose an accessible path using the Editor's main window.",{}};}
std::string Utf8(const std::filesystem::path& path) {const auto s=path.generic_u8string();return {s.begin(),s.end()};}
nfdwindowhandle_t Parent() {
#if defined(_WIN32)
    return {NFD_WINDOW_HANDLE_TYPE_WINDOWS,const_cast<void*>(sapp_win32_get_hwnd())};
#else
    return {NFD_WINDOW_HANDLE_TYPE_COCOA,const_cast<void*>(sapp_macos_get_window())};
#endif
}
}
NativeFiles::NativeFiles() {initialized_=NFD_Init() == NFD_OKAY;if (!initialized_) {error_=NFD_GetError() ? NFD_GetError():"Cannot initialize native dialogs.";}}
NativeFiles::~NativeFiles() {if (initialized_) {NFD_Quit();}}
FilePickerResult NativeFiles::Choose(const FilePickerRequest& request) const {
    if (!initialized_) {return FilePickerResult::Failure(Failure(error_.c_str()));}
    try {
        nfdu8filteritem_t filters[2]{};nfdfiltersize_t count=1;
        switch (request.Filter) {
        case FilePickerFilter::Model:filters[0]={"Supported models (glTF / GLB)","gltf,glb"};filters[1]={"Roadmap formats (Phase 6)","obj,fbx"};count=2;break;
        case FilePickerFilter::Scene:filters[0]={"Minuvex Scene (*.mvxscene.json)","json"};break;
        case FilePickerFilter::Pose:filters[0]={"Minuvex Pose (*.mvxpose.json)","json"};break;
        case FilePickerFilter::Quality:filters[0]={"Minuvex Quality (*.mvxquality.json)","json"};break;
        case FilePickerFilter::Json:filters[0]={"JSON","json"};break;
        case FilePickerFilter::Csv:filters[0]={"CSV","csv"};break;
        }
        std::error_code ec;const auto directory=std::filesystem::is_directory(request.Directory,ec) ? Utf8(request.Directory):std::string{};
        const char* defaults=directory.empty() ? nullptr:directory.c_str();
        nfdu8char_t* path=nullptr;nfdresult_t result=NFD_ERROR;
        if (request.Mode == FilePickerMode::Multiple) {
            nfdopendialogu8args_t args{};args.filterList=filters;args.filterCount=count;args.defaultPath=defaults;args.parentWindow=Parent();args.title=request.Title.c_str();
            const nfdpathset_t* paths=nullptr;result=NFD_OpenDialogMultipleU8_With(&paths,&args);
            std::unique_ptr<const nfdpathset_t,decltype(&NFD_PathSet_Free)> owner(paths,NFD_PathSet_Free);
            if (result == NFD_CANCEL) {return FilePickerResult::Success(std::nullopt);}
            if (result != NFD_OKAY) {return FilePickerResult::Failure(Failure(NFD_GetError()));}
            nfdpathsetsize_t size=0;if (NFD_PathSet_GetCount(paths,&size) != NFD_OKAY) {return FilePickerResult::Failure(Failure(NFD_GetError()));}
            std::vector<std::filesystem::path> selected;selected.reserve(size);
            for (nfdpathsetsize_t i=0;i<size;++i) {
                if (NFD_PathSet_GetPathU8(paths,i,&path) != NFD_OKAY) {return FilePickerResult::Failure(Failure(NFD_GetError()));}
                std::unique_ptr<const nfdu8char_t,decltype(&NFD_PathSet_FreePathU8)> file(path,NFD_PathSet_FreePathU8);
                selected.push_back(detail::PathFromUtf8(path));
            }
            return FilePickerResult::Success(std::move(selected));
        }
        if (request.Mode == FilePickerMode::Save) {
            nfdsavedialogu8args_t args{};args.filterList=filters;args.filterCount=count;args.defaultPath=defaults;args.defaultName=request.Name.c_str();args.parentWindow=Parent();args.title=request.Title.c_str();
            result=NFD_SaveDialogU8_With(&path,&args);
        } else if (request.Mode == FilePickerMode::Folder) {
            nfdpickfolderu8args_t args{};args.defaultPath=defaults;args.parentWindow=Parent();args.title=request.Title.c_str();result=NFD_PickFolderU8_With(&path,&args);
        } else {
            nfdopendialogu8args_t args{};args.filterList=filters;args.filterCount=count;args.defaultPath=defaults;args.parentWindow=Parent();args.title=request.Title.c_str();result=NFD_OpenDialogU8_With(&path,&args);
        }
        std::unique_ptr<nfdu8char_t,decltype(&NFD_FreePathU8)> owner(path,NFD_FreePathU8);
        if (result == NFD_CANCEL) {return FilePickerResult::Success(std::nullopt);}
        if (result != NFD_OKAY) {return FilePickerResult::Failure(Failure(NFD_GetError()));}
        return FilePickerResult::Success(std::vector<std::filesystem::path>{detail::PathFromUtf8(path)});
    }catch (const std::exception& e) {return FilePickerResult::Failure(Failure(e.what()));}
}
Result<void> NativeFiles::OpenPath(const std::filesystem::path& path) {
    std::error_code ec;if (!std::filesystem::exists(path,ec)) {return Result<void>::Failure({ErrorCode::InvalidArgument,"The requested file/folder does not exist.","Check its path or open the documentation in the source checkout.",path});}
#if defined(_WIN32)
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)) > 32) {return Result<void>::Success();}
#else
    NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:Utf8(path).c_str()]];
    if ([[NSWorkspace sharedWorkspace] openURL:url]) {return Result<void>::Success();}
#endif
    return Result<void>::Failure({ErrorCode::InvalidState,"Cannot open the file/folder with its system application.","Open the path manually in your file explorer.",path});
}
} // namespace lv::editor
