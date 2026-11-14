#include "UiLayout.h"
#include "NativeText.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace lv::detail {
namespace {
using Matrix=std::array<float,6>;
Matrix Multiply(const Matrix& a,const Matrix& b) {return {a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5]};}
float Limit(const UiElement& e,float value,bool horizontal) {
    const auto& l=e.Layout;const auto& p=e.Typography.Padding;
    const float textMin=e.Kind==UiKind::Panel ? 1:(horizontal ? p[0]+p[2]+1:p[1]+p[3]+1);
    return std::clamp(value,std::max(textMin,horizontal ? l.MinWidth:l.MinHeight),horizontal ? l.MaxWidth:l.MaxHeight);
}
float Offset(UiCrossAlign align,float available,float size) {return align==UiCrossAlign::Center ? std::max(0.0f,(available-size)*.5f):align==UiCrossAlign::End ? std::max(0.0f,available-size):0;}
struct Slot {std::size_t Index;float X=0,Y=0,W=0,H=0;};
class LayoutEngine {
    const std::vector<UiElement>& items_;
    const std::filesystem::path& directory_;
    const UiScrollMap& scroll_;
    std::unordered_map<UiId,std::size_t> indices_;
    std::vector<std::vector<std::size_t>> children_;
    std::map<std::tuple<std::size_t,float,float>,std::array<float,2>> measurements_;
    std::vector<UiResolved> result_;
    std::array<float,2> Measure(std::size_t i,float availableW,float availableH) {
        const auto key=std::make_tuple(i,availableW,availableH);if (const auto it=measurements_.find(key);it!=measurements_.end()) {return it->second;}
        const auto& e=items_[i];const auto& l=e.Layout;const auto& c=e.Container;
        float w=Limit(e,l.WidthSizing==UiSizing::Fill ? availableW:l.Width,true),h=Limit(e,l.HeightSizing==UiSizing::Fill ? availableH:l.Height,false);
        if (!e.Visible) {return {w,h};}
        if (l.WidthSizing==UiSizing::Hug||l.HeightSizing==UiSizing::Hug) {
            float naturalW=1,naturalH=1;
            if (e.Kind!=UiKind::Panel&&HasText(e)) {auto text=e;if (l.WidthSizing==UiSizing::Hug) {text.Typography.Wrap=false;}const auto measured=NativeMeasure(text,directory_,l.WidthSizing==UiSizing::Hug ? l.MaxWidth:w);naturalW=measured[0];naturalH=measured[1];}
            if (e.Kind==UiKind::Panel) {
                float cw=0,ch=0,absoluteW=0,absoluteH=0;std::size_t count=0;const float innerW=std::max(0.0f,(l.WidthSizing==UiSizing::Hug ? l.MaxWidth:w)-c.Padding[0]-c.Padding[2]),innerH=std::max(0.0f,(l.HeightSizing==UiSizing::Hug ? l.MaxHeight:h)-c.Padding[1]-c.Padding[3]);
                for (auto child:children_[i]) {const auto& e2=items_[child];if (!e2.Visible) {continue;}const auto size=Measure(child,innerW,innerH);
                    if (c.Flow==UiFlow::Absolute||e2.Layout.Absolute) {absoluteW=std::max(absoluteW,e2.Layout.X+size[0]*(1-e2.Layout.PivotX));absoluteH=std::max(absoluteH,e2.Layout.Y+size[1]*(1-e2.Layout.PivotY));continue;}
                    if (c.Flow==UiFlow::Row) {cw+=size[0];ch=std::max(ch,size[1]);}else if (c.Flow==UiFlow::Column) {cw=std::max(cw,size[0]);ch+=size[1];}else {cw=std::max(cw,size[0]);ch=std::max(ch,size[1]);}++count;
                }
                if (count) {if (c.Flow==UiFlow::Row) {cw+=c.GapX*(count-1);}if (c.Flow==UiFlow::Column) {ch+=c.GapY*(count-1);}if (c.Flow==UiFlow::Grid) {const auto columns=std::min<std::size_t>(c.Columns,count),rows=(count+columns-1)/columns;cw=cw*columns+c.GapX*(columns-1);ch=ch*rows+c.GapY*(rows-1);}}
                naturalW=std::max(cw,absoluteW)+c.Padding[0]+c.Padding[2];naturalH=std::max(ch,absoluteH)+c.Padding[1]+c.Padding[3];
            }
            if (l.WidthSizing==UiSizing::Hug) {w=Limit(e,naturalW,true);}if (l.HeightSizing==UiSizing::Hug) {h=Limit(e,naturalH,false);}
            if (l.HeightSizing==UiSizing::Hug&&e.Kind!=UiKind::Panel&&HasText(e)&&e.Typography.Wrap) {h=Limit(e,NativeMeasure(e,directory_,w)[1],false);}
        }
        return measurements_.emplace(key,std::array{w,h}).first->second;
    }
    void Flex(std::vector<Slot>& slots,float available,bool horizontal,float gap) {
        float remaining=available-gap*static_cast<float>(slots.empty() ? 0:slots.size()-1);std::vector<std::size_t> active;
        for (std::size_t i=0;i<slots.size();++i) {const auto& s=slots[i];const auto& e=items_[s.Index];const auto sizing=horizontal ? e.Layout.WidthSizing:e.Layout.HeightSizing;if (sizing==UiSizing::Fill) {active.push_back(i);}else {remaining-=horizontal ? s.W:s.H;}}
        while (!active.empty()) {float weight=0;for (auto i:active) {weight+=items_[slots[i].Index].Layout.Flex;}float frozen=0;bool limited=false;std::vector<std::size_t> next;
            for (auto i:active) {auto& s=slots[i];const auto& e=items_[s.Index];const float desired=remaining*e.Layout.Flex/weight;const float size=Limit(e,desired,horizontal);(horizontal ? s.W:s.H)=size;if (std::abs(size-desired)>.001f) {frozen+=size;limited=true;}else {next.push_back(i);}}
            if (!limited) {break;}remaining-=frozen;active=std::move(next);
        }
    }
    void Place(std::size_t index,float w,float h,float x,float y,const Matrix& parent,int parentIndex,bool visible,bool enabled,bool flowPosition) {
        const auto& e=items_[index];const auto& l=e.Layout;const auto& c=e.Container;const float px=l.PivotX*w,py=l.PivotY*h;
        // Absolute X/Y locate the pivot; a flow slot locates the untransformed top-left.
        if (flowPosition) {x+=px;y+=py;}const float co=std::cos(l.Rotation),si=std::sin(l.Rotation);
        const Matrix local{co*l.ScaleX,si*l.ScaleX,-si*l.ScaleY,co*l.ScaleY,x-co*l.ScaleX*px+si*l.ScaleY*py,y-si*l.ScaleX*px-co*l.ScaleY*py};
        UiResolved r;r.Element=e;r.Element.Layout.Width=w;r.Element.Layout.Height=h;r.Parent=parentIndex;r.Box.Id=e.Id;r.Box.Width=w;r.Box.Height=h;r.Box.Visible=visible&&e.Visible;r.Box.Enabled=enabled&&e.Enabled;r.Box.Transform=Multiply(parent,local);
        const auto& m=r.Box.Transform;const float determinant=m[0]*m[3]-m[1]*m[2];if (!std::isfinite(determinant)||std::abs(determinant)<1e-12f) {throw std::invalid_argument("UI hierarchy transform is singular or too small.");}for (auto value:m) {if (!std::isfinite(value)||std::abs(value)>1e12f) {throw std::invalid_argument("UI hierarchy transform exceeds supported range.");}}
        const float iw=std::max(0.0f,w-c.Padding[0]-c.Padding[2]),ih=std::max(0.0f,h-c.Padding[1]-c.Padding[3]);std::vector<Slot> slots,absolute;
        for (auto child:children_[index]) {const auto& e2=items_[child];const auto size=r.Box.Visible&&e2.Visible ? Measure(child,iw,ih):std::array{Limit(e2,e2.Layout.Width,true),Limit(e2,e2.Layout.Height,false)};Slot slot{child,0,0,size[0],size[1]};if (c.Flow==UiFlow::Absolute||e2.Layout.Absolute||!e2.Visible||!r.Box.Visible) {slot.X=e2.Layout.AnchorX*iw+e2.Layout.X-e2.Layout.PivotX*size[0];slot.Y=e2.Layout.AnchorY*ih+e2.Layout.Y-e2.Layout.PivotY*size[1];absolute.push_back(slot);}else {slots.push_back(slot);}}
        if (c.Flow==UiFlow::Row||c.Flow==UiFlow::Column) {
            const bool row=c.Flow==UiFlow::Row;const float gap=row ? c.GapX:c.GapY;Flex(slots,row ? iw:ih,row,gap);float used=gap*static_cast<float>(slots.empty() ? 0:slots.size()-1);for (const auto& s:slots) {used+=row ? s.W:s.H;}const float free=std::max(0.0f,(row ? iw:ih)-used);float cursor=c.Justify==UiJustify::Center ? free*.5f:c.Justify==UiJustify::End ? free:0,extra=0;
            if (c.Justify==UiJustify::SpaceBetween&&slots.size()>1) {extra=free/(slots.size()-1);}if (c.Justify==UiJustify::SpaceAround&&!slots.empty()) {extra=free/slots.size();cursor=extra*.5f;}
            for (auto& s:slots) {const auto& e2=items_[s.Index];if (row) {if (e2.Layout.HeightSizing==UiSizing::Hug&&e2.Kind!=UiKind::Panel&&HasText(e2)) {s.H=Limit(e2,NativeMeasure(e2,directory_,s.W)[1],false);}if (c.Align==UiCrossAlign::Stretch||e2.Layout.HeightSizing==UiSizing::Fill) {s.H=Limit(e2,ih,false);}s.X=cursor;s.Y=Offset(c.Align,ih,s.H);cursor+=s.W+gap+extra;}else {if (c.Align==UiCrossAlign::Stretch||e2.Layout.WidthSizing==UiSizing::Fill) {s.W=Limit(e2,iw,true);}s.X=Offset(c.Align,iw,s.W);s.Y=cursor;cursor+=s.H+gap+extra;}}
        }else if (c.Flow==UiFlow::Grid&&!slots.empty()) {
            const auto columns=std::min<std::size_t>(c.Columns,slots.size()),rows=(slots.size()+columns-1)/columns;const float cellW=std::max(0.0f,(iw-c.GapX*(columns-1))/columns),cellH=std::max(0.0f,(ih-c.GapY*(rows-1))/rows);
            for (std::size_t i=0;i<slots.size();++i) {auto& s=slots[i];const auto& e2=items_[s.Index];if (e2.Layout.WidthSizing==UiSizing::Fill||c.Align==UiCrossAlign::Stretch) {s.W=Limit(e2,cellW,true);}if (e2.Layout.HeightSizing==UiSizing::Fill||c.Align==UiCrossAlign::Stretch) {s.H=Limit(e2,cellH,false);}s.X=(i%columns)*(cellW+c.GapX)+Offset(c.Align,cellW,s.W);s.Y=(i/columns)*(cellH+c.GapY)+Offset(c.Align,cellH,s.H);}
        }
        slots.insert(slots.end(),absolute.begin(),absolute.end());float extentW=iw,extentH=ih;for (const auto& s:slots) {if (!items_[s.Index].Visible) {continue;}extentW=std::max(extentW,s.X+s.W);extentH=std::max(extentH,s.Y+s.H);}r.Box.ContentWidth=extentW;r.Box.ContentHeight=extentH;
        if (c.Overflow==UiOverflow::Scroll) {if (const auto it=scroll_.find(e.Id);it!=scroll_.end()) {r.Box.ScrollX=std::clamp(it->second[0],0.0f,extentW-iw);r.Box.ScrollY=std::clamp(it->second[1],0.0f,extentH-ih);}}
        const auto outIndex=static_cast<int>(result_.size());result_.push_back(r);
        std::stable_sort(slots.begin(),slots.end(),[&](const auto& a,const auto& b){const int za=items_[a.Index].Layout.Z,zb=items_[b.Index].Layout.Z;return za==zb ? a.Index<b.Index:za<zb;});
        for (const auto& s:slots) {Place(s.Index,s.W,s.H,c.Padding[0]+s.X-r.Box.ScrollX,c.Padding[1]+s.Y-r.Box.ScrollY,r.Box.Transform,outIndex,r.Box.Visible,r.Box.Enabled,true);}
    }
public:
    LayoutEngine(const std::vector<UiElement>& items,const std::filesystem::path& directory,const UiScrollMap& scroll):items_(items),directory_(directory),scroll_(scroll),children_(items.size()) {for (std::size_t i=0;i<items.size();++i) {indices_[items[i].Id]=i;}for (std::size_t i=0;i<items.size();++i) {if (items[i].Parent) {children_[indices_.at(items[i].Parent)].push_back(i);}}for (auto& children:children_) {std::stable_sort(children.begin(),children.end(),[&](auto a,auto b){return items_[a].Layout.Order<items_[b].Layout.Order;});}}
    std::vector<UiResolved> Run(float width,float height) {std::vector<std::size_t> roots;for (std::size_t i=0;i<items_.size();++i) {if (!items_[i].Parent) {roots.push_back(i);}}std::stable_sort(roots.begin(),roots.end(),[&](auto a,auto b){return items_[a].Layout.Z<items_[b].Layout.Z;});for (auto i:roots) {const auto size=Measure(i,width,height);Place(i,size[0],size[1],items_[i].Layout.X+items_[i].Layout.AnchorX*width,items_[i].Layout.Y+items_[i].Layout.AnchorY*height,{1,0,0,1,0,0},-1,true,true,false);}return std::move(result_);}
};
}
void ValidateUiTree(const std::vector<UiElement>& items) {
    std::unordered_map<UiId,const UiElement*> indices;for (const auto& e:items) {if (!e.Id||!indices.emplace(e.Id,&e).second) {throw std::invalid_argument("UI IDs must be unique and nonzero.");}}
    for (const auto& e:items) {auto parent=e.Parent;int depth=0;while (parent) {const auto it=indices.find(parent);if (it==indices.end()||it->second->Kind!=UiKind::Panel) {throw std::invalid_argument("UI parent must be an existing panel.");}if (parent==e.Id||++depth>32) {throw std::invalid_argument("UI hierarchy contains a cycle or exceeds depth 32.");}parent=it->second->Parent;}
        if (e.Parent) {const auto& p=*indices.at(e.Parent);if ((p.Layout.WidthSizing==UiSizing::Hug&&e.Layout.WidthSizing==UiSizing::Fill)||(p.Layout.HeightSizing==UiSizing::Hug&&e.Layout.HeightSizing==UiSizing::Fill)) {throw std::invalid_argument("Fill child cannot depend on a Hug parent on the same axis.");}if (p.Container.Flow==UiFlow::Absolute||e.Layout.Absolute) {if ((p.Layout.WidthSizing==UiSizing::Hug&&e.Layout.AnchorX!=0)||(p.Layout.HeightSizing==UiSizing::Hug&&e.Layout.AnchorY!=0)) {throw std::invalid_argument("Absolute child cannot anchor to a Hug parent on the same axis.");}}}
    }
}
std::vector<UiResolved> BuildUiLayout(const std::vector<UiElement>& items,const std::filesystem::path& directory,float width,float height,const UiScrollMap& scroll) {return LayoutEngine(items,directory,scroll).Run(width,height);}
std::array<float,2> UiLocal(const UiLayoutBox& box,float x,float y) {const auto& m=box.Transform;x-=m[4];y-=m[5];const float d=m[0]*m[3]-m[1]*m[2];return {(m[3]*x-m[2]*y)/d,(-m[1]*x+m[0]*y)/d};}
std::array<float,2> UiWorld(const UiLayoutBox& box,float x,float y) {const auto& m=box.Transform;return {m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5]};}
bool UiClipped(const std::vector<UiResolved>& resolved,std::size_t index,float x,float y) {
    int i=static_cast<int>(index);while (i>=0) {const auto& r=resolved[static_cast<std::size_t>(i)];if (!r.Box.Visible) {return true;}if (r.Element.Clip) {const auto& c=*r.Element.Clip;if (x<c.X||y<c.Y||x>=c.X+c.Width||y>=c.Y+c.Height) {return true;}}
        if (i!=static_cast<int>(index)&&r.Element.Container.Overflow!=UiOverflow::Visible) {const auto xy=UiLocal(r.Box,x,y);const auto& p=r.Element.Container.Padding;if (xy[0]<p[0]||xy[1]<p[1]||xy[0]>=r.Box.Width-p[2]||xy[1]>=r.Box.Height-p[3]) {return true;}}i=r.Parent;
    }return false;
}
}
