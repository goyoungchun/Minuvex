#include "renderer/MeshRenderer.h"

#include "mesh.glsl.h"

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace lv::detail {

static_assert(sizeof(Mat4) == 64 && std::is_trivially_copyable_v<Mat4>);
static_assert(std::extent_v<decltype(skin_params_t::joint_matrices)> == MaxSkinJoints);
static_assert(alignof(mesh_params_t) >= 16 && offsetof(mesh_params_t, normal_matrix) == 64);
static_assert(std::is_standard_layout_v<Vertex>);

MeshRenderer::~MeshRenderer() {
    Shutdown();
}

void MeshRenderer::Initialize(const MeshData& mesh, bool doubleSided, bool skinned) {
    if (vertices_.id || indices_.id || shader_.id || pipelines_[0].id) {
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
    indexDesc.data = {mesh.Indices.data(), mesh.Indices.size() * sizeof(std::uint32_t)};
    indexDesc.label = "minuvex-mesh-indices";
    indices_ = sg_make_buffer(indexDesc);
    skinned_ = skinned;
    shader_ = sg_make_shader(skinned ? skin_shader_desc(sg_query_backend()) : mesh_shader_desc(sg_query_backend()));

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
    pipelineDesc.layout.attrs[ATTR_mesh_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    pipelineDesc.layout.attrs[ATTR_mesh_texcoord0].offset = offsetof(Vertex, TexCoord);
    static_assert(ATTR_mesh_position == ATTR_skin_position && ATTR_mesh_normal == ATTR_skin_normal && ATTR_mesh_color0 == ATTR_skin_color0 && ATTR_mesh_texcoord0 == ATTR_skin_texcoord0);
    if (skinned) {
        pipelineDesc.layout.attrs[ATTR_skin_joint_indices].format = SG_VERTEXFORMAT_USHORT4;
        pipelineDesc.layout.attrs[ATTR_skin_joint_indices].offset = offsetof(Vertex,Joints);
        pipelineDesc.layout.attrs[ATTR_skin_joint_weights].format = SG_VERTEXFORMAT_FLOAT4;
        pipelineDesc.layout.attrs[ATTR_skin_joint_weights].offset = offsetof(Vertex,Weights);
    }
    pipelineDesc.index_type = SG_INDEXTYPE_UINT32;
    pipelineDesc.cull_mode = doubleSided || skinned ? SG_CULLMODE_NONE : SG_CULLMODE_BACK;
    pipelineDesc.depth.write_enabled = true;
    pipelineDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipelineDesc.label = "minuvex-mesh-pipeline";
    for (int i = 0; i < 2; ++i) {
        pipelineDesc.face_winding = i == 0 ? SG_FACEWINDING_CCW : SG_FACEWINDING_CW;
        pipelines_[i] = sg_make_pipeline(pipelineDesc);
        if (sg_query_pipeline_state(pipelines_[i]) != SG_RESOURCESTATE_VALID) {
            Shutdown();
            throw std::runtime_error("Failed to create the mesh pipeline. See Sokol diagnostics.");
        }
    }
    const std::uint32_t white = 0xFFFFFFFF;
    sg_image_desc imageDesc{};
    imageDesc.width = imageDesc.height = 1;
    imageDesc.pixel_format = SG_PIXELFORMAT_SRGB8A8;
    imageDesc.data = SG_RANGE(white);
    whiteImage_ = sg_make_image(imageDesc);
    if (sg_query_image_state(whiteImage_) != SG_RESOURCESTATE_VALID) {
        Shutdown();
        throw std::runtime_error("Failed to create the fallback image.");
    }
    sg_view_desc viewDesc{};
    viewDesc.texture.image = whiteImage_;
    whiteView_ = sg_make_view(viewDesc);
    sg_sampler_desc samplerDesc{};
    samplerDesc.min_filter = samplerDesc.mag_filter = SG_FILTER_LINEAR;
    whiteSampler_ = sg_make_sampler(samplerDesc);
    if (sg_query_image_state(whiteImage_) != SG_RESOURCESTATE_VALID || sg_query_view_state(whiteView_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(whiteSampler_) != SG_RESOURCESTATE_VALID) {
        Shutdown();
        throw std::runtime_error("Failed to create the fallback texture.");
    }
    indexCount_ = static_cast<int>(mesh.Indices.size());
}

void MeshRenderer::Draw(const Mat4& model, const Mat4& viewProjection, const DirectionalLight& light,
                        const MaterialAsset& material, sg_view texture, sg_sampler sampler, std::span<const Mat4> palette) const {
    if (!pipelines_[0].id) {
        throw std::logic_error("Initialize MeshRenderer before drawing.");
    }
    mesh_params_t uniforms{};
    uniforms.mvp = viewProjection * model;
    uniforms.normal_matrix = model.NormalMatrix();
    light.Validate();
    const auto length = std::hypot(double(light.Direction.X), double(light.Direction.Y), double(light.Direction.Z));
    material_params_t fragment{};
    fragment.base_color = {material.BaseColor[0], material.BaseColor[1], material.BaseColor[2], material.BaseColor[3]};
    fragment.light_direction = {static_cast<float>(-light.Direction.X/length), static_cast<float>(-light.Direction.Y/length), static_cast<float>(-light.Direction.Z/length), light.Intensity};
    fragment.light_color = {light.Color.X, light.Color.Y, light.Color.Z, light.Ambient};
    fragment.options = {material.AlphaCutoff, material.Unlit ? 1.0f : 0.0f, 0, 0};
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertices_;
    bindings.index_buffer = indices_;
    bindings.views[VIEW_base_texture] = texture.id ? texture : whiteView_;
    bindings.samplers[SMP_base_sampler] = sampler.id ? sampler : whiteSampler_;
    sg_apply_pipeline(pipelines_[model.LinearDeterminant() < 0 ? 1 : 0]);
    sg_apply_bindings(bindings);
    sg_apply_uniforms(UB_mesh_params, SG_RANGE(uniforms));
    sg_apply_uniforms(UB_material_params, SG_RANGE(fragment));
    if (skinned_) {
        if (palette.size() > MaxSkinJoints) { throw std::invalid_argument("Skin palette exceeds 128 joints."); }
        skin_params_t skin{};
        for (std::size_t i=0;i<palette.size();++i) { skin.joint_matrices[i] = palette[i]; }
        skin.skin_options = {palette.empty() ? 0.0f : 1.0f,0,0,0};
        sg_apply_uniforms(UB_skin_params,SG_RANGE(skin));
    }
    sg_draw(0, indexCount_, 1);
}

void MeshRenderer::Shutdown() noexcept {
    if (sg_isvalid()) {
        if (whiteSampler_.id) { sg_destroy_sampler(whiteSampler_); }
        if (whiteView_.id) { sg_destroy_view(whiteView_); }
        if (whiteImage_.id) { sg_destroy_image(whiteImage_); }
        for (auto pipeline : pipelines_) { if (pipeline.id) { sg_destroy_pipeline(pipeline); } }
        if (shader_.id) { sg_destroy_shader(shader_); }
        if (indices_.id) { sg_destroy_buffer(indices_); }
        if (vertices_.id) { sg_destroy_buffer(vertices_); }
    }
    pipelines_[0] = pipelines_[1] = {};
    whiteImage_ = {};
    whiteView_ = {};
    whiteSampler_ = {};
    shader_ = {};
    indices_ = {};
    vertices_ = {};
    indexCount_ = 0;
}

} // namespace lv::detail
