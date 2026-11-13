#pragma once
#include <minvex/Asset.h>
#include <algorithm>

// A second mesh export with reversed transform-node and skin-palette orders.
inline lv::ModelHandle EquipmentFollower(const lv::ModelHandle& source) {
    auto result = std::make_shared<lv::ModelAsset>(*source);
    const auto remap = [&](lv::JointId node) { return node == lv::NoJoint ? node : static_cast<lv::JointId>(source->Skeleton.Joints.size() - 1 - node); };
    std::reverse(result->Skeleton.Joints.begin(), result->Skeleton.Joints.end());
    for (auto& joint : result->Skeleton.Joints) { joint.Parent = remap(joint.Parent); }
    result->Skeleton.Signature = lv::SkeletonSignature(result->Skeleton);
    std::reverse(result->Nodes.begin(), result->Nodes.end());
    for (auto& node : result->Nodes) { if (node.Parent) { node.Parent = remap(static_cast<lv::JointId>(*node.Parent)); } }
    for (auto& part : result->Parts) { part.Node = remap(part.Node); }
    for (auto& skin : result->Skins) {
        for (auto& joint : skin.Joints) { joint = remap(joint); }
        std::reverse(skin.Joints.begin(), skin.Joints.end()); std::reverse(skin.InverseBindMatrices.begin(), skin.InverseBindMatrices.end());
    }
    for (auto& mesh : result->Meshes) { for (auto& primitive : mesh.Primitives) {
        if (!primitive.HasSkinning) { continue; }
        for (auto& vertex : primitive.Geometry.Vertices) {
            vertex.Position[0] *= 1.15f;
            for (auto& joint : vertex.Joints) { joint = static_cast<std::uint16_t>(result->Skins[0].Joints.size() - 1 - joint); }
        }
    } }
    result->Animations.clear(); result->SourcePath = "modular-clothing.gltf";
    return result;
}
