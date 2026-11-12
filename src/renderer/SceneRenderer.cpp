#include "renderer/SceneRenderer.h"

#include "renderer/MeshRenderer.h"
#include "renderer/TextureData.h"
#include "telemetry/ResourceSignature.h"
#include "DebugResources.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lv::detail {
namespace {

sg_wrap Wrap(TextureWrap mode) {
    switch (mode) {
    case TextureWrap::ClampToEdge: return SG_WRAP_CLAMP_TO_EDGE;
    case TextureWrap::MirroredRepeat: return SG_WRAP_MIRRORED_REPEAT;
    default: return SG_WRAP_REPEAT;
    }
}

} // namespace

struct SceneRenderer::GpuModel {
    ModelHandle Asset;
    std::vector<std::vector<std::unique_ptr<MeshRenderer>>> Meshes;
    struct GpuImage { sg_image Image{}; sg_view View{}; int Size=0; };
    std::vector<GpuImage> Images;
    std::vector<sg_sampler> Samplers;
    std::vector<Bounds> MeshBounds;
    std::uint64_t TextureBytes=0,BufferBytes=0;

    ~GpuModel() {
        Meshes.clear();
        if (sg_isvalid()) {
            for (auto sampler : Samplers) { if (sampler.id) { sg_destroy_sampler(sampler); } }
            for (auto image : Images) {
                if (image.View.id) { sg_destroy_view(image.View); }
                if (image.Image.id) { sg_destroy_image(image.Image); }
            }
        }
    }

    void Upload(ModelHandle asset,const RenderSettings& settings) {
        Asset = std::move(asset);
        Meshes.resize(Asset->Meshes.size());
        MeshBounds.resize(Asset->Meshes.size());
        Images.resize(Asset->Images.size()*2);
        Samplers.resize(Asset->Textures.size());
        for (const auto& part : Asset->Parts) {
            auto& primitives = Meshes.at(part.MeshIndex);
            if (!primitives.empty()) { continue; }
            bool first=true;
            for (const auto& primitive:Asset->Meshes.at(part.MeshIndex).Primitives) {for (const auto& v:primitive.Geometry.Vertices) {
                const Vec3 p{v.Position[0],v.Position[1],v.Position[2]};auto& b=MeshBounds[part.MeshIndex];
                if (first) {b={p,p};first=false;}b.Min={std::min(b.Min.X,p.X),std::min(b.Min.Y,p.Y),std::min(b.Min.Z,p.Z)};b.Max={std::max(b.Max.X,p.X),std::max(b.Max.Y,p.Y),std::max(b.Max.Z,p.Z)};
            }}
            for (const auto& primitive : Asset->Meshes.at(part.MeshIndex).Primitives) {
                const auto& material = Asset->Materials.at(primitive.MaterialIndex);
                auto renderer = std::make_unique<MeshRenderer>();
                renderer->Initialize(primitive.Geometry, material.DoubleSided,primitive.HasSkinning,settings.MsaaSamples,settings.PostProcessing ? SG_PIXELFORMAT_RGBA16F:SG_PIXELFORMAT_NONE,settings.Shadow != ShadowMode::Off,settings.Lod ? std::span<const MeshLod>(primitive.Lods):std::span<const MeshLod>{});
                BufferBytes+=primitive.Geometry.Vertices.size()*sizeof(Vertex)+primitive.Geometry.Indices.size()*sizeof(std::uint32_t);
                if (settings.Lod) {for (const auto& level:primitive.Lods) {BufferBytes+=level.Indices.size()*sizeof(std::uint32_t);}}
                TextureBytes+=4;
                primitives.push_back(std::move(renderer));
                for (int role=0;role<2;++role) {
                const auto index=role == 0 ? material.BaseColorTexture:material.NormalTexture;
                if (!index || (role == 1 && settings.Lighting != LightingMode::Advanced)) {continue;}
                const auto textureIndex=*index;
                const auto& texture = Asset->Textures.at(textureIndex);
                auto& gpuImage = Images.at(texture.ImageIndex+role*Asset->Images.size());
                if (!gpuImage.Image.id) {
                    const auto& image = Asset->Images.at(texture.ImageIndex);
                    const auto packed=PackTexture(image,std::min(settings.MaxTextureSize,sg_query_limits().max_image_size_2d),role == 0,settings.Compression == TextureCompression::Bc3);
                    TextureBytes+=packed.Pixels.size();
                    sg_image_desc desc{};
                    desc.width = packed.Width; desc.height = packed.Height;
                    desc.num_mipmaps = packed.Levels;
                    desc.pixel_format = packed.Compressed ? (role == 0 ? SG_PIXELFORMAT_BC3_SRGBA:SG_PIXELFORMAT_BC3_RGBA):(role == 0 ? SG_PIXELFORMAT_SRGB8A8:SG_PIXELFORMAT_RGBA8);
                    desc.data = {packed.Pixels.data(), packed.Pixels.size()};
                    gpuImage.Size=std::max(packed.Width,packed.Height);
                    desc.label = "minuvex-base-color";
                    gpuImage.Image = sg_make_image(desc);
                    if (sg_query_image_state(gpuImage.Image) != SG_RESOURCESTATE_VALID) { throw std::runtime_error("Failed to upload the base-color image."); }
                    sg_view_desc view{};
                    view.texture.image = gpuImage.Image;
                    gpuImage.View = sg_make_view(view);
                    if (sg_query_view_state(gpuImage.View) != SG_RESOURCESTATE_VALID) { throw std::runtime_error("Failed to create the base-color image view."); }
                }
                if (Samplers.at(textureIndex).id) {continue;}
                sg_sampler_desc sampler{};
                sampler.wrap_u = Wrap(texture.WrapU); sampler.wrap_v = Wrap(texture.WrapV);
                sampler.min_filter = texture.MinFilter == TextureFilter::Nearest ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
                sampler.mag_filter = texture.MagFilter == TextureFilter::Nearest ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
                sampler.mipmap_filter = texture.UseMipmaps && texture.MipFilter == TextureFilter::Linear ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
                sampler.max_anisotropy=static_cast<std::uint32_t>(settings.Anisotropy);
                if (settings.Anisotropy > 1) {sampler.min_filter=sampler.mag_filter=sampler.mipmap_filter=SG_FILTER_LINEAR;}
                if (!texture.UseMipmaps) { sampler.max_lod = 0.00001f; }
                Samplers[textureIndex] = sg_make_sampler(sampler);
                if (sg_query_sampler_state(Samplers[textureIndex]) != SG_RESOURCESTATE_VALID) { throw std::runtime_error("Failed to create the texture sampler."); }
                }
            }
        }
    }
};

struct SceneRenderer::Batch {
    std::vector<Mat4> Transforms;sg_buffer Buffer{};bool Drawn=false;
    ~Batch() {if (Buffer.id && sg_isvalid()) {sg_destroy_buffer(Buffer);}}
};
SceneRenderer::SceneRenderer(RenderSettings settings) : settings_(settings) {}
SceneRenderer::~SceneRenderer() = default;
unsigned SceneRenderer::Lod(const Scene& scene,const Bounds& b,const Mat4& world,std::size_t levels) const {
    if (!settings_.Lod || !levels || debug_.View == DebugView::Wireframe) {return 0;}
    const Vec3 center=world.TransformPoint({(b.Min.X+b.Max.X)*.5f,(b.Min.Y+b.Max.Y)*.5f,(b.Min.Z+b.Max.Z)*.5f});const auto& e=world.Elements;
    const float scale=std::sqrt(e[0]*e[0]+e[1]*e[1]+e[2]*e[2]+e[4]*e[4]+e[5]*e[5]+e[6]*e[6]+e[8]*e[8]+e[9]*e[9]+e[10]*e[10]); // Conservative upper bound, including hierarchy-induced shear.
    const float radius=.5f*std::hypot(b.Max.X-b.Min.X,b.Max.Y-b.Min.Y,b.Max.Z-b.Min.Z)*scale;const auto& c=scene.MainCamera;
    const float distance=std::max(c.NearPlane,std::hypot(center.X-c.Position.X,center.Y-c.Position.Y,center.Z-c.Position.Z)-radius);
    const float pixels=radius*height_/(std::tan(c.VerticalFovRadians*.5f)*distance);return std::isfinite(pixels) ? SelectMeshLod(pixels,settings_.LodBias,levels):0;
}
void SceneRenderer::PrepareBatches(const Scene& scene) {
    batches_.clear();stats_.Optimization={};if (!settings_.Instancing || debug_.View == DebugView::Wireframe || (!viewportKnown_ && settings_.FrustumCulling)) {return;}
    const auto vp=scene.MainCamera.ViewProjection(aspect_);
    for (const auto& item:scene.CollectRenderItems()) {if (item.Animation && item.Animation->NeedsEvaluation()) {continue;}auto& model=*models_.at(item.Model.get());
        for (const auto& part:item.Model->Parts) {if (part.SkinIndex) {continue;}const auto transform=item.WorldTransform*part.Transform;(void)transform.NormalMatrix();const bool visible=!settings_.FrustumCulling || IntersectsFrustum(model.MeshBounds[part.MeshIndex],transform,vp);
            for (std::size_t i=0;i<item.Model->Meshes[part.MeshIndex].Primitives.size();++i) {const auto& primitive=item.Model->Meshes[part.MeshIndex].Primitives[i];if (primitive.HasSkinning) {continue;}auto* mesh=model.Meshes[part.MeshIndex][i].get();
                const auto append=[&](bool shadow,unsigned lod) {BatchKey key{mesh,lod,transform.LinearDeterminant()<0,shadow};auto& group=batches_[key];if (!group) {group=std::make_unique<Batch>();}group->Transforms.push_back(transform);};
                if (visible && debug_.View != DebugView::ShadowMap) {append(false,Lod(scene,model.MeshBounds[part.MeshIndex],transform,primitive.Lods.size()));}if (settings_.Shadow != ShadowMode::Off) {append(true,0);}
            }
        }
    }
    std::erase_if(batches_,[](const auto& entry) {return entry.second->Transforms.size()<2;});
    for (auto& [key,batch]:batches_) {key.Mesh->PrepareInstances();sg_buffer_desc b{};b.data={batch->Transforms.data(),batch->Transforms.size()*sizeof(Mat4)};b.label="static-instance-transforms";batch->Buffer=sg_make_buffer(b);if (sg_query_buffer_state(batch->Buffer) != SG_RESOURCESTATE_VALID) {throw std::runtime_error("Cannot upload static instance batch.");}stats_.Optimization.InstanceBufferBytes+=b.data.size;}
}

void SceneRenderer::Prepare(const Scene& scene) {
    scene.ValidateLighting();
    if (!sg_isvalid()) { throw std::logic_error("Initialize Sokol GFX before preparing a Scene."); }
    std::unordered_set<const ModelAsset*> needed;
    std::uint64_t signature=14695981039346656037ull;
    for (const auto& item : scene.CollectRenderItems()) {
        if (needed.insert(item.Model.get()).second) {HashResourceIdentity(signature,item.Model.get());}
        if (!models_.contains(item.Model.get())) {
            auto model = std::make_unique<GpuModel>();
            model->Upload(item.Model,settings_);
            models_.emplace(item.Model.get(), std::move(model));
        }
    }
    std::erase_if(models_, [&needed](const auto& entry) { return !needed.contains(entry.first); });
    if (debug_.View == DebugView::Wireframe) {for (auto& [key,model]:models_) {(void)key;for (std::size_t i=0;i<model->Meshes.size();++i) {for (std::size_t p=0;p<model->Meshes[i].size();++p) {model->Meshes[i][p]->PrepareWireframe(model->Asset->Meshes[i].Primitives[p].Geometry);}}}}
    stats_.ResidentModels = models_.size();
    stats_.ResidentAssetSignature=signature;
    stats_.ResidentPrimitives = stats_.ResidentImages = 0;
    stats_.MaxResidentTextureSize=0;
    stats_.LogicalTextureBytes=stats_.LogicalBufferBytes=0;
    for (const auto& [key, model] : models_) {
        (void)key;
        stats_.LogicalTextureBytes+=model->TextureBytes;stats_.LogicalBufferBytes+=model->BufferBytes;
        for (const auto& mesh : model->Meshes) { stats_.ResidentPrimitives += mesh.size(); }
        for (const auto& image : model->Images) { if (image.Image.id) { ++stats_.ResidentImages;stats_.MaxResidentTextureSize=std::max(stats_.MaxResidentTextureSize,image.Size); } }
    }
    PrepareBatches(scene);
}

void SceneRenderer::Draw(const Scene& scene,float aspect,const ShadowSampling& shadow) {stats_.LastWireLines=0;DrawImpl(scene,scene.MainCamera.ViewProjection(aspect),false,shadow);}
void SceneRenderer::DrawShadow(const Scene& scene,const Mat4& matrix) {DrawImpl(scene,matrix,true,{});}
void SceneRenderer::DrawImpl(const Scene& scene,const Mat4& viewProjection,bool shadowPass,const ShadowSampling& shadow) {
    scene.ValidateLighting();
    if (shadowPass) {stats_.LastShadowDrawCalls=stats_.LastShadowTriangles=0;}else {stats_.LastDrawCalls=0;stats_.LastCulledParts=0;stats_.LastTriangles=0;if (settings_.Shadow == ShadowMode::Off) {stats_.LastShadowDrawCalls=stats_.LastShadowTriangles=0;}}
    for (const auto& item : scene.CollectRenderItems()) {
        const auto found = models_.find(item.Model.get());
        if (found == models_.end()) { throw std::logic_error("Call SceneRenderer::Prepare after changing visible model instances."); }
        const auto& model = *found->second;
        std::optional<EvaluatedPose> pose;
        if (item.Animation && item.Animation->NeedsEvaluation()) { pose = item.Animation->Evaluate(); }
        for (const auto& part : item.Model->Parts) {
            auto transform = item.WorldTransform * (pose && part.Node != NoJoint ? pose->Global.at(part.Node) : part.Transform);
            std::vector<Mat4> palette;
            if (part.SkinIndex) {
                if (!pose) { throw std::logic_error("Skinned instances require evaluated pose state."); }
                const auto& skin = item.Model->Skins.at(*part.SkinIndex);
                for (std::size_t i=0;i<skin.Joints.size();++i) {
                    const auto matrix = pose->Global.at(skin.Joints[i])*skin.InverseBindMatrices.at(i);
                    (void)matrix.NormalMatrix(); palette.push_back(matrix);
                }
                // Mesh-node world cancels against inverse(mesh world) in glTF's
                // joint palette. Apply the Scene instance transform exactly once.
                transform = item.WorldTransform;
            }
            if (!shadowPass && settings_.FrustumCulling) {
                bool visible=IntersectsFrustum(model.MeshBounds.at(part.MeshIndex),transform,viewProjection);
                if (part.SkinIndex) {
                    Bounds combined;bool first=true;const auto& b=model.MeshBounds.at(part.MeshIndex);
                    // Union first, then test: two off-screen joints can blend a
                    // vertex into the visible region between them.
                    for (const auto& joint:palette) {for (unsigned mask=0;mask<8;++mask) {
                        const auto p=joint.TransformPoint({mask&1?b.Max.X:b.Min.X,mask&2?b.Max.Y:b.Min.Y,mask&4?b.Max.Z:b.Min.Z});
                        if (first) {combined={p,p};first=false;}combined.Min={std::min(combined.Min.X,p.X),std::min(combined.Min.Y,p.Y),std::min(combined.Min.Z,p.Z)};combined.Max={std::max(combined.Max.X,p.X),std::max(combined.Max.Y,p.Y),std::max(combined.Max.Z,p.Z)};
                    }}
                    visible=IntersectsFrustum(combined,item.WorldTransform,viewProjection);
                }
                if (!visible) {++stats_.LastCulledParts;continue;}
            }
            const auto& mesh = item.Model->Meshes.at(part.MeshIndex);
            for (std::size_t i = 0; i < mesh.Primitives.size(); ++i) {
                const auto& material = item.Model->Materials.at(mesh.Primitives[i].MaterialIndex);
                sg_view texture{};
                sg_sampler sampler{};
                if (material.BaseColorTexture) {
                    const auto index = *material.BaseColorTexture;
                    texture = model.Images.at(item.Model->Textures.at(index).ImageIndex).View;
                    sampler = model.Samplers.at(index);
                }
                if (shadowPass) {auto* renderer=model.Meshes.at(part.MeshIndex).at(i).get();MeshLighting instances;auto drawTransform=transform;std::size_t count=1;const auto group=batches_.find({renderer,0,transform.LinearDeterminant()<0,true});const bool eligible=!mesh.Primitives[i].HasSkinning && (!item.Animation || !item.Animation->NeedsEvaluation());if (eligible && group != batches_.end()) {auto& batch=*group->second;if (batch.Drawn) {continue;}batch.Drawn=true;count=batch.Transforms.size();instances.Instances=batch.Buffer;instances.InstanceCount=static_cast<int>(count);instances.Mirrored=transform.LinearDeterminant()<0;drawTransform=Mat4::Identity();++stats_.Optimization.InstanceDraws;stats_.Optimization.SavedDrawCalls+=count-1;}renderer->DrawShadow(drawTransform,viewProjection,material,texture,sampler,palette,instances);++stats_.LastShadowDrawCalls;stats_.LastShadowTriangles+=mesh.Primitives[i].Geometry.Indices.size()/3*count;}
                else {
                    MeshLighting lighting;lighting.Advanced=settings_.Lighting == LightingMode::Advanced;lighting.Points=scene.PointLights;lighting.Spots=scene.SpotLights;lighting.Shadow=&shadow;
                    lighting.Debug=debug_;lighting.NearPlane=scene.MainCamera.NearPlane;lighting.FarPlane=scene.MainCamera.FarPlane;
                    if (lighting.Advanced && material.NormalTexture) {const auto index=*material.NormalTexture;lighting.NormalTexture=model.Images.at(item.Model->Textures.at(index).ImageIndex+item.Model->Images.size()).View;lighting.NormalSampler=model.Samplers.at(index);}
                    auto* renderer=model.Meshes.at(part.MeshIndex).at(i).get();const auto level=part.SkinIndex ? 0:Lod(scene,model.MeshBounds[part.MeshIndex],transform,mesh.Primitives[i].Lods.size());lighting.Lod=level;
                    const BatchKey key{renderer,level,transform.LinearDeterminant()<0,false};const auto group=batches_.find(key);const bool eligible=!mesh.Primitives[i].HasSkinning && (!item.Animation || !item.Animation->NeedsEvaluation());std::size_t count=1;Mat4 drawTransform=transform;
                    if (eligible && group != batches_.end()) {auto& batch=*group->second;if (batch.Drawn) {continue;}batch.Drawn=true;count=batch.Transforms.size();lighting.Instances=batch.Buffer;lighting.InstanceCount=static_cast<int>(count);lighting.Mirrored=key.Mirrored;drawTransform=Mat4::Identity();stats_.Optimization.InstancedObjects+=count;++stats_.Optimization.InstanceDraws;stats_.Optimization.SavedDrawCalls+=count-1;}
                    renderer->Draw(drawTransform,viewProjection,scene.Sun,material,texture,sampler,palette,settings_.Lighting == LightingMode::Unlit,lighting);
                    ++stats_.LastDrawCalls;stats_.Optimization.LodDraws[level]+=count;stats_.Optimization.SourceTriangles+=mesh.Primitives[i].Geometry.Indices.size()/3*count;
                    if (debug_.View == DebugView::Wireframe) {stats_.LastWireLines+=mesh.Primitives[i].Geometry.Indices.size();}else {const auto triangles=renderer->TriangleCount(level)*count;stats_.LastTriangles+=triangles;stats_.Optimization.SubmittedTriangles+=triangles;}
                }
            }
        }
    }
    if (!shadowPass) {stats_.Optimization.CulledParts=stats_.LastCulledParts;}
}

Mat4 SceneRenderer::ShadowViewProjection(const Scene& scene) const {
    Bounds world;bool first=true;
    const auto add=[&](Vec3 p) {if (first) {world={p,p};first=false;}world.Min={std::min(world.Min.X,p.X),std::min(world.Min.Y,p.Y),std::min(world.Min.Z,p.Z)};world.Max={std::max(world.Max.X,p.X),std::max(world.Max.Y,p.Y),std::max(world.Max.Z,p.Z)};};
    for (const auto& item:scene.CollectRenderItems()) {
        const auto found=models_.find(item.Model.get());if (found == models_.end()) {continue;}
        std::optional<EvaluatedPose> pose;if (item.Animation && item.Animation->NeedsEvaluation()) {pose=item.Animation->Evaluate();}
        for (const auto& part:item.Model->Parts) {
            const auto& bounds=found->second->MeshBounds.at(part.MeshIndex);
            const auto corners=[&](const Mat4& m) {for (unsigned mask=0;mask<8;++mask) {add(m.TransformPoint({mask&1?bounds.Max.X:bounds.Min.X,mask&2?bounds.Max.Y:bounds.Min.Y,mask&4?bounds.Max.Z:bounds.Min.Z}));}};
            if (part.SkinIndex && pose) {const auto& skin=item.Model->Skins.at(*part.SkinIndex);for (std::size_t i=0;i<skin.Joints.size();++i) {corners(item.WorldTransform*pose->Global.at(skin.Joints[i])*skin.InverseBindMatrices.at(i));}}
            else {corners(item.WorldTransform*(pose && part.Node != NoJoint ? pose->Global.at(part.Node):part.Transform));}
        }
    }
    if (first) {return Mat4::Identity();}
    const Vec3 center{(world.Min.X+world.Max.X)*.5f,(world.Min.Y+world.Max.Y)*.5f,(world.Min.Z+world.Max.Z)*.5f};
    const auto radius=std::max(1.0f,std::hypot(world.Max.X-world.Min.X,world.Max.Y-world.Min.Y,world.Max.Z-world.Min.Z)*.5f);
    const auto& direction=scene.Sun.Direction;const auto length=std::hypot(direction.X,direction.Y,direction.Z);const Vec3 d{direction.X/length,direction.Y/length,direction.Z/length};
    const Mat4 view=Mat4::LookAt({center.X-d.X*(radius*2+10),center.Y-d.Y*(radius*2+10),center.Z-d.Z*(radius*2+10)},center,std::abs(d.Y) > .98f ? Vec3{0,0,1}:Vec3{0,1,0});
    Bounds b;bool initial=true;for (unsigned mask=0;mask<8;++mask) {const auto p=view.TransformPoint({mask&1?world.Max.X:world.Min.X,mask&2?world.Max.Y:world.Min.Y,mask&4?world.Max.Z:world.Min.Z});if (initial) {b={p,p};initial=false;}b.Min={std::min(b.Min.X,p.X),std::min(b.Min.Y,p.Y),std::min(b.Min.Z,p.Z)};b.Max={std::max(b.Max.X,p.X),std::max(b.Max.Y,p.Y),std::max(b.Max.Z,p.Z)};}
    const auto margin=std::max(.05f,radius*.02f);const float left=b.Min.X-margin,right=b.Max.X+margin,bottom=b.Min.Y-margin,top=b.Max.Y+margin,near=std::max(.01f,-b.Max.Z-margin),far=std::max(near+.01f,-b.Min.Z+margin);
    Mat4 projection=Mat4::Identity();projection.Elements[0]=2/(right-left);projection.Elements[5]=2/(top-bottom);projection.Elements[10]=1/(near-far);projection.Elements[12]=-(right+left)/(right-left);projection.Elements[13]=-(top+bottom)/(top-bottom);projection.Elements[14]=near/(near-far);
    return projection*view;
}
void SceneRenderer::Clear() noexcept {batches_.clear();models_.clear(); stats_ = {}; }
void SceneRenderer::Inspect(std::vector<DebugResource>& r) const {for (const auto& [key,model]:models_) {(void)key;for (const auto& mesh:model->Meshes) {for (const auto& p:mesh) {p->Inspect(r);}}for (const auto& image:model->Images) {InspectImage(r,image.Image,"Asset texture (base/normal)");}}for (const auto& [key,batch]:batches_) {(void)key;InspectBuffer(r,batch->Buffer,"Static instance transforms");}}

} // namespace lv::detail
