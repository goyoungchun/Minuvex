#include <minvex/Viewer.h>
#include <charconv>
#include <cstdio>
#include <cmath>
#include <string_view>
int main(int argc,char** argv) {
    try {
        lv::ViewerRunOptions run;for (int i=1;i<argc;++i) {if (std::string_view(argv[i])=="--frames"&&i+1<argc) {const std::string_view text=argv[++i];const auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),run.FrameLimit);if (ec!=std::errc{}||end!=text.data()+text.size()) {return 2;}}else {return 2;}}
        lv::ViewerOptions options;options.Title="Minvex UI Overlay";options.AutoFrame=false;options.Quality=lv::MakeQualityProfile(lv::QualityPreset::Low);lv::Viewer viewer(options);
        auto model=std::make_shared<lv::ModelAsset>();model->Meshes.push_back({"Cube",{{lv::MakeCube(),0,false}}});model->Parts.push_back({0});model->Materials.push_back({});model->Materials[0].BaseColor={.4f,.65f,1,1};model->ModelBounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};auto object=viewer.GetScene().AddModel(model);viewer.GetScene().MainCamera.Position={2,1.5f,3};
        auto& ui=viewer.GetUi();ui.SetFontDirectory("Font",true).Value();
        lv::UiElement card;card.Kind=lv::UiKind::Panel;card.Name="Card";card.Layout={36,36,360,350};card.Normal.Fill={lv::UiColor{.08f,.12f,.2f,.96f},lv::UiColor{.12f,.2f,.28f,.96f},lv::UiColor{.06f,.09f,.16f,.96f},lv::UiColor{.06f,.09f,.16f,.96f}};card.Normal.Radius.fill(24);card.Normal.Border={.3f,.5f,.7f,.4f};card.Normal.BorderWidth=1;card.Normal.Shadow={0,0,0,.45f};card.Normal.ShadowBlur=18;card.Normal.ShadowY=12;ui.Add(card).Value();
        lv::UiElement title;title.Name="Title";title.Text="Make it yours.";title.Layout={52,58,320,62};title.Typography.Size=34;title.Typography.Weight=700;title.BlocksInput=false;ui.Add(title).Value();
        lv::UiElement text;text.Name="Description";text.Text="3D 위에 나만의 UI\nSystem fonts. Your own style.\n\nDrop TTF / OTF / TTC into Font.";text.Layout={52,132,320,140};text.Typography.Size=17;text.Typography.Vertical=lv::UiAlign::Start;text.Normal.Text={.7f,.8f,.9f,1};text.BlocksInput=false;ui.Add(text).Value();
        lv::UiElement button;button.Kind=lv::UiKind::Button;button.Name="Rotate";button.Text="Rotate model  →";button.Layout={64,294,304,56};button.Typography.Horizontal=lv::UiAlign::Center;button.Typography.Weight=600;button.Normal.Fill={lv::UiColor{.24f,.4f,.96f,1},lv::UiColor{.5f,.32f,.95f,1},lv::UiColor{.4f,.26f,.8f,1},lv::UiColor{.18f,.3f,.85f,1}};button.Normal.Radius.fill(14);button.Hover=button.Normal;button.Hover->Fill.fill({.45f,.4f,1,1});button.Pressed=button.Normal;button.Pressed->Fill.fill({.2f,.2f,.65f,1});button.Focused=button.Normal;button.Focused->Border={1,1,1,1};button.Focused->BorderWidth=2;const auto rotate=ui.Add(button).Value();
        lv::UiElement hint;hint.Text="Tab + Enter / Space or click to interact";hint.Layout={0,-40,500,30};hint.Layout.AnchorX=.5f;hint.Layout.AnchorY=1;hint.Layout.PivotX=.5f;hint.Typography.Size=14;hint.Typography.Horizontal=lv::UiAlign::Center;hint.Normal.Text={.6f,.7f,.8f,1};hint.BlocksInput=false;ui.Add(hint).Value();
        bool turning=false;run.OnFrame=[&](lv::Viewer& v,const lv::ViewerFrameInfo& f) {const auto activations=v.GetUi().PollActivations().Value();for (auto id:activations) {if (id==rotate) {turning=!turning;auto b=v.GetUi().Get(rotate).Value();b.Text=turning ? "Pause rotation":"Rotate model  →";v.GetUi().Set(rotate,b).Value();}}if (turning) {auto t=v.GetScene().GetObject(object).LocalTransform;const auto angle=static_cast<float>(f.FrameNumber)*.01f;t.Rotation={0,std::sin(angle),0,std::cos(angle)};v.SetTransform(object,t).Value();}};
        const auto result=viewer.Run(run);if (!result) {std::fprintf(stderr,"%s\n",result.Error().Message.c_str());return 1;}return 0;
    }catch (const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
