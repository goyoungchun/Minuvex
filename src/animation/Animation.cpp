#include <minvex/Asset.h>
#include "AnimationEvaluation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace lv {
namespace {
void Require(bool valid, const char* message) { if (!valid) { throw std::invalid_argument(message); } }
Quaternion Normalize(Quaternion q) {
    const double n = std::hypot(std::hypot(double(q.X),double(q.Y)),std::hypot(double(q.Z),double(q.W)));
    Require(std::isfinite(n) && n > 0,"Quaternion must be finite and nonzero.");
    return {static_cast<float>(q.X/n),static_cast<float>(q.Y/n),static_cast<float>(q.Z/n),static_cast<float>(q.W/n)};
}
Mat4 ReferenceMatrix(const SkeletonJoint& joint) { return joint.Editable ? joint.ReferenceLocal.Matrix() : joint.ReferenceMatrix; }
}
Quaternion Slerp(Quaternion from, Quaternion to, float amount) {
    Require(std::isfinite(amount) && amount >= 0 && amount <= 1,"Slerp amount must be in [0,1].");
    from = Normalize(from); to = Normalize(to);
    double dot = double(from.X)*to.X+double(from.Y)*to.Y+double(from.Z)*to.Z+double(from.W)*to.W;
    if (dot < 0) { dot = -dot; to = {-to.X,-to.Y,-to.Z,-to.W}; }
    double a = 1-amount, b = amount;
    if (dot < 0.9995) { const auto angle = std::acos(std::clamp(dot,0.0,1.0)); a = std::sin((1-amount)*angle)/std::sin(angle); b = std::sin(amount*angle)/std::sin(angle); }
    return Normalize({static_cast<float>(a*from.X+b*to.X),static_cast<float>(a*from.Y+b*to.Y),static_cast<float>(a*from.Z+b*to.Z),static_cast<float>(a*from.W+b*to.W)});
}
void ValidateSkeleton(const SkeletonAsset& skeleton) {
    Require(skeleton.Joints.size() <= 65536,"Skeleton exceeds 65536 transform nodes.");
    std::vector<unsigned char> visited(skeleton.Joints.size(),0);
    for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
        (void)ReferenceMatrix(skeleton.Joints[i]).NormalMatrix();
        std::vector<JointId> chain;
        auto current = static_cast<JointId>(i);
        while (current != NoJoint) {
            Require(current < skeleton.Joints.size(),"Skeleton parent is out of range.");
            if (visited[current] == 2) { break; }
            Require(visited[current] == 0,"Skeleton hierarchy contains a cycle.");
            visited[current] = 1; chain.push_back(current); current = skeleton.Joints[current].Parent;
        }
        for (auto joint : chain) { visited[joint] = 2; }
    }
}
std::uint64_t SkeletonSignature(const SkeletonAsset& skeleton) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto number = [&](std::uint64_t value) { for (int i=0;i<8;++i) { hash ^= (value>>(i*8))&255; hash *= 1099511628211ULL; } };
    number(skeleton.Joints.size());
    for (const auto& joint : skeleton.Joints) {
        number(joint.Parent); number(joint.Name.size());
        for (auto c : joint.Name) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ULL; }
        number(joint.Editable); number(joint.IsJoint); number(joint.MatrixAuthored);
        const auto component = [&](float value) { number(std::bit_cast<std::uint32_t>(value == 0 ? 0.0f : value)); };
        // Hash source TRS/matrix values in explicit byte order. Do not hash a
        // platform-dependent quaternion-to-matrix/decomposition calculation.
        if (joint.MatrixAuthored || !joint.Editable) { for (auto value : joint.ReferenceMatrix.Elements) { component(value); } }
        else {
            const auto& t = joint.ReferenceLocal;
            for (auto value : {t.Position.X,t.Position.Y,t.Position.Z,t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W,t.Scale.X,t.Scale.Y,t.Scale.Z}) { component(value); }
        }
    }
    return hash;
}
void ValidatePose(const SkeletonAsset& skeleton, const PoseAsset& pose) {
    Require(pose.SkeletonSignature == SkeletonSignature(skeleton),"Pose belongs to a different skeleton/reference hierarchy.");
    Require(pose.Joints.size() <= skeleton.Joints.size(),"Pose contains too many joints.");
    std::unordered_set<JointId> seen;
    for (const auto& joint : pose.Joints) {
        Require(joint.Joint < skeleton.Joints.size() && skeleton.Joints[joint.Joint].Editable,"Pose references an invalid/read-only joint.");
        Require(seen.insert(joint.Joint).second,"Pose contains a duplicate joint.");
        (void)joint.Local.Matrix().NormalMatrix();
    }
}
PoseAsset ReferencePose(const SkeletonAsset& skeleton) {
    ValidateSkeleton(skeleton);
    PoseAsset pose; pose.Name = "Reference Pose"; pose.SkeletonSignature = SkeletonSignature(skeleton);
    for (std::size_t i=0;i<skeleton.Joints.size();++i) { if (skeleton.Joints[i].Editable) { pose.Joints.push_back({static_cast<JointId>(i),skeleton.Joints[i].ReferenceLocal}); } }
    return pose;
}
void ValidateAnimation(const SkeletonAsset& skeleton, const AnimationClip& clip) {
    Require(std::isfinite(clip.Duration) && clip.Duration >= 0 && clip.Tracks.size() <= skeleton.Joints.size()*3,"Invalid animation duration/track count.");
    std::unordered_set<std::uint64_t> seen;
    for (const auto& track : clip.Tracks) {
        Require(track.Joint < skeleton.Joints.size() && skeleton.Joints[track.Joint].Editable,"Animation targets an invalid/read-only joint.");
        Require(track.Path == AnimationPath::Translation || track.Path == AnimationPath::Rotation || track.Path == AnimationPath::Scale,"Invalid animation path.");
        Require(track.Interpolation == AnimationInterpolation::Step || track.Interpolation == AnimationInterpolation::Linear,"Invalid animation interpolation.");
        Require(seen.insert(std::uint64_t(track.Joint)*3+static_cast<unsigned>(track.Path)).second,"Animation has duplicate target/path tracks.");
        Require(!track.Times.empty() && track.Times.size() == track.Values.size(),"Animation key counts differ or are empty.");
        for (std::size_t i=0;i<track.Times.size();++i) {
            Require(std::isfinite(track.Times[i]) && track.Times[i] >= 0 && (i == 0 || track.Times[i] > track.Times[i-1]) && track.Times[i] <= clip.Duration,"Animation times must be finite, nonnegative, increasing and within duration.");
            const auto& value = track.Values[i];
            for (float component : value) { Require(std::isfinite(component),"Animation value is nonfinite."); }
            if (track.Path == AnimationPath::Rotation) { (void)Normalize({value[0],value[1],value[2],value[3]}); }
            if (track.Path == AnimationPath::Scale) {
                for (int k=0;k<3;++k) {
                    Require(value[k] != 0,"Animation scale must be nonzero.");
                    Require(i == 0 || track.Interpolation != AnimationInterpolation::Linear || std::signbit(value[k]) == std::signbit(track.Values[i-1][k]),"Linear scale track crosses a singular zero scale.");
                }
            }
        }
    }
    std::unordered_set<JointId> morphNodes;
    for (const auto& track:clip.MorphTracks) {
        Require(track.Node<skeleton.Joints.size() && morphNodes.insert(track.Node).second && track.TargetCount>0 && track.TargetCount<=MaxMorphTargets,"Invalid/duplicate morph channel.");
        Require((track.Interpolation==AnimationInterpolation::Linear || track.Interpolation==AnimationInterpolation::Step) && !track.Times.empty() && track.Times.size()<=SIZE_MAX/track.TargetCount && track.Values.size()==track.Times.size()*track.TargetCount,"Invalid morph sample layout.");
        for (std::size_t i=0;i<track.Times.size();++i) {Require(std::isfinite(track.Times[i]) && track.Times[i]>=0 && track.Times[i]<=clip.Duration && (!i || track.Times[i]>track.Times[i-1]),"Invalid morph key times.");}for (auto weight:track.Values) {Require(std::isfinite(weight) && std::abs(weight)<=100,"Invalid animated morph weight.");}
    }
}
std::uint64_t MorphSignature(const ModelAsset& model) {
    if (std::none_of(model.Meshes.begin(),model.Meshes.end(),[](const auto& mesh){return !mesh.MorphWeights.empty();})) {return 0;}
    std::uint64_t hash=14695981039346656037ull;const auto number=[&](std::uint64_t value) {for (int i=0;i<8;++i) {hash^=(value>>(i*8))&255;hash*=1099511628211ull;}};
    number(model.Meshes.size());for (const auto& mesh:model.Meshes) {number(mesh.MorphWeights.size());for (auto w:mesh.MorphWeights) {number(std::bit_cast<std::uint32_t>(w == 0 ? 0.0f:w));}for (const auto& primitive:mesh.Primitives) {number(primitive.Geometry.Vertices.size());number(primitive.MorphTargets.size());for (const auto& target:primitive.MorphTargets) {number(target.Name.size());for (unsigned char c:target.Name) {number(c);}}}}
    number(model.Nodes.size());for (const auto& node:model.Nodes) {number(node.Name.size());for (unsigned char c:node.Name) {number(c);}number(node.Parent.value_or(SIZE_MAX));number(node.MeshIndex.value_or(SIZE_MAX));number(node.MorphWeights.size());for (auto w:node.MorphWeights) {number(std::bit_cast<std::uint32_t>(w == 0 ? 0.0f:w));}}return hash;
}
std::vector<float> AnimationPlayer::EvaluateMorphClip(JointId node,std::optional<std::size_t> clip,double time) const {
    Require(model_ && node<model_->Nodes.size() && model_->Nodes[node].MeshIndex,"Morph evaluation requires a mesh node.");const auto& source=model_->Nodes[node];auto weights=source.MorphWeights.empty() ? model_->Meshes.at(*source.MeshIndex).MorphWeights:source.MorphWeights;
    if (clip) {for (const auto& track:model_->Animations[*clip].MorphTracks) {if (track.Node!=node) {continue;}const auto upper=std::upper_bound(track.Times.begin(),track.Times.end(),time);const auto left=upper==track.Times.begin() ? 0:static_cast<std::size_t>(upper-track.Times.begin()-1),right=std::min(left+1,track.Times.size()-1);const auto t=track.Interpolation==AnimationInterpolation::Step || left==right || time<=track.Times.front() ? 0.0f:static_cast<float>((time-track.Times[left])/(track.Times[right]-track.Times[left]));for (std::size_t i=0;i<weights.size();++i) {weights[i]=std::lerp(track.Values[left*track.TargetCount+i],track.Values[right*track.TargetCount+i],t);}}}
    return weights;
}
std::vector<float> AnimationPlayer::EvaluateMorphBase(JointId node) const {
    if (tree_) {auto result=EvaluateMorphClip(node,std::nullopt,0);std::vector<double> accumulated(result.size(),0);double total=0;const auto weights=BlendWeights();for (std::size_t i=0;i<weights.size();++i) {if (weights[i]<=0) {continue;}total+=weights[i];const auto index=tree_->Tree->Description().Samples[i].Clip;const auto sample=EvaluateMorphClip(node,index,tree_->Phase*model_->Animations[index].Duration);for (std::size_t t=0;t<result.size();++t) {accumulated[t]+=double(weights[i])*sample[t];}}for (std::size_t t=0;t<result.size();++t) {result[t]=static_cast<float>(accumulated[t]/total);}return result;}
    auto weights=EvaluateMorphClip(node,clip_,time_);if (!blend_) {return weights;}auto source=EvaluateMorphClip(node,blend_->SourceClip,blend_->SourceTime);
    if (!blend_->SourceClip) {const auto found=std::find_if(blend_->SourceMorph.begin(),blend_->SourceMorph.end(),[&](const auto& pose) {return pose.Node==node;});Require(found!=blend_->SourceMorph.end(),"Frozen blend is missing a morph node.");source=found->Weights;}
    for (std::size_t i=0;i<weights.size();++i) {weights[i]=std::lerp(source[i],weights[i],BlendWeight());}return weights;
}
std::vector<float> AnimationPlayer::EvaluateMorphWeights(JointId node) const {
    auto weights=EvaluateMorphBase(node);const auto found=std::find_if(morphOverrides_.begin(),morphOverrides_.end(),[&](const auto& pose){return pose.Node==node;});if (found!=morphOverrides_.end()) {weights=found->Weights;}ValidateMorphWeights(*model_,node,weights);return weights;
}
void AnimationPlayer::SetMorphWeights(JointId node,std::span<const float> weights) {
    Require(bool(model_),"Bind a model before editing morph weights.");ValidateMorphWeights(*model_,node,weights);auto next=morphOverrides_;auto found=std::find_if(next.begin(),next.end(),[&](const auto& pose){return pose.Node==node;});if (found==next.end()) {next.push_back({node,{weights.begin(),weights.end()}});}else {found->Weights.assign(weights.begin(),weights.end());}morphOverrides_=std::move(next);
}
void AnimationPlayer::ClearMorphWeights(JointId node) {const auto defaults=EvaluateMorphClip(node,std::nullopt,0);ValidateMorphWeights(*model_,node,defaults);std::erase_if(morphOverrides_,[&](const auto& pose){return pose.Node==node;});}
void AnimationPlayer::Bind(ModelHandle model) {
    if (model) {
        ValidateSkeleton(model->Skeleton);
        for (const auto& skin : model->Skins) {
            Require(!skin.Joints.empty() && skin.Joints.size() <= MaxSkinJoints && skin.Joints.size() == skin.InverseBindMatrices.size(),"Invalid skin palette size.");
            std::unordered_set<JointId> seen;
            for (std::size_t i=0;i<skin.Joints.size();++i) {
                Require(skin.Joints[i] < model->Skeleton.Joints.size() && seen.insert(skin.Joints[i]).second,"Skin references an invalid/duplicate joint.");
                (void)skin.InverseBindMatrices[i].NormalMatrix();
            }
        }
        for (const auto& part : model->Parts) {
            Require(part.MeshIndex < model->Meshes.size() && (part.Node == NoJoint || part.Node < model->Skeleton.Joints.size()),"Invalid model part mesh/node.");
            if (!part.SkinIndex) { continue; }
            Require(*part.SkinIndex < model->Skins.size(),"Invalid part skin."); const auto& skin = model->Skins[*part.SkinIndex];
            for (const auto& primitive : model->Meshes[part.MeshIndex].Primitives) {
                Require(primitive.HasSkinning,"Skinned part is missing joint attributes.");
                for (const auto& vertex : primitive.Geometry.Vertices) { for (auto joint : vertex.Joints) { Require(joint < skin.Joints.size(),"Vertex joint is outside skin mapping."); } }
            }
        }
    }
    if (model) {ValidateMorphModel(*model);}
    morphOverrides_.clear();
    model_ = std::move(model); clip_.reset(); time_ = 0; speed_ = 1; loop_ = true; state_ = PlaybackState::Stopped;
    blend_.reset();tree_.reset();additive_.reset();overrides_ = {}; if (model_) { overrides_.SkeletonSignature = SkeletonSignature(model_->Skeleton); }
}
bool AnimationPlayer::NeedsEvaluation() const noexcept { return model_ && (clip_.has_value() || tree_.has_value() || additive_.has_value() || !overrides_.Joints.empty() || !model_->Skins.empty() || std::any_of(model_->Meshes.begin(),model_->Meshes.end(),[](const auto& mesh){return !mesh.MorphWeights.empty();})); }
void AnimationPlayer::Rebind(ModelHandle model) {
    Require(bool(model),"Rebind requires a nonempty replacement model.");
    AnimationPlayer next; next.Bind(std::move(model));
    next.speed_ = speed_; next.loop_ = loop_;
    if (clip_ || tree_ || additive_ || !overrides_.Joints.empty()) {
        Require(model_ && SkeletonSignature(model_->Skeleton) == SkeletonSignature(next.model_->Skeleton),
            "Reimport changed a skeleton/reference hierarchy used by animation or pose overrides.");
    }
    if (clip_) {
        Require(*clip_ < next.model_->Animations.size() && next.model_->Animations[*clip_].Name == model_->Animations.at(*clip_).Name,
            "Reimport changed the selected animation clip identity.");
        ValidateAnimation(next.model_->Skeleton,next.model_->Animations[*clip_]);
        Require(time_ <= next.model_->Animations[*clip_].Duration,"Reimport shortened the clip below the current playhead.");
        next.clip_ = clip_; next.time_ = time_; next.state_ = state_;
    }
    if (!overrides_.Joints.empty()) {next.ApplyPose(overrides_);}
    else {next.overrides_.Name = overrides_.Name;}
    if (tree_) {next.PlayBlendTree(tree_->Tree);next.tree_=tree_;next.state_=state_;}
    if (!morphOverrides_.empty() || clip_ || blend_ || tree_) {Require(MorphSignature(*model_)==MorphSignature(*next.model_),"Reimport changed a morph target/default layout in use.");}
    for (const auto& pose:morphOverrides_) {next.SetMorphWeights(pose.Node,pose.Weights);}
    if (blend_) {
        if (blend_->SourceClip) {const auto source=*blend_->SourceClip;Require(source<next.model_->Animations.size() && next.model_->Animations[source].Name == model_->Animations[source].Name,"Reimport changed the blend source clip identity.");}
        next.RestoreBlend(*blend_);
    }
    if (additive_) {const auto clip=additive_->Clip;Require(clip<next.model_->Animations.size() && next.model_->Animations[clip].Name==model_->Animations[clip].Name,"Reimport changed the additive clip identity.");next.RestoreAdditiveAnimation(*additive_);}
    (void)next.Evaluate();
    *this = std::move(next);
}
void AnimationPlayer::Play(std::size_t clip, bool restart) {
    Require(model_ && clip < model_->Animations.size(),"Animation clip does not belong to the bound model.");
    ValidateAnimation(model_->Skeleton,model_->Animations[clip]);
    if (restart || clip_ != clip) { time_ = speed_ < 0 ? model_->Animations[clip].Duration : 0; }
    clip_ = clip; state_ = PlaybackState::Playing;blend_.reset();tree_.reset();
}
float AnimationPlayer::BlendWeight() const noexcept {return blend_ ? static_cast<float>(blend_->Elapsed/blend_->Duration):1.0f;}
void AnimationPlayer::CrossFade(std::size_t clip,double duration) {
    Require(std::isfinite(duration) && duration>=0,"Blend duration must be finite and nonnegative.");
    if (duration == 0) {Play(clip);return;}
    Require(bool(model_),"Bind a model before blending clips.");
    AnimationBlendState state;state.Duration=duration;
    if (!blend_ && clip_ && state_ == PlaybackState::Playing) {state.SourceClip=clip_;state.SourceTime=time_;}
    else {const auto base=EvaluateBase();state.SourcePose.Name="Blend source";state.SourcePose.SkeletonSignature=base.SkeletonSignature;for (std::size_t i=0;i<base.LocalTransforms.size();++i) {if (model_->Skeleton.Joints[i].Editable) {state.SourcePose.Joints.push_back({static_cast<JointId>(i),base.LocalTransforms[i]});}}}
    if (!state.SourceClip) {for (std::size_t i=0;i<model_->Nodes.size();++i) {const auto& node=model_->Nodes[i];if (node.MeshIndex && !model_->Meshes[*node.MeshIndex].MorphWeights.empty()) {state.SourceMorph.push_back({static_cast<JointId>(i),EvaluateMorphBase(static_cast<JointId>(i))});}}}
    auto next=*this;next.Play(clip);next.RestoreBlend(state);*this=std::move(next);
}
void AnimationPlayer::ValidateBlend(const AnimationBlendState& state) const {
    Require(model_ && clip_ && state_ != PlaybackState::Stopped,"Blend restore requires a selected, playing or paused target clip.");
    Require(std::isfinite(state.Duration) && state.Duration>0 && std::isfinite(state.Elapsed) && state.Elapsed>=0 && state.Elapsed<state.Duration,"Invalid blend duration/progress.");
    Require(std::isfinite(state.SourceTime) && state.SourceTime>=0,"Invalid blend source time.");
    if (state.SourceClip) {
        Require(*state.SourceClip<model_->Animations.size() && state.SourcePose.Joints.empty(),"Blend source must be one live clip or one frozen pose.");
        const auto& clip=model_->Animations[*state.SourceClip];ValidateAnimation(model_->Skeleton,clip);Require(state.SourceTime<=clip.Duration,"Blend source time exceeds clip duration.");
    }else {
        ValidatePose(model_->Skeleton,state.SourcePose);const auto count=std::count_if(model_->Skeleton.Joints.begin(),model_->Skeleton.Joints.end(),[](const auto& joint){return joint.Editable;});
        Require(state.SourceTime == 0 && state.SourcePose.Joints.size() == static_cast<std::size_t>(count),"Frozen blend source requires a complete pose and zero source time.");
    }
    if (state.SourceClip) {Require(state.SourceMorph.empty(),"Live source cannot contain a frozen morph pose.");}else {
        std::size_t expected=0;for (const auto& node:model_->Nodes) {expected+=node.MeshIndex && !model_->Meshes[*node.MeshIndex].MorphWeights.empty();}Require(state.SourceMorph.size()==expected,"Frozen blend requires every morph node.");std::unordered_set<JointId> nodes;for (const auto& pose:state.SourceMorph) {Require(nodes.insert(pose.Node).second,"Duplicate frozen morph node.");ValidateMorphWeights(*model_,pose.Node,pose.Weights);}
    }
    auto source=EvaluateClip(state.SourceClip,state.SourceTime);if (!state.SourceClip) {source=ApplyPoseOverrides(model_->Skeleton,source,state.SourcePose);}
    const auto target=EvaluateClip(clip_,time_);
    (void)BlendPoses(model_->Skeleton,source,target,.5f);
    const auto signs=[](Vec3 a,Vec3 b) {return std::signbit(a.X) == std::signbit(b.X) && std::signbit(a.Y) == std::signbit(b.Y) && std::signbit(a.Z) == std::signbit(b.Z);};
    const auto check=[&](std::size_t clip) {for (const auto& track:model_->Animations[clip].Tracks) {if (track.Path == AnimationPath::Scale) {for (const auto& value:track.Values) {Require(signs(source.LocalTransforms[track.Joint].Scale,{value[0],value[1],value[2]}),"Blend clips must retain compatible scale signs.");}}}};
    check(*clip_);if (state.SourceClip) {check(*state.SourceClip);}
}
void AnimationPlayer::RestoreBlend(const AnimationBlendState& state) {ValidateBlend(state);auto candidate=state;blend_=std::move(candidate);}
void AnimationPlayer::Pause() noexcept { if (state_ == PlaybackState::Playing) { state_ = PlaybackState::Paused; }if (additive_ && additive_->State==PlaybackState::Playing) {additive_->State=PlaybackState::Paused;} }
void AnimationPlayer::Resume() { Require(model_ && (clip_ || tree_ || additive_),"Select an animation clip, tree or additive layer before resuming.");if (clip_ || tree_) {state_=PlaybackState::Playing;}if (additive_) {additive_->State=model_->Animations[additive_->Clip].Duration==0 ? PlaybackState::Stopped:PlaybackState::Playing;} }
void AnimationPlayer::Stop() noexcept { time_ = 0;if (tree_) {tree_->Phase=0;}if (additive_) {additive_->Time=0;additive_->State=PlaybackState::Stopped;}state_ = PlaybackState::Stopped;blend_.reset(); }
void AnimationPlayer::Seek(double seconds) {
    Require(model_ && clip_.has_value() && std::isfinite(seconds) && seconds >= 0,"Seek requires a clip and finite nonnegative time.");
    time_ = std::min(seconds,model_->Animations[*clip_].Duration);
    blend_.reset();
}
void AnimationPlayer::SetSpeed(double speed) { Require(std::isfinite(speed) && std::abs(speed) <= 100,"Playback speed must be finite and in [-100,100]."); speed_ = speed; }
void AnimationPlayer::Update(double deltaSeconds) {
    Require(std::isfinite(deltaSeconds) && deltaSeconds >= 0,"Animation delta time must be finite and nonnegative.");
    double additiveTime=additive_ ? additive_->Time:0;auto additiveState=additive_ ? additive_->State:PlaybackState::Stopped;
    if (additive_ && additiveState==PlaybackState::Playing && speed_!=0 && additive_->Speed!=0 && deltaSeconds!=0) {
        const auto duration=model_->Animations[additive_->Clip].Duration;const auto shift=deltaSeconds*speed_*additive_->Speed;Require(std::isfinite(shift),"Additive time advance exceeds numeric range.");const auto next=additiveTime+shift;Require(std::isfinite(next),"Additive time advance exceeds numeric range.");
        if (duration==0) {additiveTime=0;additiveState=PlaybackState::Stopped;}else if (additive_->Loop) {additiveTime=std::fmod(next,duration);if (additiveTime<0) {additiveTime+=duration;}}else {additiveTime=std::clamp(next,0.0,duration);if ((shift>0 && next>=duration) || (shift<0 && next<=0)) {additiveState=PlaybackState::Stopped;}}
    }
    const auto commitAdditive=[&] {if (additive_) {additive_->Time=additiveTime;additive_->State=additiveState;}};
    if (tree_) {
        if (state_!=PlaybackState::Playing || speed_==0 || deltaSeconds==0) {commitAdditive();return;}const auto duration=BlendTreeDuration();if (duration==0) {tree_->Phase=0;state_=PlaybackState::Stopped;commitAdditive();return;}
        const auto next=tree_->Phase+deltaSeconds*speed_/duration;Require(std::isfinite(next),"Tree phase advance exceeds numeric range.");
        if (loop_) {auto phase=std::fmod(next,1.0);tree_->Phase=phase<0 ? phase+1:phase;}else {tree_->Phase=std::clamp(next,0.0,1.0);if ((speed_>0 && next>=1) || (speed_<0 && next<=0)) {state_=PlaybackState::Stopped;}}commitAdditive();return;
    }
    if (state_ != PlaybackState::Playing || !model_ || !clip_ || speed_ == 0 || deltaSeconds == 0) {commitAdditive();return;}
    const double shift=deltaSeconds*speed_;Require(std::isfinite(shift),"Animation time advance exceeds numeric range.");
    const auto advance=[&](std::size_t clip,double time) {const double duration=model_->Animations[clip].Duration;if (duration == 0) {return 0.0;}const double next=time+shift;Require(std::isfinite(next),"Animation time advance exceeds numeric range.");if (!loop_) {return std::clamp(next,0.0,duration);}auto wrapped=std::fmod(next,duration);return wrapped<0 ? wrapped+duration:wrapped;};
    const auto next=advance(*clip_,time_);double source=0,elapsed=0;
    if (blend_) {if (blend_->SourceClip) {source=advance(*blend_->SourceClip,blend_->SourceTime);}elapsed=blend_->Elapsed+std::min(deltaSeconds,blend_->Duration-blend_->Elapsed);}
    time_=next;
    if (blend_) {blend_->SourceTime=source;blend_->Elapsed=elapsed;if (elapsed>=blend_->Duration) {blend_.reset();}}
    const auto duration=model_->Animations[*clip_].Duration;
    if (!blend_ && (duration == 0 || (!loop_ && ((speed_>0 && time_>=duration) || (speed_<0 && time_<=0))))) {state_=PlaybackState::Stopped;}
    commitAdditive();
}
void AnimationPlayer::ApplyPose(const PoseAsset& pose) {
    Require(bool(model_),"Bind a model before applying a pose."); ValidatePose(model_->Skeleton,pose); overrides_ = pose;
}
void AnimationPlayer::SetJointOverride(JointId joint, const Transform& local) {
    auto pose = overrides_; auto found = std::find_if(pose.Joints.begin(),pose.Joints.end(),[&](const auto& p) { return p.Joint == joint; });
    if (found == pose.Joints.end()) { pose.Joints.push_back({joint,local}); } else { found->Local = local; } ApplyPose(pose);
}
void AnimationPlayer::ClearJointOverride(JointId joint) {
    Require(model_ && joint < model_->Skeleton.Joints.size(),"Joint does not belong to the bound model.");
    std::erase_if(overrides_.Joints,[&](const auto& p) { return p.Joint == joint; });
}
void AnimationPlayer::ClearOverrides() noexcept { overrides_.Joints.clear(); }
void AnimationPlayer::ResetPose() noexcept { clip_.reset();tree_.reset();additive_.reset(); time_ = 0; state_ = PlaybackState::Stopped;blend_.reset();ClearOverrides();ClearMorphOverrides(); }
PoseBuffer AnimationPlayer::EvaluateClip(std::optional<std::size_t> clip,double time) const {
    Require(bool(model_),"Bind a model before evaluating its pose.");
    return detail::SampleAnimation(model_->Skeleton,clip ? &model_->Animations[*clip]:nullptr,time);
}
PoseBuffer AnimationPlayer::EvaluateBase() const {
    if (tree_) {return detail::SampleBlendTree(*model_,*tree_->Tree,BlendWeights(),tree_->Phase);}
    auto local=EvaluateClip(clip_,time_);if (!blend_) {return local;}
    auto source=EvaluateClip(blend_->SourceClip,blend_->SourceTime);if (!blend_->SourceClip) {source=ApplyPoseOverrides(model_->Skeleton,source,blend_->SourcePose);}
    return BlendPoses(model_->Skeleton,source,local,BlendWeight());
}
PoseBuffer AnimationPlayer::EvaluatePose() const {
    auto base=EvaluateBase();if (additive_ && additive_->Weight>0) {base=ApplyAdditivePose(model_->Skeleton,base,EvaluateAdditive(),additive_->Weight,additive_->JointMask);}return overrides_.Joints.empty() ? base:ApplyPoseOverrides(model_->Skeleton,base,overrides_);
}
PoseAsset AnimationPlayer::CapturePose(std::string name) const {
    const auto local = EvaluatePose(); PoseAsset pose; pose.Name = std::move(name); pose.SkeletonSignature = local.SkeletonSignature;
    for (std::size_t i=0;i<local.LocalTransforms.size();++i) { if (model_->Skeleton.Joints[i].Editable) { pose.Joints.push_back({static_cast<JointId>(i),local.LocalTransforms[i]}); } }
    return pose;
}
EvaluatedPose AnimationPlayer::Evaluate() const {
    const auto pose=EvaluatePose();return ComposePose(model_->Skeleton,pose);
}
Bounds AnimationPlayer::GetBounds() const {
    Require(bool(model_),"Bind a model before computing its bounds.");
    if (!NeedsEvaluation()) { return model_->ModelBounds; }
    const auto pose = Evaluate(); Bounds bounds; bool first = true;
    for (const auto& part : model_->Parts) {
        const auto matrix = part.Node == NoJoint ? part.Transform : pose.Global.at(part.Node);
        for (const auto& primitive : model_->Meshes.at(part.MeshIndex).Primitives) {
            const auto geometry=primitive.MorphTargets.empty() ? MeshData{}:EvaluateMorph(primitive,EvaluateMorphWeights(part.Node));
            const auto& vertices=primitive.MorphTargets.empty() ? primitive.Geometry.Vertices:geometry.Vertices;
            for (const auto& vertex : vertices) {
                const Vec3 p{vertex.Position[0],vertex.Position[1],vertex.Position[2]}; Vec3 point;
                if (part.SkinIndex) {
                    const auto& skin = model_->Skins.at(*part.SkinIndex);
                    for (std::size_t k=0;k<4;++k) {
                        const auto index = vertex.Joints[k];
                        const auto transformed = (pose.Global.at(skin.Joints.at(index))*skin.InverseBindMatrices.at(index)).TransformPoint(p);
                        point.X += transformed.X*vertex.Weights[k]; point.Y += transformed.Y*vertex.Weights[k]; point.Z += transformed.Z*vertex.Weights[k];
                    }
                } else { point = matrix.TransformPoint(p); }
                Require(std::isfinite(point.X) && std::isfinite(point.Y) && std::isfinite(point.Z),"Animated bounds exceed numeric range.");
                if (first) { bounds = {point,point}; first = false; }
                bounds.Min = {std::min(bounds.Min.X,point.X),std::min(bounds.Min.Y,point.Y),std::min(bounds.Min.Z,point.Z)};
                bounds.Max = {std::max(bounds.Max.X,point.X),std::max(bounds.Max.Y,point.Y),std::max(bounds.Max.Z,point.Z)};
            }
        }
    }
    return bounds;
}
} // namespace lv
