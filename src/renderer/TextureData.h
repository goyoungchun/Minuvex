#pragma once
#include <minuvex/Asset.h>
namespace lv::detail {
struct PackedTexture { int Width=0,Height=0,Levels=0;std::vector<std::uint8_t> Pixels; };
PackedTexture PackTexture(const ImageAsset& image,int limit);
}
