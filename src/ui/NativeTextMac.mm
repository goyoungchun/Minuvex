#include "NativeText.h"
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include <algorithm>

namespace lv::detail {
namespace {
NSString* String(const std::string& s) {return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];}
NSArray* Descriptors(const std::filesystem::path& file) {const auto s=file.u8string();auto* path=String({s.begin(),s.end()});auto descriptors=CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)[NSURL fileURLWithPath:path]);if (!descriptors) {throw std::invalid_argument("Cannot read Font file.");}return CFBridgingRelease(descriptors);}
}
std::vector<UiFont> NativeFonts(const std::filesystem::path& file) {
    @autoreleasepool {std::vector<UiFont> result;if (file.empty()) {for (NSString* family in [[NSFontManager sharedFontManager] availableFontFamilies]) {result.push_back({[family UTF8String],{}});}}
        else {for (id d in Descriptors(file)) {auto family=CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)d,kCTFontFamilyNameAttribute);if (family) {NSString* name=CFBridgingRelease(family);result.push_back({[name UTF8String],file});}}}return result;}
}
std::vector<std::uint8_t> NativeText(const UiElement& e,const std::filesystem::path& directory,int width,int height,float scale) {
    @autoreleasepool {
        const auto& t=e.Typography;NSFont* font=nil;const auto file=FontPath(directory,t.FontFile);
        if (!file.empty()) {for (id descriptor in Descriptors(file)) {auto d=(__bridge CTFontDescriptorRef)descriptor;NSString* family=CFBridgingRelease(CTFontDescriptorCopyAttribute(d,kCTFontFamilyNameAttribute));if (t.Family.empty()||[family isEqualToString:String(t.Family)]) {font=CFBridgingRelease(CTFontCreateWithFontDescriptor(d,t.Size,nullptr));break;}}}
        else if (t.Family.empty()) {font=[NSFont systemFontOfSize:t.Size weight:std::clamp((t.Weight-400)/500.0,-1.0,1.0)];}
        else {font=[[NSFontManager sharedFontManager] fontWithFamily:String(t.Family) traits:t.Italic ? NSItalicFontMask:0 weight:std::clamp(t.Weight/100,1,9) size:t.Size];}
        if (!font) {throw std::invalid_argument("Requested font family is unavailable in the selected system/file collection.");}
        if (t.Italic) {font=[[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSItalicFontMask];}
        auto* paragraph=[[NSMutableParagraphStyle alloc] init];paragraph.alignment=t.Horizontal==UiAlign::Start ? NSTextAlignmentLeft:t.Horizontal==UiAlign::Center ? NSTextAlignmentCenter:NSTextAlignmentRight;paragraph.lineBreakMode=t.Wrap ? NSLineBreakByWordWrapping:NSLineBreakByClipping;paragraph.minimumLineHeight=paragraph.maximumLineHeight=t.Size*t.LineHeight;
        auto* attributes=[NSMutableDictionary dictionaryWithDictionary:@{NSFontAttributeName:font,NSForegroundColorAttributeName:[NSColor whiteColor],NSParagraphStyleAttributeName:paragraph,NSKernAttributeName:@(t.LetterSpacing)}];if (t.Underline) {attributes[NSUnderlineStyleAttributeName]=@(NSUnderlineStyleSingle);}if (t.StrikeThrough) {attributes[NSStrikethroughStyleAttributeName]=@(NSUnderlineStyleSingle);}
        auto* text=[[NSAttributedString alloc] initWithString:String(e.Text) attributes:attributes];const float w=e.Layout.Width-t.Padding[0]-t.Padding[2],h=e.Layout.Height-t.Padding[1]-t.Padding[3];
        const auto framesetter=CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)text);const auto suggested=CTFramesetterSuggestFrameSizeWithConstraints(framesetter,CFRangeMake(0,0),nullptr,CGSizeMake(w,CGFLOAT_MAX),nullptr);const float used=std::min(h,static_cast<float>(suggested.height));const float y=t.Padding[1]+(t.Vertical==UiAlign::Start ? 0:t.Vertical==UiAlign::Center ? (h-used)*.5f:h-used);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);const auto cs=CGColorSpaceCreateDeviceRGB();const auto ctx=CGBitmapContextCreate(pixels.data(),width,height,8,width*4,cs,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);CGColorSpaceRelease(cs);if (!ctx) {CFRelease(framesetter);throw std::runtime_error("Cannot create native text bitmap.");}
        CGContextScaleCTM(ctx,scale,scale);CGContextSetTextMatrix(ctx,CGAffineTransformIdentity);CGContextClipToRect(ctx,CGRectMake(t.Padding[0],e.Layout.Height-t.Padding[1]-h,w,h));const auto path=CGPathCreateWithRect(CGRectMake(t.Padding[0],e.Layout.Height-y-used,w,used),nullptr);const auto frame=CTFramesetterCreateFrame(framesetter,CFRangeMake(0,0),path,nullptr);CTFrameDraw(frame,ctx);CFRelease(frame);CGPathRelease(path);CFRelease(framesetter);CGContextRelease(ctx);
        return pixels;
    }
}
}
