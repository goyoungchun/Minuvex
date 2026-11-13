// Headless public SDK example: independent pose evaluation/composition, playback, Pose IO and reset.
#include <minvex/Scene.h>
#include <minvex/PoseIO.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

int main(int argc,char** argv) {
    if (argc != 3) {std::puts("Usage: minvex_sdk_animation animated-model.gltf output.mvxpose.json");return 2;}
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
        const auto sampled=lv::EvaluateAnimation(model->Skeleton,model->Animations[0],delta);
        const auto incoming=lv::EvaluateAnimation(model->Skeleton,model->Animations[model->Animations.size()>1 ? 1:0],0);
        const std::vector<lv::PoseChannels> mask(model->Skeleton.Joints.size(),lv::PoseChannels::All);
        const auto mixed=lv::BlendPoses(model->Skeleton,sampled,incoming,.5f,mask);
        const auto restored=lv::ComposePose(model->Skeleton,lv::ApplyPoseOverrides(model->Skeleton,mixed,loaded.Value()));
        for (std::size_t node=0;node<restored.Global.size();++node) {for (std::size_t element=0;element<16;++element) {
            if (std::abs(restored.Global[node].Elements[element]-expected.Global[node].Elements[element])>1e-5f) {throw std::runtime_error("PoseBuffer composition/override changed the captured pose.");}
        }}
        second.ResetPose();
        for (std::size_t i=0;i<model->Nodes.size();++i) {const auto& node=model->Nodes[i];if (!node.MeshIndex || model->Meshes[*node.MeshIndex].MorphWeights.empty()) {continue;}const auto joint=static_cast<lv::JointId>(i);auto weights=player.EvaluateMorphWeights(joint);weights[0]=.75f;player.SetMorphWeights(joint,weights);const auto geometry=lv::EvaluateMorph(model->Meshes[*node.MeshIndex].Primitives[0],weights);if (geometry.Vertices.empty() || player.MorphOverrides().empty()) {throw std::runtime_error("CPU Morph evaluation failed.");}player.ClearMorphWeights(joint);}
        if (second.Clip() || !second.Overrides().Joints.empty()) {throw std::runtime_error("ResetPose must restore reference playback state.");}
        player.CrossFade(model->Animations.size()>1 ? 1:0,.5);
        scene.UpdateAnimations(.25);
        if (!player.Blend() || std::abs(player.BlendWeight()-.5f)>1e-6f) {throw std::runtime_error("Cross-fade must reach half weight after half its duration.");}
        player.Pause();scene.UpdateAnimations(.25);
        if (std::abs(player.BlendWeight()-.5f)>1e-6f) {throw std::runtime_error("Pause must freeze the transition.");}
        player.Resume();scene.UpdateAnimations(.25);
        if (player.Blend()) {throw std::runtime_error("Cross-fade must finish at its duration.");}
        lv::BlendTreeDesc treeDesc;treeDesc.Samples={{0,0,0},{model->Animations.size()>1 ? 1u:0u,1,0}};const auto tree=lv::CreateBlendTree(*model,treeDesc);
        player.PlayBlendTree(tree);player.SetBlendParameter("Speed",.5f);player.SeekBlendTree(.5);player.Pause();
        auto independent=player;independent.SetBlendParameter("Speed",1);
        if (player.Tree()->Tree!=independent.Tree()->Tree || player.Tree()->X!=.5f || independent.Tree()->X!=1) {throw std::runtime_error("Blend Tree instance state must be independent.");}
        const auto treePose=lv::ComposePose(model->Skeleton,lv::EvaluateBlendTree(*model,*tree,.5f,0,.5));const auto played=player.Evaluate();
        for (std::size_t node=0;node<played.Global.size();++node) {for (std::size_t element=0;element<16;++element) {if (std::abs(treePose.Global[node].Elements[element]-played.Global[node].Elements[element])>1e-5f) {throw std::runtime_error("Blend Tree facade and evaluator disagree.");}}}
        const auto additiveClip=std::find_if(model->Animations.begin(),model->Animations.end(),[](const auto& clip){return !clip.Tracks.empty();});
        if (additiveClip!=model->Animations.end()) {
            player.SetAdditiveAnimation(static_cast<std::size_t>(additiveClip-model->Animations.begin()),.25f);player.SeekAdditive(additiveClip->Duration*.5);
            const auto deltaPose=player.EvaluateAdditive();const auto finalPose=lv::ComposePose(model->Skeleton,lv::ApplyAdditivePose(model->Skeleton,player.EvaluateBasePose(),deltaPose,.25f));const auto evaluated=player.Evaluate();
            for (std::size_t node=0;node<evaluated.Global.size();++node) {for (std::size_t element=0;element<16;++element) {if (std::abs(finalPose.Global[node].Elements[element]-evaluated.Global[node].Elements[element])>1e-5f) {throw std::runtime_error("Additive player and standalone composition disagree.");}}}
            auto layerCopy=player;layerCopy.SetAdditiveWeight(0);if (player.Additive()->Weight!=.25f) {throw std::runtime_error("Additive state must be independent per instance.");}player.ClearAdditiveAnimation();
        }
        if (model->Animations[0].Duration>0) {
            const auto time=std::min(.1,model->Animations[0].Duration*.5);const std::array<lv::AnimationEvent,1> events{{{time,"SDK event","{}"}}};player.SetAnimationEvents(0,events);player.Play(0);player.SetSpeed(1);player.SetLoop(false);player.Update(time);
            const auto polled=scene.PollAnimationEvents();if (polled.size()!=1 || polled[0].Object!=animated || polled[0].Occurrence.Event.Name!="SDK event") {throw std::runtime_error("Event polling lost clip/instance provenance.");}
            player.Seek(0);player.Update(time);std::size_t received=0;const auto count=scene.DispatchAnimationEvents([&](const lv::SceneAnimationEvent& event){if (event.Object!=animated || event.Occurrence.Event.Payload!="{}") {throw std::runtime_error("Invalid event payload.");}++received;});if (count!=1 || received!=1) {throw std::runtime_error("Event dispatch must consume exactly one snapshot.");}
        }
        std::printf("Animation/Pose: %zu nodes; PoseBuffer, Blend Tree, Additive, Events and independent instances passed.\n",model->Skeleton.Joints.size());
        return 0;
    }catch (const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
