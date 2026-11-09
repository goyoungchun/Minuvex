#include "renderer/SceneRenderer.h"

#include "renderer/MeshRenderer.h"
#include "renderer/TextureData.h"

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
        Images.resize(Asset->Images.size());
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
                renderer->Initialize(primitive.Geometry, material.DoubleSided,primitive.HasSkinning,settings.MsaaSamples);
                primitives.push_back(std::move(renderer));
                if (!material.BaseColorTexture) { continue; }
                const auto textureIndex = *material.BaseColorTexture;
                if (Samplers.at(textureIndex).id) { continue; }
                const auto& texture = Asset->Textures.at(textureIndex);
                auto& gpuImage = Images.at(texture.ImageIndex);
                if (!gpuImage.Image.id) {
                    const auto& image = Asset->Images.at(texture.ImageIndex);
                    const auto packed=PackTexture(image,std::min(settings.MaxTextureSize,sg_query_limits().max_image_size_2d));
                    sg_image_desc desc{};
                    desc.width = packed.Width; desc.height = packed.Height;
                    desc.num_mipmaps = packed.Levels;
                    desc.pixel_format = SG_PIXELFORMAT_SRGB8A8;
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
};

SceneRenderer::SceneRenderer(RenderSettings settings) : settings_(settings) {}
SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::Prepare(const Scene& scene) {
    if (!sg_isvalid()) { throw std::logic_error("Initialize Sokol GFX before preparing a Scene."); }
    std::unordered_set<const ModelAsset*> needed;
    for (const auto& item : scene.CollectRenderItems()) {
        needed.insert(item.Model.get());
        if (!models_.contains(item.Model.get())) {
            auto model = std::make_unique<GpuModel>();
            model->Upload(item.Model,settings_);
            models_.emplace(item.Model.get(), std::move(model));
        }
    }
    std::erase_if(models_, [&needed](const auto& entry) { return !needed.contains(entry.first); });
    stats_.ResidentModels = models_.size();
    stats_.ResidentPrimitives = stats_.ResidentImages = 0;
    stats_.MaxResidentTextureSize=0;
    for (const auto& [key, model] : models_) {
        (void)key;
        for (const auto& mesh : model->Meshes) { stats_.ResidentPrimitives += mesh.size(); }
        for (const auto& image : model->Images) { if (image.Image.id) { ++stats_.ResidentImages;stats_.MaxResidentTextureSize=std::max(stats_.MaxResidentTextureSize,image.Size); } }
    }
}

void SceneRenderer::Draw(const Scene& scene, float aspect) {
    const auto viewProjection = scene.MainCamera.ViewProjection(aspect);
    scene.Sun.Validate();
    stats_.LastDrawCalls = 0;
    stats_.LastCulledParts=0;
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
            if (settings_.FrustumCulling) {
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
                model.Meshes.at(part.MeshIndex).at(i)->Draw(transform, viewProjection, scene.Sun, material, texture, sampler,palette,settings_.Lighting == LightingMode::Unlit);
                ++stats_.LastDrawCalls;
            }
        }
    }
}

void SceneRenderer::Clear() noexcept { models_.clear(); stats_ = {}; }

} // namespace lv::detail
