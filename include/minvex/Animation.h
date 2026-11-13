#pragma once
#include <minvex/Math.h>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <functional>
#include <exception>
#include <stdexcept>
#include <utility>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lv {
using JointId = std::uint32_t;
inline constexpr JointId NoJoint = UINT32_MAX;
inline constexpr std::size_t MaxSkinJoints = 128;
inline constexpr std::size_t MaxMorphTargets = 8;
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
struct MorphTrack {
    JointId Node = NoJoint;
    std::size_t TargetCount = 0;
    AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
    std::vector<float> Times, Values; // Sample-major scalar weights, Times.size()*TargetCount.
};
struct MorphPose { JointId Node = NoJoint; std::vector<float> Weights; };
inline constexpr std::size_t MaxAnimationEvents = 1024, MaxAnimationEventQueue = 1024;
inline constexpr std::size_t MaxAnimationEventBytes = 1024*1024, MaxAnimationEventTracks = 256;
struct AnimationEvent { double TimeSeconds = 0; std::string Name, Payload; };
struct AnimationEventTrack { std::size_t Clip = 0; std::vector<AnimationEvent> Events; };
struct AnimationEventOccurrence {
    std::uint64_t Model = 0;
    std::size_t Clip = 0, EventIndex = 0;
    std::string ClipName;
    AnimationEvent Event; // Owned snapshot; safe after track edits/model replacement.
    bool Reverse = false;
};
enum class AnimationSeekEvents { Silent, Collect };
class AnimationEventOverflow : public std::length_error {public: using std::length_error::length_error;};
struct AnimationEventCallbackError : std::runtime_error {
    explicit AnimationEventCallbackError(std::exception_ptr cause) : std::runtime_error("Animation event callback failed."), Cause(std::move(cause)) {}
    std::exception_ptr Cause;
};
struct AnimationClip {
    std::string Name;
    double Duration = 0;
    std::vector<AnimationTrack> Tracks;
    std::vector<MorphTrack> MorphTracks;
    std::vector<AnimationEvent> Events;
};
enum class PoseChannels : std::uint8_t { None = 0, Translation = 1, Rotation = 2, Scale = 4, All = 7 };
constexpr PoseChannels operator|(PoseChannels a, PoseChannels b) noexcept {
    return static_cast<PoseChannels>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr PoseChannels operator&(PoseChannels a, PoseChannels b) noexcept {
    return static_cast<PoseChannels>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
struct PoseBuffer {
    std::uint64_t SkeletonSignature = 0;
    std::vector<Transform> LocalTransforms;
    std::vector<PoseChannels> Channels; // Per-node authored TRS bits; missing channels use reference values.
};
struct EvaluatedPose { std::vector<Mat4> Local, Global; };
struct AdditivePose {
    std::uint64_t SkeletonSignature = 0;
    std::vector<Transform> LocalDeltas; // Position difference, local rotation delta, positive Scale ratio.
    std::vector<PoseChannels> Channels;
};
enum class BlendTreeKind { Blend1D, Blend2D };
inline constexpr std::size_t MaxBlendSamples = 64;
struct BlendSample { std::size_t Clip = 0; float X = 0, Y = 0; };
struct BlendTreeDesc {
    std::string Name = "Blend Tree", ParameterX = "Speed", ParameterY = "Direction";
    BlendTreeKind Kind = BlendTreeKind::Blend1D;
    std::vector<BlendSample> Samples; // 1D: strictly increasing X. 2D: unique, non-collinear points.
};
class BlendTreeAsset;
using BlendTreeHandle = std::shared_ptr<const BlendTreeAsset>;
class BlendTreeAsset {
public:
    const BlendTreeDesc& Description() const noexcept { return description_; }
    std::span<const std::array<std::size_t,3>> Triangles() const noexcept { return triangles_; }
    std::uint64_t Signature() const noexcept { return signature_; }
private:
    BlendTreeAsset() = default;
    BlendTreeDesc description_;
    std::vector<std::array<std::size_t,3>> triangles_;
    std::vector<std::array<std::size_t,2>> hull_;
    std::vector<std::string> clipNames_;
    std::uint64_t skeleton_ = 0, morph_ = 0, signature_ = 0;
    friend BlendTreeHandle CreateBlendTree(const ModelAsset&, BlendTreeDesc);
    friend void ValidateBlendTree(const ModelAsset&, const BlendTreeAsset&);
    friend std::vector<float> EvaluateBlendWeights(const BlendTreeAsset&, float, float);
};
struct BlendTreeState {
    BlendTreeHandle Tree;
    double Phase = 0; // Normalized [0,1], shared by all samples; zero-duration clips sample at zero.
    float X = 0, Y = 0;
};
BlendTreeHandle CreateBlendTree(const ModelAsset& model, BlendTreeDesc description);
void ValidateBlendTree(const ModelAsset& model, const BlendTreeAsset& tree);
std::vector<float> EvaluateBlendWeights(const BlendTreeAsset& tree, float x, float y = 0);
PoseBuffer EvaluateBlendTree(const ModelAsset& model, const BlendTreeAsset& tree, float x, float y, double phase);
enum class PlaybackState { Stopped, Playing, Paused };
struct AdditiveAnimationState {
    std::size_t Clip = 0;
    double Time = 0, Speed = 1; // Layer speed multiplies AnimationPlayer::Speed().
    bool Loop = true;
    float Weight = 1;
    PlaybackState State = PlaybackState::Playing;
    PoseAsset Reference; // Frozen full/partial reference; empty default selects skeleton reference at setup.
    std::vector<float> JointMask; // Empty means all nodes; otherwise one [0,1] value per node.
};
struct AnimationBlendState {
    std::optional<std::size_t> SourceClip; // Live source; absent means a frozen full SourcePose.
    double SourceTime = 0;
    PoseAsset SourcePose;
    double Duration = 0, Elapsed = 0; // Seconds; 0 <= Elapsed < Duration.
    std::vector<MorphPose> SourceMorph;
};

// Invalid data/compatibility/numerics throw std::invalid_argument.
void ValidateSkeleton(const SkeletonAsset& skeleton);
std::uint64_t SkeletonSignature(const SkeletonAsset& skeleton);
void ValidatePose(const SkeletonAsset& skeleton, const PoseAsset& pose);
void ValidateAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip);
void ValidateAnimationEvents(double duration, std::span<const AnimationEvent> events);
PoseAsset ReferencePose(const SkeletonAsset& skeleton);
Quaternion Slerp(Quaternion from, Quaternion to, float amount);
void ValidatePoseBuffer(const SkeletonAsset& skeleton, const PoseBuffer& pose);
PoseBuffer ReferencePoseBuffer(const SkeletonAsset& skeleton);
// Stateless CPU sampling: finite nonnegative seconds, clamped to clip duration; no looping or manual overrides.
PoseBuffer EvaluateAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip, double seconds);
// Empty mask selects all channels; otherwise one mask per node. Unselected channels retain 'from'.
// Weight is [0,1]; selected scale channels must retain compatible signs, including at endpoints.
PoseBuffer BlendPoses(const SkeletonAsset& skeleton, const PoseBuffer& from, const PoseBuffer& to,
    float weight, std::span<const PoseChannels> channelMask = {});
PoseBuffer ApplyPoseOverrides(const SkeletonAsset& skeleton, const PoseBuffer& base, const PoseAsset& overrides);
EvaluatedPose ComposePose(const SkeletonAsset& skeleton, const PoseBuffer& pose);
void ValidateAdditivePose(const SkeletonAsset& skeleton, const AdditivePose& pose);
AdditivePose MakeAdditivePose(const SkeletonAsset& skeleton, const PoseBuffer& sample, const PoseBuffer& reference);
PoseBuffer ApplyAdditivePose(const SkeletonAsset& skeleton, const PoseBuffer& base, const AdditivePose& delta,
    float weight, std::span<const float> jointMask = {});

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
    const std::optional<AnimationBlendState>& Blend() const noexcept { return blend_; }
    const std::optional<BlendTreeState>& Tree() const noexcept { return tree_; }
    const std::optional<AdditiveAnimationState>& Additive() const noexcept { return additive_; }
    float BlendWeight() const noexcept; // Target weight; 1 when no transition is active.
    bool NeedsEvaluation() const noexcept;
    void Play(std::size_t clip, bool restart = true);
    void PlayBlendTree(BlendTreeHandle tree, bool restart = true);
    void SetBlendParameter(std::string_view name, float value);
    void SeekBlendTree(double phase, AnimationSeekEvents events = AnimationSeekEvents::Silent);
    std::vector<float> BlendWeights() const;
    double BlendTreeDuration() const; // Weighted cycle duration in seconds at current parameters.
    void SetAdditiveAnimation(std::size_t clip, float weight = 1, const PoseAsset& reference = {}, std::span<const float> jointMask = {});
    void RestoreAdditiveAnimation(const AdditiveAnimationState& state); // Validated setup/Scene restore, strong replacement.
    void SetAdditiveWeight(float weight);
    void SetAdditiveMask(std::span<const float> jointMask);
    void SeekAdditive(double seconds); // Clamp to additive clip duration; preserve playback state.
    void ClearAdditiveAnimation() noexcept { additive_.reset(); }
    void CrossFade(std::size_t clip, double durationSeconds = .3); // Restarts target; zero switches immediately.
    void RestoreBlend(const AnimationBlendState& state); // Validated Scene restore on a selected, non-stopped target.
    void Pause() noexcept;
    void Resume();
    void Stop() noexcept; // Keeps clip; returns to its first frame, retains overrides.
    void Seek(double seconds, AnimationSeekEvents events = AnimationSeekEvents::Silent); // Clamped, no wrapping.
    void SetSpeed(double speed); // Finite [-100,100], including reverse playback.
    void SetLoop(bool loop) noexcept { loop_ = loop; }
    void Update(double deltaSeconds); // Nonnegative; explicit update for headless use.
    void ApplyPose(const PoseAsset& pose); // Replaces manual overrides, applied after animation.
    void SetJointOverride(JointId joint, const Transform& local);
    void ClearJointOverride(JointId joint);
    void ClearOverrides() noexcept;
    void ResetPose() noexcept; // Reference pose: clears clip and overrides.
    PoseAsset CapturePose(std::string name = {}) const;
    PoseBuffer EvaluateBasePose() const { return EvaluateBase(); } // Before additive/manual layers.
    AdditivePose EvaluateAdditive() const;
    PoseBuffer EvaluatePose() const; // Current blended local TRS plus manual overrides; Morph stays separate.
    EvaluatedPose Evaluate() const;
    Bounds GetBounds() const; // Current deformed geometry bounds, for camera fitting.
    Bounds GetBounds(const EvaluatedPose& pose) const; // Explicit model-node pose; retains this player's morph weights.
    std::vector<float> EvaluateMorphWeights(JointId node) const;
    void SetMorphWeights(JointId node, std::span<const float> weights);
    void ClearMorphWeights(JointId node);
    void ClearMorphOverrides() noexcept { morphOverrides_.clear(); }
    const std::vector<MorphPose>& MorphOverrides() const noexcept { return morphOverrides_; }
    void SetAnimationEvents(std::size_t clip, std::span<const AnimationEvent> events); // Replace instance overlay; empty suppresses asset events.
    void ResetAnimationEvents(std::size_t clip); // Resume immutable Clip events.
    const std::vector<AnimationEventTrack>& EventOverrides() const noexcept { return eventOverrides_; }
    std::span<const AnimationEvent> AnimationEvents(std::size_t clip) const;
    std::optional<std::size_t> PrimaryEventClip() const; // Target Clip, or greatest Tree weight (tie: sample order).
    std::span<const AnimationEventOccurrence> PendingAnimationEvents() const noexcept { return eventQueue_; }
    std::vector<AnimationEventOccurrence> PollAnimationEvents();
    std::size_t DispatchAnimationEvents(std::function<void(const AnimationEventOccurrence&)> callback);
    void ClearAnimationEventQueue() noexcept { eventQueue_.clear(); }
private:
    friend class Scene; // Preflight bounded queues before advancing any scene player.
    ModelHandle model_;
    std::optional<std::size_t> clip_;
    double time_ = 0, speed_ = 1;
    bool loop_ = true;
    PlaybackState state_ = PlaybackState::Stopped;
    PoseAsset overrides_;
    std::optional<AnimationBlendState> blend_;
    std::optional<BlendTreeState> tree_;
    std::optional<AdditiveAnimationState> additive_;
    std::vector<MorphPose> morphOverrides_;
    std::vector<AnimationEventTrack> eventOverrides_;
    std::vector<AnimationEventOccurrence> eventQueue_;
    void CollectEvents(std::size_t clip, double oldTime, double newTime, bool loop, std::vector<AnimationEventOccurrence>* output, bool normalized = false) const;
    void CollectUpdateEvents(double deltaSeconds, std::vector<AnimationEventOccurrence>* output) const;
    void QueueEvents(std::vector<AnimationEventOccurrence> events);
    std::vector<float> EvaluateMorphClip(JointId node, std::optional<std::size_t> clip, double time) const;
    std::vector<float> EvaluateMorphBase(JointId node) const;
    PoseBuffer EvaluateClip(std::optional<std::size_t> clip,double time) const;
    PoseBuffer EvaluateBase() const;
    void ValidateBlend(const AnimationBlendState& state) const;
};
std::uint64_t MorphSignature(const ModelAsset& model);
void ValidateMorphWeights(const ModelAsset& model, JointId node, std::span<const float> weights);
} // namespace lv
