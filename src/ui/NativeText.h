#pragma once
#include <minvex/Ui.h>
namespace lv::detail {
inline std::filesystem::path FontPath(const std::filesystem::path& directory,const std::filesystem::path& file) {
    if (file.empty()) {return {};}
    const auto root=std::filesystem::canonical(directory),path=std::filesystem::canonical(root/file);
    const auto relative=path.lexically_relative(root);
    if (relative.empty() || *relative.begin()==".." || std::filesystem::file_size(path)>64*1024*1024) {throw std::invalid_argument("Font file must remain inside Font directory and be at most 64 MiB.");}
    return path;
}
std::vector<UiFont> NativeFonts(const std::filesystem::path& file); // Empty = system.
std::vector<std::uint8_t> NativeText(const UiElement& element,const std::filesystem::path& directory,int width,int height,float scale);
}
