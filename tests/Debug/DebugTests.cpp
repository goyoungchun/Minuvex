#include "TestHarness.h"
#include <minuvex/Viewer.h>
#include <thread>
#include <limits>
using namespace test;

void TestDebugContracts() {
    lv::DebugDraw draw;draw.DrawBox({{-1,-1,-1},{1,1,1}},{0,1,0,1},lv::Mat4::Translation({2,0,0}));
    Check(draw.Lines().size() == 12 && draw.Lines()[0].Start.X == 1,"debug box transforms all twelve edges to world space");
    const auto before=draw.Lines().size();Rejects([&]{draw.DrawSphere({},-1);},"negative debug radius rejected");Rejects([&]{draw.DrawLine({},{},{1,0,std::numeric_limits<float>::infinity(),1});},"invalid color rejected");
    Rejects([&]{draw.DrawBox({{1,0,0},{0,1,1}});},"unordered debug bounds rejected");Check(draw.Lines().size() == before,"invalid shape leaves command list intact");
    draw.Clear();draw.DrawSphere({},2);Check(draw.Lines().size() == 72 && Near(draw.Lines()[0].Start.Y,2),"sphere uses three closed great-circle rings");
    draw.Clear();draw.DrawCapsule({0,-1,0},{0,1,0},.5f);bool finite=true,upper=false,lower=false;for (const auto& l:draw.Lines()) {finite&=std::isfinite(l.Start.X) && std::isfinite(l.Start.Y) && std::isfinite(l.Start.Z);upper|=l.Start.Y > 1.4f;lower|=l.Start.Y < -1.4f;}Check(finite && upper && lower,"capsule caps extend beyond both endpoints");
    draw.Clear();draw.DrawCapsule({},{},1);Check(draw.Lines().size() == 72,"zero-length capsule reduces to a sphere");
    draw.Clear();draw.DrawArrow({},{1,0,0});draw.DrawAxis(lv::Mat4::Identity());Check(draw.Lines().size() == 8,"arrow head and RGB axis generate the expected edge set");
    draw.Clear();lv::Camera camera;camera.Position={0,0,0};camera.Target={0,0,-1};camera.NearPlane=1;camera.FarPlane=2;draw.DrawCameraFrustum(camera,2);bool depths=true;for (const auto& l:draw.Lines()) {depths&=Near(l.Start.Z,-1) || Near(l.Start.Z,-2);}Check(draw.Lines().size() == 12 && depths,"camera frustum follows the camera near/far planes");
    draw.Clear();for (std::size_t i=0;i<lv::MaxDebugLines;++i) {draw.DrawLine({},{1,0,0});}bool limited=false;try {draw.DrawBox({{0,0,0},{1,1,1}});}catch (const std::length_error&) {limited=true;}Check(limited,"debug geometry budget is bounded with length_error");Check(draw.Lines().size() == lv::MaxDebugLines,"budget rejection does not partially append a shape");
    lv::AssetRegistry registry;const auto model=registry.LoadModel(Fixture("AnimatedRibbon.gltf"));lv::AnimationPlayer player;player.Bind(model);player.Play(0);player.Seek(.5);
    const auto time=player.Time();const auto state=player.State();const auto pose=player.Evaluate();draw.Clear();draw.DrawSkeleton(model->Skeleton,pose);Check(draw.Lines().size() == 3,"skeleton preserves intermediary node hierarchy");
    auto shortPose=pose;shortPose.Global.clear();Rejects([&]{draw.DrawSkeleton(model->Skeleton,shortPose);},"unrelated debug pose rejected");
    const auto info=lv::InspectJoint(player,1);Check(info.SkinningMatrices.size() == 1 && !Near(info.Global.Elements[12],info.ReferenceGlobal.Elements[12]),"joint debugger exposes current/reference and skin palette matrices");
    Check(player.Time() == time && player.State() == state && player.Overrides().Joints.empty(),"joint inspection does not mutate animation or pose overrides");
    const auto weights=lv::InspectBoneInfluences(*model);std::size_t classified=0;for (auto count:weights.InfluenceCounts) {classified+=count;}Check(weights.SkinnedVertices > 0 && classified == weights.SkinnedVertices && !weights.UnweightedVertices && std::abs(weights.MinimumWeightSum-1)<.00001 && std::abs(weights.MaximumWeightSum-1)<.00001,"bone influence histogram agrees with normalized imported vertices");
    lv::Viewer viewer;Check(viewer.SetDebugView(lv::DebugView::Wireframe) && viewer.SetShowSkeleton(true) && viewer.GetDebugOptions().Value().ShowSkeleton,"Viewer stages diagnostic settings without a GPU");
    const auto options=viewer.GetDebugOptions().Value();Check(!viewer.SetDebugView(static_cast<lv::DebugView>(99)) && viewer.GetDebugOptions().Value() == options,"invalid debug request retains previous settings");
    Check(viewer.SetDebugDraw(draw).HasValue() && !viewer.GetRenderDebugInfo(),"CPU geometry submission is separate from active GPU inspection");
    bool wrong=false;std::thread thread([&]{const auto r=viewer.SetShowSkeleton(false);wrong=!r && r.Error().Code == lv::ErrorCode::WrongThread;});thread.join();Check(wrong,"debug facade enforces owner thread");
}
