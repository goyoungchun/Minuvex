#pragma once
#include <minvex/Ui.h>
namespace lv::detail {
struct NativeTextUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
inline std::filesystem::path FontPath(const std::filesystem::path& directory,const std::filesystem::path& file) {
    if (file.empty()) {return {};}
    const auto root=std::filesystem::canonical(directory),path=std::filesystem::canonical(root/file);
    const auto relative=path.lexically_relative(root);
    if (relative.empty() || *relative.begin()==".." || std::filesystem::file_size(path)>64*1024*1024) {throw std::invalid_argument("Font file must remain inside Font directory and be at most 64 MiB.");}
    return path;
}
std::vector<UiFont> NativeFonts(const std::filesystem::path& file); // Empty = system.
struct NativeRun {std::string Text;UiTextStyle Style;UiColor Color;};
inline std::vector<NativeRun> TextRuns(const UiElement& element,const UiColor& color) {
    if (element.Runs.empty()) {return {{element.Text,element.Typography,color}};}
    std::vector<NativeRun> runs;
    for (const auto& r:element.Runs) {auto t=element.Typography;
        if (r.Family) {t.Family=*r.Family;}if (r.FontFile) {t.FontFile=*r.FontFile;}
        if (r.Size) {t.Size=*r.Size;}if (r.Weight) {t.Weight=*r.Weight;}if (r.LetterSpacing) {t.LetterSpacing=*r.LetterSpacing;}
        if (r.Italic) {t.Italic=*r.Italic;}if (r.Underline) {t.Underline=*r.Underline;}if (r.StrikeThrough) {t.StrikeThrough=*r.StrikeThrough;}
        runs.push_back({r.Text,std::move(t),r.Color.value_or(color)});
    }return runs;
}
inline bool HasText(const UiElement& e) {if (e.Runs.empty()) {return !e.Text.empty();}for (const auto& r:e.Runs) {if (!r.Text.empty()) {return true;}}return false;}
std::array<float,2> NativeMeasure(const UiElement& element,const std::filesystem::path& directory,float maxWidth);
std::vector<std::uint8_t> NativeText(const UiElement& element,const std::filesystem::path& directory,int width,int height,float scale,const UiColor& color);
}
