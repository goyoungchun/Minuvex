#include "EditorUI.h"
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

namespace lv::editor {
namespace {
std::vector<SkeletonSocket> Sockets(const SceneObject& object) {
    auto sockets = object.Sockets;
    if (object.SharedSkeleton) {
        for (const auto& socket : object.SharedSkeleton->Sockets()) {
            if (std::none_of(sockets.begin(), sockets.end(), [&](const auto& s) { return s.Name == socket.Name; })) { sockets.push_back(socket); }
        }
    }
    return sockets;
}
bool SocketTransform(Transform& transform) {
    float position[]{transform.Position.X, transform.Position.Y, transform.Position.Z};
    float rotation[]{transform.Rotation.X, transform.Rotation.Y, transform.Rotation.Z, transform.Rotation.W};
    float scale[]{transform.Scale.X, transform.Scale.Y, transform.Scale.Z};
    bool changed = ImGui::DragFloat3("Socket position", position, .01f);
    changed = ImGui::DragFloat4("Socket rotation (xyzw)", rotation, .01f) || changed;
    changed = ImGui::DragFloat3("Socket scale", scale, .01f) || changed;
    transform = {{position[0], position[1], position[2]}, {rotation[0], rotation[1], rotation[2], rotation[3]}, {scale[0], scale[1], scale[2]}};
    return changed;
}
}
void EditorUI::EquipmentControls(ObjectId id) {
    if (!ImGui::CollapsingHeader("Skeleton / Equipment")) { return; }
    const auto& scene = Document.GetScene(); const auto object = scene.GetObject(id);
    if (equipmentObject_ != id) {
        equipmentObject_ = id; socketParent_ = object.Parent; attachmentDraft_ = object.AttachmentSocket;
        socketDraft_ = {"Socket", ""};
        if (object.Model && !object.Model->Skeleton.Joints.empty()) { socketDraft_.Joint = object.Model->Skeleton.Joints.front().Name; }
    }
    if (object.Model) {
        if (!object.SharedSkeleton) {
            if (ImGui::Button("Create Shared Skeleton")) {
                try { Report(Document.SetSharedSkeleton(id, lv::CreateSharedSkeleton(object.Model->Skeleton))); }
                catch (const std::exception& error) { error_ = error.what(); }
            }
        } else {
            ImGui::TextUnformatted("Shared Skeleton assigned");
            if (ImGui::Button("Clear Shared Skeleton")) { Report(Document.SetSharedSkeleton(id, {})); }
        }
        const auto label = object.LeaderPose ? scene.GetObject(object.LeaderPose).Name : "<independent>";
        if (ImGui::BeginCombo("Leader Pose", label.c_str())) {
            if (ImGui::Selectable("<independent>", !object.LeaderPose)) { Report(Document.SetLeaderPose(id, NoObject)); }
            for (auto candidate : scene.ObjectIds()) {
                const auto source = scene.GetObject(candidate);
                if (candidate == id || !source.SharedSkeleton) { continue; }
                ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(candidate)));
                if (ImGui::Selectable(source.Name.c_str(), object.LeaderPose == candidate)) {
                    auto result = Document.BeginEdit();
                    if (result) { result = Document.SetLeaderPose(id, NoObject); }
                    if (result) { result = Document.SetSharedSkeleton(id, source.SharedSkeleton); }
                    if (result) { result = Document.SetLeaderPose(id, candidate); }
                    if (result) { result = Document.CommitEdit(); }
                    Report(result);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Leader Pose shares bone motion. Use Parent to follow the body transform. The independent player stays dormant while following.");
        for (auto socket : Sockets(object)) {
            ImGui::PushID(socket.Name.c_str());
            if (ImGui::TreeNode("Socket", "%s (%s)", socket.Name.c_str(), socket.Joint.c_str())) {
                if (SocketTransform(socket.LocalTransform)) { Report(Document.SetSocket(id, socket)); }
                if (ImGui::Button("Create attachment node")) { Report(Document.CreateAttachmentNode(id, socket.Name, {}, socket.Name + " attachment")); }
                const bool overridden = std::any_of(object.Sockets.begin(), object.Sockets.end(), [&](const auto& s) { return s.Name == socket.Name; });
                if (overridden && ImGui::Button("Remove socket / override")) { Report(Document.RemoveSocket(id, socket.Name)); }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::InputText("New socket name", &socketDraft_.Name);
        if (ImGui::BeginCombo("Socket joint", socketDraft_.Joint.c_str())) {
            for (std::size_t i = 0; i < object.Model->Skeleton.Joints.size(); ++i) {
                const auto& joint = object.Model->Skeleton.Joints[i]; ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(joint.Name.c_str(), socketDraft_.Joint == joint.Name)) { socketDraft_.Joint = joint.Name; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Add socket")) { Report(Document.SetSocket(id, socketDraft_)); }
    }
    if (!object.AttachmentSocket.empty()) {
        ImGui::Text("Attached: %s", object.AttachmentSocket.c_str());
        if (ImGui::Button("Detach (keep world)")) { Report(Document.DetachFromSocket(id)); }
    }
    if (!scene.Contains(socketParent_)) { socketParent_ = NoObject; }
    const auto parentLabel = socketParent_ ? scene.GetObject(socketParent_).Name : "<choose object>";
    if (ImGui::BeginCombo("Socket owner", parentLabel.c_str())) {
        for (auto candidate : scene.ObjectIds()) {
            const auto owner = scene.GetObject(candidate);
            if (candidate == id || Sockets(owner).empty()) { continue; }
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(candidate)));
            if (ImGui::Selectable(owner.Name.c_str(), socketParent_ == candidate)) { socketParent_ = candidate; attachmentDraft_.clear(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (socketParent_ && ImGui::BeginCombo("Attach socket", attachmentDraft_.c_str())) {
        for (const auto& socket : Sockets(scene.GetObject(socketParent_))) {
            if (ImGui::Selectable(socket.Name.c_str(), attachmentDraft_ == socket.Name)) { attachmentDraft_ = socket.Name; }
        }
        ImGui::EndCombo();
    }
    if (socketParent_ && !attachmentDraft_.empty() && ImGui::Button("Attach (snap to socket)")) { Report(Document.AttachToSocket(id, socketParent_, attachmentDraft_)); }
}
} // namespace lv::editor
