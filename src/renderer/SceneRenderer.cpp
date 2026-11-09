#include "renderer/SceneRenderer.h"

#include "renderer/MeshRenderer.h"

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

float ToLinear(float value) { return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); }
float ToSrgb(float value) { return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f; }

std::vector<std::uint8_t> PackMipmaps(const ImageAsset& image, int& levels) {
    auto pixels = image.Rgba;
    auto previous = image.Rgba;
    int width = image.Width, height = image.Height;
    levels = 1;
    while (width > 1 || height > 1) {
        const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
        std::vector<std::uint8_t> next(static_cast<std::size_t>(nextWidth) * nextHeight * 4);
        for (int y = 0; y < nextHeight; ++y) {
            for (int x = 0; x < nextWidth; ++x) {
                float sum[4]{};
                int samples = 0;
                for (int sy = y * height / nextHeight; sy < (y + 1) * height / nextHeight; ++sy) {
                    for (int sx = x * width / nextWidth; sx < (x + 1) * width / nextWidth; ++sx) {
                        const auto offset = (static_cast<std::size_t>(sy) * width + sx) * 4;
                        for (int c = 0; c < 4; ++c) {
                            const float value = previous[offset + c] / 255.0f;
                            sum[c] += c < 3 ? ToLinear(value) : value;
                        }
                        ++samples;
                    }
                }
                for (int c = 0; c < 4; ++c) {
                    float value = sum[c] / static_cast<float>(samples);
                    if (c < 3) { value = ToSrgb(value); }
                    next[(static_cast<std::size_t>(y) * nextWidth + x) * 4 + c] = static_cast<std::uint8_t>(std::clamp(std::lround(value * 255), 0L, 255L));
                }
            }
        }
        pixels.insert(pixels.end(), next.begin(), next.end());
        previous = std::move(next);
        width = nextWidth; height = nextHeight; ++levels;
    }
    return pixels;
}

} // namespace

struct SceneRenderer::GpuModel {
    ModelHandle Asset;
    std::vector<std::vector<std::unique_ptr<MeshRenderer>>> Meshes;
    struct GpuImage { sg_image Image{}; sg_view View{}; };
    std::vector<GpuImage> Images;
    std::vector<sg_sampler> Samplers;

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

    void Upload(ModelHandle asset) {
        Asset = std::move(asset);
        Meshes.resize(Asset->Meshes.size());
        Images.resize(Asset->Images.size());
        Samplers.resize(Asset->Textures.size());
        for (const auto& part : Asset->Parts) {
            auto& primitives = Meshes.at(part.MeshIndex);
            if (!primitives.empty()) { continue; }
            for (const auto& primitive : Asset->Meshes.at(part.MeshIndex).Primitives) {
                const auto& material = Asset->Materials.at(primitive.MaterialIndex);
                auto renderer = std::make_unique<MeshRenderer>();
                renderer->Initialize(primitive.Geometry, material.DoubleSided);
                primitives.push_back(std::move(renderer));
                if (!material.BaseColorTexture) { continue; }
                const auto textureIndex = *material.BaseColorTexture;
                if (Samplers.at(textureIndex).id) { continue; }
                const auto& texture = Asset->Textures.at(textureIndex);
                auto& gpuImage = Images.at(texture.ImageIndex);
                if (!gpuImage.Image.id) {
                    const auto& image = Asset->Images.at(texture.ImageIndex);
                    if (image.Width > sg_query_limits().max_image_size_2d || image.Height > sg_query_limits().max_image_size_2d) {
                        throw std::runtime_error("Texture dimensions exceed this GPU's capabilities.");
                    }
                    int levels = 0;
                    const auto pixels = PackMipmaps(image, levels);
                    sg_image_desc desc{};
                    desc.width = image.Width; desc.height = image.Height;
                    desc.num_mipmaps = levels;
                    desc.pixel_format = SG_PIXELFORMAT_SRGB8A8;
                    desc.data = {pixels.data(), pixels.size()};
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
                if (!texture.UseMipmaps) { sampler.max_lod = 0.25f; }
                Samplers[textureIndex] = sg_make_sampler(sampler);
                if (sg_query_sampler_state(Samplers[textureIndex]) != SG_RESOURCESTATE_VALID) { throw std::runtime_error("Failed to create the texture sampler."); }
            }
        }
    }
};

SceneRenderer::SceneRenderer() = default;
SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::Prepare(const Scene& scene) {
    if (!sg_isvalid()) { throw std::logic_error("Initialize Sokol GFX before preparing a Scene."); }
    std::unordered_set<const ModelAsset*> needed;
    for (const auto& item : scene.CollectRenderItems()) {
        needed.insert(item.Model.get());
        if (!models_.contains(item.Model.get())) {
            auto model = std::make_unique<GpuModel>();
            model->Upload(item.Model);
            models_.emplace(item.Model.get(), std::move(model));
        }
    }
    std::erase_if(models_, [&needed](const auto& entry) { return !needed.contains(entry.first); });
    stats_.ResidentModels = models_.size();
    stats_.ResidentPrimitives = stats_.ResidentImages = 0;
    for (const auto& [key, model] : models_) {
        (void)key;
        for (const auto& mesh : model->Meshes) { stats_.ResidentPrimitives += mesh.size(); }
        for (const auto& image : model->Images) { if (image.Image.id) { ++stats_.ResidentImages; } }
    }
}

void SceneRenderer::Draw(const Scene& scene, float aspect) {
    const auto viewProjection = scene.MainCamera.ViewProjection(aspect);
    scene.Sun.Validate();
    stats_.LastDrawCalls = 0;
    for (const auto& item : scene.CollectRenderItems()) {
        const auto found = models_.find(item.Model.get());
        if (found == models_.end()) { throw std::logic_error("Call SceneRenderer::Prepare after changing visible model instances."); }
        const auto& model = *found->second;
        for (const auto& part : item.Model->Parts) {
            const auto transform = item.WorldTransform * part.Transform;
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
                model.Meshes.at(part.MeshIndex).at(i)->Draw(transform, viewProjection, scene.Sun, material, texture, sampler);
                ++stats_.LastDrawCalls;
            }
        }
    }
}

void SceneRenderer::Clear() noexcept { models_.clear(); stats_ = {}; }

} // namespace lv::detail
