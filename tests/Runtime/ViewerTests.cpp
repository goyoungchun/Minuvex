#include "TestHarness.h"
#include <minuvex/Viewer.h>
#include <minuvex/EmbeddedRenderer.h>

#include <atomic>
#include <limits>
#include <thread>

using namespace test;
namespace {
template<typename T>
void ErrorIs(const lv::Result<T>& result, lv::ErrorCode code, const char* message) {
    Check(!result && result.Error().Code == code && !result.Error().Hint.empty(),message);
}
double Distance(const lv::Camera& camera) {
    return std::hypot(double(camera.Position.X)-camera.Target.X,double(camera.Position.Y)-camera.Target.Y,double(camera.Position.Z)-camera.Target.Z);
}
bool Same(lv::Vec3 a, lv::Vec3 b) { return Near(a.X,b.X) && Near(a.Y,b.Y) && Near(a.Z,b.Z); }

void CheckFramed(const lv::Viewer& viewer, float aspect) {
    const auto& scene = viewer.GetScene();
    const auto projection = scene.MainCamera.ViewProjection(aspect);
    bool inside = true;
    for (const auto& item : scene.CollectRenderItems()) {
        const auto matrix = projection * item.WorldTransform;
        const auto& b = item.Model->ModelBounds;
        for (unsigned mask = 0; mask < 8; ++mask) {
            const lv::Vec3 point{mask&1 ? b.Max.X : b.Min.X,mask&2 ? b.Max.Y : b.Min.Y,mask&4 ? b.Max.Z : b.Min.Z};
            const auto clip = matrix.TransformPoint(point);
            const auto& m = matrix.Elements;
            const float w = m[3]*point.X + m[7]*point.Y + m[11]*point.Z + m[15];
            inside = inside && w > 0 && std::abs(clip.X/w) < 1 && std::abs(clip.Y/w) < 1 && clip.Z/w > 0 && clip.Z/w < 1;
        }
    }
    Check(inside,"automatically fitted transformed bounds lie inside the camera frustum");
}

void TestFacade() {
    lv::Viewer viewer;
    Check(!viewer.IsRunning(),"construction does not open a graphics window");
    viewer.RequestClose();
    const auto first = viewer.AddModel(Fixture("SharedBox.glb"));
    Check(first.HasValue() && viewer.GetScene().Size() == 1,"minimal AddModel creates a scene instance");
    if (!first) { throw std::runtime_error(first.Error().Message); }
    CheckFramed(viewer,1280.0f/720);
    lv::ModelOptions options;
    options.Transform.Position = {20,5,-3}; options.Transform.Scale = {-2,3,1};
    const auto second = viewer.AddModel(Fixture("SharedBox.glb"),options);
    Check(second.HasValue(),"parameter object configures another model instance");
    if (!second) { throw std::runtime_error(second.Error().Message); }
    Check(viewer.GetScene().GetObject(first.Value()).Model == viewer.GetScene().GetObject(second.Value()).Model,"Facade repeated loads share the same CPU asset");
    CheckFramed(viewer,1280.0f/720);
    lv::ViewerInputEvent resize;
    resize.Type = lv::ViewerInputType::Resized; resize.Width = 300; resize.Height = 1000;
    Check(viewer.ProcessInput(resize).HasValue(),"resize event updates default framing");
    CheckFramed(viewer,0.3f);
    resize.Width = resize.Height = 0;
    Check(viewer.ProcessInput(resize).HasValue(),"minimized viewport does not divide by zero");
    resize.Width = 300; resize.Height = 1000;
    Check(viewer.ProcessInput(resize).HasValue(),"viewport restoration reframes the scene");
    lv::Camera manual;
    manual.Position = {10,5,7};
    Check(viewer.SetCamera(manual).HasValue(),"camera can be set through the facade");
    Check(viewer.AddModel(Fixture("SharedBox.glb")).HasValue(),"model can be added with a manual camera");
    Check(Same(viewer.GetScene().MainCamera.Position,manual.Position),"adding models preserves an explicitly set camera");
    Check(viewer.FrameScene().HasValue(),"explicit framing restores automatic camera placement");
    CheckFramed(viewer,0.3f);
    lv::Transform moved; moved.Position.X = 2;
    Check(viewer.SetTransform(first.Value(),moved).HasValue() && Near(viewer.GetScene().GetObject(first.Value()).LocalTransform.Position.X,2),"transform facade updates the selected instance");
    Check(viewer.SetVisible(second.Value(),false).HasValue(),"visibility facade updates the instance");
    Check(viewer.RemoveModel(second.Value()).HasValue(),"RemoveModel removes its instance");
    ErrorIs(viewer.RemoveModel(second.Value()),lv::ErrorCode::InvalidHandle,"stale model ID returns structured error");
    lv::Viewer other;
    ErrorIs(other.SetVisible(first.Value(),false),lv::ErrorCode::InvalidHandle,"foreign Viewer ID returns InvalidHandle");
    lv::DirectionalLight light; light.Intensity = 2;
    Check(viewer.SetLight(light).HasValue() && Near(viewer.GetScene().Sun.Intensity,2),"directional light facade applies settings");
}

void TestFailures() {
    lv::Viewer viewer;
    ErrorIs(viewer.FrameScene(),lv::ErrorCode::InvalidState,"empty scene framing has a recoverable error");
    const auto missing = Fixture("does-not-exist.glb");
    const auto absent = viewer.AddModel(missing);
    ErrorIs(absent,lv::ErrorCode::AssetLoad,"missing model maps to AssetLoad");
    Check(absent.Error().SourcePath == missing && viewer.GetScene().Size() == 0,"failed import retains path and leaves Scene unchanged");
    lv::ModelOptions parent;
    parent.Parent = 123456789;
    ErrorIs(viewer.AddModel(missing,parent),lv::ErrorCode::InvalidHandle,"invalid parent is rejected before IO");
    lv::ModelOptions badTransform; badTransform.Transform.Scale.Y = 0;
    ErrorIs(viewer.AddModel(Fixture("SharedBox.glb"),badTransform),lv::ErrorCode::InvalidArgument,"bad model transform is a Result error");
    lv::ModelOptions small;
    small.Import.MaxVertices = 1;
    ErrorIs(viewer.AddModel(Fixture("SharedBox.glb"),small),lv::ErrorCode::InsufficientResource,"import resource limit maps to InsufficientResource");
    TemporaryDirectory temp;
    Write(temp.File("bad.gltf"),"{bad");
    ErrorIs(viewer.AddModel(temp.File("bad.gltf")),lv::ErrorCode::InvalidData,"invalid asset maps to InvalidData");
    Write(temp.File("unsupported.gltf"),Replace(Read(Fixture("SharedBox.gltf")),"\"asset\":","\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],\"asset\":"));
    ErrorIs(viewer.AddModel(temp.File("unsupported.gltf")),lv::ErrorCode::UnsupportedFeature,"unsupported asset maps to UnsupportedFeature");
    const auto valid = viewer.AddModel(Fixture("SharedBox.glb"));
    Check(valid.HasValue(),"viewer recovers after failed model loads");
    if (!valid) { throw std::runtime_error(valid.Error().Message); }
    const auto original = viewer.GetScene().GetObject(valid.Value()).LocalTransform;
    ErrorIs(viewer.SetTransform(valid.Value(),badTransform.Transform),lv::ErrorCode::InvalidArgument,"bad transform update is rejected");
    Check(Near(viewer.GetScene().GetObject(valid.Value()).LocalTransform.Scale.Y,original.Scale.Y),"failed transform update preserves the previous value");
    lv::Camera invalid; invalid.Target = invalid.Position;
    const auto camera = viewer.GetScene().MainCamera;
    ErrorIs(viewer.SetCamera(invalid),lv::ErrorCode::InvalidArgument,"bad camera returns InvalidArgument");
    Check(Same(viewer.GetScene().MainCamera.Position,camera.Position),"failed camera update preserves the previous camera");
    lv::DirectionalLight badLight; badLight.Direction = {};
    ErrorIs(viewer.SetLight(badLight),lv::ErrorCode::InvalidArgument,"bad light returns InvalidArgument");
    const auto group = viewer.GetScene().CreateObject("Group");
    ErrorIs(viewer.RemoveModel(group),lv::ErrorCode::InvalidHandle,"empty group is not treated as a model");
    lv::ModelOptions child; child.Parent = group;
    Check(viewer.AddModel(Fixture("SharedBox.glb"),child).HasValue(),"advanced Scene parents work with simple AddModel");
    lv::ViewerOptions options; options.Width = 0;
    lv::Viewer invalidOptions(options);
    ErrorIs(invalidOptions.Run(),lv::ErrorCode::InvalidArgument,"bad window settings fail before native initialization");
    options = {}; options.CameraControls.MinimumDistance = 0;
    lv::Viewer invalidControls(options);
    ErrorIs(invalidControls.AddModel(Fixture("SharedBox.glb")),lv::ErrorCode::InvalidArgument,"bad camera control settings fail before import");
    for (const auto& title : {std::string(127,'a'),std::string("bad\0title",9),std::string("\xC0\xAF",2),std::string("\xED\xA0\x80",3)}) {
        options = {}; options.Title = title;
        lv::Viewer invalidTitle(options);
        ErrorIs(invalidTitle.Run(),lv::ErrorCode::InvalidArgument,"invalid/truncated UTF-8 title is rejected before native initialization");
    }
    options = {}; options.Title = "Minuvex 모델 🤖";
    lv::Viewer unicodeTitle(options);
    Check(unicodeTitle.FrameScene().Error().Code == lv::ErrorCode::InvalidState,"valid Unicode title passes configuration validation");
    options = {}; options.AutoFrame = false;
    lv::Viewer unframed(options);
    const auto originalCamera = unframed.GetScene().MainCamera;
    Check(unframed.AddModel(Fixture("SharedBox.glb")).HasValue() && Same(originalCamera.Position,unframed.GetScene().MainCamera.Position),"automatic initial framing can be disabled");
    viewer.GetScene().MainCamera.Target = viewer.GetScene().MainCamera.Position;
    ErrorIs(viewer.Run(),lv::ErrorCode::InvalidArgument,"advanced invalid camera is validated before native initialization");
    const auto count = viewer.GetScene().Size();
    const auto rejectedFrame = viewer.AddModel(Fixture("SharedBox.glb"));
    ErrorIs(rejectedFrame,lv::ErrorCode::InvalidArgument,"failed automatic camera framing returns an error");
    Check(viewer.GetScene().Size() == count && rejectedFrame.Error().SourcePath == Fixture("SharedBox.glb"),"failed framing rolls back the new instance and retains its source path");
    viewer.GetScene().MainCamera = camera;
    std::atomic_bool rejected{false};
    std::thread worker([&] {
        const auto result = viewer.AddModel(Fixture("SharedBox.glb"));
        rejected = !result && result.Error().Code == lv::ErrorCode::WrongThread;
        ErrorIs(viewer.Run(),lv::ErrorCode::WrongThread,"wrong-thread Run is rejected before native initialization");
        viewer.RequestClose();
        Check(!viewer.IsRunning(),"cross-thread status query is safe before Run");
    });
    worker.join();
    Check(rejected,"wrong-thread operations return WrongThread");
#if MINUVEX_TEST_NO_GRAPHICS
    ErrorIs(viewer.Run(),lv::ErrorCode::UnsupportedFeature,"CPU-only Viewer explicitly reports missing graphics support");
#endif
}

void TestInput() {
    lv::Viewer viewer;
    Check(viewer.AddModel(Fixture("SharedBox.glb")).HasValue(),"input test model loads");
    auto& camera = viewer.GetScene().MainCamera;
    const auto start = camera;
    lv::ViewerInputEvent event;
    event.Type = lv::ViewerInputType::MouseMove; event.DeltaX = 50;
    Check(viewer.ProcessInput(event).HasValue() && Same(camera.Position,start.Position),"unpressed pointer motion does not orbit");
    event.Type = lv::ViewerInputType::MouseDown; event.Button = lv::ViewerMouseButton::Left;
    Check(viewer.ProcessInput(event).HasValue(),"left mouse starts orbit");
    event.Type = lv::ViewerInputType::MouseMove; event.DeltaX = 50; event.DeltaY = 10;
    Check(viewer.ProcessInput(event).HasValue() && !Same(camera.Position,start.Position),"drag orbits the camera");
    Check(std::abs(Distance(camera)-Distance(start)) < 0.0001 && Same(camera.Target,start.Target),"orbit preserves target and radius");
    event.DeltaY = 1'000'000;
    Check(viewer.ProcessInput(event).HasValue() && std::isfinite(camera.ViewProjection(1).Elements[0]),"orbit clamps pitch away from singular poles");
    event.Type = lv::ViewerInputType::FocusLost;
    Check(viewer.ProcessInput(event).HasValue(),"focus loss clears pressed buttons");
    const auto stopped = camera;
    event.Type = lv::ViewerInputType::MouseMove;
    Check(viewer.ProcessInput(event).HasValue() && Same(camera.Position,stopped.Position),"motion after focus loss does not remain stuck in orbit");
    Check(viewer.SetCamera(start).HasValue(),"manual camera reset succeeds");
    event = {}; event.Type = lv::ViewerInputType::MouseDown; event.Button = lv::ViewerMouseButton::Right;
    Check(viewer.ProcessInput(event).HasValue(),"right mouse starts pan");
    event.Type = lv::ViewerInputType::MouseMove; event.DeltaX = 15; event.DeltaY = 5;
    Check(viewer.ProcessInput(event).HasValue(),"pan input succeeds");
    Check(!Same(camera.Target,start.Target) && std::abs(Distance(camera)-Distance(start)) < 0.0001,"pan translates target and eye while retaining distance");
    event.Type = lv::ViewerInputType::MouseUp;
    Check(viewer.ProcessInput(event).HasValue(),"mouse release stops pan");
    const auto panned = camera;
    event.Type = lv::ViewerInputType::MouseMove;
    Check(viewer.ProcessInput(event).HasValue() && Same(camera.Position,panned.Position),"motion after release leaves the camera stationary");
    event = {}; event.Type = lv::ViewerInputType::Scroll; event.ScrollY = 2;
    const auto beforeZoom = Distance(camera);
    Check(viewer.ProcessInput(event).HasValue() && Distance(camera) < beforeZoom,"positive wheel zooms toward the target");
    event.ScrollY = -2;
    Check(viewer.ProcessInput(event).HasValue() && std::abs(Distance(camera)-beforeZoom) < 0.0001,"opposite wheel delta reverses zoom");
    const auto finiteCamera = camera;
    event.DeltaX = std::numeric_limits<float>::quiet_NaN();
    ErrorIs(viewer.ProcessInput(event),lv::ErrorCode::InvalidArgument,"non-finite input is rejected");
    Check(Same(camera.Position,finiteCamera.Position),"invalid input does not corrupt the camera");
    event = {}; event.Type = lv::ViewerInputType::KeyDown; event.Key = lv::ViewerKey::F;
    Check(viewer.ProcessInput(event).HasValue(),"F frames the scene");
    CheckFramed(viewer,1280.0f/720);
    lv::ViewerOptions disabled; disabled.EnableDefaultInput = false;
    lv::Viewer custom(disabled);
    const auto before = custom.GetScene().MainCamera;
    event = {}; event.Type = lv::ViewerInputType::Scroll; event.ScrollY = 3;
    Check(custom.ProcessInput(event).HasValue() && Same(custom.GetScene().MainCamera.Position,before.Position),"default input can be disabled");
    lv::ViewerOptions bounded;
    bounded.CameraControls.MinimumDistance = 0.1f; bounded.CameraControls.MaximumDistance = 10;
    lv::Viewer constrained(bounded);
    auto clippedCamera = constrained.GetScene().MainCamera;
    clippedCamera.FarPlane = 5;
    Check(constrained.SetCamera(clippedCamera).HasValue(),"set a finite, tight clipping range before zoom");
    event.ScrollY = 100000;
    Check(constrained.ProcessInput(event).HasValue() && std::abs(Distance(constrained.GetScene().MainCamera)-0.1) < 0.001,"zoom is bounded by minimum distance");
    event.ScrollY = -100000;
    Check(constrained.ProcessInput(event).HasValue() && std::abs(Distance(constrained.GetScene().MainCamera)-10) < 0.001,"zoom is bounded by maximum distance");
    Check(constrained.GetScene().MainCamera.FarPlane > Distance(constrained.GetScene().MainCamera),"zoom-out retains the target inside the far plane");
    Check(constrained.GetScene().MainCamera.NearPlane < 0.1f,"zoom-in shrinks the near plane to avoid clipping the target");
    Check(viewer.SetCamera(start).HasValue(),"camera can be restored after controls");
    event = {}; event.Type = lv::ViewerInputType::MouseDown; event.Button = lv::ViewerMouseButton::Left;
    Check(viewer.ProcessInput(event).HasValue(),"start an idle drag");
    event.Type = lv::ViewerInputType::MouseMove;
    Check(viewer.ProcessInput(event).HasValue() && Same(start.Position,camera.Position),"zero pointer deltas do not alter the camera");
}
}

int main() {
    try {
        TestFacade();TestFailures();TestInput();
        lv::EmbeddedRenderer embedded;lv::Scene scene;
        Check(!embedded.IsInitialized() && embedded.Shutdown().HasValue(),"Embedded inactive shutdown is idempotent");
        ErrorIs(embedded.Render(scene,{}),lv::ErrorCode::InvalidState,"Embedded render requires initialization");
#if MINUVEX_TEST_NO_GRAPHICS
        ErrorIs(embedded.Initialize({}),lv::ErrorCode::UnsupportedFeature,"CPU-only Embedded facade reports unavailable backend");
#else
        ErrorIs(embedded.Initialize({}),lv::ErrorCode::InvalidArgument,"Embedded rejects missing native device/context without starting graphics");
#endif
        bool wrong=false;std::thread other([&]{const auto r=embedded.Initialize({});wrong=!r && r.Error().Code == lv::ErrorCode::WrongThread;});other.join();Check(wrong,"Embedded initialization uses owner thread");
    }
    catch (const std::exception& error) { Check(false,error.what()); }
    return Finish();
}
