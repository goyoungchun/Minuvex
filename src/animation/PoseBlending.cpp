#include "AnimationEvaluation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lv {
namespace {
void Require(bool valid, const char* message) { if (!valid) { throw std::invalid_argument(message); } }
bool Has(PoseChannels channels, PoseChannels bit) { return (channels & bit) != PoseChannels::None; }
bool Valid(PoseChannels channels) { return (static_cast<unsigned>(channels) & ~7u) == 0; }
Transform Resolve(const SkeletonJoint& joint, const Transform& local, PoseChannels channels) {
    auto result = joint.ReferenceLocal;
    if (Has(channels, PoseChannels::Translation)) { result.Position = local.Position; }
    if (Has(channels, PoseChannels::Rotation)) { result.Rotation = local.Rotation; }
    if (Has(channels, PoseChannels::Scale)) { result.Scale = local.Scale; }
    return result;
}
void ValidateBuffer(const SkeletonAsset& skeleton, const PoseBuffer& pose, std::uint64_t signature) {
    Require(pose.SkeletonSignature == signature, "Pose buffer belongs to a different skeleton/reference hierarchy.");
    Require(pose.LocalTransforms.size() == skeleton.Joints.size() && pose.Channels.size() == skeleton.Joints.size(),
        "Pose buffer requires one transform and channel mask per node.");
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        Require(Valid(pose.Channels[i]), "Invalid pose channel bits.");
        const auto& joint = skeleton.Joints[i];
        Require(joint.Editable || pose.Channels[i] == PoseChannels::None, "Pose buffer targets a read-only matrix node.");
        if (joint.Editable) { (void)Resolve(joint, pose.LocalTransforms[i], pose.Channels[i]).Matrix().NormalMatrix(); }
    }
}
PoseBuffer ResolveBuffer(const SkeletonAsset& skeleton, const PoseBuffer& pose) {
    auto result = pose;
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        result.LocalTransforms[i] = Resolve(skeleton.Joints[i], pose.LocalTransforms[i], pose.Channels[i]);
    }
    return result;
}
Vec3 Lerp(Vec3 a, Vec3 b, float weight) {
    return {std::lerp(a.X,b.X,weight), std::lerp(a.Y,b.Y,weight), std::lerp(a.Z,b.Z,weight)};
}
bool SameSigns(Vec3 a, Vec3 b) {
    return std::signbit(a.X) == std::signbit(b.X) && std::signbit(a.Y) == std::signbit(b.Y) && std::signbit(a.Z) == std::signbit(b.Z);
}
}

namespace detail {
PoseBuffer SampleAnimation(const SkeletonAsset& skeleton, const AnimationClip* clip, double seconds) {
    PoseBuffer result; result.SkeletonSignature = SkeletonSignature(skeleton);
    result.Channels.resize(skeleton.Joints.size(), PoseChannels::None);
    result.LocalTransforms.reserve(skeleton.Joints.size());
    for (const auto& joint : skeleton.Joints) { result.LocalTransforms.push_back(joint.ReferenceLocal); }
    if (!clip) { return result; }
    const auto time = std::min(seconds, clip->Duration);
    for (const auto& track : clip->Tracks) {
        const auto end = std::upper_bound(track.Times.begin(), track.Times.end(), time);
        const auto left = end == track.Times.begin() ? 0 : static_cast<std::size_t>(end-track.Times.begin()-1);
        const auto right = std::min(left+1, track.Times.size()-1);
        const float factor = track.Interpolation == AnimationInterpolation::Step || right == left || time <= track.Times.front() ? 0 :
            static_cast<float>(std::clamp((time-track.Times[left])/(double(track.Times[right])-track.Times[left]),0.0,1.0));
        const auto& a = track.Values[left]; const auto& b = track.Values[right]; auto& local = result.LocalTransforms[track.Joint];
        auto& channels = result.Channels[track.Joint];
        if (track.Path == AnimationPath::Rotation) {
            local.Rotation = Slerp({a[0],a[1],a[2],a[3]}, {b[0],b[1],b[2],b[3]}, factor);
            channels = channels | PoseChannels::Rotation;
        } else {
            const Vec3 value = Lerp({a[0],a[1],a[2]}, {b[0],b[1],b[2]}, factor);
            if (track.Path == AnimationPath::Translation) { local.Position = value; channels = channels | PoseChannels::Translation; }
            else { local.Scale = value; channels = channels | PoseChannels::Scale; }
        }
    }
    return result;
}
}

void ValidatePoseBuffer(const SkeletonAsset& skeleton, const PoseBuffer& pose) {
    ValidateSkeleton(skeleton); ValidateBuffer(skeleton, pose, SkeletonSignature(skeleton));
}
PoseBuffer ReferencePoseBuffer(const SkeletonAsset& skeleton) {
    ValidateSkeleton(skeleton); return detail::SampleAnimation(skeleton, nullptr, 0);
}
PoseBuffer EvaluateAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip, double seconds) {
    Require(std::isfinite(seconds) && seconds >= 0, "Animation sample time must be finite and nonnegative.");
    ValidateSkeleton(skeleton); ValidateAnimation(skeleton, clip);
    return detail::SampleAnimation(skeleton, &clip, seconds);
}
PoseBuffer BlendPoses(const SkeletonAsset& skeleton, const PoseBuffer& from, const PoseBuffer& to,
    float weight, std::span<const PoseChannels> channelMask) {
    Require(std::isfinite(weight) && weight >= 0 && weight <= 1, "Pose blend weight must be in [0,1].");
    ValidateSkeleton(skeleton); const auto signature = SkeletonSignature(skeleton);
    ValidateBuffer(skeleton, from, signature); ValidateBuffer(skeleton, to, signature);
    Require(channelMask.empty() || channelMask.size() == skeleton.Joints.size(), "Blend mask requires one entry per node.");
    auto result = ResolveBuffer(skeleton, from);
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        const auto selected = channelMask.empty() ? PoseChannels::All : channelMask[i];
        Require(Valid(selected), "Invalid blend channel mask bits.");
        if (!skeleton.Joints[i].Editable) { continue; }
        const auto target = Resolve(skeleton.Joints[i], to.LocalTransforms[i], to.Channels[i]);
        auto& local = result.LocalTransforms[i];
        const auto active = (from.Channels[i] | to.Channels[i]) & selected;
        if (Has(active, PoseChannels::Translation)) { local.Position = Lerp(local.Position, target.Position, weight); }
        if (Has(active, PoseChannels::Rotation)) { local.Rotation = Slerp(local.Rotation, target.Rotation, weight); }
        if (Has(active, PoseChannels::Scale)) {
            Require(SameSigns(local.Scale, target.Scale), "Blend cannot cross a singular zero scale.");
            local.Scale = Lerp(local.Scale, target.Scale, weight);
        }
        result.Channels[i] = from.Channels[i] | (to.Channels[i] & selected);
        (void)local.Matrix().NormalMatrix();
    }
    return result;
}
PoseBuffer ApplyPoseOverrides(const SkeletonAsset& skeleton, const PoseBuffer& base, const PoseAsset& overrides) {
    ValidateSkeleton(skeleton); ValidateBuffer(skeleton, base, SkeletonSignature(skeleton)); ValidatePose(skeleton, overrides);
    auto result = ResolveBuffer(skeleton, base);
    for (const auto& joint : overrides.Joints) { result.LocalTransforms[joint.Joint] = joint.Local; result.Channels[joint.Joint] = PoseChannels::All; }
    return result;
}
EvaluatedPose ComposePose(const SkeletonAsset& skeleton, const PoseBuffer& pose) {
    ValidatePoseBuffer(skeleton, pose); EvaluatedPose result;
    result.Local.resize(skeleton.Joints.size()); result.Global.resize(skeleton.Joints.size());
    std::vector<bool> visited(skeleton.Joints.size(), false);
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        const auto& joint = skeleton.Joints[i];
        result.Local[i] = joint.Editable ? Resolve(joint, pose.LocalTransforms[i], pose.Channels[i]).Matrix() : joint.ReferenceMatrix;
    }
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        std::vector<JointId> chain; auto current = static_cast<JointId>(i);
        while (current != NoJoint && !visited[current]) { chain.push_back(current); current = skeleton.Joints[current].Parent; }
        auto parent = current == NoJoint ? Mat4::Identity() : result.Global[current];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            parent = parent * result.Local[*it]; (void)parent.NormalMatrix(); result.Global[*it] = parent; visited[*it] = true;
        }
    }
    return result;
}
}
