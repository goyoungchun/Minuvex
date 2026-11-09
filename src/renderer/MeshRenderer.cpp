#include "renderer/MeshRenderer.h"

#include "mesh.glsl.h"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace lv::detail {

static_assert(sizeof(Mat4) == 64 && std::is_trivially_copyable_v<Mat4>);
static_assert(std::is_standard_layout_v<Vertex>);

MeshRenderer::~MeshRenderer() {
    Shutdown();
}

void MeshRenderer::Initialize(const MeshData& mesh) {
    if (vertices_.id || indices_.id || shader_.id || pipeline_.id) {
        throw std::logic_error("MeshRenderer is already initialized.");
    }
    if (!sg_isvalid()) {
        throw std::logic_error("Initialize Sokol GFX before creating the mesh renderer.");
    }
    ValidateMesh(mesh);

    sg_buffer_desc vertexDesc{};
    vertexDesc.data = {mesh.Vertices.data(), mesh.Vertices.size() * sizeof(Vertex)};
    vertexDesc.label = "minuvex-mesh-vertices";
    vertices_ = sg_make_buffer(vertexDesc);

    sg_buffer_desc indexDesc{};
    indexDesc.usage.index_buffer = true;
    indexDesc.data = {mesh.Indices.data(), mesh.Indices.size() * sizeof(std::uint16_t)};
    indexDesc.label = "minuvex-mesh-indices";
    indices_ = sg_make_buffer(indexDesc);
    shader_ = sg_make_shader(mesh_shader_desc(sg_query_backend()));

    if (sg_query_buffer_state(vertices_) != SG_RESOURCESTATE_VALID ||
        sg_query_buffer_state(indices_) != SG_RESOURCESTATE_VALID ||
        sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID) {
        Shutdown();
        throw std::runtime_error("Failed to create mesh buffers or shader. See Sokol diagnostics.");
    }

    sg_pipeline_desc pipelineDesc{};
    pipelineDesc.shader = shader_;
    pipelineDesc.layout.buffers[0].stride = sizeof(Vertex);
    pipelineDesc.layout.attrs[ATTR_mesh_position].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.attrs[ATTR_mesh_position].offset = offsetof(Vertex, Position);
    pipelineDesc.layout.attrs[ATTR_mesh_normal].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.attrs[ATTR_mesh_normal].offset = offsetof(Vertex, Normal);
    pipelineDesc.layout.attrs[ATTR_mesh_color0].format = SG_VERTEXFORMAT_FLOAT4;
    pipelineDesc.layout.attrs[ATTR_mesh_color0].offset = offsetof(Vertex, Color);
    pipelineDesc.index_type = SG_INDEXTYPE_UINT16;
    pipelineDesc.cull_mode = SG_CULLMODE_BACK;
    pipelineDesc.face_winding = SG_FACEWINDING_CCW;
    pipelineDesc.depth.write_enabled = true;
    pipelineDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipelineDesc.label = "minuvex-mesh-pipeline";
    pipeline_ = sg_make_pipeline(pipelineDesc);
    if (sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID) {
        Shutdown();
        throw std::runtime_error("Failed to create the mesh pipeline. See Sokol diagnostics.");
    }
    indexCount_ = static_cast<int>(mesh.Indices.size());
}

void MeshRenderer::Draw(const Mat4& model, const Mat4& viewProjection) const {
    if (!pipeline_.id) {
        throw std::logic_error("Initialize MeshRenderer before drawing.");
    }
    mesh_params_t uniforms{};
    uniforms.mvp = viewProjection * model;
    uniforms.model = model;
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertices_;
    bindings.index_buffer = indices_;
    sg_apply_pipeline(pipeline_);
    sg_apply_bindings(bindings);
    sg_apply_uniforms(UB_mesh_params, SG_RANGE(uniforms));
    sg_draw(0, indexCount_, 1);
}

void MeshRenderer::Shutdown() noexcept {
    if (sg_isvalid()) {
        if (pipeline_.id) { sg_destroy_pipeline(pipeline_); }
        if (shader_.id) { sg_destroy_shader(shader_); }
        if (indices_.id) { sg_destroy_buffer(indices_); }
        if (vertices_.id) { sg_destroy_buffer(vertices_); }
    }
    pipeline_ = {};
    shader_ = {};
    indices_ = {};
    vertices_ = {};
    indexCount_ = 0;
}

} // namespace lv::detail
