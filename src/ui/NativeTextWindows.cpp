#include "NativeText.h"
#include <dwrite_1.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <algorithm>

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
std::vector<std::uint8_t> NativeText(const UiElement& e,const std::filesystem::path& directory,int width,int height,float scale) {
    ComApartment apartment;const auto& t=e.Typography;const auto file=FontPath(directory,t.FontFile);Collection c(file);
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);const bool systemFont=SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)!=0;
    const auto family=t.Family.empty() ? (file.empty() ? std::wstring(systemFont ? metrics.lfMessageFont.lfFaceName:L"Segoe UI"):Family(c.Fonts.Get(),0)):Wide(t.Family);
    UINT32 index=0;BOOL exists=FALSE;Check(c.Fonts->FindFamilyName(family.c_str(),&index,&exists));if (!exists) {throw std::invalid_argument("Requested font family is unavailable in the selected system/file collection.");}
    ComPtr<IDWriteTextFormat> format;Check(c.Factory->CreateTextFormat(family.c_str(),c.Fonts.Get(),static_cast<DWRITE_FONT_WEIGHT>(t.Weight),t.Italic ? DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,t.Size,L"",&format));
    Check(format->SetTextAlignment(t.Horizontal==UiAlign::Start ? DWRITE_TEXT_ALIGNMENT_LEADING:t.Horizontal==UiAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_TRAILING));
    Check(format->SetParagraphAlignment(t.Vertical==UiAlign::Start ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR:t.Vertical==UiAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_FAR));
    Check(format->SetWordWrapping(t.Wrap ? DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP));Check(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,t.Size*t.LineHeight,t.Size*.9f));
    const auto text=Wide(e.Text);ComPtr<IDWriteTextLayout> layout;const float w=e.Layout.Width-t.Padding[0]-t.Padding[2],h=e.Layout.Height-t.Padding[1]-t.Padding[3];
    Check(c.Factory->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format.Get(),w,h,&layout));DWRITE_TEXT_RANGE range{0,static_cast<UINT32>(text.size())};Check(layout->SetUnderline(t.Underline,range));Check(layout->SetStrikethrough(t.StrikeThrough,range));
    if (t.LetterSpacing!=0) {ComPtr<IDWriteTextLayout1> layout1;Check(layout.As(&layout1));Check(layout1->SetCharacterSpacing(0,t.LetterSpacing,0,range));}
    ComPtr<IWICImagingFactory> wic;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));ComPtr<IWICBitmap> bitmap;Check(wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
    ComPtr<ID2D1Factory> d2d;Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()));ComPtr<ID2D1RenderTarget> target;
    const auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));Check(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),props,&target));
    ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&brush));target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale));target->BeginDraw();target->Clear(D2D1::ColorF(0,0,0,0));target->PushAxisAlignedClip(D2D1::RectF(t.Padding[0],t.Padding[1],e.Layout.Width-t.Padding[2],e.Layout.Height-t.Padding[3]),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);target->DrawTextLayout(D2D1::Point2F(t.Padding[0],t.Padding[1]),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);target->PopAxisAlignedClip();Check(target->EndDraw());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);Check(bitmap->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));return pixels; // White mask: BGRA == RGBA.
}
}
