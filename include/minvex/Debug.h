#pragma once
#include <minvex/Scene.h>
#include <minvex/Quality.h>
#include <minvex/Optimization.h>
#include <span>

namespace lv {
enum class DebugView { Lit, Unlit, Wireframe, Normal, UV, Depth, ShadowMap, BoneWeight, Skeleton, BoundingBox,
    ShadowBlockers, ShadowBlockerDepth, ShadowPenumbra, ShadowVisibility };
const char* DebugViewName(DebugView view) noexcept;
struct DebugOptions {
    DebugView View=DebugView::Lit;
    bool ShowSkeleton=false, ShowBounds=false, ShowReferencePose=false, DepthTest=true;
    int WeightJoint=-1; // Skin palette index; -1 displays the strongest influence. Rigid vertices = zero.
    bool operator==(const DebugOptions&) const = default;
};
void ValidateDebugOptions(const DebugOptions& options); // invalid_argument on invalid enum/joint.
using DebugColor=std::array<float,4>;
struct DebugLine {Vec3 Start, End;DebugColor Color{1,1,1,1};};
inline constexpr std::size_t MaxDebugLines=65536;
// CPU-only persistent command list. Renderers copy it; Clear does not affect previously submitted copies.
// Finite geometry, nonnegative radii and [0,1] colors required. Invalid input throws invalid_argument;
// exceeding the line budget throws length_error. Each Draw operation is transactional.
class DebugDraw {
public:
    void DrawLine(Vec3 start,Vec3 end,DebugColor color={1,1,1,1});
    void DrawBox(Bounds bounds,DebugColor color={1,1,1,1},const Mat4& transform=Mat4::Identity());
    void DrawBoundingBox(Bounds bounds,DebugColor color={1,1,1,1},const Mat4& transform=Mat4::Identity()) {DrawBox(bounds,color,transform);}
    void DrawSphere(Vec3 center,float radius,DebugColor color={1,1,1,1},unsigned segments=24);
    void DrawCapsule(Vec3 start,Vec3 end,float radius,DebugColor color={1,1,1,1},unsigned segments=24);
    void DrawArrow(Vec3 start,Vec3 end,DebugColor color={1,1,1,1});
    void DrawAxis(const Mat4& transform,float length=1);
    void DrawCameraFrustum(const Camera& camera,float aspect,DebugColor color={1,1,1,1});
    void DrawSkeleton(const SkeletonAsset& skeleton,const EvaluatedPose& pose,const Mat4& world=Mat4::Identity(),DebugColor color={1,1,0,1});
    void Clear() noexcept {lines_.clear();}
    std::span<const DebugLine> Lines() const noexcept {return lines_;}
private:
    std::vector<DebugLine> lines_;
    void Append(std::span<const DebugLine> lines);
};
enum class DebugResourceKind { RenderTarget, Texture, Buffer, Shader, Pipeline };
enum class DebugResourceState { Valid, Invalid, Allocated, Failed };
struct DebugResource {
    DebugResourceKind Kind=DebugResourceKind::Buffer;
    std::string Name, Format;
    std::uint32_t Id=0; // Diagnostic identity only; never a native handle or ownership transfer.
    DebugResourceState State=DebugResourceState::Invalid;
    int Width=0,Height=0,Samples=1;
    std::uint64_t Bytes=0; // Logical size, zero when unknown/not applicable.
};
struct DebugPass {std::string Name,Shader,Pipeline;std::uint64_t DrawCalls=0,Triangles=0;};
struct RenderDebugInfo {
    bool Live=false;
    DebugOptions Options;
    std::uint64_t RenderedFrames=0,LineCount=0,LineDrawCalls=0,DebugBufferBytes=0;
    std::vector<DebugPass> Passes;
    std::vector<DebugResource> Resources;
    OptimizationStatistics Optimization;
};
struct BoneInfluenceStatistics {
    std::size_t SkinnedVertices=0,RigidVertices=0,UnweightedVertices=0;
    std::array<std::size_t,5> InfluenceCounts{}; // Nonzero weights per skinned vertex, 0..4.
    double MinimumWeightSum=0,MaximumWeightSum=0;
};
BoneInfluenceStatistics InspectBoneInfluences(const ModelAsset& model);
struct JointDebugInfo {Mat4 Local,Global,ReferenceLocal,ReferenceGlobal;std::vector<Mat4> SkinningMatrices;};
// Palette matrices for each skin referencing this skeleton node, in model space.
JointDebugInfo InspectJoint(const AnimationPlayer& player,JointId joint);
JointDebugInfo InspectJoint(const Scene& scene,ObjectId object,JointId joint); // Includes Leader Pose.
} // namespace lv
