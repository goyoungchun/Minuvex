#include "EditorUI.h"
#include "core/Utf8Path.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

namespace lv::editor {
namespace {
bool StyleEditor(UiStyle& s) {
    bool changed=ImGui::ColorEdit4("Text",s.Text.data());
    const char* labels[]={"Fill top left","Fill top right","Fill bottom right","Fill bottom left"};
    for (int i=0;i<4;++i) {changed=ImGui::ColorEdit4(labels[i],s.Fill[i].data())||changed;}
    if (ImGui::Button("Use top left as solid fill")) {s.Fill.fill(s.Fill[0]);changed=true;}
    changed=ImGui::DragFloat4("Corner radius",s.Radius.data(),.5f,0,4096)||changed;
    changed=ImGui::ColorEdit4("Border",s.Border.data())||changed;changed=ImGui::DragFloat("Border width",&s.BorderWidth,.25f,0,1024)||changed;
    changed=ImGui::ColorEdit4("Shadow",s.Shadow.data())||changed;changed=ImGui::DragFloat("Shadow X",&s.ShadowX,.5f,-1024,1024)||changed;changed=ImGui::DragFloat("Shadow Y",&s.ShadowY,.5f,-1024,1024)||changed;changed=ImGui::DragFloat("Shadow blur",&s.ShadowBlur,.5f,0,128)||changed;changed=ImGui::SliderFloat("Opacity",&s.Opacity,0,1)||changed;return changed;
}
}
void EditorUI::UiDesigner() {
    if (!uiDesigner_) {return;}if (const auto* inspector=ImGui::FindWindowByName("Inspector")) {if (inspector->DockId) {ImGui::SetNextWindowDockID(inspector->DockId,ImGuiCond_FirstUseEver);}}if (!ImGui::Begin("UI Designer",&uiDesigner_)) {ImGui::End();return;}
    auto& canvas=Viewport.Ui;
    ImGui::TextUnformatted("Screen UI / logical pixels");ImGui::Checkbox("Interact with buttons in viewport",&uiPreview_);
    for (int i=0;i<3;++i) {if (i) {ImGui::SameLine();}if (ImGui::Button(i==0 ? "+ Text":i==1 ? "+ Button":"+ Panel")) {UiElement e;e.Kind=static_cast<UiKind>(i);e.Name=i==0 ? "Text":i==1 ? "Button":"Panel";e.Text=i==0 ? "Minvex 텍스트":i==1 ? "Button":"";e.Layout.Y=24+static_cast<float>(canvas.Elements().Value().size())*64;
        if (i>0) {e.Normal.Fill.fill({.16f,.22f,.32f,1});e.Normal.Radius.fill(12);e.Normal.Shadow={0,0,0,.25f};}if (i==1) {e.Typography.Horizontal=UiAlign::Center;e.Hover=e.Normal;e.Hover->Fill.fill({.25f,.42f,.8f,1});e.Pressed=e.Normal;e.Pressed->Fill.fill({.12f,.23f,.52f,1});e.Focused=e.Normal;e.Focused->Border={.5f,.75f,1,1};e.Focused->BorderWidth=2;}auto added=canvas.Add(e);if (added) {uiSelected_=added.Value();}else {ShowError(added.Error());}}}
    ImGui::InputText("UI file",&uiPath_);if (ImGui::Button("Save UI")) {Report(canvas.Save(detail::PathFromUtf8(uiPath_)));}ImGui::SameLine();if (ImGui::Button("Load UI")) {if (Report(canvas.Load(detail::PathFromUtf8(uiPath_)))) {uiSelected_=NoUi;const auto path=canvas.FontDirectory().Value().u8string();uiFontPath_={path.begin(),path.end()};}}
    ImGui::InputText("Font directory",&uiFontPath_);if (ImGui::Button("Create / use Font folder")) {Report(canvas.SetFontDirectory(detail::PathFromUtf8(uiFontPath_),true));}
    ImGui::SameLine();if (ImGui::Button("Refresh fonts")) {auto fonts=canvas.Fonts();if (fonts) {uiFonts_=std::move(fonts.Value());Report(canvas.RefreshFonts());}else {ShowError(fonts.Error());}}
    ImGui::TextWrapped("Font files: TTF, OTF, TTC. Save UI separately from Scene. Fonts stay on this computer or in the selected folder.");
    const auto elements=canvas.Elements().Value();for (const auto& e:elements) {ImGui::PushID(static_cast<int>(e.Id));if (ImGui::Selectable((e.Name.empty() ? "Unnamed":e.Name).c_str(),e.Id==uiSelected_)) {uiSelected_=e.Id;}ImGui::PopID();}
    auto selected=canvas.Get(uiSelected_);if (selected) {auto e=selected.Value();bool changed=false;ImGui::Separator();changed=ImGui::InputText("Name",&e.Name)||changed;if (e.Kind!=UiKind::Panel) {changed=ImGui::InputTextMultiline("Text",&e.Text)||changed;}
        changed=ImGui::Checkbox("Visible",&e.Visible)||changed;ImGui::SameLine();changed=ImGui::Checkbox("Enabled",&e.Enabled)||changed;changed=ImGui::Checkbox("Block scene input",&e.BlocksInput)||changed;
        auto& l=e.Layout;changed=ImGui::DragFloat("X",&l.X,1,-65536,65536)||changed;changed=ImGui::DragFloat("Y",&l.Y,1,-65536,65536)||changed;changed=ImGui::DragFloat("Width",&l.Width,1,1,2048)||changed;changed=ImGui::DragFloat("Height",&l.Height,1,1,2048)||changed;
        changed=ImGui::SliderFloat("Anchor X",&l.AnchorX,0,1)||changed;changed=ImGui::SliderFloat("Anchor Y",&l.AnchorY,0,1)||changed;changed=ImGui::SliderFloat("Pivot X",&l.PivotX,0,1)||changed;changed=ImGui::SliderFloat("Pivot Y",&l.PivotY,0,1)||changed;changed=ImGui::SliderAngle("Rotation",&l.Rotation,-180,180)||changed;changed=ImGui::DragFloat("Scale X",&l.ScaleX,.01f,.01f,16)||changed;changed=ImGui::DragFloat("Scale Y",&l.ScaleY,.01f,.01f,16)||changed;changed=ImGui::InputInt("Z order",&l.Z)||changed;
        bool clipped=e.Clip.has_value();if (ImGui::Checkbox("Clip to viewport rectangle",&clipped)) {if (clipped) {e.Clip=UiRect{0,0,500,500};}else {e.Clip.reset();}changed=true;}if (e.Clip) {changed=ImGui::DragFloat("Clip X",&e.Clip->X)||changed;changed=ImGui::DragFloat("Clip Y",&e.Clip->Y)||changed;changed=ImGui::DragFloat("Clip width",&e.Clip->Width,1,0,65536)||changed;changed=ImGui::DragFloat("Clip height",&e.Clip->Height,1,0,65536)||changed;}
        if (e.Kind!=UiKind::Panel&&ImGui::CollapsingHeader("Typography",ImGuiTreeNodeFlags_DefaultOpen)) {auto& t=e.Typography;
            if (ImGui::BeginCombo("Font",t.Family.empty() ? "OS default":t.Family.c_str())) {if (ImGui::Selectable("OS default",t.Family.empty()&&t.FontFile.empty())) {t.Family.clear();t.FontFile.clear();changed=true;}for (std::size_t i=0;i<uiFonts_.size();++i) {const auto& f=uiFonts_[i];ImGui::PushID(static_cast<int>(i));const auto label=f.Family+(f.File.empty() ? "":" (Font folder)");if (ImGui::Selectable(label.c_str(),t.Family==f.Family&&t.FontFile==f.File)) {t.Family=f.Family;t.FontFile=f.File;changed=true;}ImGui::PopID();}ImGui::EndCombo();}
            changed=ImGui::DragFloat("Font size",&t.Size,.5f,1,256)||changed;changed=ImGui::SliderInt("Weight",&t.Weight,100,900)||changed;changed=ImGui::Checkbox("Italic",&t.Italic)||changed;ImGui::SameLine();changed=ImGui::Checkbox("Underline",&t.Underline)||changed;changed=ImGui::Checkbox("Strike through",&t.StrikeThrough)||changed;changed=ImGui::Checkbox("Word wrap",&t.Wrap)||changed;changed=ImGui::DragFloat("Line height",&t.LineHeight,.01f,.5f,4)||changed;changed=ImGui::DragFloat("Letter spacing",&t.LetterSpacing,.1f,-32,128)||changed;
            int h=static_cast<int>(t.Horizontal),v=static_cast<int>(t.Vertical);if (ImGui::Combo("Horizontal",&h,"Start\0Center\0End\0")) {t.Horizontal=static_cast<UiAlign>(h);changed=true;}if (ImGui::Combo("Vertical",&v,"Start\0Center\0End\0")) {t.Vertical=static_cast<UiAlign>(v);changed=true;}changed=ImGui::DragFloat4("Padding L T R B",t.Padding.data(),.5f,0,1024)||changed;
        }
        if (ImGui::CollapsingHeader("Normal style",ImGuiTreeNodeFlags_DefaultOpen)) {ImGui::PushID("normal");changed=StyleEditor(e.Normal)||changed;ImGui::PopID();}
        if (e.Kind==UiKind::Button) {const char* names[]={"Hover style","Pressed style","Focused style","Disabled style"};std::optional<UiStyle>* states[]={&e.Hover,&e.Pressed,&e.Focused,&e.Disabled};for (int i=0;i<4;++i) {ImGui::PushID(i);bool enabled=states[i]->has_value();if (ImGui::Checkbox(names[i],&enabled)) {if (enabled) {*states[i]=e.Normal;}else {states[i]->reset();}changed=true;}if (*states[i]&&ImGui::TreeNode("Edit state")) {changed=StyleEditor(**states[i])||changed;ImGui::TreePop();}ImGui::PopID();}}
        if (changed) {auto result=canvas.Set(e.Id,e);if (!result) {uiStatus_=result.Error().Message;}else {uiStatus_.clear();}}
        if (ImGui::Button("Delete UI element")) {Report(canvas.Remove(e.Id));uiSelected_=NoUi;}
    }
    if (Viewport.UiError) {ImGui::TextWrapped("UI preview: %s",Viewport.UiError->Message.c_str());if (ImGui::Button("Retry preview")) {Report(canvas.RefreshFonts());}}
    if (!uiStatus_.empty()) {ImGui::TextWrapped("%s",uiStatus_.c_str());}ImGui::End();
}
bool EditorUI::UiViewportInput(float x,float y,float width,float height,bool hovered,bool modal) {
    auto& ui=Viewport.Ui;const auto& io=ImGui::GetIO();if (!uiPreview_||io.AppFocusLost||modal) {ui.ProcessInput({UiInputType::Cancel},width,height).Value();uiPointerDown_=false;return false;}
    const float px=io.MousePos.x-x,py=io.MousePos.y-y;bool blocked=false;
    auto send=[&](UiInputType type) {const auto r=ui.ProcessInput({type,px,py,io.KeyShift},width,height);if (!r) {ShowError(r.Error());return false;}return r.Value();};
    if (hovered||uiPointerDown_) {blocked=send(UiInputType::Move);}
    else {send(UiInputType::Move);}
    if (hovered&&ImGui::IsMouseClicked(0)) {uiPointerDown_=send(UiInputType::Down);blocked=uiPointerDown_||blocked;}
    if (ImGui::IsMouseReleased(0)) {blocked=send(UiInputType::Up)||blocked;uiPointerDown_=false;}
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!io.WantTextInput) {if (ImGui::IsKeyPressed(ImGuiKey_Tab,false)) {blocked=send(UiInputType::Tab)||blocked;}if (ImGui::IsKeyPressed(ImGuiKey_Enter,false)||ImGui::IsKeyPressed(ImGuiKey_Space,false)) {blocked=send(UiInputType::ActivateDown)||blocked;}if (ImGui::IsKeyReleased(ImGuiKey_Enter)||ImGui::IsKeyReleased(ImGuiKey_Space)) {blocked=send(UiInputType::ActivateUp)||blocked;}}
    const auto events=ui.PollActivations();if (events) {for (auto id:events.Value()) {uiStatus_="Activated UI #"+std::to_string(id);}}return blocked;
}
}
