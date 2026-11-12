#include "EditorUI.h"
#include "core/Utf8Path.h"

#include <imgui.h>
#include <imgui_internal.h> // Pinned docking release: initial layout builder only.
#include <misc/cpp/imgui_stdlib.h>
#include <sokol_app.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <numbers>
#include <unordered_map>

namespace lv::editor {
namespace {
std::string Text(const std::filesystem::path& path) { const auto s = path.generic_u8string(); return {s.begin(),s.end()}; }
bool ModelFile(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".gltf" || ext == ".glb";
}
void Id(ObjectId id) { ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id))); }
bool VecInput(const char* label, Vec3& value, float speed = 0.05f) {
    float xyz[]{value.X,value.Y,value.Z};
    if (!ImGui::DragFloat3(label,xyz,speed)) { return false; } value = {xyz[0],xyz[1],xyz[2]}; return true;
}
bool RotationInput(Quaternion& value) {
    const auto length = std::hypot(std::hypot(double(value.X),double(value.Y)),std::hypot(double(value.Z),double(value.W)));
    const double x = value.X/length, y = value.Y/length, z = value.Z/length, w = value.W/length;
    constexpr double degrees = 180/std::numbers::pi;
    float angles[]{static_cast<float>(std::atan2(2*(w*x+y*z),1-2*(x*x+y*y))*degrees),
        static_cast<float>(std::asin(std::clamp(2*(w*y-z*x),-1.0,1.0))*degrees),
        static_cast<float>(std::atan2(2*(w*z+x*y),1-2*(y*y+z*z))*degrees)};
    if (!ImGui::DragFloat3("Rotation (deg)",angles,1)) { return false; }
    const double ax = angles[0]/degrees/2, ay = angles[1]/degrees/2, az = angles[2]/degrees/2;
    const auto cx = std::cos(ax), cy = std::cos(ay), cz = std::cos(az), sx = std::sin(ax), sy = std::sin(ay), sz = std::sin(az);
    value = {static_cast<float>(sx*cy*cz-cx*sy*sz),static_cast<float>(cx*sy*cz+sx*cy*sz),
        static_cast<float>(cx*cy*sz-sx*sy*cz),static_cast<float>(cx*cy*cz+sx*sy*sz)};
    return true;
}
}
void EditorUI::ShowError(const Error& error) { error_ = error.Message + "\n" + error.Hint + (error.SourcePath.empty() ? "" : "\n"+Text(error.SourcePath)); }
void EditorUI::Import(const std::filesystem::path& path, bool place) {
    auto imported = Document.ImportModel(path);
    if (!Report(imported)) { return; }
    selectedAsset_ = imported.Value();
    if (place && Report(Document.PlaceModel(selectedAsset_))) { FrameScene(); }
    status_ = "Imported " + Text(path.filename());
}
void EditorUI::FrameScene() {
    try {
        const float aspect = Viewport.Aspect();
        Report(Document.SetCamera(detail::FitSceneCamera(Document.GetScene(),aspect)));
    } catch (const std::exception& e) { ShowError({ErrorCode::InvalidState,e.what(),"Place a visible model in the Scene first.",{}}); }
}
void EditorUI::Ask(Action action) { pending_ = action; if (!Document.IsDirty()) { ApplyPending(); } }
void EditorUI::RequestOpen(const std::filesystem::path& path) { openPath_ = path; Ask(Action::Open); }
void EditorUI::RequestNew() { Ask(Action::New); }
void EditorUI::RequestQuit() { Ask(Action::Quit); }
void EditorUI::ApplyPending() {
    switch (pending_) {
    case Action::New: Document.New(); selectedAsset_ = 0; controls_.Reset(); status_ = "New Scene"; break;
    case Action::Open:
        if (Report(Document.Open(openPath_))) { selectedAsset_ = 0; controls_.Reset(); status_ = "Opened " + Text(openPath_); }
        break;
    case Action::Quit: quit_ = true; break;
    default: break;
    }
    pending_ = Action::None;
}
void EditorUI::FileDialog(Dialog type) {
    dialog_ = type;
    pathInput_ = type == Dialog::Save ? (Document.Path().empty() ? Text(directory_/"Scene.mvxscene.json") : Text(Document.Path())) : Text(directory_) + "/";
    if (type == Dialog::PoseFull || type == Dialog::PosePartial || type == Dialog::PoseLoad) { poseObject_ = Document.Selection(); pathInput_ = Text(directory_/"Pose.mvxpose.json"); }
    if (type == Dialog::QualitySave || type == Dialog::QualityLoad) {pathInput_=Text(directory_/"Profile.mvxquality.json");}
    if (type == Dialog::TelemetryJson || type == Dialog::TelemetryCsv) {pathInput_=Text(directory_/(type == Dialog::TelemetryJson ? "Telemetry.json":"Telemetry.csv"));}
}
bool EditorUI::SaveDialog(Dialog type, const std::filesystem::path& path) {
    if (type == Dialog::Save) { return Save(path); }
    if (type == Dialog::QualitySave) {return Report(SaveQuality(qualityDraft_,path));}
    if (type == Dialog::TelemetryJson || type == Dialog::TelemetryCsv) {return Report(SaveTelemetry(Viewport.Telemetry(),path,type == Dialog::TelemetryJson ? TelemetryFormat::Json:TelemetryFormat::Csv));}
    if (!Report(Document.SavePoseFile(poseObject_,path,type == Dialog::PosePartial))) { return false; }
    status_ = "Saved Pose " + Text(path); return true;
}
void EditorUI::LoadQualityProfile(const std::filesystem::path& path) {
    auto profile=LoadQuality(path);if (Report(profile)) {qualityDraft_=std::move(profile).Value();quality_=true;status_="Loaded quality profile; Apply to use it.";}
}
Result<void> EditorUI::ApplyQualityProfile(const QualityProfile& profile) {
    auto result=Viewport.SetQuality(profile,Document.GetScene());if (result) {qualityDraft_=profile;}return result;
}
bool EditorUI::Save(const std::filesystem::path& path) {
    if (!Report(Document.Save(path))) { return false; }
    status_ = "Saved " + Text(Document.Path());
    if (pending_ != Action::None) { ApplyPending(); }
    return true;
}
void EditorUI::Menu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene","Ctrl+N")) { RequestNew(); }
            if (ImGui::MenuItem("Open Scene...","Ctrl+O")) { FileDialog(Dialog::Open); }
            if (ImGui::MenuItem("Save","Ctrl+S")) { if (Document.Path().empty()) { FileDialog(Dialog::Save); } else { Save(Document.Path()); } }
            if (ImGui::MenuItem("Save As...","Ctrl+Shift+S")) { FileDialog(Dialog::Save); }
            ImGui::Separator();
            if (ImGui::MenuItem("Import glTF / GLB...")) { FileDialog(Dialog::Import); }
            if (ImGui::MenuItem("Quit")) { RequestQuit(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Viewport",nullptr,&viewport_); ImGui::MenuItem("Hierarchy",nullptr,&hierarchy_);
            ImGui::MenuItem("Inspector",nullptr,&inspector_); ImGui::MenuItem("Asset Browser",nullptr,&assets_);
            ImGui::MenuItem("Bone Inspector",nullptr,&bones_); ImGui::MenuItem("Animation Controls",nullptr,&animation_);
            ImGui::MenuItem("Quality Settings",nullptr,&quality_);
            ImGui::MenuItem("Performance",nullptr,&performance_);
            if (ImGui::MenuItem("Reset layout")) { resetLayout_ = true; viewport_ = hierarchy_ = inspector_ = assets_ = bones_ = animation_ = quality_ = performance_ = true; }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::TextUnformatted(Document.Path().empty() ? "Untitled" : Text(Document.Path().filename()).c_str());
        if (Document.IsDirty()) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
        ImGui::EndMainMenuBar();
    }
    const auto& io = ImGui::GetIO();
    if ((io.KeyCtrl || io.KeySuper) && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_S,false)) {
            if (io.KeyShift || Document.Path().empty()) { FileDialog(Dialog::Save); } else { Save(Document.Path()); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O,false)) { FileDialog(Dialog::Open); }
        if (ImGui::IsKeyPressed(ImGuiKey_N,false)) { RequestNew(); }
    }
}
void EditorUI::Dockspace() {
    const auto dock = ImHashStr("MinuvexMainDockspace");
    const bool existed = ImGui::DockBuilderGetNode(dock) != nullptr;
    ImGui::DockSpaceOverViewport(dock,ImGui::GetMainViewport());
    if (resetLayout_ || !existed) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock,ImGui::GetMainViewport()->WorkSize);
        auto center = dock;
        const auto left = ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,0.19f,nullptr,&center);
        const auto right = ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,0.29f,nullptr,&center);
        const auto bottom = ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,0.30f,nullptr,&center);
        ImGui::DockBuilderDockWindow("Hierarchy",left); ImGui::DockBuilderDockWindow("Inspector",right);
        ImGui::DockBuilderDockWindow("Asset Browser",bottom); ImGui::DockBuilderDockWindow("Viewport",center);
        ImGui::DockBuilderDockWindow("Bone Inspector",right); ImGui::DockBuilderDockWindow("Animation Controls",bottom);
        ImGui::DockBuilderDockWindow("Quality Settings",right);
        ImGui::DockBuilderDockWindow("Performance",right);
        ImGui::DockBuilderFinish(dock);
    }
    // Add Phase 5 panels to a saved Phase 4 layout without resetting user splits.
    for (const auto& panel : {std::pair{"Bone Inspector","Inspector"},std::pair{"Animation Controls","Asset Browser"},std::pair{"Quality Settings","Inspector"},std::pair{"Performance","Inspector"}}) {
        if (!ImGui::FindWindowSettingsByID(ImHashStr(panel.first))) {
            const auto* anchor = ImGui::FindWindowSettingsByID(ImHashStr(panel.second));
            ImGui::DockBuilderDockWindow(panel.first,anchor && anchor->DockId ? anchor->DockId : dock);
        }
    }
}
void EditorUI::Hierarchy() {
    if (!hierarchy_) { return; }
    if (ImGui::Begin("Hierarchy",&hierarchy_)) {
        if (ImGui::Button("+ Empty")) { Report(Document.CreateObject()); }
        ImGui::SameLine(); if (ImGui::Button("Root")) { if (Document.Selection()) { Report(Document.SetParent(Document.Selection(),NoObject)); } }
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* p = ImGui::AcceptDragDropPayload("MVX_OBJECT")) { Report(Document.SetParent(*static_cast<const ObjectId*>(p->Data),NoObject)); }
            ImGui::EndDragDropTarget();
        }
        ImGui::Separator();
        std::unordered_map<ObjectId,std::vector<ObjectId>> children;
        for (auto id : Document.GetScene().ObjectIds()) { children[Document.GetScene().GetObject(id).Parent].push_back(id); }
        std::vector<ObjectId> stack(children[NoObject].rbegin(),children[NoObject].rend());
        while (!stack.empty()) {
            const auto id = stack.back(); stack.pop_back();
            if (!id) { ImGui::TreePop(); continue; }
            const auto& object = Document.GetScene().GetObject(id);
            Id(id);
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (Document.Selection() == id) { flags |= ImGuiTreeNodeFlags_Selected; }
            if (children[id].empty()) { flags |= ImGuiTreeNodeFlags_Leaf; }
            const std::string label = (object.Visible ? "" : "[hidden] ") + (object.Name.empty() ? "Object" : object.Name);
            const bool open = ImGui::TreeNodeEx("node",flags,"%s",label.c_str());
            if (ImGui::IsItemClicked()) { Report(Document.Select(id)); }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("MVX_OBJECT",&id,sizeof(id)); ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_OBJECT")) { Report(Document.SetParent(*static_cast<const ObjectId*>(p->Data),id)); }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
            if (open) { stack.push_back(NoObject); for (auto it = children[id].rbegin(); it != children[id].rend(); ++it) { stack.push_back(*it); } }
        }
    }
    ImGui::End();
}
void EditorUI::Inspector() {
    if (!inspector_) { return; }
    if (ImGui::Begin("Inspector",&inspector_)) {
        const auto id = Document.Selection();
        if (id && Document.GetScene().Contains(id)) {
            const auto object = Document.GetScene().GetObject(id);
            auto name = object.Name; if (ImGui::InputText("Name",&name)) { Report(Document.RenameObject(id,std::move(name))); }
            bool visible = object.Visible; if (ImGui::Checkbox("Visible",&visible)) { Report(Document.SetVisible(id,visible)); }
            if (ImGui::BeginCombo("Parent",object.Parent ? Document.GetScene().GetObject(object.Parent).Name.c_str() : "<root>")) {
                if (ImGui::Selectable("<root>",!object.Parent)) { Report(Document.SetParent(id,NoObject)); }
                for (auto parent : Document.GetScene().ObjectIds()) { if (parent == id) { continue; } Id(parent);
                    if (ImGui::Selectable(Document.GetScene().GetObject(parent).Name.c_str(),object.Parent == parent)) { Report(Document.SetParent(id,parent)); } ImGui::PopID(); }
                ImGui::EndCombo();
            }
            auto t = object.LocalTransform;
            bool changed = VecInput("Position",t.Position); changed = RotationInput(t.Rotation) || changed; changed = VecInput("Scale",t.Scale,0.01f) || changed;
            if (changed) { Report(Document.SetTransform(id,t)); }
            ImGui::TextWrapped("%s",object.Model ? Text(object.Model->SourcePath.filename()).c_str() : "Empty object");
            if (ImGui::Button("Delete subtree")) { Report(Document.RemoveObject(id)); }
            ImGui::Separator();
        } else { ImGui::TextUnformatted("Select an object in Hierarchy."); }
        if (ImGui::CollapsingHeader("Camera")) {
            auto camera = Document.GetScene().MainCamera;
            bool changed = VecInput("Eye",camera.Position); changed = VecInput("Target",camera.Target) || changed;
            changed = VecInput("Up",camera.Up) || changed;
            float fov = camera.VerticalFovRadians*180/std::numbers::pi_v<float>;
            if (ImGui::DragFloat("FOV (deg)",&fov,0.2f,1,175)) { camera.VerticalFovRadians = fov*std::numbers::pi_v<float>/180; changed = true; }
            changed = ImGui::DragFloat("Near",&camera.NearPlane,0.01f) || changed; changed = ImGui::DragFloat("Far",&camera.FarPlane,1) || changed;
            if (changed) { Report(Document.SetCamera(camera)); }
            if (ImGui::Button("Frame Scene")) { FrameScene(); }
        }
        if (ImGui::CollapsingHeader("Directional Light")) {
            auto light = Document.GetScene().Sun;
            bool changed = VecInput("Direction",light.Direction);
            float color[]{light.Color.X,light.Color.Y,light.Color.Z};
            if (ImGui::ColorEdit3("Color",color)) { light.Color = {color[0],color[1],color[2]}; changed = true; }
            changed = ImGui::DragFloat("Intensity",&light.Intensity,0.01f,0,10) || changed;
            changed = ImGui::DragFloat("Ambient",&light.Ambient,0.01f,0,10) || changed;
            if (changed) { Report(Document.SetLight(light)); }
        }
        if (ImGui::CollapsingHeader("Point / Spot Lights (Advanced)")) {
            auto points=Document.GetScene().PointLights;auto spots=Document.GetScene().SpotLights;bool changed=false;
            if (points.size()+spots.size() < MaxLocalLights) {if (ImGui::Button("+ Point")) {points.push_back({});changed=true;}ImGui::SameLine();if (ImGui::Button("+ Spot")) {spots.push_back({});changed=true;}}
            const auto edit=[&](auto& l) {bool c=VecInput("Position",l.Position);float color[]{l.Color.X,l.Color.Y,l.Color.Z};if (ImGui::ColorEdit3("Color",color)) {l.Color={color[0],color[1],color[2]};c=true;}c=ImGui::DragFloat("Intensity",&l.Intensity,.05f,0,100) || c;c=ImGui::DragFloat("Range",&l.Range,.1f,.01f,10000) || c;return c;};
            for (std::size_t i=0;i<points.size();++i) {ImGui::PushID(static_cast<int>(i));if (ImGui::TreeNode("Point","Point %zu",i)) {changed=edit(points[i]) || changed;if (ImGui::Button("Remove")) {points.erase(points.begin()+static_cast<std::ptrdiff_t>(i));changed=true;ImGui::TreePop();ImGui::PopID();break;}ImGui::TreePop();}ImGui::PopID();}
            for (std::size_t i=0;i<spots.size();++i) {ImGui::PushID(static_cast<int>(i)+100);if (ImGui::TreeNode("Spot","Spot %zu",i)) {changed=edit(spots[i]) || changed;changed=VecInput("Direction",spots[i].Direction) || changed;changed=ImGui::SliderFloat("Inner cone",&spots[i].InnerConeRadians,0,1.55f) || changed;changed=ImGui::SliderFloat("Outer cone",&spots[i].OuterConeRadians,.01f,1.56f) || changed;if (ImGui::Button("Remove")) {spots.erase(spots.begin()+static_cast<std::ptrdiff_t>(i));changed=true;ImGui::TreePop();ImGui::PopID();break;}ImGui::TreePop();}ImGui::PopID();}
            if (changed) {Report(Document.SetLocalLights(std::move(points),std::move(spots)));}
        }
    }
    ImGui::End();
}
void EditorUI::BoneInspector() {
    if (!bones_) { return; }
    if (ImGui::Begin("Bone Inspector",&bones_)) {
        const auto object = Document.Selection();
        if (!object || !Document.GetScene().GetObject(object).Model) { ImGui::TextUnformatted("Select a model instance in Hierarchy."); }
        else {
            const auto& instance = Document.GetScene().GetObject(object); const auto& skeleton = instance.Model->Skeleton;
            if (boneObject_ != object) { boneObject_ = object; bone_ = NoJoint; }
            if (bone_ >= skeleton.Joints.size()) {
                for (std::size_t i=0;i<skeleton.Joints.size();++i) { if (skeleton.Joints[i].IsJoint) { bone_ = static_cast<JointId>(i); break; } }
                if (bone_ == NoJoint && !skeleton.Joints.empty()) { bone_ = 0; }
            }
            ImGui::Text("%zu skins | %zu transform nodes",instance.Model->Skins.size(),skeleton.Joints.size());
            if (bone_ != NoJoint && ImGui::BeginCombo("Joint / Node",skeleton.Joints[bone_].Name.c_str())) {
                for (std::size_t i=0;i<skeleton.Joints.size();++i) { ImGui::PushID(static_cast<int>(i));
                    const auto& joint = skeleton.Joints[i]; const auto label = (joint.IsJoint ? "[bone] " : "[node] ") + (joint.Name.empty() ? std::to_string(i) : joint.Name);
                    if (ImGui::Selectable(label.c_str(),bone_ == i)) { bone_ = static_cast<JointId>(i); } ImGui::PopID(); }
                ImGui::EndCombo();
            }
            if (bone_ != NoJoint) {
                const auto& joint = skeleton.Joints[bone_];
                ImGui::Text("Joint ID: %u",bone_);
                ImGui::TextWrapped("Parent: %s",joint.Parent == NoJoint ? "<root>" : skeleton.Joints[joint.Parent].Name.c_str());
                if (!joint.Editable) { ImGui::TextWrapped("Matrix contains shear; TRS editing is unavailable."); }
                else {
                    const auto pose = instance.Animation.CapturePose();
                    auto local = std::find_if(pose.Joints.begin(),pose.Joints.end(),[&](const auto& p) { return p.Joint == bone_; })->Local;
                    ImGui::TextWrapped("Local TRS overrides replace animation on this node.");
                    bool edited = VecInput("Position",local.Position); edited = RotationInput(local.Rotation) || edited; edited = VecInput("Scale",local.Scale,0.01f) || edited;
                    if (edited) { Report(Document.SetJointPose(object,bone_,local)); }
                    const auto& overrides = instance.Animation.Overrides().Joints;
                    ImGui::Text("Override: %s",std::any_of(overrides.begin(),overrides.end(),[&](const auto& p) { return p.Joint == bone_; }) ? "enabled" : "none");
                    if (ImGui::Button("Clear joint override")) { Report(Document.ClearJointPose(object,bone_)); }
                }
            }
            if (ImGui::Button("Clear all overrides")) { Report(Document.ClearPoseOverrides(object)); }
            if (ImGui::Button("Reference Pose")) { Report(Document.ResetPose(object)); }
            ImGui::Separator();
            if (ImGui::Button("Save full Pose...")) { FileDialog(Dialog::PoseFull); }
            if (ImGui::Button("Save overrides...")) { FileDialog(Dialog::PosePartial); }
            if (ImGui::Button("Load Pose...")) { FileDialog(Dialog::PoseLoad); }
        }
    } ImGui::End();
}
void EditorUI::AnimationControls() {
    if (!animation_) { return; }
    if (ImGui::Begin("Animation Controls",&animation_)) {
        const auto object = Document.Selection();
        if (!object || !Document.GetScene().GetObject(object).Model) { ImGui::TextUnformatted("Select a model instance in Hierarchy."); }
        else {
            const auto& instance = Document.GetScene().GetObject(object); const auto& clips = instance.Model->Animations; const auto& player = instance.Animation;
            if (clips.empty()) { ImGui::TextUnformatted("This model has no animation clips. Pose editing remains available."); }
            else {
                if (ImGui::BeginCombo("Clip",player.Clip() ? clips[*player.Clip()].Name.c_str() : "<none>")) {
                    for (std::size_t i=0;i<clips.size();++i) { ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(clips[i].Name.c_str(),player.Clip() == i)) { Report(Document.PlayAnimation(object,i)); } ImGui::PopID(); }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Play / Restart")) { Report(Document.PlayAnimation(object,player.Clip().value_or(0))); }
                ImGui::SameLine(); if (ImGui::Button(player.State() == PlaybackState::Playing ? "Pause" : "Resume")) {
                    if (player.State() == PlaybackState::Playing) { Report(Document.PauseAnimation(object)); }
                    else if (player.Clip()) { Report(Document.ResumeAnimation(object)); } else { Report(Document.PlayAnimation(object,0)); }
                }
                ImGui::SameLine(); if (ImGui::Button("Stop")) { Report(Document.StopAnimation(object)); }
                if (player.Clip()) {
                    double time = player.Time(), zero = 0, duration = clips[*player.Clip()].Duration;
                    if (ImGui::SliderScalar("Time",ImGuiDataType_Double,&time,&zero,&duration,"%.3f s")) { Report(Document.SeekAnimation(object,time)); }
                }
                float speed = static_cast<float>(player.Speed()); if (ImGui::DragFloat("Speed",&speed,0.05f,-100,100)) { Report(Document.SetAnimationSpeed(object,speed)); }
                bool loop = player.Loop(); if (ImGui::Checkbox("Loop",&loop)) { Report(Document.SetAnimationLoop(object,loop)); }
                ImGui::Text("%s | %zu joint overrides",player.State() == PlaybackState::Playing ? "Playing" : player.State() == PlaybackState::Paused ? "Paused" : "Stopped",player.Overrides().Joints.size());
            }
        }
    } ImGui::End();
}
void EditorUI::QualitySettings() {
    if (!quality_) {return;}
    if (ImGui::Begin("Quality Settings",&quality_)) {
        ImGui::InputText("Profile name",&qualityDraft_.Name);
        int base=static_cast<int>(qualityDraft_.Base);
        if (ImGui::Combo("Base preset",&base,"Low\0Medium\0High\0")) {qualityDraft_=MakeQualityProfile(static_cast<QualityPreset>(base));}
        const auto expanded=ExpandQuality(qualityDraft_);
        if (expanded) {
            auto s=expanded.Value();auto& o=qualityDraft_.Overrides;
            int resolution=static_cast<int>(s.Resolution);if (ImGui::Combo("Resolution",&resolution,"Relative\0Fixed\0")) {o.Resolution=static_cast<ResolutionMode>(resolution);}
            if (s.Resolution == ResolutionMode::Fixed) {
                if (ImGui::DragInt("Width",&s.FixedWidth,8,1,16384)) {o.FixedWidth=s.FixedWidth;}
                if (ImGui::DragInt("Height",&s.FixedHeight,8,1,16384)) {o.FixedHeight=s.FixedHeight;}
            } else {
                if (ImGui::SliderFloat("Scale",&s.ResolutionScale,.5f,1,"%.2f")) {o.ResolutionScale=s.ResolutionScale;}
                if (ImGui::DragInt("Height cap",&s.MaxRenderHeight,8,1,16384)) {o.MaxRenderHeight=s.MaxRenderHeight;}
            }
            if (ImGui::DragInt("Target FPS (0 = unlimited)",&s.TargetFps,1,0,240)) {o.TargetFps=s.TargetFps;}
            int samples=s.MsaaSamples == 1 ? 0:s.MsaaSamples == 2 ? 1:s.MsaaSamples == 4 ? 2:3;
            if (ImGui::Combo("MSAA",&samples,"Off\0x2\0x4\0x8\0")) {o.MsaaSamples=1<<samples;}
            int texture=static_cast<int>(s.Textures);if (ImGui::Combo("Textures",&texture,"Low (512)\0Medium (2048)\0High (8192)\0")) {o.Textures=static_cast<TextureQuality>(texture);}
            if (ImGui::SliderInt("Anisotropy",&s.Anisotropy,1,16)) {o.Anisotropy=s.Anisotropy;}
            int lighting=static_cast<int>(s.Lighting);if (ImGui::Combo("Lighting",&lighting,"Unlit\0Basic\0Advanced (normal maps + local lights)\0")) {o.Lighting=static_cast<LightingMode>(lighting);}
            if (ImGui::Checkbox("Frustum culling",&s.FrustumCulling)) {o.FrustumCulling=s.FrustumCulling;}
            int demand=static_cast<int>(s.OnDemand);if (ImGui::Combo("On demand",&demand,"Off\0On\0Auto\0")) {o.OnDemand=static_cast<OnDemandMode>(demand);}
            int shadow=static_cast<int>(s.Shadow);if (ImGui::Combo("Sun shadow",&shadow,"Off\0Basic\0PCF (3x3)\0PCSS (unavailable)\0")) {o.Shadow=static_cast<ShadowMode>(shadow);}
            int shadowSize=s.ShadowResolution == 512 ? 0:s.ShadowResolution == 1024 ? 1:s.ShadowResolution == 2048 ? 2:3;if (ImGui::Combo("Shadow size",&shadowSize,"512\0 1024\0 2048\0 4096\0")) {o.ShadowResolution=512<<shadowSize;}
            if (ImGui::Checkbox("HDR + FXAA + Reinhard",&s.PostProcessing)) {o.PostProcessing=s.PostProcessing;}
            ImGui::TextWrapped("PCSS and LOD are not implemented. Shadow applies to the sun only.");
        } else {ImGui::TextWrapped("%s",expanded.Error().Message.c_str());}
        ImGui::Checkbox("Allow supported fallback",&qualityDraft_.AllowFallback);
        ImGui::TextUnformatted(qualityDraft_.IsCustom() ? "Custom overrides":"Preset defaults");
        if (ImGui::Button("Clear overrides")) {qualityDraft_.Overrides={};}
        ImGui::SameLine();if (ImGui::Button("Apply")) {if (Report(Viewport.SetQuality(qualityDraft_,Document.GetScene()))) {status_="Quality applied";}}
        if (ImGui::Button("Save profile...")) {FileDialog(Dialog::QualitySave);}ImGui::SameLine();if (ImGui::Button("Load profile...")) {FileDialog(Dialog::QualityLoad);}
        const auto& q=Viewport.Quality();ImGui::Separator();
        ImGui::Text("Applied: %s",q.Applied ? "yes":"pending visible viewport");
        if (q.Effective) {
            const auto& effective=*q.Effective;
            ImGui::Text("Render: %d x %d",q.RenderWidth,q.RenderHeight);
            ImGui::Text("MSAA requested / effective: %d / %d",q.RequestedSettings.MsaaSamples,effective.MsaaSamples);
            ImGui::Text("Texture limit: %d / %d",q.RequestedSettings.MaxTextureSize,effective.MaxTextureSize);
            ImGui::Text("Anisotropy: %d / %d",q.RequestedSettings.Anisotropy,effective.Anisotropy);
            ImGui::Text("FPS target: %d | culling: %s",effective.TargetFps,effective.FrustumCulling ? "on":"off");
            const auto mask=effective.PostProcessing ? q.Capabilities.HdrSampleCounts:q.Capabilities.SampleCounts;
            ImGui::Text("Supported %s MSAA: 1%s%s%s",effective.PostProcessing ? "HDR":"LDR",mask&(1u<<2)?", 2":"",mask&(1u<<4)?", 4":"",mask&(1u<<8)?", 8":"");
            ImGui::Text("Shadow format: %s | HDR: %s",q.Capabilities.ShadowMapping ? "supported":"unavailable",q.Capabilities.HdrPostProcessing ? "supported":"unavailable");
            for (const auto& a:q.Adjustments) {ImGui::TextWrapped("%s: %s",a.Field.c_str(),a.Reason.c_str());}
        }
        if (q.LastError) {ImGui::TextWrapped("Previous quality retained: %s",q.LastError->Message.c_str());}
    }ImGui::End();
}
void EditorUI::AssetBrowser() {
    if (!assets_) { return; }
    if (ImGui::Begin("Asset Browser",&assets_)) {
        if (ImGui::Button("Import path...")) { FileDialog(Dialog::Import); }
        ImGui::SameLine(); if (ImGui::Button("Up")) { directory_ = directory_.parent_path(); }
        ImGui::SameLine(); ImGui::TextUnformatted(Text(directory_).c_str());
        ImGui::SetNextItemWidth(200); ImGui::InputTextWithHint("##filter","Filter files",&filter_);
        if (ImGui::BeginTabBar("Assets")) {
            if (ImGui::BeginTabItem("Imported")) {
                ImGui::TextUnformatted("Drag an asset to Viewport, or select and Place.");
                AssetId reimport = 0;
                for (const auto& model : Document.Assets()) {
                    Id(model->Id);
                    if (ImGui::Selectable(Text(model->SourcePath.filename()).c_str(),selectedAsset_ == model->Id,ImGuiSelectableFlags_AllowDoubleClick)) {
                        selectedAsset_ = model->Id;
                        if (ImGui::IsMouseDoubleClicked(0)) { Report(Document.PlaceModel(selectedAsset_)); }
                    }
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("MVX_ASSET",&model->Id,sizeof(AssetId)); ImGui::TextUnformatted(Text(model->SourcePath.filename()).c_str()); ImGui::EndDragDropSource();
                    }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s",Text(model->SourcePath).c_str()); }
                    if (selectedAsset_ == model->Id) {
                        if (ImGui::SmallButton("Place instance")) { if (Report(Document.PlaceModel(selectedAsset_))) { FrameScene(); } }
                        ImGui::SameLine();if (ImGui::SmallButton("Reimport")) {reimport = model->Id;}
                        for (const auto& warning : model->Warnings) { ImGui::TextWrapped("%s",warning.c_str()); }
                    }
                    ImGui::PopID();
                }
                if (reimport) {Report(Document.ReimportModel(reimport));}
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                try {
                    std::vector<std::filesystem::directory_entry> files;
                    for (const auto& entry : std::filesystem::directory_iterator(directory_,std::filesystem::directory_options::skip_permission_denied)) {
                        const auto name = Text(entry.path().filename());
                        if ((entry.is_directory() || ModelFile(entry.path())) && (filter_.empty() || name.find(filter_) != std::string::npos)) { files.push_back(entry); }
                    }
                    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) { return a.path() < b.path(); });
                    for (const auto& entry : files) {
                        const bool folder = entry.is_directory(); const auto path = Text(entry.path());
                        const auto label = (folder ? "[dir] " : "") + Text(entry.path().filename());
                        if (ImGui::Selectable(label.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
                            if (folder) { directory_ = entry.path(); } else { Import(entry.path(),false); }
                        }
                        if (!folder && ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("MVX_FILE",path.c_str(),path.size()+1); ImGui::TextUnformatted(label.c_str()); ImGui::EndDragDropSource(); }
                    }
                } catch (const std::exception& e) { ImGui::TextWrapped("%s",e.what()); }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
void EditorUI::ViewportPanel() {
    if (!viewport_) { controls_.Reset(); cameraDrag_ = false; Viewport.Resize(0,0); return; }
    if (ImGui::Begin("Viewport",&viewport_)) {
        if (ImGui::Button("Frame (F)")) { FrameScene(); }
        ImGui::SameLine(); ImGui::TextUnformatted("Left: Orbit  |  Right/Middle: Pan  |  Wheel: Zoom");
        const auto available = ImGui::GetContentRegionAvail(); const auto& io = ImGui::GetIO();
        if (available.x > 1 && available.y > 1) {
            Viewport.Resize(static_cast<int>(std::min(65536.0f,available.x*sapp_dpi_scale())),static_cast<int>(std::min(65536.0f,available.y*sapp_dpi_scale())));
            ImGui::Image(simgui_imtextureid(Viewport.Texture()),available);
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_ASSET")) { if (Report(Document.PlaceModel(*static_cast<const AssetId*>(p->Data)))) { FrameScene(); } }
                if (const auto* p = ImGui::AcceptDragDropPayload("MVX_FILE")) { Import(detail::PathFromUtf8(static_cast<const char*>(p->Data)),true); }
                ImGui::EndDragDropTarget();
            }
            const bool modal = ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) || dialog_ != Dialog::None || pending_ != Action::None;
            if (io.AppFocusLost || modal) { controls_.Reset(); cameraDrag_ = false; }
            else {
                auto camera = Document.GetScene().MainCamera;
                ViewerInputEvent event; event.Shift = io.KeyShift;
                bool changed = false;
                for (int button = 0; button < 3; ++button) {
                    event.Button = button == 0 ? ViewerMouseButton::Left : button == 1 ? ViewerMouseButton::Right : ViewerMouseButton::Middle;
                    if (hovered && ImGui::IsMouseClicked(button)) { event.Type = ViewerInputType::MouseDown; controls_.Process(camera,event,{},static_cast<int>(available.y)); cameraDrag_ = true; }
                    if (ImGui::IsMouseReleased(button)) { event.Type = ViewerInputType::MouseUp; controls_.Process(camera,event,{},static_cast<int>(available.y)); }
                }
                if (cameraDrag_) { event.Type = ViewerInputType::MouseMove; event.DeltaX = io.MouseDelta.x; event.DeltaY = io.MouseDelta.y; changed = controls_.Process(camera,event,{},static_cast<int>(available.y)); }
                if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2]) { cameraDrag_ = false; }
                if (hovered && io.MouseWheel != 0) { event.Type = ViewerInputType::Scroll; event.ScrollY = io.MouseWheel; changed = controls_.Process(camera,event,{},static_cast<int>(available.y)) || changed; }
                if (changed) { Report(Document.SetCamera(camera)); }
                if (hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F,false)) { FrameScene(); }
            }
        } else { Viewport.Resize(0,0); }
    } else { controls_.Reset(); cameraDrag_ = false; Viewport.Resize(0,0); }
    ImGui::End();
}
void EditorUI::Dialogs() {
    if (pending_ != Action::None && Document.IsDirty() && dialog_ == Dialog::None && overwritePath_.empty() && error_.empty()) { ImGui::OpenPopup("Unsaved changes"); }
    if (ImGui::BeginPopupModal("Unsaved changes",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before continuing?");
        if (ImGui::Button("Save")) {
            if (Document.Path().empty()) { FileDialog(Dialog::Save); ImGui::CloseCurrentPopup(); }
            else if (Save(Document.Path())) { ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Discard")) { ApplyPending(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { pending_ = Action::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (dialog_ != Dialog::None) { ImGui::OpenPopup("File path"); }
    if (ImGui::BeginPopupModal("File path",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(dialog_ == Dialog::Save ? "Save Scene (.mvxscene.json)" : dialog_ == Dialog::Open ? "Open Scene (.mvxscene.json)" : dialog_ == Dialog::Import ? "Import model (.gltf / .glb)" : dialog_ == Dialog::QualitySave || dialog_ == Dialog::QualityLoad ? "Quality profile (.mvxquality.json)":dialog_ == Dialog::TelemetryJson || dialog_ == Dialog::TelemetryCsv ? "Export telemetry snapshot (.json / .csv)":"Pose file (.mvxpose.json)");
        ImGui::SetNextItemWidth(620);
        if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
        const bool enter = ImGui::InputText("Path",&pathInput_,ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("OK") || enter) {
            const auto path = detail::PathFromUtf8(pathInput_);
            bool close = true;
            if (dialog_ == Dialog::Save || dialog_ == Dialog::PoseFull || dialog_ == Dialog::PosePartial || dialog_ == Dialog::QualitySave || dialog_ == Dialog::TelemetryJson || dialog_ == Dialog::TelemetryCsv) {
                std::error_code error;
                if (std::filesystem::is_regular_file(path,error)) { overwritePath_ = path; overwriteDialog_ = dialog_; }
                else { close = SaveDialog(dialog_,path); }
            }
            else if (dialog_ == Dialog::Open) { RequestOpen(path); }
            else if (dialog_ == Dialog::PoseLoad) { close = Report(Document.LoadPoseFile(poseObject_,path)); }
            else if (dialog_ == Dialog::QualityLoad) {auto profile=LoadQuality(path);close=Report(profile);if (close) {qualityDraft_=std::move(profile).Value();}}
            else { Import(path,false); }
            if (close) { dialog_ = Dialog::None; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { dialog_ = Dialog::None; pending_ = Action::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (!overwritePath_.empty()) { ImGui::OpenPopup("Replace file?"); }
    if (ImGui::BeginPopupModal("Replace file?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A file with this name already exists. Replace it?");
        if (ImGui::Button("Replace")) {
            if (SaveDialog(overwriteDialog_,overwritePath_)) { overwritePath_.clear(); ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { overwritePath_.clear(); pending_ = Action::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (!error_.empty()) { ImGui::OpenPopup("Operation failed"); }
    if (ImGui::BeginPopupModal("Operation failed",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*65); ImGui::TextUnformatted(error_.c_str()); ImGui::PopTextWrapPos();
        if (ImGui::Button("Close")) { error_.clear(); ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
    }
}
void EditorUI::Draw() {
    if (pendingQuality_) {const auto profile=std::move(*pendingQuality_);pendingQuality_.reset();Report(ApplyQualityProfile(profile));lastAnalysis_=-1;}
    Menu(); Dockspace(); Hierarchy(); Inspector(); BoneInspector(); AssetBrowser(); AnimationControls(); QualitySettings(); ViewportPanel(); PerformancePanel(); Dialogs();
    if (ImGui::BeginViewportSideBar("Status",ImGui::GetMainViewport(),ImGuiDir_Down,ImGui::GetFrameHeight(),ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings)) {
        const auto stats = Viewport.Stats();
        ImGui::Text("%zu objects | %zu assets | %zu draw calls | %s",Document.GetScene().Size(),Document.Assets().size(),stats.LastDrawCalls,status_.c_str());
    } ImGui::End();
}
} // namespace lv::editor
