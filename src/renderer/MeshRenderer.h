#pragma once

#include <minuvex/Math.h>
#include <minuvex/Mesh.h>
#include <minuvex/Scene.h>
#include <minuvex/Quality.h>
#include <minuvex/Debug.h>

#include <sokol_gfx.h>
#include <span>

namespace lv::detail {

struct alignas(16) ShaderVec4 { float X, Y, Z, W; };
struct ShadowSampling {
    Mat4 ViewProjection=Mat4::Identity();
    sg_view Texture{};sg_sampler Sampler{};
    ShadowMode Mode=ShadowMode::Off;
    int Resolution=1024;
};
struct MeshLighting {
    sg_view NormalTexture{};sg_sampler NormalSampler{};
    std::span<const PointLight> Points;
    std::span<const SpotLight> Spots;
    const ShadowSampling* Shadow=nullptr;
    bool Advanced=false;
    DebugOptions Debug;
    float NearPlane=.1f,FarPlane=1000;
};

// Internal mesh renderer. The caller owns the graphics context and pass.
// All calls and destruction must occur on the context's rendering thread.
class MeshRenderer final {
public:
    MeshRenderer() = default;
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    void Initialize(const MeshData& mesh, bool doubleSided = false, bool skinned = false, int samples = 1, sg_pixel_format colorFormat=SG_PIXELFORMAT_NONE, bool shadow=false);
    void Draw(const Mat4& model, const Mat4& viewProjection, const DirectionalLight& light = {},
              const MaterialAsset& material = {}, sg_view texture = {}, sg_sampler sampler = {}, std::span<const Mat4> palette = {}, bool forceUnlit = false, const MeshLighting& lighting={}) const;
    void DrawShadow(const Mat4& model,const Mat4& lightViewProjection,const MaterialAsset& material,sg_view texture,sg_sampler sampler,std::span<const Mat4> palette) const;
    void Shutdown() noexcept;
    void PrepareWireframe(const MeshData& mesh);
    void Inspect(std::vector<DebugResource>& resources) const;

private:
    sg_buffer vertices_{};
    sg_buffer indices_{};
    sg_shader shader_{};
    sg_pipeline pipelines_[2]{};
    sg_shader shadowShader_{};sg_pipeline shadowPipelines_[2]{};
    sg_sampler nearestSampler_{};
    sg_image whiteImage_{};
    sg_view whiteView_{};
    sg_sampler whiteSampler_{};
    int indexCount_ = 0;
    bool skinned_ = false;
    sg_buffer edges_{};
    sg_pipeline wirePipelines_[2]{};
    int edgeIndexCount_=0;
};

} // namespace lv::detail
