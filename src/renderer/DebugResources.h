#pragma once
#include <minvex/Debug.h>
#include <sokol_gfx.h>
#include <algorithm>
namespace lv::detail {
inline DebugResourceState DebugState(sg_resource_state s) {return s == SG_RESOURCESTATE_VALID ? DebugResourceState::Valid:s == SG_RESOURCESTATE_ALLOC ? DebugResourceState::Allocated:s == SG_RESOURCESTATE_FAILED ? DebugResourceState::Failed:DebugResourceState::Invalid;}
inline const char* DebugFormat(sg_pixel_format f) {switch (f) {case SG_PIXELFORMAT_RGBA8:return "RGBA8";case SG_PIXELFORMAT_SRGB8A8:return "SRGB8A8";case SG_PIXELFORMAT_BGRA8:return "BGRA8";case SG_PIXELFORMAT_SBGR8A8:return "SBGR8A8";case SG_PIXELFORMAT_RGBA16F:return "RGBA16F";case SG_PIXELFORMAT_R32F:return "R32F";case SG_PIXELFORMAT_BC3_RGBA:return "BC3_RGBA";case SG_PIXELFORMAT_BC3_SRGBA:return "BC3_SRGBA";case SG_PIXELFORMAT_DEPTH:return "DEPTH";case SG_PIXELFORMAT_DEPTH_STENCIL:return "DEPTH_STENCIL";default:return "Other";}}
inline void InspectImage(std::vector<DebugResource>& r,sg_image image,const char* name,bool target=false) {
    if (!image.id) {return;}const auto d=sg_query_image_desc(image);DebugResource x;x.Kind=target ? DebugResourceKind::RenderTarget:DebugResourceKind::Texture;x.Name=name;x.Format=DebugFormat(d.pixel_format);x.Id=image.id;x.State=DebugState(sg_query_image_state(image));x.Width=d.width;x.Height=d.height;x.Samples=d.sample_count;
    const bool compressed=d.pixel_format == SG_PIXELFORMAT_BC3_RGBA || d.pixel_format == SG_PIXELFORMAT_BC3_SRGBA;int w=d.width,h=d.height;
    for (int level=0;level<std::max(1,d.num_mipmaps);++level) {x.Bytes+=(compressed ? std::uint64_t((w+3)/4)*((h+3)/4)*16:std::uint64_t(w)*h*(d.pixel_format == SG_PIXELFORMAT_RGBA16F ? 8:4))*std::max(1,d.sample_count);w=std::max(1,w/2);h=std::max(1,h/2);}r.push_back(std::move(x));
}
inline void InspectBuffer(std::vector<DebugResource>& r,sg_buffer buffer,const char* name) {if (!buffer.id) {return;}DebugResource x;x.Kind=DebugResourceKind::Buffer;x.Name=name;x.Id=buffer.id;x.State=DebugState(sg_query_buffer_state(buffer));x.Bytes=sg_query_buffer_desc(buffer).size;r.push_back(std::move(x));}
inline void InspectShader(std::vector<DebugResource>& r,sg_shader shader,const char* name) {if (!shader.id) {return;}DebugResource x;x.Kind=DebugResourceKind::Shader;x.Name=name;x.Id=shader.id;x.State=DebugState(sg_query_shader_state(shader));r.push_back(std::move(x));}
inline void InspectPipeline(std::vector<DebugResource>& r,sg_pipeline p,const char* name) {if (!p.id) {return;}DebugResource x;x.Kind=DebugResourceKind::Pipeline;x.Name=name;x.Id=p.id;x.State=DebugState(sg_query_pipeline_state(p));r.push_back(std::move(x));}
}
