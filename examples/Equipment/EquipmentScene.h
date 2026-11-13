#pragma once
#include <minvex/Scene.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace equipment {
inline void Cube(lv::MeshData& mesh, lv::Vec3 center, lv::Vec3 size, std::uint16_t bone = 0) {
    auto cube = lv::MakeCube(); const auto offset = static_cast<std::uint32_t>(mesh.Vertices.size());
    for (auto vertex : cube.Vertices) {
        vertex.Position = {center.X + vertex.Position[0] * size.X, center.Y + vertex.Position[1] * size.Y, center.Z + vertex.Position[2] * size.Z};
        vertex.Joints = {bone, 0, 0, 0}; mesh.Vertices.push_back(vertex);
    }
    for (auto index : cube.Indices) { mesh.Indices.push_back(offset + index); }
}
inline lv::Bounds Bounds(const lv::MeshData& mesh) {
    const auto& first = mesh.Vertices.at(0).Position; lv::Bounds bounds{{first[0], first[1], first[2]}, {first[0], first[1], first[2]}};
    for (const auto& vertex : mesh.Vertices) {
        const auto& p = vertex.Position;
        bounds.Min = {std::min(bounds.Min.X, p[0]), std::min(bounds.Min.Y, p[1]), std::min(bounds.Min.Z, p[2])};
        bounds.Max = {std::max(bounds.Max.X, p[0]), std::max(bounds.Max.Y, p[1]), std::max(bounds.Max.Z, p[2])};
    }
    return bounds;
}
inline lv::ModelHandle Rigid(std::string name, lv::MeshData geometry, std::array<float, 4> color) {
    auto model = std::make_shared<lv::ModelAsset>(); model->SourcePath = name + ".custom"; model->ModelBounds = Bounds(geometry);
    model->Meshes.push_back({name, {{std::move(geometry)}}}); model->Parts.push_back({0}); model->Materials.push_back({});
    model->Materials[0].BaseColor = color; lv::ValidateModel(*model); return model;
}
inline lv::ModelHandle Character(bool clothes) {
    auto model = std::make_shared<lv::ModelAsset>(); const std::string name = clothes ? "Coat" : "Body"; model->SourcePath = name + ".custom";
    const auto bone = [&](std::string label, lv::JointId parent, lv::Vec3 position) {
        lv::SkeletonJoint joint; joint.Name = std::move(label); joint.Parent = parent; joint.IsJoint = true; joint.ReferenceLocal.Position = position;
        joint.ReferenceMatrix = joint.ReferenceLocal.Matrix(); model->Skeleton.Joints.push_back(joint);
        lv::ModelNode node; node.Name = joint.Name; if (parent != lv::NoJoint) { node.Parent = parent; } node.LocalTransform = joint.ReferenceMatrix; model->Nodes.push_back(node);
    };
    bone("Pelvis", lv::NoJoint, {0, 1, 0}); bone("Chest", 0, {0, .55f, 0}); bone("Head", 1, {0, .55f, 0});
    bone("LeftHand", 1, {-.65f, -.1f, 0}); bone("RightHand", 1, {.65f, -.1f, 0});
    bone("LeftLeg", 0, {-.2f, -.55f, 0}); bone("RightLeg", 0, {.2f, -.55f, 0});
    const auto rest = lv::ComposePose(model->Skeleton, lv::ReferencePoseBuffer(model->Skeleton));
    lv::SkinAsset skin; skin.Name = name;
    for (std::size_t i = 0; i < model->Skeleton.Joints.size(); ++i) { skin.Joints.push_back(static_cast<lv::JointId>(i)); skin.InverseBindMatrices.push_back(rest.Global[i].InverseAffine()); }
    lv::MeshData mesh;
    if (clothes) {
        Cube(mesh, {0, 1.55f, 0}, {.68f, .75f, .43f}, 1); Cube(mesh, {0, 1, 0}, {.64f, .3f, .42f});
        Cube(mesh, {-.65f, 1.55f, 0}, {.27f, .3f, .4f}, 3); Cube(mesh, {.65f, 1.55f, 0}, {.27f, .3f, .4f}, 4);
    } else {
        Cube(mesh, {0, 1, 0}, {.55f, .3f, .35f}); Cube(mesh, {0, 1.55f, 0}, {.58f, .7f, .35f}, 1);
        Cube(mesh, {0, 2.1f, 0}, {.4f, .4f, .4f}, 2);
        Cube(mesh, {-.65f, 1.35f, 0}, {.22f, .7f, .3f}, 3); Cube(mesh, {.65f, 1.35f, 0}, {.22f, .7f, .3f}, 4);
        Cube(mesh, {-.2f, .45f, 0}, {.28f, .8f, .32f}, 5); Cube(mesh, {.2f, .45f, 0}, {.28f, .8f, .32f}, 6);
    }
    model->ModelBounds = Bounds(mesh); model->Meshes.push_back({name, {{std::move(mesh), 0, true}}});
    lv::ModelNode node; node.Name = name + "Mesh"; node.MeshIndex = 0; node.SkinIndex = 0; model->Nodes.push_back(node);
    model->Skeleton.Joints.push_back({node.Name}); model->Skeleton.Signature = lv::SkeletonSignature(model->Skeleton);
    model->Parts.push_back({0, lv::Mat4::Identity(), 7, 0}); model->Skins.push_back(std::move(skin));
    model->Materials.push_back({}); model->Materials[0].BaseColor = clothes ? std::array<float,4>{.05f,.55f,.65f,1} : std::array<float,4>{.8f,.55f,.35f,1};
    if (!clothes) {
        lv::AnimationClip clip; clip.Name = "Equipment motion"; clip.Duration = 2;
        for (lv::JointId joint : {3u, 4u}) {
            const float angle = joint == 3 ? -.35f : .5f;
            lv::AnimationTrack track; track.Joint = joint; track.Path = lv::AnimationPath::Rotation;
            // Bake a cosine swing at 60 Hz: ease to zero velocity at each turn
            // and the loop seam, while retaining the SDK's standard Slerp path.
            for (int sample = 0; sample <= 120; ++sample) {
                const float halfAngle = angle * static_cast<float>(std::cos(sample * std::numbers::pi / 60));
                track.Times.push_back(sample / 60.f); track.Values.push_back({0, 0, std::sin(halfAngle), std::cos(halfAngle)});
            }
            clip.Tracks.push_back(std::move(track));
        }
        lv::AnimationTrack bounce; bounce.Joint = 0;
        for (int sample = 0; sample <= 120; ++sample) {
            const float height = 1 + .05f * (1 - static_cast<float>(std::cos(sample * std::numbers::pi / 60)));
            bounce.Times.push_back(sample / 60.f); bounce.Values.push_back({0, height, 0, 0});
        }
        clip.Tracks.push_back(std::move(bounce));
        model->Animations.push_back(std::move(clip));
    }
    lv::ValidateModel(*model); return model;
}
}
