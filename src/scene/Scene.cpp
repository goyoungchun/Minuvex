#include <minuvex/Scene.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>
#include <type_traits>

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

void PointLight::Validate() const {
    if (!std::isfinite(Position.X) || !std::isfinite(Position.Y) || !std::isfinite(Position.Z) || !std::isfinite(Color.X) || !std::isfinite(Color.Y) || !std::isfinite(Color.Z) || Color.X < 0 || Color.Y < 0 || Color.Z < 0 || !std::isfinite(Intensity) || Intensity < 0 || !std::isfinite(Range) || Range <= 0) {throw std::invalid_argument("Local lights require finite position, nonnegative color/intensity and positive range.");}
}
void SpotLight::Validate() const {
    PointLight{Position,Color,Intensity,Range}.Validate();const auto length=std::hypot(double(Direction.X),double(Direction.Y),double(Direction.Z));
    if (!std::isfinite(length) || length == 0 || !std::isfinite(InnerConeRadians) || !std::isfinite(OuterConeRadians) || InnerConeRadians < 0 || InnerConeRadians > OuterConeRadians || OuterConeRadians <= 0 || OuterConeRadians >= 1.570796327f) {throw std::invalid_argument("Spot lights require nonzero direction and 0 <= inner <= outer < pi/2 (outer > 0).");}
}
void Scene::ValidateLighting() const {
    Sun.Validate();if (PointLights.size()+SpotLights.size() > MaxLocalLights) {throw std::invalid_argument("A Scene supports at most 8 local lights.");}
    for (const auto& l:PointLights) {l.Validate();}for (const auto& l:SpotLights) {l.Validate();}
}

ObjectId Scene::AddModel(ModelHandle model, const Transform& transform, ObjectId parent) {
    if (!model) { throw std::invalid_argument("AddModel requires a nonempty model handle."); }
    (void)transform.Matrix().NormalMatrix();
    AnimationPlayer animation; animation.Bind(model);
    const auto utf8Name = model->SourcePath.stem().generic_u8string();
    const auto id = CreateObject(std::string(utf8Name.begin(), utf8Name.end()), parent);
    auto& object = objects_.at(id);
    object.LocalTransform = transform;
    object.Model = std::move(model);
    object.Animation = std::move(animation);
    return id;
}

const SceneObject& Scene::GetObject(ObjectId id) const {
    const auto found = objects_.find(id);
    if (found == objects_.end()) { throw std::invalid_argument("Object handle does not belong to this Scene or was removed."); }
    return found->second;
}

bool Scene::Contains(ObjectId id) const noexcept { return objects_.contains(id); }

void Scene::SetName(ObjectId id, std::string name) { (void)GetObject(id); objects_.at(id).Name = std::move(name); }

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
void Scene::SetModel(ObjectId id, ModelHandle model) {
    (void)GetObject(id); AnimationPlayer animation; animation.Bind(model);
    objects_.at(id).Model = std::move(model); objects_.at(id).Animation = std::move(animation);
}
AnimationPlayer& Scene::GetAnimation(ObjectId id) {
    if (!GetObject(id).Model) { throw std::invalid_argument("Object has no model animation state."); }
    return objects_.at(id).Animation;
}
void Scene::UpdateAnimations(double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) { throw std::invalid_argument("Animation delta must be finite and nonnegative."); }
    for (auto id : order_) { if (objects_.at(id).Model) { objects_.at(id).Animation.Update(deltaSeconds); } }
}

std::size_t Scene::ReplaceModel(ModelHandle previous, ModelHandle replacement) {
    static_assert(std::is_nothrow_move_assignable_v<AnimationPlayer>);
    if (!previous || !replacement) {throw std::invalid_argument("ReplaceModel requires nonempty model handles.");}
    if (previous == replacement) {return 0;}
    std::vector<std::pair<ObjectId,AnimationPlayer>> pending;
    for (auto id : order_) {
        const auto& object = objects_.at(id);
        if (object.Model != previous) {continue;}
        if (object.Animation.Model() != previous) {throw std::invalid_argument("Scene player was rebound outside its owning Scene.");}
        auto player = object.Animation; player.Rebind(replacement);
        pending.emplace_back(id,std::move(player));
    }
    for (auto& [id,player] : pending) {
        auto& object = objects_.at(id); object.Model = replacement; object.Animation = std::move(player);
    }
    return pending.size();
}

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
        if (object.Model && object.Animation.Model() != object.Model) { throw std::invalid_argument("Scene-owned player was rebound; use Scene::SetModel."); }
        if (states.at(id).Visible && object.Model) { result.push_back({id, object.Model, states.at(id).World,&object.Animation}); }
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
