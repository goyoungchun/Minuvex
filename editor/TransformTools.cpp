#include "EditorUI.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <cmath>

namespace lv::editor {
void EditorUI::StopCameraEdit(bool cancel) {
    controls_.Reset();cameraDrag_=false;
    if (cameraEditing_ && Document.IsEditing()) {Report(cancel ? Document.CancelEdit():Document.CommitEdit());}
    cameraEditing_=false;
}
void EditorUI::TransformTools(float x,float y,float width,float height,bool enabled) {
    viewportRect_={x,y,width,height};gizmoVisible_=gizmoBlockInput_=false;
    const auto& io=ImGui::GetIO();const auto object=Document.Selection();
    if (gizmoEditing_ && !Document.IsEditing()) {gizmoEditing_=false;gizmoSuppressed_=io.MouseDown[0];}
    if (gizmoEditing_ && (!enabled || io.AppFocusLost || object != gizmoObject_)) {(void)Document.CancelEdit();gizmoEditing_=false;gizmoSuppressed_=io.MouseDown[0];}
    if (gizmoEditing_ && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {(void)Document.CancelEdit();ImGuizmo::Enable(false);gizmoEditing_=false;gizmoSuppressed_=true;}
    if (!io.MouseDown[0]) {gizmoSuppressed_=false;}
    const auto& camera=Document.GetScene().MainCamera;
    const auto view=Mat4::LookAt(camera.Position,camera.Target,camera.Up),projection=camera.ProjectionMatrix(width/height);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());ImGuizmo::SetRect(x,y,width,height);ImGuizmo::SetOrthographic(camera.Projection == CameraProjection::Orthographic);
    ImGui::GetWindowDrawList()->PushClipRect({x,y},{x+width,y+height},true);
    if (grid_) {const auto identity=Mat4::Identity();ImGuizmo::DrawGrid(view.Elements.data(),projection.Elements.data(),identity.Elements.data(),10);
        const auto vp=camera.ViewProjection(width/height);
        const auto point=[&](Vec3 p) {const auto clip=vp.TransformPoint(p);const auto& m=vp.Elements;const float w=m[3]*p.X+m[7]*p.Y+m[11]*p.Z+m[15];return ImVec2(x+(clip.X/w*.5f+.5f)*width,y+(.5f-clip.Y/w*.5f)*height);};
        if (view.TransformPoint({}).Z < -camera.NearPlane) {
            const auto origin=point({});const Vec3 axes[]{{1,0,0},{0,1,0},{0,0,1}};const ImU32 colors[]{IM_COL32(240,70,70,255),IM_COL32(90,220,90,255),IM_COL32(80,140,255,255)};
            for (int axis=0;axis<3;++axis) {if (view.TransformPoint(axes[axis]).Z < -camera.NearPlane) {ImGui::GetWindowDrawList()->AddLine(origin,point(axes[axis]),colors[axis],2);}}
        }
    }
    const bool allow=enabled && !io.AppFocusLost && !gizmoSuppressed_ && !io.WantTextInput && (!Document.IsEditing() || gizmoEditing_) && (!ImGui::IsAnyItemActive() || ImGuizmo::IsUsing());
    ImGuizmo::Enable(allow);
    if (object && Document.GetScene().Contains(object)) {
        auto world=Document.GetScene().GetWorldTransform(object);
        const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE,ImGuizmo::ROTATE,ImGuizmo::SCALE};
        gizmoVisible_=true;
        const bool changed=ImGuizmo::Manipulate(view.Elements.data(),projection.Elements.data(),operations[transformTool_],ImGuizmo::LOCAL,world.Elements.data());
        if (ImGuizmo::IsUsing() && !gizmoEditing_) {if (Report(Document.BeginEdit(),object)) {gizmoEditing_=true;gizmoObject_=object;}}
        if (changed && gizmoEditing_) {if (!Report(Document.SetWorldTransform(object,world),object)) {gizmoEditing_=false;gizmoSuppressed_=true;ImGuizmo::Enable(false);}}
        if (gizmoEditing_ && !ImGuizmo::IsUsing()) {Report(Document.CommitEdit(),object);gizmoEditing_=false;}
        gizmoBlockInput_=gizmoSuppressed_ || ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }
    ImGui::GetWindowDrawList()->PopClipRect();
}
}
