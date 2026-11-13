#include <minvex/Debug.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lv {
namespace {
constexpr float Pi=3.14159265358979323846f;
Vec3 Add(Vec3 a,Vec3 b) {return {a.X+b.X,a.Y+b.Y,a.Z+b.Z};}
Vec3 Sub(Vec3 a,Vec3 b) {return {a.X-b.X,a.Y-b.Y,a.Z-b.Z};}
Vec3 Mul(Vec3 a,float s) {return {a.X*s,a.Y*s,a.Z*s};}
Vec3 Cross(Vec3 a,Vec3 b) {return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X};}
Vec3 Unit(Vec3 a) {const auto n=std::hypot(a.X,a.Y,a.Z);if (!std::isfinite(n) || n < 1e-12f) {throw std::invalid_argument("Debug direction must be finite and nonzero.");}return Mul(a,1/n);}
void Point(Vec3 p) {if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z)) {throw std::invalid_argument("Debug geometry must be finite.");}}
void Matrix(const Mat4& m) {for (const auto v:m.Elements) {if (!std::isfinite(v)) {throw std::invalid_argument("Debug matrix must be finite.");}}if (m.Elements[3] || m.Elements[7] || m.Elements[11] || m.Elements[15] != 1) {throw std::invalid_argument("Debug transform must be affine.");}}
void Radius(float r,unsigned n) {if (!std::isfinite(r) || r < 0 || n < 8 || n > 128) {throw std::invalid_argument("Debug radius must be nonnegative; segments must be 8..128.");}}
}
const char* DebugViewName(DebugView v) noexcept {
    switch (v) {case DebugView::Lit:return "Lit";case DebugView::Unlit:return "Unlit";case DebugView::Wireframe:return "Wireframe";case DebugView::Normal:return "Normal";case DebugView::UV:return "UV";case DebugView::Depth:return "Depth";case DebugView::ShadowMap:return "Shadow Map";case DebugView::BoneWeight:return "Bone Weight";case DebugView::Skeleton:return "Skeleton";case DebugView::BoundingBox:return "Bounding Box";}return "Unsupported";
}
void ValidateDebugOptions(const DebugOptions& o) {if (o.View < DebugView::Lit || o.View > DebugView::BoundingBox || o.WeightJoint < -1 || o.WeightJoint >= static_cast<int>(MaxSkinJoints)) {throw std::invalid_argument("Invalid debug view or skin palette index (-1..127).");}}
void DebugDraw::Append(std::span<const DebugLine> lines) {
    if (lines.size() > MaxDebugLines-lines_.size()) {throw std::length_error("Debug line budget exceeded (65536).");}
    for (const auto& l:lines) {Point(l.Start);Point(l.End);for (float c:l.Color) {if (!std::isfinite(c) || c < 0 || c > 1) {throw std::invalid_argument("Debug colors must be finite in [0,1].");}}}
    lines_.insert(lines_.end(),lines.begin(),lines.end());
}
void DebugDraw::DrawLine(Vec3 a,Vec3 b,DebugColor c) {const DebugLine line{a,b,c};Append({&line,1});}
void DebugDraw::DrawBox(Bounds b,DebugColor c,const Mat4& m) {
    Point(b.Min);Point(b.Max);Matrix(m);if (b.Min.X > b.Max.X || b.Min.Y > b.Max.Y || b.Min.Z > b.Max.Z) {throw std::invalid_argument("Debug bounds must be ordered.");}
    std::vector<DebugLine> lines;for (unsigned i=0;i<8;++i) {const auto p=[&](unsigned v) {return m.TransformPoint({v&1?b.Max.X:b.Min.X,v&2?b.Max.Y:b.Min.Y,v&4?b.Max.Z:b.Min.Z});};for (unsigned bit=1;bit<=4;bit<<=1) {if (!(i&bit)) {lines.push_back({p(i),p(i|bit),c});}}}Append(lines);
}
void DebugDraw::DrawSphere(Vec3 p,float r,DebugColor c,unsigned n) {
    Point(p);Radius(r,n);std::vector<DebugLine> lines;
    for (unsigned axis=0;axis<3;++axis) {const auto ring=[&](unsigned i) {Vec3 v;const float a=2*Pi*i/n;float* values[]={&v.X,&v.Y,&v.Z};*values[(axis+1)%3]=std::cos(a)*r;*values[(axis+2)%3]=std::sin(a)*r;return Add(p,v);};for (unsigned i=0;i<n;++i) {lines.push_back({ring(i),ring(i+1),c});}}Append(lines);
}
void DebugDraw::DrawCapsule(Vec3 a,Vec3 b,float r,DebugColor c,unsigned n) {
    Point(a);Point(b);Radius(r,n);if (a.X == b.X && a.Y == b.Y && a.Z == b.Z) {DrawSphere(a,r,c,n);return;}const auto d=Unit(Sub(b,a));const auto x=Unit(Cross(d,std::abs(d.Y)<.9f ? Vec3{0,1,0}:Vec3{1,0,0}));const auto y=Cross(d,x);std::vector<DebugLine> lines;
    const auto ring=[&](Vec3 p,unsigned i) {const float t=2*Pi*i/n;return Add(p,Mul(Add(Mul(x,std::cos(t)),Mul(y,std::sin(t))),r));};
    for (unsigned i=0;i<n;++i) {lines.push_back({ring(a,i),ring(a,i+1),c});lines.push_back({ring(b,i),ring(b,i+1),c});}
    for (unsigned axis=0;axis<4;++axis) {const auto radial=Add(Mul(x,std::cos(axis*Pi/2)),Mul(y,std::sin(axis*Pi/2)));lines.push_back({Add(a,Mul(radial,r)),Add(b,Mul(radial,r)),c});if (axis >= 2) {continue;}for (unsigned i=0;i<n/2;++i) {const auto arc=[&](Vec3 p,float sign,unsigned k) {const float t=Pi*k/(n/2);return Add(p,Mul(Add(Mul(radial,std::cos(t)),Mul(d,sign*std::sin(t))),r));};lines.push_back({arc(a,-1,i),arc(a,-1,i+1),c});lines.push_back({arc(b,1,i),arc(b,1,i+1),c});}}Append(lines);
}
void DebugDraw::DrawArrow(Vec3 a,Vec3 b,DebugColor c) {
    Point(a);Point(b);const auto delta=Sub(b,a);const auto d=Unit(delta),x=Unit(Cross(d,std::abs(d.Y)<.9f ? Vec3{0,1,0}:Vec3{1,0,0})),y=Cross(d,x);const float size=std::hypot(delta.X,delta.Y,delta.Z)*.2f;const auto base=Sub(b,Mul(d,size));
    const DebugLine lines[]={{a,b,c},{b,Add(base,Mul(x,size*.5f)),c},{b,Sub(base,Mul(x,size*.5f)),c},{b,Add(base,Mul(y,size*.5f)),c},{b,Sub(base,Mul(y,size*.5f)),c}};Append(lines);
}
void DebugDraw::DrawAxis(const Mat4& m,float length) {Matrix(m);if (!std::isfinite(length) || length <= 0) {throw std::invalid_argument("Axis length must be finite and positive.");}const auto p=m.TransformPoint({});const DebugLine lines[]={{p,m.TransformPoint({length,0,0}),{1,0,0,1}},{p,m.TransformPoint({0,length,0}),{0,1,0,1}},{p,m.TransformPoint({0,0,length}),{0,0,1,1}}};Append(lines);}
void DebugDraw::DrawCameraFrustum(const Camera& camera,float aspect,DebugColor c) {
    (void)camera.ViewProjection(aspect);const auto z=Unit(Sub(camera.Target,camera.Position)),x=Unit(Cross(z,camera.Up)),y=Cross(x,z);std::array<Vec3,8> p;
    for (unsigned i=0;i<8;++i) {const auto d=i&4 ? camera.FarPlane:camera.NearPlane;const float h=std::tan(camera.VerticalFovRadians*.5f)*d,w=h*aspect;p[i]=Add(Add(Add(camera.Position,Mul(z,d)),Mul(x,i&1?w:-w)),Mul(y,i&2?h:-h));}
    std::vector<DebugLine> lines;for (unsigned i=0;i<8;++i) {for (unsigned bit=1;bit<=4;bit<<=1) {if (!(i&bit)) {lines.push_back({p[i],p[i|bit],c});}}}Append(lines);
}
void DebugDraw::DrawSkeleton(const SkeletonAsset& skeleton,const EvaluatedPose& pose,const Mat4& world,DebugColor c) {
    ValidateSkeleton(skeleton);Matrix(world);if (pose.Global.size() != skeleton.Joints.size()) {throw std::invalid_argument("Debug pose does not match skeleton.");}std::vector<DebugLine> lines;
    for (std::size_t i=0;i<skeleton.Joints.size();++i) {Matrix(pose.Global[i]);const auto parent=skeleton.Joints[i].Parent;if (parent != NoJoint) {lines.push_back({(world*pose.Global[parent]).TransformPoint({}),(world*pose.Global[i]).TransformPoint({}),c});}}Append(lines);
}
BoneInfluenceStatistics InspectBoneInfluences(const ModelAsset& model) {
    BoneInfluenceStatistics s;s.MinimumWeightSum=1e300;
    for (const auto& m:model.Meshes) {for (const auto& p:m.Primitives) {for (const auto& v:p.Geometry.Vertices) {if (!p.HasSkinning) {++s.RigidVertices;continue;}++s.SkinnedVertices;unsigned n=0;double sum=0;for (const float w:v.Weights) {if (!std::isfinite(w) || w < 0) {throw std::invalid_argument("Invalid skin weight in debugger.");}n+=w>0;sum+=w;}++s.InfluenceCounts[n];s.UnweightedVertices+=n==0;s.MinimumWeightSum=std::min(s.MinimumWeightSum,sum);s.MaximumWeightSum=std::max(s.MaximumWeightSum,sum);}}}
    if (!s.SkinnedVertices) {s.MinimumWeightSum=0;}return s;
}
JointDebugInfo InspectJoint(const AnimationPlayer& player,JointId joint) {
    if (!player.Model() || joint >= player.Model()->Skeleton.Joints.size()) {throw std::invalid_argument("Select a valid skeleton node.");}
    const auto pose=player.Evaluate();AnimationPlayer reference;reference.Bind(player.Model());const auto rest=reference.Evaluate();
    JointDebugInfo info{pose.Local.at(joint),pose.Global.at(joint),rest.Local.at(joint),rest.Global.at(joint),{}};
    for (const auto& skin:player.Model()->Skins) {for (std::size_t i=0;i<skin.Joints.size();++i) {if (skin.Joints[i] == joint) {const auto matrix=pose.Global.at(joint)*skin.InverseBindMatrices.at(i);for (float v:matrix.Elements) {if (!std::isfinite(v)) {throw std::invalid_argument("Nonfinite skinning matrix.");}}info.SkinningMatrices.push_back(matrix);}}}return info;
}
} // namespace lv
