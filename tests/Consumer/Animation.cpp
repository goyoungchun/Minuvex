#include <minvex/SceneIO.h>
#include <minvex/PoseIO.h>

int main(int argc, char** argv) {
    if (argc != 4) { return 2; }
    lv::AssetRegistry assets; const auto model = assets.LoadModel(argv[1]);
    lv::Scene scene; const auto object = scene.AddModel(model);
    auto& player = scene.GetAnimation(object); player.Play(0); player.Seek(.5); player.Pause();
    const auto pose = player.CapturePose("Consumer Pose");
    if (!lv::SavePose(pose,model->Skeleton,argv[3]) || !lv::SaveScene(scene,argv[2])) { return 1; }
    auto loaded = lv::LoadScene(argv[2],assets); auto savedPose = lv::LoadPose(argv[3],model->Skeleton);
    return loaded && savedPose && loaded.Value().World.GetObject(loaded.Value().World.ObjectIds()[0]).Animation.Time() == .5 ? 0 : 1;
}
