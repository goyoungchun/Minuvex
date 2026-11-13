#include <minvex/Scene.h>

namespace lv {
void Scene::ValidateLeaderBinding(ObjectId id, const SkeletonBinding& binding) const {
    const auto& object = GetObject(id);
    const auto validate = [](const SkeletonBinding& follower, const SkeletonBinding& leader) {
        for (auto bone : follower.ModelToShared) {
            if (bone != NoJoint && (bone >= leader.SharedToModel.size() || leader.SharedToModel[bone] == NoJoint)) {
                throw std::invalid_argument("Replacement leader is missing a follower bone.");
            }
        }
    };
    if (object.LeaderPose) { validate(binding, GetObject(object.LeaderPose).SkeletonMap); }
    for (const auto& [otherId, other] : objects_) { (void)otherId; if (other.LeaderPose == id) { validate(other.SkeletonMap, binding); } }
}
void Scene::SetSharedSkeleton(ObjectId id, SharedSkeletonHandle skeleton) {
    const auto& object = GetObject(id);
    if (!object.Model) { throw std::invalid_argument("Shared Skeleton requires a model instance."); }
    if (skeleton == object.SharedSkeleton) { return; }
    if (object.LeaderPose) { throw std::invalid_argument("Clear Leader Pose before changing Shared Skeleton."); }
    for (const auto& [otherId, other] : objects_) {
        (void)otherId;
        if (other.LeaderPose == id) { throw std::invalid_argument("Clear followers before changing Shared Skeleton."); }
        if (other.Parent != id || other.AttachmentSocket.empty()) { continue; }
        bool found = false;
        for (const auto& socket : object.Sockets) { found |= socket.Name == other.AttachmentSocket; }
        if (skeleton) { for (const auto& socket : skeleton->Sockets()) { found |= socket.Name == other.AttachmentSocket; } }
        if (!found) { throw std::invalid_argument("Shared Skeleton change would orphan a socket attachment."); }
    }
    auto binding = skeleton ? BindSkeleton(*skeleton, object.Model->Skeleton) : SkeletonBinding{};
    auto& target = objects_.at(id); target.SharedSkeleton = std::move(skeleton); target.SkeletonMap = std::move(binding);
}

void Scene::SetLeaderPose(ObjectId follower, ObjectId leader) {
    const auto& object = GetObject(follower);
    if (leader) {
        const auto& source = GetObject(leader);
        if (!object.Model || !source.Model || !object.SharedSkeleton || object.SharedSkeleton != source.SharedSkeleton) {
            throw std::invalid_argument("Leader and follower require the same Shared Skeleton handle.");
        }
        for (auto current = leader; current; current = GetObject(current).LeaderPose) {
            if (current == follower) { throw std::invalid_argument("Leader Pose cannot create a cycle."); }
        }
        for (auto bone : object.SkeletonMap.ModelToShared) {
            if (bone != NoJoint && source.SkeletonMap.SharedToModel.at(bone) == NoJoint) {
                throw std::invalid_argument("Leader is missing a follower bone.");
            }
        }
    }
    objects_.at(follower).LeaderPose = leader;
    objects_.at(follower).Animation.ClearAnimationEventQueue();
}

std::shared_ptr<const EvaluatedPose> Scene::ResolvePose(ObjectId id, PoseCache& cache) const {
    (void)GetObject(id);
    std::vector<ObjectId> chain;
    for (auto current = id; current && !cache.contains(current); current = GetObject(current).LeaderPose) { chain.push_back(current); }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto& object = GetObject(*it);
        if (!object.Model || object.Animation.Model() != object.Model) { throw std::invalid_argument("Scene pose requires its original model-bound player."); }
        if (!object.LeaderPose) { cache.emplace(*it, std::make_shared<EvaluatedPose>(object.Animation.Evaluate())); continue; }
        const auto& leader = GetObject(object.LeaderPose);
        if (object.Model == leader.Model) { cache.emplace(*it, cache.at(leader.Id)); continue; }
        const auto& source = *cache.at(leader.Id);
        const auto& skeleton = object.Model->Skeleton;
        auto pose = ComposePose(skeleton, ReferencePoseBuffer(skeleton));
        std::vector<bool> visited(skeleton.Joints.size(), false);
        for (std::size_t i = 0; i < skeleton.Joints.size(); ++i) {
            std::vector<JointId> nodes; auto current = static_cast<JointId>(i);
            while (current != NoJoint && !visited[current]) { nodes.push_back(current); current = skeleton.Joints[current].Parent; }
            auto parent = current == NoJoint ? Mat4::Identity() : pose.Global[current];
            for (auto node = nodes.rbegin(); node != nodes.rend(); ++node) {
                const auto bone = object.SkeletonMap.ModelToShared.at(*node);
                if (bone == NoJoint) { pose.Global[*node] = parent * pose.Local[*node]; }
                else {
                    pose.Global[*node] = source.Global.at(leader.SkeletonMap.SharedToModel.at(bone));
                    pose.Local[*node] = parent.InverseAffine() * pose.Global[*node];
                }
                (void)pose.Global[*node].NormalMatrix(); parent = pose.Global[*node]; visited[*node] = true;
            }
        }
        cache.emplace(*it, std::make_shared<EvaluatedPose>(std::move(pose)));
    }
    return cache.at(id);
}
} // namespace lv
