#include <minvex/Ui.h>
#include "NativeText.h"
#include "UiLayout.h"
#include "core/JsonFile.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <cctype>
#include <deque>

namespace lv {
namespace {
constexpr std::size_t MaxElements=512,MaxText=16384,MaxQueue=1024;
struct UiBudgetExceeded : std::runtime_error {using std::runtime_error::runtime_error;};
Error ErrorFromException(ErrorCode code=ErrorCode::InvalidArgument,const std::filesystem::path& path={}) {
    try {throw;}
    catch (const std::bad_alloc& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce UI size or text length.",path};}
    catch (const detail::NativeTextUnavailable& e) {return {ErrorCode::UnsupportedFeature,e.what(),"Use native Windows or macOS typography.",path};}
    catch (const UiBudgetExceeded& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce UI content or rich text overrides.",path};}
    catch (const std::exception& e) {return {code,e.what(),"Check UI values, font files and native platform support.",path};}
}
template<class T> Result<T> Fail(ErrorCode code,const char* message) {return Result<T>::Failure({code,message,"Use valid UI IDs and the canvas owner thread.",{}});}
void Require(bool condition,const char* message) {if (!condition) {throw std::invalid_argument(message);}}
bool Utf8(const std::string& s) {
    for (std::size_t i=0;i<s.size();) {
        const auto c=static_cast<unsigned char>(s[i++]);if (c==0) {return false;}if (c<128) {continue;}
        int n=c>=0xc2 && c<=0xdf ? 1:c>=0xe0 && c<=0xef ? 2:c>=0xf0 && c<=0xf4 ? 3:-1;
        if (n<0 || i+static_cast<std::size_t>(n)>s.size()) {return false;}
        std::uint32_t cp=c & ((1u<<(6-n))-1);for (int j=0;j<n;++j) {const auto b=static_cast<unsigned char>(s[i++]);if ((b&0xc0)!=0x80) {return false;}cp=(cp<<6)|(b&63);}
        if ((n==1 && cp<128)||(n==2 && cp<2048)||(n==3 && cp<65536)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff)) {return false;}
    }return true;
}
void Range(float value,float low,float high) {Require(std::isfinite(value)&&value>=low&&value<=high,"UI number is outside its finite supported range.");}
void Color(const UiColor& color) {for (auto c:color) {Range(c,0,1);}}
void ValidateStyle(const UiStyle& s) {
    Color(s.Text);Color(s.Border);Color(s.Shadow);for (const auto& c:s.Fill) {Color(c);}for (auto r:s.Radius) {Range(r,0,4096);}
    Range(s.BorderWidth,0,1024);Range(s.ShadowX,-1024,1024);Range(s.ShadowY,-1024,1024);Range(s.ShadowBlur,0,128);Range(s.Opacity,0,1);
}
void Validate(const UiElement& e) {
    Require(e.Name.size()<=256 && e.Text.size()<=MaxText && e.Typography.Family.size()<=256,"UI name/family/text is too long.");
    Require(Utf8(e.Name)&&Utf8(e.Text)&&Utf8(e.Typography.Family),"UI strings must be valid UTF-8 without NUL.");
    Require(e.Kind>=UiKind::Text&&e.Kind<=UiKind::Panel,"Unknown UI kind.");
    const auto& l=e.Layout;Range(l.X,-65536,65536);Range(l.Y,-65536,65536);Range(l.Width,1,2048);Range(l.Height,1,2048);
    Range(l.AnchorX,0,1);Range(l.AnchorY,0,1);Range(l.PivotX,0,1);Range(l.PivotY,0,1);Range(l.Rotation,-1000,1000);Range(l.ScaleX,.01f,16);Range(l.ScaleY,.01f,16);
    Require(l.Z>=-1000000&&l.Z<=1000000&&l.Order>=-1000000&&l.Order<=1000000,"UI Z/order is out of range.");
    Require(l.WidthSizing>=UiSizing::Fixed&&l.WidthSizing<=UiSizing::Hug&&l.HeightSizing>=UiSizing::Fixed&&l.HeightSizing<=UiSizing::Hug,"Unknown UI sizing.");
    Range(l.MinWidth,1,2048);Range(l.MinHeight,1,2048);Range(l.MaxWidth,l.MinWidth,2048);Range(l.MaxHeight,l.MinHeight,2048);Range(l.Flex,.001f,1000);
    const auto& c=e.Container;Require(c.Flow>=UiFlow::Absolute&&c.Flow<=UiFlow::Grid&&c.Justify>=UiJustify::Start&&c.Justify<=UiJustify::SpaceAround&&c.Align>=UiCrossAlign::Start&&c.Align<=UiCrossAlign::Stretch&&c.Overflow>=UiOverflow::Visible&&c.Overflow<=UiOverflow::Scroll,"Unknown container layout choice.");for (auto p:c.Padding) {Range(p,0,1024);}Range(c.GapX,0,2048);Range(c.GapY,0,2048);Require(c.Columns>=1&&c.Columns<=64,"Grid columns must be 1..64.");Require(e.Kind==UiKind::Panel||c.Flow==UiFlow::Absolute,"Only a panel can define a flow container.");
    const auto& t=e.Typography;Range(t.Size,1,256);Range(t.LineHeight,.5f,4);Range(t.LetterSpacing,-32,128);Require(t.Weight>=100&&t.Weight<=900,"Font weight must be 100..900.");
    Require(t.Horizontal>=UiAlign::Start&&t.Horizontal<=UiAlign::End&&t.Vertical>=UiAlign::Start&&t.Vertical<=UiAlign::End,"Unknown text alignment.");
    for (auto p:t.Padding) {Range(p,0,1024);}Require(e.Kind==UiKind::Panel||(t.Padding[0]+t.Padding[2]<l.Width&&t.Padding[1]+t.Padding[3]<l.Height),"Text padding must leave positive content area.");
    Require(!t.FontFile.is_absolute()&&!t.FontFile.has_root_path(),"FontFile must be relative to Font directory.");for (const auto& p:t.FontFile) {Require(p!="..","FontFile cannot escape Font directory.");}
    Require(t.FontFile.generic_u8string().size()<=4096,"FontFile exceeds 4096 UTF-8 bytes.");
    Require(e.Kind==UiKind::Panel||(l.MaxWidth>t.Padding[0]+t.Padding[2]&&l.MaxHeight>t.Padding[1]+t.Padding[3]),"Sizing limits must leave a positive text content area.");
    Require(e.Runs.size()<=128,"An element supports at most 128 rich text runs.");std::size_t bytes=0;
    for (const auto& r:e.Runs) {bytes+=r.Text.size();Require(bytes<=MaxText&&Utf8(r.Text),"Rich text must be valid UTF-8 and at most 16384 bytes total.");if (r.Family) {Require(r.Family->size()<=256&&Utf8(*r.Family),"Invalid rich text font family.");}if (r.FontFile) {Require(r.FontFile->generic_u8string().size()<=4096,"Run font path exceeds 4096 UTF-8 bytes.");Require(!r.FontFile->is_absolute()&&!r.FontFile->has_root_path(),"Run font must be relative.");for (const auto& p:*r.FontFile) {Require(p!="..","Run font cannot escape Font directory.");}}if (r.Size) {Range(*r.Size,1,256);}if (r.Weight) {Require(*r.Weight>=100&&*r.Weight<=900,"Run weight must be 100..900.");}if (r.LetterSpacing) {Range(*r.LetterSpacing,-32,128);}if (r.Color) {Color(*r.Color);}}
    ValidateStyle(e.Normal);for (auto* s:{&e.Hover,&e.Pressed,&e.Focused,&e.Disabled}) {if (*s) {ValidateStyle(**s);}}
    if (e.Clip) {Range(e.Clip->X,-65536,65536);Range(e.Clip->Y,-65536,65536);Range(e.Clip->Width,0,65536);Range(e.Clip->Height,0,65536);}
}
struct Geometry {
    UiLayoutBox Box;float W,H,SX,SY;
    explicit Geometry(const UiLayoutBox& b):Box(b),W(b.Width),H(b.Height),SX(std::hypot(b.Transform[0],b.Transform[1])),SY(std::hypot(b.Transform[2],b.Transform[3])) {}
    std::array<float,2> Local(float x,float y) const {return detail::UiLocal(Box,x,y);}
    std::array<float,2> World(float x,float y) const {return detail::UiWorld(Box,x,y);}
};
float Distance(float x,float y,float w,float h,const std::array<float,4>& radii) {
    const int i=y<h*.5f ? (x<w*.5f ? 0:1):(x<w*.5f ? 3:2);const float r=std::min({radii[i],w*.5f,h*.5f});
    const float qx=std::abs(x-w*.5f)-w*.5f+r,qy=std::abs(y-h*.5f)-h*.5f+r;
    return std::sqrt(std::max(qx,0.0f)*std::max(qx,0.0f)+std::max(qy,0.0f)*std::max(qy,0.0f))+std::min(std::max(qx,qy),0.0f)-r;
}
void Blend(std::uint8_t* pixel,const UiColor& c,float coverage) {
    const float a=std::clamp(c[3]*coverage,0.0f,1.0f);for (int k=0;k<3;++k) {pixel[k]=static_cast<std::uint8_t>(std::lround(c[k]*a*255+pixel[k]*(1-a)));}pixel[3]=static_cast<std::uint8_t>(std::lround(a*255+pixel[3]*(1-a)));
}
void Composite(std::uint8_t* target,const std::uint8_t* source,float opacity) {
    const float alpha=source[3]/255.0f*opacity;for (int k=0;k<4;++k) {target[k]=static_cast<std::uint8_t>(std::lround(source[k]*opacity+target[k]*(1-alpha)));}
}
using Json=nlohmann::json;
std::string PathText(const std::filesystem::path& p) {const auto s=p.generic_u8string();return {s.begin(),s.end()};}
Json StyleJson(const UiStyle& s) {return {{"text",s.Text},{"fill",s.Fill},{"radius",s.Radius},{"border",s.Border},{"shadow",s.Shadow},{"borderWidth",s.BorderWidth},{"shadowX",s.ShadowX},{"shadowY",s.ShadowY},{"shadowBlur",s.ShadowBlur},{"opacity",s.Opacity}};}
UiStyle ReadStyle(const Json& j) {UiStyle s;s.Text=j.at("text").get<UiColor>();s.Fill=j.at("fill").get<std::array<UiColor,4>>();s.Radius=j.at("radius").get<std::array<float,4>>();s.Border=j.at("border").get<UiColor>();s.Shadow=j.at("shadow").get<UiColor>();s.BorderWidth=j.at("borderWidth");s.ShadowX=j.at("shadowX");s.ShadowY=j.at("shadowY");s.ShadowBlur=j.at("shadowBlur");s.Opacity=j.at("opacity");return s;}
int Integer(const Json& j) {Require(j.is_number_integer(),"Expected UI integer.");if (j.is_number_unsigned()) {Require(j.get<std::uint64_t>()<=static_cast<std::uint64_t>(std::numeric_limits<int>::max()),"UI integer out of range.");}const auto v=j.get<std::int64_t>();Require(v>=std::numeric_limits<int>::min()&&v<=std::numeric_limits<int>::max(),"UI integer out of range.");return static_cast<int>(v);}
UiId Identifier(const Json& j) {Require(j.is_number_integer()&&(!j.is_number_integer()||j.is_number_unsigned()||j.get<std::int64_t>()>=0),"Expected nonnegative UI identifier.");return j.get<UiId>();}
Json RunJson(const UiTextRun& r) {Json j={{"text",r.Text}};
#define MVX_RUN_SAVE(field,key) if (r.field) {j[key]=*r.field;}
    MVX_RUN_SAVE(Family,"family") MVX_RUN_SAVE(Size,"size") MVX_RUN_SAVE(Weight,"weight") MVX_RUN_SAVE(LetterSpacing,"letterSpacing") MVX_RUN_SAVE(Italic,"italic") MVX_RUN_SAVE(Underline,"underline") MVX_RUN_SAVE(StrikeThrough,"strike") MVX_RUN_SAVE(Color,"color")
#undef MVX_RUN_SAVE
    if (r.FontFile) {j["file"]=PathText(*r.FontFile);}return j;
}
UiTextRun ReadRun(const Json& j) {UiTextRun r;r.Text=j.at("text");if (j.contains("family")) {r.Family=j.at("family").get<std::string>();}if (j.contains("file")) {const auto s=j.at("file").get<std::string>();r.FontFile=std::filesystem::path(std::u8string(s.begin(),s.end()));}
    if (j.contains("size")) {r.Size=j.at("size").get<float>();}if (j.contains("weight")) {r.Weight=Integer(j.at("weight"));}if (j.contains("letterSpacing")) {r.LetterSpacing=j.at("letterSpacing").get<float>();}if (j.contains("color")) {r.Color=j.at("color").get<UiColor>();}if (j.contains("italic")) {r.Italic=j.at("italic").get<bool>();}if (j.contains("underline")) {r.Underline=j.at("underline").get<bool>();}if (j.contains("strike")) {r.StrikeThrough=j.at("strike").get<bool>();}return r;
}
Json ElementJson(const UiElement& e) {
    const auto& l=e.Layout;const auto& t=e.Typography;
    Json j={{"kind",static_cast<int>(e.Kind)},{"name",e.Name},{"text",e.Text},{"layout",{l.X,l.Y,l.Width,l.Height,l.AnchorX,l.AnchorY,l.PivotX,l.PivotY,l.Rotation,l.ScaleX,l.ScaleY}},{"z",l.Z},
        {"typography",{{"family",t.Family},{"file",PathText(t.FontFile)},{"size",t.Size},{"lineHeight",t.LineHeight},{"letterSpacing",t.LetterSpacing},{"weight",t.Weight},{"italic",t.Italic},{"underline",t.Underline},{"strike",t.StrikeThrough},{"wrap",t.Wrap},{"horizontal",static_cast<int>(t.Horizontal)},{"vertical",static_cast<int>(t.Vertical)},{"padding",t.Padding}}},
        {"normal",StyleJson(e.Normal)},{"visible",e.Visible},{"enabled",e.Enabled},{"blocksInput",e.BlocksInput}};
    j["key"]=e.Id;j["parent"]=e.Parent;j["sizing"]={{"width",static_cast<int>(l.WidthSizing)},{"height",static_cast<int>(l.HeightSizing)},{"minWidth",l.MinWidth},{"minHeight",l.MinHeight},{"maxWidth",l.MaxWidth},{"maxHeight",l.MaxHeight},{"flex",l.Flex},{"absolute",l.Absolute},{"order",l.Order}};const auto& c=e.Container;
    j["container"]={{"flow",static_cast<int>(c.Flow)},{"padding",c.Padding},{"gapX",c.GapX},{"gapY",c.GapY},{"columns",c.Columns},{"justify",static_cast<int>(c.Justify)},{"align",static_cast<int>(c.Align)},{"overflow",static_cast<int>(c.Overflow)}};j["runs"]=Json::array();for (const auto& r:e.Runs) {j["runs"].push_back(RunJson(r));}
    if (e.Clip) {j["clip"]={e.Clip->X,e.Clip->Y,e.Clip->Width,e.Clip->Height};}
    if (e.Hover) {j["hover"]=StyleJson(*e.Hover);}if (e.Pressed) {j["pressed"]=StyleJson(*e.Pressed);}if (e.Focused) {j["focused"]=StyleJson(*e.Focused);}if (e.Disabled) {j["disabled"]=StyleJson(*e.Disabled);}return j;
}
UiElement ReadElement(const Json& j) {
    UiElement e;e.Kind=static_cast<UiKind>(Integer(j.at("kind")));e.Name=j.at("name");e.Text=j.at("text");const auto a=j.at("layout").get<std::array<float,11>>();auto& l=e.Layout;
    l={a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],Integer(j.at("z"))};const auto& o=j.at("typography");auto& t=e.Typography;
    t.Family=o.at("family");const auto p=o.at("file").get<std::string>();t.FontFile=std::filesystem::path(std::u8string(p.begin(),p.end()));t.Size=o.at("size");t.LineHeight=o.at("lineHeight");t.LetterSpacing=o.at("letterSpacing");t.Weight=Integer(o.at("weight"));t.Italic=o.at("italic");t.Underline=o.at("underline");t.StrikeThrough=o.at("strike");t.Wrap=o.at("wrap");t.Horizontal=static_cast<UiAlign>(Integer(o.at("horizontal")));t.Vertical=static_cast<UiAlign>(Integer(o.at("vertical")));t.Padding=o.at("padding").get<std::array<float,4>>();
    e.Normal=ReadStyle(j.at("normal"));e.Visible=j.at("visible");e.Enabled=j.at("enabled");e.BlocksInput=j.at("blocksInput");if (j.contains("clip")) {const auto c=j.at("clip").get<std::array<float,4>>();e.Clip=UiRect{c[0],c[1],c[2],c[3]};}
    if (j.contains("parent")) {e.Parent=Identifier(j.at("parent"));}if (j.contains("sizing")) {const auto& v=j.at("sizing");l.WidthSizing=static_cast<UiSizing>(Integer(v.at("width")));l.HeightSizing=static_cast<UiSizing>(Integer(v.at("height")));l.MinWidth=v.at("minWidth");l.MinHeight=v.at("minHeight");l.MaxWidth=v.at("maxWidth");l.MaxHeight=v.at("maxHeight");l.Flex=v.at("flex");l.Absolute=v.at("absolute");if (v.contains("order")) {l.Order=Integer(v.at("order"));}}
    if (j.contains("container")) {const auto& v=j.at("container");auto& c=e.Container;c.Flow=static_cast<UiFlow>(Integer(v.at("flow")));c.Padding=v.at("padding").get<std::array<float,4>>();c.GapX=v.at("gapX");c.GapY=v.at("gapY");c.Columns=Integer(v.at("columns"));c.Justify=static_cast<UiJustify>(Integer(v.at("justify")));c.Align=static_cast<UiCrossAlign>(Integer(v.at("align")));c.Overflow=static_cast<UiOverflow>(Integer(v.at("overflow")));}
    if (j.contains("runs")) {Require(j.at("runs").is_array()&&j.at("runs").size()<=128,"Invalid rich text run count.");for (const auto& r:j.at("runs")) {e.Runs.push_back(ReadRun(r));}}
    if (j.contains("hover")) {e.Hover=ReadStyle(j.at("hover"));}if (j.contains("pressed")) {e.Pressed=ReadStyle(j.at("pressed"));}if (j.contains("focused")) {e.Focused=ReadStyle(j.at("focused"));}if (j.contains("disabled")) {e.Disabled=ReadStyle(j.at("disabled"));}Validate(e);return e;
}
}
struct UiCanvas::Impl {
    std::thread::id Owner=std::this_thread::get_id();std::vector<UiElement> Items;std::vector<UiId> Events;
    std::filesystem::path Directory="Font";UiId Next=1,Hover=0,Capture=0,Focus=0,KeyCapture=0;std::uint64_t Version=1;
    detail::UiScrollMap Scroll;
    struct Snapshot {std::vector<UiElement> Items;std::filesystem::path Directory;std::uint64_t State;};
    std::deque<Snapshot> Past,Future;
    std::optional<Snapshot> Group;
    std::uint64_t State=1,Saved=1,Serial=1;
    std::uint64_t LayoutEpoch=1;
    mutable std::uint64_t CachedEpoch=0;
    mutable float CachedWidth=-1,CachedHeight=-1;
    mutable std::vector<detail::UiResolved> CachedLayout;
    auto Find(UiId id) {return std::find_if(Items.begin(),Items.end(),[=](const auto& e){return e.Id==id;});}
    auto Find(UiId id) const {return std::find_if(Items.begin(),Items.end(),[=](const auto& e){return e.Id==id;});}
    const std::vector<detail::UiResolved>& Resolve(float width,float height) const {if (CachedEpoch!=LayoutEpoch||CachedWidth!=width||CachedHeight!=height) {auto candidate=detail::BuildUiLayout(Items,Directory,width,height,Scroll);CachedLayout=std::move(candidate);CachedWidth=width;CachedHeight=height;CachedEpoch=LayoutEpoch;}return CachedLayout;}
    Snapshot CaptureState() const {return {Items,Directory,State};}
    static std::size_t Bytes(const Snapshot& s) {std::size_t n=sizeof(s)+s.Directory.native().size()*sizeof(std::filesystem::path::value_type);for (const auto& e:s.Items) {n+=sizeof(e)+e.Name.size()+e.Text.size()+e.Typography.Family.size()+e.Typography.FontFile.native().size()*sizeof(std::filesystem::path::value_type);for (const auto& r:e.Runs) {n+=sizeof(r)+r.Text.size()+(r.Family ? r.Family->size():0)+(r.FontFile ? r.FontFile->native().size()*sizeof(std::filesystem::path::value_type):0);}}return n;}
    void Trim() {std::size_t bytes=Group ? Bytes(*Group):0;for (const auto& s:Past) {bytes+=Bytes(s);}for (const auto& s:Future) {bytes+=Bytes(s);}while (Past.size()+Future.size()>128||bytes>32*1024*1024) {if (!Past.empty()) {bytes-=Bytes(Past.front());Past.pop_front();}else if (!Future.empty()) {bytes-=Bytes(Future.front());Future.pop_front();}else {break;}}}
    void ResetInput() {Events.clear();Hover=Capture=Focus=KeyCapture=0;Scroll.clear();}
    bool Active(UiId id,bool button=false) const {auto it=Find(id);if (it==Items.end()||(button&&it->Kind!=UiKind::Button)) {return false;}for (;;) {if (!it->Visible||!it->Enabled) {return false;}if (!it->Parent) {return true;}it=Find(it->Parent);if (it==Items.end()) {return false;}}}
    bool Apply(std::vector<UiElement> items,std::filesystem::path directory) {
        if (items==Items&&directory==Directory) {return false;}detail::ValidateUiTree(items);
        Snapshot next{std::move(items),std::move(directory),Serial+1};if (Bytes(next)>32*1024*1024) {throw UiBudgetExceeded("UI content exceeds the 32 MiB snapshot budget.");}
        if (!Group) {Past.push_back(CaptureState());Future.clear();}
        Items=std::move(next.Items);Directory=std::move(next.Directory);State=++Serial;++Version;++LayoutEpoch;
        if (!Active(Hover)) {Hover=0;}if (!Active(Capture)) {Capture=0;}if (!Active(Focus,true)) {Focus=0;}if (!Active(KeyCapture,true)) {KeyCapture=0;}
        std::erase_if(Events,[&](UiId id){return !Active(id,true);});std::erase_if(Scroll,[&](const auto& pair){return Find(pair.first)==Items.end();});Trim();return true;
    }
    void Restore(Snapshot state) {Items=std::move(state.Items);Directory=std::move(state.Directory);State=state.State;ResetInput();++Version;++LayoutEpoch;}
    bool Ready() const {return Owner==std::this_thread::get_id();}
    const UiStyle& Style(const UiElement& e) const {
        if (!e.Enabled&&e.Disabled) {return *e.Disabled;}if ((Capture==e.Id||KeyCapture==e.Id)&&e.Pressed) {return *e.Pressed;}if (Hover==e.Id&&e.Hover) {return *e.Hover;}if (Focus==e.Id&&e.Focused) {return *e.Focused;}return e.Normal;
    }
};
UiCanvas::UiCanvas():impl_(std::make_unique<Impl>()) {}
UiCanvas::~UiCanvas()=default;
std::uint64_t UiCanvas::Revision() const noexcept {return impl_->Version;}
UiId UiCanvas::Focused() const noexcept {return impl_->Focus;}
bool UiCanvas::Empty() const noexcept {return impl_->Items.empty();}
Result<UiId> UiCanvas::Add(const UiElement& description) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<UiId>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Items.size()>=MaxElements) {return Fail<UiId>(ErrorCode::InsufficientResource,"UI supports at most 512 elements.");}
    try {Validate(description);auto e=description;e.Id=p.Next;auto candidate=p.Items;candidate.push_back(std::move(e));p.Apply(std::move(candidate),p.Directory);++p.Next;return Result<UiId>::Success(p.Next-1);}catch (...) {return Result<UiId>::Failure(ErrorFromException());}
}
Result<void> UiCanvas::Set(UiId id,const UiElement& description) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);if (it==p.Items.end()) {return Fail<void>(ErrorCode::InvalidHandle,"Unknown UI ID.");}
    try {Validate(description);auto candidate=p.Items;auto e=description;e.Id=id;candidate[static_cast<std::size_t>(it-p.Items.begin())]=std::move(e);p.Apply(std::move(candidate),p.Directory);return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}
}
Result<UiElement> UiCanvas::Get(UiId id) const {const auto& p=*impl_;if (!p.Ready()) {return Fail<UiElement>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);return it==p.Items.end() ? Fail<UiElement>(ErrorCode::InvalidHandle,"Unknown UI ID."):Result<UiElement>::Success(*it);}
Result<std::vector<UiElement>> UiCanvas::Elements() const {if (!impl_->Ready()) {return Fail<std::vector<UiElement>>(ErrorCode::WrongThread,"UI accessed from another thread.");}return Result<std::vector<UiElement>>::Success(impl_->Items);}
Result<void> UiCanvas::Remove(UiId id) {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Find(id)==p.Items.end()) {return Fail<void>(ErrorCode::InvalidHandle,"Unknown UI ID.");}try {auto candidate=p.Items;std::erase_if(candidate,[&](const UiElement& e){auto parent=e.Id;while (parent) {if (parent==id) {return true;}parent=p.Find(parent)->Parent;}return false;});p.Apply(std::move(candidate),p.Directory);return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<void> UiCanvas::Clear() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}try {p.Apply({},p.Directory);p.ResetInput();return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<std::vector<UiLayoutBox>> UiCanvas::ResolveLayout(float width,float height) const {const auto& p=*impl_;if (!p.Ready()) {return Fail<std::vector<UiLayoutBox>>(ErrorCode::WrongThread,"UI accessed from another thread.");}try {Range(width,0,65536);Range(height,0,65536);const auto& resolved=p.Resolve(width,height);std::vector<UiLayoutBox> boxes;for (const auto& r:resolved) {boxes.push_back(r.Box);}return Result<std::vector<UiLayoutBox>>::Success(std::move(boxes));}catch (...) {return Result<std::vector<UiLayoutBox>>::Failure(ErrorFromException(ErrorCode::InvalidData));}}
Result<void> UiCanvas::SetScroll(UiId id,float x,float y) {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);if (it==p.Items.end()) {return Fail<void>(ErrorCode::InvalidHandle,"Unknown UI ID.");}try {Require(it->Container.Overflow==UiOverflow::Scroll,"SetScroll requires a scrolling panel.");Range(x,0,2000000);Range(y,0,2000000);const auto next=std::array{x,y};if (!p.Scroll.contains(id)||p.Scroll.at(id)!=next) {p.Scroll[id]=next;++p.Version;++p.LayoutEpoch;}return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<void> UiCanvas::BeginEdit() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Group) {return Fail<void>(ErrorCode::InvalidState,"UI edit is already active.");}try {p.Group=p.CaptureState();p.Trim();return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<void> UiCanvas::CommitEdit() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (!p.Group) {return Fail<void>(ErrorCode::InvalidState,"Begin a UI edit before committing.");}try {if (p.Group->Items!=p.Items||p.Group->Directory!=p.Directory) {p.Past.push_back(std::move(*p.Group));p.Future.clear();}else {p.State=p.Group->State;}p.Group.reset();p.Trim();return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<void> UiCanvas::CancelEdit() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (!p.Group) {return Fail<void>(ErrorCode::InvalidState,"Begin a UI edit before cancelling.");}p.Restore(std::move(*p.Group));p.Group.reset();return Result<void>::Success();}
Result<void> UiCanvas::Undo() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Group||p.Past.empty()) {return Fail<void>(ErrorCode::InvalidState,"Finish the active edit and provide an undo entry.");}try {p.Future.push_back(p.CaptureState());auto state=std::move(p.Past.back());p.Past.pop_back();p.Restore(std::move(state));p.Trim();return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<void> UiCanvas::Redo() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Group||p.Future.empty()) {return Fail<void>(ErrorCode::InvalidState,"Finish the active edit and provide a redo entry.");}try {p.Past.push_back(p.CaptureState());auto state=std::move(p.Future.back());p.Future.pop_back();p.Restore(std::move(state));p.Trim();return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}}
Result<UiHistoryState> UiCanvas::History() const {const auto& p=*impl_;if (!p.Ready()) {return Fail<UiHistoryState>(ErrorCode::WrongThread,"UI accessed from another thread.");}return Result<UiHistoryState>::Success({!p.Group&&!p.Past.empty(),!p.Group&&!p.Future.empty(),p.Group.has_value(),p.State!=p.Saved,p.Past.size(),p.Future.size()});}
Result<void> UiCanvas::ClearHistory() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}if (p.Group) {return Fail<void>(ErrorCode::InvalidState,"Finish the active edit first.");}p.Past.clear();p.Future.clear();return Result<void>::Success();}
Result<bool> UiCanvas::ProcessInput(const UiInput& in,float width,float height) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<bool>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {
        Range(in.X,-1000000,1000000);Range(in.Y,-1000000,1000000);Range(width,0,65536);Range(height,0,65536);Range(in.ScrollX,-2000000,2000000);Range(in.ScrollY,-2000000,2000000);Require(in.Type>=UiInputType::Move&&in.Type<=UiInputType::Scroll,"Unknown UI input type.");
        if (in.Type==UiInputType::Cancel) {if (p.Hover||p.Capture||p.Focus||p.KeyCapture) {p.Hover=p.Capture=p.Focus=p.KeyCapture=0;++p.Version;}return Result<bool>::Success(false);}
        const auto& resolved=p.Resolve(width,height);UiId hit=0;int hitIndex=-1;for (std::size_t i=0;i<resolved.size();++i) {const auto& r=resolved[i];const auto& e=r.Element;if (!r.Box.Visible||(!e.BlocksInput&&!(in.Type==UiInputType::Scroll&&e.Container.Overflow==UiOverflow::Scroll))||detail::UiClipped(resolved,i,in.X,in.Y)) {continue;}const auto xy=detail::UiLocal(r.Box,in.X,in.Y);if (Distance(xy[0],xy[1],r.Box.Width,r.Box.Height,p.Style(e).Radius)<=0) {hit=e.Id;hitIndex=static_cast<int>(i);}}
        const auto old=std::array{p.Hover,p.Capture,p.Focus,p.KeyCapture};bool consumed=false;UiId activate=0;
        auto button=[&](UiId id) {return p.Active(id,true);};
        switch (in.Type) {
        case UiInputType::Move:p.Hover=hit;consumed=hit||p.Capture;break;
        case UiInputType::Down:p.Hover=hit;p.Capture=hit;p.Focus=button(hit) ? hit:0;p.KeyCapture=0;consumed=hit!=0;break;
        case UiInputType::Up:consumed=hit||p.Capture;if (hit==p.Capture&&button(hit)) {activate=hit;}p.Capture=0;p.Hover=hit;break;
        case UiInputType::Cancel:p.Hover=p.Capture=p.Focus=p.KeyCapture=0;break;
        case UiInputType::Tab:{std::vector<UiId> focus;for (const auto& e:p.Items) {if (button(e.Id)&&e.BlocksInput) {focus.push_back(e.Id);}}if (!focus.empty()) {auto it=std::find(focus.begin(),focus.end(),p.Focus);const auto index=it==focus.end() ? (in.Shift ? 0:static_cast<int>(focus.size())-1):static_cast<int>(it-focus.begin());p.Focus=focus[static_cast<std::size_t>((index+(in.Shift ? static_cast<int>(focus.size())-1:1))%static_cast<int>(focus.size()))];consumed=true;}p.KeyCapture=p.Capture=0;break;}
        case UiInputType::ActivateDown:consumed=button(p.Focus);if (consumed&&!in.Repeat) {p.KeyCapture=p.Focus;}break;
        case UiInputType::ActivateUp:consumed=button(p.Focus);if (p.KeyCapture==p.Focus&&button(p.Focus)) {activate=p.Focus;}p.KeyCapture=0;break;
        case UiInputType::Scroll:consumed=hit!=0;for (int i=hitIndex;i>=0;i=resolved[static_cast<std::size_t>(i)].Parent) {const auto& r=resolved[static_cast<std::size_t>(i)];if (r.Element.Container.Overflow!=UiOverflow::Scroll||!r.Box.Enabled) {continue;}const auto& pad=r.Element.Container.Padding;const float x=std::clamp(r.Box.ScrollX+in.ScrollX,0.0f,std::max(0.0f,r.Box.ContentWidth-r.Box.Width+pad[0]+pad[2])),y=std::clamp(r.Box.ScrollY+in.ScrollY,0.0f,std::max(0.0f,r.Box.ContentHeight-r.Box.Height+pad[1]+pad[3]));if (x!=r.Box.ScrollX||y!=r.Box.ScrollY) {const auto changed=SetScroll(r.Box.Id,x,y);if (!changed) {return Result<bool>::Failure(changed.Error());}break;}}break;
        }
        if (old!=std::array{p.Hover,p.Capture,p.Focus,p.KeyCapture}) {++p.Version;}
        if (activate) {if (p.Events.size()>=MaxQueue) {return Fail<bool>(ErrorCode::InsufficientResource,"Drain the UI activation queue (1024 maximum).");}p.Events.push_back(activate);}
        return Result<bool>::Success(consumed);
    }catch (...) {return Result<bool>::Failure(ErrorFromException());}
}
Result<std::vector<UiId>> UiCanvas::PollActivations() {if (!impl_->Ready()) {return Fail<std::vector<UiId>>(ErrorCode::WrongThread,"UI accessed from another thread.");}auto events=std::move(impl_->Events);impl_->Events.clear();return Result<std::vector<UiId>>::Success(std::move(events));}
Result<void> UiCanvas::SetFontDirectory(const std::filesystem::path& directory,bool create) {
    if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {Require(!directory.empty(),"Font directory cannot be empty.");auto path=std::filesystem::absolute(directory).lexically_normal();if (create) {std::filesystem::create_directories(path);}Require(std::filesystem::is_directory(path),"Font directory does not exist.");impl_->Apply(impl_->Items,std::move(path));return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidArgument,directory));}
}
Result<std::filesystem::path> UiCanvas::FontDirectory() const {if (!impl_->Ready()) {return Fail<std::filesystem::path>(ErrorCode::WrongThread,"UI accessed from another thread.");}return Result<std::filesystem::path>::Success(impl_->Directory);}
Result<std::vector<UiFont>> UiCanvas::Fonts(bool system) const {
    if (!impl_->Ready()) {return Fail<std::vector<UiFont>>(ErrorCode::WrongThread,"UI accessed from another thread.");}
#if !defined(_WIN32) && !defined(__APPLE__)
    (void)system;return Fail<std::vector<UiFont>>(ErrorCode::UnsupportedFeature,"Native fonts require Windows or macOS.");
#else
    try {auto result=system ? detail::NativeFonts({}):std::vector<UiFont>{};std::size_t files=0;
        if (std::filesystem::is_directory(impl_->Directory)) {for (const auto& f:std::filesystem::directory_iterator(impl_->Directory)) {if (!f.is_regular_file()) {continue;}auto ext=f.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if (ext!=".ttf"&&ext!=".otf"&&ext!=".ttc") {continue;}Require(++files<=128,"Font directory supports at most 128 font files.");Require(f.file_size()<=64*1024*1024,"Font file exceeds 64 MiB.");auto fonts=detail::NativeFonts(f.path());for (auto& font:fonts) {font.File=f.path().filename();result.push_back(std::move(font));}}}
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.Family==b.Family ? a.File<b.File:a.Family<b.Family;});return Result<std::vector<UiFont>>::Success(std::move(result));
    }catch (...) {return Result<std::vector<UiFont>>::Failure(ErrorFromException(ErrorCode::InvalidData,impl_->Directory));}
#endif
}
Result<void> UiCanvas::RefreshFonts() {if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}++impl_->Version;++impl_->LayoutEpoch;return Result<void>::Success();}
Result<void> UiCanvas::Save(const std::filesystem::path& path) const {
    if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    if (impl_->Group) {return Fail<void>(ErrorCode::InvalidState,"Commit or cancel the UI edit before saving.");}
    try {Json a=Json::array();for (const auto& e:impl_->Items) {a.push_back(ElementJson(e));}const auto base=std::filesystem::absolute(path).parent_path();const auto folder=std::filesystem::weakly_canonical(impl_->Directory);const auto relative=std::filesystem::relative(folder,base);detail::WriteJsonFile({{"format","Minvex.UI"},{"version",2},{"fontDirectory",PathText(relative)},{"elements",a}},path,8*1024*1024);impl_->Saved=impl_->State;return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidData,path));}
}
Result<void> UiCanvas::Load(const std::filesystem::path& path) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    if (p.Group) {return Fail<void>(ErrorCode::InvalidState,"Commit or cancel the UI edit before loading.");}
    try {const auto j=detail::ReadJsonFile(path,8*1024*1024);const int version=Integer(j.at("version"));Require(j.at("format")=="Minvex.UI"&&version>=1&&version<=2,"Unsupported UI document format/version.");Require(j.at("elements").is_array()&&j.at("elements").size()<=MaxElements,"UI element limit exceeded.");auto next=p.Next;std::vector<UiElement> candidate;std::unordered_map<UiId,UiId> remap;
        for (const auto& value:j.at("elements")) {auto e=ReadElement(value);e.Id=next++;if (version==2) {const auto key=Identifier(value.at("key"));Require(key&&remap.emplace(key,e.Id).second,"Duplicate or zero persisted UI key.");}else {e.Parent=NoUi;e.Container={};e.Runs.clear();e.Layout.WidthSizing=e.Layout.HeightSizing=UiSizing::Fixed;}candidate.push_back(std::move(e));}
        for (auto& e:candidate) {if (e.Parent) {Require(remap.contains(e.Parent),"Missing persisted UI parent.");e.Parent=remap.at(e.Parent);}}
        const auto s=j.at("fontDirectory").get<std::string>();auto folder=(std::filesystem::absolute(path).parent_path()/std::filesystem::path(std::u8string(s.begin(),s.end()))).lexically_normal();
        p.Apply(std::move(candidate),std::move(folder));p.Next=next;p.ResetInput();p.Saved=p.State;return Result<void>::Success();
    }catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidData,path));}
}
Result<std::vector<std::uint8_t>> UiCanvas::Rasterize(int width,int height,float scale) const {
    const auto& p=*impl_;if (!p.Ready()) {return Fail<std::vector<std::uint8_t>>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {
        Require(width>=1&&height>=1&&width<=4096&&height<=4096,"UI raster dimensions must be 1..4096.");Range(scale,.25f,4);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);const auto& resolved=p.Resolve(width/scale,height/scale);
        for (std::size_t index=0;index<resolved.size();++index) {
            const auto& r=resolved[index];if (!r.Box.Visible) {continue;}auto e=r.Element;e.Enabled=r.Box.Enabled;const auto& s=p.Style(e);if (s.Opacity==0) {continue;}const Geometry g(r.Box);
            const int tw=std::max(1,static_cast<int>(std::ceil(g.W*scale))),th=std::max(1,static_cast<int>(std::ceil(g.H*scale)));
            Require(tw<=4096&&th<=4096,"Text bitmap exceeds 4096 pixels; reduce element size or DPI.");
#if !defined(_WIN32) && !defined(__APPLE__)
            if (e.Kind!=UiKind::Panel&&detail::HasText(e)) {return Fail<std::vector<std::uint8_t>>(ErrorCode::UnsupportedFeature,"Native text requires Windows or macOS.");}
#endif
            auto text=e.Kind!=UiKind::Panel&&detail::HasText(e) ? detail::NativeText(e,p.Directory,tw,th,scale,s.Text):std::vector<std::uint8_t>{};
            const float pad=s.ShadowBlur*3+std::max(std::abs(s.ShadowX),std::abs(s.ShadowY))+2;
            float minX=static_cast<float>(width),minY=static_cast<float>(height),maxX=0,maxY=0;for (auto c:std::array<std::array<float,2>,4>{{{-pad,-pad},{g.W+pad,-pad},{g.W+pad,g.H+pad},{-pad,g.H+pad}}}) {const auto q=g.World(c[0],c[1]);minX=std::min(minX,q[0]*scale);minY=std::min(minY,q[1]*scale);maxX=std::max(maxX,q[0]*scale);maxY=std::max(maxY,q[1]*scale);}
            const int x0=static_cast<int>(std::clamp(std::floor(minX),0.0f,static_cast<float>(width))),x1=static_cast<int>(std::clamp(std::ceil(maxX),0.0f,static_cast<float>(width)));
            const int y0=static_cast<int>(std::clamp(std::floor(minY),0.0f,static_cast<float>(height))),y1=static_cast<int>(std::clamp(std::ceil(maxY),0.0f,static_cast<float>(height)));
            const float aa=1/(scale*std::min(g.SX,g.SY));
            for (int y=y0;y<y1;++y) {for (int x=x0;x<x1;++x) {const float wx=(x+.5f)/scale,wy=(y+.5f)/scale;if (detail::UiClipped(resolved,index,wx,wy)) {continue;}const auto xy=g.Local(wx,wy);const float lx=xy[0],ly=xy[1];auto* target=pixels.data()+(static_cast<std::size_t>(y)*width+x)*4;std::uint8_t layer[4]{};auto* dst=layer;
                if (s.Shadow[3]>0) {const float sd=Distance(lx-s.ShadowX,ly-s.ShadowY,g.W,g.H,s.Radius);const float coverage=s.ShadowBlur>0 ? .5f*std::erfc(sd/(s.ShadowBlur*.70710678f)):std::clamp(.5f-sd/aa,0.0f,1.0f);Blend(dst,s.Shadow,coverage);}
                const float distance=Distance(lx,ly,g.W,g.H,s.Radius),coverage=std::clamp(.5f-distance/aa,0.0f,1.0f);if (coverage<=0) {Composite(target,layer,s.Opacity);continue;}
                const float u=std::clamp(lx/g.W,0.0f,1.0f),v=std::clamp(ly/g.H,0.0f,1.0f);UiColor fill;for (int k=0;k<4;++k) {fill[k]=std::lerp(std::lerp(s.Fill[0][k],s.Fill[1][k],u),std::lerp(s.Fill[3][k],s.Fill[2][k],u),v);}Blend(dst,fill,coverage);
                if (s.BorderWidth>0) {Blend(dst,s.Border,std::clamp((distance+s.BorderWidth)/aa+.5f,0.0f,1.0f)*coverage);}
                if (!text.empty()&&lx>=0&&ly>=0&&lx<g.W&&ly<g.H) {const int tx=std::clamp(static_cast<int>(lx*scale),0,tw-1),ty=std::clamp(static_cast<int>(ly*scale),0,th-1);Composite(dst,text.data()+(static_cast<std::size_t>(ty)*tw+tx)*4,coverage);}Composite(target,layer,s.Opacity);
            }}
        }return Result<std::vector<std::uint8_t>>::Success(std::move(pixels));
    }catch (...) {return Result<std::vector<std::uint8_t>>::Failure(ErrorFromException(ErrorCode::InvalidData));}
}
} // namespace lv
