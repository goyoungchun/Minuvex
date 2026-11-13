#pragma once

#include <minvex/Math.h>
#include <minvex/Mesh.h>
#include <minvex/Scene.h>
#include <minvex/Quality.h>
#include <minvex/Debug.h>
#include <minvex/Optimization.h>

#include <sokol_gfx.h>
#include <span>
#if defined(__EMSCRIPTEN__)
#include <map>
#endif

namespace lv::detail {

#if defined(__EMSCRIPTEN__)
inline constexpr std::size_t ShaderSkinJoints=48; // Fits WebGL2's minimum vertex-uniform limit.
#else
inline constexpr std::size_t ShaderSkinJoints=MaxSkinJoints;
#endif

struct alignas(16) ShaderVec4 { float X, Y, Z, W; };
struct ShadowSampling {
    Mat4 ViewProjection=Mat4::Identity();
    sg_view Texture{};sg_sampler Sampler{};
    RenderSettings Settings;
    Vec3 Extent{1,1,1}; // Orthographic width, height and linear depth range, in world units.
};
struct MeshLighting {
    sg_view NormalTexture{};sg_sampler NormalSampler{};
    std::span<const PointLight> Points;
    std::span<const SpotLight> Spots;
    const ShadowSampling* Shadow=nullptr;
    bool Advanced=false;
    DebugOptions Debug;
    float NearPlane=.1f,FarPlane=1000;
    bool Orthographic=false;
    bool Pbr=false;
    Vec3 Eye{},ViewDirection{0,0,1};
    sg_view MetallicRoughnessTexture{};sg_sampler MetallicRoughnessSampler{};
    std::span<const float> MorphWeights;
    ObjectId Object=NoObject;JointId Node=NoJoint;
    sg_buffer Instances{};int InstanceCount=0;unsigned Lod=0;bool Mirrored=false;
};

// Internal mesh renderer. The caller owns the graphics context and pass.
// All calls and destruction must occur on the context's rendering thread.
class MeshRenderer final {
public:
    MeshRenderer() = default;
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    void Initialize(const MeshData& mesh, bool doubleSided = false, bool skinned = false, int samples = 1, sg_pixel_format colorFormat=SG_PIXELFORMAT_NONE, bool shadow=false,std::span<const MeshLod> lods={},std::span<const MorphTarget> morphs={},const MeshPrimitive* source=nullptr);
#if defined(__EMSCRIPTEN__)
    void PruneWebMorph(std::span<const ObjectId> live);
    std::uint64_t WebMorphBytes() const noexcept;
#endif
    void Draw(const Mat4& model, const Mat4& viewProjection, const DirectionalLight& light = {},
              const MaterialAsset& material = {}, sg_view texture = {}, sg_sampler sampler = {}, std::span<const Mat4> palette = {}, bool forceUnlit = false, const MeshLighting& lighting={}) const;
    void DrawShadow(const Mat4& model,const Mat4& lightViewProjection,const MaterialAsset& material,sg_view texture,sg_sampler sampler,std::span<const Mat4> palette,const MeshLighting& instances={}) const;
    void Shutdown() noexcept;
    void PrepareWireframe(const MeshData& mesh);
    void Inspect(std::vector<DebugResource>& resources) const;
    void PrepareInstances();
    void PrepareDoubleSided();
    std::size_t TriangleCount(unsigned lod=0) const noexcept {return (lod && lod<=lodCounts_.size() ? lodCounts_[lod-1]:indexCount_)/3;}

private:
    sg_buffer vertices_{};
    sg_buffer indices_{};
    sg_shader shader_{};
    sg_pipeline pipelines_[2]{};
    sg_pipeline doubleSidedPipelines_[2]{},doubleSidedShadowPipelines_[2]{};
    bool doubleSided_=false;
    sg_buffer morphBuffer_{};sg_view morphView_{};
    std::size_t morphCount_=0,morphVertices_=0;
    void ApplyMorph(std::span<const float> weights) const;
#if defined(__EMSCRIPTEN__)
    const MeshPrimitive* webSource_=nullptr;std::vector<std::uint32_t> webRemap_;
    struct WebStream {sg_buffer Buffer{};std::vector<float> Weights;std::vector<Vertex> Vertices;std::uint32_t Frame=UINT32_MAX;};
    mutable std::map<std::pair<ObjectId,JointId>,WebStream> webStreams_;
    sg_buffer WebMorph(const MeshLighting& lighting) const;
#endif
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
    std::vector<sg_buffer> lodBuffers_;std::vector<int> lodCounts_;
    sg_shader instanceShader_{};sg_pipeline instancePipelines_[2]{};
    sg_shader shadowInstanceShader_{};sg_pipeline shadowInstancePipelines_[2]{};
};

} // namespace lv::detail
