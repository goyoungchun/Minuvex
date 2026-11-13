#include <minvex/Skeleton.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace lv {
namespace {
void Name(std::string_view name) {
    if (name.empty() || name.size() > 256 || name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Socket and bone names require 1..256 bytes without NUL.");
    }
}
JointId BoneParent(const SkeletonAsset& skeleton, JointId node) {
    auto parent = skeleton.Joints.at(node).Parent;
    while (parent != NoJoint && !skeleton.Joints.at(parent).IsJoint) { parent = skeleton.Joints[parent].Parent; }
    return parent;
}
std::unordered_map<std::string, JointId> Bones(const SkeletonAsset& skeleton) {
    std::unordered_map<std::string, JointId> result;
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        const auto& joint = skeleton.Joints[i];
        if (!joint.IsJoint) { continue; }
        Name(joint.Name);
        if (!result.emplace(joint.Name, static_cast<JointId>(i)).second) { throw std::invalid_argument("Shared Skeleton requires unique bone names."); }
    }
    if (result.empty()) { throw std::invalid_argument("Shared Skeleton requires at least one bone."); }
    return result;
}
}
JointId FindSkeletonJoint(const SkeletonAsset& skeleton, std::string_view name) {
    Name(name); JointId result = NoJoint;
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        if (skeleton.Joints[i].Name != name) { continue; }
        if (result != NoJoint) { throw std::invalid_argument("Skeleton node name is ambiguous."); }
        result = static_cast<JointId>(i);
    }
    if (result == NoJoint) { throw std::invalid_argument("Skeleton node does not exist."); }
    return result;
}
void ValidateSkeletonSocket(const SkeletonAsset& skeleton, const SkeletonSocket& socket) {
    Name(socket.Name); (void)FindSkeletonJoint(skeleton, socket.Joint); (void)socket.LocalTransform.Matrix().NormalMatrix();
}
SharedSkeletonHandle CreateSharedSkeleton(SkeletonAsset skeleton, std::vector<SkeletonSocket> sockets) {
    ValidateSkeleton(skeleton); (void)Bones(skeleton);
    if (sockets.size() > 256) { throw std::invalid_argument("Shared Skeleton supports at most 256 sockets."); }
    std::unordered_map<std::string, bool> names;
    for (const auto& socket : sockets) {
        ValidateSkeletonSocket(skeleton, socket);
        if (!skeleton.Joints[FindSkeletonJoint(skeleton, socket.Joint)].IsJoint || !names.emplace(socket.Name, true).second) {
            throw std::invalid_argument("Shared socket requires a bone and a unique socket name.");
        }
    }
    skeleton.Signature = SkeletonSignature(skeleton);
    auto result = std::shared_ptr<SharedSkeletonAsset>(new SharedSkeletonAsset);
    result->skeleton_ = std::move(skeleton); result->sockets_ = std::move(sockets);
    return result;
}
SkeletonBinding BindSkeleton(const SharedSkeletonAsset& shared, const SkeletonAsset& model) {
    const auto& canonical = shared.Skeleton(); ValidateSkeleton(model);
    const auto source = Bones(canonical), target = Bones(model);
    const auto canonicalPose = ComposePose(canonical, ReferencePoseBuffer(canonical));
    const auto modelPose = ComposePose(model, ReferencePoseBuffer(model));
    SkeletonBinding result;
    result.ModelToShared.resize(model.Joints.size(), NoJoint); result.SharedToModel.resize(canonical.Joints.size(), NoJoint);
    for (const auto& [name, node] : target) {
        const auto found = source.find(name);
        if (found == source.end()) { throw std::invalid_argument("Model contains a bone absent from Shared Skeleton."); }
        const auto bone = found->second;
        const auto a = BoneParent(canonical, bone), b = BoneParent(model, node);
        if ((a == NoJoint) != (b == NoJoint) || (a != NoJoint && canonical.Joints[a].Name != model.Joints[b].Name)) {
            throw std::invalid_argument("Shared Skeleton bone hierarchy differs.");
        }
        for (std::size_t i = 0; i < 16; ++i) {
            const auto x = canonicalPose.Global[bone].Elements[i], y = modelPose.Global[node].Elements[i];
            if (std::abs(double(x) - y) > 1e-5 * std::max({1.0, std::abs(double(x)), std::abs(double(y))})) {
                throw std::invalid_argument("Shared Skeleton model-space reference poses differ; retargeting is required.");
            }
        }
        result.ModelToShared[node] = bone; result.SharedToModel[bone] = node;
    }
    for (const auto& socket : shared.Sockets()) { ValidateSkeletonSocket(model, socket); }
    return result;
}
} // namespace lv
