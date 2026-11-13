#include "GraphicsQuality.h"
#include <sokol_gfx.h>
#include <bit>
#include <algorithm>
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#endif
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
    caps.PbrLighting=caps.GraphicsAvailable;
    caps.MaxRenderSize=std::min(8192,sg_query_limits().max_image_size_2d);caps.MaxTextureSize=sg_query_limits().max_image_size_2d;
    const auto shadow=sg_query_pixelformat(SG_PIXELFORMAT_R32F),shadowDepth=sg_query_pixelformat(SG_PIXELFORMAT_DEPTH),hdr=sg_query_pixelformat(SG_PIXELFORMAT_RGBA16F);
    caps.ShadowMapping=shadow.render && shadow.sample && shadowDepth.render;
    caps.HdrPostProcessing=hdr.render && hdr.sample && hdr.filter;
    const auto bc3=sg_query_pixelformat(SG_PIXELFORMAT_BC3_RGBA),sbc3=sg_query_pixelformat(SG_PIXELFORMAT_BC3_SRGBA);caps.Bc3Textures=bc3.sample && bc3.filter && sbc3.sample && sbc3.filter;
#if defined(__EMSCRIPTEN__)
    caps.Backend=GraphicsBackend::WebGL2;caps.ShadowMapping=false;caps.HdrPostProcessing=false;
    caps.Bc3Textures=false;caps.MaxAnisotropy=1;
    GLint renderbufferSize=0;glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&renderbufferSize);caps.MaxRenderSize=std::min(caps.MaxRenderSize,renderbufferSize);
#endif
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
    if (hdr.msaa && depth.msaa && depthFormat != DXGI_FORMAT_UNKNOWN) {for (int count:{2,4,8}) {UINT c=0,d=0;if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT,count,&c)) && SUCCEEDED(device->CheckMultisampleQualityLevels(depthFormat,count,&d)) && c && d) {caps.HdrSampleCounts|=1u<<count;}}}
#elif defined(__APPLE__)
    caps.Backend=GraphicsBackend::Metal;id<MTLDevice> device=(__bridge id<MTLDevice>)sg_mtl_device();
    if (color.msaa && depth.msaa) {for (int count:{2,4,8}) {if ([device supportsTextureSampleCount:static_cast<NSUInteger>(count)]) {caps.SampleCounts|=1u<<count;}}}
    if (hdr.msaa && depth.msaa) {for (int count:{2,4,8}) {if ([device supportsTextureSampleCount:static_cast<NSUInteger>(count)]) {caps.HdrSampleCounts|=1u<<count;}}}
#endif
    return caps;
}
std::uint64_t SceneRenderSignature(const Scene& scene) {
    std::uint64_t hash=14695981039346656037ull;
    const auto integer=[&](std::uint64_t value){for (int i=0;i<8;++i) {hash^=(value>>(i*8))&255;hash*=1099511628211ull;}};
    const auto scalar=[&](float value){integer(std::bit_cast<std::uint32_t>(value));};
    const auto vector=[&](Vec3 value){scalar(value.X);scalar(value.Y);scalar(value.Z);};
    const auto& c=scene.MainCamera;vector(c.Position);vector(c.Target);vector(c.Up);scalar(c.VerticalFovRadians);scalar(c.NearPlane);scalar(c.FarPlane);
    integer(static_cast<unsigned>(c.Projection));scalar(c.OrthographicHeight);
    const auto& l=scene.Sun;vector(l.Direction);vector(l.Color);scalar(l.Intensity);scalar(l.Ambient);
    integer(scene.PointLights.size());for (const auto& light:scene.PointLights) {vector(light.Position);vector(light.Color);scalar(light.Intensity);scalar(light.Range);}
    integer(scene.SpotLights.size());for (const auto& light:scene.SpotLights) {vector(light.Position);vector(light.Direction);vector(light.Color);scalar(light.Intensity);scalar(light.Range);scalar(light.InnerConeRadians);scalar(light.OuterConeRadians);}
    for (const auto& item:scene.CollectRenderItems()) {
        integer(item.Object);integer(reinterpret_cast<std::uintptr_t>(item.Model.get()));
        if (item.MaterialOverrides) {integer(item.MaterialOverrides->size());for (std::size_t index=0;index<item.Model->Materials.size();++index) {const auto found=item.MaterialOverrides->find(index);if (found == item.MaterialOverrides->end()) {continue;}integer(index);const auto& m=found->second;for (auto value:m.BaseColor) {scalar(value);}scalar(m.AlphaCutoff);scalar(m.NormalScale);scalar(m.MetallicFactor);scalar(m.RoughnessFactor);integer(static_cast<unsigned>(m.Shading));integer(m.Unlit);integer(m.DoubleSided);for (auto texture:{m.BaseColorTexture,m.NormalTexture,m.MetallicRoughnessTexture}) {integer(texture.has_value());integer(texture.value_or(0));}}}
        for (auto value:item.WorldTransform.Elements) {scalar(value);}
        if (item.Pose) { for (const auto& matrix : item.Pose->Global) { for (auto value : matrix.Elements) { scalar(value); } } }
        if (item.Animation) {
            integer(item.Animation->Clip().has_value());integer(item.Animation->Clip().value_or(0));integer(std::bit_cast<std::uint64_t>(item.Animation->Time()));
            integer(item.Animation->Tree().has_value());if (const auto& tree=item.Animation->Tree()) {integer(tree->Tree->Signature());integer(std::bit_cast<std::uint64_t>(tree->Phase));scalar(tree->X);scalar(tree->Y);}
            integer(item.Animation->Additive().has_value());if (const auto& a=item.Animation->Additive()) {integer(a->Clip);scalar(a->Weight);integer(a->JointMask.size());for (auto weight:a->JointMask) {scalar(weight);}if (a->Weight>0 && (a->JointMask.empty() || std::any_of(a->JointMask.begin(),a->JointMask.end(),[](float weight){return weight>0;}))) {integer(std::bit_cast<std::uint64_t>(a->Time));}integer(a->Reference.SkeletonSignature);integer(a->Reference.Joints.size());for (const auto& joint:a->Reference.Joints) {integer(joint.Joint);vector(joint.Local.Position);vector(joint.Local.Scale);const auto& q=joint.Local.Rotation;scalar(q.X);scalar(q.Y);scalar(q.Z);scalar(q.W);}}
            integer(item.Animation->Blend().has_value());
            if (const auto& blend=item.Animation->Blend()) {integer(blend->SourceClip.has_value());integer(blend->SourceClip.value_or(0));for (auto value:{blend->SourceTime,blend->Duration,blend->Elapsed}) {integer(std::bit_cast<std::uint64_t>(value));}integer(blend->SourcePose.Joints.size());for (const auto& joint:blend->SourcePose.Joints) {integer(joint.Joint);vector(joint.Local.Position);vector(joint.Local.Scale);const auto& q=joint.Local.Rotation;scalar(q.X);scalar(q.Y);scalar(q.Z);scalar(q.W);}}
            const auto morphs=[&](const auto& poses) {integer(poses.size());for (const auto& pose:poses) {integer(pose.Node);for (auto weight:pose.Weights) {scalar(weight);}}};morphs(item.Animation->MorphOverrides());if (item.Animation->Blend()) {morphs(item.Animation->Blend()->SourceMorph);}
            integer(item.Animation->Overrides().Joints.size());
            for (const auto& joint:item.Animation->Overrides().Joints) {integer(joint.Joint);vector(joint.Local.Position);vector(joint.Local.Scale);const auto& q=joint.Local.Rotation;scalar(q.X);scalar(q.Y);scalar(q.Z);scalar(q.W);}
        }
    } return hash;
}
bool SceneHasActiveAnimation(const Scene& scene) {
    for (const auto& item : scene.CollectRenderItems()) {
        auto driver = item.Object;
        while (scene.GetObject(driver).LeaderPose) { driver = scene.GetObject(driver).LeaderPose; }
        const auto& player = scene.GetObject(driver).Animation;
        if (player.Speed() == 0) { continue; }
        if (player.State() == PlaybackState::Playing) { return true; }
        if (const auto& a = player.Additive(); a && a->State == PlaybackState::Playing && a->Speed != 0 && a->Weight > 0 &&
            (a->JointMask.empty() || std::any_of(a->JointMask.begin(), a->JointMask.end(), [](float weight) { return weight > 0; }))) { return true; }
    }
    return false;
}
}
