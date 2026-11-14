#include <minvex/Viewer.h>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

int main(int argc,char** argv) {
    try {
        lv::ViewerRunOptions run;
        for (int i=1;i<argc;++i) {
            if (std::string_view(argv[i])!="--frames"||i+1>=argc) {return 2;}
            const std::string_view text=argv[++i];
            const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),run.FrameLimit);
            if (error!=std::errc{}||end!=text.data()+text.size()) {return 2;}
        }
        lv::ViewerOptions options;options.Title="Minvex UI Overlay";options.AutoFrame=false;
        options.Quality=lv::MakeQualityProfile(lv::QualityPreset::Low);
        lv::Viewer viewer(options);
        auto model=std::make_shared<lv::ModelAsset>();
        model->Meshes.push_back({"Cube",{{lv::MakeCube(),0,false}}});model->Parts.push_back({0});
        model->Materials.push_back({});model->Materials[0].BaseColor={.4f,.65f,1,1};
        model->ModelBounds={{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};
        const auto object=viewer.GetScene().AddModel(model);viewer.GetScene().MainCamera.Position={2,1.5f,3};
        auto& ui=viewer.GetUi();ui.SetFontDirectory("Font",true).Value();

        lv::UiElement card;card.Kind=lv::UiKind::Panel;card.Name="Card";
        card.Layout={36,36,370,350};card.Layout.HeightSizing=lv::UiSizing::Hug;
        card.Container.Flow=lv::UiFlow::Column;card.Container.Padding.fill(24);card.Container.GapY=12;
        card.Normal.Fill.fill({.07f,.1f,.17f,.96f});card.Normal.Radius.fill(24);
        card.Normal.Border={.3f,.5f,.7f,.4f};card.Normal.BorderWidth=1;
        card.Normal.Shadow={0,0,0,.45f};card.Normal.ShadowBlur=18;card.Normal.ShadowY=12;
        const auto cardId=ui.Add(card).Value();

        lv::UiElement title;title.Parent=cardId;title.Name="Title";
        title.Layout.WidthSizing=lv::UiSizing::Fill;title.Layout.HeightSizing=lv::UiSizing::Hug;
        title.Typography.Size=30;title.Typography.Weight=700;title.Typography.Padding.fill(0);
        title.BlocksInput=false;lv::UiTextRun prefix;prefix.Text="Make it ";
        lv::UiTextRun accent;accent.Text="yours.";accent.Color=lv::UiColor{.65f,.5f,1,1};
        title.Runs={prefix,accent};const auto titleId=ui.Add(title).Value();

        lv::UiElement description;description.Parent=cardId;description.Name="Description";
        description.Text="3D 위에 나만의 UI\nSystem fonts. Rich typography.\n\nDrop TTF / OTF / TTC into Font.";
        description.Layout.WidthSizing=lv::UiSizing::Fill;description.Layout.HeightSizing=lv::UiSizing::Hug;
        description.Typography.Size=16;description.Typography.Padding.fill(0);
        description.Normal.Text={.7f,.8f,.9f,1};description.BlocksInput=false;ui.Add(description).Value();

        lv::UiElement button;button.Kind=lv::UiKind::Button;button.Parent=cardId;button.Name="Rotate";
        button.Text="Toggle model rotation";button.Layout={0,0,304,52};button.Layout.WidthSizing=lv::UiSizing::Fill;
        button.Typography.Horizontal=lv::UiAlign::Center;button.Typography.Weight=600;
        button.Normal.Fill={lv::UiColor{.24f,.4f,.96f,1},lv::UiColor{.5f,.32f,.95f,1},lv::UiColor{.4f,.26f,.8f,1},lv::UiColor{.18f,.3f,.85f,1}};
        button.Normal.Radius.fill(14);button.Hover=button.Normal;button.Hover->Fill.fill({.45f,.4f,1,1});
        button.Pressed=button.Normal;button.Pressed->Fill.fill({.2f,.2f,.65f,1});
        button.Focused=button.Normal;button.Focused->Border={1,1,1,1};button.Focused->BorderWidth=2;
        const auto rotate=ui.Add(button).Value();button.Name="Theme";button.Text="Change card theme";
        const auto theme=ui.Add(button).Value();

        lv::UiElement toolbar;toolbar.Kind=lv::UiKind::Panel;toolbar.Parent=cardId;
        toolbar.Layout={0,0,304,44};toolbar.Layout.WidthSizing=lv::UiSizing::Fill;
        toolbar.Container.Flow=lv::UiFlow::Row;toolbar.Container.Align=lv::UiCrossAlign::Stretch;
        const auto toolbarId=ui.Add(toolbar).Value();button.Parent=toolbarId;
        button.Layout.HeightSizing=lv::UiSizing::Fill;button.Text="Undo UI";button.Name="Undo";
        const auto undo=ui.Add(button).Value();button.Text="Redo UI";button.Name="Redo";
        const auto redo=ui.Add(button).Value();ui.ClearHistory().Value();

        bool turning=false;float angle=0;
        run.OnFrame=[&](lv::Viewer& v,const lv::ViewerFrameInfo& frame) {
            const auto events=v.GetUi().PollActivations().Value();
            for (auto id:events) {
                if (id==rotate) {turning=!turning;}
                if (id==theme) {
                    auto panel=ui.Get(cardId).Value();auto heading=ui.Get(titleId).Value();
                    const bool blue=panel.Normal.Fill[0][0]<.1f;
                    panel.Normal.Fill.fill(blue ? lv::UiColor{.13f,.08f,.2f,.96f}:lv::UiColor{.07f,.1f,.17f,.96f});
                    heading.Runs[1].Color=blue ? lv::UiColor{1,.55f,.35f,1}:lv::UiColor{.65f,.5f,1,1};
                    ui.BeginEdit().Value();ui.Set(cardId,panel).Value();ui.Set(titleId,heading).Value();ui.CommitEdit().Value();
                }
                const auto history=ui.History().Value();
                if (id==undo&&history.CanUndo) {ui.Undo().Value();}
                if (id==redo&&history.CanRedo) {ui.Redo().Value();}
            }
            if (turning) {angle+=static_cast<float>(frame.DeltaTimeSeconds);auto transform=v.GetScene().GetObject(object).LocalTransform;
                transform.Rotation={0,std::sin(angle*.5f),0,std::cos(angle*.5f)};v.SetTransform(object,transform).Value();}
        };
        const auto result=viewer.Run(run);
        if (!result) {std::fprintf(stderr,"%s\n",result.Error().Message.c_str());return 1;}
        return 0;
    }catch (const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
