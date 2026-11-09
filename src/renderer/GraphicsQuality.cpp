#include "GraphicsQuality.h"
#include <sokol_gfx.h>
#include <bit>
#include <algorithm>
#if defined(_WIN32)
#include <d3d11.h>
#undef GetObject
#elif defined(__APPLE__)
#import <Metal/Metal.h>
#endif

namespace lv::detail {
QualityCapabilities QueryQualityCapabilities() {
    QualityCapabilities caps;if (!sg_isvalid()) {return caps;}
    const auto defaults=sg_query_desc().environment.defaults;
    const auto color=sg_query_pixelformat(defaults.color_format),depth=sg_query_pixelformat(defaults.depth_format);
    caps.GraphicsAvailable=color.render && color.sample && color.filter && depth.render;
    caps.MaxRenderSize=std::min(8192,sg_query_limits().max_image_size_2d);caps.MaxTextureSize=sg_query_limits().max_image_size_2d;
#if defined(_WIN32)
    caps.Backend=GraphicsBackend::D3D11;
    auto* device=static_cast<ID3D11Device*>(const_cast<void*>(sg_d3d11_device()));
    DXGI_FORMAT colorFormat=DXGI_FORMAT_UNKNOWN,depthFormat=DXGI_FORMAT_UNKNOWN;
    switch (defaults.color_format) {
    case SG_PIXELFORMAT_RGBA8:colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM;break;
    case SG_PIXELFORMAT_SRGB8A8:colorFormat=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;break;
    case SG_PIXELFORMAT_BGRA8:colorFormat=DXGI_FORMAT_B8G8R8A8_UNORM;break;
    case SG_PIXELFORMAT_SBGR8A8:colorFormat=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;break;
    default:break;
    }
    if (defaults.depth_format == SG_PIXELFORMAT_DEPTH) {depthFormat=DXGI_FORMAT_D32_FLOAT;}
    if (defaults.depth_format == SG_PIXELFORMAT_DEPTH_STENCIL) {depthFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;}
    if (color.msaa && depth.msaa && colorFormat != DXGI_FORMAT_UNKNOWN && depthFormat != DXGI_FORMAT_UNKNOWN) {
        for (int count:{2,4,8}) {UINT c=0,d=0;if (SUCCEEDED(device->CheckMultisampleQualityLevels(colorFormat,count,&c)) && SUCCEEDED(device->CheckMultisampleQualityLevels(depthFormat,count,&d)) && c && d) {caps.SampleCounts|=1u<<count;}}
    }
#elif defined(__APPLE__)
    caps.Backend=GraphicsBackend::Metal;id<MTLDevice> device=(__bridge id<MTLDevice>)sg_mtl_device();
    if (color.msaa && depth.msaa) {for (int count:{2,4,8}) {if ([device supportsTextureSampleCount:static_cast<NSUInteger>(count)]) {caps.SampleCounts|=1u<<count;}}}
#endif
    return caps;
}
std::uint64_t SceneRenderSignature(const Scene& scene) {
    std::uint64_t hash=14695981039346656037ull;
    const auto integer=[&](std::uint64_t value){for (int i=0;i<8;++i) {hash^=(value>>(i*8))&255;hash*=1099511628211ull;}};
    const auto scalar=[&](float value){integer(std::bit_cast<std::uint32_t>(value));};
    const auto vector=[&](Vec3 value){scalar(value.X);scalar(value.Y);scalar(value.Z);};
    const auto& c=scene.MainCamera;vector(c.Position);vector(c.Target);vector(c.Up);scalar(c.VerticalFovRadians);scalar(c.NearPlane);scalar(c.FarPlane);
    const auto& l=scene.Sun;vector(l.Direction);vector(l.Color);scalar(l.Intensity);scalar(l.Ambient);
    for (const auto& item:scene.CollectRenderItems()) {
        integer(item.Object);integer(reinterpret_cast<std::uintptr_t>(item.Model.get()));
        for (auto value:item.WorldTransform.Elements) {scalar(value);}
        if (item.Animation) {
            integer(item.Animation->Clip().has_value());integer(item.Animation->Clip().value_or(0));integer(std::bit_cast<std::uint64_t>(item.Animation->Time()));
            integer(item.Animation->Overrides().Joints.size());
            for (const auto& joint:item.Animation->Overrides().Joints) {integer(joint.Joint);vector(joint.Local.Position);vector(joint.Local.Scale);const auto& q=joint.Local.Rotation;scalar(q.X);scalar(q.Y);scalar(q.Z);scalar(q.W);}
        }
    } return hash;
}
bool SceneHasActiveAnimation(const Scene& scene) {
    for (const auto& item:scene.CollectRenderItems()) {if (item.Animation && item.Animation->State() == PlaybackState::Playing && item.Animation->Speed() != 0) {return true;}}return false;
}
}
