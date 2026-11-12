#include "EditorUI.h"
#include "core/Utf8Path.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

namespace lv::editor {
namespace {
std::string Text(const std::filesystem::path& path) {const auto s=path.generic_u8string();return {s.begin(),s.end()};}
void Summary(const ModelAsset& model) {
    const auto info=InspectModel(model);
    ImGui::Text("Format: %s | Meshes: %zu | Primitives: %zu",AssetFormatName(model.Format),info.Meshes,info.Primitives);
    ImGui::Text("Vertices: %zu | Triangles: %zu | Materials: %zu",info.Vertices,info.Indices/3,info.Materials);
    ImGui::Text("Nodes: %zu | Bones: %zu | Skins: %zu | Clips: %zu",info.Nodes,info.Bones,info.Skins,info.Animations);
    ImGui::Text("Geometry: %.2f MiB | Pixels: %.2f MiB | Animation keys: %zu",info.GeometryBytes/1048576.0,info.ImageBytes/1048576.0,info.AnimationSamples);
    ImGui::Text("Bounds: (%.3g, %.3g, %.3g) - (%.3g, %.3g, %.3g)",model.ModelBounds.Min.X,model.ModelBounds.Min.Y,model.ModelBounds.Min.Z,model.ModelBounds.Max.X,model.ModelBounds.Max.Y,model.ModelBounds.Max.Z);
}
void Diagnostics(const ModelAsset& model) {
    for (const auto& missing:model.MissingDependencies) {ImGui::TextColored({1,.65f,.3f,1},"Missing: %s",Text(missing).c_str());}
    for (const auto& warning:model.Warnings) {ImGui::TextWrapped("Warning: %s",warning.c_str());}
}
}
void EditorUI::ImportPreview() {
    if (importQueue_.empty()) {return;}
    ImGui::OpenPopup("Import Preview");ImGui::SetNextWindowSize({720,600},ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import Preview",nullptr,ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%zu file(s) remaining",importQueue_.size());ImGui::TextWrapped("%s",Text(importQueue_.front().Path).c_str());
        ImGui::TextDisabled("Preview is CPU-only. The Scene/library changes only after Import.");
        bool changed=ImGui::Checkbox("Import Animation",&importSettings_.ImportAnimations);ImGui::SameLine();changed=ImGui::Checkbox("Import Material",&importSettings_.ImportMaterials)||changed;
        changed=ImGui::Checkbox("Generate Missing Normals",&importSettings_.GenerateMissingNormals)||changed;
        float rate=static_cast<float>(importSettings_.AnimationBakeRate);ImGui::SetNextItemWidth(170);
        if (ImGui::SliderFloat("FBX bake rate (Hz)",&rate,1,240,"%.0f")) {importSettings_.AnimationBakeRate=rate;changed=true;}
        auto textures=Text(importSettings_.TextureDirectory);ImGui::SetNextItemWidth(480);
        if (ImGui::InputTextWithHint("##texture-dir","Optional texture search directory",&textures)) {importSettings_.TextureDirectory=detail::PathFromUtf8(textures);changed=true;}
        ImGui::SameLine();if (ImGui::Button("Browse...")) {
            FilePickerRequest request;request.Mode=FilePickerMode::Folder;request.Directory=directory_;request.Title="Texture search directory";
            const auto picked=picker_ ? picker_(request):nativeFiles_.Choose(request);
            if (!picked) {importError_=picked.Error();}else if (picked.Value() && !picked.Value()->empty()) {importSettings_.TextureDirectory=picked.Value()->front();changed=true;}
        }
        if (changed) {importCandidate_.reset();importError_.reset();}
        if (ImGui::Button("Refresh Preview")) {RefreshImportPreview(importSettings_);}
        if (!projectRoot_.empty()) {ImGui::Checkbox("Copy source + dependencies into project assets",&copyImport_);if (!copyImport_) {ImGui::TextDisabled("External references: moving the project also requires moving source files.");}}
        else {ImGui::TextDisabled("External references (no project workspace). Quick Open also preserves the originals.");}
        ImGui::Separator();
        ImGui::BeginChild("Import result",{0,-45},ImGuiChildFlags_Borders);
        if (importError_) {ImGui::TextColored({1,.4f,.35f,1},"Import failed");ImGui::TextWrapped("%s\n%s",importError_->Message.c_str(),importError_->Hint.c_str());}
        else if (importCandidate_) {Summary(*importCandidate_);Diagnostics(*importCandidate_);}
        else {ImGui::TextWrapped("Settings changed. Refresh Preview before importing.");}
        ImGui::EndChild();
        ImGui::BeginDisabled(!importCandidate_ || importError_.has_value());
        if (ImGui::Button("Import",{120,0})) {if (ConfirmImportPreview() && importQueue_.empty()) {ImGui::CloseCurrentPopup();}}
        ImGui::EndDisabled();ImGui::SameLine();
        if (ImGui::Button("Skip file")) {AdvanceImportPreview();if (importQueue_.empty()) {ImGui::CloseCurrentPopup();}}
        ImGui::SameLine();if (ImGui::Button("Cancel all") || ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {CancelImportPreview();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
}
void EditorUI::AssetInspector() {
    if (!assetInspector_) {return;}
    if (ImGui::Begin("Asset Inspector",&assetInspector_)) {
        ModelHandle selected;
        for (const auto& model:Document.Assets()) {if (model->Id == selectedAsset_) {selected=model;break;}}
        if (!selected && Document.Selection()) {selected=Document.GetScene().GetObject(Document.Selection()).Model;}
        if (!selected) {ImGui::TextWrapped("Select an Imported asset or a model instance to inspect its source data.");}
        else {
            const auto& model=*selected;
            ImGui::TextWrapped("%s",Text(model.SourcePath).c_str());
            ImGui::Text("Asset ID: %llu | Import version: %u",static_cast<unsigned long long>(model.Id),model.ImportVersion);
            ImGui::Text("Content hash: %016llx",static_cast<unsigned long long>(model.ContentHash));
            ImGui::Text("Status: %s",model.Status == AssetImportStatus::Ready ? "Ready":"Ready with warnings");
            Summary(model);
            if (ImGui::CollapsingHeader("Bone influences")) {const auto stats=InspectBoneInfluences(model);ImGui::Text("Skinned / rigid vertices: %zu / %zu",stats.SkinnedVertices,stats.RigidVertices);ImGui::Text("Unweighted: %zu | weight sum: %.6f .. %.6f",stats.UnweightedVertices,stats.MinimumWeightSum,stats.MaximumWeightSum);for (unsigned n=0;n<=4;++n) {ImGui::Text("%u influences: %zu",n,stats.InfluenceCounts[n]);}}
            if (ImGui::Button("Reimport source")) {ReimportAsset(model.Id);}
            ImGui::SameLine();if (ImGui::Button("Open folder")) {Report(NativeFiles::OpenPath(model.SourcePath.parent_path()));}
            if (ImGui::CollapsingHeader("Import Settings")) {
                ImGui::Text("Animation: %s | Materials: %s | Generate normals: %s",model.Settings.ImportAnimations ? "on":"off",model.Settings.ImportMaterials ? "on":"off",model.Settings.GenerateMissingNormals ? "on":"off");
                ImGui::Text("FBX bake: %.0f Hz | Key budget: %zu",model.Settings.AnimationBakeRate,model.Settings.MaxAnimationSamples);
                ImGui::TextWrapped("Texture directory: %s",model.Settings.TextureDirectory.empty() ? "source directory":Text(model.Settings.TextureDirectory).c_str());
                ImGui::Text("Limits: %zu vertices / %zu indices / %zu nodes",model.Settings.MaxVertices,model.Settings.MaxIndices,model.Settings.MaxNodes);
            }
            if (ImGui::CollapsingHeader("Dependencies / Warnings")) {for (const auto& dependency:model.Dependencies) {ImGui::TextWrapped("%s",Text(dependency).c_str());}Diagnostics(model);}
            if (ImGui::CollapsingHeader("Meshes / Materials")) {
                for (std::size_t i=0;i<model.Meshes.size();++i) {const auto& mesh=model.Meshes[i];ImGui::PushID(static_cast<int>(i));if (ImGui::TreeNode("mesh","%s (%zu primitives)",mesh.Name.c_str(),mesh.Primitives.size())) {for (const auto& primitive:mesh.Primitives) {ImGui::Text("%zu vertices / %zu indices | material %zu | %s",primitive.Geometry.Vertices.size(),primitive.Geometry.Indices.size(),primitive.MaterialIndex,primitive.HasSkinning ? "skin":"rigid");}ImGui::TreePop();}ImGui::PopID();}
                for (std::size_t i=0;i<model.Materials.size();++i) {const auto& material=model.Materials[i];ImGui::Text("Material %zu: %s",i,material.Name.c_str());ImGui::Text("Color: %.2f %.2f %.2f %.2f | Base texture: %s | Normal: %s",material.BaseColor[0],material.BaseColor[1],material.BaseColor[2],material.BaseColor[3],material.BaseColorTexture ? "yes":"none",material.NormalTexture ? "yes":"none");}
            }
            if (ImGui::CollapsingHeader("Node Hierarchy / Animation")) {
                for (std::size_t i=0;i<model.Nodes.size();++i) {const auto& node=model.Nodes[i];ImGui::Text("%zu: %s | parent %s | %s",i,node.Name.c_str(),node.Parent ? std::to_string(*node.Parent).c_str():"root",model.Skeleton.Joints[i].IsJoint ? "bone":"node");}
                for (const auto& clip:model.Animations) {ImGui::Text("%s: %.3f seconds / %zu tracks",clip.Name.c_str(),clip.Duration,clip.Tracks.size());}
            }
            if (ImGui::CollapsingHeader("Textures / Images")) {for (std::size_t i=0;i<model.Textures.size();++i) {const auto& texture=model.Textures[i];const auto& image=model.Images[texture.ImageIndex];ImGui::Text("Texture %zu -> image %zu: %d x %d / %zu bytes",i,texture.ImageIndex,image.Width,image.Height,image.Rgba.size());}}
            if (ImGui::CollapsingHeader("Material texture connections")) {for (std::size_t i=0;i<model.Materials.size();++i) {const auto& m=model.Materials[i];ImGui::Text("Material %zu: %s",i,m.Name.c_str());const auto connection=[&](const char* role,std::optional<std::size_t> index) {if (index) {ImGui::Text("%s -> texture %zu -> image %zu [connected]",role,*index,model.Textures.at(*index).ImageIndex);}else {ImGui::Text("%s: none / fallback",role);}};connection("Base Color (sRGB)",m.BaseColorTexture);connection("Normal (linear, Advanced)",m.NormalTexture);}}
        }
    }ImGui::End();
}
} // namespace lv::editor
