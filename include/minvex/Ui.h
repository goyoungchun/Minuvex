#pragma once
#include <minvex/Result.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lv {
using UiId = std::uint64_t;
inline constexpr UiId NoUi = 0;
using UiColor = std::array<float,4>; // Straight sRGB + linear alpha, [0,1].
enum class UiKind { Text, Button, Panel };
enum class UiAlign { Start, Center, End };
struct UiRect { float X=0,Y=0,Width=0,Height=0; bool operator==(const UiRect&) const = default; };
enum class UiSizing { Fixed, Fill, Hug };
enum class UiFlow { Absolute, Row, Column, Grid };
enum class UiJustify { Start, Center, End, SpaceBetween, SpaceAround };
enum class UiCrossAlign { Start, Center, End, Stretch };
enum class UiOverflow { Visible, Clip, Scroll };
struct UiLayout {
    float X=24,Y=24,Width=240,Height=48;
    float AnchorX=0,AnchorY=0,PivotX=0,PivotY=0;
    float Rotation=0,ScaleX=1,ScaleY=1; // Radians; rotation/scale around pivot.
    int Z=0; // Sibling stacking order; stable insertion order within each Z.
    UiSizing WidthSizing=UiSizing::Fixed,HeightSizing=UiSizing::Fixed;
    float MinWidth=1,MinHeight=1,MaxWidth=2048,MaxHeight=2048,Flex=1;
    bool Absolute=false; // Opt out of the parent's flow.
    int Order=0; // Flow layout order; does not change Z or keyboard creation order.
    bool operator==(const UiLayout&) const = default;
};
struct UiContainer {
    UiFlow Flow=UiFlow::Absolute;
    std::array<float,4> Padding{0,0,0,0};
    float GapX=8,GapY=8;
    int Columns=2; // Uniform grid tracks.
    UiJustify Justify=UiJustify::Start;
    UiCrossAlign Align=UiCrossAlign::Start;
    UiOverflow Overflow=UiOverflow::Visible;
    bool operator==(const UiContainer&) const = default;
};
struct UiTextStyle {
    std::string Family; // Empty selects OS UI font. UTF-8.
    std::filesystem::path FontFile; // Empty = system; otherwise relative to Font directory.
    float Size=18,LineHeight=1.25f,LetterSpacing=0;
    int Weight=400; // 100..900
    bool Italic=false,Underline=false,StrikeThrough=false,Wrap=true;
    UiAlign Horizontal=UiAlign::Start,Vertical=UiAlign::Center;
    std::array<float,4> Padding{12,8,12,8}; // Left, top, right, bottom.
    bool operator==(const UiTextStyle&) const = default;
};
// Missing overrides inherit the element's typography / active text color.
struct UiTextRun {
    std::string Text;
    std::optional<std::string> Family;
    std::optional<std::filesystem::path> FontFile;
    std::optional<float> Size,LetterSpacing;
    std::optional<int> Weight;
    std::optional<bool> Italic,Underline,StrikeThrough;
    std::optional<UiColor> Color;
    bool operator==(const UiTextRun&) const = default;
};
struct UiStyle {
    UiColor Text{1,1,1,1};
    std::array<UiColor,4> Fill{{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}}; // TL, TR, BR, BL.
    std::array<float,4> Radius{0,0,0,0}; // TL, TR, BR, BL, logical pixels.
    UiColor Border{0,0,0,0},Shadow{0,0,0,0};
    float BorderWidth=0,ShadowX=0,ShadowY=4,ShadowBlur=8,Opacity=1;
    bool operator==(const UiStyle&) const = default;
};
struct UiElement {
    UiId Id=NoUi; // Assigned by Add; ignored in Add's description.
    std::string Name,Text;
    UiKind Kind=UiKind::Text;
    UiLayout Layout;
    UiTextStyle Typography;
    UiStyle Normal;
    // Optional complete overrides, not partial patches.
    std::optional<UiStyle> Hover,Pressed,Focused,Disabled;
    bool Visible=true,Enabled=true,BlocksInput=true;
    std::optional<UiRect> Clip; // Viewport-aligned logical clip, also clips shadow/input.
    UiId Parent=NoUi; // Parent must be a Panel; Remove removes its subtree.
    UiContainer Container;
    std::vector<UiTextRun> Runs; // When nonempty, replaces Text for display.
    bool operator==(const UiElement&) const = default;
};
struct UiLayoutBox {
    UiId Id=NoUi;
    float Width=0,Height=0,ContentWidth=0,ContentHeight=0,ScrollX=0,ScrollY=0;
    // Local top-left to viewport: x'=a*x+c*y+tx, y'=b*x+d*y+ty.
    std::array<float,6> Transform{1,0,0,1,0,0};
    bool Visible=true,Enabled=true;
};
struct UiHistoryState { bool CanUndo=false,CanRedo=false,Editing=false,Dirty=false; std::size_t UndoCount=0,RedoCount=0; };
struct UiFont { std::string Family; std::filesystem::path File; };
enum class UiInputType { Move,Down,Up,Cancel,Tab,ActivateDown,ActivateUp,Scroll };
struct UiInput { UiInputType Type=UiInputType::Move; float X=0,Y=0; bool Shift=false,Repeat=false; float ScrollX=0,ScrollY=0; };
// Retained, viewport-space overlay; construct/use/destroy on one owner thread.
// No global font installation, callbacks, graphics handles or Scene ownership.
class UiCanvas final {
public:
    UiCanvas();
    ~UiCanvas();
    UiCanvas(const UiCanvas&)=delete;
    UiCanvas& operator=(const UiCanvas&)=delete;
    Result<UiId> Add(const UiElement& description);
    Result<void> Set(UiId id,const UiElement& description);
    Result<UiElement> Get(UiId id) const; // Independent copy.
    Result<std::vector<UiElement>> Elements() const;
    Result<void> Remove(UiId id);
    Result<void> Clear();
    Result<std::vector<UiLayoutBox>> ResolveLayout(float logicalWidth,float logicalHeight) const;
    Result<void> SetScroll(UiId id,float x,float y); // Runtime only, clamped during layout.
    Result<void> BeginEdit();
    Result<void> CommitEdit();
    Result<void> CancelEdit();
    Result<void> Undo();
    Result<void> Redo();
    Result<UiHistoryState> History() const;
    Result<void> ClearHistory(); // Keeps content and its dirty state.
    Result<bool> ProcessInput(const UiInput& input,float logicalWidth,float logicalHeight);
    Result<std::vector<UiId>> PollActivations(); // Drains queue; at most 1024.
    Result<void> SetFontDirectory(const std::filesystem::path& directory,bool create=false);
    Result<std::filesystem::path> FontDirectory() const;
    Result<std::vector<UiFont>> Fonts(bool includeSystem=true) const; // Explicit rescan.
    Result<void> RefreshFonts(); // Invalidates raster cache after changing font files.
    Result<void> Save(const std::filesystem::path& path) const;
    Result<void> Load(const std::filesystem::path& path); // Transactional; runtime IDs remapped.
    std::uint64_t Revision() const noexcept; // Owner thread, for render integration.
    UiId Focused() const noexcept;
    bool Empty() const noexcept;
    // Pixel export uses native text shaping. Premultiplied RGBA8, top-left origin.
    // width/height 1..4096, scale .25..4; unsupported OS returns UnsupportedFeature.
    Result<std::vector<std::uint8_t>> Rasterize(int width,int height,float scale=1) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lv
