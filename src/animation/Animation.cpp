#include <minuvex/Asset.h>

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
}
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
    model_ = std::move(model); clip_.reset(); time_ = 0; speed_ = 1; loop_ = true; state_ = PlaybackState::Stopped;
    overrides_ = {}; if (model_) { overrides_.SkeletonSignature = SkeletonSignature(model_->Skeleton); }
}
bool AnimationPlayer::NeedsEvaluation() const noexcept { return model_ && (clip_.has_value() || !overrides_.Joints.empty() || !model_->Skins.empty()); }
void AnimationPlayer::Rebind(ModelHandle model) {
    Require(bool(model),"Rebind requires a nonempty replacement model.");
    AnimationPlayer next; next.Bind(std::move(model));
    next.speed_ = speed_; next.loop_ = loop_;
    if (clip_ || !overrides_.Joints.empty()) {
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
    (void)next.Evaluate();
    *this = std::move(next);
}
void AnimationPlayer::Play(std::size_t clip, bool restart) {
    Require(model_ && clip < model_->Animations.size(),"Animation clip does not belong to the bound model.");
    ValidateAnimation(model_->Skeleton,model_->Animations[clip]);
    if (restart || clip_ != clip) { time_ = speed_ < 0 ? model_->Animations[clip].Duration : 0; }
    clip_ = clip; state_ = PlaybackState::Playing;
}
void AnimationPlayer::Pause() noexcept { if (state_ == PlaybackState::Playing) { state_ = PlaybackState::Paused; } }
void AnimationPlayer::Resume() { Require(model_ && clip_.has_value(),"Select an animation clip before resuming."); state_ = PlaybackState::Playing; }
void AnimationPlayer::Stop() noexcept { time_ = 0; state_ = PlaybackState::Stopped; }
void AnimationPlayer::Seek(double seconds) {
    Require(model_ && clip_.has_value() && std::isfinite(seconds) && seconds >= 0,"Seek requires a clip and finite nonnegative time.");
    time_ = std::min(seconds,model_->Animations[*clip_].Duration);
}
void AnimationPlayer::SetSpeed(double speed) { Require(std::isfinite(speed) && std::abs(speed) <= 100,"Playback speed must be finite and in [-100,100]."); speed_ = speed; }
void AnimationPlayer::Update(double deltaSeconds) {
    Require(std::isfinite(deltaSeconds) && deltaSeconds >= 0,"Animation delta time must be finite and nonnegative.");
    if (state_ != PlaybackState::Playing || !model_ || !clip_ || speed_ == 0 || deltaSeconds == 0) { return; }
    const auto duration = model_->Animations[*clip_].Duration;
    if (duration == 0) { time_ = 0; state_ = PlaybackState::Stopped; return; }
    const double next = time_+deltaSeconds*speed_; Require(std::isfinite(next),"Animation time advance exceeds numeric range.");
    if (loop_) { time_ = std::fmod(next,duration); if (time_ < 0) { time_ += duration; } }
    else { time_ = std::clamp(next,0.0,duration); if ((speed_ > 0 && next >= duration) || (speed_ < 0 && next <= 0)) { state_ = PlaybackState::Stopped; } }
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
void AnimationPlayer::ResetPose() noexcept { clip_.reset(); time_ = 0; state_ = PlaybackState::Stopped; ClearOverrides(); }
std::vector<Transform> AnimationPlayer::EvaluateLocal() const {
    Require(bool(model_),"Bind a model before evaluating its pose.");
    std::vector<Transform> local; local.reserve(model_->Skeleton.Joints.size());
    for (const auto& joint : model_->Skeleton.Joints) { local.push_back(joint.ReferenceLocal); }
    if (clip_) {
        for (const auto& track : model_->Animations[*clip_].Tracks) {
            const auto end = std::upper_bound(track.Times.begin(),track.Times.end(),time_);
            const auto left = end == track.Times.begin() ? 0 : static_cast<std::size_t>(end-track.Times.begin()-1);
            const auto right = std::min(left+1,track.Times.size()-1);
            const float factor = track.Interpolation == AnimationInterpolation::Step || right == left || time_ <= track.Times.front() ? 0 :
                static_cast<float>(std::clamp((time_-track.Times[left])/(double(track.Times[right])-track.Times[left]),0.0,1.0));
            const auto& a = track.Values[left]; const auto& b = track.Values[right]; auto& t = local[track.Joint];
            if (track.Path == AnimationPath::Rotation) { t.Rotation = Slerp({a[0],a[1],a[2],a[3]},{b[0],b[1],b[2],b[3]},factor); }
            else {
                Vec3 value{std::lerp(a[0],b[0],factor),std::lerp(a[1],b[1],factor),std::lerp(a[2],b[2],factor)};
                if (track.Path == AnimationPath::Translation) { t.Position = value; } else { t.Scale = value; }
            }
        }
    }
    for (const auto& joint : overrides_.Joints) { local[joint.Joint] = joint.Local; }
    return local;
}
PoseAsset AnimationPlayer::CapturePose(std::string name) const {
    const auto local = EvaluateLocal(); PoseAsset pose; pose.Name = std::move(name); pose.SkeletonSignature = SkeletonSignature(model_->Skeleton);
    for (std::size_t i=0;i<local.size();++i) { if (model_->Skeleton.Joints[i].Editable) { pose.Joints.push_back({static_cast<JointId>(i),local[i]}); } }
    return pose;
}
EvaluatedPose AnimationPlayer::Evaluate() const {
    const auto transforms = EvaluateLocal(); EvaluatedPose pose;
    pose.Local.resize(transforms.size()); pose.Global.resize(transforms.size());
    std::vector<bool> visited(transforms.size(),false);
    for (std::size_t i=0;i<transforms.size();++i) { pose.Local[i] = model_->Skeleton.Joints[i].Editable ? transforms[i].Matrix() : model_->Skeleton.Joints[i].ReferenceMatrix; }
    for (std::size_t i=0;i<transforms.size();++i) {
        std::vector<JointId> chain; auto current = static_cast<JointId>(i);
        while (current != NoJoint && !visited[current]) { chain.push_back(current); current = model_->Skeleton.Joints[current].Parent; }
        auto parent = current == NoJoint ? Mat4::Identity() : pose.Global[current];
        for (auto it=chain.rbegin();it!=chain.rend();++it) { parent = parent*pose.Local[*it]; (void)parent.NormalMatrix(); pose.Global[*it] = parent; visited[*it] = true; }
    }
    return pose;
}
Bounds AnimationPlayer::GetBounds() const {
    Require(bool(model_),"Bind a model before computing its bounds.");
    if (!NeedsEvaluation()) { return model_->ModelBounds; }
    const auto pose = Evaluate(); Bounds bounds; bool first = true;
    for (const auto& part : model_->Parts) {
        const auto matrix = part.Node == NoJoint ? part.Transform : pose.Global.at(part.Node);
        for (const auto& primitive : model_->Meshes.at(part.MeshIndex).Primitives) {
            for (const auto& vertex : primitive.Geometry.Vertices) {
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
