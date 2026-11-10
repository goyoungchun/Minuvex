#include "TextureData.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace lv::detail {
namespace {
float Linear(float v) {return v <= .04045f ? v/12.92f : std::pow((v+.055f)/1.055f,2.4f);}
float Srgb(float v) {return v <= .0031308f ? v*12.92f : 1.055f*std::pow(v,1/2.4f)-.055f;}
std::vector<std::uint8_t> Downsample(std::span<const std::uint8_t> source,int width,int height,bool srgb) {
    const int nw=std::max(1,width/2),nh=std::max(1,height/2);std::vector<std::uint8_t> next(static_cast<std::size_t>(nw)*nh*4);
    for (int y=0;y<nh;++y) {for (int x=0;x<nw;++x) {
        float sum[4]{};int samples=0;
        for (int sy=y*height/nh;sy<(y+1)*height/nh;++sy) {for (int sx=x*width/nw;sx<(x+1)*width/nw;++sx) {
            const auto offset=(static_cast<std::size_t>(sy)*width+sx)*4;
            for (int c=0;c<4;++c) {const float v=source[offset+c]/255.0f;sum[c]+=srgb && c < 3 ? Linear(v):v;}++samples;
        }}
        for (int c=0;c<4;++c) {float v=sum[c]/static_cast<float>(samples);if (srgb && c<3) {v=Srgb(v);}next[(static_cast<std::size_t>(y)*nw+x)*4+c]=static_cast<std::uint8_t>(std::clamp(std::lround(v*255),0l,255l));}
    }}return next;
}
}
PackedTexture PackTexture(const ImageAsset& image,int limit,bool srgb) {
    if (image.Width <= 0 || image.Height <= 0 || limit <= 0 || image.Rgba.size() != static_cast<std::size_t>(image.Width)*image.Height*4) {throw std::invalid_argument("Invalid texture dimensions/data/limit.");}
    int w=image.Width,h=image.Height;std::span<const std::uint8_t> previous=image.Rgba;std::vector<std::uint8_t> owned;
    while (w > limit || h > limit) {auto next=Downsample(previous,w,h,srgb);w=std::max(1,w/2);h=std::max(1,h/2);owned=std::move(next);previous=owned;}
    PackedTexture packed;packed.Width=w;packed.Height=h;packed.Levels=1;packed.Pixels.assign(previous.begin(),previous.end());
    while (w > 1 || h > 1) {auto next=Downsample(previous,w,h,srgb);packed.Pixels.insert(packed.Pixels.end(),next.begin(),next.end());owned=std::move(next);previous=owned;w=std::max(1,w/2);h=std::max(1,h/2);++packed.Levels;}
    return packed;
}
}
