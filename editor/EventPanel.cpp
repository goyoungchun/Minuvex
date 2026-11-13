#include "EditorUI.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace lv::editor {
void EditorUI::PollAnimationEvents() {
    auto events=Document.PollAnimationEvents();if (!Report(events)) {return;}
    for (auto& event:events.Value()) {const auto& e=event.Occurrence;Record(ConsoleLevel::Info,"Event ["+std::to_string(event.Object)+"] "+e.ClipName+" / "+e.Event.Name+" @ "+std::to_string(e.Event.TimeSeconds)+(e.Reverse ? " (reverse) ":" ")+e.Event.Payload,{},event.Object);if (eventLog_.size()==128) {eventLog_.erase(eventLog_.begin());}eventLog_.push_back(std::move(event));}
}
void EditorUI::EventControls(ObjectId object,ModelHandle model) {
    const auto& player=Document.GetScene().GetObject(object).Animation;if (!ImGui::CollapsingHeader("Animation Events",player.EventOverrides().empty() ? ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen)) {return;}const auto primary=player.PrimaryEventClip();
    if (eventObject_!=object || eventModel_!=model) {eventObject_=object;eventModel_=model;eventClip_=primary.value_or(0);eventIndex_=0;}
    eventClip_=std::min(eventClip_,model->Animations.size()-1);const auto editingClip=eventClip_;const auto span=player.AnimationEvents(eventClip_);std::vector<AnimationEvent> events(span.begin(),span.end());const auto duration=model->Animations[eventClip_].Duration;double cursor=0;if (primary==eventClip_) {cursor=player.Tree() ? player.Tree()->Phase*duration:player.Time();}
    ImGui::PushID("Animation events");
    if (ImGui::BeginCombo("Event clip",model->Animations[eventClip_].Name.c_str())) {for (std::size_t i=0;i<model->Animations.size();++i) {ImGui::PushID(static_cast<int>(i));if (ImGui::Selectable(model->Animations[i].Name.c_str(),eventClip_==i)) {eventClip_=i;eventIndex_=0;}ImGui::PopID();}ImGui::EndCombo();}
    if (eventClip_!=editingClip) {ImGui::PopID();return;}
    if (ImGui::Button("Play event clip")) {Report(Document.PlayAnimation(object,eventClip_));}
    ImGui::Checkbox("Collect events when seeking",&collectEventSeek_);bool changed=false;std::optional<double> seek;
    const auto origin=ImGui::GetCursorScreenPos();const ImVec2 size{std::max(120.0f,ImGui::GetContentRegionAvail().x),42};ImGui::InvisibleButton("Event timeline",size);auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(25,29,35,255));
    const auto px=[&](double time){return origin.x+8+static_cast<float>(duration>0 ? time/duration:0)*(size.x-16);};const float mid=origin.y+20;draw->AddLine({origin.x+8,mid},{origin.x+size.x-8,mid},IM_COL32(120,130,140,255));
    for (std::size_t i=0;i<events.size();++i) {const auto x=px(events[i].TimeSeconds);draw->AddTriangleFilled({x,mid-8},{x-5,mid+1},{x+5,mid+1},i==eventIndex_ ? IM_COL32(255,190,90,255):IM_COL32(100,190,245,255));}
    if (primary==eventClip_) {const auto x=px(cursor);draw->AddLine({x,origin.y+2},{x,origin.y+size.y-2},IM_COL32(240,100,100,255));}
    if (ImGui::IsItemHovered()) {ImGui::SetTooltip("Click a marker to select it. Double-click the timeline to add an event.");if (ImGui::IsMouseDoubleClicked(0) && events.size()<MaxAnimationEvents) {const auto t=duration*std::clamp(double(ImGui::GetIO().MousePos.x-origin.x-8)/double(size.x-16),0.0,1.0);events.push_back({t,"Event",{}});changed=true;eventIndex_=events.size()-1;}else if (ImGui::IsMouseClicked(0)) {float closest=9;for (std::size_t i=0;i<events.size();++i) {const auto distance=std::abs(px(events[i].TimeSeconds)-ImGui::GetIO().MousePos.x);if (distance<closest) {closest=distance;eventIndex_=i;}}}}
    if (events.size()<MaxAnimationEvents && ImGui::Button("Add event")) {events.push_back({cursor,"Event",{}});eventIndex_=events.size()-1;changed=true;}
    if (!events.empty()) {
        eventIndex_=std::min(eventIndex_,events.size()-1);ImGui::SameLine();if (ImGui::Button("Delete event")) {events.erase(events.begin()+static_cast<std::ptrdiff_t>(eventIndex_));changed=true;eventIndex_=0;}
    }
    if (!events.empty()) {
        if (ImGui::BeginCombo("Marker",events[eventIndex_].Name.c_str())) {for (std::size_t i=0;i<events.size();++i) {ImGui::PushID(static_cast<int>(i));const auto label=std::to_string(events[i].TimeSeconds)+" / "+events[i].Name;if (ImGui::Selectable(label.c_str(),eventIndex_==i)) {eventIndex_=i;}ImGui::PopID();}ImGui::EndCombo();}
        auto& event=events[eventIndex_];const double zero=0;changed=ImGui::DragScalar("Event time",ImGuiDataType_Double,&event.TimeSeconds,.01f,&zero,&duration,"%.3f s",ImGuiSliderFlags_AlwaysClamp)||changed;
        char name[257]{},payload[4097]{};std::copy_n(event.Name.c_str(),std::min<std::size_t>(event.Name.size(),256),name);std::copy_n(event.Payload.c_str(),std::min<std::size_t>(event.Payload.size(),4096),payload);if (ImGui::InputText("Name",name,sizeof(name))) {event.Name=name;changed=true;}if (ImGui::InputTextMultiline("Payload",payload,sizeof(payload),{-1,55})) {event.Payload=payload;changed=true;}
        if (primary==eventClip_ && ImGui::Button("Seek selected marker")) {seek=event.TimeSeconds;}
    }
    if (changed) {const auto selected=events.empty() ? AnimationEvent{}:events[eventIndex_];std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.TimeSeconds<b.TimeSeconds;});if (Report(Document.SetAnimationEvents(object,eventClip_,events))) {const auto found=std::find_if(events.begin(),events.end(),[&](const auto& e){return e.TimeSeconds==selected.TimeSeconds && e.Name==selected.Name && e.Payload==selected.Payload;});eventIndex_=found==events.end() ? 0:static_cast<std::size_t>(found-events.begin());}}
    if (seek) {const auto policy=collectEventSeek_ ? AnimationSeekEvents::Collect:AnimationSeekEvents::Silent;if (Document.GetScene().GetObject(object).Animation.Tree()) {Report(Document.SeekBlendTree(object,duration>0 ? *seek/duration:0,policy));}else {Report(Document.SeekAnimation(object,*seek,policy));}}
    if (ImGui::Button("Reset event overlay")) {Report(Document.ResetAnimationEvents(object,eventClip_));}
    if (ImGui::TreeNode("Event occurrence log")) {if (ImGui::Button("Clear event log")) {eventLog_.clear();}for (const auto& event:eventLog_) {const auto& e=event.Occurrence;ImGui::TextWrapped("Object %llu | %s [%zu] | %.3f %s %s | %s",static_cast<unsigned long long>(event.Object),e.ClipName.c_str(),e.Clip,e.Event.TimeSeconds,e.Reverse ? "reverse":"forward",e.Event.Name.c_str(),e.Event.Payload.c_str());}ImGui::TreePop();}
    ImGui::PopID();
}
}
