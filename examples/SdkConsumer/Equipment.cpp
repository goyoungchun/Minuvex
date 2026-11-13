#include <minvex/SceneIO.h>
#include <minvex/Debug.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) { return 2; }
    try {
        lv::AssetRegistry assets; const auto model = assets.LoadModel(argv[1]);
        const auto joint = model->Skins.at(0).Joints.back();
        const auto shared = lv::CreateSharedSkeleton(model->Skeleton, {{"Equipment", model->Skeleton.Joints.at(joint).Name}});
        lv::Scene scene; const auto body = scene.AddModel(model), clothes = scene.AddModel(model, {}, body);
        scene.SetSharedSkeleton(body, shared); scene.SetSharedSkeleton(clothes, shared); scene.SetLeaderPose(clothes, body);
        const auto node = scene.CreateAttachmentNode(clothes, "Equipment", {}, "Equipment slot");
        scene.GetAnimation(body).Play(0); scene.UpdateAnimations(.5);
        if (scene.EvaluatePose(body).Global[joint].Elements != scene.EvaluatePose(clothes).Global[joint].Elements ||
            lv::InspectJoint(scene, clothes, joint).Global.Elements != scene.GetWorldTransform(node).Elements) { return 1; }
        if (!lv::SaveScene(scene, argv[2])) { return 1; }
        auto loaded = lv::LoadScene(argv[2], assets); if (!loaded) { return 1; }
        const auto ids = loaded.Value().World.ObjectIds();
        if (loaded.Value().World.GetObject(ids[1]).LeaderPose != ids[0] || loaded.Value().World.GetObject(ids[2]).AttachmentSocket != "Equipment") { return 1; }
        std::puts("Installed Minvex SDK: Shared Skeleton, Leader Pose, sockets and Scene v12 passed."); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
