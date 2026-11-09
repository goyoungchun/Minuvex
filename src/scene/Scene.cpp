#include <minuvex/Scene.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>

namespace lv {
namespace {
std::atomic<ObjectId> nextObjectId{1};
}

Mat4 Camera::ViewProjection(float aspect) const {
    return Mat4::Perspective(VerticalFovRadians, aspect, NearPlane, FarPlane) * Mat4::LookAt(Position, Target, Up);
}

void DirectionalLight::Validate() const {
    const double length = std::hypot(double(Direction.X), double(Direction.Y), double(Direction.Z));
    if (!std::isfinite(length) || length == 0 || !std::isfinite(Intensity) || Intensity < 0 ||
        !std::isfinite(Ambient) || Ambient < 0 || !std::isfinite(Color.X) || !std::isfinite(Color.Y) ||
        !std::isfinite(Color.Z) || Color.X < 0 || Color.Y < 0 || Color.Z < 0) {
        throw std::invalid_argument("Directional light requires a finite nonzero direction and nonnegative color/intensity/ambient.");
    }
}

ObjectId Scene::CreateObject(std::string name, ObjectId parent) {
    if (parent) { (void)GetObject(parent); }
    const auto id = nextObjectId.fetch_add(1);
    SceneObject object;
    object.Id = id;
    object.Name = std::move(name);
    object.Parent = parent;
    order_.push_back(id);
    try { objects_.emplace(id, std::move(object)); }
    catch (...) { order_.pop_back(); throw; }
    return id;
}

ObjectId Scene::AddModel(ModelHandle model, const Transform& transform, ObjectId parent) {
    if (!model) { throw std::invalid_argument("AddModel requires a nonempty model handle."); }
    (void)transform.Matrix().NormalMatrix();
    const auto utf8Name = model->SourcePath.stem().generic_u8string();
    const auto id = CreateObject(std::string(utf8Name.begin(), utf8Name.end()), parent);
    auto& object = objects_.at(id);
    object.LocalTransform = transform;
    object.Model = std::move(model);
    return id;
}

const SceneObject& Scene::GetObject(ObjectId id) const {
    const auto found = objects_.find(id);
    if (found == objects_.end()) { throw std::invalid_argument("Object handle does not belong to this Scene or was removed."); }
    return found->second;
}

bool Scene::Contains(ObjectId id) const noexcept { return objects_.contains(id); }

void Scene::SetParent(ObjectId id, ObjectId parent) {
    (void)GetObject(id);
    for (auto ancestor = parent; ancestor; ancestor = GetObject(ancestor).Parent) {
        if (ancestor == id) { throw std::invalid_argument("Scene parenting cannot create a cycle."); }
    }
    objects_.at(id).Parent = parent;
}

void Scene::SetTransform(ObjectId id, const Transform& transform) {
    (void)GetObject(id);
    (void)transform.Matrix().NormalMatrix();
    objects_.at(id).LocalTransform = transform;
}

void Scene::SetVisible(ObjectId id, bool visible) { (void)GetObject(id); objects_.at(id).Visible = visible; }
void Scene::SetModel(ObjectId id, ModelHandle model) { (void)GetObject(id); objects_.at(id).Model = std::move(model); }

Mat4 Scene::GetWorldTransform(ObjectId id) const {
    auto result = Mat4::Identity();
    for (auto current = id; current; current = GetObject(current).Parent) {
        result = GetObject(current).LocalTransform.Matrix() * result;
    }
    if (!id) { (void)GetObject(id); }
    return result;
}

std::vector<RenderItem> Scene::CollectRenderItems() const {
    struct State { Mat4 World; bool Visible; };
    std::unordered_map<ObjectId, State> states;
    states.reserve(objects_.size());
    std::vector<RenderItem> result;
    // Iterative parent-chain evaluation works even after reparenting to an object
    // created later, and avoids recursion for deeply nested imported scenes.
    for (const auto id : order_) {
        std::vector<ObjectId> pending;
        auto current = id;
        while (current && !states.contains(current)) {
            pending.push_back(current);
            current = GetObject(current).Parent;
        }
        State state{Mat4::Identity(), true};
        if (current) { state = states.at(current); }
        for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
            const auto& object = GetObject(*it);
            state.World = state.World * object.LocalTransform.Matrix();
            state.Visible = state.Visible && object.Visible;
            states.emplace(*it, state);
        }
        const auto& object = GetObject(id);
        if (states.at(id).Visible && object.Model) { result.push_back({id, object.Model, states.at(id).World}); }
    }
    return result;
}

void Scene::RemoveObject(ObjectId id) {
    (void)GetObject(id);
    std::unordered_set<ObjectId> removed{id};
    // Each object's ancestry is evaluated once via the same topological traversal.
    std::unordered_map<ObjectId, bool> membership;
    for (const auto candidate : order_) {
        std::vector<ObjectId> chain;
        auto current = candidate;
        while (current && !membership.contains(current) && current != id) {
            chain.push_back(current);
            current = GetObject(current).Parent;
        }
        const bool inSubtree = current == id || (current && membership.at(current));
        for (auto ancestor : chain) { membership[ancestor] = inSubtree; }
        if (inSubtree) { removed.insert(candidate); }
    }
    for (const auto removedId : removed) { objects_.erase(removedId); }
    std::erase_if(order_, [&removed](ObjectId candidate) { return removed.contains(candidate); });
}

} // namespace lv
