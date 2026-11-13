#pragma once

#include <minvex/Asset.h>

#include <unordered_map>

namespace lv {

using ObjectId = std::uint64_t;
inline constexpr ObjectId NoObject = 0;
struct SceneAnimationEvent { ObjectId Object = NoObject; AnimationEventOccurrence Occurrence; };

enum class CameraProjection { Perspective, Orthographic };
struct Camera {
    Vec3 Position{0, 1.5f, 4};
    Vec3 Target{};
    Vec3 Up{0, 1, 0};
    float VerticalFovRadians = 1.04719755f;
    float NearPlane = 0.1f;
    float FarPlane = 1000.0f;
    CameraProjection Projection = CameraProjection::Perspective;
    float OrthographicHeight = 10.0f; // Full vertical span in world units.
    Mat4 ProjectionMatrix(float aspect) const;
    Mat4 ViewProjection(float aspect) const;
};

struct DirectionalLight {
    Vec3 Direction{-0.4f, -0.8f, -0.6f}; // Direction in which light travels.
    Vec3 Color{1, 1, 1};
    float Intensity = 0.75f;
    float Ambient = 0.25f;
    void Validate() const;
};
inline constexpr std::size_t MaxLocalLights = 8;
struct PointLight {
    Vec3 Position{}, Color{1,1,1};
    float Intensity = 1, Range = 10;
    void Validate() const;
};
struct SpotLight {
    Vec3 Position{}, Direction{0,-1,0}, Color{1,1,1};
    float Intensity = 1, Range = 10;
    float InnerConeRadians = .35f, OuterConeRadians = .7f;
    void Validate() const;
};

struct SceneObject {
    ObjectId Id = NoObject;
    std::string Name;
    ObjectId Parent = NoObject;
    Transform LocalTransform;
    bool Visible = true;
    ModelHandle Model;
    AnimationPlayer Animation;
    std::unordered_map<std::size_t,MaterialAsset> MaterialOverrides;
};

struct RenderItem {
    ObjectId Object = NoObject;
    ModelHandle Model;
    Mat4 WorldTransform;
    const AnimationPlayer* Animation = nullptr; // Valid until the owning Scene object is removed.
    const std::unordered_map<std::size_t,MaterialAsset>* MaterialOverrides = nullptr;
};

class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    ObjectId CreateObject(std::string name = {}, ObjectId parent = NoObject);
    ObjectId AddModel(ModelHandle model, const Transform& transform = {}, ObjectId parent = NoObject);
    void RemoveObject(ObjectId id); // Removes the entire subtree.
    const SceneObject& GetObject(ObjectId id) const;
    bool Contains(ObjectId id) const noexcept;
    std::size_t Size() const noexcept { return objects_.size(); }
    std::vector<ObjectId> ObjectIds() const { return order_; } // Creation order, including empty/hidden objects.
    void SetName(ObjectId id, std::string name);
    void SetParent(ObjectId id, ObjectId parent); // Keeps local TRS; rejects cycles.
    void SetTransform(ObjectId id, const Transform& transform);
    void SetVisible(ObjectId id, bool visible);
    void SetModel(ObjectId id, ModelHandle model); // Empty handle detaches the model.
    const MaterialAsset& GetMaterial(ObjectId id, std::size_t index) const;
    void SetMaterial(ObjectId id, std::size_t index, const MaterialAsset& material); // Per-instance override.
    void ResetMaterial(ObjectId id, std::size_t index);
    std::size_t ReplaceModel(ModelHandle previous, ModelHandle replacement); // All matching instances, transactional; preserves compatible animation/pose.
    AnimationPlayer& GetAnimation(ObjectId id); // Model instance state; throws for empty objects.
    void UpdateAnimations(double deltaSeconds);
    std::vector<SceneAnimationEvent> PollAnimationEvents(); // Scene order, then each player's traversal order.
    std::size_t DispatchAnimationEvents(std::function<void(const SceneAnimationEvent&)> callback);
    void ClearAnimationEventQueues() noexcept;
    Mat4 GetWorldTransform(ObjectId id) const;
    std::vector<RenderItem> CollectRenderItems() const;

    Camera MainCamera;
    DirectionalLight Sun;
    std::vector<PointLight> PointLights;
    std::vector<SpotLight> SpotLights;
    void ValidateLighting() const; // At most 8 point+spot lights in total.

private:
    friend class EditorDocument; // Private CPU history snapshots preserve object identities.
    std::unordered_map<ObjectId, SceneObject> objects_;
    std::vector<ObjectId> order_;
};

} // namespace lv
