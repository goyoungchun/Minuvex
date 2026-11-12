#include "TestHarness.h"
#include <minuvex/Scene.h>

#include <numbers>
#include <limits>

using namespace test;
namespace {

void TestTransforms() {
    lv::Transform transform;
    transform.Position = {2,3,4};
    transform.Scale = {2,3,4};
    const auto sine = std::sqrt(0.5f);
    transform.Rotation = {0,0,sine,sine};
    const auto point = transform.Matrix().TransformPoint({1,0,0});
    Check(Near(point.X, 2) && Near(point.Y, 5) && Near(point.Z, 4), "TRS applies scale then quaternion rotation then translation");
    auto scaledQuaternion = transform;
    scaledQuaternion.Rotation = {0,0,sine*4,sine*4};
    const auto normalizedPoint = scaledQuaternion.Matrix().TransformPoint({1,0,0});
    Check(Near(point.X, normalizedPoint.X) && Near(point.Y, normalizedPoint.Y), "non-unit quaternions are normalized");
    const auto normal = transform.Matrix().NormalMatrix();
    Check(Near(normal.Elements[1], 0.5f) && Near(normal.Elements[4], -1.0f/3), "inverse transpose handles rotation and nonuniform scale");
    auto shear = lv::Mat4::Identity();
    shear.Elements[4] = 2;
    const auto inverseTranspose = shear.NormalMatrix();
    Check(Near(inverseTranspose.Elements[1], -2), "normal matrix handles affine shear");
    auto mirrored = lv::Mat4::Scale({-2,3,4});
    Check(mirrored.LinearDeterminant() < 0 && Near(mirrored.NormalMatrix().Elements[0], -0.5f), "negative scale retains winding and normal handedness");
    Rejects([] { lv::Transform t; t.Scale.Y = 0; (void)t.Matrix(); }, "zero scales are rejected");
    Rejects([] { lv::Transform t; t.Rotation = {0,0,0,0}; (void)t.Matrix(); }, "zero quaternion is rejected");
    Rejects([] { auto m = lv::Mat4::Identity(); m.Elements[3] = 1; (void)m.NormalMatrix(); }, "projective model transform is rejected");
    Rejects([] { lv::Mat4 m; (void)m.NormalMatrix(); }, "singular normal transform is rejected");
}

void TestScene() {
    lv::AssetRegistry registry;
    const auto model = registry.LoadModel(Fixture("SharedBox.glb"));
    lv::Scene scene;
    const auto root = scene.CreateObject("Root");
    lv::Transform parent; parent.Position = {10,0,0}; parent.Scale = {2,2,2};
    scene.SetTransform(root, parent);
    lv::Transform local; local.Position = {1,2,3};
    const auto first = scene.AddModel(model, local, root);
    const auto second = scene.AddModel(model);
    const auto child = scene.CreateObject("Child", first);
    Check(scene.Size() == 4 && scene.GetObject(first).Model == scene.GetObject(second).Model, "instances share one immutable asset");
    auto point = scene.GetWorldTransform(first).TransformPoint({0,0,0});
    Check(Near(point.X, 12) && Near(point.Y, 4) && Near(point.Z, 6), "world transform includes parent scale and translation");
    auto items = scene.CollectRenderItems();
    Check(items.size() == 2 && items[0].Object == first && items[1].Object == second, "render items have deterministic creation order");
    Check(Near(items[0].WorldTransform.Elements[12], 12), "collected world transform matches query");
    scene.SetVisible(root, false);
    Check(scene.CollectRenderItems().size() == 1, "hidden parent hides model descendants");
    scene.SetVisible(root, true);
    scene.SetVisible(first, false);
    Check(scene.CollectRenderItems().size() == 1, "visibility belongs to the instance, not the asset");
    scene.SetVisible(first, true);
    auto changed = local; changed.Position.X = 4;
    scene.SetTransform(first, changed);
    Check(Near(scene.GetObject(second).LocalTransform.Position.X, 0), "instance transforms are independent");
    scene.SetParent(first, second);
    Check(Near(scene.GetWorldTransform(first).Elements[12], 4), "reparenting to a later-created object updates world transform");
    Rejects([&] { scene.SetParent(second, child); }, "parenting cycle is rejected");
    Rejects([&] { scene.SetParent(first, first); }, "self parenting is rejected");
    Rejects([&] { scene.SetParent(first, 999999999); }, "invalid parent is rejected");
    Check(scene.GetObject(first).Parent == second, "failed reparenting preserves the hierarchy");
    scene.SetModel(second, {});
    Check(scene.CollectRenderItems().size() == 1, "model can be detached from an object");
    scene.SetModel(second, model);
    lv::Scene other;
    const auto otherId = other.AddModel(model);
    Check(otherId != first && !other.Contains(first), "object handles are unique across scenes");
    Rejects([&] { other.SetVisible(first, false); }, "cross-scene object handle is rejected");
    Rejects([&] { scene.AddModel({}); }, "empty model handle is rejected");
    Rejects([&] { (void)scene.GetWorldTransform(lv::NoObject); }, "invalid world transform handle is rejected");
    registry.Clear();
    Check(scene.GetObject(first).Model == model && other.GetObject(otherId).Model == model, "Scene retains assets after registry clear");
    scene.RemoveObject(second);
    Check(!scene.Contains(first) && !scene.Contains(child) && !scene.Contains(second) && scene.Contains(root), "remove deletes the entire reparented subtree");
    Rejects([&] { (void)scene.GetObject(first); }, "stale handle is rejected after deletion");
    Rejects([&] { scene.RemoveObject(first); }, "repeated removal is rejected");
    Check(scene.CollectRenderItems().empty(), "empty scene has no draw items");
    const auto newId = scene.AddModel(model);
    Check(newId != first, "deleted object IDs are never reused");
    scene.SetParent(newId, lv::NoObject);
    Check(scene.GetObject(newId).Parent == lv::NoObject, "object can be detached to the root");
    lv::Transform bad; bad.Scale.X = 0;
    Rejects([&] { scene.SetTransform(newId, bad); }, "bad transform does not enter a Scene");
    Check(Near(scene.GetObject(newId).LocalTransform.Scale.X, 1), "failed transform update preserves previous value");
}

void TestCameraLightAndDepth() {
    lv::Camera camera;
    const auto matrix = camera.ViewProjection(16.0f/9);
    Check(std::isfinite(matrix.Elements[0]), "default camera creates a finite projection");
    Rejects([&] { (void)camera.ViewProjection(0); }, "camera rejects zero aspect");
    camera.Projection=lv::CameraProjection::Orthographic;camera.OrthographicHeight=4;
    const auto ortho=camera.ProjectionMatrix(2);
    Check(Near(ortho.Elements[0],.25f) && Near(ortho.Elements[5],.5f) && Near(ortho.Elements[15],1),"orthographic full span and aspect produce affine projection");
    Check(Near(ortho.TransformPoint({0,0,-camera.NearPlane}).Z,0) && Near(ortho.TransformPoint({0,0,-camera.FarPlane}).Z,1),"orthographic RH depth maps near/far to zero/one");
    Check(Near(ortho.TransformPoint({1,1,-2}).X,ortho.TransformPoint({1,1,-20}).X),"orthographic apparent size does not depend on depth");
    camera.OrthographicHeight=0;Rejects([&] {(void)camera.ViewProjection(1);},"orthographic height must be positive");camera.OrthographicHeight=4;
    camera.Projection=static_cast<lv::CameraProjection>(99);Rejects([&] {(void)camera.ViewProjection(1);},"unknown camera projection is rejected");camera.Projection=lv::CameraProjection::Perspective;
    Rejects([&] {(void)lv::Mat4::Perspective(std::numeric_limits<float>::denorm_min(),1,.1f,100);},"projection overflow is rejected before rendering");
    camera.Target = camera.Position;
    Rejects([&] { (void)camera.ViewProjection(1); }, "camera rejects degenerate view");
    lv::DirectionalLight light;
    light.Validate(); Check(true, "default directional light is valid");
    light.Intensity = -1;
    Rejects([&] { light.Validate(); }, "negative light intensity is rejected");
    light = {}; light.Direction = {};
    Rejects([&] { light.Validate(); }, "zero light direction is rejected");
    lv::Scene deep;
    auto parent = deep.CreateObject();
    for (int i = 0; i < 2000; ++i) { parent = deep.CreateObject({}, parent); }
    Check(deep.CollectRenderItems().empty() && Near(deep.GetWorldTransform(parent).Elements[0], 1), "deep hierarchy evaluates without recursion");
    deep.RemoveObject(deep.GetObject(parent).Parent);
    Check(deep.Size() == 1999, "deep subtree removal is iterative");
}
} // namespace

int main() {
    try { TestTransforms(); TestScene(); TestCameraLightAndDepth(); }
    catch (const std::exception& error) { Check(false, error.what()); }
    return Finish();
}
