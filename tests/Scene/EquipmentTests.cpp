#include "TestHarness.h"
#include <minvex/Scene.h>
#include "EquipmentFixture.h"
#include <minvex/EditorDocument.h>
#include <minvex/PoseIO.h>
#include <minvex/Debug.h>
#include <json.hpp>
#include "../../examples/Equipment/EquipmentScene.h"

using namespace test;
namespace {
void Sockets() {
    lv::AssetRegistry assets;
    const auto model = assets.LoadModel(Fixture("AnimatedRibbon.gltf"));
    lv::Scene scene;
    lv::Transform root; root.Position = {3, 0, 0}; root.Scale = {2, 2, 2};
    const auto body = scene.AddModel(model, root);
    const auto joint = model->Skins.at(0).Joints.back();
    lv::SkeletonSocket socket{"Hand", model->Skeleton.Joints[joint].Name}; socket.LocalTransform.Position = {.2f, .1f, 0};
    scene.SetSocket(body, socket);
    lv::Transform relative; relative.Position.X = .3f;
    const auto node = scene.CreateAttachmentNode(body, "Hand", relative, "Weapon attachment");
    const auto weapon = scene.AddModel(assets.LoadModel(Fixture("SharedBox.glb")), {}, node);
    scene.GetAnimation(body).Play(0); scene.GetAnimation(body).Seek(.5);
    const auto expected = root.Matrix() * scene.GetAnimation(body).Evaluate().Global.at(joint) * socket.LocalTransform.Matrix() * relative.Matrix();
    Check(scene.GetWorldTransform(weapon).Elements == expected.Elements, "animated joint, socket offset, node offset and parent scale reach equipment world transform");
    const auto items = scene.CollectRenderItems();
    Check(items.size() == 2 && items.back().WorldTransform.Elements == expected.Elements, "render collection uses the same attachment transform");
    const auto before = scene.GetWorldTransform(weapon);
    scene.UpdateAnimations(.2);
    Check(before.Elements != scene.GetWorldTransform(weapon).Elements, "equipment follows animation without explicit attachment update");
    Rejects([&] { scene.RemoveSocket(body, "Hand"); }, "in-use socket cannot be removed");
    Rejects([&] { scene.AttachToSocket(body, body, "Hand"); }, "attachment cycle is rejected");
    Rejects([&] { scene.AttachToSocket(node, body, "missing"); }, "unknown socket is rejected without modifying attachment");
    Rejects([&] { scene.SetSocket(body, {"Invalid", "missing"}); }, "missing joint is rejected");
    Rejects([&] { scene.SetModel(body, {}); }, "model detach cannot orphan sockets");
    Check(scene.GetObject(node).AttachmentSocket == "Hand", "failed edits retain original attachment");
    const auto world = scene.GetWorldTransform(node);
    scene.DetachFromSocket(node);
    const auto detached = scene.GetWorldTransform(node);
    for (std::size_t i = 0; i < 16; ++i) { Check(Near(world.Elements[i], detached.Elements[i]), "detach preserves representable world TRS"); }
    scene.RemoveSocket(body, "Hand");
    scene.SetVisible(body, false);
    Check(scene.CollectRenderItems().size() == 1, "detached equipment has independent visibility");
    scene.SetSocket(body, socket); scene.AttachToSocket(node, body, "Hand");
    root.Scale.Y = 3; scene.SetTransform(body, root);
    Rejects([&] { scene.DetachFromSocket(node); }, "keep-world detach rejects hierarchy-induced shear without changing attachment");
    Check(scene.GetObject(node).Parent == body && scene.GetObject(node).AttachmentSocket == "Hand", "failed shear detach retains original hierarchy and socket");
    Check(scene.CollectRenderItems().empty(), "attached subtree inherits body visibility");
    scene.RemoveObject(body);
    Check(scene.Size() == 0, "removing body removes attachment node and all equipment descendants");
}
void Leader() {
    lv::AssetRegistry assets;
    const auto bodyModel = assets.LoadModel(Fixture("AnimatedRibbon.gltf"));
    const auto clothing = EquipmentFollower(bodyModel);
    lv::ValidateModel(*clothing);
    const auto shared = lv::CreateSharedSkeleton(bodyModel->Skeleton, {{"Hand", "Tip"}});
    lv::Scene scene;
    const auto follower = scene.AddModel(clothing), body = scene.AddModel(bodyModel), secondBody = scene.AddModel(bodyModel);
    for (auto id : {body, follower, secondBody}) { scene.SetSharedSkeleton(id, shared); }
    scene.SetLeaderPose(follower, body);
    const auto node = scene.CreateAttachmentNode(follower, "Hand");
    scene.GetAnimation(body).Play(0); scene.UpdateAnimations(.5);
    const auto source = scene.EvaluatePose(body), target = scene.EvaluatePose(follower);
    const auto& map = scene.GetObject(follower).SkeletonMap;
    for (std::size_t i = 0; i < clothing->Skeleton.Joints.size(); ++i) {
        if (!clothing->Skeleton.Joints[i].IsJoint) { continue; }
        const auto expected = lv::FindSkeletonJoint(bodyModel->Skeleton, clothing->Skeleton.Joints[i].Name);
        Check(target.Global[i].Elements == source.Global[expected].Elements, "reordered clothing bones use leader's final model-space matrices");
        Check(map.ModelToShared[i] == expected, "model-to-shared binding is independent from node and palette indices");
    }
    Check(scene.GetWorldTransform(node).Elements == source.Global[lv::FindSkeletonJoint(bodyModel->Skeleton, "Tip")].Elements, "socket on follower uses the resolved leader pose");
    Check(scene.GetAnimation(follower).Time() == 0 && scene.GetAnimation(body).Time() == .5, "follower does not advance its independent player");
    Check(scene.EvaluatePose(secondBody).Global[2].Elements != source.Global[2].Elements, "sharing a skeleton does not share independent animation state");
    const auto snapshots = scene.CollectRenderItems();
    const auto snapshot = snapshots.front().Pose;
    scene.GetAnimation(body).CrossFade(1, .4); scene.UpdateAnimations(.2);
    scene.GetAnimation(body).SetAdditiveAnimation(0, .3f); scene.GetAnimation(body).SeekAdditive(.4);
    auto manual = bodyModel->Skeleton.Joints[2].ReferenceLocal; manual.Position.X = .4f; scene.GetAnimation(body).SetJointOverride(2, manual);
    const auto layered = scene.EvaluatePose(body), followed = scene.EvaluatePose(follower);
    Check(followed.Global[lv::FindSkeletonJoint(clothing->Skeleton, "Tip")].Elements == layered.Global[2].Elements, "follower includes cross-fade, additive and final manual overrides");
    Check(snapshot->Global[0].Elements == target.Global[0].Elements, "render pose snapshot stays valid after scene animation changes");
    manual.Position.X = 50; scene.GetAnimation(follower).SetJointOverride(lv::FindSkeletonJoint(clothing->Skeleton, "Tip"), manual);
    Check(scene.EvaluatePose(follower).Global[lv::FindSkeletonJoint(clothing->Skeleton, "Tip")].Elements == layered.Global[2].Elements, "follower's dormant pose overrides do not alter leader motion");
    const auto bounds = scene.GetModelBounds(follower), own = scene.GetAnimation(follower).GetBounds();
    Check(!Near(bounds.Min.X, own.Min.X) || !Near(bounds.Max.Y, own.Max.Y), "follower bounds use leader deformation for focus and picking");
    Rejects([&] { scene.SetLeaderPose(body, follower); }, "leader pose cycle is rejected");
    Rejects([&] { scene.SetLeaderPose(follower, follower); }, "self leader is rejected");
    Rejects([&] { scene.SetSharedSkeleton(body, {}); }, "shared skeleton cannot change under active followers");
    auto bad = bodyModel->Skeleton; bad.Joints[2].ReferenceLocal.Position.X += .2f;
    Rejects([&] { (void)lv::BindSkeleton(*shared, bad); }, "incompatible reference pose requires explicit retargeting");
    bad = bodyModel->Skeleton; bad.Joints[2].Name = "Hip";
    Rejects([&] { (void)lv::CreateSharedSkeleton(bad); }, "ambiguous bone names are rejected");
    scene.SetLeaderPose(secondBody, body); scene.SetLeaderPose(follower, secondBody);
    Check(scene.EvaluatePose(follower).Global[0].Elements == followed.Global[0].Elements, "leader chains resolve even when leader was created later");
    const auto sharedItems = scene.CollectRenderItems();
    Check(sharedItems[1].Pose == sharedItems[2].Pose, "identical model layouts reuse one owned leader pose snapshot per collection");
    scene.SetLeaderPose(follower, lv::NoObject);
    Check(scene.EvaluatePose(follower).Global[0].Elements == scene.GetAnimation(follower).Evaluate().Global[0].Elements, "clearing leader restores dormant own player");
    scene.SetLeaderPose(follower, body); scene.RemoveObject(body);
    Check(scene.GetObject(follower).LeaderPose == lv::NoObject && scene.GetObject(secondBody).LeaderPose == lv::NoObject, "removing external leader clears surviving follower links");
    Check(!scene.CollectRenderItems().empty(), "surviving clothing renders after leader removal");
    bad = bodyModel->Skeleton; bad.Joints[2].Parent = lv::NoJoint;
    Rejects([&] { (void)lv::BindSkeleton(*shared, bad); }, "same bone names with different hierarchy are rejected");
    bad = bodyModel->Skeleton; bad.Joints[2].Name = "ExtraBone";
    Rejects([&] { (void)lv::BindSkeleton(*shared, bad); }, "unregistered clothing bone is rejected");
    const auto noSockets = lv::CreateSharedSkeleton(bodyModel->Skeleton);
    auto subset = std::make_shared<lv::ModelAsset>(*bodyModel); subset->Skeleton.Joints[2].IsJoint = false;
    const auto partial = scene.AddModel(subset), full = scene.AddModel(bodyModel);
    scene.SetSharedSkeleton(partial, noSockets); scene.SetSharedSkeleton(full, noSockets);
    Rejects([&] { scene.SetLeaderPose(full, partial); }, "leader missing a required follower bone is rejected");
    scene.SetLeaderPose(partial, full);
    Check(scene.GetObject(partial).LeaderPose == full, "follower may use a compatible subset of canonical bones");
}
void Motion() {
    const auto bodyModel = equipment::Character(false), clothingModel = equipment::Character(true);
    const auto shared = lv::CreateSharedSkeleton(bodyModel->Skeleton);
    lv::Scene scene; const auto body = scene.AddModel(bodyModel), clothes = scene.AddModel(clothingModel);
    scene.SetSharedSkeleton(body, shared); scene.SetSharedSkeleton(clothes, shared); scene.SetLeaderPose(clothes, body);
    auto& player = scene.GetAnimation(body); player.Play(0);
    constexpr double step = 1.0 / 240;
    double previousAngle = 1, previousVelocity = 0, maxVelocityChange = 0, followerError = 0, minAngle = 1, maxAngle = -1;
    for (int frame = 1; frame <= 960; ++frame) {
        scene.UpdateAnimations(step);
        const auto pose = scene.EvaluatePose(body), follower = scene.EvaluatePose(clothes);
        const auto& matrix = pose.Local[4].Elements;
        const double angle = std::atan2(double(matrix[1]), matrix[0]), velocity = (angle - previousAngle) / step;
        minAngle = std::min(minAngle, angle); maxAngle = std::max(maxAngle, angle);
        if (frame > 1) { maxVelocityChange = std::max(maxVelocityChange, std::abs(velocity - previousVelocity)); }
        previousAngle = angle; previousVelocity = velocity;
        for (std::size_t bone = 0; bone < 7; ++bone) { for (std::size_t i = 0; i < 16; ++i) {
            followerError = std::max(followerError, std::abs(double(pose.Global[bone].Elements[i]) - follower.Global[bone].Elements[i]));
        } }
    }
    Check(maxVelocityChange < .2, "equipment swing eases through both direction changes and loop seam without velocity snaps");
    Check(minAngle < -.99 && maxAngle > .99, "smooth equipment motion retains the original full arm swing range");
    Check(followerError == 0, "Leader Pose follows every sampled bone exactly over two smooth animation loops");
    Check(!player.Blend() && !player.Tree(), "equipment preview uses single-clip Slerp rather than an animation blend transition");
    std::printf("Equipment motion: maximum per-sample angular velocity change %.6f rad/s, follower error %.8g\n", maxVelocityChange, followerError);
}
void Persistence() {
    TemporaryDirectory temp;
    lv::EditorDocument doc;
    const auto asset = doc.ImportModel(Fixture("AnimatedRibbon.gltf")).Value();
    const auto follower = doc.PlaceModel(asset).Value(), body = doc.PlaceModel(asset).Value();
    Check(doc.RenameObject(body, "Body") && doc.RenameObject(follower, "Clothing"), "prepare named modular instances");
    const auto model = doc.GetScene().GetObject(body).Model;
    const auto shared = lv::CreateSharedSkeleton(model->Skeleton, {{"Hand", "Tip"}});
    Check(doc.SetSharedSkeleton(body, shared) && doc.SetSharedSkeleton(follower, shared) && doc.SetLeaderPose(follower, body), "document creates shared skeleton and later-created leader");
    Check(doc.SetParent(follower, body) && doc.PlayAnimation(body, 0) && doc.SeekAnimation(body, .5), "clothing follows body transform and animation");
    const auto node = doc.CreateAttachmentNode(follower, "Hand", {}, "Weapon slot").Value();
    const auto weaponAsset = doc.ImportModel(Fixture("SharedBox.glb")).Value();
    const auto weapon = doc.PlaceModel(weaponAsset, {}, node).Value();
    const auto target = lv::Mat4::Translation({.2f, 0, 0}) * doc.GetScene().GetWorldTransform(node);
    Check(bool(doc.SetWorldTransform(node, target)), "gizmo world transform accepts socket parent frame");
    const auto actual = doc.GetScene().GetWorldTransform(node);
    for (std::size_t i = 0; i < 16; ++i) { Check(Near(actual.Elements[i], target.Elements[i]), "gizmo retains requested world position on animated socket"); }
    lv::SkeletonSocket overrideSocket{"Hand", "Tip"}; overrideSocket.LocalTransform.Position.X = .3f;
    Check(doc.SetSocket(follower, overrideSocket) && doc.Undo() && doc.GetScene().GetObject(follower).Sockets.empty() && doc.Redo(), "socket overrides are independently undoable");
    Check(doc.DetachFromSocket(node) && doc.Undo() && doc.GetScene().GetObject(node).AttachmentSocket == "Hand" && doc.Redo() && doc.Undo(), "detach and undo restore exact attachment relation");
    const auto path = temp.File("Equipment.mvxscene.json");
    Check(bool(doc.Save(path)), "Scene saves complete modular character");
    const auto saved = nlohmann::json::parse(Read(path));
    Check(saved["version"] == 12 && saved["sharedSkeletons"].size() == 1, "Scene v12 deduplicates shared skeleton definitions");
    lv::EditorDocument loaded;
    Check(bool(loaded.Open(path)), "Scene v12 restores complete modular character");
    const auto ids = loaded.GetScene().ObjectIds();
    const auto restoredFollower = ids[0], restoredBody = ids[1], restoredNode = ids[2], restoredWeapon = ids[3];
    Check(loaded.GetScene().GetObject(restoredFollower).SharedSkeleton == loaded.GetScene().GetObject(restoredBody).SharedSkeleton &&
        loaded.GetScene().GetObject(restoredFollower).LeaderPose == restoredBody && restoredBody != body, "load remaps IDs and preserves shared handle identity");
    const auto before = doc.GetScene().GetWorldTransform(weapon), after = loaded.GetScene().GetWorldTransform(restoredWeapon);
    for (std::size_t i = 0; i < 16; ++i) { Check(Near(before.Elements[i], after.Elements[i]), "archive restores animated socket and equipment world transform"); }
    const auto posePath = temp.File("Clothing.mvxpose.json");
    Check(bool(loaded.SavePoseFile(restoredFollower, posePath)), "full follower pose export uses resolved visible pose");
    const auto exported = lv::LoadPose(posePath, model->Skeleton);
    lv::AnimationPlayer independent; independent.Bind(model); independent.ApplyPose(exported.Value());
    const auto finalPose = loaded.GetScene().EvaluatePose(restoredFollower);
    for (std::size_t i = 0; i < 16; ++i) { Check(Near(independent.Evaluate().Global[2].Elements[i], finalPose.Global[2].Elements[i]), "exported follower pose reconstructs leader deformation"); }
    Check(lv::InspectJoint(loaded.GetScene(), restoredFollower, 2).Global.Elements == finalPose.Global[2].Elements, "scene joint inspection reports actual follower pose");
    Check(loaded.RemoveSocket(restoredFollower, "Hand") && loaded.GetScene().GetObject(restoredFollower).Sockets.empty(), "removing socket override restores inherited shared socket while attached");
    Check(bool(loaded.Undo()), "undo restores removed socket override");
    const auto rejects = [&](nlohmann::json changed, const char* label) {
        Write(path, changed.dump()); Check(!loaded.Open(path) && loaded.GetScene().Contains(restoredNode), label);
    };
    auto bad = saved; bad["objects"][0]["leaderPose"] = 999999; rejects(bad, "unknown leader fails without replacing document");
    bad = saved; bad["objects"][1]["leaderPose"] = saved["objects"][0]["id"]; rejects(bad, "leader cycle is rejected before restoring links");
    bad = saved; bad["objects"][0]["sharedSkeleton"] = 999999; rejects(bad, "unknown shared skeleton is rejected");
    bad = saved; bad["objects"][2]["attachmentSocket"] = "Missing"; rejects(bad, "unknown attachment socket is rejected");
    bad = saved; bad["sharedSkeletons"][0]["definition"]["sockets"][0]["joint"] = "Missing"; rejects(bad, "invalid shared socket joint is rejected");
    bad = saved; bad["sharedSkeletons"][0]["definition"]["nodes"][2]["position"][0] = 10; rejects(bad, "incompatible canonical reference pose is rejected");
    bad = saved; bad["objects"][0].erase("leaderPose"); rejects(bad, "Scene v12 requires explicit pose link field");
    bad = saved; bad["objects"][0]["sockets"].push_back(bad["objects"][0]["sockets"][0]); rejects(bad, "duplicate instance socket names are rejected");
    auto legacy = saved; legacy["version"] = 11; legacy.erase("sharedSkeletons");
    Write(path, legacy.dump()); Check(bool(loaded.Open(path)) && !loaded.GetScene().GetObject(loaded.GetScene().ObjectIds()[0]).SharedSkeleton, "Scene v11 remains readable without equipment fields");

    lv::Scene scene; const auto a = scene.AddModel(model), b = scene.AddModel(model);
    scene.SetSocket(a, {"Hand", "Tip"}); scene.SetSocket(b, {"Hand", "Tip"});
    auto invalid = std::make_shared<lv::ModelAsset>(*model); invalid->Skeleton.Joints[2].Name = "Changed";
    Rejects([&] { (void)scene.ReplaceModel(model, invalid); }, "reimport cannot orphan an existing socket");
    Check(scene.GetObject(a).Model == model && scene.GetObject(b).Model == model, "failed reimport preserves every instance");
    scene.SetSharedSkeleton(a, shared); scene.SetSharedSkeleton(b, shared); scene.SetLeaderPose(b, a);
    auto replacement = std::make_shared<lv::ModelAsset>(*model);
    Check(scene.ReplaceModel(model, replacement) == 2 && scene.GetObject(b).LeaderPose == a, "compatible reimport preserves shared skeleton and leader links");
    invalid = std::make_shared<lv::ModelAsset>(*replacement); invalid->Skeleton.Joints[2].ReferenceLocal.Position.X += .2f;
    Rejects([&] { (void)scene.ReplaceModel(replacement, invalid); }, "shared reference mismatch aborts reimport transaction");
    Check(scene.GetObject(a).Model == replacement && scene.GetObject(b).Model == replacement, "shared reimport failure preserves both models");
    Check(doc.RemoveObject(body) && !doc.GetScene().Contains(weapon) && doc.Undo() && doc.GetScene().GetObject(follower).LeaderPose == body && doc.GetScene().Contains(weapon), "delete subtree undo restores clothing, sockets, weapon and links");
    lv::Scene deep; lv::ObjectId leader = lv::NoObject;
    for (int i = 0; i < 257; ++i) {
        const auto id = deep.AddModel(model); deep.SetSharedSkeleton(id, shared); deep.SetLeaderPose(id, leader); leader = id;
    }
    const auto tooDeep = temp.File("TooDeep.mvxscene.json");
    Check(!lv::SaveScene(deep, tooDeep) && !std::filesystem::exists(tooDeep), "save rejects Leader Pose chains the reader cannot restore before creating a file");
}
}
int main(int argc, char** argv) {
    try { if (argc == 2 && std::string_view(argv[1]) == "sockets") { Sockets(); } else if (argc == 2 && std::string_view(argv[1]) == "leader") { Leader(); } else if (argc == 2 && std::string_view(argv[1]) == "persistence") { Persistence(); } else if (argc == 2 && std::string_view(argv[1]) == "motion") { Motion(); } else { Check(false, "select an equipment issue test"); } }
    catch (const std::exception& error) { Check(false, error.what()); }
    return Finish();
}
