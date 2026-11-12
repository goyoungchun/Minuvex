#include "renderer/MeshRenderer.h"

#include "mesh.glsl.h"
#include "DebugResources.h"

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

void MeshRenderer::Initialize(const MeshData& mesh, bool doubleSided, bool skinned, int samples,sg_pixel_format colorFormat,bool shadow) {
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
    pipelineDesc.sample_count=samples;
    if (colorFormat != SG_PIXELFORMAT_NONE) {pipelineDesc.colors[0].pixel_format=colorFormat;}
    pipelineDesc.layout.buffers[0].stride = sizeof(Vertex);
    pipelineDesc.layout.attrs[ATTR_mesh_position].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.attrs[ATTR_mesh_position].offset = offsetof(Vertex, Position);
    pipelineDesc.layout.attrs[ATTR_mesh_normal].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.attrs[ATTR_mesh_normal].offset = offsetof(Vertex, Normal);
    pipelineDesc.layout.attrs[ATTR_mesh_color0].format = SG_VERTEXFORMAT_FLOAT4;
    pipelineDesc.layout.attrs[ATTR_mesh_color0].offset = offsetof(Vertex, Color);
    pipelineDesc.layout.attrs[ATTR_mesh_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    pipelineDesc.layout.attrs[ATTR_mesh_texcoord0].offset = offsetof(Vertex, TexCoord);
    pipelineDesc.layout.attrs[ATTR_mesh_tangent].format=SG_VERTEXFORMAT_FLOAT4;pipelineDesc.layout.attrs[ATTR_mesh_tangent].offset=offsetof(Vertex,Tangent);
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
    if (shadow) {
        shadowShader_=sg_make_shader(skinned ? shadow_skin_shader_desc(sg_query_backend()):shadow_mesh_shader_desc(sg_query_backend()));
        sg_pipeline_desc d{};d.shader=shadowShader_;d.sample_count=1;d.colors[0].pixel_format=SG_PIXELFORMAT_R32F;d.depth.pixel_format=SG_PIXELFORMAT_DEPTH;d.depth.compare=SG_COMPAREFUNC_LESS_EQUAL;d.depth.write_enabled=true;d.index_type=SG_INDEXTYPE_UINT32;d.cull_mode=doubleSided || skinned ? SG_CULLMODE_NONE:SG_CULLMODE_BACK;d.layout.buffers[0].stride=sizeof(Vertex);
        const auto attr=[&](int index,sg_vertex_format format,int offset) {d.layout.attrs[index].format=format;d.layout.attrs[index].offset=offset;};
        if (skinned) {attr(ATTR_shadow_skin_position,SG_VERTEXFORMAT_FLOAT3,offsetof(Vertex,Position));attr(ATTR_shadow_skin_color0,SG_VERTEXFORMAT_FLOAT4,offsetof(Vertex,Color));attr(ATTR_shadow_skin_texcoord0,SG_VERTEXFORMAT_FLOAT2,offsetof(Vertex,TexCoord));attr(ATTR_shadow_skin_joint_indices,SG_VERTEXFORMAT_USHORT4,offsetof(Vertex,Joints));attr(ATTR_shadow_skin_joint_weights,SG_VERTEXFORMAT_FLOAT4,offsetof(Vertex,Weights));}
        else {attr(ATTR_shadow_mesh_position,SG_VERTEXFORMAT_FLOAT3,offsetof(Vertex,Position));attr(ATTR_shadow_mesh_color0,SG_VERTEXFORMAT_FLOAT4,offsetof(Vertex,Color));attr(ATTR_shadow_mesh_texcoord0,SG_VERTEXFORMAT_FLOAT2,offsetof(Vertex,TexCoord));}
        for (int i=0;i<2;++i) {d.face_winding=i == 0 ? SG_FACEWINDING_CCW:SG_FACEWINDING_CW;shadowPipelines_[i]=sg_make_pipeline(d);}
        if (sg_query_shader_state(shadowShader_) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(shadowPipelines_[0]) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(shadowPipelines_[1]) != SG_RESOURCESTATE_VALID) {Shutdown();throw std::runtime_error("Cannot create shadow mesh pipelines.");}
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
    samplerDesc.min_filter=samplerDesc.mag_filter=SG_FILTER_NEAREST;nearestSampler_=sg_make_sampler(samplerDesc);
    if (sg_query_image_state(whiteImage_) != SG_RESOURCESTATE_VALID || sg_query_view_state(whiteView_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(whiteSampler_) != SG_RESOURCESTATE_VALID || sg_query_sampler_state(nearestSampler_) != SG_RESOURCESTATE_VALID) {
        Shutdown();
        throw std::runtime_error("Failed to create the fallback texture.");
    }
    indexCount_ = static_cast<int>(mesh.Indices.size());
}

void MeshRenderer::Draw(const Mat4& model, const Mat4& viewProjection, const DirectionalLight& light,
                        const MaterialAsset& material, sg_view texture, sg_sampler sampler, std::span<const Mat4> palette, bool forceUnlit,const MeshLighting& lighting) const {
    if (!pipelines_[0].id) {
        throw std::logic_error("Initialize MeshRenderer before drawing.");
    }
    mesh_params_t uniforms{};
    uniforms.mvp = viewProjection * model;
    uniforms.normal_matrix = model.NormalMatrix();
    uniforms.model_matrix=model;uniforms.shadow_matrix=lighting.Shadow ? lighting.Shadow->ViewProjection*model:Mat4::Identity();
    light.Validate();
    const auto length = std::hypot(double(light.Direction.X), double(light.Direction.Y), double(light.Direction.Z));
    material_params_t fragment{};
    fragment.base_color = {material.BaseColor[0], material.BaseColor[1], material.BaseColor[2], material.BaseColor[3]};
    fragment.light_direction = {static_cast<float>(-light.Direction.X/length), static_cast<float>(-light.Direction.Y/length), static_cast<float>(-light.Direction.Z/length), light.Intensity};
    fragment.light_color = {light.Color.X, light.Color.Y, light.Color.Z, light.Ambient};
    fragment.options = {material.AlphaCutoff, material.Unlit || forceUnlit ? 1.0f : 0.0f, 0,lighting.Advanced ? 1.0f:0.0f};
    fragment.normal_options={material.NormalScale,lighting.Advanced && lighting.NormalTexture.id ? 1.0f:0.0f,0,0};
    fragment.debug_options={static_cast<float>(lighting.Debug.View),lighting.NearPlane,lighting.FarPlane,0};
    if (lighting.Shadow) {fragment.shadow_options={static_cast<float>(lighting.Shadow->Mode),1.0f/lighting.Shadow->Resolution,.0008f,.003f};}
    std::size_t local=0;
    const auto point=[&](Vec3 position,Vec3 color,float intensity,float range) {fragment.local_positions[local]={position.X,position.Y,position.Z,range};fragment.local_colors[local]={color.X,color.Y,color.Z,intensity};};
    for (const auto& l:lighting.Points) {if (local >= MaxLocalLights) {throw std::invalid_argument("Local light limit exceeded.");}point(l.Position,l.Color,l.Intensity,l.Range);++local;}
    for (const auto& l:lighting.Spots) {if (local >= MaxLocalLights) {throw std::invalid_argument("Local light limit exceeded.");}point(l.Position,l.Color,l.Intensity,l.Range);const auto n=std::hypot(double(l.Direction.X),double(l.Direction.Y),double(l.Direction.Z));fragment.local_directions[local]={static_cast<float>(l.Direction.X/n),static_cast<float>(l.Direction.Y/n),static_cast<float>(l.Direction.Z/n),std::cos(l.OuterConeRadians)};fragment.local_cones[local]={std::cos(l.InnerConeRadians),1,0,0};++local;}
    fragment.options.Z=static_cast<float>(local);
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertices_;
    bindings.index_buffer = indices_;
    bindings.views[VIEW_base_texture] = texture.id ? texture : whiteView_;
    bindings.samplers[SMP_base_sampler] = sampler.id ? sampler : whiteSampler_;
    bindings.views[VIEW_normal_texture]=lighting.NormalTexture.id ? lighting.NormalTexture:whiteView_;bindings.samplers[SMP_normal_sampler]=lighting.NormalSampler.id ? lighting.NormalSampler:whiteSampler_;
    bindings.views[VIEW_shadow_texture]=lighting.Shadow && lighting.Shadow->Texture.id ? lighting.Shadow->Texture:whiteView_;bindings.samplers[SMP_shadow_sampler]=lighting.Shadow && lighting.Shadow->Sampler.id ? lighting.Shadow->Sampler:nearestSampler_;
    const bool wire=lighting.Debug.View == DebugView::Wireframe;
    if (wire) {bindings.index_buffer=edges_;}
    sg_apply_pipeline(wire ? wirePipelines_[model.LinearDeterminant() < 0 ? 1:0]:pipelines_[model.LinearDeterminant() < 0 ? 1 : 0]);
    sg_apply_bindings(bindings);
    sg_apply_uniforms(UB_mesh_params, SG_RANGE(uniforms));
    sg_apply_uniforms(UB_material_params, SG_RANGE(fragment));
    if (skinned_) {
        if (palette.size() > MaxSkinJoints) { throw std::invalid_argument("Skin palette exceeds 128 joints."); }
        skin_params_t skin{};
        for (std::size_t i=0;i<palette.size();++i) { skin.joint_matrices[i] = palette[i]; }
        skin.skin_options = {palette.empty() ? 0.0f : 1.0f,static_cast<float>(lighting.Debug.WeightJoint),0,0};
        sg_apply_uniforms(UB_skin_params,SG_RANGE(skin));
    }
    sg_draw(0,wire ? edgeIndexCount_:indexCount_,1);
}

void MeshRenderer::PrepareWireframe(const MeshData& mesh) {
    if (edges_.id) {return;}
    if (mesh.Indices.size() > static_cast<std::size_t>(INT_MAX)/2) {throw std::length_error("Wireframe index limit exceeded.");}
    std::vector<std::uint32_t> edges;edges.reserve(mesh.Indices.size()*2);
    for (std::size_t i=0;i<mesh.Indices.size();i+=3) {const auto a=mesh.Indices[i],b=mesh.Indices[i+1],c=mesh.Indices[i+2];edges.insert(edges.end(),{a,b,b,c,c,a});}
    sg_buffer_desc b{};b.usage.index_buffer=true;b.data={edges.data(),edges.size()*sizeof(std::uint32_t)};b.label="debug-wireframe-indices";
    sg_buffer buffer=sg_make_buffer(b);sg_pipeline pipelines[2]{};
    for (int i=0;i<2;++i) {auto d=sg_query_pipeline_desc(pipelines_[i]);d.primitive_type=SG_PRIMITIVETYPE_LINES;d.cull_mode=SG_CULLMODE_NONE;d.label="debug-wireframe-pipeline";pipelines[i]=sg_make_pipeline(d);}
    if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(pipelines[0]) != SG_RESOURCESTATE_VALID || sg_query_pipeline_state(pipelines[1]) != SG_RESOURCESTATE_VALID) {for (auto p:pipelines) {if (p.id) {sg_destroy_pipeline(p);}}if (buffer.id) {sg_destroy_buffer(buffer);}throw std::runtime_error("Cannot create debug wireframe resources.");}
    edges_=buffer;wirePipelines_[0]=pipelines[0];wirePipelines_[1]=pipelines[1];edgeIndexCount_=static_cast<int>(edges.size());
}
void MeshRenderer::Inspect(std::vector<DebugResource>& r) const {
    InspectBuffer(r,vertices_,"Mesh vertices");InspectBuffer(r,indices_,"Mesh triangle indices");InspectBuffer(r,edges_,"Wireframe edge indices");InspectImage(r,whiteImage_,"Fallback white");
    InspectShader(r,shader_,skinned_ ? "skin":"mesh");InspectShader(r,shadowShader_,skinned_ ? "shadow_skin":"shadow_mesh");
    for (auto p:pipelines_) {InspectPipeline(r,p,"Mesh winding pipeline");}for (auto p:wirePipelines_) {InspectPipeline(r,p,"Wireframe winding pipeline");}for (auto p:shadowPipelines_) {InspectPipeline(r,p,"Shadow winding pipeline");}
}

void MeshRenderer::DrawShadow(const Mat4& model,const Mat4& lightViewProjection,const MaterialAsset& material,sg_view texture,sg_sampler sampler,std::span<const Mat4> palette) const {
    if (!shadowShader_.id) {throw std::logic_error("Shadow pipeline was not prepared.");}
    sg_bindings b{};b.vertex_buffers[0]=vertices_;b.index_buffer=indices_;b.views[VIEW_mask_texture]=texture.id ? texture:whiteView_;b.samplers[SMP_mask_sampler]=sampler.id ? sampler:whiteSampler_;
    shadow_params_t params{};params.light_mvp=lightViewProjection*model;shadow_material_t mask{};mask.shadow_mask={material.AlphaCutoff,material.BaseColor[3],0,0};
    sg_apply_pipeline(shadowPipelines_[model.LinearDeterminant() < 0 ? 1:0]);sg_apply_bindings(b);sg_apply_uniforms(UB_shadow_params,SG_RANGE(params));sg_apply_uniforms(UB_shadow_material,SG_RANGE(mask));
    if (skinned_) {if (palette.size() > MaxSkinJoints) {throw std::invalid_argument("Skin palette exceeds shadow limit.");}skin_params_t skin{};for (std::size_t i=0;i<palette.size();++i) {skin.joint_matrices[i]=palette[i];}skin.skin_options={palette.empty() ? 0.0f:1.0f,0,0,0};sg_apply_uniforms(UB_skin_params,SG_RANGE(skin));}
    sg_draw(0,indexCount_,1);
}
void MeshRenderer::Shutdown() noexcept {
    if (sg_isvalid()) {
        if (edges_.id) {sg_destroy_buffer(edges_);}for (auto p:wirePipelines_) {if (p.id) {sg_destroy_pipeline(p);}}
        if (whiteSampler_.id) { sg_destroy_sampler(whiteSampler_); }
        if (nearestSampler_.id) {sg_destroy_sampler(nearestSampler_);}
        for (const auto pipeline:shadowPipelines_) {if (pipeline.id) {sg_destroy_pipeline(pipeline);}}
        if (shadowShader_.id) {sg_destroy_shader(shadowShader_);}
        if (whiteView_.id) { sg_destroy_view(whiteView_); }
        if (whiteImage_.id) { sg_destroy_image(whiteImage_); }
        for (auto pipeline : pipelines_) { if (pipeline.id) { sg_destroy_pipeline(pipeline); } }
        if (shader_.id) { sg_destroy_shader(shader_); }
        if (indices_.id) { sg_destroy_buffer(indices_); }
        if (vertices_.id) { sg_destroy_buffer(vertices_); }
    }
    pipelines_[0] = pipelines_[1] = {};
    shadowPipelines_[0]=shadowPipelines_[1]={};shadowShader_={};nearestSampler_={};
    whiteImage_ = {};
    whiteView_ = {};
    whiteSampler_ = {};
    shader_ = {};
    indices_ = {};
    vertices_ = {};
    indexCount_ = 0;
    edges_={};wirePipelines_[0]=wirePipelines_[1]={};edgeIndexCount_=0;
}

} // namespace lv::detail
