#include <minvex/Scene.h>

#include <algorithm>

namespace lv {

void Scene::ValidateSocketModel(ObjectId id, const ModelHandle& model) const {
    const auto& object = GetObject(id);
    if (!object.Sockets.empty() && !model) { throw std::invalid_argument("Remove sockets before detaching their model."); }
    for (const auto& socket : object.Sockets) { ValidateSkeletonSocket(model->Skeleton, socket); }
}

void Scene::SetSocket(ObjectId id, const SkeletonSocket& socket) {
    const auto& object = GetObject(id);
    if (!object.Model) { throw std::invalid_argument("Sockets require a model."); }
    ValidateSkeletonSocket(object.Model->Skeleton, socket);
    auto& sockets = objects_.at(id).Sockets;
    const auto found = std::find_if(sockets.begin(), sockets.end(), [&](const auto& s) { return s.Name == socket.Name; });
    if (found != sockets.end()) { auto copy = socket; *found = std::move(copy); }
    else {
        if (sockets.size() >= 256) { throw std::invalid_argument("An object supports at most 256 sockets."); }
        sockets.push_back(socket);
    }
}

const SkeletonSocket& Scene::GetSocket(ObjectId id, std::string_view name) const {
    const auto& object = GetObject(id);
    const auto& sockets = object.Sockets;
    const auto found = std::find_if(sockets.begin(), sockets.end(), [&](const auto& s) { return s.Name == name; });
    if (found != sockets.end()) { return *found; }
    if (object.SharedSkeleton) {
        const auto shared = object.SharedSkeleton->Sockets();
        const auto inherited = std::find_if(shared.begin(), shared.end(), [&](const auto& s) { return s.Name == name; });
        if (inherited != shared.end()) { return *inherited; }
    }
    throw std::invalid_argument("Socket does not exist on this object.");
}

void Scene::RemoveSocket(ObjectId id, std::string_view name) {
    (void)GetSocket(id, name);
    const auto& object = GetObject(id);
    if (std::none_of(object.Sockets.begin(), object.Sockets.end(), [&](const auto& s) { return s.Name == name; })) {
        throw std::invalid_argument("Shared sockets are immutable; only instance overrides can be removed.");
    }
    bool inherited = false;
    if (object.SharedSkeleton) { for (const auto& s : object.SharedSkeleton->Sockets()) { inherited |= s.Name == name; } }
    for (const auto& [childId, child] : objects_) {
        (void)childId;
        if (!inherited && child.Parent == id && child.AttachmentSocket == name) { throw std::invalid_argument("Detach socket children before removing their socket."); }
    }
    std::erase_if(objects_.at(id).Sockets, [&](const auto& s) { return s.Name == name; });
}

EvaluatedPose Scene::EvaluatePose(ObjectId id) const {
    PoseCache cache; return *ResolvePose(id, cache);
}

Bounds Scene::GetModelBounds(ObjectId id) const {
    const auto& object = GetObject(id);
    return object.LeaderPose ? object.Animation.GetBounds(EvaluatePose(id)) : object.Animation.GetBounds();
}

Mat4 Scene::AttachmentTransform(const SceneObject& object, PoseCache* cache) const {
    if (object.AttachmentSocket.empty()) { return Mat4::Identity(); }
    const auto& parent = GetObject(object.Parent);
    const auto& socket = GetSocket(parent.Id, object.AttachmentSocket);
    PoseCache local;
    return ResolvePose(parent.Id, cache ? *cache : local)->Global.at(FindSkeletonJoint(parent.Model->Skeleton, socket.Joint)) * socket.LocalTransform.Matrix();
}

Mat4 Scene::GetSocketWorldTransform(ObjectId id, std::string_view name) const {
    const auto& socket = GetSocket(id, name);
    const auto& object = GetObject(id);
    return GetWorldTransform(id) * EvaluatePose(id).Global.at(FindSkeletonJoint(object.Model->Skeleton, socket.Joint)) * socket.LocalTransform.Matrix();
}

void Scene::AttachToSocket(ObjectId id, ObjectId parent, std::string socket, const Transform& relative) {
    (void)GetObject(id);
    (void)GetSocket(parent, socket);
    (void)relative.Matrix().NormalMatrix();
    SetParent(id, parent); // Validates the entire ancestry before mutation.
    auto& object = objects_.at(id);
    object.AttachmentSocket = std::move(socket);
    object.LocalTransform = relative;
}

ObjectId Scene::CreateAttachmentNode(ObjectId parent, std::string socket, const Transform& relative, std::string name) {
    (void)GetSocket(parent, socket);
    (void)relative.Matrix().NormalMatrix();
    const auto id = CreateObject(std::move(name), parent);
    auto& object = objects_.at(id);
    object.AttachmentSocket = std::move(socket);
    object.LocalTransform = relative;
    return id;
}

void Scene::DetachFromSocket(ObjectId id, bool keepWorld) {
    const auto& object = GetObject(id);
    if (object.AttachmentSocket.empty()) { throw std::invalid_argument("Object is not attached to a socket."); }
    const auto transform = keepWorld ? DecomposeTRS(GetWorldTransform(id)) : object.LocalTransform;
    SetParent(id, NoObject);
    objects_.at(id).LocalTransform = transform;
}
void Scene::SetWorldTransform(ObjectId id, const Mat4& world) {
    const auto& object = GetObject(id);
    const auto parent = object.Parent ? GetWorldTransform(object.Parent) * AttachmentTransform(object) : Mat4::Identity();
    SetTransform(id, DecomposeTRS(parent.InverseAffine() * world));
}
} // namespace lv
