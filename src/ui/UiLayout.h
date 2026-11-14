#pragma once
#include <minvex/Ui.h>
#include <unordered_map>
namespace lv::detail {
using UiScrollMap=std::unordered_map<UiId,std::array<float,2>>;
struct UiResolved {
    UiElement Element;
    UiLayoutBox Box;
    int Parent=-1; // Index in the paint-ordered resolved vector.
};
void ValidateUiTree(const std::vector<UiElement>& items);
std::vector<UiResolved> BuildUiLayout(const std::vector<UiElement>& items,const std::filesystem::path& directory,float width,float height,const UiScrollMap& scroll);
std::array<float,2> UiLocal(const UiLayoutBox& box,float x,float y);
std::array<float,2> UiWorld(const UiLayoutBox& box,float x,float y);
bool UiClipped(const std::vector<UiResolved>& resolved,std::size_t index,float x,float y);
}
