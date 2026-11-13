#include <minuvex/Asset.h>
#include "AnimationEvaluation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace lv {
namespace {
void Require(bool ok, const char* message) { if (!ok) { throw std::invalid_argument(message); } }
struct Point { double X, Y; };
double Cross(Point a, Point b, Point c) { return (b.X-a.X)*(c.Y-a.Y)-(b.Y-a.Y)*(c.X-a.X); }
struct Coordinates {
    double X, Y, Scale;
    explicit Coordinates(const BlendTreeDesc& desc) {
        double loX=desc.Samples[0].X,hiX=loX,loY=desc.Samples[0].Y,hiY=loY;
        for (const auto& s:desc.Samples) {loX=std::min(loX,double(s.X));hiX=std::max(hiX,double(s.X));loY=std::min(loY,double(s.Y));hiY=std::max(hiY,double(s.Y));}
        X=(loX+hiX)*.5;Y=(loY+hiY)*.5;Scale=std::max(hiX-loX,hiY-loY);if (Scale==0) {Scale=1;}
    }
    Point operator()(float x,float y) const {return {(double(x)-X)/Scale,(double(y)-Y)/Scale};}
};
bool InCircle(Point a,Point b,Point c,Point p) {
    a={a.X-p.X,a.Y-p.Y};b={b.X-p.X,b.Y-p.Y};c={c.X-p.X,c.Y-p.Y};
    const auto aa=a.X*a.X+a.Y*a.Y,bb=b.X*b.X+b.Y*b.Y,cc=c.X*c.X+c.Y*c.Y;
    return aa*(b.X*c.Y-b.Y*c.X)-bb*(a.X*c.Y-a.Y*c.X)+cc*(a.X*b.Y-a.Y*b.X)>1e-14;
}
using Edge=std::array<std::size_t,2>;
Edge Ordered(std::size_t a,std::size_t b) {return a<b ? Edge{a,b}:Edge{b,a};}
void Triangulate(const BlendTreeDesc& desc,std::vector<std::array<std::size_t,3>>& triangles,std::vector<Edge>& hull) {
    const Coordinates coordinates(desc);std::vector<Point> points;for (const auto& s:desc.Samples) {points.push_back(coordinates(s.X,s.Y));}
    const auto n=points.size();for (std::size_t i=0;i<n;++i) {for (std::size_t j=0;j<i;++j) {Require(std::hypot(points[i].X-points[j].X,points[i].Y-points[j].Y)>1e-7,"Duplicate or numerically coincident 2D samples.");}}
    points.insert(points.end(),{{-16,-8},{16,-8},{0,16}});triangles.push_back({n,n+1,n+2});
    std::vector<std::size_t> order(n);std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return points[a].X==points[b].X ? points[a].Y<points[b].Y:points[a].X<points[b].X;});
    // Bowyer-Watson, CCW facets. Coordinate ordering fixes the diagonal for cocircular samples.
    for (auto index:order) {
        std::map<Edge,unsigned> edges;std::vector<std::array<std::size_t,3>> kept;
        for (const auto& t:triangles) {if (InCircle(points[t[0]],points[t[1]],points[t[2]],points[index])) {for (int k=0;k<3;++k) {++edges[Ordered(t[k],t[(k+1)%3])];}}else {kept.push_back(t);}}
        Require(!edges.empty(),"Cannot construct a stable 2D triangulation.");
        for (const auto& [edge,count]:edges) {if (count!=1) {continue;}auto a=edge[0],b=edge[1];const auto area=Cross(points[a],points[b],points[index]);if (std::abs(area)<=1e-12) {continue;}if (area<0) {std::swap(a,b);}kept.push_back({a,b,index});}triangles=std::move(kept);
    }
    std::erase_if(triangles,[&](const auto& t){return t[0]>=n || t[1]>=n || t[2]>=n;});
    Require(!triangles.empty(),"Blend2D samples must span a nondegenerate area.");std::vector<bool> used(n,false);std::map<Edge,unsigned> edges;
    for (const auto& t:triangles) {Require(Cross(points[t[0]],points[t[1]],points[t[2]])>1e-10,"Numerically degenerate Blend2D triangle.");for (int k=0;k<3;++k) {used[t[k]]=true;++edges[Ordered(t[k],t[(k+1)%3])];}}
    Require(std::all_of(used.begin(),used.end(),[](bool value){return value;}),"Triangulation lost a sample; separate nearly collinear points.");
    for (const auto& [edge,count]:edges) {Require(count<=2,"Overlapping Blend2D facets.");if (count==1) {hull.push_back(edge);}}
}
bool SameSigns(Vec3 a,Vec3 b) {return std::signbit(a.X)==std::signbit(b.X) && std::signbit(a.Y)==std::signbit(b.Y) && std::signbit(a.Z)==std::signbit(b.Z);}
void ValidateSamples(const ModelAsset& model,const BlendTreeDesc& desc) {
    ValidateSkeleton(model.Skeleton);std::vector<Vec3> signs;for (const auto& j:model.Skeleton.Joints) {signs.push_back(j.ReferenceLocal.Scale);}bool first=true;std::map<std::size_t,std::vector<Vec3>> checked;
    for (const auto& sample:desc.Samples) {
        Require(sample.Clip<model.Animations.size(),"Blend sample refers to an unknown clip.");
        if (const auto found=checked.find(sample.Clip);found!=checked.end()) {for (std::size_t i=0;i<signs.size();++i) {Require(SameSigns(signs[i],found->second[i]),"Tree samples have incompatible scale signs.");}continue;}
        const auto& clip=model.Animations[sample.Clip];ValidateAnimation(model.Skeleton,clip);
        std::vector<Vec3> scales;for (const auto& j:model.Skeleton.Joints) {scales.push_back(j.ReferenceLocal.Scale);}
        for (const auto& track:clip.Tracks) {if (track.Path==AnimationPath::Scale) {const auto& value=track.Values[0];scales[track.Joint]={value[0],value[1],value[2]};for (const auto& key:track.Values) {Require(SameSigns(scales[track.Joint],{key[0],key[1],key[2]}),"Tree scale tracks must keep consistent signs.");}}}
        if (first) {signs=scales;first=false;}else {for (std::size_t i=0;i<signs.size();++i) {Require(SameSigns(signs[i],scales[i]),"Tree samples have incompatible scale signs, including reference channels.");}}checked.emplace(sample.Clip,std::move(scales));
    }
}
}

BlendTreeHandle CreateBlendTree(const ModelAsset& model,BlendTreeDesc description) {
    Require(description.Kind==BlendTreeKind::Blend1D || description.Kind==BlendTreeKind::Blend2D,"Unknown Blend Tree kind.");
    const auto name=[](const std::string& s){return !s.empty() && s.size()<=256;};
    Require(name(description.Name) && name(description.ParameterX),"Tree/parameter names must contain 1..256 bytes.");
    const bool two=description.Kind==BlendTreeKind::Blend2D;Require(description.ParameterY.size()<=256 && (!two || (name(description.ParameterY) && description.ParameterX!=description.ParameterY)),"Blend2D requires distinct parameter names of at most 256 bytes.");
    Require(description.Samples.size()>=(two ? 3u:1u) && description.Samples.size()<=MaxBlendSamples,"Blend Tree sample count is invalid (at most 64).");
    for (std::size_t i=0;i<description.Samples.size();++i) {const auto& s=description.Samples[i];Require(std::isfinite(s.X) && std::isfinite(s.Y),"Blend sample coordinates must be finite.");if (!two) {Require(s.Y==0 && (!i || s.X>description.Samples[i-1].X),"Blend1D thresholds must be strictly increasing and Y must be zero.");}}
    ValidateMorphModel(model);ValidateSamples(model,description);
    auto tree=std::shared_ptr<BlendTreeAsset>(new BlendTreeAsset);tree->description_=std::move(description);tree->skeleton_=SkeletonSignature(model.Skeleton);tree->morph_=MorphSignature(model);
    for (const auto& sample:tree->description_.Samples) {tree->clipNames_.push_back(model.Animations[sample.Clip].Name);}
    if (two) {Triangulate(tree->description_,tree->triangles_,tree->hull_);}
    std::uint64_t hash=14695981039346656037ull;const auto number=[&](std::uint64_t value){for (int i=0;i<8;++i) {hash^=(value>>(i*8))&255;hash*=1099511628211ull;}};
    const auto text=[&](const std::string& value){number(value.size());for (unsigned char c:value) {number(c);}};
    number(tree->skeleton_);number(tree->morph_);number(static_cast<unsigned>(tree->description_.Kind));text(tree->description_.Name);text(tree->description_.ParameterX);text(tree->description_.ParameterY);number(tree->description_.Samples.size());
    for (std::size_t i=0;i<tree->description_.Samples.size();++i) {const auto& s=tree->description_.Samples[i];number(s.Clip);number(std::bit_cast<std::uint32_t>(s.X==0 ? 0.0f:s.X));number(std::bit_cast<std::uint32_t>(s.Y==0 ? 0.0f:s.Y));text(tree->clipNames_[i]);}tree->signature_=hash;return tree;
}
void ValidateBlendTree(const ModelAsset& model,const BlendTreeAsset& tree) {
    Require(tree.skeleton_==SkeletonSignature(model.Skeleton) && tree.morph_==MorphSignature(model),"Tree belongs to a different skeleton/morph layout.");
    for (std::size_t i=0;i<tree.description_.Samples.size();++i) {const auto clip=tree.description_.Samples[i].Clip;Require(clip<model.Animations.size() && model.Animations[clip].Name==tree.clipNames_[i],"Tree clip identity changed.");}ValidateSamples(model,tree.description_);
}
std::vector<float> EvaluateBlendWeights(const BlendTreeAsset& tree,float x,float y) {
    Require(std::isfinite(x) && std::isfinite(y),"Blend parameters must be finite.");const auto& desc=tree.description_;const auto& samples=desc.Samples;std::vector<float> result(samples.size(),0);
    if (desc.Kind==BlendTreeKind::Blend1D) {
        if (x<=samples.front().X) {result.front()=1;return result;}if (x>=samples.back().X) {result.back()=1;return result;}
        const auto right=static_cast<std::size_t>(std::upper_bound(samples.begin(),samples.end(),x,[](float value,const auto& s){return value<s.X;})-samples.begin()),left=right-1;
        result[right]=static_cast<float>((double(x)-samples[left].X)/(double(samples[right].X)-samples[left].X));result[left]=1-result[right];return result;
    }
    const Coordinates coordinates(desc);const auto p=coordinates(x,y);std::vector<Point> points;
    for (std::size_t i=0;i<samples.size();++i) {if (x==samples[i].X && y==samples[i].Y) {result[i]=1;return result;}points.push_back(coordinates(samples[i].X,samples[i].Y));}
    for (const auto& t:tree.triangles_) {const auto a=points[t[0]],b=points[t[1]],c=points[t[2]];const auto area=Cross(a,b,c);std::array<double,3> w{Cross(b,c,p)/area,Cross(c,a,p)/area,Cross(a,b,p)/area};
        if (*std::min_element(w.begin(),w.end())>=-1e-9) {double total=0;for (auto& value:w) {value=std::max(0.0,value);total+=value;}for (int k=0;k<3;++k) {result[t[k]]=static_cast<float>(w[k]/total);}return result;}}
    double best=std::numeric_limits<double>::infinity(),factor=0;Edge nearest{};
    for (const auto& edge:tree.hull_) {const auto a=points[edge[0]],b=points[edge[1]];const auto dx=b.X-a.X,dy=b.Y-a.Y,t=std::clamp(((p.X-a.X)*dx+(p.Y-a.Y)*dy)/(dx*dx+dy*dy),0.0,1.0);const auto qx=a.X+t*dx,qy=a.Y+t*dy;
        // Compare squared distances without the common |p|^2 term, retaining accuracy for far-away inputs.
        const auto distance=qx*qx+qy*qy-2*(p.X*qx+p.Y*qy);if (distance<best) {best=distance;factor=t;nearest=edge;}}
    result[nearest[0]]=static_cast<float>(1-factor);result[nearest[1]]=static_cast<float>(factor);return result;
}
namespace detail {
PoseBuffer SampleBlendTree(const ModelAsset& model,const BlendTreeAsset& tree,std::span<const float> weights,double phase) {
    PoseBuffer result;double total=0;
    for (std::size_t i=0;i<weights.size();++i) {if (weights[i]<=0) {continue;}const auto& clip=model.Animations[tree.Description().Samples[i].Clip];auto pose=SampleAnimation(model.Skeleton,&clip,phase*clip.Duration);const auto next=total+weights[i];if (total==0) {result=std::move(pose);}else {result=BlendPoses(model.Skeleton,result,pose,static_cast<float>(weights[i]/next));}total=next;}
    return result;
}
}
PoseBuffer EvaluateBlendTree(const ModelAsset& model,const BlendTreeAsset& tree,float x,float y,double phase) {
    Require(std::isfinite(phase) && phase>=0 && phase<=1,"Normalized tree phase must be in [0,1].");ValidateBlendTree(model,tree);return detail::SampleBlendTree(model,tree,EvaluateBlendWeights(tree,x,y),phase);
}
void AnimationPlayer::PlayBlendTree(BlendTreeHandle tree,bool restart) {
    Require(model_ && tree,"Bind a model and supply a Blend Tree before playing.");ValidateBlendTree(*model_,*tree);
    auto next=*this;
    if (restart || !next.tree_ || next.tree_->Tree!=tree) {const auto& sample=tree->Description().Samples[0];next.tree_=BlendTreeState{tree,speed_<0 ? 1.0:0.0,sample.X,sample.Y};}
    next.clip_.reset();next.blend_.reset();next.time_=0;next.state_=next.BlendTreeDuration()>0 ? PlaybackState::Playing:PlaybackState::Stopped;*this=std::move(next);
}
void AnimationPlayer::SetBlendParameter(std::string_view name,float value) {
    Require(tree_.has_value() && std::isfinite(value),"A tree and finite parameter value are required.");const auto& desc=tree_->Tree->Description();
    if (name==desc.ParameterX) {tree_->X=value;}else if (desc.Kind==BlendTreeKind::Blend2D && name==desc.ParameterY) {tree_->Y=value;}else {throw std::invalid_argument("Unknown Blend Tree parameter.");}
}
void AnimationPlayer::SeekBlendTree(double phase) {Require(tree_ && std::isfinite(phase) && phase>=0 && phase<=1,"Tree seek requires a normalized phase in [0,1].");tree_->Phase=phase;}
std::vector<float> AnimationPlayer::BlendWeights() const {Require(tree_.has_value(),"Select a Blend Tree before querying weights.");return EvaluateBlendWeights(*tree_->Tree,tree_->X,tree_->Y);}
double AnimationPlayer::BlendTreeDuration() const {
    const auto weights=BlendWeights();double duration=0,total=0;for (std::size_t i=0;i<weights.size();++i) {if (weights[i]<=0) {continue;}total+=weights[i];duration=std::lerp(duration,model_->Animations[tree_->Tree->Description().Samples[i].Clip].Duration,double(weights[i])/total);}Require(std::isfinite(duration),"Weighted tree duration exceeds numeric range.");return duration;
}
}
