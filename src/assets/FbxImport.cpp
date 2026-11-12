#include "ImportCommon.h"
#include <ufbx.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>

namespace lv::detail {
namespace {
std::string Text(ufbx_string text) {return {text.data,text.length};}
float Float(double value) {if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) {ImportInvalid("FBX value exceeds finite float range.");}return static_cast<float>(value);}
Vec3 Vector(ufbx_vec3 v) {return {Float(v.x),Float(v.y),Float(v.z)};}
Transform TransformOf(const ufbx_transform& t) {return {Vector(t.translation),{Float(t.rotation.x),Float(t.rotation.y),Float(t.rotation.z),Float(t.rotation.w)},Vector(t.scale)};}
Mat4 Matrix(const ufbx_matrix& m) {Mat4 result=Mat4::Identity();for (std::size_t c=0;c<4;++c) {result.Elements[c*4]=Float(m.cols[c].x);result.Elements[c*4+1]=Float(m.cols[c].y);result.Elements[c*4+2]=Float(m.cols[c].z);}return result;}
[[noreturn]] void Failure(const ufbx_error& error) {
    char message[2048]{};ufbx_format_error(message,sizeof(message),&error);
    if (error.type == UFBX_ERROR_MEMORY_LIMIT || error.type == UFBX_ERROR_ALLOCATION_LIMIT || error.type == UFBX_ERROR_OUT_OF_MEMORY) {ImportLimit(message);}
    ImportInvalid(std::string("FBX: ")+message);
}
std::optional<std::size_t> Texture(ImportContext& context,const ufbx_material_map& map,std::map<const ufbx_texture*,std::optional<std::size_t>>& cache) {
    if (!map.texture || !map.texture_enabled) {return {};}
    const auto* texture=map.texture;
    if (texture->type!=UFBX_TEXTURE_FILE) {context.Model.Warnings.push_back("Layered/procedural FBX texture is not supported: "+Text(texture->name));return {};}
    if (texture->has_uv_transform) {context.Model.Warnings.push_back("FBX texture UV transform is not evaluated: "+Text(texture->name));}
    if (const auto found=cache.find(texture);found!=cache.end()) {return found->second;}
    const auto name=texture->relative_filename.length ? Text(texture->relative_filename):Text(texture->filename);
    auto index=context.Texture(name,{static_cast<const std::uint8_t*>(texture->content.data),texture->content.size});
    if (index) {auto& target=context.Model.Textures[*index];target.WrapU=texture->wrap_u == UFBX_WRAP_CLAMP ? TextureWrap::ClampToEdge:TextureWrap::Repeat;target.WrapV=texture->wrap_v == UFBX_WRAP_CLAMP ? TextureWrap::ClampToEdge:TextureWrap::Repeat;}
    cache.emplace(texture,index);return index;
}
}
std::shared_ptr<ModelAsset> ImportFbx(const std::filesystem::path& path,const ImportSettings& settings) {
    ValidateImportSettings(settings);auto model=std::make_shared<ModelAsset>();model->SourcePath=path;model->Settings=settings;model->Format=AssetFormat::Fbx;model->ContentHash=14695981039346656037ULL;
    ImportContext context{*model};const auto bytes=context.Read(path);const auto filename=PathText(path);
    ufbx_load_opts options{};options.filename={filename.c_str(),filename.size()};options.file_format=UFBX_FILE_FORMAT_FBX;
    options.temp_allocator.memory_limit=settings.MaxResourceBytes;options.result_allocator.memory_limit=settings.MaxResourceBytes;
    options.node_depth_limit=256;options.generate_missing_normals=settings.GenerateMissingNormals;options.ignore_animation=!settings.ImportAnimations;
    options.load_external_files=false;options.target_axes=ufbx_axes_right_handed_y_up;options.target_unit_meters=1;
    options.space_conversion=UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.geometry_transform_handling=UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    options.inherit_mode_handling=UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.index_error_handling=UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    ufbx_error error{};std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> scene(ufbx_load_memory(bytes.data(),bytes.size(),&options,&error),ufbx_free_scene);if (!scene) {Failure(error);}
    if (scene->nodes.count>settings.MaxNodes || scene->anim_stacks.count>256) {ImportLimit("FBX exceeds node/clip limits.");}
    model->Nodes.resize(scene->nodes.count);model->Skeleton.Joints.resize(scene->nodes.count);
    for (const auto* node:scene->nodes) {
        auto& target=model->Nodes[node->typed_id];auto& joint=model->Skeleton.Joints[node->typed_id];
        target.Name=Text(node->name);if (node->parent) {target.Parent=node->parent->typed_id;}
        joint.Name=target.Name;joint.Parent=node->parent ? node->parent->typed_id:NoJoint;joint.IsJoint=node->bone!=nullptr;
        joint.ReferenceLocal=TransformOf(node->local_transform);joint.ReferenceMatrix=joint.ReferenceLocal.Matrix();target.LocalTransform=joint.ReferenceMatrix;
        if (node->light || node->camera) {model->Warnings.push_back("FBX camera/light is retained as a transform node: "+target.Name);}
    }
    std::map<const ufbx_texture*,std::optional<std::size_t>> textureCache;
    if (settings.ImportMaterials) {for (const auto* source:scene->materials) {
        MaterialAsset material;material.Name=Text(source->name);
        const auto& color=source->pbr.base_color.has_value ? source->pbr.base_color:source->fbx.diffuse_color;
        const auto& factor=source->pbr.base_color.has_value ? source->pbr.base_factor:source->fbx.diffuse_factor;
        if (color.has_value) {const auto f=factor.has_value ? factor.value_real:1;material.BaseColor={Float(color.value_vec3.x*f),Float(color.value_vec3.y*f),Float(color.value_vec3.z*f),1};}
        material.BaseColorTexture=Texture(context,color,textureCache);
        material.NormalTexture=Texture(context,source->fbx.normal_map,textureCache);
        if (source->fbx.transparency_factor.has_value && source->fbx.transparency_factor.value_real>0) {model->Warnings.push_back("FBX transparency uses opaque rendering: "+material.Name);}
        if (source->fbx.bump.texture) {model->Warnings.push_back("FBX height/bump map is not converted to a normal map: "+material.Name);}
        model->Materials.push_back(std::move(material));
    }}
    const auto fallback=model->Materials.size();model->Materials.push_back({});
    bool reducedWeights=false;
    for (const auto* node:scene->nodes) {
        if (!node->mesh || !node->mesh->num_triangles) {continue;}
        const auto& source=*node->mesh;
        if (!source.vertex_normal.exists) {ImportInvalid("FBX mesh lacks normals; enable GenerateMissingNormals.");}
        if (source.num_vertices>settings.MaxVertices || source.num_triangles>settings.MaxIndices/3) {ImportLimit("FBX mesh exceeds vertex/index limits.");}
        if (source.blend_deformers.count || source.cache_deformers.count) {model->Warnings.push_back("FBX morph/geometry cache deformation is not imported: "+Text(node->name));}
        if (source.skin_deformers.count>1) {throw ImportError(ImportErrorCode::UnsupportedFeature,"FBX mesh has multiple skin deformers.");}
        const auto* deformer=source.skin_deformers.count ? source.skin_deformers[0]:nullptr;
        std::optional<std::size_t> skinIndex;std::size_t fallbackJoint=0;
        if (deformer) {
            if (deformer->skinning_method!=UFBX_SKINNING_METHOD_LINEAR && deformer->skinning_method!=UFBX_SKINNING_METHOD_RIGID) {throw ImportError(ImportErrorCode::UnsupportedFeature,"FBX dual-quaternion skinning is not supported.");}
            SkinAsset skin;skin.Name=Text(deformer->name);std::set<JointId> joints;
            for (const auto* cluster:deformer->clusters) {
                if (!cluster->bone_node || !joints.insert(cluster->bone_node->typed_id).second) {ImportInvalid("FBX skin contains missing/duplicate bone bindings.");}
                skin.Joints.push_back(cluster->bone_node->typed_id);skin.InverseBindMatrices.push_back(Matrix(cluster->geometry_to_bone));model->Skeleton.Joints[cluster->bone_node->typed_id].IsJoint=true;
            }
            if (deformer->vertices.count!=source.num_vertices) {ImportInvalid("FBX skin vertex table is inconsistent.");}
            bool unweighted=false;for (const auto& vertex:deformer->vertices) {double sum=0;for (std::size_t k=0;k<vertex.num_weights;++k) {const auto& w=deformer->weights[vertex.weight_begin+k];if (!std::isfinite(w.weight) || w.weight<0) {ImportInvalid("Invalid FBX skin weight.");}sum+=w.weight;}unweighted|=sum<=0;}
            if (unweighted) {fallbackJoint=skin.Joints.size();if (!joints.insert(node->typed_id).second) {ImportInvalid("Unweighted vertices cannot use an existing mesh-node bone.");}skin.Joints.push_back(node->typed_id);skin.InverseBindMatrices.push_back(Matrix(node->geometry_to_node));}
            if (skin.Joints.empty() || skin.Joints.size()>MaxSkinJoints) {ImportLimit("FBX skin exceeds the 128-joint GPU palette limit.");}
            skinIndex=model->Skins.size();model->Skins.push_back(std::move(skin));
        }
        MeshAsset mesh;mesh.Name=Text(node->name);
        for (const auto& segment:source.material_parts) {
            if (!segment.num_triangles) {continue;}
            MeshPrimitive primitive;primitive.HasSkinning=skinIndex.has_value();primitive.MaterialIndex=fallback;
            if (settings.ImportMaterials && segment.index<node->materials.count) {primitive.MaterialIndex=node->materials[segment.index]->typed_id;}
            for (auto faceIndex:segment.face_indices) {
                const auto face=source.faces[faceIndex];if (face.num_indices<3) {continue;}
                if (face.num_indices-2>settings.MaxIndices/3) {ImportLimit("FBX polygon exceeds index budget.");}
                std::vector<std::uint32_t> indices(static_cast<std::size_t>(face.num_indices-2)*3);
                const auto triangles=ufbx_triangulate_face(indices.data(),indices.size(),&source,face);if (!triangles) {ImportInvalid("FBX polygon triangulation failed.");}
                context.Geometry(static_cast<std::size_t>(triangles)*3,static_cast<std::size_t>(triangles)*3);
                for (std::size_t corner=0;corner<static_cast<std::size_t>(triangles)*3;++corner) {
                    const auto index=indices[corner];Vertex v{};const auto p=Vector(ufbx_get_vertex_vec3(&source.vertex_position,index)),n=Vector(ufbx_get_vertex_vec3(&source.vertex_normal,index));v.Position={p.X,p.Y,p.Z};v.Normal={n.X,n.Y,n.Z};v.Color={1,1,1,1};
                    if (source.vertex_uv.exists) {const auto uv=ufbx_get_vertex_vec2(&source.vertex_uv,index);v.TexCoord={Float(uv.x),1-Float(uv.y)};}
                    if (source.vertex_color.exists) {const auto c=ufbx_get_vertex_vec4(&source.vertex_color,index);v.Color={Float(c.x),Float(c.y),Float(c.z),Float(c.w)};}
                    if (source.vertex_tangent.exists && source.vertex_bitangent.exists) {const auto t=Vector(ufbx_get_vertex_vec3(&source.vertex_tangent,index)),b=Vector(ufbx_get_vertex_vec3(&source.vertex_bitangent,index));const auto sign=(n.Y*t.Z-n.Z*t.Y)*b.X+(n.Z*t.X-n.X*t.Z)*b.Y+(n.X*t.Y-n.Y*t.X)*b.Z;v.Tangent={t.X,t.Y,t.Z,sign<0 ? 1.0f:-1.0f};}
                    if (deformer) {
                        const auto& vertex=deformer->vertices[source.vertex_indices[index]];v.Weights={};double sum=0;
                        for (std::size_t k=0;k<std::min<std::size_t>(4,vertex.num_weights);++k) {const auto& weight=deformer->weights[vertex.weight_begin+k];if (weight.cluster_index>=deformer->clusters.count) {ImportInvalid("FBX skin cluster index is invalid.");}v.Joints[k]=static_cast<std::uint16_t>(weight.cluster_index);v.Weights[k]=Float(weight.weight);sum+=weight.weight;}
                        if (sum>0) {for (auto& weight:v.Weights) {weight=Float(weight/sum);}}
                        else {v.Joints[0]=static_cast<std::uint16_t>(fallbackJoint);v.Weights[0]=1;}
                        if (vertex.num_weights>4) {reducedWeights=true;}
                    }
                    primitive.Geometry.Indices.push_back(static_cast<std::uint32_t>(primitive.Geometry.Vertices.size()));primitive.Geometry.Vertices.push_back(v);
                }
            }
            mesh.Primitives.push_back(std::move(primitive));
        }
        if (!mesh.Primitives.empty()) {const auto meshIndex=model->Meshes.size();model->Meshes.push_back(std::move(mesh));auto& target=model->Nodes[node->typed_id];target.MeshIndex=meshIndex;target.SkinIndex=skinIndex;model->Parts.push_back({meshIndex,Matrix(node->node_to_world),node->typed_id,skinIndex});}
    }
    if (reducedWeights) {model->Warnings.push_back("FBX vertices with more than four influences use the strongest four, renormalized.");}
    std::size_t totalSamples=0;
    if (settings.ImportAnimations) {for (const auto* stack:scene->anim_stacks) {
        ufbx_bake_opts bake{};bake.resample_rate=settings.AnimationBakeRate;bake.temp_allocator.memory_limit=settings.MaxResourceBytes;bake.result_allocator.memory_limit=settings.MaxResourceBytes;
        std::unique_ptr<ufbx_baked_anim,decltype(&ufbx_free_baked_anim)> baked(ufbx_bake_anim(scene.get(),stack->anim,&bake,&error),ufbx_free_baked_anim);if (!baked) {Failure(error);}
        AnimationClip clip;clip.Name=Text(stack->name);clip.Duration=stack->time_end-stack->time_begin;
        if (!std::isfinite(clip.Duration) || clip.Duration<0 || clip.Duration*settings.AnimationBakeRate>settings.MaxAnimationSamples) {ImportLimit("FBX animation duration exceeds bake budget.");}
        const auto count=static_cast<std::size_t>(std::ceil(clip.Duration*settings.AnimationBakeRate))+1;
        if (baked->nodes.count && count>(settings.MaxAnimationSamples-totalSamples)/(baked->nodes.count*3)) {ImportLimit("FBX TRS bake exceeds MaxAnimationSamples; lower AnimationBakeRate or disable ImportAnimations.");}
        for (const auto& animated:baked->nodes) {
            AnimationTrack translation,rotation,scale;translation.Joint=rotation.Joint=scale.Joint=animated.typed_id;translation.Path=AnimationPath::Translation;rotation.Path=AnimationPath::Rotation;scale.Path=AnimationPath::Scale;
            context.Reserve(count*3*(sizeof(float)+sizeof(std::array<float,4>)));totalSamples+=count*3;
            for (std::size_t sample=0;sample<count;++sample) {
                const auto seconds=std::min(static_cast<double>(sample)/settings.AnimationBakeRate,clip.Duration);const auto time=Float(seconds);
                const auto t=TransformOf(ufbx_evaluate_transform(stack->anim,scene->nodes[animated.typed_id],stack->time_begin+seconds));
                translation.Times.push_back(time);rotation.Times.push_back(time);scale.Times.push_back(time);
                translation.Values.push_back({t.Position.X,t.Position.Y,t.Position.Z,0});rotation.Values.push_back({t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W});scale.Values.push_back({t.Scale.X,t.Scale.Y,t.Scale.Z,0});
            }
            clip.Tracks.push_back(std::move(translation));clip.Tracks.push_back(std::move(rotation));clip.Tracks.push_back(std::move(scale));
        }
        if (!clip.Tracks.empty()) {model->Animations.push_back(std::move(clip));}
    }}
    if (!model->Animations.empty()) {model->Warnings.push_back("FBX animation stacks/layers are sampled to local TRS at "+std::to_string(settings.AnimationBakeRate)+" Hz; interpolation between samples is approximate.");}
    model->Warnings.push_back("FBX converted to right-handed Y-up meters; base color/normal textures supported, advanced PBR is not evaluated.");
    FinishImportedModel(*model);return model;
}
} // namespace lv::detail
