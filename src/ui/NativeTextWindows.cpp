#include "NativeText.h"
#include <dwrite_1.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <map>

namespace lv::detail {
namespace {
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) {if (FAILED(hr)) {throw std::runtime_error("Native font/layout operation failed (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+").");}}
std::wstring Wide(const std::string& s) {if (s.empty()) {return {};}const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if (!n) {throw std::invalid_argument("Invalid UTF-8 text.");}std::wstring result(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n);return result;}
std::string Narrow(const wchar_t* s) {const auto n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,s,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;}
class Enumerator final:public IDWriteFontFileEnumerator {
    std::atomic<ULONG> refs_{1};ComPtr<IDWriteFontFile> file_;bool moved_=false;
public:
    Enumerator(IDWriteFactory* factory,const wchar_t* path) {Check(factory->CreateFontFileReference(path,nullptr,&file_));}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if (!out) {return E_POINTER;}*out=nullptr;if (id==__uuidof(IUnknown)||id==__uuidof(IDWriteFontFileEnumerator)) {*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs_;if (!n) {delete this;}return n;}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* more) override {if (!more) {return E_POINTER;}*more=!moved_;moved_=true;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file) override {if (!file) {return E_POINTER;}return file_.CopyTo(file);}
};
class Loader final:public IDWriteFontCollectionLoader {
    std::atomic<ULONG> refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if (!out) {return E_POINTER;}*out=nullptr;if (id==__uuidof(IUnknown)||id==__uuidof(IDWriteFontCollectionLoader)) {*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs_;if (!n) {delete this;}return n;}
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory,const void* key,UINT32 size,IDWriteFontFileEnumerator** out) override {
        if (!out||!key||size<sizeof(wchar_t)) {return E_INVALIDARG;}*out=nullptr;try {*out=new Enumerator(factory,static_cast<const wchar_t*>(key));return S_OK;}catch (...) {return E_FAIL;}
    }
};
struct Collection {
    std::wstring Path;
    ComPtr<IDWriteFactory> Factory;ComPtr<Loader> Custom;ComPtr<IDWriteFontCollection> Fonts;
    explicit Collection(const std::filesystem::path& file) {
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(Factory.GetAddressOf())));
        if (file.empty()) {Check(Factory->GetSystemFontCollection(&Fonts));}
        else {Custom.Attach(new Loader);Check(Factory->RegisterFontCollectionLoader(Custom.Get()));Path=file.wstring();const auto hr=Factory->CreateCustomFontCollection(Custom.Get(),Path.c_str(),static_cast<UINT32>((Path.size()+1)*sizeof(wchar_t)),&Fonts);if (FAILED(hr)||!Fonts->GetFontFamilyCount()) {Factory->UnregisterFontCollectionLoader(Custom.Get());Custom.Reset();Check(hr);throw std::invalid_argument("Font file contains no usable font families.");}}
    }
    ~Collection() {Fonts.Reset();if (Custom) {Factory->UnregisterFontCollectionLoader(Custom.Get());}}
};
std::wstring Family(IDWriteFontCollection* fonts,UINT32 index) {ComPtr<IDWriteFontFamily> f;Check(fonts->GetFontFamily(index,&f));ComPtr<IDWriteLocalizedStrings> names;Check(f->GetFamilyNames(&names));UINT32 i=0;BOOL found=FALSE;names->FindLocaleName(L"en-us",&i,&found);if (!found) {i=0;}UINT32 length=0;Check(names->GetStringLength(i,&length));std::wstring result(length+1,L'\0');Check(names->GetString(i,result.data(),length+1));result.resize(length);return result;}
struct ComApartment {bool Release=false;ComApartment() {const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if (hr!=RPC_E_CHANGED_MODE) {Check(hr);Release=true;}}~ComApartment() {if (Release) {CoUninitialize();}}};
}
std::vector<UiFont> NativeFonts(const std::filesystem::path& file) {Collection c(file);std::vector<UiFont> out;for (UINT32 i=0;i<c.Fonts->GetFontFamilyCount();++i) {out.push_back({Narrow(Family(c.Fonts.Get(),i).c_str()),file});}return out;}
namespace {
struct TextData {
    Collection System{{}};
    std::map<std::filesystem::path,std::unique_ptr<Collection>> Files;
    ComPtr<IDWriteTextLayout> Layout;
    std::vector<NativeRun> Runs;
    std::vector<DWRITE_TEXT_RANGE> Ranges;
    IDWriteFontCollection* Fonts(const UiTextStyle& t,const std::filesystem::path& directory) {
        const auto file=FontPath(directory,t.FontFile);if (file.empty()) {return System.Fonts.Get();}
        auto it=Files.find(file);if (it==Files.end()) {it=Files.emplace(file,std::make_unique<Collection>(file)).first;}return it->second->Fonts.Get();
    }
    std::wstring Name(const UiTextStyle& t,IDWriteFontCollection* fonts) {
        NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);const bool system=SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)!=0;
        const auto name=t.Family.empty() ? (t.FontFile.empty() ? std::wstring(system ? metrics.lfMessageFont.lfFaceName:L"Segoe UI"):Family(fonts,0)):Wide(t.Family);
        UINT32 index=0;BOOL exists=FALSE;Check(fonts->FindFamilyName(name.c_str(),&index,&exists));if (!exists) {throw std::invalid_argument("Requested font family is unavailable in the selected system/file collection.");}return name;
    }
    TextData(const UiElement& e,const std::filesystem::path& directory,float width,float height,const UiColor& color,bool measure=false) {
        const auto& t=e.Typography;auto* fonts=Fonts(t,directory);const auto family=Name(t,fonts);ComPtr<IDWriteTextFormat> format;
        Check(System.Factory->CreateTextFormat(family.c_str(),fonts,static_cast<DWRITE_FONT_WEIGHT>(t.Weight),t.Italic ? DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,t.Size,L"",&format));
        Check(format->SetTextAlignment(measure ? DWRITE_TEXT_ALIGNMENT_LEADING:t.Horizontal==UiAlign::Start ? DWRITE_TEXT_ALIGNMENT_LEADING:t.Horizontal==UiAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_TRAILING));
        Check(format->SetParagraphAlignment(measure ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR:t.Vertical==UiAlign::Start ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR:t.Vertical==UiAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_FAR));
        Check(format->SetWordWrapping(t.Wrap ? DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP));Runs=TextRuns(e,color);std::wstring text;float maxSize=t.Size;
        for (const auto& r:Runs) {const auto wide=Wide(r.Text);Ranges.push_back({static_cast<UINT32>(text.size()),static_cast<UINT32>(wide.size())});text+=wide;maxSize=std::max(maxSize,r.Style.Size);}
        Check(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,maxSize*t.LineHeight,maxSize*.9f));
        Check(System.Factory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format.Get(),std::max(1.0f,width-t.Padding[0]-t.Padding[2]),std::max(1.0f,height-t.Padding[1]-t.Padding[3]),&Layout));
        ComPtr<IDWriteTextLayout1> spacing;Check(Layout.As(&spacing));
        for (std::size_t i=0;i<Runs.size();++i) {const auto& style=Runs[i].Style;const auto range=Ranges[i];if (!range.length) {continue;}auto* collection=Fonts(style,directory);const auto name=Name(style,collection);
            Check(Layout->SetFontCollection(collection,range));Check(Layout->SetFontFamilyName(name.c_str(),range));Check(Layout->SetFontSize(style.Size,range));Check(Layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(style.Weight),range));Check(Layout->SetFontStyle(style.Italic ? DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,range));Check(Layout->SetUnderline(style.Underline,range));Check(Layout->SetStrikethrough(style.StrikeThrough,range));Check(spacing->SetCharacterSpacing(0,style.LetterSpacing,0,range));
        }
    }
};
}
std::array<float,2> NativeMeasure(const UiElement& e,const std::filesystem::path& directory,float width) {
    TextData text(e,directory,width,65536,{1,1,1,1},true);DWRITE_TEXT_METRICS metrics{};Check(text.Layout->GetMetrics(&metrics));const auto& p=e.Typography.Padding;return {std::ceil(metrics.widthIncludingTrailingWhitespace+p[0]+p[2]),std::ceil(metrics.height+p[1]+p[3])};
}
std::vector<std::uint8_t> NativeText(const UiElement& e,const std::filesystem::path& directory,int width,int height,float scale,const UiColor& color) {
    ComApartment apartment;TextData text(e,directory,e.Layout.Width,e.Layout.Height,color);const auto& t=e.Typography;
    ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> d2d;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()));ComPtr<ID2D1RenderTarget> target;const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));Check(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target));
    ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(color[0],color[1],color[2],color[3]),&brush));
    for (std::size_t i=0;i<text.Runs.size();++i) {if (!text.Ranges[i].length) {continue;}const auto c=text.Runs[i].Color;ComPtr<ID2D1SolidColorBrush> effect;Check(target->CreateSolidColorBrush(D2D1::ColorF(c[0],c[1],c[2],c[3]),&effect));Check(text.Layout->SetDrawingEffect(effect.Get(),text.Ranges[i]));}
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale));target->BeginDraw();target->Clear(D2D1::ColorF(0,0,0,0));target->PushAxisAlignedClip(D2D1::RectF(t.Padding[0],t.Padding[1],e.Layout.Width-t.Padding[2],e.Layout.Height-t.Padding[3]),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target->DrawTextLayout(D2D1::Point2F(t.Padding[0],t.Padding[1]),text.Layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);target->PopAxisAlignedClip();Check(target->EndDraw());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);Check(bitmap->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));for (std::size_t i=0;i<pixels.size();i+=4) {std::swap(pixels[i],pixels[i+2]);}return pixels;
}
}
