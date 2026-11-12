#pragma once
#include <minuvex/Math.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lv {
using JointId = std::uint32_t;
inline constexpr JointId NoJoint = UINT32_MAX;
inline constexpr std::size_t MaxSkinJoints = 128;
struct ModelAsset;
using ModelHandle = std::shared_ptr<const ModelAsset>;

// Includes intermediary transform nodes so animated non-joint ancestors survive.
struct SkeletonJoint {
    std::string Name;
    JointId Parent = NoJoint;
    Transform ReferenceLocal;
    Mat4 ReferenceMatrix = Mat4::Identity();
    bool Editable = true; // Matrix-authored shear nodes are retained, read-only.
    bool IsJoint = false;
    bool MatrixAuthored = false; // Signature uses original matrix, not decomposed floating-point results.
};
struct SkeletonAsset {
    std::vector<SkeletonJoint> Joints;
    std::uint64_t Signature = 0;
};
struct SkinAsset {
    std::string Name;
    std::vector<JointId> Joints; // Vertex indices address this mapping.
    std::vector<Mat4> InverseBindMatrices;
};
struct PoseJoint { JointId Joint = NoJoint; Transform Local; };
struct PoseAsset {
    std::string Name;
    std::uint64_t SkeletonSignature = 0;
    std::vector<PoseJoint> Joints; // Full or partial; missing joints keep the base pose.
};
enum class AnimationPath { Translation, Rotation, Scale };
enum class AnimationInterpolation { Step, Linear };
struct AnimationTrack {
    JointId Joint = NoJoint;
    AnimationPath Path = AnimationPath::Translation;
    AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
    std::vector<float> Times;
    std::vector<std::array<float,4>> Values;
};
struct AnimationClip {
    std::string Name;
    double Duration = 0;
    std::vector<AnimationTrack> Tracks;
};
struct EvaluatedPose { std::vector<Mat4> Local, Global; };
enum class PlaybackState { Stopped, Playing, Paused };

// Invalid data/compatibility/numerics throw std::invalid_argument.
void ValidateSkeleton(const SkeletonAsset& skeleton);
std::uint64_t SkeletonSignature(const SkeletonAsset& skeleton);
void ValidatePose(const SkeletonAsset& skeleton, const PoseAsset& pose);
void ValidateAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip);
PoseAsset ReferencePose(const SkeletonAsset& skeleton);
Quaternion Slerp(Quaternion from, Quaternion to, float amount);

// CPU-only, copyable instance state, strong immutable ModelHandle ownership.
// No shared mutable pose or playback state between Scene model instances.
class AnimationPlayer {
public:
    void Bind(ModelHandle model); // Resets clip, time, speed, loop and overrides.
    void Rebind(ModelHandle model); // Preserve compatible instance state; incompatible changes throw before replacement.
    const ModelHandle& Model() const noexcept { return model_; }
    std::optional<std::size_t> Clip() const noexcept { return clip_; }
    double Time() const noexcept { return time_; }
    double Speed() const noexcept { return speed_; }
    bool Loop() const noexcept { return loop_; }
    PlaybackState State() const noexcept { return state_; }
    const PoseAsset& Overrides() const noexcept { return overrides_; }
    bool NeedsEvaluation() const noexcept;
    void Play(std::size_t clip, bool restart = true);
    void Pause() noexcept;
    void Resume();
    void Stop() noexcept; // Keeps clip; returns to its first frame, retains overrides.
    void Seek(double seconds); // Clamped [0, duration], even in loop mode.
    void SetSpeed(double speed); // Finite [-100,100], including reverse playback.
    void SetLoop(bool loop) noexcept { loop_ = loop; }
    void Update(double deltaSeconds); // Nonnegative; explicit update for headless use.
    void ApplyPose(const PoseAsset& pose); // Replaces manual overrides, applied after animation.
    void SetJointOverride(JointId joint, const Transform& local);
    void ClearJointOverride(JointId joint);
    void ClearOverrides() noexcept;
    void ResetPose() noexcept; // Reference pose: clears clip and overrides.
    PoseAsset CapturePose(std::string name = {}) const;
    EvaluatedPose Evaluate() const;
    Bounds GetBounds() const; // Current deformed geometry bounds, for camera fitting.
private:
    ModelHandle model_;
    std::optional<std::size_t> clip_;
    double time_ = 0, speed_ = 1;
    bool loop_ = true;
    PlaybackState state_ = PlaybackState::Stopped;
    PoseAsset overrides_;
    std::vector<Transform> EvaluateLocal() const;
};
} // namespace lv
