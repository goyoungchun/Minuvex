#include "Bindings.h"
#include <minvex/SceneIO.h>
#include <minvex/Version.h>
#include <thread>

namespace mvxpy {
namespace {
[[noreturn]] void Fail(lv::ErrorCode code,const char* message) {throw Failure({code,message,"Use a live handle on its Viewer owner thread.",{}});}
void SetError(const lv::Error& e,py::object cause=py::none()) {
    const auto type=py::module_::import("minvex._native").attr("Error");
    auto instance=type(e.Message);instance.attr("code")=e.Code;instance.attr("message")=e.Message;
    instance.attr("hint")=e.Hint;instance.attr("source_path")=e.SourcePath.empty() ? py::str(""):py::str(py::cast(e.SourcePath));
    if (!cause.is_none()) {instance.attr("__cause__")=cause;}
    PyErr_SetObject(type.ptr(),instance.ptr());
}
[[noreturn]] void CallbackFailed(py::object cause) {
    SetError({lv::ErrorCode::CallbackFailure,"Python callback failed.","Inspect __cause__ for the original Python exception.",{}},std::move(cause));
    throw py::error_already_set();
}
py::object Cause(py::error_already_set& e) {auto value=e.value();value.attr("__traceback__")=e.trace();return value;}
struct State {
    explicit State(const lv::ViewerOptions& options) : Native(std::make_unique<lv::Viewer>(options)) {Check(Native->GetQuality());}
    std::unique_ptr<lv::Viewer> Native;
    const std::thread::id Owner=std::this_thread::get_id();
    unsigned Busy=0; // Prevent close() from destroying a C++ dispatch/run stack.
    lv::Viewer& Get() const {
        if (std::this_thread::get_id()!=Owner) {Fail(lv::ErrorCode::WrongThread,"Viewer operation requires its construction thread.");}
        if (!Native) {Fail(lv::ErrorCode::InvalidState,"Viewer is closed.");}
        return *Native;
    }
};
struct BusyScope {
    explicit BusyScope(State& s) : S(s) {++S.Busy;}
    ~BusyScope() {--S.Busy;}
    State& S;
};
std::shared_ptr<State> Lock(const std::weak_ptr<State>& weak) {
    auto s=weak.lock();if (!s) {Fail(lv::ErrorCode::InvalidState,"Viewer no longer exists.");}s->Get();return s;
}
struct Object {
    std::weak_ptr<State> Owner;lv::ObjectId Id;
    std::shared_ptr<State> Get() const {
        auto s=Lock(Owner);if (!s->Get().GetScene().Contains(Id)) {Fail(lv::ErrorCode::InvalidHandle,"Scene object has been removed.");}return s;
    }
    lv::AnimationPlayer& Player(const std::shared_ptr<State>& s) const {return s->Get().GetScene().GetAnimation(Id);}
    std::size_t Clip(const std::shared_ptr<State>& s,py::handle clip) const {
        if (!py::isinstance<py::str>(clip)) {
            if (!py::isinstance<py::int_>(clip)) {throw py::type_error("Clip requires an integer index or string name.");}
            try {return py::cast<std::size_t>(clip);}
            catch (const py::cast_error&) {Fail(lv::ErrorCode::InvalidArgument,"Clip index must be a nonnegative integer in the supported range.");}
        }
        const auto name=py::cast<std::string>(clip);const auto& clips=Player(s).Model()->Animations;
        std::optional<std::size_t> found;
        for (std::size_t i=0;i<clips.size();++i) {if (clips[i].Name==name) {if (found) {Fail(lv::ErrorCode::InvalidArgument,"Animation name is ambiguous; use a clip index.");}found=i;}}
        if (!found) {Fail(lv::ErrorCode::InvalidArgument,"Animation name does not exist.");}return *found;
    }
};
struct Scene {std::weak_ptr<State> Owner;};
struct Tree {lv::BlendTreeHandle Native;};
Object AddModel(const std::shared_ptr<State>& s,const std::filesystem::path& path,const lv::Transform& transform,lv::ObjectId parent,const lv::ImportSettings& settings) {
    auto& v=s->Get();lv::ModelOptions options;options.Transform=transform;options.Parent=parent;options.Import=settings;
    py::gil_scoped_release release;return {s,Check(v.AddModel(path,options))};
}
std::size_t Dispatch(const std::shared_ptr<State>& s,py::function callback) {
    auto& v=s->Get();BusyScope busy(*s);py::object failure=py::none();
    auto result=v.DispatchAnimationEvents([&](const lv::SceneAnimationEvent& e) {
        try {callback(py::cast(lv::SceneAnimationEvent(e)));}catch (py::error_already_set& error) {failure=Cause(error);throw std::runtime_error("Python event callback failed.");}
    });
    if (!failure.is_none()) {CallbackFailed(std::move(failure));}return Check(std::move(result));
}
class Viewer {
public:
    explicit Viewer(const lv::ViewerOptions& options) : S(std::make_shared<State>(options)) {}
    std::shared_ptr<State> S;
    void Close() {
        if (std::this_thread::get_id()!=S->Owner) {Fail(lv::ErrorCode::WrongThread,"Close the Viewer on its owner thread.");}
        if (S->Busy) {Fail(lv::ErrorCode::InvalidState,"Cannot close during run/dispatch; call request_close(), then close after run returns.");}
        S->Native.reset();
    }
    void Run(std::uint64_t frameLimit,py::object onFrame,py::object onEvent,bool advance) {
        auto s=S;auto& v=s->Get();if (s->Busy) {Fail(lv::ErrorCode::InvalidState,"Reentrant Viewer run/dispatch is unavailable.");}
        if ((!onFrame.is_none() && !PyCallable_Check(onFrame.ptr())) || (!onEvent.is_none() && !PyCallable_Check(onEvent.ptr()))) {throw py::type_error("Callbacks must be callable or None.");}
        BusyScope busy(*s);py::object failure=py::none();lv::ViewerRunOptions options;options.FrameLimit=frameLimit;options.AdvanceAnimations=advance;
        // Only references to Python objects are captured. Run copies this lambda with
        // the GIL released; all actual Python operations occur inside gil_scoped_acquire.
        if (!onFrame.is_none() || !onEvent.is_none()) {
            options.OnFrame=[&](lv::Viewer& native,const lv::ViewerFrameInfo& info) {
                py::gil_scoped_acquire acquire;
                try {
                    if (!onFrame.is_none()) {onFrame(py::cast(this,py::return_value_policy::reference),py::cast(lv::ViewerFrameInfo(info)));}
                    if (!onEvent.is_none()) {
                        auto dispatched=native.DispatchAnimationEvents([&](const lv::SceneAnimationEvent& event) {
                            try {onEvent(py::cast(lv::SceneAnimationEvent(event)));}
                            catch (py::error_already_set& e) {failure=Cause(e);throw std::runtime_error("Python event callback failed.");}
                        });
                        if (!failure.is_none()) {native.RequestClose();}else {Check(std::move(dispatched));}
                    }
                }catch (py::error_already_set& e) {failure=Cause(e);native.RequestClose();}
                catch (const Failure& e) {SetError(e.Detail);py::error_already_set error;failure=Cause(error);native.RequestClose();}
            };
        }
        lv::Result<void> result=lv::Result<void>::Success();
        {py::gil_scoped_release release;result=v.Run(options);}
        if (!failure.is_none()) {CallbackFailed(std::move(failure));}Check(std::move(result));
    }
};
}

PYBIND11_MODULE(_native,m) {
    m.doc()="Minvex native SDK bindings";m.attr("__version__")=std::string(lv::Version);
    m.attr("graphics_available")=lv::GetBuildInfo().GraphicsAvailable;
    BindValues(m);
    auto error=py::reinterpret_steal<py::object>(PyErr_NewException("minvex.Error",PyExc_RuntimeError,nullptr));m.attr("Error")=error;
    py::register_exception_translator([](std::exception_ptr p) {
        try {if (p) {std::rethrow_exception(p);}}
        catch (const Failure& e) {SetError(e.Detail);}
        catch (const lv::ImportError& e) {SetError({e.Code==lv::ImportErrorCode::UnsupportedFeature ? lv::ErrorCode::UnsupportedFeature:e.Code==lv::ImportErrorCode::ResourceLimit ? lv::ErrorCode::InsufficientResource:e.Code==lv::ImportErrorCode::Io ? lv::ErrorCode::AssetLoad:lv::ErrorCode::InvalidData,e.what(),"Check the source model and import limits.",{}});}
        catch (const std::invalid_argument& e) {SetError({lv::ErrorCode::InvalidArgument,e.what(),"Check the supplied value.",{}});}
        catch (const std::out_of_range& e) {SetError({lv::ErrorCode::InvalidArgument,e.what(),"Check the supplied index.",{}});}
        catch (const std::length_error& e) {SetError({lv::ErrorCode::InsufficientResource,e.what(),"Reduce event/resource counts.",{}});}
    });
    py::class_<Tree>(m,"BlendTree")
        .def_property_readonly("description",[](const Tree& t){return lv::BlendTreeDesc(t.Native->Description());})
        .def("weights",[](const Tree& t,float x,float y){return lv::EvaluateBlendWeights(*t.Native,x,y);},py::arg("x"),py::arg("y")=0);
    py::class_<Object>(m,"Object")
        .def_property_readonly("id",[](const Object& o){return o.Id;})
        .def_property("name",[](const Object& o){auto s=o.Get();return std::string(s->Get().GetScene().GetObject(o.Id).Name);},[](const Object& o,std::string name){auto s=o.Get();s->Get().GetScene().SetName(o.Id,std::move(name));})
        .def_property("transform",[](const Object& o){auto s=o.Get();return lv::Transform(s->Get().GetScene().GetObject(o.Id).LocalTransform);},[](const Object& o,const lv::Transform& t){auto s=o.Get();Check(s->Get().SetTransform(o.Id,t));})
        .def_property("visible",[](const Object& o){auto s=o.Get();return s->Get().GetScene().GetObject(o.Id).Visible;},[](const Object& o,bool v){auto s=o.Get();Check(s->Get().SetVisible(o.Id,v));})
        .def("remove",[](const Object& o){auto s=o.Get();s->Get().GetScene().RemoveObject(o.Id);})
        .def("set_parent",[](const Object& o,lv::ObjectId parent){auto s=o.Get();s->Get().GetScene().SetParent(o.Id,parent);})
        .def_property_readonly("animation_clips",[](const Object& o){auto s=o.Get();const auto model=o.Player(s).Model();py::list clips;for (const auto& c:model->Animations) {py::dict d;d["name"]=c.Name;d["duration"]=c.Duration;clips.append(d);}return clips;})
        .def("play_animation",[](const Object& o,py::object clip){auto s=o.Get();Check(s->Get().PlayAnimation(o.Id,o.Clip(s,clip)));})
        .def("cross_fade",[](const Object& o,py::object clip,double seconds){auto s=o.Get();Check(s->Get().CrossFadeAnimation(o.Id,o.Clip(s,clip),seconds));},py::arg("clip"),py::arg("seconds")=.3)
        .def("pause_animation",[](const Object& o){auto s=o.Get();Check(s->Get().PauseAnimation(o.Id));})
        .def("resume_animation",[](const Object& o){auto s=o.Get();o.Player(s).Resume();})
        .def("stop_animation",[](const Object& o){auto s=o.Get();Check(s->Get().StopAnimation(o.Id));})
        .def("seek_animation",[](const Object& o,double time,bool collect){auto s=o.Get();Check(s->Get().SeekAnimation(o.Id,time,collect ? lv::AnimationSeekEvents::Collect:lv::AnimationSeekEvents::Silent));},py::arg("seconds"),py::arg("collect_events")=false)
        .def_property("animation_speed",[](const Object& o){auto s=o.Get();return o.Player(s).Speed();},[](const Object& o,double speed){auto s=o.Get();o.Player(s).SetSpeed(speed);})
        .def_property("animation_loop",[](const Object& o){auto s=o.Get();return o.Player(s).Loop();},[](const Object& o,bool loop){auto s=o.Get();o.Player(s).SetLoop(loop);})
        .def_property_readonly("animation_time",[](const Object& o){auto s=o.Get();return o.Player(s).Time();})
        .def_property_readonly("playback_state",[](const Object& o){auto s=o.Get();return o.Player(s).State();})
        .def("create_blend_tree",[](const Object& o,lv::BlendTreeDesc desc){auto s=o.Get();return Tree{lv::CreateBlendTree(*o.Player(s).Model(),std::move(desc))};})
        .def("play_blend_tree",[](const Object& o,const Tree& tree,bool restart){auto s=o.Get();Check(s->Get().PlayBlendTree(o.Id,tree.Native,restart));},py::arg("tree"),py::arg("restart")=true)
        .def("set_blend_parameter",[](const Object& o,const std::string& name,float value){auto s=o.Get();Check(s->Get().SetBlendParameter(o.Id,name,value));})
        .def("seek_blend_tree",[](const Object& o,double phase){auto s=o.Get();Check(s->Get().SeekBlendTree(o.Id,phase));})
        .def_property_readonly("blend_weights",[](const Object& o){auto s=o.Get();return o.Player(s).BlendWeights();})
        .def("set_morph_weights",[](const Object& o,lv::JointId node,const std::vector<float>& weights){auto s=o.Get();Check(s->Get().SetMorphWeights(o.Id,node,weights));})
        .def("clear_morph_weights",[](const Object& o,lv::JointId node){auto s=o.Get();Check(s->Get().ClearMorphWeights(o.Id,node));})
        .def("get_morph_weights",[](const Object& o,lv::JointId node){auto s=o.Get();return o.Player(s).EvaluateMorphWeights(node);})
        .def_property_readonly("morph_nodes",[](const Object& o){auto s=o.Get();const auto model=o.Player(s).Model();py::list nodes;for (std::size_t i=0;i<model->Nodes.size();++i) {const auto& n=model->Nodes[i];if (n.MeshIndex && !model->Meshes.at(*n.MeshIndex).MorphWeights.empty()) {py::dict d;d["node"]=i;d["name"]=n.Name;d["weights"]=o.Player(s).EvaluateMorphWeights(static_cast<lv::JointId>(i));nodes.append(d);}}return nodes;})
        .def("get_material",[](const Object& o,std::size_t index){auto s=o.Get();return lv::MaterialAsset(s->Get().GetScene().GetMaterial(o.Id,index));},py::arg("index")=0)
        .def("set_material",[](const Object& o,std::size_t index,const lv::MaterialAsset& mat){auto s=o.Get();Check(s->Get().SetMaterial(o.Id,index,mat));})
        .def("reset_material",[](const Object& o,std::size_t index){auto s=o.Get();Check(s->Get().ResetMaterial(o.Id,index));},py::arg("index")=0)
        .def("set_animation_events",[](const Object& o,py::object clip,const std::vector<lv::AnimationEvent>& events){auto s=o.Get();Check(s->Get().SetAnimationEvents(o.Id,o.Clip(s,clip),events));})
        .def("reset_animation_events",[](const Object& o,py::object clip){auto s=o.Get();Check(s->Get().ResetAnimationEvents(o.Id,o.Clip(s,clip)));});
    py::class_<Scene>(m,"Scene")
        .def_property_readonly("size",[](const Scene& scene){auto s=Lock(scene.Owner);return s->Get().GetScene().Size();})
        .def_property_readonly("objects",[](const Scene& scene){auto s=Lock(scene.Owner);std::vector<Object> result;for (auto id:s->Get().GetScene().ObjectIds()) {result.push_back({s,id});}return result;})
        .def("create_object",[](const Scene& scene,std::string name,lv::ObjectId parent){auto s=Lock(scene.Owner);return Object{s,s->Get().GetScene().CreateObject(std::move(name),parent)};},py::arg("name")="",py::arg("parent")=lv::NoObject)
        .def("object",[](const Scene& scene,lv::ObjectId id){auto s=Lock(scene.Owner);Object o{s,id};(void)o.Get();return o;})
        .def_property("camera",[](const Scene& scene){auto s=Lock(scene.Owner);return lv::Camera(s->Get().GetScene().MainCamera);},[](const Scene& scene,const lv::Camera& c){auto s=Lock(scene.Owner);Check(s->Get().SetCamera(c));})
        .def_property("sun",[](const Scene& scene){auto s=Lock(scene.Owner);return lv::DirectionalLight(s->Get().GetScene().Sun);},[](const Scene& scene,const lv::DirectionalLight& l){auto s=Lock(scene.Owner);Check(s->Get().SetLight(l));})
        .def("update_animations",[](const Scene& scene,double dt){auto s=Lock(scene.Owner);s->Get().GetScene().UpdateAnimations(dt);})
        .def("save",[](const Scene& scene,const std::filesystem::path& path){auto s=Lock(scene.Owner);py::gil_scoped_release release;Check(lv::SaveScene(s->Get().GetScene(),path));})
        .def("load",[](const Scene& scene,const std::filesystem::path& path){
            auto s=Lock(scene.Owner);auto& native=s->Get();
            if (s->Busy) {Fail(lv::ErrorCode::InvalidState,"Load a Scene outside run/dispatch.");}
            lv::AssetRegistry assets;py::gil_scoped_release release;auto archive=Check(lv::LoadScene(path,assets));
            native.GetScene()=std::move(archive.World);Check(native.SetCamera(native.GetScene().MainCamera));
        });
    py::class_<Viewer>(m,"Viewer")
        .def(py::init<const lv::ViewerOptions&>(),py::arg("options")=lv::ViewerOptions{})
        .def_property_readonly("scene",[](const Viewer& v){v.S->Get();return Scene{v.S};})
        .def("add_model",[](const Viewer& v,const std::filesystem::path& path,const lv::Transform& transform,lv::ObjectId parent,const lv::ImportSettings& settings){return AddModel(v.S,path,transform,parent,settings);},py::arg("path"),py::arg("transform")=lv::Transform{},py::arg("parent")=lv::NoObject,py::arg("import_settings")=lv::ImportSettings{})
        .def("poll_animation_events",[](const Viewer& v){return Check(v.S->Get().PollAnimationEvents());})
        .def("dispatch_animation_events",[](const Viewer& v,py::function callback){return Dispatch(v.S,std::move(callback));})
        .def("clear_animation_events",[](const Viewer& v){Check(v.S->Get().ClearAnimationEventQueues());})
        .def("set_quality",[](const Viewer& v,const lv::QualityProfile& p){Check(v.S->Get().SetQuality(p));})
        .def_property_readonly("quality",[](const Viewer& v){return Check(v.S->Get().GetQuality());})
        .def("set_debug_view",[](const Viewer& v,lv::DebugView view){Check(v.S->Get().SetDebugView(view));})
        .def("frame_scene",[](const Viewer& v){Check(v.S->Get().FrameScene());})
        .def("request_redraw",[](const Viewer& v){Check(v.S->Get().RequestRedraw());})
        .def("run",&Viewer::Run,py::arg("frame_limit")=0,py::arg("on_frame")=py::none(),py::arg("on_event")=py::none(),py::arg("advance_animations")=true)
        .def("request_close",[](const Viewer& v){if (v.S->Native) {v.S->Native->RequestClose();}})
        .def_property_readonly("is_running",[](const Viewer& v){return v.S->Native && v.S->Native->IsRunning();})
        .def_property_readonly("closed",[](const Viewer& v){return !v.S->Native;})
        .def("close",&Viewer::Close)
        .def("__enter__",[](Viewer& v)->Viewer& {v.S->Get();return v;},py::return_value_policy::reference_internal)
        .def("__exit__",[](Viewer& v,py::object,py::object,py::object){v.Close();return false;});
}
}
