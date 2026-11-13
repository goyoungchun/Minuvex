#include "EditorUI.h"
#include <imgui.h>
#include <algorithm>

namespace lv::editor {
void EditorUI::AdditiveControls(ObjectId object,ModelHandle model) {
    const auto active=Document.GetScene().GetObject(object).Animation.Additive();
    if (!ImGui::CollapsingHeader("Additive Animation",active ? ImGuiTreeNodeFlags_DefaultOpen:ImGuiTreeNodeFlags_None)) {return;}
    if (additiveObject_!=object || additiveModel_!=model) {additiveObject_=object;additiveModel_=model;additiveClip_=active ? active->Clip:0;additiveReferenceClip_=0;additiveReferenceTime_=0;additiveReferenceKind_=0;}
    ImGui::PushID("Additive");
    const auto choose=[&](const char* label,std::size_t& index) {index=std::min(index,model->Animations.size()-1);if (ImGui::BeginCombo(label,model->Animations[index].Name.c_str())) {for (std::size_t i=0;i<model->Animations.size();++i) {ImGui::PushID(static_cast<int>(i));if (ImGui::Selectable(model->Animations[i].Name.c_str(),index==i)) {index=i;}ImGui::PopID();}ImGui::EndCombo();}};
    choose("Additive clip",additiveClip_);
    ImGui::Combo("Reference pose",&additiveReferenceKind_,"Skeleton reference\0Current base pose\0Clip sample\0");
    if (additiveReferenceKind_==2) {choose("Reference clip",additiveReferenceClip_);const double zero=0,duration=model->Animations[additiveReferenceClip_].Duration;additiveReferenceTime_=std::clamp(additiveReferenceTime_,0.0,duration);ImGui::SliderScalar("Reference time",ImGuiDataType_Double,&additiveReferenceTime_,&zero,&duration,"%.3f s");}
    if (ImGui::Button("Apply / Restart layer")) {
        try {
            PoseAsset reference;
            if (additiveReferenceKind_!=0) {const auto buffer=additiveReferenceKind_==1 ? Document.GetScene().GetObject(object).Animation.EvaluateBasePose():EvaluateAnimation(model->Skeleton,model->Animations[additiveReferenceClip_],additiveReferenceTime_);reference.SkeletonSignature=buffer.SkeletonSignature;reference.Name=additiveReferenceKind_==1 ? "Captured base pose":"Frozen clip sample";for (std::size_t i=0;i<buffer.LocalTransforms.size();++i) {if (model->Skeleton.Joints[i].Editable) {reference.Joints.push_back({static_cast<JointId>(i),buffer.LocalTransforms[i]});}}}
            Report(Document.SetAdditiveAnimation(object,additiveClip_,active ? active->Weight:1,reference,active ? std::span<const float>(active->JointMask):std::span<const float>{}));
        }catch (const std::exception& error) {Record(ConsoleLevel::Error,error.what());}
    }
    if (active) {
        float weight=active->Weight;if (ImGui::SliderFloat("Layer weight",&weight,0,1)) {Report(Document.SetAdditiveWeight(object,weight));}
        ImGui::TextWrapped("Reference: %s",active->Reference.Name.empty() ? "Explicit pose":active->Reference.Name.c_str());
        auto playback=*active;const double zero=0,duration=model->Animations[active->Clip].Duration;
        if (ImGui::SliderScalar("Layer time",ImGuiDataType_Double,&playback.Time,&zero,&duration,"%.3f s")) {Report(Document.SeekAdditive(object,playback.Time));}
        bool changed=ImGui::DragScalar("Layer speed",ImGuiDataType_Double,&playback.Speed,.05f,nullptr,nullptr,"%.2f");changed=ImGui::Checkbox("Layer loop",&playback.Loop)||changed;
        if (ImGui::Button(active->State==PlaybackState::Playing ? "Pause layer":"Resume layer")) {playback.State=active->State==PlaybackState::Playing ? PlaybackState::Paused:PlaybackState::Playing;changed=true;}if (changed) {Report(Document.RestoreAdditiveAnimation(object,playback));}
        ImGui::SameLine();if (ImGui::Button("Clear layer")) {Report(Document.ClearAdditiveAnimation(object));}
        if (ImGui::TreeNode("Joint mask")) {
            auto mask=active->JointMask;if (mask.empty()) {mask.assign(model->Skeleton.Joints.size(),1);}bool edited=false;
            if (ImGui::Button("All joints")) {std::fill(mask.begin(),mask.end(),1);edited=true;}ImGui::SameLine();if (ImGui::Button("No joints")) {std::fill(mask.begin(),mask.end(),0);edited=true;}
            if (boneObject_==object && bone_<model->Skeleton.Joints.size() && ImGui::Button("Selected joint subtree")) {std::fill(mask.begin(),mask.end(),0);for (std::size_t i=0;i<mask.size();++i) {for (auto node=static_cast<JointId>(i);node!=NoJoint;node=model->Skeleton.Joints[node].Parent) {if (node==bone_) {mask[i]=1;break;}}}edited=true;}
            for (std::size_t i=0;i<mask.size();++i) {if (!model->Skeleton.Joints[i].Editable) {continue;}ImGui::PushID(static_cast<int>(i));edited=ImGui::SliderFloat(model->Skeleton.Joints[i].Name.c_str(),&mask[i],0,1,"%.2f")||edited;ImGui::PopID();}
            if (edited) {Report(Document.SetAdditiveMask(object,mask));}ImGui::TreePop();
        }
        if (Document.GetScene().GetObject(object).Animation.Additive() && ImGui::TreeNode("Compare Base / Delta / Final")) {
            try {
                auto node=boneObject_==object ? bone_:NoJoint;if (node==NoJoint || node>=model->Skeleton.Joints.size() || !model->Skeleton.Joints[node].Editable) {for (std::size_t i=0;i<model->Skeleton.Joints.size();++i) {if (model->Skeleton.Joints[i].Editable) {node=static_cast<JointId>(i);break;}}}
                if (node<model->Skeleton.Joints.size()) {const auto& player=Document.GetScene().GetObject(object).Animation;const auto base=player.EvaluateBasePose(),final=player.EvaluatePose();const auto delta=player.EvaluateAdditive();ImGui::Text("Joint %u: %s",node,model->Skeleton.Joints[node].Name.c_str());const auto show=[](const char* label,const Transform& value) {ImGui::TextUnformatted(label);ImGui::Text("T %.3f %.3f %.3f | S %.3f %.3f %.3f",value.Position.X,value.Position.Y,value.Position.Z,value.Scale.X,value.Scale.Y,value.Scale.Z);ImGui::Text("R %.3f %.3f %.3f %.3f",value.Rotation.X,value.Rotation.Y,value.Rotation.Z,value.Rotation.W);};show("Base",base.LocalTransforms[node]);show("Delta (Scale ratio)",delta.LocalDeltas[node]);show("Final",final.LocalTransforms[node]);}
            }catch (const std::exception& error) {ImGui::TextWrapped("%s",error.what());}ImGui::TreePop();
        }
    }
    ImGui::PopID();
}
}
