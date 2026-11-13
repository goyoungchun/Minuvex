#include <minvex/Viewer.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace {
void Cube(lv::MeshData& mesh, lv::Vec3 center, lv::Vec3 size, std::uint16_t bone = 0) {
    auto cube = lv::MakeCube(); const auto offset = static_cast<std::uint32_t>(mesh.Vertices.size());
    for (auto vertex : cube.Vertices) {
        vertex.Position = {center.X + vertex.Position[0] * size.X, center.Y + vertex.Position[1] * size.Y, center.Z + vertex.Position[2] * size.Z};
        vertex.Joints = {bone, 0, 0, 0}; mesh.Vertices.push_back(vertex);
    }
    for (auto index : cube.Indices) { mesh.Indices.push_back(offset + index); }
}
lv::Bounds Bounds(const lv::MeshData& mesh) {
    const auto& first = mesh.Vertices.at(0).Position; lv::Bounds bounds{{first[0], first[1], first[2]}, {first[0], first[1], first[2]}};
    for (const auto& vertex : mesh.Vertices) {
        const auto& p = vertex.Position;
        bounds.Min = {std::min(bounds.Min.X, p[0]), std::min(bounds.Min.Y, p[1]), std::min(bounds.Min.Z, p[2])};
        bounds.Max = {std::max(bounds.Max.X, p[0]), std::max(bounds.Max.Y, p[1]), std::max(bounds.Max.Z, p[2])};
    }
    return bounds;
}
lv::ModelHandle Rigid(std::string name, lv::MeshData geometry, std::array<float, 4> color) {
    auto model = std::make_shared<lv::ModelAsset>(); model->SourcePath = name + ".custom"; model->ModelBounds = Bounds(geometry);
    model->Meshes.push_back({name, {{std::move(geometry)}}}); model->Parts.push_back({0}); model->Materials.push_back({});
    model->Materials[0].BaseColor = color; lv::ValidateModel(*model); return model;
}
lv::ModelHandle Character(bool clothes) {
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
            lv::AnimationTrack track; track.Joint = joint; track.Path = lv::AnimationPath::Rotation; track.Times = {0, 1, 2};
            track.Values = {{0,0,std::sin(angle),std::cos(angle)}, {0,0,-std::sin(angle),std::cos(angle)}, {0,0,std::sin(angle),std::cos(angle)}}; clip.Tracks.push_back(track);
        }
        lv::AnimationTrack bounce; bounce.Joint = 0; bounce.Times = {0,1,2}; bounce.Values = {{0,1,0,0},{0,1.1f,0,0},{0,1,0,0}}; clip.Tracks.push_back(bounce);
        model->Animations.push_back(std::move(clip));
    }
    lv::ValidateModel(*model); return model;
}
}
int main(int argc, char** argv) {
    try {
        lv::ViewerRunOptions run; bool exercise = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--frames" && i + 1 < argc) {
                const std::string_view number = argv[++i]; const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), run.FrameLimit);
                if (error != std::errc{} || end != number.data() + number.size()) { return 2; }
            } else if (argument == "--exercise") { exercise = true; }
            else { std::puts("Usage: minvex_equipment [--frames N] [--exercise]\nSpace: cycle gear; Shift+Space: detach/attach weapon"); return argument == "--help" ? 0 : 2; }
        }
        lv::ViewerOptions options; options.AutoFrame = false; options.Title = "Minvex Equipment | Space: gear | Shift+Space: detach weapon";
        options.Quality = lv::MakeQualityProfile(lv::QualityPreset::Medium); options.Quality.AllowFallback = true;
        lv::Viewer viewer(options); auto& scene = viewer.GetScene();
        const auto bodyModel = Character(false), clothingModel = Character(true);
        lv::SkeletonSocket hand{"Weapon", "RightHand"}; hand.LocalTransform.Position = {0, -.25f, .1f};
        lv::SkeletonSocket head{"Hat", "Head"}; head.LocalTransform.Position.Y = .28f;
        const auto shared = lv::CreateSharedSkeleton(bodyModel->Skeleton, {hand, head});
        const auto body = scene.AddModel(bodyModel), clothes = scene.AddModel(clothingModel, {}, body);
        scene.SetSharedSkeleton(body, shared); scene.SetSharedSkeleton(clothes, shared); scene.SetLeaderPose(clothes, body); scene.GetAnimation(body).Play(0);
        lv::MeshData hatMesh; Cube(hatMesh, {}, {.65f,.08f,.6f}); Cube(hatMesh, {0,.15f,0}, {.45f,.25f,.45f});
        const auto hatNode = scene.CreateAttachmentNode(body, "Hat"); scene.AddModel(Rigid("Hat", std::move(hatMesh), {.95f,.65f,.08f,1}), {}, hatNode);
        lv::MeshData sword; Cube(sword, {0,-.35f,0}, {.08f,.85f,.08f}); Cube(sword, {0,.02f,0}, {.35f,.06f,.12f});
        const auto weaponNode = scene.CreateAttachmentNode(clothes, "Weapon"); scene.AddModel(Rigid("Weapon", std::move(sword), {.75f,.82f,.9f,1}), {}, weaponNode);
        lv::MeshData ground; Cube(ground, {0,-.07f,0}, {5,.1f,4}); scene.AddModel(Rigid("Ground", std::move(ground), {.15f,.18f,.23f,1}));
        scene.MainCamera.Position = {3, 2.5f, 5}; scene.MainCamera.Target = {0, 1.1f, 0};
        bool detached = false; unsigned gear = 0;
        const auto detach = [&] { if (detached) { scene.AttachToSocket(weaponNode, clothes, "Weapon"); } else { scene.DetachFromSocket(weaponNode); } detached = !detached; };
        const auto cycle = [&] { gear = (gear + 1) % 4; scene.SetVisible(clothes, gear != 1); scene.SetVisible(hatNode, gear != 2); scene.SetVisible(weaponNode, gear != 3); };
        run.OnInput = [&](lv::Viewer&, const lv::ViewerInputEvent& event) {
            if (event.Type != lv::ViewerInputType::KeyDown || event.Key != lv::ViewerKey::Space || event.Repeat) { return false; }
            if (event.Shift) { detach(); } else { cycle(); } return true;
        };
        if (exercise) { run.OnFrame = [&](lv::Viewer&, const lv::ViewerFrameInfo& frame) { if (frame.FrameNumber == 1 || frame.FrameNumber == 3) { detach(); } if (frame.FrameNumber == 4 || frame.FrameNumber == 5) { cycle(); } }; }
        const auto result = viewer.Run(run); if (!result) { std::fprintf(stderr, "%s\n", result.Error().Message.c_str()); return 1; }
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
