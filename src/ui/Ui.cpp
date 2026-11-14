#include <minvex/Ui.h>
#include "NativeText.h"
#include "core/JsonFile.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <cctype>

namespace lv {
namespace {
constexpr std::size_t MaxElements=512,MaxText=16384,MaxQueue=1024;
Error ErrorFromException(ErrorCode code=ErrorCode::InvalidArgument,const std::filesystem::path& path={}) {
    try {throw;}
    catch (const std::bad_alloc& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce UI size or text length.",path};}
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
    Require(l.Z>=-1000000&&l.Z<=1000000,"UI Z is out of range.");
    const auto& t=e.Typography;Range(t.Size,1,256);Range(t.LineHeight,.5f,4);Range(t.LetterSpacing,-32,128);Require(t.Weight>=100&&t.Weight<=900,"Font weight must be 100..900.");
    Require(t.Horizontal>=UiAlign::Start&&t.Horizontal<=UiAlign::End&&t.Vertical>=UiAlign::Start&&t.Vertical<=UiAlign::End,"Unknown text alignment.");
    for (auto p:t.Padding) {Range(p,0,1024);}Require(e.Kind==UiKind::Panel||(t.Padding[0]+t.Padding[2]<l.Width&&t.Padding[1]+t.Padding[3]<l.Height),"Text padding must leave positive content area.");
    Require(!t.FontFile.is_absolute()&&!t.FontFile.has_root_path(),"FontFile must be relative to Font directory.");for (const auto& p:t.FontFile) {Require(p!="..","FontFile cannot escape Font directory.");}
    ValidateStyle(e.Normal);for (auto* s:{&e.Hover,&e.Pressed,&e.Focused,&e.Disabled}) {if (*s) {ValidateStyle(**s);}}
    if (e.Clip) {Range(e.Clip->X,-65536,65536);Range(e.Clip->Y,-65536,65536);Range(e.Clip->Width,0,65536);Range(e.Clip->Height,0,65536);}
}
struct Geometry {
    float X,Y,W,H,PX,PY,SX,SY,C,S;
    Geometry(const UiLayout& l,float w,float h):X(l.AnchorX*w+l.X),Y(l.AnchorY*h+l.Y),W(l.Width),H(l.Height),PX(l.PivotX*l.Width),PY(l.PivotY*l.Height),SX(l.ScaleX),SY(l.ScaleY),C(std::cos(l.Rotation)),S(std::sin(l.Rotation)) {}
    std::array<float,2> Local(float x,float y) const {x-=X;y-=Y;return {(C*x+S*y)/SX+PX,(-S*x+C*y)/SY+PY};}
    std::array<float,2> World(float x,float y) const {x=(x-PX)*SX;y=(y-PY)*SY;return {X+C*x-S*y,Y+S*x+C*y};}
};
bool Clipped(const UiElement& e,float x,float y) {return e.Clip && (x<e.Clip->X||y<e.Clip->Y||x>=e.Clip->X+e.Clip->Width||y>=e.Clip->Y+e.Clip->Height);}
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
Json ElementJson(const UiElement& e) {
    const auto& l=e.Layout;const auto& t=e.Typography;
    Json j={{"kind",static_cast<int>(e.Kind)},{"name",e.Name},{"text",e.Text},{"layout",{l.X,l.Y,l.Width,l.Height,l.AnchorX,l.AnchorY,l.PivotX,l.PivotY,l.Rotation,l.ScaleX,l.ScaleY}},{"z",l.Z},
        {"typography",{{"family",t.Family},{"file",PathText(t.FontFile)},{"size",t.Size},{"lineHeight",t.LineHeight},{"letterSpacing",t.LetterSpacing},{"weight",t.Weight},{"italic",t.Italic},{"underline",t.Underline},{"strike",t.StrikeThrough},{"wrap",t.Wrap},{"horizontal",static_cast<int>(t.Horizontal)},{"vertical",static_cast<int>(t.Vertical)},{"padding",t.Padding}}},
        {"normal",StyleJson(e.Normal)},{"visible",e.Visible},{"enabled",e.Enabled},{"blocksInput",e.BlocksInput}};
    if (e.Clip) {j["clip"]={e.Clip->X,e.Clip->Y,e.Clip->Width,e.Clip->Height};}
    if (e.Hover) {j["hover"]=StyleJson(*e.Hover);}if (e.Pressed) {j["pressed"]=StyleJson(*e.Pressed);}if (e.Focused) {j["focused"]=StyleJson(*e.Focused);}if (e.Disabled) {j["disabled"]=StyleJson(*e.Disabled);}return j;
}
UiElement ReadElement(const Json& j) {
    UiElement e;e.Kind=static_cast<UiKind>(Integer(j.at("kind")));e.Name=j.at("name");e.Text=j.at("text");const auto a=j.at("layout").get<std::array<float,11>>();auto& l=e.Layout;
    l={a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],Integer(j.at("z"))};const auto& o=j.at("typography");auto& t=e.Typography;
    t.Family=o.at("family");const auto p=o.at("file").get<std::string>();t.FontFile=std::filesystem::path(std::u8string(p.begin(),p.end()));t.Size=o.at("size");t.LineHeight=o.at("lineHeight");t.LetterSpacing=o.at("letterSpacing");t.Weight=Integer(o.at("weight"));t.Italic=o.at("italic");t.Underline=o.at("underline");t.StrikeThrough=o.at("strike");t.Wrap=o.at("wrap");t.Horizontal=static_cast<UiAlign>(Integer(o.at("horizontal")));t.Vertical=static_cast<UiAlign>(Integer(o.at("vertical")));t.Padding=o.at("padding").get<std::array<float,4>>();
    e.Normal=ReadStyle(j.at("normal"));e.Visible=j.at("visible");e.Enabled=j.at("enabled");e.BlocksInput=j.at("blocksInput");if (j.contains("clip")) {const auto c=j.at("clip").get<std::array<float,4>>();e.Clip=UiRect{c[0],c[1],c[2],c[3]};}
    if (j.contains("hover")) {e.Hover=ReadStyle(j.at("hover"));}if (j.contains("pressed")) {e.Pressed=ReadStyle(j.at("pressed"));}if (j.contains("focused")) {e.Focused=ReadStyle(j.at("focused"));}if (j.contains("disabled")) {e.Disabled=ReadStyle(j.at("disabled"));}Validate(e);return e;
}
}
struct UiCanvas::Impl {
    std::thread::id Owner=std::this_thread::get_id();std::vector<UiElement> Items;std::vector<UiId> Events;
    std::filesystem::path Directory="Font";UiId Next=1,Hover=0,Capture=0,Focus=0,KeyCapture=0;std::uint64_t Version=1;
    bool Ready() const {return Owner==std::this_thread::get_id();}
    auto Find(UiId id) {return std::find_if(Items.begin(),Items.end(),[=](const auto& e){return e.Id==id;});}
    auto Find(UiId id) const {return std::find_if(Items.begin(),Items.end(),[=](const auto& e){return e.Id==id;});}
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
    try {Validate(description);auto e=description;e.Id=p.Next;p.Items.push_back(std::move(e));++p.Next;++p.Version;return Result<UiId>::Success(p.Next-1);}catch (...) {return Result<UiId>::Failure(ErrorFromException());}
}
Result<void> UiCanvas::Set(UiId id,const UiElement& description) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);if (it==p.Items.end()) {return Fail<void>(ErrorCode::InvalidHandle,"Unknown UI ID.");}
    try {Validate(description);auto e=description;e.Id=id;*it=std::move(e);if (!it->Visible||!it->Enabled||it->Kind!=UiKind::Button) {if (p.Focus==id) {p.Focus=0;}if (p.Capture==id) {p.Capture=0;}if (p.KeyCapture==id) {p.KeyCapture=0;}}++p.Version;return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException());}
}
Result<UiElement> UiCanvas::Get(UiId id) const {const auto& p=*impl_;if (!p.Ready()) {return Fail<UiElement>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);return it==p.Items.end() ? Fail<UiElement>(ErrorCode::InvalidHandle,"Unknown UI ID."):Result<UiElement>::Success(*it);}
Result<std::vector<UiElement>> UiCanvas::Elements() const {if (!impl_->Ready()) {return Fail<std::vector<UiElement>>(ErrorCode::WrongThread,"UI accessed from another thread.");}return Result<std::vector<UiElement>>::Success(impl_->Items);}
Result<void> UiCanvas::Remove(UiId id) {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}const auto it=p.Find(id);if (it==p.Items.end()) {return Fail<void>(ErrorCode::InvalidHandle,"Unknown UI ID.");}p.Items.erase(it);std::erase(p.Events,id);if (p.Hover==id) {p.Hover=0;}if (p.Focus==id) {p.Focus=0;}if (p.Capture==id) {p.Capture=0;}if (p.KeyCapture==id) {p.KeyCapture=0;}++p.Version;return Result<void>::Success();}
Result<void> UiCanvas::Clear() {auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}p.Items.clear();p.Events.clear();p.Hover=p.Capture=p.Focus=p.KeyCapture=0;++p.Version;return Result<void>::Success();}
Result<bool> UiCanvas::ProcessInput(const UiInput& in,float width,float height) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<bool>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {
        Range(in.X,-1000000,1000000);Range(in.Y,-1000000,1000000);Range(width,0,65536);Range(height,0,65536);Require(in.Type>=UiInputType::Move&&in.Type<=UiInputType::ActivateUp,"Unknown UI input type.");
        UiId hit=0;int z=std::numeric_limits<int>::min();for (const auto& e:p.Items) {if (!e.Visible||!e.BlocksInput||Clipped(e,in.X,in.Y)||e.Layout.Z<z) {continue;}const auto xy=Geometry(e.Layout,width,height).Local(in.X,in.Y);if (Distance(xy[0],xy[1],e.Layout.Width,e.Layout.Height,p.Style(e).Radius)<=0) {hit=e.Id;z=e.Layout.Z;}}
        const auto old=std::array{p.Hover,p.Capture,p.Focus,p.KeyCapture};bool consumed=false;UiId activate=0;
        auto button=[&](UiId id) {const auto it=p.Find(id);return it!=p.Items.end()&&it->Kind==UiKind::Button&&it->Visible&&it->Enabled;};
        switch (in.Type) {
        case UiInputType::Move:p.Hover=hit;consumed=hit||p.Capture;break;
        case UiInputType::Down:p.Hover=hit;p.Capture=hit;p.Focus=button(hit) ? hit:0;p.KeyCapture=0;consumed=hit!=0;break;
        case UiInputType::Up:consumed=hit||p.Capture;if (hit==p.Capture&&button(hit)) {activate=hit;}p.Capture=0;p.Hover=hit;break;
        case UiInputType::Cancel:p.Hover=p.Capture=p.Focus=p.KeyCapture=0;break;
        case UiInputType::Tab:{std::vector<UiId> focus;for (const auto& e:p.Items) {if (button(e.Id)&&e.BlocksInput) {focus.push_back(e.Id);}}if (!focus.empty()) {auto it=std::find(focus.begin(),focus.end(),p.Focus);const auto index=it==focus.end() ? (in.Shift ? 0:static_cast<int>(focus.size())-1):static_cast<int>(it-focus.begin());p.Focus=focus[static_cast<std::size_t>((index+(in.Shift ? static_cast<int>(focus.size())-1:1))%static_cast<int>(focus.size()))];consumed=true;}p.KeyCapture=p.Capture=0;break;}
        case UiInputType::ActivateDown:consumed=button(p.Focus);if (consumed&&!in.Repeat) {p.KeyCapture=p.Focus;}break;
        case UiInputType::ActivateUp:consumed=button(p.Focus);if (p.KeyCapture==p.Focus&&button(p.Focus)) {activate=p.Focus;}p.KeyCapture=0;break;
        }
        if (old!=std::array{p.Hover,p.Capture,p.Focus,p.KeyCapture}) {++p.Version;}
        if (activate) {if (p.Events.size()>=MaxQueue) {return Fail<bool>(ErrorCode::InsufficientResource,"Drain the UI activation queue (1024 maximum).");}p.Events.push_back(activate);}
        return Result<bool>::Success(consumed);
    }catch (...) {return Result<bool>::Failure(ErrorFromException());}
}
Result<std::vector<UiId>> UiCanvas::PollActivations() {if (!impl_->Ready()) {return Fail<std::vector<UiId>>(ErrorCode::WrongThread,"UI accessed from another thread.");}auto events=std::move(impl_->Events);impl_->Events.clear();return Result<std::vector<UiId>>::Success(std::move(events));}
Result<void> UiCanvas::SetFontDirectory(const std::filesystem::path& directory,bool create) {
    if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {Require(!directory.empty(),"Font directory cannot be empty.");auto path=std::filesystem::absolute(directory).lexically_normal();if (create) {std::filesystem::create_directories(path);}Require(std::filesystem::is_directory(path),"Font directory does not exist.");impl_->Directory=std::move(path);++impl_->Version;return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidArgument,directory));}
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
Result<void> UiCanvas::RefreshFonts() {if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}++impl_->Version;return Result<void>::Success();}
Result<void> UiCanvas::Save(const std::filesystem::path& path) const {
    if (!impl_->Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {Json a=Json::array();for (const auto& e:impl_->Items) {a.push_back(ElementJson(e));}const auto base=std::filesystem::absolute(path).parent_path();const auto folder=std::filesystem::weakly_canonical(impl_->Directory);const auto relative=std::filesystem::relative(folder,base);detail::WriteJsonFile({{"format","Minvex.UI"},{"version",1},{"fontDirectory",PathText(relative)},{"elements",a}},path,8*1024*1024);return Result<void>::Success();}catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidData,path));}
}
Result<void> UiCanvas::Load(const std::filesystem::path& path) {
    auto& p=*impl_;if (!p.Ready()) {return Fail<void>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {const auto j=detail::ReadJsonFile(path,8*1024*1024);Require(j.at("format")=="Minvex.UI"&&Integer(j.at("version"))==1,"Unsupported UI document format/version.");Require(j.at("elements").is_array()&&j.at("elements").size()<=MaxElements,"UI element limit exceeded.");auto next=p.Next;std::vector<UiElement> candidate;for (const auto& value:j.at("elements")) {auto e=ReadElement(value);e.Id=next++;candidate.push_back(std::move(e));}const auto s=j.at("fontDirectory").get<std::string>();auto folder=(std::filesystem::absolute(path).parent_path()/std::filesystem::path(std::u8string(s.begin(),s.end()))).lexically_normal();
        p.Directory=std::move(folder);p.Items=std::move(candidate);p.Next=next;p.Events.clear();p.Hover=p.Capture=p.Focus=p.KeyCapture=0;++p.Version;return Result<void>::Success();
    }catch (...) {return Result<void>::Failure(ErrorFromException(ErrorCode::InvalidData,path));}
}
Result<std::vector<std::uint8_t>> UiCanvas::Rasterize(int width,int height,float scale) const {
    const auto& p=*impl_;if (!p.Ready()) {return Fail<std::vector<std::uint8_t>>(ErrorCode::WrongThread,"UI accessed from another thread.");}
    try {
        Require(width>=1&&height>=1&&width<=4096&&height<=4096,"UI raster dimensions must be 1..4096.");Range(scale,.25f,4);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);std::vector<const UiElement*> sorted;for (const auto& e:p.Items) {if (e.Visible) {sorted.push_back(&e);}}std::stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->Layout.Z<b->Layout.Z;});
        for (const auto* item:sorted) {
            const auto& e=*item;const auto& s=p.Style(e);if (s.Opacity==0) {continue;}const Geometry g(e.Layout,width/scale,height/scale);
            const int tw=std::max(1,static_cast<int>(std::ceil(g.W*scale))),th=std::max(1,static_cast<int>(std::ceil(g.H*scale)));
            Require(tw<=4096&&th<=4096,"Text bitmap exceeds 4096 pixels; reduce element size or DPI.");
#if !defined(_WIN32) && !defined(__APPLE__)
            if (e.Kind!=UiKind::Panel&&!e.Text.empty()) {return Fail<std::vector<std::uint8_t>>(ErrorCode::UnsupportedFeature,"Native text requires Windows or macOS.");}
#endif
            auto text=e.Kind!=UiKind::Panel&&!e.Text.empty() ? detail::NativeText(e,p.Directory,tw,th,scale):std::vector<std::uint8_t>{};
            const float pad=s.ShadowBlur*3+std::max(std::abs(s.ShadowX),std::abs(s.ShadowY))+2;
            float minX=static_cast<float>(width),minY=static_cast<float>(height),maxX=0,maxY=0;for (auto c:std::array<std::array<float,2>,4>{{{-pad,-pad},{g.W+pad,-pad},{g.W+pad,g.H+pad},{-pad,g.H+pad}}}) {const auto q=g.World(c[0],c[1]);minX=std::min(minX,q[0]*scale);minY=std::min(minY,q[1]*scale);maxX=std::max(maxX,q[0]*scale);maxY=std::max(maxY,q[1]*scale);}
            const int x0=static_cast<int>(std::clamp(std::floor(minX),0.0f,static_cast<float>(width))),x1=static_cast<int>(std::clamp(std::ceil(maxX),0.0f,static_cast<float>(width)));
            const int y0=static_cast<int>(std::clamp(std::floor(minY),0.0f,static_cast<float>(height))),y1=static_cast<int>(std::clamp(std::ceil(maxY),0.0f,static_cast<float>(height)));
            const float aa=1/(scale*std::min(g.SX,g.SY));
            for (int y=y0;y<y1;++y) {for (int x=x0;x<x1;++x) {const float wx=(x+.5f)/scale,wy=(y+.5f)/scale;if (Clipped(e,wx,wy)) {continue;}const auto xy=g.Local(wx,wy);const float lx=xy[0],ly=xy[1];auto* target=pixels.data()+(static_cast<std::size_t>(y)*width+x)*4;std::uint8_t layer[4]{};auto* dst=layer;
                if (s.Shadow[3]>0) {const float sd=Distance(lx-s.ShadowX,ly-s.ShadowY,g.W,g.H,s.Radius);const float coverage=s.ShadowBlur>0 ? .5f*std::erfc(sd/(s.ShadowBlur*.70710678f)):std::clamp(.5f-sd/aa,0.0f,1.0f);Blend(dst,s.Shadow,coverage);}
                const float distance=Distance(lx,ly,g.W,g.H,s.Radius),coverage=std::clamp(.5f-distance/aa,0.0f,1.0f);if (coverage<=0) {Composite(target,layer,s.Opacity);continue;}
                const float u=std::clamp(lx/g.W,0.0f,1.0f),v=std::clamp(ly/g.H,0.0f,1.0f);UiColor fill;for (int k=0;k<4;++k) {fill[k]=std::lerp(std::lerp(s.Fill[0][k],s.Fill[1][k],u),std::lerp(s.Fill[3][k],s.Fill[2][k],u),v);}Blend(dst,fill,coverage);
                if (s.BorderWidth>0) {Blend(dst,s.Border,std::clamp((distance+s.BorderWidth)/aa+.5f,0.0f,1.0f)*coverage);}
                if (!text.empty()&&lx>=0&&ly>=0&&lx<g.W&&ly<g.H) {const int tx=std::clamp(static_cast<int>(lx*scale),0,tw-1),ty=std::clamp(static_cast<int>(ly*scale),0,th-1);Blend(dst,s.Text,text[(static_cast<std::size_t>(ty)*tw+tx)*4+3]/255.0f*coverage);}Composite(target,layer,s.Opacity);
            }}
        }return Result<std::vector<std::uint8_t>>::Success(std::move(pixels));
    }catch (...) {return Result<std::vector<std::uint8_t>>::Failure(ErrorFromException(ErrorCode::InvalidData));}
}
} // namespace lv
