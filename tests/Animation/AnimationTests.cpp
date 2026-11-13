#include <minvex/EditorDocument.h>
#include <minvex/Viewer.h>
#include "TestHarness.h"
#include <json.hpp>
#include <limits>
#include <numbers>

using namespace test;
using Json = nlohmann::json;
void TestAdditive();
namespace {
void MatrixNear(const lv::Mat4& a, const lv::Mat4& b, const char* label) {
    bool near = true; for (std::size_t i=0;i<16;++i) { near &= Near(a.Elements[i],b.Elements[i]); } Check(near,label);
}
void PoseBlendingCore() {
    using C = lv::PoseChannels;
    lv::SkeletonAsset skeleton; skeleton.Joints.resize(3);
    skeleton.Joints[0].Name="Child";skeleton.Joints[0].Parent=2;skeleton.Joints[0].ReferenceLocal.Position.Y=2;
    skeleton.Joints[1].Name="Shear";skeleton.Joints[1].Editable=false;skeleton.Joints[1].ReferenceMatrix.Elements[4]=.5f;
    skeleton.Joints[2].Name="Root";skeleton.Joints[2].ReferenceLocal.Position.X=1;
    lv::AnimationClip first;first.Duration=2;
    first.Tracks={{2,lv::AnimationPath::Translation,lv::AnimationInterpolation::Linear,{0,2},{{1,0,0,0},{5,0,0,0}}},
        {0,lv::AnimationPath::Rotation,lv::AnimationInterpolation::Linear,{0,2},{{0,0,0,1},{0,0,1,0}}}};
    lv::AnimationClip second;second.Duration=2;
    second.Tracks={{2,lv::AnimationPath::Translation,lv::AnimationInterpolation::Step,{0,1,2},{{9,0,0,0},{11,0,0,0},{13,0,0,0}}},
        {0,lv::AnimationPath::Scale,lv::AnimationInterpolation::Linear,{0,2},{{1,1,1,0},{3,3,3,0}}}};
    const auto a=lv::EvaluateAnimation(skeleton,first,1),b=lv::EvaluateAnimation(skeleton,second,.5);
    Check(a.Channels==std::vector<C>{C::Rotation,C::None,C::Translation} && b.Channels[0]==C::Scale,"stateless evaluation reports authored channels including sparse joints");
    Check(Near(a.LocalTransforms[2].Position.X,3) && Near(a.LocalTransforms[0].Rotation.Z,std::sqrt(.5f)) && Near(b.LocalTransforms[2].Position.X,9),"sampler handles independent clip times, linear TRS and STEP keys");
    const auto mixed=lv::BlendPoses(skeleton,a,b,.5f);const auto& child=mixed.LocalTransforms[0];
    Check(Near(mixed.LocalTransforms[2].Position.X,6) && Near(child.Scale.X,1.25f) && Near(child.Rotation.Z,std::sin(std::numbers::pi_v<float>/8)),"two-pose blend uses reference values for absent channels and shortest-arc rotation");
    Check(mixed.Channels[0]==(C::Rotation|C::Scale) && mixed.Channels[1]==C::None,"blended channel provenance is the union of selected authored channels");
    const auto zero=lv::ComposePose(skeleton,lv::BlendPoses(skeleton,a,b,0)),one=lv::ComposePose(skeleton,lv::BlendPoses(skeleton,a,b,1));
    const auto ca=lv::ComposePose(skeleton,a),cb=lv::ComposePose(skeleton,b);
    for (std::size_t i=0;i<3;++i) {MatrixNear(zero.Global[i],ca.Global[i],"weight zero matches source pose");MatrixNear(one.Global[i],cb.Global[i],"weight one matches target pose");}
    MatrixNear(ca.Global[0],ca.Local[2]*ca.Local[0],"composer supports parents after children in storage order");
    MatrixNear(lv::ComposePose(skeleton,mixed).Local[1],skeleton.Joints[1].ReferenceMatrix,"read-only shear matrix survives sampling and blending");
    const std::array<C,3> mask{C::Rotation,C::None,C::None};const auto masked=lv::BlendPoses(skeleton,a,b,.5f,mask);
    Check(Near(masked.LocalTransforms[2].Position.X,3) && Near(masked.LocalTransforms[0].Scale.X,1) && masked.Channels[0]==C::Rotation,"per-node channel mask retains unselected source values and provenance");
    auto absent=lv::ReferencePoseBuffer(skeleton);absent.LocalTransforms[0].Position.X=std::numeric_limits<float>::quiet_NaN();
    Check(Near(lv::ComposePose(skeleton,absent).Local[0].Elements[12],0),"inactive channel storage is ignored and resolved from the reference pose");
    auto local=skeleton.Joints[0].ReferenceLocal;local.Position.X=7;
    const lv::PoseAsset overridePose{"Override",lv::SkeletonSignature(skeleton),{{0,local}}};
    const auto overridden=lv::ApplyPoseOverrides(skeleton,mixed,overridePose);
    Check(overridden.Channels[0]==C::All && Near(overridden.LocalTransforms[0].Position.X,7) && Near(overridden.LocalTransforms[0].Scale.X,1) && Near(mixed.LocalTransforms[0].Scale.X,1.25f),"manual full-TRS overrides apply last without changing input buffers");
    auto antipodal=a;auto& rotation=antipodal.LocalTransforms[0].Rotation;rotation={-rotation.X,-rotation.Y,-rotation.Z,-rotation.W};
    MatrixNear(lv::ComposePose(skeleton,lv::BlendPoses(skeleton,a,antipodal,.5f)).Local[0],ca.Local[0],"antipodal quaternion blending preserves orientation");
    Check(Near(lv::EvaluateAnimation(skeleton,first,100).LocalTransforms[2].Position.X,5),"standalone sampling clamps beyond the endpoint without looping");
    auto invalid=b;invalid.SkeletonSignature^=1;Rejects([&]{lv::BlendPoses(skeleton,a,invalid,.5f);},"foreign pose signature rejected");
    auto changed=skeleton;changed.Joints[2].Name="Other";changed.Signature=a.SkeletonSignature;Rejects([&]{lv::ComposePose(changed,a);},"compatibility checks actual skeleton values rather than a stale cached signature");
    invalid=b;invalid.Channels.pop_back();Rejects([&]{lv::ComposePose(skeleton,invalid);},"incomplete dense channel layout rejected");
    invalid=b;invalid.LocalTransforms.pop_back();Rejects([&]{lv::BlendPoses(skeleton,a,invalid,.5f);},"incomplete transform layout rejected");
    invalid=b;invalid.Channels[0]=static_cast<C>(8);Rejects([&]{lv::ValidatePoseBuffer(skeleton,invalid);},"unknown channel bits rejected");
    invalid=b;invalid.Channels[1]=C::Translation;Rejects([&]{lv::ComposePose(skeleton,invalid);},"matrix-authored read-only node cannot be overridden");
    invalid=b;invalid.Channels[0]=C::Rotation;invalid.LocalTransforms[0].Rotation={0,0,0,0};Rejects([&]{lv::ComposePose(skeleton,invalid);},"active zero quaternion rejected");
    invalid=b;invalid.LocalTransforms[0].Scale.X=0;Rejects([&]{lv::BlendPoses(skeleton,a,invalid,.5f);},"singular active scale rejected");
    invalid=b;invalid.LocalTransforms[0].Scale.X=-1;Rejects([&]{lv::BlendPoses(skeleton,a,invalid,.5f);},"opposite scale signs cannot blend through zero");
    Check(Near(lv::BlendPoses(skeleton,a,invalid,.5f,mask).LocalTransforms[0].Scale.X,1),"unselected scale channel does not impose a blend sign constraint");
    Rejects([&]{lv::BlendPoses(skeleton,a,b,std::numeric_limits<float>::quiet_NaN());},"nonfinite blend weight rejected");
    Rejects([&]{lv::BlendPoses(skeleton,a,b,1.1f);},"out-of-range blend weight rejected");
    Rejects([&]{lv::BlendPoses(skeleton,a,b,.5f,std::span(mask).first(2));},"incorrect blend mask size rejected");
    auto badMask=mask;badMask[1]=static_cast<C>(8);Rejects([&]{lv::BlendPoses(skeleton,a,b,.5f,badMask);},"unknown mask bits rejected even on read-only nodes");
    Rejects([&]{lv::EvaluateAnimation(skeleton,first,-1);},"negative stateless sample time rejected");
    Rejects([&]{lv::EvaluateAnimation(skeleton,first,std::numeric_limits<double>::infinity());},"nonfinite stateless sample time rejected");
    changed=skeleton;changed.Joints[2].Parent=0;Rejects([&]{lv::ComposePose(changed,a);},"composer rejects cyclic skeletons before hierarchy traversal");
    Check(lv::ComposePose({},lv::ReferencePoseBuffer({})).Global.empty(),"empty reference skeleton composes safely");
    lv::AssetRegistry assets;const auto model=assets.LoadModel(Fixture("AnimatedRibbon.gltf"));lv::AnimationPlayer player;player.Bind(model);player.Play(0);player.Seek(.5);player.CrossFade(1,1);player.Update(.25);
    const auto direct=lv::ComposePose(model->Skeleton,lv::BlendPoses(model->Skeleton,lv::EvaluateAnimation(model->Skeleton,model->Animations[0],.75),lv::EvaluateAnimation(model->Skeleton,model->Animations[1],.25),.25f));
    MatrixNear(player.Evaluate().Global[2],direct.Global[2],"AnimationPlayer cross-fade uses the independent evaluator and composer");
    Check(player.EvaluatePose().SkeletonSignature==lv::SkeletonSignature(model->Skeleton),"Player exposes its current local pose buffer without changing playback");
}
void DataAndPlayer() {
    lv::AssetRegistry registry; auto model = registry.LoadModel(Fixture("AnimatedRibbon.gltf"));
    Check(model->ImportVersion == 6 && model->Skins.size() == 1 && model->Animations.size() == 4,"skin and four clips imported");
    Check(model->Skeleton.Joints.size() == 4 && model->Skeleton.Joints[1].IsJoint && model->Skeleton.Joints[2].Parent == 1,"joint mapping includes non-joint ancestors");
    Check(model->Skins[0].Joints == std::vector<lv::JointId>({1,2}) && Near(model->Skins[0].InverseBindMatrices[1].Elements[13],-1),"inverse binds and joint mapping retained");
    Check(model->Parts.size() == 1 && model->Parts[0].SkinIndex == 0 && model->Parts[0].Node == 3,"part keeps skin and node identity");
    const auto& v = model->Meshes[0].Primitives[0].Geometry.Vertices[2];
    Check(v.Joints[1] == 1 && Near(v.Weights[0],128/255.0f) && Near(v.Weights[0]+v.Weights[1],1),"ushort joints and normalized-byte weights decoded");
    Check(Near(model->ModelBounds.Min.X,-.15f) && Near(model->ModelBounds.Max.Y,2),"reference bounds use skin and ignore mesh-node transform");
    Check(model->Skeleton.Signature == lv::SkeletonSignature(model->Skeleton),"stable reference skeleton signature");
    auto reference = lv::ReferencePose(model->Skeleton); Check(reference.Joints.size() == 4,"full reference pose contains every editable transform node");
    lv::AnimationPlayer player; player.Bind(model);
    Check(player.State() == lv::PlaybackState::Stopped && !player.Clip() && player.Speed() == 1 && player.Loop(),"bound player starts at reference pose");
    auto evaluated = player.Evaluate(); Check(Near(evaluated.Global[2].Elements[12],.2f) && Near(evaluated.Global[2].Elements[13],1),"joint world includes ancestor transform");
    player.Play(0); player.Seek(.5); auto pose = player.CapturePose();
    Check(Near(pose.Joints[1].Local.Position.X,.25f) && Near(pose.Joints[2].Local.Rotation.Z,std::sin(std::numbers::pi_v<float>/8)),"linear translation and quaternion slerp sampled");
    player.Seek(1); const auto bent = player.GetBounds(); Check(bent.Min.X < -.29f && bent.Max.Y < 1.1f,"pose changes deformed bounds");
    player.Play(1); player.Seek(.999); Check(Near(player.CapturePose().Joints[1].Local.Position.X,0),"STEP holds preceding sample");
    player.Seek(1); Check(Near(player.CapturePose().Joints[1].Local.Position.X,1),"STEP switches at exact key");
    player.Play(2); player.Seek(1); Check(Near(player.CapturePose().Joints[2].Local.Scale.X,1.5f),"scale track interpolates");
    player.Play(3); player.Seek(2); Check(Near(player.GetBounds().Min.X,.85f),"animated non-joint ancestor reaches skin");
    player.Play(0); player.Update(5.25); Check(Near(static_cast<float>(player.Time()),1.25f),"large looping delta wraps repeatedly");
    player.SetSpeed(-.5); player.Update(3); Check(Near(static_cast<float>(player.Time()),1.75f),"reverse playback wraps below zero");
    player.SetLoop(false); player.SetSpeed(2); player.Seek(1); player.Update(1);
    Check(player.Time() == 2 && player.State() == lv::PlaybackState::Stopped,"non-looping playback holds endpoint and stops");
    player.Update(1); Check(player.Time() == 2,"stopped endpoint does not advance");
    player.Stop(); Check(player.Time() == 0 && player.Clip() == 0,"Stop keeps clip and returns to first frame");
    player.SetSpeed(1); player.Play(0); player.Update(.25); player.Pause(); player.Update(1);
    Check(player.Time() == .25 && player.State() == lv::PlaybackState::Paused,"paused playback holds time");
    player.Resume(); player.Update(.25); Check(player.Time() == .5,"resume advances");
    player.Play(0,false); Check(player.Time() == .5,"same clip can resume without restart");
    player.Play(1,false); Check(player.Time() == 0,"new clip resets time");
    player.SetSpeed(-1); player.Play(0); Check(player.Time() == 2,"reverse restart begins at clip end");
    player.SetSpeed(0); player.Update(1); Check(player.Time() == 2,"zero speed freezes playing state");
    player.Seek(100); Check(player.Time() == 2,"seek clamps at duration even in loop mode");
    const auto old = player.Time(); Rejects([&] { player.Update(-1); },"negative delta rejected");
    Rejects([&] { player.Update(std::numeric_limits<double>::infinity()); },"nonfinite delta rejected");
    player.SetSpeed(100); Rejects([&] { player.Update(std::numeric_limits<double>::max()); },"numeric time overflow rejected");
    Check(player.Time() == old,"failed update preserves time");
    Rejects([&] { player.Seek(-1); },"negative seek rejected"); Rejects([&] { player.SetSpeed(101); },"speed bound enforced");
    Rejects([&] { player.Play(99); },"foreign clip rejected");
    player.SetSpeed(1); player.Play(0); player.Seek(.5);
    auto local = model->Skeleton.Joints[2].ReferenceLocal; local.Position.X = 2;
    player.SetJointOverride(2,local); player.Update(.25);
    Check(Near(player.CapturePose().Joints[2].Local.Position.X,2) && Near(player.CapturePose().Joints[2].Local.Rotation.Z,0),"manual override replaces full TRS after animation");
    auto independent = player; independent.ClearOverrides(); independent.Seek(1);
    Check(player.Overrides().Joints.size() == 1 && independent.Overrides().Joints.empty() && player.Time() != independent.Time(),"player copies share assets but have independent state");
    player.ClearJointOverride(2); Check(player.Overrides().Joints.empty() && player.CapturePose().Joints[2].Local.Rotation.Z > .5f,"clearing override restores animation");
    const auto full = player.CapturePose("Snapshot"); player.ApplyPose(full); player.Seek(0);
    Check(player.CapturePose().Joints[2].Local.Rotation.Z > .5f,"full captured pose freezes all overridden channels");
    player.ResetPose(); Check(!player.Clip() && player.Overrides().Joints.empty() && player.State() == lv::PlaybackState::Stopped,"reference reset clears clip and overrides");
    lv::PoseAsset partial{"Partial",model->Skeleton.Signature,{{2,local}}}; player.ApplyPose(partial);
    Check(player.Overrides().Joints.size() == 1 && Near(player.CapturePose().Joints[1].Local.Position.X,0),"partial pose keeps unspecified reference joints");
    auto invalid = partial; invalid.SkeletonSignature ^= 1; Rejects([&] { player.ApplyPose(invalid); },"incompatible pose rejected");
    invalid = partial; invalid.Joints.push_back(invalid.Joints[0]); Rejects([&] { player.ApplyPose(invalid); },"duplicate pose joints rejected");
    invalid = partial; invalid.Joints[0].Joint = 999; Rejects([&] { player.ApplyPose(invalid); },"unknown pose joint rejected");
    local.Scale.X = 0; Rejects([&] { player.SetJointOverride(2,local); },"singular pose rejected");
    Check(player.Overrides().Joints.size() == 1,"failed override preserves prior pose");
    auto q = lv::Slerp({0,0,0,1},{0,0,0,-1},.5f); Check(Near(std::abs(q.W),1),"slerp uses shortest quaternion arc");
    Rejects([&] { (void)lv::Slerp({0,0,0,0},{},.5f); },"zero quaternion rejected");
    auto badClip = model->Animations[0]; badClip.Tracks[0].Times[1] = 0; Rejects([&] { lv::ValidateAnimation(model->Skeleton,badClip); },"non-increasing times rejected");
    badClip = model->Animations[0]; badClip.Tracks.push_back(badClip.Tracks[0]); Rejects([&] { lv::ValidateAnimation(model->Skeleton,badClip); },"duplicate channel rejected");
    badClip = model->Animations[2]; badClip.Tracks[0].Values[1][0] = -1; Rejects([&] { lv::ValidateAnimation(model->Skeleton,badClip); },"linear scale crossing zero rejected");
    auto badSkeleton = model->Skeleton; badSkeleton.Joints[0].Parent = 2; Rejects([&] { lv::ValidateSkeleton(badSkeleton); },"skeleton cycle rejected");
    badSkeleton = model->Skeleton; badSkeleton.Joints[2].Parent = 99; Rejects([&] { lv::ValidateSkeleton(badSkeleton); },"skeleton foreign parent rejected");
    lv::Transform trs; trs.Position = {1,2,3}; trs.Rotation = {.2f,.3f,.4f,.5f}; trs.Scale = {-2,3,4};
    MatrixNear(lv::DecomposeTRS(trs.Matrix()).Matrix(),trs.Matrix(),"TRS matrix decomposition handles reflection");
    auto shear = lv::Mat4::Identity(); shear.Elements[4] = .5f; Rejects([&] { (void)lv::DecomposeTRS(shear); },"shear is retained as read-only, not silently decomposed");
    auto rigid = registry.LoadModel(Fixture("AnimatedRigid.gltf")); lv::AnimationPlayer rigidPlayer; rigidPlayer.Bind(rigid); rigidPlayer.Play(0); rigidPlayer.Seek(1);
    Check(Near(rigidPlayer.GetBounds().Min.X,1.05f),"rigid node animation updates render-node transform");
    lv::Scene scene; const auto a = scene.AddModel(model), b = scene.AddModel(model);
    scene.GetAnimation(a).Play(0); scene.GetAnimation(a).SetSpeed(.5); scene.UpdateAnimations(1);
    Check(scene.GetObject(a).Animation.Time() == .5 && !scene.GetObject(b).Animation.Clip(),"Scene instances update independently");
    scene.SetVisible(a,false); scene.UpdateAnimations(1); Check(scene.GetObject(a).Animation.Time() == 1,"hidden instances still advance deterministically");
    scene.SetModel(a,rigid); Check(!scene.GetAnimation(a).Clip() && scene.GetAnimation(a).Model() == rigid,"model replacement resets incompatible playback state");
    auto empty = scene.CreateObject(); Rejects([&] { (void)scene.GetAnimation(empty); },"empty object has no animation state");
    lv::Viewer viewer; auto object = viewer.AddModel(Fixture("AnimatedRibbon.gltf")); Check(bool(object) && bool(viewer.PlayAnimation(object.Value(),0)),"Viewer facade can select an animation");
    Check(bool(viewer.PauseAnimation(object.Value())) && bool(viewer.StopAnimation(object.Value())) && bool(viewer.SetPose(object.Value(),partial)) && bool(viewer.ResetPose(object.Value())),"Viewer pose and playback facade");
    Check(!viewer.PlayAnimation(999,0) && !viewer.PlayAnimation(object.Value(),999),"Viewer animation errors are Results");
}
void BlendTrees() {
    lv::ModelAsset model;model.Skeleton.Joints.resize(1);
    for (int i=0;i<3;++i) {lv::AnimationClip clip;clip.Name="Clip "+std::to_string(i);clip.Duration=std::pow(2.0,i);const auto end=static_cast<float>(clip.Duration*10);clip.Tracks={{0,lv::AnimationPath::Translation,lv::AnimationInterpolation::Linear,{0,static_cast<float>(clip.Duration)},{{0,0,0,0},{end,0,0,0}}}};model.Animations.push_back(clip);}
    lv::BlendTreeDesc desc;desc.Samples={{0,0,0},{1,1,0},{2,2,0}};const auto line=lv::CreateBlendTree(model,desc);
    auto weights=lv::EvaluateBlendWeights(*line,.5f);Check(Near(weights[0],.5f) && Near(weights[1],.5f) && weights[2]==0,"Blend1D selects adjacent thresholds and normalized weights");
    Check(lv::EvaluateBlendWeights(*line,-100)[0]==1 && lv::EvaluateBlendWeights(*line,100)[2]==1 && lv::EvaluateBlendWeights(*line,1)[1]==1,"1D outside values clamp and exact thresholds select one sample");
    auto bad=desc;bad.Samples[1].X=0;Rejects([&]{lv::CreateBlendTree(model,bad);},"duplicate 1D thresholds rejected");bad=desc;std::swap(bad.Samples[0],bad.Samples[1]);Rejects([&]{lv::CreateBlendTree(model,bad);},"unordered thresholds rejected rather than silently reordered");
    bad=desc;bad.Samples[0].Clip=99;Rejects([&]{lv::CreateBlendTree(model,bad);},"unknown tree clip rejected");bad=desc;bad.Samples[0].X=std::numeric_limits<float>::quiet_NaN();Rejects([&]{lv::CreateBlendTree(model,bad);},"nonfinite sample coordinate rejected");
    bad=desc;bad.Samples.resize(lv::MaxBlendSamples+1);Rejects([&]{lv::CreateBlendTree(model,bad);},"tree sample budget is explicit");
    lv::BlendTreeDesc plane;plane.Kind=lv::BlendTreeKind::Blend2D;plane.Samples={{0,0,0},{1,1,0},{2,0,1}};const auto triangle=lv::CreateBlendTree(model,plane);
    weights=lv::EvaluateBlendWeights(*triangle,.25f,.25f);Check(triangle->Triangles().size()==1 && Near(weights[0],.5f) && Near(weights[1],.25f) && Near(weights[2],.25f),"Blend2D computes barycentric weights in its containing facet");
    weights=lv::EvaluateBlendWeights(*triangle,2,2);Check(weights[0]==0 && Near(weights[1],.5f) && Near(weights[2],.5f),"outside 2D input projects to the nearest hull edge");
    Check(lv::EvaluateBlendWeights(*triangle,-1,-1)[0]==1 && lv::EvaluateBlendWeights(*triangle,1,0)[1]==1,"2D vertex and outside corner clamps reproduce exact samples");
    const auto blended=lv::EvaluateBlendTree(model,*triangle,.25f,.25f,.5);Check(Near(blended.LocalTransforms[0].Position.X,10),"three-sample pose uses synchronized phase for unequal clip lengths");
    auto square=plane;square.Samples={{0,0,0},{1,1,0},{2,1,1},{0,0,1}};const auto quad=lv::CreateBlendTree(model,square);Check(quad->Triangles().size()==2,"cocircular square gets a deterministic two-facet triangulation");
    square.Samples.push_back({1,.5f,.5f});const auto center=lv::CreateBlendTree(model,square);Check(center->Triangles().size()==4 && lv::EvaluateBlendWeights(*center,.5f,.5f)[4]==1,"interior sample participates in Delaunay triangulation and owns its exact point");
    auto cloud=square;for (int i=0;i<9;++i) {cloud.Samples.push_back({static_cast<std::size_t>(i%3),.13f+.28f*(i%3)+.007f*(i/3),.17f+.27f*(i/3)});}const auto field=lv::CreateBlendTree(model,cloud);bool linearPrecision=true;for (int x=1;x<10;++x) {for (int y=1;y<10;++y) {const auto w=lv::EvaluateBlendWeights(*field,x*.1f,y*.1f);float px=0,py=0;for (std::size_t i=0;i<w.size();++i) {px+=w[i]*cloud.Samples[i].X;py+=w[i]*cloud.Samples[i].Y;}linearPrecision &= Near(px,x*.1f) && Near(py,y*.1f);}}Check(linearPrecision,"nonuniform Delaunay field covers the convex hull and reproduces affine input coordinates");
    Check(lv::EvaluateBlendWeights(*quad,std::numeric_limits<float>::max(),std::numeric_limits<float>::max())[2]==1,"far-away finite parameters retain the nearest convex-hull corner");
    bool coverage=true;for (int x=-2;x<=12;++x) {for (int y=-2;y<=12;++y) {const auto w=lv::EvaluateBlendWeights(*center,x*.1f,y*.1f);float sum=0;int active=0;for (auto value:w) {sum+=value;active+=value>0;coverage &= value>=0 && std::isfinite(value);}coverage &= Near(sum,1) && active<=3;}}Check(coverage,"2D interior, edges and exterior retain nonnegative normalized weights with at most three contributors");
    bool delaunay=true;for (const auto& t:center->Triangles()) {const auto& a=square.Samples[t[0]];const auto& b=square.Samples[t[1]];const auto& c=square.Samples[t[2]];for (const auto& p:square.Samples) {const double ax=a.X-p.X,ay=a.Y-p.Y,bx=b.X-p.X,by=b.Y-p.Y,cx=c.X-p.X,cy=c.Y-p.Y;const auto det=(ax*ax+ay*ay)*(bx*cy-by*cx)-(bx*bx+by*by)*(ax*cy-ay*cx)+(cx*cx+cy*cy)*(ax*by-ay*bx);delaunay &= det<=1e-8;}}Check(delaunay,"generated facets satisfy the independent empty-circumcircle condition");
    bad=plane;bad.Samples[2]=bad.Samples[1];Rejects([&]{lv::CreateBlendTree(model,bad);},"duplicate 2D points rejected");bad=plane;bad.Samples[2]={2,2,0};Rejects([&]{lv::CreateBlendTree(model,bad);},"collinear 2D points rejected");bad=plane;bad.ParameterY=bad.ParameterX;Rejects([&]{lv::CreateBlendTree(model,bad);},"2D parameter names must be distinct");
    Rejects([&]{lv::EvaluateBlendWeights(*triangle,std::numeric_limits<float>::infinity(),0);},"nonfinite parameter rejected");Rejects([&]{lv::EvaluateBlendTree(model,*line,0,0,1.01);},"out-of-range normalized phase rejected");
    auto reflected=model;reflected.Animations[1].Tracks.push_back({0,lv::AnimationPath::Scale,lv::AnimationInterpolation::Step,{0},{{-1,1,1,0}}});Rejects([&]{lv::CreateBlendTree(reflected,desc);},"tree validates scale signs against missing reference channels");
    auto asset=std::make_shared<const lv::ModelAsset>(model);lv::AnimationPlayer player;player.Bind(asset);player.PlayBlendTree(line);player.SetBlendParameter("Speed",.5f);Check(!player.Clip() && player.Tree() && Near(static_cast<float>(player.BlendTreeDuration()),1.5f),"tree selection has independent normalized clock and weighted cycle duration");
    player.Update(.75);Check(Near(static_cast<float>(player.Tree()->Phase),.5f) && Near(player.EvaluatePose().LocalTransforms[0].Position.X,7.5f),"tree playback samples each clip at a common normalized phase");
    auto copy=player;copy.SetBlendParameter("Speed",2);copy.SeekBlendTree(.25);Check(player.Tree()->Tree==copy.Tree()->Tree && Near(player.Tree()->X,.5f) && Near(static_cast<float>(player.Tree()->Phase),.5f),"instances share immutable tree data but own parameters and phase");
    player.Pause();player.Update(1);player.SetBlendParameter("Speed",1);Check(player.State()==lv::PlaybackState::Paused && Near(static_cast<float>(player.Tree()->Phase),.5f),"paused parameter edits preserve phase");player.Resume();player.SetSpeed(0);player.Update(1);Check(Near(static_cast<float>(player.Tree()->Phase),.5f),"zero speed freezes tree time");
    player.SetSpeed(-1);player.SetLoop(false);player.Update(2);Check(player.Tree()->Phase==0 && player.State()==lv::PlaybackState::Stopped,"reverse nonlooping tree stops at phase zero");player.PlayBlendTree(line);Check(player.Tree()->Phase==1,"reverse tree restart begins at phase one");
    const auto old=player.Tree();Rejects([&]{player.SetBlendParameter("Missing",1);},"unknown parameter rejected");Rejects([&]{player.SeekBlendTree(-.1);},"negative tree phase rejected");Check(player.Tree()->Phase==old->Phase && player.Tree()->X==old->X,"invalid edits preserve tree state");
    player.SetSpeed(1);player.SeekBlendTree(.5);const auto before=player.Evaluate();player.CrossFade(0,1);MatrixNear(player.Evaluate().Global[0],before.Global[0],"tree to clip cross-fade freezes the blended source without popping");Check(!player.Tree() && player.Blend() && !player.Blend()->SourceClip,"tree-to-clip transition uses a frozen source");
    player.PlayBlendTree(line);player.SeekBlendTree(.5);auto local=model.Skeleton.Joints[0].ReferenceLocal;local.Position.X=99;player.SetJointOverride(0,local);Check(Near(player.EvaluatePose().LocalTransforms[0].Position.X,99),"manual pose override runs after tree mixing");player.ClearOverrides();player.Rebind(std::make_shared<const lv::ModelAsset>(model));Check(player.Tree()->Tree==line && player.Tree()->Phase==.5,"compatible reimport preserves tree state");
    auto renamed=std::make_shared<lv::ModelAsset>(model);renamed->Animations[1].Name="Changed";Rejects([&]{player.Rebind(renamed);},"tree reimport checks every referenced clip identity");
    auto constant=model;constant.Animations[0].Duration=0;constant.Animations[0].Tracks[0].Times={0};constant.Animations[0].Tracks[0].Values={{7,0,0,0}};auto only=desc;only.Samples={{0,0,0}};const auto staticTree=lv::CreateBlendTree(constant,only);lv::AnimationPlayer fixed;fixed.Bind(std::make_shared<const lv::ModelAsset>(constant));fixed.PlayBlendTree(staticTree);Check(fixed.State()==lv::PlaybackState::Stopped && Near(fixed.EvaluatePose().LocalTransforms[0].Position.X,7),"all-zero-duration tree is a stopped constant pose");
    lv::AssetRegistry assets;const auto morph=assets.LoadModel(Fixture("MorphRibbon.gltf"));auto morphDesc=desc;morphDesc.Samples={{4,0,0},{5,1,0}};const auto morphTree=lv::CreateBlendTree(*morph,morphDesc);lv::AnimationPlayer mp;mp.Bind(morph);mp.PlayBlendTree(morphTree);mp.SetBlendParameter("Speed",.5f);mp.SeekBlendTree(.25);Check(Near(mp.EvaluateMorphWeights(3)[0],.25f),"Morph channels use the same tree weights and normalized clip phase");mp.SetMorphWeights(3,std::array<float,2>{.8f,0});Check(Near(mp.EvaluateMorphWeights(3)[0],.8f),"manual Morph override remains last after tree evaluation");mp.ResetPose();Check(!mp.Tree() && mp.MorphOverrides().empty(),"reference reset clears tree and overrides");
}
void TreePersistence() {
    TemporaryDirectory temp;lv::EditorDocument doc;const auto asset=doc.ImportModel(Fixture("MorphRibbon.gltf")).Value(),a=doc.PlaceModel(asset).Value(),b=doc.PlaceModel(asset).Value();lv::BlendTreeDesc desc;desc.Kind=lv::BlendTreeKind::Blend2D;desc.Samples={{4,0,0},{5,1,0},{0,0,1}};const auto tree=lv::CreateBlendTree(*doc.GetScene().GetObject(a).Model,desc);
    Check(bool(doc.PlayBlendTree(a,tree)) && bool(doc.PlayBlendTree(b,tree)) && bool(doc.SetBlendParameter(a,"Speed",.25f)) && bool(doc.SetBlendParameter(b,"Speed",.75f)) && bool(doc.SeekBlendTree(a,.5)) && bool(doc.SeekBlendTree(b,.25)) && bool(doc.PauseAnimation(a)) && bool(doc.PauseAnimation(b)),"Editor facades edit independent tree playback");
    Check(bool(doc.SetBlendParameter(a,"Speed",.4f)) && bool(doc.Undo()) && Near(doc.GetScene().GetObject(a).Animation.Tree()->X,.25f) && bool(doc.Redo()),"tree parameter edits participate in Undo/Redo");
    Check(bool(doc.SetBlendParameter(b,"Direction",.2f)),"Editor records the second 2D parameter independently");const auto path=temp.File("Tree.mvxscene.json");Check(bool(doc.Save(path)),"Scene writes shared tree definition and instance state");const auto saved=Json::parse(Read(path));lv::EditorDocument loaded;Check(saved["version"]==10 && saved["blendTrees"].size()==1 && bool(loaded.Open(path)),"Scene v9 deduplicates and restores shared tree assets");
    const auto ids=loaded.GetScene().ObjectIds();const auto& first=loaded.GetScene().GetObject(ids[0]).Animation;const auto& second=loaded.GetScene().GetObject(ids[1]).Animation;Check(first.Tree()->Tree==second.Tree()->Tree && Near(first.Tree()->X,.4f) && Near(second.Tree()->X,.75f) && Near(second.Tree()->Y,.2f) && first.Tree()->Phase==.5 && second.Tree()->Phase==.25 && first.State()==lv::PlaybackState::Paused,"loaded instances share tree ownership while preserving independent parameters and clocks");
    Check(Near(first.EvaluateMorphWeights(3)[0],doc.GetScene().GetObject(a).Animation.EvaluateMorphWeights(3)[0]),"tree-driven Morph state survives Scene roundtrip");
    const auto rejects=[&](Json bad,const char* message){Write(path,bad.dump());Check(!loaded.Open(path) && loaded.GetScene().Contains(ids[0]),message);};auto bad=saved;bad["objects"][0]["animation"]["tree"]["phase"]=2;rejects(bad,"invalid saved tree phase rejected without replacing document");bad=saved;bad["blendTrees"][0]["definition"]["signature"]="0000000000000000";rejects(bad,"changed saved tree layout rejected");bad=saved;bad["objects"][0]["animation"]["tree"]["id"]=999;rejects(bad,"unknown saved tree reference rejected");bad=saved;bad["blendTrees"].push_back(bad["blendTrees"][0]);rejects(bad,"duplicate tree IDs rejected");
    auto legacy=saved;legacy["version"]=8;legacy.erase("blendTrees");for (auto& object:legacy["objects"]) {object["animation"].erase("tree");object["animation"]["state"]=0;}Write(path,legacy.dump());Check(bool(loaded.Open(path)) && !loaded.GetScene().GetObject(loaded.GetScene().ObjectIds()[0]).Animation.Tree(),"Scene v8 without trees remains readable");
    Check(bool(doc.ReimportModel(asset)) && doc.GetScene().GetObject(a).Animation.Tree()->Tree==tree,"compatible Editor reimport preserves shared tree asset and playback");
    lv::Viewer viewer;const auto id=viewer.AddModel(Fixture("MorphRibbon.gltf")).Value();Check(bool(viewer.PlayBlendTree(id,tree)) && bool(viewer.SetBlendParameter(id,"Speed",.5f)) && bool(viewer.SeekBlendTree(id,.5)) && !viewer.SetBlendParameter(id,"Missing",0),"Viewer Blend Tree controls return validated Results");
}
void Blending() {
    lv::AssetRegistry registry;const auto model=registry.LoadModel(Fixture("AnimatedRibbon.gltf"));lv::AnimationPlayer player;player.Bind(model);
    player.CrossFade(0,1);player.Update(.5);auto pose=player.CapturePose();
    Check(player.Blend() && !player.Blend()->SourceClip && Near(player.BlendWeight(),.5f) && Near(pose.Joints[1].Local.Position.X,.125f) && Near(pose.Joints[2].Local.Rotation.Z,std::sin(std::numbers::pi_v<float>/16)),"reference to clip cross-fade interpolates translation and shortest-arc rotation");
    player.Pause();player.Update(1);Check(player.Time() == .5 && player.Blend()->Elapsed == .5,"Pause freezes both clip and fade clock");
    player.Resume();player.SetSpeed(0);player.Update(1);Check(player.Blend()->Elapsed == .5,"zero speed freezes fade clock");player.SetSpeed(1);
    const auto before=player.Evaluate();player.CrossFade(2,1);MatrixNear(player.Evaluate().Global[2],before.Global[2],"interrupted fade starts from current base pose without popping");
    auto local=model->Skeleton.Joints[2].ReferenceLocal;local.Position.X=2;player.SetJointOverride(2,local);player.Update(.5);
    Check(Near(player.CapturePose().Joints[2].Local.Position.X,2) && Near(player.CapturePose().Joints[2].Local.Scale.X,1),"manual full-TRS overrides run after blending");
    player.ClearOverrides();Check(Near(player.CapturePose().Joints[2].Local.Scale.X,1.125f),"scale interpolation remains available after clearing override");
    player.Update(.5);Check(!player.Blend() && Near(player.CapturePose().Joints[2].Local.Scale.X,1.5f),"completed transition releases source and evaluates target alone");
    player.Play(0);player.Seek(.5);player.CrossFade(1,1);player.Update(.5);
    Check(player.Blend()->SourceClip == 0 && player.Blend()->SourceTime == 1 && player.Time() == .5 && Near(player.CapturePose().Joints[1].Local.Position.X,.25f),"live source and incoming STEP clip advance on independent playheads");
    auto copy=player;copy.Update(.25);Check(player.Blend()->Elapsed == .5 && copy.Blend()->Elapsed == .75,"copied transitions retain independent clocks");
    player.Rebind(std::make_shared<lv::ModelAsset>(*model));Check(player.Blend()->SourceTime == 1,"compatible rebind preserves transition");
    auto changed=std::make_shared<lv::ModelAsset>(*model);changed->Animations[0].Name="Different source";Rejects([&]{player.Rebind(changed);},"rebind checks live source identity as well as target");
    Rejects([&]{player.CrossFade(999,1);},"foreign blend target rejected");Rejects([&]{player.CrossFade(0,-1);},"negative blend duration rejected");Rejects([&]{player.CrossFade(0,std::numeric_limits<double>::infinity());},"nonfinite blend duration rejected");
    auto invalid=*player.Blend();invalid.Elapsed=invalid.Duration;Rejects([&]{player.RestoreBlend(invalid);},"completed progress cannot be restored as an active blend");
    Check(player.Clip() == 1 && player.Blend()->Elapsed == .5,"failed transitions preserve existing state");
    player.Seek(.5);Check(!player.Blend(),"scrubbing clears transition");player.CrossFade(0,1);player.Stop();Check(!player.Blend(),"Stop clears transition");
    player.CrossFade(1,1);player.ResetPose();Check(!player.Blend() && !player.Clip(),"reference reset clears transition");player.CrossFade(0,0);Check(!player.Blend() && player.Clip() == 0,"zero duration switches immediately");
    player.SetLoop(false);player.SetSpeed(-1);player.Play(0);player.CrossFade(1,3);player.Update(2);
    Check(player.Time() == 0 && player.State() == lv::PlaybackState::Playing && player.Blend(),"reverse non-looping target holds endpoint until fade completes");player.Update(1);
    Check(!player.Blend() && player.State() == lv::PlaybackState::Stopped && player.Time() == 0,"fade completion stops a non-looping endpoint");
    auto reflected=std::make_shared<lv::ModelAsset>(*model);for (auto& value:reflected->Animations[2].Tracks[0].Values) {value[0]=-value[0];}player.Bind(reflected);
    Rejects([&]{player.CrossFade(2,1);},"opposite scale signs cannot blend through singular zero");Check(!player.Clip(),"failed scale validation preserves reference state");
    lv::Viewer viewer;const auto id=viewer.AddModel(Fixture("AnimatedRibbon.gltf")).Value();
    Check(bool(viewer.CrossFadeAnimation(id,0,.5)) && viewer.GetScene().GetObject(id).Animation.Blend() && !viewer.CrossFadeAnimation(id,999,.5),"Viewer exposes validated cross-fade Results before native initialization");
}
void BlendPersistence() {
    TemporaryDirectory temp;lv::EditorDocument doc;const auto asset=doc.ImportModel(Fixture("AnimatedRibbon.gltf")).Value(),id=doc.PlaceModel(asset).Value();
    Check(bool(doc.PlayAnimation(id,0)) && bool(doc.SeekAnimation(id,.5)),"prepare blend source");Check(bool(doc.CrossFadeAnimation(id,1,1)) && doc.IsDirty(),"Editor cross-fade records an undoable edit");
    Check(bool(doc.Undo()) && !doc.GetScene().GetObject(id).Animation.Blend() && bool(doc.Redo()) && doc.GetScene().GetObject(id).Animation.Blend(),"Undo/Redo restores transition state");
    Check(bool(doc.UpdateAnimations(.25)) && bool(doc.PauseAnimation(id)),"pause blend preview");const auto file=temp.File("Blend.mvxscene.json");Check(bool(doc.Save(file)),"active transition saves");
    const auto original=Json::parse(Read(file));lv::EditorDocument loaded;Check(original["version"] == 10 && bool(loaded.Open(file)),"Scene v8 restores active live-source blend");
    const auto other=loaded.GetScene().ObjectIds()[0];const auto& restored=loaded.GetScene().GetObject(other).Animation;
    Check(restored.State() == lv::PlaybackState::Paused && restored.Blend()->SourceClip == 0 && restored.Blend()->SourceTime == .75 && restored.Time() == .25 && restored.Blend()->Elapsed == .25,"paused transition clocks and clip identities survive serialization");
    MatrixNear(restored.Evaluate().Global[2],doc.GetScene().GetObject(id).Animation.Evaluate().Global[2],"live blend roundtrip preserves evaluated pose");
    Check(bool(loaded.CrossFadeAnimation(other,2,1)) && bool(loaded.UpdateAnimations(.5)) && bool(loaded.PauseAnimation(other)) && bool(loaded.Save(file)),"persist interrupted blend");lv::EditorDocument frozen;
    Check(bool(frozen.Open(file)) && !frozen.GetScene().GetObject(frozen.GetScene().ObjectIds()[0]).Animation.Blend()->SourceClip,"interrupted transition persists complete frozen source pose");
    MatrixNear(frozen.GetScene().GetObject(frozen.GetScene().ObjectIds()[0]).Animation.Evaluate().Global[2],loaded.GetScene().GetObject(other).Animation.Evaluate().Global[2],"frozen source roundtrip preserves evaluated pose");
    auto legacy=original;legacy["version"]=5;legacy["objects"][0]["animation"].erase("blend");Write(file,legacy.dump());
    Check(bool(frozen.Open(file)) && !frozen.GetScene().GetObject(frozen.GetScene().ObjectIds()[0]).Animation.Blend(),"Scene v5 without transition field remains readable");
    const auto rejects=[&](Json changed,const char* label) {Write(file,changed.dump());Check(!loaded.Open(file) && loaded.GetScene().Contains(other) && loaded.GetScene().GetObject(other).Animation.Blend(),label);};
    auto bad=original;bad["objects"][0]["animation"]["blend"]["sourceClipName"]="Changed";rejects(bad,"stale blend source identity is rejected without replacing document");
    bad=original;bad["objects"][0]["animation"]["blend"]["elapsed"]=1;rejects(bad,"invalid transition clock rejected");
    bad=original;bad["objects"][0]["animation"]["state"]=0;rejects(bad,"stopped active transition rejected");
    bad=original;bad["objects"][0]["animation"].erase("blend");rejects(bad,"Scene v6 requires explicit transition field");
}
void Persistence() {
    TemporaryDirectory temp; lv::EditorDocument doc;
    auto asset = doc.ImportModel(Fixture("AnimatedRibbon.gltf")); auto object = doc.PlaceModel(asset.Value()); const auto id = object.Value();
    Check(bool(doc.PlayAnimation(id,0)) && bool(doc.PauseAnimation(id)) && bool(doc.SeekAnimation(id,.75)),"Editor controls select/pause/seek");
    Check(bool(doc.SetAnimationSpeed(id,-2)) && bool(doc.SetAnimationLoop(id,false)),"Editor controls speed and loop");
    auto local = doc.GetScene().GetObject(id).Model->Skeleton.Joints[2].ReferenceLocal; local.Rotation = {0,0,.5f,.8660254f};
    Check(bool(doc.SetJointPose(id,2,local)),"Editor bone override edits document");
    const auto scene = temp.File("Scene.mvxscene.json"), full = temp.File("Full.mvxpose.json"), partial = temp.File("Partial.mvxpose.json");
    Check(bool(doc.SavePoseFile(id,full)) && bool(doc.SavePoseFile(id,partial,true)),"full and partial standalone poses saved");
    auto json = Json::parse(Read(partial)); Check(json["joints"].size() == 1,"partial file contains only manual overrides");
    const auto& skeleton = doc.GetScene().GetObject(id).Model->Skeleton;
    auto loaded = lv::LoadPose(full,skeleton); Check(loaded && loaded.Value().Joints.size() == 4,"standalone full pose loads without Scene");
    const auto newPoseBytes=Read(full);auto legacyBrand=Json::parse(newPoseBytes);
    Check(legacyBrand.at("format") == "MinvexPose", "new poses write the renamed format");
    legacyBrand["format"]="MinuvexPose";Write(full,legacyBrand.dump());
    Check(bool(lv::LoadPose(full,skeleton)),"pre-rename poses remain readable");Write(full,newPoseBytes);
    Check(bool(doc.SetLocalLights({{{1,2,3},{1,.5f,.25f},2,5}},{{{0,3,0},{0,-1,0},{.2f,.4f,1},3,10,.2f,.5f}})),"Editor validates local light edits");
    Check(bool(doc.Save(scene)) && !doc.IsDirty(),"animated Scene version 3 saves");
    Check(!doc.SetLocalLights({{{0,0,0},{1,1,1},1,-1}}, {}) && doc.GetScene().PointLights.size() == 1 && doc.GetScene().SpotLights.size() == 1 && !doc.IsDirty(),"invalid light edit preserves both lists and clean document state");
    Check(bool(doc.UpdateAnimations(.5)) && !doc.IsDirty(),"preview tick does not dirty document");
    lv::EditorDocument reopened; Check(bool(reopened.Open(scene)),"animated Scene reloads");
    Check(reopened.GetScene().PointLights.size() == 1 && reopened.GetScene().SpotLights.size() == 1 && reopened.GetScene().PointLights[0].Range == 5,"Scene v3 round-trips point and spot lights");
    const auto newId = reopened.GetScene().ObjectIds()[0]; const auto& player = reopened.GetScene().GetObject(newId).Animation;
    Check(player.Clip() == 0 && player.Time() == .75 && player.State() == lv::PlaybackState::Paused && player.Speed() == -2 && !player.Loop(),"clip/time/state/speed/loop restored per instance");
    Check(player.Overrides().Joints.size() == 1 && Near(player.CapturePose().Joints[2].Local.Rotation.Z,.5f),"Scene pose overrides restored");
    Check(bool(reopened.ClearPoseOverrides(newId)) && bool(reopened.LoadPoseFile(newId,full)),"Editor can load full pose onto an existing instance");
    const auto original = Json::parse(Read(scene)); auto legacy = original; legacy["version"] = 1; for (auto& entry : legacy["objects"]) { entry.erase("animation"); }
    auto version2=original;version2["version"]=2;version2.erase("pointLights");version2.erase("spotLights");Write(scene,version2.dump());lv::EditorDocument v2;Check(bool(v2.Open(scene)) && v2.GetScene().GetObject(v2.GetScene().ObjectIds()[0]).Animation.Clip() == 0 && v2.GetScene().PointLights.empty(),"legacy Scene v2 retains animation and defaults local lights to empty");
    Write(scene,legacy.dump()); lv::EditorDocument old; Check(bool(old.Open(scene)) && !old.GetScene().GetObject(old.GetScene().ObjectIds()[0]).Animation.Clip(),"version-1 Scene loads in reference pose");
    const auto rejects = [&](Json changed,const char* label) {
        Write(scene,changed.dump()); auto result = reopened.Open(scene); Check(!result && reopened.GetScene().Contains(newId) && reopened.IsDirty(),label);
    };
    auto changed = original; changed["objects"][0]["animation"]["clip"] = 99; rejects(changed,"stale clip rejected without replacing document");
    changed = original; changed["objects"][0]["animation"]["clipName"] = "different"; rejects(changed,"changed clip identity rejected");
    changed = original; changed["objects"][0]["animation"]["time"] = 99; rejects(changed,"invalid saved playhead rejected");
    changed = original; changed["objects"][0]["animation"]["state"] = 3; rejects(changed,"unknown playback state rejected");
    changed = original; changed["objects"][0]["animation"]["overrides"]["skeleton"] = "0000000000000000"; rejects(changed,"incompatible saved skeleton rejected");
    auto invalid = Json::parse(Read(full)); invalid["joints"].push_back(invalid["joints"][0]); Write(full,invalid.dump());
    Check(!reopened.LoadPoseFile(newId,full) && reopened.GetScene().GetObject(newId).Animation.Overrides().Joints.size() == 4,"failed Pose load preserves existing overrides");
    const auto before = Read(partial); auto bad = player.Overrides(); bad.SkeletonSignature = 0;
    Check(!lv::SavePose(bad,skeleton,partial) && Read(partial) == before,"failed Pose save preserves previous file");
    Check(!doc.LoadPoseFile(id,temp.File("missing.mvxpose.json")),"missing Pose has recoverable IO error");
    Check(bool(doc.SetAnimationSpeed(id,1.234567890123)) && bool(doc.StopAnimation(id)) && bool(doc.SeekAnimation(id,2)) && bool(doc.Save(scene)),"stopped endpoint and precise speed saved");
    lv::EditorDocument precise; Check(bool(precise.Open(scene)),"precise playback state loads");
    const auto& restored = precise.GetScene().GetObject(precise.GetScene().ObjectIds()[0]).Animation;
    Check(restored.Speed() == 1.234567890123 && restored.Time() == 2 && restored.State() == lv::PlaybackState::Stopped,"double speed and stopped endpoint survive serialization exactly");
}
void Reimport() {
    TemporaryDirectory temp;const auto file=temp.File("reimport.gltf");auto source=Json::parse(Read(Fixture("AnimatedRibbon.gltf")));Write(file,source.dump());
    lv::EditorDocument doc;const auto asset=doc.ImportModel(file).Value();const auto reference=doc.PlaceModel(asset).Value(),a=doc.PlaceModel(asset).Value(),b=doc.PlaceModel(asset).Value();
    Check(bool(doc.PlayAnimation(a,0)) && bool(doc.SeekAnimation(a,.75)) && bool(doc.PauseAnimation(a)) && bool(doc.SetAnimationSpeed(a,-2)) && bool(doc.PlayAnimation(b,0)) && bool(doc.SeekAnimation(b,1.5)),"reimport fixture has independent playback states");
    lv::Transform pose;pose.Position.X=.2f;Check(bool(doc.SetJointPose(a,2,pose)),"reimport fixture has an active manual pose");
    const auto old=doc.GetScene().GetObject(a).Model;
    source["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"]={.2,.4,.6,1};Write(file,source.dump());
    Check(bool(doc.ReimportModel(asset)) && doc.IsDirty(),"compatible reimport updates a document");
    const auto replacement=doc.GetScene().GetObject(a).Model;
    const auto& pa=doc.GetScene().GetObject(a).Animation;const auto& pb=doc.GetScene().GetObject(b).Animation;
    Check(replacement != old && replacement->Id == asset && doc.Assets()[0] == replacement && doc.GetScene().GetObject(reference).Model == replacement && doc.GetScene().GetObject(b).Model == replacement && doc.Selection() == b,"reimport replaces every shared instance and library while preserving selection and IDs");
    Check(pa.Time() == .75 && pa.Speed() == -2 && pa.State() == lv::PlaybackState::Paused && pa.Overrides().Joints.size() == 1 && pb.Time() == 1.5 && pb.State() == lv::PlaybackState::Playing,"reimport retains per-instance playback and pose");
    Check(bool(doc.Save(temp.File("world.mvxscene.json"))) && !doc.IsDirty(),"reimported Scene remains serializable");
    auto incompatible=source;incompatible["nodes"][1]["name"]="Changed hierarchy";Write(file,incompatible.dump());
    const auto failed=doc.ReimportModel(asset);
    Check(!failed && failed.Error().Code == lv::ErrorCode::InvalidArgument && doc.GetScene().GetObject(reference).Model == replacement && doc.GetScene().GetObject(a).Model == replacement && doc.GetScene().GetObject(b).Model == replacement && doc.Assets()[0] == replacement && !doc.IsDirty(),"incompatible skeleton reimport rolls back all instances, including an earlier compatible reference instance");
    incompatible=source;incompatible["animations"][0]["name"]="Different clip";Write(file,incompatible.dump());
    Check(!doc.ReimportModel(asset) && doc.GetScene().GetObject(a).Animation.Time() == .75 && !doc.IsDirty(),"changed selected clip identity is rejected without resetting playback");
    Write(file,source.dump());const auto c=doc.PlaceModel(asset).Value();Check(doc.GetScene().GetObject(c).Model == replacement,"failed transaction also preserves the registry for later placement");
}
void ImportValidation() {
    TemporaryDirectory temp; const auto original = Json::parse(Read(Fixture("AnimatedRibbon.gltf")));
    const auto rejects = [&](Json json,lv::ImportErrorCode code,const char* label) {
        const auto path = temp.File("invalid.gltf"); Write(path,json.dump()); lv::AssetRegistry registry;
        ImportRejects([&] { (void)registry.LoadModel(path); },code,label);
    };
    auto j = original; j["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_0"); rejects(j,lv::ImportErrorCode::InvalidData,"unpaired joint attributes rejected");
    j = original; j["meshes"][0]["primitives"][0]["attributes"]["JOINTS_1"] = 2; j["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_1"] = 3;
    rejects(j,lv::ImportErrorCode::UnsupportedFeature,"more than four influences rejected explicitly");
    j = original; j["skins"][0]["joints"] = {1,1}; rejects(j,lv::ImportErrorCode::InvalidData,"duplicate skin joints rejected");
    j = original; j["accessors"][5]["count"] = 1; rejects(j,lv::ImportErrorCode::InvalidData,"inverse-bind count mismatch rejected");
    j = original; j["animations"][0]["samplers"][0]["interpolation"] = "CUBICSPLINE"; j["accessors"][7]["count"] = 9;
    // Storage is truncated before interpolation validation: still fail rather than silently playing it.
    rejects(j,lv::ImportErrorCode::InvalidData,"malformed cubic clip rejected");
    j = original; j["animations"] = Json::array({original["animations"][0]}); j["animations"][0]["channels"].erase(1); j["animations"][0]["samplers"].erase(1);
    j["animations"][0]["samplers"][0]["interpolation"] = "CUBICSPLINE"; j["accessors"][6]["count"] = 1; j["accessors"][6]["max"] = {0};
    rejects(j,lv::ImportErrorCode::UnsupportedFeature,"valid CUBICSPLINE is rejected explicitly, not silently sampled as linear");
    j = original; j["animations"][0]["channels"].push_back(j["animations"][0]["channels"][0]); rejects(j,lv::ImportErrorCode::InvalidData,"duplicate animation target/path rejected");
    j = original; j["accessors"][2]["normalized"] = true; rejects(j,lv::ImportErrorCode::InvalidData,"normalized JOINTS rejected");
    lv::ImportSettings limited; limited.MaxResourceBytes = 1024; lv::AssetRegistry registry;
    ImportRejects([&] { (void)registry.LoadModel(Fixture("AnimatedRibbon.gltf"),limited); },lv::ImportErrorCode::ResourceLimit,"animation import observes resource budget");
    j = original; j["meshes"][0]["primitives"][0]["attributes"].erase("NORMAL"); Write(temp.File("flat.gltf"),j.dump());
    auto flat = registry.LoadModel(temp.File("flat.gltf"));
    Check(flat->Meshes[0].Primitives[0].Geometry.Vertices.size() == 12 && flat->Meshes[0].Primitives[0].Geometry.Vertices.back().Joints[1] == 1,"flat-normal expansion preserves joint attributes");
    j = original; j["skins"][0].erase("inverseBindMatrices"); Write(temp.File("identity.gltf"),j.dump());
    auto identity = registry.LoadModel(temp.File("identity.gltf")); Check(Near(identity->Skins[0].InverseBindMatrices[1].Elements[13],0) && Near(identity->ModelBounds.Max.Y,3),"missing inverse binds default to identity");
}
}
void MorphPlayback() {
    lv::AssetRegistry assets;const auto model=assets.LoadModel(Fixture("MorphRibbon.gltf"));lv::AnimationPlayer player;player.Bind(model);
    Check(model->Meshes[0].Primitives[0].MorphTargets.size() == 2 && model->Animations[4].MorphTracks.size() == 1,"glTF morph channels and flattened weight animation imported");
    Check(Near(player.EvaluateMorphWeights(3)[0],.6f) && Near(player.EvaluateMorphWeights(4)[0],.2f),"node defaults override mesh defaults independently on a shared mesh");
    const auto& primitive=model->Meshes[0].Primitives[0];const std::array<float,2> half{.5f,0};const auto evaluated=lv::EvaluateMorph(primitive,half);
    Check(Near(evaluated.Vertices[0].Position[0],primitive.Geometry.Vertices[0].Position[0]+.25f) && Near(std::hypot(evaluated.Vertices[0].Normal[0],evaluated.Vertices[0].Normal[2]),1) && evaluated.Vertices[0].Tangent[3] == primitive.Geometry.Vertices[0].Tangent[3],"CPU morph evaluates deltas and normalizes directions while retaining tangent handedness");
    player.Play(4);player.Seek(.5);Check(Near(player.EvaluateMorphWeights(3)[0],.5f) && Near(player.EvaluateMorphWeights(4)[0],.2f),"LINEAR morph samples affect only the target node");player.Pause();player.Update(1);Check(Near(player.EvaluateMorphWeights(3)[0],.5f),"Pause holds animated morph weights");
    player.Play(5);player.Seek(.99);Check(player.EvaluateMorphWeights(3)[0] == 0,"STEP morph holds previous sample");player.Seek(1);Check(player.EvaluateMorphWeights(3)[0] == 1,"STEP morph switches at the exact key");
    const std::array<float,2> manual{-.5f,1.5f};player.SetMorphWeights(3,manual);auto other=player;other.ClearMorphWeights(3);Check(player.EvaluateMorphWeights(3) == std::vector<float>(manual.begin(),manual.end()) && other.EvaluateMorphWeights(3)[0] == 1,"signed/out-of-unit weights are supported with independent copied overrides");
    const std::array<float,1> wrong{0};Rejects([&]{player.SetMorphWeights(3,wrong);},"wrong target count rejected");const std::array<float,2> bad{NAN,0};Rejects([&]{player.SetMorphWeights(3,bad);},"nonfinite weight rejected");Check(player.MorphOverrides()[0].Weights[0] == -.5f,"failed morph edit preserves previous override");
    player.ResetPose();Check(Near(player.EvaluateMorphWeights(3)[0],.6f) && player.MorphOverrides().empty(),"ResetPose restores node/mesh default weights");
    player.Play(4);player.Seek(.5);player.CrossFade(5,1);player.Update(.5);Check(Near(player.EvaluateMorphWeights(3)[0],.5f),"live clip cross-fade blends morph channels");player.CrossFade(0,1);Check(Near(player.EvaluateMorphWeights(3)[0],.5f),"interrupted cross-fade captures current base morph without popping");player.Update(.5);Check(Near(player.EvaluateMorphWeights(3)[0],.55f),"frozen morph source blends to target reference defaults");
    auto renamed=std::make_shared<lv::ModelAsset>(*model);renamed->Meshes[0].Primitives[0].MorphTargets[0].Name="Renamed";Rejects([&]{player.Rebind(renamed);},"rebind rejects a changed target identity in use");
    const auto sparse=assets.LoadModel(Fixture("MorphSparse.gltf"));const auto& delta=sparse->Meshes[0].Primitives[0].MorphTargets[0].PositionDeltas;Check(delta[0].X == 0 && delta[1].X == 1 && delta[2].X == 0,"sparse morph deltas retain implicit zeros");
    lv::ImportSettings limited;limited.MaxMorphTargets=1;ImportRejects([&]{assets.LoadModel(Fixture("MorphRibbon.gltf"),limited);},lv::ImportErrorCode::ResourceLimit,"morph target budget overflow is explicit");
    const auto fbx=assets.LoadModel(Fixture("MorphProgressive.fbx"));lv::AnimationPlayer fbxPlayer;fbxPlayer.Bind(fbx);const auto node=fbx->Parts[0].Node;
    Check(fbx->Meshes[0].MorphWeights.size() == 2 && Near(fbxPlayer.EvaluateMorphWeights(node)[0],.5f),"FBX progressive shapes become independent targets with normalized default weights");fbxPlayer.Play(0);fbxPlayer.Seek(.75);const auto weights=fbxPlayer.EvaluateMorphWeights(node);Check(Near(weights[0],.5f) && Near(weights[1],.5f),"FBX progressive animation bake interpolates between shape thresholds");
}
int main() {
    try { TestAdditive();BlendTrees();TreePersistence();PoseBlendingCore();DataAndPlayer(); Blending(); BlendPersistence(); Persistence(); ImportValidation(); Reimport();MorphPlayback(); }
    catch (const std::exception& e) { Check(false,e.what()); }
    return Finish();
}
