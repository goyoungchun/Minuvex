// Headless public SDK example: import, independent playback, Pose IO and reset.
#include <minuvex/Scene.h>
#include <minuvex/PoseIO.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

int main(int argc,char** argv) {
    if (argc != 3) {std::puts("Usage: minuvex_sdk_animation animated-model.gltf output.mvxpose.json");return 2;}
    try {
        lv::AssetRegistry assets;
        const auto model=assets.LoadModel(argv[1]);
        if (model->Animations.empty() || model->Skeleton.Joints.empty()) {throw std::invalid_argument("Use a model containing a skeleton and animation clip.");}
        lv::Scene scene;
        const auto animated=scene.AddModel(model),reference=scene.AddModel(model);
        auto& player=scene.GetAnimation(animated);
        const double delta=std::min(.25,model->Animations.front().Duration*.5);
        player.Play(0);scene.UpdateAnimations(delta);player.Pause();
        if (std::abs(player.Time()-delta)>1e-8 || scene.GetAnimation(reference).Time() != 0) {throw std::runtime_error("Playback must be independent per Scene instance.");}
        const auto pose=player.CapturePose("SDK example");
        const auto saved=lv::SavePose(pose,model->Skeleton,argv[2]);
        if (!saved) {throw std::runtime_error(saved.Error().Message);}
        const auto loaded=lv::LoadPose(argv[2],model->Skeleton);
        if (!loaded) {throw std::runtime_error(loaded.Error().Message);}
        auto& second=scene.GetAnimation(reference);second.ApplyPose(loaded.Value());
        const auto expected=player.Evaluate(),actual=second.Evaluate();
        if (actual.Global.size() != expected.Global.size()) {throw std::runtime_error("Pose roundtrip changed the node count.");}
        for (std::size_t node=0;node<actual.Global.size();++node) {for (std::size_t element=0;element<16;++element) {
            if (std::abs(actual.Global[node].Elements[element]-expected.Global[node].Elements[element])>1e-5f) {throw std::runtime_error("Pose roundtrip changed the evaluated transforms.");}
        }}
        second.ResetPose();
        if (second.Clip() || !second.Overrides().Joints.empty()) {throw std::runtime_error("ResetPose must restore reference playback state.");}
        std::printf("Animation/Pose: %zu nodes, %.3f seconds; independent instances and Pose IO passed.\n",model->Skeleton.Joints.size(),player.Time());
        return 0;
    }catch (const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
