#include <emscripten/bind.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <minvex/Version.h>
#include "renderer/RenderSurface.h"
#include "platform/GraphicsSession.h"
#include "CameraController.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <cstdlib>

namespace {
using Json=nlohmann::json;
struct Failure : std::runtime_error {lv::Error Detail;explicit Failure(lv::Error e):std::runtime_error(e.Message),Detail(std::move(e)) {}};
template<class T> T Check(lv::Result<T> r) {if (!r) {throw Failure(r.Error());}return std::move(r).Value();}
void Check(lv::Result<void> r) {if (!r) {throw Failure(r.Error());}}
[[noreturn]] void Fail(lv::ErrorCode code,const char* text) {throw Failure({code,text,"Check the browser capability and supplied value.",{}});}
const char* Code(lv::ErrorCode code) {
    switch(code) {
    case lv::ErrorCode::InvalidHandle:return "InvalidHandle";case lv::ErrorCode::InvalidState:return "InvalidState";
    case lv::ErrorCode::UnsupportedFeature:return "UnsupportedFeature";case lv::ErrorCode::AssetLoad:return "AssetLoad";
    case lv::ErrorCode::InsufficientResource:return "InsufficientResource";case lv::ErrorCode::Rendering:return "Rendering";
    case lv::ErrorCode::InvalidData:return "InvalidData";default:return "InvalidArgument";
    }
}
lv::Vec3 Vector(const Json& j) {if (!j.is_array() || j.size()!=3) {throw std::invalid_argument("Expected three vector components.");}return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};}
Json Vector(lv::Vec3 v) {return {v.X,v.Y,v.Z};}
lv::ObjectId Id(const Json& request) {
    const auto n=request.at("id").get<double>();if (!std::isfinite(n) || n<1 || n>9007199254740991.0 || std::floor(n)!=n) {Fail(lv::ErrorCode::InvalidHandle,"Invalid object ID.");}return static_cast<lv::ObjectId>(n);
}
lv::QualityProfile Profile(const Json& value) {
    const auto p=value.is_string() ? value.get<std::string>():value.value("preset",std::string("low"));
    if (p!="low" && p!="medium" && p!="high") {throw std::invalid_argument("Unknown quality preset.");}
    auto q=lv::MakeQualityProfile(p=="low" ? lv::QualityPreset::Low:p=="medium" ? lv::QualityPreset::Medium:lv::QualityPreset::High);
    q.AllowFallback=value.is_object() ? value.value("allowFallback",true):true;
    if (value.is_object()) {
        for (const auto& [key,item]:value.items()) {(void)item;if (key!="preset" && key!="allowFallback" && key!="pbr" && key!="targetFps" && key!="resolutionScale" && key!="shadow" && key!="postProcessing" && key!="msaaSamples" && key!="onDemand") {throw std::invalid_argument("Unknown Web quality option: "+key);}}
        auto& o=q.Overrides;
        if (value.contains("pbr")) {o.Pbr=value.at("pbr").get<bool>();}
        if (value.contains("targetFps")) {o.TargetFps=value.at("targetFps").get<int>();}
        if (value.contains("resolutionScale")) {o.ResolutionScale=value.at("resolutionScale").get<float>();}
        if (value.contains("shadow")) {const auto mode=value.at("shadow").get<std::string>();if (mode!="off" && mode!="basic" && mode!="pcf" && mode!="pcss") {throw std::invalid_argument("Unknown shadow mode.");}o.Shadow=mode=="off" ? lv::ShadowMode::Off:mode=="basic" ? lv::ShadowMode::Basic:mode=="pcf" ? lv::ShadowMode::Pcf:lv::ShadowMode::Pcss;}
        if (value.contains("postProcessing")) {o.PostProcessing=value.at("postProcessing").get<bool>();}
        if (value.contains("msaaSamples")) {o.MsaaSamples=value.at("msaaSamples").get<int>();}
        if (value.contains("onDemand")) {o.OnDemand=value.at("onDemand").get<bool>() ? lv::OnDemandMode::On:lv::OnDemandMode::Off;}
    }
    (void)Check(lv::ExpandQuality(q));return q;
}
class WebHost {
    lv::Scene world_;lv::AssetRegistry assets_;
    std::unique_ptr<lv::detail::RenderSurface> surface_;
    lv::detail::GraphicsLease lease_{this};lv::detail::CameraController controls_;
    std::unordered_map<lv::ObjectId,lv::BlendTreeHandle> trees_;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_=0;
    int width_=1,height_=1;bool disposed_=false,lost_=false,graphicsError_=false;
    double now_=0;lv::QualityProfile profile_=lv::MakeQualityProfile(lv::QualityPreset::Low);
    static void Log(const char*,std::uint32_t level,std::uint32_t item,const char* message,std::uint32_t,const char*,void* user) {auto* host=static_cast<WebHost*>(user);if (host->context_ && emscripten_is_webgl_context_lost(host->context_)) {return;}if (level<=1) {host->graphicsError_=true;}if (level<=1 || (message && std::string_view(message).find("ERROR")!=std::string_view::npos)) {emscripten_log(EM_LOG_ERROR,"Sokol WebGL item %u: %s",item,message ? message:"resource error");}}
    void Current() {if (emscripten_webgl_make_context_current(context_)!=EMSCRIPTEN_RESULT_SUCCESS) {Fail(lv::ErrorCode::Rendering,"Cannot activate the WebGL2 context.");}}
    void Setup() {
        Current();
        // Browser restoration invalidates extension activation even though the
        // Emscripten context retains its supported-extension cache.
        if (char* extensions=emscripten_webgl_get_supported_extensions()) {
            std::istringstream names(extensions);std::string name;while (names>>name) {emscripten_webgl_enable_extension(context_,name.c_str());}std::free(extensions);
        }
        GLint attributes=0,uniforms=0;glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&attributes);glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS,&uniforms);
        if (attributes<11 || uniforms<static_cast<int>(lv::detail::ShaderSkinJoints*4+17)) {Fail(lv::ErrorCode::UnsupportedFeature,"WebGL2 attribute/uniform limits cannot support the skin palette.");}
        graphicsError_=false;sg_desc d{};d.environment.defaults.color_format=SG_PIXELFORMAT_RGBA8;d.environment.defaults.depth_format=SG_PIXELFORMAT_DEPTH_STENCIL;d.environment.defaults.sample_count=1;
        d.buffer_pool_size=2048;d.image_pool_size=1024;d.view_pool_size=2048;d.shader_pool_size=512;d.pipeline_pool_size=4096;d.sampler_pool_size=1024;
        d.logger.func=Log;d.logger.user_data=this;sg_setup(d);
        if (!sg_isvalid() || graphicsError_) {Fail(lv::ErrorCode::Rendering,"Sokol WebGL2 initialization failed.");}
        try {auto candidate=std::make_unique<lv::detail::RenderSurface>();Check(candidate->ApplyQuality(profile_,world_,width_,height_));surface_=std::move(candidate);lost_=false;}
        catch (...) {surface_.reset();if (sg_isvalid()) {sg_shutdown();}lost_=true;throw;}
    }
    void Suspend() {controls_.Reset();if (context_) {Current();surface_.reset();if (sg_isvalid()) {sg_shutdown();}}lost_=true;}
    const lv::SceneObject& Object(const Json& r) {const auto id=Id(r);if (!world_.Contains(id)) {Fail(lv::ErrorCode::InvalidHandle,"The object has been removed.");}return world_.GetObject(id);}
    std::size_t Clip(const lv::SceneObject& o,const Json& clip) {
        if (!o.Model) {throw std::invalid_argument("Object has no model.");}
        if (clip.is_number_unsigned() || clip.is_number_integer()) {const auto n=clip.get<std::int64_t>();if (n<0 || static_cast<std::size_t>(n)>=o.Model->Animations.size()) {throw std::invalid_argument("Animation index is out of range.");}return static_cast<std::size_t>(n);}
        const auto name=clip.get<std::string>();std::optional<std::size_t> found;
        for (std::size_t i=0;i<o.Model->Animations.size();++i) {if (o.Model->Animations[i].Name==name) {if (found) {throw std::invalid_argument("Ambiguous animation name; use an index.");}found=i;}}
        if (!found) {throw std::invalid_argument("Animation name does not exist.");}return *found;
    }
    Json Info(const lv::SceneObject& o) {
        Json clips=Json::array(),nodes=Json::array();if (o.Model) {
            for (const auto& c:o.Model->Animations) {clips.push_back({{"name",c.Name},{"duration",c.Duration}});}
            for (std::size_t i=0;i<o.Model->Nodes.size();++i) {const auto& n=o.Model->Nodes[i];if (n.MeshIndex && !o.Model->Meshes[*n.MeshIndex].MorphWeights.empty()) {nodes.push_back({{"node",i},{"name",n.Name},{"weights",o.Animation.EvaluateMorphWeights(static_cast<lv::JointId>(i))}});}}
        }
        const auto& t=o.LocalTransform;return {{"id",o.Id},{"name",o.Name},{"position",Vector(t.Position)},{"scale",Vector(t.Scale)},{"rotation",{t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W}},{"clips",clips},{"morphNodes",nodes},{"time",o.Animation.Time()},{"blendWeights",o.Animation.Tree() ? Json(o.Animation.BlendWeights()):Json::array()}};
    }
    Json Quality() {
        const auto& q=surface_->Quality();const auto& e=*q.Effective;Json changes=Json::array();for (const auto& a:q.Adjustments) {changes.push_back({{"field",a.Field},{"reason",a.Reason}});}
        return {{"pbr",e.Pbr},{"shadow",static_cast<int>(e.Shadow)},{"postProcessing",e.PostProcessing},{"msaaSamples",e.MsaaSamples},{"width",q.RenderWidth},{"height",q.RenderHeight},{"adjustments",changes},{"backend","WebGL2"},{"gpuSkinning",true},{"morph","CPU"},{"maxSkinJoints",lv::detail::ShaderSkinJoints}};
    }
    Json Execute(const Json& r) {
        const auto op=r.at("op").get<std::string>();if (op=="dispose") {Dispose();return nullptr;}
        if (disposed_) {Fail(lv::ErrorCode::InvalidState,"Viewer is disposed.");}
        if (op=="initialize") {
            if (context_) {Fail(lv::ErrorCode::InvalidState,"Viewer is already initialized.");}
            width_=r.at("width").get<int>();height_=r.at("height").get<int>();if (width_<1 || height_<1 || width_>8192 || height_>8192) {throw std::invalid_argument("Invalid Canvas extent.");}
            profile_=Profile(r.value("quality",Json("low")));if (!lease_.Acquire()) {Fail(lv::ErrorCode::InvalidState,"This module already owns a graphics context.");}
            EmscriptenWebGLContextAttributes a;emscripten_webgl_init_context_attributes(&a);a.majorVersion=2;a.minorVersion=0;a.alpha=false;a.antialias=false;a.depth=true;a.stencil=true;a.preserveDrawingBuffer=true;
            context_=emscripten_webgl_create_context(r.at("selector").get<std::string>().c_str(),&a);
            if (context_<=0) {context_=0;lease_.Release();Fail(lv::ErrorCode::UnsupportedFeature,"WebGL2 is unavailable for this Canvas.");}
            try {Setup();}catch (...) {Dispose();throw;}return Quality();
        }
        if (!context_) {Fail(lv::ErrorCode::InvalidState,"Initialize the Web Host first.");}
        if (op=="suspend") {Suspend();return nullptr;}
        if (op=="restore") {if (!lost_) {return Quality();}Setup();return Quality();}
        if (op=="load") {
            if (world_.Size()>=256) {Fail(lv::ErrorCode::InsufficientResource,"Web Scene object limit reached.");}
            lv::ImportSettings settings;settings.MaxFileBytes=64*1024*1024;settings.MaxResourceBytes=128*1024*1024;settings.MaxVertices=500000;settings.MaxIndices=1500000;settings.MaxNodes=8192;settings.MaxImageDimension=4096;
            auto model=assets_.LoadModel(r.at("path").get<std::string>(),settings);assets_.Clear();for (const auto& skin:model->Skins) {if (skin.Joints.size()>lv::detail::ShaderSkinJoints) {Fail(lv::ErrorCode::UnsupportedFeature,"WebGL2 supports at most 48 joints per skin.");}}const auto id=world_.AddModel(std::move(model));world_.MainCamera=lv::detail::FitSceneCamera(world_,float(width_)/height_);return Info(world_.GetObject(id));
        }
        if (op=="quality") {if (lost_) {Fail(lv::ErrorCode::InvalidState,"Restore the context before applying quality.");}auto q=Profile(r.at("value"));Current();Check(surface_->ApplyQuality(q,world_,width_,height_));profile_=std::move(q);return Quality();}
        if (op=="getQuality") {if (lost_) {Fail(lv::ErrorCode::InvalidState,"Context is lost.");}return Quality();}
        if (op=="camera") {
            auto c=world_.MainCamera;if (r.contains("position")) {c.Position=Vector(r.at("position"));}if (r.contains("target")) {c.Target=Vector(r.at("target"));}
            if (r.contains("orthographicHeight")) {c.Projection=lv::CameraProjection::Orthographic;c.OrthographicHeight=r.at("orthographicHeight").get<float>();}
            if (r.contains("position") || r.contains("target")) {const auto distance=std::hypot(c.Position.X-c.Target.X,c.Position.Y-c.Target.Y,c.Position.Z-c.Target.Z);c.NearPlane=std::min(c.NearPlane,distance*.001f);c.FarPlane=std::max(c.FarPlane,distance*2);}
            if (r.contains("up")) {c.Up=Vector(r.at("up"));}if (r.contains("nearPlane")) {c.NearPlane=r.at("nearPlane").get<float>();}if (r.contains("farPlane")) {c.FarPlane=r.at("farPlane").get<float>();}if (r.contains("fovRadians")) {c.VerticalFovRadians=r.at("fovRadians").get<float>();}
            if (r.contains("projection")) {const auto p=r.at("projection").get<std::string>();if (p!="perspective" && p!="orthographic") {throw std::invalid_argument("Unknown camera projection.");}c.Projection=p=="orthographic" ? lv::CameraProjection::Orthographic:lv::CameraProjection::Perspective;}
            (void)c.ViewProjection(float(width_)/height_);world_.MainCamera=c;return nullptr;
        }
        if (op=="frame") {world_.MainCamera=lv::detail::FitSceneCamera(world_,float(width_)/height_);return nullptr;}
        if (op=="input") {
            lv::ViewerInputEvent e;const auto type=r.at("type").get<std::string>();e.Type=type=="down" ? lv::ViewerInputType::MouseDown:type=="up" ? lv::ViewerInputType::MouseUp:type=="wheel" ? lv::ViewerInputType::Scroll:type=="reset" ? lv::ViewerInputType::FocusLost:lv::ViewerInputType::MouseMove;
            e.Button=r.value("button",0)==1 ? lv::ViewerMouseButton::Right:lv::ViewerMouseButton::Left;e.DeltaX=r.value("dx",0.f);e.DeltaY=r.value("dy",0.f);e.ScrollY=r.value("scroll",0.f);controls_.Process(world_.MainCamera,e,{},height_);return nullptr;
        }
        if (op=="tick") {
            if (lost_ || emscripten_is_webgl_context_lost(context_)) {Fail(lv::ErrorCode::InvalidState,"Context is lost.");}Current();const auto dt=r.value("dt",0.0);if (!std::isfinite(dt) || dt<0 || dt>1) {throw std::invalid_argument("Frame delta must be in [0,1].");}
            const int w=r.value("width",width_),h=r.value("height",height_);if (w<1 || h<1 || w>8192 || h>8192) {throw std::invalid_argument("Invalid Canvas extent.");}
            if (w!=width_ || h!=height_) {Check(surface_->Resize(world_,w,h));width_=w;height_=h;}
            world_.UpdateAnimations(dt);now_+=dt;const bool rendered=surface_->Render(world_,now_,r.value("force",false),{.025f,.035f,.055f,1});
            sg_pass pass{};pass.swapchain.width=width_;pass.swapchain.height=height_;pass.swapchain.sample_count=1;pass.swapchain.color_format=SG_PIXELFORMAT_RGBA8;pass.swapchain.depth_format=SG_PIXELFORMAT_DEPTH_STENCIL;pass.swapchain.gl.framebuffer=0;pass.action.colors[0].load_action=SG_LOADACTION_DONTCARE;
            sg_begin_pass(pass);surface_->Present();sg_end_pass();sg_commit();if (emscripten_is_webgl_context_lost(context_)) {Fail(lv::ErrorCode::InvalidState,"Context is lost.");}if (graphicsError_) {Fail(lv::ErrorCode::Rendering,"WebGL2 reported a graphics resource or shader error.");}
            Json events=Json::array();for (const auto& e:world_.PollAnimationEvents()) {events.push_back({{"object",e.Object},{"clip",e.Occurrence.Clip},{"name",e.Occurrence.Event.Name},{"time",e.Occurrence.Event.TimeSeconds},{"payload",e.Occurrence.Event.Payload},{"reverse",e.Occurrence.Reverse}});}
            const auto stats=surface_->Stats();return {{"rendered",rendered},{"drawCalls",stats.LastDrawCalls},{"events",events}};
        }
        const auto& object=Object(r);const auto id=object.Id;
        if (op=="info") {return Info(object);}
        if (op=="clone") {if (!object.Model) {throw std::invalid_argument("Object has no model.");}if (world_.Size()>=256) {Fail(lv::ErrorCode::InsufficientResource,"Web Scene object limit reached.");}const auto added=world_.AddModel(object.Model,object.LocalTransform);return Info(world_.GetObject(added));}
        if (op=="remove") {world_.RemoveObject(id);trees_.erase(id);return nullptr;}
        if (op=="transform") {
            auto t=object.LocalTransform;if (r.contains("position")) {t.Position=Vector(r.at("position"));}if (r.contains("scale")) {t.Scale=Vector(r.at("scale"));}
            if (r.contains("rotation")) {const auto& q=r.at("rotation");if (q.size()!=4) {throw std::invalid_argument("Expected four quaternion components.");}t.Rotation={q[0],q[1],q[2],q[3]};}world_.SetTransform(id,t);return nullptr;
        }
        if (op=="material") {
            auto mat=world_.GetMaterial(id,r.value("index",0u));const auto& v=r.at("value");if (v.contains("baseColor")) {mat.BaseColor=v.at("baseColor").get<std::array<float,4>>();}if (v.contains("metallic")) {mat.MetallicFactor=v.at("metallic").get<float>();mat.Shading=lv::ShadingModel::Pbr;}if (v.contains("roughness")) {mat.RoughnessFactor=v.at("roughness").get<float>();mat.Shading=lv::ShadingModel::Pbr;}if (v.contains("unlit")) {mat.Unlit=v.at("unlit").get<bool>();}world_.SetMaterial(id,r.value("index",0u),mat);return nullptr;
        }
        auto& player=world_.GetAnimation(id);
        if (op=="play") {player.Play(Clip(object,r.at("clip")));}
        else if (op=="pause") {player.Pause();}
        else if (op=="stop") {player.Stop();}
        else if (op=="seek") {player.Seek(r.at("time").get<double>(),r.value("collect",false) ? lv::AnimationSeekEvents::Collect:lv::AnimationSeekEvents::Silent);}
        else if (op=="speed") {player.SetSpeed(r.at("speed").get<double>());}
        else if (op=="morph") {const auto weights=r.at("weights").get<std::vector<float>>();player.SetMorphWeights(r.at("node").get<lv::JointId>(),weights);}
        else if (op=="clearMorph") {player.ClearMorphWeights(r.at("node").get<lv::JointId>());}
        else if (op=="tree") {
            lv::BlendTreeDesc d;d.Name=r.value("name",std::string("Web Blend"));d.ParameterX=r.value("parameterX",std::string("Speed"));d.ParameterY=r.value("parameterY",std::string("Direction"));const auto kind=r.value("kind",std::string("1d"));if (kind!="1d" && kind!="2d") {throw std::invalid_argument("Unknown blend tree kind.");}d.Kind=kind=="2d" ? lv::BlendTreeKind::Blend2D:lv::BlendTreeKind::Blend1D;
            for (const auto& sample:r.at("samples")) {d.Samples.push_back({Clip(object,sample.at("clip")),sample.at("x").get<float>(),sample.value("y",0.f)});}
            auto tree=lv::CreateBlendTree(*object.Model,std::move(d));player.PlayBlendTree(tree);trees_[id]=std::move(tree);
        }
        else if (op=="parameter") {player.SetBlendParameter(r.at("name").get<std::string>(),r.at("value").get<float>());}
        else if (op=="seekTree") {player.SeekBlendTree(r.at("phase").get<double>());}
        else if (op=="additive") {const auto mask=r.value("mask",std::vector<float>{});player.SetAdditiveAnimation(Clip(object,r.at("clip")),r.value("weight",1.f),{},mask);}
        else if (op=="additiveWeight") {player.SetAdditiveWeight(r.at("weight").get<float>());}
        else if (op=="seekAdditive") {player.SeekAdditive(r.at("time").get<double>());}
        else if (op=="clearAdditive") {player.ClearAdditiveAnimation();}
        else if (op=="events") {std::vector<lv::AnimationEvent> events;for (const auto& e:r.at("events")) {events.push_back({e.at("time").get<double>(),e.at("name").get<std::string>(),e.value("payload",std::string{})});}player.SetAnimationEvents(Clip(object,r.at("clip")),events);}
        else {throw std::invalid_argument("Unknown Web command.");}
        return nullptr;
    }
public:
    WebHost()=default;
    ~WebHost() {Dispose();}
    void Dispose() noexcept {if (!disposed_) {try {if (context_) {Current();surface_.reset();if (sg_isvalid()) {sg_shutdown();}emscripten_webgl_destroy_context(context_);}}catch (...) {}context_=0;lease_.Release();world_=lv::Scene{};disposed_=true;}}
    std::string Command(const std::string& input) {
        std::string fallback="InvalidArgument";
        try {const auto request=Json::parse(input);if (request.value("op",std::string{})=="tick") {fallback="Rendering";}return Json{{"value",Execute(request)}}.dump();}
        catch (const Failure& e) {return Json{{"error",{{"code",Code(e.Detail.Code)},{"message",e.Detail.Message},{"hint",e.Detail.Hint},{"sourcePath",e.Detail.SourcePath.string()}}}}.dump();}
        catch (const lv::ImportError& e) {return Json{{"error",{{"code",e.Code==lv::ImportErrorCode::ResourceLimit ? "InsufficientResource":e.Code==lv::ImportErrorCode::UnsupportedFeature ? "UnsupportedFeature":e.Code==lv::ImportErrorCode::Io ? "AssetLoad":"InvalidData"},{"message",e.what()}}}}.dump();}
        catch (const std::bad_alloc&) {return Json{{"error",{{"code","InsufficientResource"},{"message","WebAssembly memory budget exceeded."}}}}.dump();}
        catch (const std::length_error& e) {return Json{{"error",{{"code","InsufficientResource"},{"message",e.what()}}}}.dump();}
        catch (const std::invalid_argument& e) {return Json{{"error",{{"code","InvalidArgument"},{"message",e.what()}}}}.dump();}
        catch (const Json::exception& e) {return Json{{"error",{{"code","InvalidArgument"},{"message",e.what()}}}}.dump();}
        catch (const std::exception& e) {return Json{{"error",{{"code",fallback},{"message",e.what()}}}}.dump();}
    }
};
}
EMSCRIPTEN_BINDINGS(minvex_web) {emscripten::class_<WebHost>("WebHost").constructor<>().function("command",&WebHost::Command);emscripten::constant("version",std::string(lv::Version));}
