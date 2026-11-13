#include "EditorUI.h"
#include <imgui.h>
#include <sokol_app.h>
#include <util/sokol_imgui.h>
#include <algorithm>

namespace lv::editor {
namespace {
void MatrixRows(const char* name,const Mat4& matrix) {
    if (ImGui::TreeNode(name)) {const auto& e=matrix.Elements;for (int r=0;r<4;++r) {ImGui::Text("% .5f  % .5f  % .5f  % .5f",e[r],e[r+4],e[r+8],e[r+12]);}ImGui::TreePop();}
}
const char* State(DebugResourceState s) {return s == DebugResourceState::Valid ? "Valid":s == DebugResourceState::Allocated ? "Allocated":s == DebugResourceState::Failed ? "Failed":"Invalid";}
const char* Kind(DebugResourceKind k) {switch (k) {case DebugResourceKind::RenderTarget:return "Target";case DebugResourceKind::Texture:return "Texture";case DebugResourceKind::Buffer:return "Buffer";case DebugResourceKind::Shader:return "Shader";case DebugResourceKind::Pipeline:return "Pipeline";}return "Other";}
}
void EditorUI::RenderDebugger() {
    if (!renderDebugger_) {return;}
    if (ImGui::Begin("Render Debugger",&renderDebugger_)) {
        auto o=Viewport.Debug();bool changed=false;
        const bool shadow=Viewport.Quality().Effective && Viewport.Quality().Effective->Shadow != ShadowMode::Off;
        if (ImGui::BeginCombo("Debug view",DebugViewName(o.View))) {
            for (int i=0;i<=static_cast<int>(DebugView::ShadowVisibility);++i) {const auto v=static_cast<DebugView>(i);const bool pcss=Viewport.Quality().Effective && Viewport.Quality().Effective->Shadow == ShadowMode::Pcss;const bool supported=(v == DebugView::ShadowMap || v == DebugView::ShadowVisibility) ? shadow:(v >= DebugView::ShadowBlockers && v <= DebugView::ShadowPenumbra) ? pcss:true;ImGui::BeginDisabled(!supported);if (ImGui::Selectable(DebugViewName(v),o.View == v)) {o.View=v;changed=true;}ImGui::EndDisabled();if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {ImGui::SetTooltip("Apply compatible Sun shadows (PCSS for blocker/penumbra views) in Quality Settings first.");}}
            ImGui::EndCombo();
        }
        changed=ImGui::Checkbox("Skeleton overlay",&o.ShowSkeleton)||changed;
        changed=ImGui::Checkbox("Bounding boxes",&o.ShowBounds)||changed;
        changed=ImGui::Checkbox("Reference skeleton (cyan)",&o.ShowReferencePose)||changed;
        changed=ImGui::Checkbox("Depth test lines",&o.DepthTest)||changed;
        if (o.View == DebugView::BoneWeight) {changed=ImGui::SliderInt("Skin palette index (-1 = strongest)",&o.WeightJoint,-1,127)||changed;ImGui::TextWrapped("Indices refer to each mesh skin's palette, not global Skeleton node IDs. Rigid vertices show zero (blue).");}
        if (changed) {Report(Viewport.SetDebugOptions(o));}
        if (o.View == DebugView::Depth) {ImGui::TextWrapped("Linear view depth / FarPlane: near black, far white. Surface depth is recomputed into color; the hardware depth attachment is not directly sampled.");}
        if (o.View >= DebugView::ShadowBlockers) {ImGui::TextWrapped("Grayscale: blockers / search samples, average linear map depth (1 = none), radius / maximum, or final visibility. Single directional Sun only.");}
        if (o.View == DebugView::ShadowMap) {ImGui::TextWrapped("Raw directional shadow depth in grayscale. Overlays are hidden in this view.");}
        if (ImGui::Button("Draw origin axis")) {editorDebugDraw_.Clear();editorDebugDraw_.DrawAxis(Mat4::Identity());Report(Viewport.SetDebugDraw(editorDebugDraw_));}ImGui::SameLine();if (ImGui::Button("Clear lines")) {editorDebugDraw_.Clear();Report(Viewport.SetDebugDraw(editorDebugDraw_));}
        const auto info=Viewport.Inspect();const auto t=Viewport.Telemetry();
        ImGui::Text("Submitted: %llu draws / %llu triangles",static_cast<unsigned long long>(t.Frames.DrawCalls),static_cast<unsigned long long>(t.Frames.Triangles));
        ImGui::Text("Debug lines: %llu | overlay draws: %llu",static_cast<unsigned long long>(info.LineCount),static_cast<unsigned long long>(info.LineDrawCalls));
        ImGui::Text("Debug buffers: %.2f MiB [Estimated, additional to Quality costs]",info.DebugBufferBytes/1048576.0);
        ImGui::Text("Instancing saved draws: %llu | instance buffers: %.2f MiB",static_cast<unsigned long long>(info.Optimization.SavedDrawCalls),info.Optimization.InstanceBufferBytes/1048576.0);ImGui::Text("LOD instance draws: %llu / %llu / %llu / %llu",static_cast<unsigned long long>(info.Optimization.LodDraws[0]),static_cast<unsigned long long>(info.Optimization.LodDraws[1]),static_cast<unsigned long long>(info.Optimization.LodDraws[2]),static_cast<unsigned long long>(info.Optimization.LodDraws[3]));
        if (ImGui::CollapsingHeader("Render passes",ImGuiTreeNodeFlags_DefaultOpen)) {for (const auto& p:info.Passes) {ImGui::TextWrapped("%s: %llu draws / %llu triangles | %s | %s",p.Name.c_str(),static_cast<unsigned long long>(p.DrawCalls),static_cast<unsigned long long>(p.Triangles),p.Shader.c_str(),p.Pipeline.c_str());}}
        if (ImGui::CollapsingHeader("Render target inspection",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("Preview",&inspectedTarget_,"Final output\0Resolved scene color\0Shadow depth\0");
            const auto texture=Viewport.InspectionTexture(inspectedTarget_);
            if (texture.id) {const auto width=std::max(1.0f,ImGui::GetContentRegionAvail().x);const float aspect=inspectedTarget_ == 2 ? 1:static_cast<float>(std::max(1,Viewport.Width()))/std::max(1,Viewport.Height());ImGui::Image(simgui_imtextureid_with_sampler(texture,Viewport.InspectionSampler(inspectedTarget_)),{width,width/aspect});}
            else {ImGui::TextWrapped("Unavailable: viewport suspended or shadow mapping disabled.");}
            ImGui::TextWrapped("MSAA color is inspected through its resolved image. Hardware depth/stencil preview is unsupported; select Depth view for linear depth.");
            if (inspectedTarget_ == 2) {ImGui::TextWrapped("Raw R32F is shown in the red channel with nearest sampling. Shadow Map view displays grayscale.");}
        }
        if (ImGui::CollapsingHeader("GPU resources")) {
            for (std::size_t i=0;i<info.Resources.size();++i) {const auto& r=info.Resources[i];ImGui::PushID(static_cast<int>(i));if (ImGui::TreeNode("resource","%s: %s #%u [%s]",Kind(r.Kind),r.Name.c_str(),r.Id,State(r.State))) {ImGui::Text("%s | %d x %d | %dx samples",r.Format.c_str(),r.Width,r.Height,r.Samples);ImGui::Text("Logical bytes: %llu",static_cast<unsigned long long>(r.Bytes));ImGui::TreePop();}ImGui::PopID();}
        }
        ImGui::TextWrapped("Inspection describes Minvex-owned resources and latest submissions. Scene timing excludes GUI/Present. Debug execution is excluded from preset performance evidence.");
    }ImGui::End();
}
void EditorUI::AnimationDebugger() {
    if (!animationDebugger_) {return;}
    if (ImGui::Begin("Animation Debugger",&animationDebugger_)) {
        const auto object=Document.Selection();
        if (!object || !Document.GetScene().GetObject(object).Model) {ImGui::TextWrapped("Select a model instance.");}
        else {
            const auto& instance=Document.GetScene().GetObject(object);const auto& player=instance.Animation;const auto& model=*instance.Model;
            ImGui::TextWrapped("Animation: %s",player.Tree() ? player.Tree()->Tree->Description().Name.c_str():player.Clip() ? model.Animations.at(*player.Clip()).Name.c_str():"Reference pose");
            if (player.Tree()) {ImGui::Text("Tree phase %.3f | X %.3f Y %.3f",player.Tree()->Phase,player.Tree()->X,player.Tree()->Y);}
            ImGui::Text("Time: %.6f | speed: %.2f | state: %d",player.Time(),player.Speed(),static_cast<int>(player.State()));ImGui::Text("Pose: %s | overrides: %zu",player.Overrides().Name.c_str(),player.Overrides().Joints.size());
            if (player.Blend()) {ImGui::Text("Cross-fade: %.1f%% | source: %s",100.0f*player.BlendWeight(),player.Blend()->SourceClip ? model.Animations.at(*player.Blend()->SourceClip).Name.c_str():"frozen pose");}
            if (model.Skeleton.Joints.empty()) {ImGui::TextWrapped("No skeleton nodes available.");}
            else {
                int joint=boneObject_ == object && bone_ < model.Skeleton.Joints.size() ? static_cast<int>(bone_):0;
                if (ImGui::SliderInt("Skeleton node",&joint,0,static_cast<int>(model.Skeleton.Joints.size()-1))) {bone_=static_cast<JointId>(joint);boneObject_=object;}
                try {const auto data=InspectJoint(Document.GetScene(),object,static_cast<JointId>(joint));ImGui::TextWrapped("%s (model-space matrices)",model.Skeleton.Joints[joint].Name.c_str());MatrixRows("Current local",data.Local);MatrixRows("Current global",data.Global);MatrixRows("Reference local",data.ReferenceLocal);MatrixRows("Reference global",data.ReferenceGlobal);
                    const auto current=data.Global.TransformPoint({}),reference=data.ReferenceGlobal.TransformPoint({});ImGui::Text("Reference translation delta: %.5f %.5f %.5f",current.X-reference.X,current.Y-reference.Y,current.Z-reference.Z);
                    ImGui::Text("Skin palette entries: %zu (finite)",data.SkinningMatrices.size());for (std::size_t i=0;i<data.SkinningMatrices.size();++i) {ImGui::PushID(static_cast<int>(i));MatrixRows("Global * inverse bind",data.SkinningMatrices[i]);ImGui::Text("Linear determinant: %.8g",data.SkinningMatrices[i].LinearDeterminant());ImGui::PopID();}
                }catch (const std::exception& e) {ImGui::TextWrapped("Invalid animation data: %s",e.what());}
                if (ImGui::Button("Compare reference skeleton")) {auto o=Viewport.Debug();o.ShowSkeleton=o.ShowReferencePose=true;Report(Viewport.SetDebugOptions(o));}
                ImGui::TextWrapped("Reference comparison is read-only. InspectJoint and overlays do not change playback, manual overrides or Scene dirty state.");
            }
        }
    }ImGui::End();
}
}
