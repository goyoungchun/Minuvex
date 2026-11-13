#include <minvex/Asset.h>
#include "AnimationEvaluation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lv {
namespace {
void Require(bool ok,const char* message) {if (!ok) {throw std::invalid_argument(message);}}
bool Has(PoseChannels channels,PoseChannels bit) {return (channels&bit)!=PoseChannels::None;}
float Float(double value) {Require(std::isfinite(value) && std::abs(value)<=std::numeric_limits<float>::max(),"Additive result exceeds finite float range.");return static_cast<float>(value);}
Quaternion Unit(Quaternion q) {
    const auto n=std::hypot(std::hypot(double(q.X),double(q.Y)),std::hypot(double(q.Z),double(q.W)));Require(std::isfinite(n) && n>0,"Additive quaternion must be finite and nonzero.");
    return {static_cast<float>(q.X/n),static_cast<float>(q.Y/n),static_cast<float>(q.Z/n),static_cast<float>(q.W/n)};
}
Quaternion Multiply(Quaternion a,Quaternion b) {
    return Unit({Float(double(a.W)*b.X+double(a.X)*b.W+double(a.Y)*b.Z-double(a.Z)*b.Y),Float(double(a.W)*b.Y-double(a.X)*b.Z+double(a.Y)*b.W+double(a.Z)*b.X),Float(double(a.W)*b.Z+double(a.X)*b.Y-double(a.Y)*b.X+double(a.Z)*b.W),Float(double(a.W)*b.W-double(a.X)*b.X-double(a.Y)*b.Y-double(a.Z)*b.Z)});
}
Transform Resolve(const SkeletonJoint& joint,const Transform& local,PoseChannels channels) {
    auto result=joint.ReferenceLocal;if (Has(channels,PoseChannels::Translation)) {result.Position=local.Position;}if (Has(channels,PoseChannels::Rotation)) {result.Rotation=local.Rotation;}if (Has(channels,PoseChannels::Scale)) {result.Scale=local.Scale;}return result;
}
Vec3 Difference(Vec3 sample,Vec3 reference) {return {Float(double(sample.X)-reference.X),Float(double(sample.Y)-reference.Y),Float(double(sample.Z)-reference.Z)};}
Vec3 Ratio(Vec3 sample,Vec3 reference) {
    const auto component=[](float s,float r){Require(std::abs(r)>=1e-8f && s!=0 && std::signbit(s)==std::signbit(r),"Additive Scale needs a non-negligible reference and compatible nonzero signs.");const auto ratio=Float(double(s)/r);Require(ratio>0,"Additive Scale ratio underflowed to zero.");return ratio;};
    return {component(sample.X,reference.X),component(sample.Y,reference.Y),component(sample.Z,reference.Z)};
}
void Mask(std::span<const float> mask,std::size_t count) {Require(mask.empty() || mask.size()==count,"Additive mask requires one weight per node.");for (auto value:mask) {Require(std::isfinite(value) && value>=0 && value<=1,"Additive mask weights must be in [0,1].");}}
void Weight(float value) {Require(std::isfinite(value) && value>=0 && value<=1,"Additive weight must be in [0,1].");}
PoseBuffer ReferenceBuffer(const SkeletonAsset& skeleton,const PoseAsset& reference) {
    return ApplyPoseOverrides(skeleton,ReferencePoseBuffer(skeleton),reference);
}
void ValidateClip(const SkeletonAsset& skeleton,const AnimationClip& clip,const PoseBuffer& reference) {
    ValidateAnimation(skeleton,clip);Require(!clip.Tracks.empty(),"Additive animation requires at least one TRS channel.");
    for (const auto& track:clip.Tracks) {const auto& r=reference.LocalTransforms[track.Joint];for (const auto& v:track.Values) {
        if (track.Path==AnimationPath::Translation) {(void)Difference({v[0],v[1],v[2]},r.Position);}else if (track.Path==AnimationPath::Scale) {(void)Ratio({v[0],v[1],v[2]},r.Scale);}
    }}
}
}

void ValidateAdditivePose(const SkeletonAsset& skeleton,const AdditivePose& pose) {
    ValidateSkeleton(skeleton);Require(pose.SkeletonSignature==SkeletonSignature(skeleton),"Additive pose belongs to a different skeleton.");
    Require(pose.LocalDeltas.size()==skeleton.Joints.size() && pose.Channels.size()==skeleton.Joints.size(),"Additive pose requires dense delta/channel arrays.");
    for (std::size_t i=0;i<skeleton.Joints.size();++i) {const auto channels=pose.Channels[i];Require((static_cast<unsigned>(channels)&~7u)==0 && (skeleton.Joints[i].Editable || channels==PoseChannels::None),"Invalid additive channel or read-only node.");const auto& d=pose.LocalDeltas[i];
        if (Has(channels,PoseChannels::Translation)) {for (auto v:{d.Position.X,d.Position.Y,d.Position.Z}) {Require(std::isfinite(v),"Nonfinite additive translation.");}}
        if (Has(channels,PoseChannels::Rotation)) {(void)Unit(d.Rotation);}
        if (Has(channels,PoseChannels::Scale)) {for (auto v:{d.Scale.X,d.Scale.Y,d.Scale.Z}) {Require(std::isfinite(v) && v>0,"Additive scale ratios must be finite and positive.");}}
    }
}
AdditivePose MakeAdditivePose(const SkeletonAsset& skeleton,const PoseBuffer& sample,const PoseBuffer& reference) {
    ValidatePoseBuffer(skeleton,sample);ValidatePoseBuffer(skeleton,reference);AdditivePose result;result.SkeletonSignature=sample.SkeletonSignature;result.Channels=sample.Channels;result.LocalDeltas.resize(skeleton.Joints.size());
    for (std::size_t i=0;i<skeleton.Joints.size();++i) {const auto channels=sample.Channels[i];if (channels==PoseChannels::None) {continue;}const auto s=Resolve(skeleton.Joints[i],sample.LocalTransforms[i],channels),r=Resolve(skeleton.Joints[i],reference.LocalTransforms[i],reference.Channels[i]);auto& delta=result.LocalDeltas[i];
        if (Has(channels,PoseChannels::Translation)) {delta.Position=Difference(s.Position,r.Position);}
        if (Has(channels,PoseChannels::Rotation)) {auto inverse=Unit(r.Rotation);inverse={-inverse.X,-inverse.Y,-inverse.Z,inverse.W};delta.Rotation=Multiply(inverse,Unit(s.Rotation));}
        if (Has(channels,PoseChannels::Scale)) {delta.Scale=Ratio(s.Scale,r.Scale);}
    }return result;
}
PoseBuffer ApplyAdditivePose(const SkeletonAsset& skeleton,const PoseBuffer& base,const AdditivePose& delta,float weight,std::span<const float> jointMask) {
    Weight(weight);Mask(jointMask,skeleton.Joints.size());ValidatePoseBuffer(skeleton,base);ValidateAdditivePose(skeleton,delta);if (weight==0) {return base;}auto result=base;
    for (std::size_t i=0;i<skeleton.Joints.size();++i) {const auto w=weight*(jointMask.empty() ? 1:jointMask[i]);const auto channels=delta.Channels[i];if (w==0 || channels==PoseChannels::None) {continue;}
        auto& local=result.LocalTransforms[i];local=Resolve(skeleton.Joints[i],local,base.Channels[i]);const auto& d=delta.LocalDeltas[i];
        if (Has(channels,PoseChannels::Translation)) {local.Position={Float(double(local.Position.X)+double(w)*d.Position.X),Float(double(local.Position.Y)+double(w)*d.Position.Y),Float(double(local.Position.Z)+double(w)*d.Position.Z)};}
        if (Has(channels,PoseChannels::Rotation)) {local.Rotation=Multiply(Unit(local.Rotation),Slerp({},d.Rotation,w));}
        if (Has(channels,PoseChannels::Scale)) {const auto scale=[&](float b,float ratio){return Float(double(b)*std::lerp(1.0,double(ratio),double(w)));};local.Scale={scale(local.Scale.X,d.Scale.X),scale(local.Scale.Y,d.Scale.Y),scale(local.Scale.Z,d.Scale.Z)};}
        (void)local.Matrix().NormalMatrix();result.Channels[i]=base.Channels[i]|channels;
    }return result;
}
void AnimationPlayer::SetAdditiveAnimation(std::size_t clip,float weight,const PoseAsset& reference,std::span<const float> jointMask) {
    Require(model_ && clip<model_->Animations.size(),"Additive clip does not belong to this model.");AdditiveAnimationState state;state.Clip=clip;state.Weight=weight;state.Reference=reference;state.JointMask.assign(jointMask.begin(),jointMask.end());state.Time=speed_<0 ? model_->Animations[clip].Duration:0;state.State=model_->Animations[clip].Duration==0 ? PlaybackState::Stopped:PlaybackState::Playing;RestoreAdditiveAnimation(state);
}
void AnimationPlayer::RestoreAdditiveAnimation(const AdditiveAnimationState& state) {
    Require(model_ && state.Clip<model_->Animations.size(),"Additive clip does not belong to this model.");Weight(state.Weight);Mask(state.JointMask,model_->Skeleton.Joints.size());
    Require(std::isfinite(state.Speed) && std::abs(state.Speed)<=100 && std::isfinite(state.Time) && state.Time>=0 && state.Time<=model_->Animations[state.Clip].Duration,"Invalid additive speed/time.");Require(state.State==PlaybackState::Playing || state.State==PlaybackState::Paused || state.State==PlaybackState::Stopped,"Invalid additive playback state.");
    auto next=state;if (next.Reference.SkeletonSignature==0 && next.Reference.Joints.empty() && next.Reference.Name.empty()) {next.Reference=ReferencePose(model_->Skeleton);}const auto reference=ReferenceBuffer(model_->Skeleton,next.Reference);ValidateClip(model_->Skeleton,model_->Animations[next.Clip],reference);additive_=std::move(next);
}
void AnimationPlayer::SetAdditiveWeight(float weight) {Require(additive_.has_value(),"Select an additive animation first.");Weight(weight);additive_->Weight=weight;}
void AnimationPlayer::SetAdditiveMask(std::span<const float> jointMask) {Require(additive_.has_value(),"Select an additive animation first.");Mask(jointMask,model_->Skeleton.Joints.size());std::vector<float> next(jointMask.begin(),jointMask.end());additive_->JointMask=std::move(next);}
void AnimationPlayer::SeekAdditive(double seconds) {Require(additive_ && std::isfinite(seconds) && seconds>=0,"Additive seek requires finite nonnegative seconds.");additive_->Time=std::min(seconds,model_->Animations[additive_->Clip].Duration);}
AdditivePose AnimationPlayer::EvaluateAdditive() const {
    Require(additive_.has_value(),"Select an additive animation first.");const auto& layer=*additive_;const auto sample=detail::SampleAnimation(model_->Skeleton,&model_->Animations[layer.Clip],layer.Time);return MakeAdditivePose(model_->Skeleton,sample,ReferenceBuffer(model_->Skeleton,layer.Reference));
}
}
