#include "DebugRenderer.h"
#include "DebugResources.h"
#include "MeshRenderer.h"
#include "debug.glsl.h"
#include <cstddef>
#include <stdexcept>
namespace lv::detail {
namespace {struct Vertex {Vec3 Position;DebugColor Color;};}
DebugRenderer::DebugRenderer(const RenderSettings& s) {
    try {
        shader_=sg_make_shader(debug_lines_shader_desc(sg_query_backend()));
        sg_pipeline_desc p{};p.shader=shader_;p.sample_count=s.MsaaSamples;p.colors[0].pixel_format=s.PostProcessing ? SG_PIXELFORMAT_RGBA16F:sg_query_desc().environment.defaults.color_format;p.primitive_type=SG_PRIMITIVETYPE_LINES;p.layout.buffers[0].stride=sizeof(Vertex);p.layout.attrs[ATTR_debug_lines_position].format=SG_VERTEXFORMAT_FLOAT3;p.layout.attrs[ATTR_debug_lines_position].offset=offsetof(Vertex,Position);p.layout.attrs[ATTR_debug_lines_color0].format=SG_VERTEXFORMAT_FLOAT4;p.layout.attrs[ATTR_debug_lines_color0].offset=offsetof(Vertex,Color);p.depth.write_enabled=false;p.colors[0].blend.enabled=true;p.colors[0].blend.src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA;p.colors[0].blend.dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;p.label="debug-lines-pipeline";
        for (int i=0;i<2;++i) {p.depth.compare=i ? SG_COMPAREFUNC_ALWAYS:SG_COMPAREFUNC_LESS_EQUAL;pipelines_[i]=sg_make_pipeline(p);}
        if (sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(pipelines_[0]) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(pipelines_[1]) != SG_RESOURCESTATE_VALID) {throw std::runtime_error("Cannot create debug drawing resources.");}
    }catch (...) {Destroy();throw;}
}
DebugRenderer::~DebugRenderer() {Destroy();}
void DebugRenderer::Destroy() noexcept {for (auto p:pipelines_) {if (p.id) {sg_destroy_pipeline(p);}}if (vertices_.id) {sg_destroy_buffer(vertices_);}if (shader_.id) {sg_destroy_shader(shader_);}}
void DebugRenderer::Prepare(const Scene& scene,const DebugOptions& o,const DebugDraw& commands) {
    DebugDraw draw=commands;
    if (o.ShowSkeleton || o.ShowBounds || o.ShowReferencePose || o.View == DebugView::Skeleton || o.View == DebugView::BoundingBox) {
        for (const auto& item:scene.CollectRenderItems()) {
            if (o.ShowSkeleton || o.View == DebugView::Skeleton) {const auto pose=item.Animation->Evaluate();draw.DrawSkeleton(item.Model->Skeleton,pose,item.WorldTransform,{1,.8f,0,1});for (std::size_t i=0;i<item.Model->Skeleton.Joints.size();++i) {if (item.Model->Skeleton.Joints[i].IsJoint) {draw.DrawAxis(item.WorldTransform*pose.Global[i],.05f);}}}
            if (o.ShowReferencePose) {AnimationPlayer reference;reference.Bind(item.Model);draw.DrawSkeleton(item.Model->Skeleton,reference.Evaluate(),item.WorldTransform,{0,1,1,1});}
            if (o.ShowBounds || o.View == DebugView::BoundingBox) {draw.DrawBoundingBox(item.Animation->GetBounds(),{0,1,0,1},item.WorldTransform);}
        }
    }
    std::vector<Vertex> vertices;vertices.reserve(draw.Lines().size()*2);for (const auto& l:draw.Lines()) {vertices.push_back({l.Start,l.Color});vertices.push_back({l.End,l.Color});}
    lines_=draw.Lines().size();if (!lines_) {return;}
    // Immutable per submission also permits multiple offscreen renders in one Sokol frame.
    sg_buffer_desc b{};b.data={vertices.data(),vertices.size()*sizeof(Vertex)};b.label="debug-line-vertices";const auto next=sg_make_buffer(b);
    if (sg_query_buffer_state(next) != SG_RESOURCESTATE_VALID) {if (next.id) {sg_destroy_buffer(next);}throw std::runtime_error("Cannot upload debug lines.");}
    if (vertices_.id) {sg_destroy_buffer(vertices_);}vertices_=next;offset_=0;
}
void DebugRenderer::Draw(const Mat4& vp,bool depthTest) {
    if (!lines_) {return;}sg_bindings b{};b.vertex_buffers[0]=vertices_;b.vertex_buffer_offsets[0]=offset_;debug_line_params_t params{};params.view_projection=vp;
    sg_apply_pipeline(pipelines_[depthTest ? 0:1]);sg_apply_bindings(b);sg_apply_uniforms(UB_debug_line_params,SG_RANGE(params));sg_draw(0,static_cast<int>(lines_*2),1);
}
void DebugRenderer::Inspect(std::vector<DebugResource>& r) const {InspectBuffer(r,vertices_,"Debug line stream");InspectShader(r,shader_,"debug_lines");for (auto p:pipelines_) {InspectPipeline(r,p,"Debug lines depth/overlay");}}
}
