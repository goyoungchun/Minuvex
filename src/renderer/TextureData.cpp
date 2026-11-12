#include "TextureData.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <climits>

namespace lv::detail {
namespace {
std::vector<std::uint8_t> Bc3(std::span<const std::uint8_t> pixels,int width,int height) {
    std::vector<std::uint8_t> bytes;bytes.reserve(static_cast<std::size_t>((width+3)/4)*((height+3)/4)*16);
    for (int y=0;y<height;y+=4) {for (int x=0;x<width;x+=4) {
        std::array<std::array<int,4>,16> block;int low[4]{255,255,255,255},high[4]{};
        for (int i=0;i<16;++i) {const auto offset=(static_cast<std::size_t>(std::min(height-1,y+i/4))*width+std::min(width-1,x+i%4))*4;for (int c=0;c<4;++c) {block[i][c]=pixels[offset+c];low[c]=std::min(low[c],block[i][c]);high[c]=std::max(high[c],block[i][c]);}}
        bytes.push_back(static_cast<std::uint8_t>(high[3]));bytes.push_back(static_cast<std::uint8_t>(low[3]));int alpha[8]{high[3],low[3]};
        if (high[3]>low[3]) {for (int i=1;i<=6;++i) {alpha[i+1]=((7-i)*high[3]+i*low[3])/7;}}else {for (int i=1;i<=4;++i) {alpha[i+1]=((5-i)*high[3]+i*low[3])/5;}alpha[6]=0;alpha[7]=255;}
        std::uint64_t abits=0;for (int i=0;i<16;++i) {int nearest=0;for (int j=1;j<8;++j) {if (std::abs(block[i][3]-alpha[j])<std::abs(block[i][3]-alpha[nearest])) {nearest=j;}}abits|=std::uint64_t(nearest)<<(3*i);}for (int i=0;i<6;++i) {bytes.push_back(static_cast<std::uint8_t>(abits>>(8*i)));}
        const auto pack=[](const int* c) {return static_cast<std::uint16_t>(((c[0]*31+127)/255)<<11|((c[1]*63+127)/255)<<5|((c[2]*31+127)/255));};auto a=pack(high),b=pack(low);if (a<b) {std::swap(a,b);}for (auto c:{a,b}) {bytes.push_back(static_cast<std::uint8_t>(c));bytes.push_back(static_cast<std::uint8_t>(c>>8));}
        int palette[4][3]{};for (int j=0;j<2;++j) {const auto c=j ? b:a;palette[j][0]=((c>>11)&31)*255/31;palette[j][1]=((c>>5)&63)*255/63;palette[j][2]=(c&31)*255/31;}for (int c=0;c<3;++c) {palette[2][c]=(2*palette[0][c]+palette[1][c])/3;palette[3][c]=(palette[0][c]+2*palette[1][c])/3;}
        std::uint32_t bits=0;for (int i=0;i<16;++i) {int nearest=0,best=INT_MAX;for (int j=0;j<4;++j) {int error=0;for (int c=0;c<3;++c) {const int d=block[i][c]-palette[j][c];error+=d*d;}if (error<best) {best=error;nearest=j;}}bits|=static_cast<std::uint32_t>(nearest)<<(2*i);}for (int i=0;i<4;++i) {bytes.push_back(static_cast<std::uint8_t>(bits>>(8*i)));}
    }}return bytes;
}
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
PackedTexture PackTexture(const ImageAsset& image,int limit,bool srgb,bool compress) {
    if (image.Width <= 0 || image.Height <= 0 || limit <= 0 || image.Rgba.size() != static_cast<std::size_t>(image.Width)*image.Height*4) {throw std::invalid_argument("Invalid texture dimensions/data/limit.");}
    int w=image.Width,h=image.Height;std::span<const std::uint8_t> previous=image.Rgba;std::vector<std::uint8_t> owned;
    while (w > limit || h > limit) {auto next=Downsample(previous,w,h,srgb);w=std::max(1,w/2);h=std::max(1,h/2);owned=std::move(next);previous=owned;}
    PackedTexture packed;packed.Width=w;packed.Height=h;packed.Levels=1;packed.Compressed=compress && w>=4 && h>=4 && w%4==0 && h%4==0;
    const auto append=[&] {if (packed.Compressed) {const auto block=Bc3(previous,w,h);packed.Pixels.insert(packed.Pixels.end(),block.begin(),block.end());}else {packed.Pixels.insert(packed.Pixels.end(),previous.begin(),previous.end());}};append();
    while (w > 1 || h > 1) {auto next=Downsample(previous,w,h,srgb);owned=std::move(next);previous=owned;w=std::max(1,w/2);h=std::max(1,h/2);++packed.Levels;append();}
    return packed;
}
}
