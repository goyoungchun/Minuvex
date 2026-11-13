#pragma once
#include <minvex/Animation.h>

namespace lv {
struct SkeletonSocket {
    std::string Name, Joint;
    Transform LocalTransform;
};

class SharedSkeletonAsset;
using SharedSkeletonHandle = std::shared_ptr<const SharedSkeletonAsset>;

// Immutable canonical bone hierarchy/reference pose, shared by modular meshes.
// Model-specific non-bone nodes remain in each ModelAsset.
class SharedSkeletonAsset {
public:
    const SkeletonAsset& Skeleton() const noexcept { return skeleton_; }
    std::span<const SkeletonSocket> Sockets() const noexcept { return sockets_; }
private:
    SharedSkeletonAsset() = default;
    SkeletonAsset skeleton_;
    std::vector<SkeletonSocket> sockets_;
    friend SharedSkeletonHandle CreateSharedSkeleton(SkeletonAsset, std::vector<SkeletonSocket>);
};
struct SkeletonBinding {
    std::vector<JointId> ModelToShared, SharedToModel; // NoJoint for model-specific/missing nodes.
};
SharedSkeletonHandle CreateSharedSkeleton(SkeletonAsset skeleton, std::vector<SkeletonSocket> sockets = {});
// Names, nearest bone ancestry and model-space reference matrices must match.
// Reordered/subset bones and unrelated mesh nodes are supported; no retargeting.
SkeletonBinding BindSkeleton(const SharedSkeletonAsset& shared, const SkeletonAsset& model);
JointId FindSkeletonJoint(const SkeletonAsset& skeleton, std::string_view name); // Rejects missing/ambiguous names.
void ValidateSkeletonSocket(const SkeletonAsset& skeleton, const SkeletonSocket& socket);
} // namespace lv
