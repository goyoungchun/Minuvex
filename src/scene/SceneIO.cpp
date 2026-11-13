#include <minvex/SceneIO.h>
#include <minvex/PoseIO.h>
#include "core/Utf8Path.h"
#include "core/JsonFile.h"

#include <json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace lv {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MaxBytes = 16 * 1024 * 1024, MaxObjects = 65536, MaxAssets = 4096, MaxDepth = 256;
void Require(bool valid, const char* message) { if (!valid) { throw std::invalid_argument(message); } }
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string(); return {text.begin(),text.end()};
}
std::string String(const Json& value) {
    Require(value.is_string(),"Expected a JSON string.");
    auto text = value.get<std::string>();
    Require(text.size() <= 16384 && text.find('\0') == std::string::npos,"Scene string is too long or contains NUL.");
    return text;
}
std::uint64_t UInt(const Json& value, std::uint64_t max = UINT64_MAX) {
    Require(value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() >= 0),"Expected a nonnegative integer.");
    const auto number = value.get<std::uint64_t>();
    Require(number <= max,"Scene integer exceeds its supported range."); return number;
}
double Number(const Json& value) {
    Require(value.is_number(),"Expected a numeric value."); const auto number = value.get<double>();
    Require(std::isfinite(number),"Archive number is nonfinite."); return number;
}
float Float(const Json& value) {
    const auto number = Number(value);
    Require(std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(),"Scene number is nonfinite or too large.");
    return static_cast<float>(number);
}
Vec3 Vector(const Json& value) {
    Require(value.is_array() && value.size() == 3,"Expected a three-component vector.");
    return {Float(value[0]),Float(value[1]),Float(value[2])};
}
Json Local(const Transform& t) {
    (void)t.Matrix().NormalMatrix();
    return {{"position",Json::array({t.Position.X,t.Position.Y,t.Position.Z})},
        {"rotation",Json::array({t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W})},
        {"scale",Json::array({t.Scale.X,t.Scale.Y,t.Scale.Z})}};
}
Transform Local(const Json& j) {
    const auto& q = j.at("rotation"); Require(q.is_array() && q.size() == 4,"Expected a quaternion.");
    Transform t{Vector(j.at("position")),{Float(q[0]),Float(q[1]),Float(q[2]),Float(q[3])},Vector(j.at("scale"))};
    (void)t.Matrix().NormalMatrix(); return t;
}
Json Pose(const PoseAsset& pose, const SkeletonAsset& skeleton) {
    ValidatePose(skeleton,pose); char signature[17]; std::snprintf(signature,sizeof(signature),"%016llx",static_cast<unsigned long long>(pose.SkeletonSignature));
    Json json{{"name",String(pose.Name)},{"skeleton",signature},{"joints",Json::array()}};
    for (const auto& joint : pose.Joints) { auto entry = Local(joint.Local); entry["joint"] = joint.Joint; json["joints"].push_back(std::move(entry)); }
    return json;
}
PoseAsset Pose(const Json& json, const SkeletonAsset& skeleton) {
    PoseAsset pose; pose.Name = String(json.at("name")); const auto signature = String(json.at("skeleton"));
    const auto [end,error] = std::from_chars(signature.data(),signature.data()+signature.size(),pose.SkeletonSignature,16);
    Require(signature.size() == 16 && error == std::errc{} && end == signature.data()+signature.size(),"Invalid skeleton signature.");
    const auto& joints = json.at("joints"); Require(joints.is_array() && joints.size() <= skeleton.Joints.size(),"Invalid pose joint list.");
    for (const auto& joint : joints) { pose.Joints.push_back({static_cast<JointId>(UInt(joint.at("joint"),UINT32_MAX)),Local(joint)}); }
    ValidatePose(skeleton,pose); return pose;
}
Json Materials(const SceneObject& object) {
    Json result=Json::array();if (!object.Model) {Require(object.MaterialOverrides.empty(),"Empty object cannot have material overrides.");return result;}
    for (std::size_t index=0;index<object.Model->Materials.size();++index) {const auto found=object.MaterialOverrides.find(index);if (found == object.MaterialOverrides.end()) {continue;}const auto& m=found->second;ValidateMaterial(m,*object.Model);
        Json entry{{"index",index},{"sourceName",String(object.Model->Materials[index].Name)},{"name",String(m.Name)},{"baseColor",m.BaseColor},{"doubleSided",m.DoubleSided},{"unlit",m.Unlit},{"alphaCutoff",m.AlphaCutoff},{"normalScale",m.NormalScale},{"shading",static_cast<unsigned>(m.Shading)},{"metallic",m.MetallicFactor},{"roughness",m.RoughnessFactor},{"baseTexture",nullptr},{"normalTexture",nullptr},{"metallicRoughnessTexture",nullptr}};
        if (m.BaseColorTexture) {entry["baseTexture"]=*m.BaseColorTexture;}if (m.NormalTexture) {entry["normalTexture"]=*m.NormalTexture;}if (m.MetallicRoughnessTexture) {entry["metallicRoughnessTexture"]=*m.MetallicRoughnessTexture;}result.push_back(std::move(entry));
    }Require(result.size() == object.MaterialOverrides.size(),"Invalid overridden material index.");return result;
}
void Materials(const Json& entries,Scene& scene,ObjectId id) {
    const auto& object=scene.GetObject(id);Require(entries.is_array() && entries.size()<=(object.Model ? object.Model->Materials.size():0),"Invalid material override list.");std::unordered_set<std::size_t> seen;
    for (const auto& entry:entries) {const auto index=static_cast<std::size_t>(UInt(entry.at("index")));Require(seen.insert(index).second,"Duplicate material override index.");
        const auto& source=scene.GetMaterial(id,index);Require(String(entry.at("sourceName")) == source.Name,"Saved material identity changed.");auto m=source;m.Name=String(entry.at("name"));
        const auto& color=entry.at("baseColor");Require(color.is_array() && color.size() == 4,"Expected four material color components.");for (int i=0;i<4;++i) {m.BaseColor[i]=Float(color[i]);}
        m.DoubleSided=entry.at("doubleSided").get<bool>();m.Unlit=entry.at("unlit").get<bool>();m.AlphaCutoff=Float(entry.at("alphaCutoff"));m.NormalScale=Float(entry.at("normalScale"));m.Shading=static_cast<ShadingModel>(UInt(entry.at("shading"),2));m.MetallicFactor=Float(entry.at("metallic"));m.RoughnessFactor=Float(entry.at("roughness"));
        const auto texture=[&](const char* key)->std::optional<std::size_t> {const auto& value=entry.at(key);return value.is_null() ? std::nullopt:std::optional<std::size_t>(static_cast<std::size_t>(UInt(value)));};m.BaseColorTexture=texture("baseTexture");m.NormalTexture=texture("normalTexture");m.MetallicRoughnessTexture=texture("metallicRoughnessTexture");scene.SetMaterial(id,index,m);
    }
}
std::string Fingerprint(std::uint64_t value) {char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(value));return text;}
std::string MorphFingerprint(const ModelAsset& model) {return Fingerprint(MorphSignature(model));}
Json TreeDefinition(const BlendTreeAsset& tree) {
    const auto& d=tree.Description();Json result{{"name",String(d.Name)},{"x",String(d.ParameterX)},{"y",String(d.ParameterY)},{"kind",d.Kind==BlendTreeKind::Blend1D ? "1d":"2d"},{"signature",Fingerprint(tree.Signature())},{"samples",Json::array()}};
    for (const auto& s:d.Samples) {result["samples"].push_back({{"clip",s.Clip},{"x",s.X},{"y",s.Y}});}return result;
}
BlendTreeHandle TreeDefinition(const Json& json,const ModelAsset& model) {
    BlendTreeDesc d;d.Name=String(json.at("name"));d.ParameterX=String(json.at("x"));d.ParameterY=String(json.at("y"));const auto kind=String(json.at("kind"));Require(kind=="1d" || kind=="2d","Unknown Blend Tree kind.");d.Kind=kind=="1d" ? BlendTreeKind::Blend1D:BlendTreeKind::Blend2D;
    const auto& samples=json.at("samples");Require(samples.is_array() && samples.size()<=MaxBlendSamples,"Invalid tree samples.");for (const auto& s:samples) {d.Samples.push_back({static_cast<std::size_t>(UInt(s.at("clip"))),Float(s.at("x")),Float(s.at("y"))});}auto tree=CreateBlendTree(model,std::move(d));Require(String(json.at("signature"))==Fingerprint(tree->Signature()),"Saved tree skeleton/morph/clip identity changed.");return tree;
}
using TreeIds=std::unordered_map<const BlendTreeAsset*,std::size_t>;
using Trees=std::unordered_map<std::uint64_t,BlendTreeHandle>;
Json Additive(const AdditiveAnimationState& a,const ModelAsset& model) {
    return {{"clip",a.Clip},{"clipName",String(model.Animations.at(a.Clip).Name)},{"time",a.Time},{"speed",a.Speed},{"loop",a.Loop},{"weight",a.Weight},{"state",static_cast<unsigned>(a.State)},{"reference",Pose(a.Reference,model.Skeleton)},{"mask",a.JointMask}};
}
AdditiveAnimationState Additive(const Json& json,const ModelAsset& model) {
    AdditiveAnimationState a;a.Clip=static_cast<std::size_t>(UInt(json.at("clip")));Require(a.Clip<model.Animations.size() && String(json.at("clipName"))==model.Animations[a.Clip].Name,"Saved additive clip identity changed.");a.Time=Number(json.at("time"));a.Speed=Number(json.at("speed"));a.Loop=json.at("loop").get<bool>();a.Weight=Float(json.at("weight"));a.State=static_cast<PlaybackState>(UInt(json.at("state"),2));a.Reference=Pose(json.at("reference"),model.Skeleton);
    const auto& mask=json.at("mask");Require(mask.is_array() && mask.size()<=model.Skeleton.Joints.size(),"Invalid additive mask.");for (const auto& value:mask) {a.JointMask.push_back(Float(value));}return a;
}
Json Morphs(const std::vector<MorphPose>& poses) {Json result=Json::array();for (const auto& pose:poses) {result.push_back({{"node",pose.Node},{"weights",pose.Weights}});}return result;}
std::vector<MorphPose> Morphs(const Json& json,const ModelAsset& model) {
    Require(json.is_array() && json.size()<=model.Nodes.size(),"Invalid morph pose list.");std::vector<MorphPose> result;std::unordered_set<JointId> seen;
    for (const auto& entry:json) {MorphPose pose;pose.Node=static_cast<JointId>(UInt(entry.at("node"),UINT32_MAX));Require(seen.insert(pose.Node).second,"Duplicate morph node.");const auto& weights=entry.at("weights");Require(weights.is_array() && weights.size()<=MaxMorphTargets,"Invalid morph weights.");for (const auto& weight:weights) {pose.Weights.push_back(Float(weight));}ValidateMorphWeights(model,pose.Node,pose.Weights);result.push_back(std::move(pose));}return result;
}
Json Animation(const SceneObject& object,const TreeIds& trees) {
    if (!object.Model) { return nullptr; }
    const auto& player = object.Animation;
    Require(player.Model() == object.Model,"Scene player was rebound to a foreign model; use Scene::SetModel.");
    (void)player.Evaluate();
    Json json{{"clip",nullptr},{"clipName",""},{"time",player.Time()},{"speed",player.Speed()},{"loop",player.Loop()},
        {"state",static_cast<int>(player.State())},{"overrides",Pose(player.Overrides(),object.Model->Skeleton)},{"blend",nullptr},{"tree",nullptr},{"additive",nullptr},{"morphSignature",MorphFingerprint(*object.Model)},{"morphOverrides",Morphs(player.MorphOverrides())}};
    if (player.Additive()) {json["additive"]=Additive(*player.Additive(),*object.Model);}
    if (player.Tree()) {const auto& state=*player.Tree();ValidateBlendTree(*object.Model,*state.Tree);json["tree"]={{"id",trees.at(state.Tree.get())},{"phase",state.Phase},{"x",state.X},{"y",state.Y}};}
    if (player.Clip()) { json["clip"] = *player.Clip(); json["clipName"] = String(object.Model->Animations.at(*player.Clip()).Name); }
    if (player.Blend()) {
        const auto& blend=*player.Blend();
        json["blend"]={{"sourceClip",nullptr},{"sourceClipName",""},{"sourceTime",blend.SourceTime},{"duration",blend.Duration},{"elapsed",blend.Elapsed},{"sourcePose",nullptr},{"sourceMorph",Morphs(blend.SourceMorph)}};
        if (blend.SourceClip) {json["blend"]["sourceClip"]=*blend.SourceClip;json["blend"]["sourceClipName"]=String(object.Model->Animations.at(*blend.SourceClip).Name);}
        else {json["blend"]["sourcePose"]=Pose(blend.SourcePose,object.Model->Skeleton);}
    }
    return json;
}
void Animation(const Json& json, Scene& scene, ObjectId id, bool restoreBlend, bool restoreMorph,const Trees* trees,bool restoreAdditive) {
    const auto& object = scene.GetObject(id);
    if (!object.Model) { Require(json.is_null(),"Empty object cannot have animation state."); return; }
    auto& player = scene.GetAnimation(id);
    player.SetSpeed(Number(json.at("speed"))); player.SetLoop(json.at("loop").get<bool>());
    const auto state = UInt(json.at("state"),2); const auto time = Number(json.at("time"));
    Require(std::isfinite(time) && time >= 0,"Invalid playback time.");
    if (trees && !json.at("tree").is_null()) {
        Require(json.at("clip").is_null() && String(json.at("clipName")).empty() && json.at("blend").is_null() && time==0,"Tree playback cannot also select a clip/cross-fade.");const auto& saved=json.at("tree");const auto tree=UInt(saved.at("id"));Require(trees->contains(tree),"Unknown Blend Tree ID.");player.PlayBlendTree(trees->at(tree));const auto& desc=player.Tree()->Tree->Description();player.SetBlendParameter(desc.ParameterX,Float(saved.at("x")));const auto y=Float(saved.at("y"));if (desc.Kind==BlendTreeKind::Blend2D) {player.SetBlendParameter(desc.ParameterY,y);}else {Require(y==0,"Blend1D Y parameter must be zero.");}
        if (state==static_cast<unsigned>(PlaybackState::Stopped)) {player.Stop();}else {player.Resume();if (state==static_cast<unsigned>(PlaybackState::Paused)) {player.Pause();}}player.SeekBlendTree(Number(saved.at("phase")));
    } else if (!json.at("clip").is_null()) {
        const auto clip = UInt(json.at("clip"));
        Require(clip < object.Model->Animations.size(),"Saved animation clip no longer exists.");
        Require(String(json.at("clipName")) == object.Model->Animations[clip].Name,"Saved animation clip identity has changed.");
        Require(time <= object.Model->Animations[clip].Duration,"Saved animation time exceeds duration.");
        player.Play(static_cast<std::size_t>(clip));
        if (state == static_cast<unsigned>(PlaybackState::Stopped)) { player.Stop(); }
        if (state == static_cast<unsigned>(PlaybackState::Paused)) { player.Pause(); }
        player.Seek(time);
    } else { Require(time == 0 && state == static_cast<unsigned>(PlaybackState::Stopped),"A clipless player must be stopped at time zero."); }
    if (restoreMorph) {const auto signature=String(json.at("morphSignature"));std::uint64_t fingerprint=0;const auto [end,error]=std::from_chars(signature.data(),signature.data()+signature.size(),fingerprint,16);Require(signature.size()==16 && error==std::errc{} && end==signature.data()+signature.size(),"Invalid morph signature.");const auto morphs=Morphs(json.at("morphOverrides"),*object.Model);if (player.Clip() || player.Tree() || !morphs.empty() || !json.at("blend").is_null()) {Require(fingerprint==MorphSignature(*object.Model),"Saved morph layout/defaults changed.");}for (const auto& pose:morphs) {player.SetMorphWeights(pose.Node,pose.Weights);}}
    player.ApplyPose(Pose(json.at("overrides"),object.Model->Skeleton));
    if (restoreBlend && !json.at("blend").is_null()) {
        const auto& saved=json.at("blend");AnimationBlendState blend;
        blend.SourceTime=Number(saved.at("sourceTime"));blend.Duration=Number(saved.at("duration"));blend.Elapsed=Number(saved.at("elapsed"));
        if (!saved.at("sourceClip").is_null()) {
            const auto source=UInt(saved.at("sourceClip"));Require(source<object.Model->Animations.size(),"Saved blend source clip no longer exists.");
            Require(String(saved.at("sourceClipName")) == object.Model->Animations[source].Name && saved.at("sourcePose").is_null(),"Saved blend source identity has changed.");blend.SourceClip=static_cast<std::size_t>(source);
        }else {Require(String(saved.at("sourceClipName")).empty(),"Frozen blend source must not name a clip.");blend.SourcePose=Pose(saved.at("sourcePose"),object.Model->Skeleton);}
        if (restoreMorph) {blend.SourceMorph=Morphs(saved.at("sourceMorph"),*object.Model);}else if (!blend.SourceClip) {for (std::size_t i=0;i<object.Model->Nodes.size();++i) {const auto& node=object.Model->Nodes[i];if (node.MeshIndex && !object.Model->Meshes[*node.MeshIndex].MorphWeights.empty()) {blend.SourceMorph.push_back({static_cast<JointId>(i),node.MorphWeights.empty() ? object.Model->Meshes[*node.MeshIndex].MorphWeights:node.MorphWeights});}}}
        player.RestoreBlend(blend);
    }
    if (restoreAdditive && !json.at("additive").is_null()) {player.RestoreAdditiveAnimation(Additive(json.at("additive"),*object.Model));}
    (void)player.Evaluate();
}
Json ReadJson(const std::filesystem::path& path) { return detail::ReadJsonFile(path,MaxBytes); }
void WriteJson(const Json& json,const std::filesystem::path& path) { detail::WriteJsonFile(json,path,MaxBytes); }
Json Vector(Vec3 value) {
    Require(std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z),"Cannot serialize a nonfinite vector.");
    return Json::array({value.X,value.Y,value.Z});
}
Json Settings(const ImportSettings& s) {
    return {{"generateMissingNormals",s.GenerateMissingNormals},{"maxFileBytes",s.MaxFileBytes},
        {"maxResourceBytes",s.MaxResourceBytes},{"maxVertices",s.MaxVertices},{"maxIndices",s.MaxIndices},
        {"maxNodes",s.MaxNodes},{"maxImageDimension",s.MaxImageDimension},
        {"importAnimations",s.ImportAnimations},{"importMaterials",s.ImportMaterials},
        {"animationBakeRate",s.AnimationBakeRate},{"maxAnimationSamples",s.MaxAnimationSamples},{"maxMorphTargets",s.MaxMorphTargets},{"textureDirectory",Utf8(s.TextureDirectory)}};
}
ImportSettings Settings(const Json& j) {
    ImportSettings s, ceiling;
    s.GenerateMissingNormals = j.at("generateMissingNormals").get<bool>();
    s.MaxFileBytes = static_cast<std::size_t>(UInt(j.at("maxFileBytes"),ceiling.MaxFileBytes));
    s.MaxResourceBytes = static_cast<std::size_t>(UInt(j.at("maxResourceBytes"),ceiling.MaxResourceBytes));
    s.MaxVertices = static_cast<std::size_t>(UInt(j.at("maxVertices"),ceiling.MaxVertices));
    s.MaxIndices = static_cast<std::size_t>(UInt(j.at("maxIndices"),ceiling.MaxIndices));
    s.MaxNodes = static_cast<std::size_t>(UInt(j.at("maxNodes"),ceiling.MaxNodes));
    s.MaxImageDimension = static_cast<int>(UInt(j.at("maxImageDimension"),ceiling.MaxImageDimension));
    Require(s.MaxFileBytes && s.MaxResourceBytes && s.MaxVertices && s.MaxIndices && s.MaxNodes && s.MaxImageDimension,"Import limits must be positive.");
    if (j.contains("maxMorphTargets")) {s.MaxMorphTargets=static_cast<std::size_t>(UInt(j.at("maxMorphTargets"),MaxMorphTargets));Require(s.MaxMorphTargets>0,"Invalid morph target budget.");}
    if (j.contains("importAnimations")) {s.ImportAnimations=j.at("importAnimations").get<bool>();}
    if (j.contains("importMaterials")) {s.ImportMaterials=j.at("importMaterials").get<bool>();}
    if (j.contains("animationBakeRate")) {s.AnimationBakeRate=Number(j.at("animationBakeRate"));Require(s.AnimationBakeRate>=1 && s.AnimationBakeRate<=240,"Invalid FBX bake rate.");}
    if (j.contains("maxAnimationSamples")) {s.MaxAnimationSamples=static_cast<std::size_t>(UInt(j.at("maxAnimationSamples"),ceiling.MaxAnimationSamples));Require(s.MaxAnimationSamples>0,"Animation sample budget must be positive.");}
    if (j.contains("textureDirectory")) {s.TextureDirectory=detail::PathFromUtf8(String(j.at("textureDirectory")));}
    return s;
}
void ValidateScene(const Scene& scene) {
    Require(scene.Size() <= MaxObjects,"Scene object limit exceeded.");
    (void)scene.MainCamera.ViewProjection(1); scene.ValidateLighting();
    for (auto id : scene.ObjectIds()) {
        std::size_t depth = 0;
        for (auto current = id; current; current = scene.GetObject(current).Parent) {
            Require(++depth <= MaxDepth,"Scene hierarchy exceeds 256 levels.");
        }
        (void)scene.GetWorldTransform(id).NormalMatrix();
        (void)Materials(scene.GetObject(id));
    }
}
Error Failure(const std::filesystem::path& path) {
    try { throw; }
    catch (const std::bad_alloc& e) { return {ErrorCode::InsufficientResource,e.what(),"Reduce the scene size and resource budgets.",path}; }
    catch (const ImportError& e) {
        const auto code = e.Code == ImportErrorCode::UnsupportedFeature ? ErrorCode::UnsupportedFeature :
            e.Code == ImportErrorCode::ResourceLimit ? ErrorCode::InsufficientResource :
            e.Code == ImportErrorCode::InvalidData ? ErrorCode::InvalidData : ErrorCode::AssetLoad;
        return {code,e.what(),"Restore the referenced glTF/GLB and its resources, or check import settings.",path};
    }
    catch (const std::invalid_argument& e) { return {ErrorCode::InvalidData,e.what(),"Check the Minvex Scene/Pose schema, skeleton compatibility and supported limits.",path}; }
    catch (const Json::exception& e) { return {ErrorCode::InvalidData,e.what(),"Check the scene JSON field types, UTF-8 encoding and required fields.",path}; }
    catch (const std::exception& e) { return {ErrorCode::SceneIo,e.what(),"Check the scene path, permissions and available storage.",path}; }
}
} // namespace

Result<void> SaveScene(const Scene& scene, const std::filesystem::path& path, std::span<const ModelHandle> library) {
    try {
        Require(!path.empty(),"Scene path is empty."); ValidateScene(scene);
        const auto destination = std::filesystem::absolute(path).lexically_normal();
        Json root{{"format","MinvexScene"},{"version",10},{"assets",Json::array()},{"objects",Json::array()},{"blendTrees",Json::array()}};
        TreeIds trees;
        std::unordered_map<const ModelAsset*,std::size_t> assetIds;
        const auto addAsset = [&](const ModelHandle& model) {
            Require(bool(model),"Asset library contains an empty model handle.");
            if (assetIds.contains(model.get())) { return; }
            Require(assetIds.size() < MaxAssets,"Scene asset limit exceeded.");
            auto sourceSettings=model->Settings;
            if (!sourceSettings.TextureDirectory.empty()) {auto relative=sourceSettings.TextureDirectory.lexically_relative(destination.parent_path());if (!relative.empty()) {sourceSettings.TextureDirectory=std::move(relative);}}
            const auto settings = Settings(sourceSettings); (void)Settings(settings);
            auto relative = model->SourcePath.lexically_relative(destination.parent_path());
            if (relative.empty()) { relative = model->SourcePath; }
            const auto id = assetIds.size()+1;
            root["assets"].push_back({{"id",id},{"path",String(Utf8(relative))},{"import",settings}});
            assetIds.emplace(model.get(),id);
        };
        for (const auto& model : library) { addAsset(model); }
        const auto objects = scene.ObjectIds();
        std::unordered_map<ObjectId,std::size_t> ids;
        for (auto id : objects) { ids.emplace(id,ids.size()+1); }
        for (auto id : objects) {
            const auto& object = scene.GetObject(id); const auto& t = object.LocalTransform;
            if (object.Model) { addAsset(object.Model); }
            if (object.Model && object.Animation.Tree()) {const auto& tree=object.Animation.Tree()->Tree;if (!trees.contains(tree.get())) {Require(trees.size()<1024,"Scene tree limit exceeded.");const auto treeId=trees.size()+1;trees.emplace(tree.get(),treeId);root["blendTrees"].push_back({{"id",treeId},{"model",assetIds.at(object.Model.get())},{"definition",TreeDefinition(*tree)}});}}
            const Json rotation = Json::array({t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W});
            root["objects"].push_back({{"id",ids.at(id)},{"parent",object.Parent ? ids.at(object.Parent) : 0},
                {"name",String(object.Name)},{"visible",object.Visible},{"asset",object.Model ? assetIds.at(object.Model.get()) : 0},
                {"position",Vector(t.Position)},{"rotation",rotation},{"scale",Vector(t.Scale)},{"animation",Animation(object,trees)},{"materials",Materials(object)}});
        }
        const auto& c = scene.MainCamera; const auto& l = scene.Sun;
        root["camera"] = {{"position",Vector(c.Position)},{"target",Vector(c.Target)},{"up",Vector(c.Up)},
            {"fov",c.VerticalFovRadians},{"near",c.NearPlane},{"far",c.FarPlane},{"projection",c.Projection == CameraProjection::Orthographic ? "orthographic":"perspective"},{"orthographicHeight",c.OrthographicHeight}};
        root["light"] = {{"direction",Vector(l.Direction)},{"color",Vector(l.Color)},{"intensity",l.Intensity},{"ambient",l.Ambient}};
        root["pointLights"]=Json::array();for (const auto& light:scene.PointLights) {root["pointLights"].push_back({{"position",Vector(light.Position)},{"color",Vector(light.Color)},{"intensity",light.Intensity},{"range",light.Range}});}
        root["spotLights"]=Json::array();for (const auto& light:scene.SpotLights) {root["spotLights"].push_back({{"position",Vector(light.Position)},{"direction",Vector(light.Direction)},{"color",Vector(light.Color)},{"intensity",light.Intensity},{"range",light.Range},{"inner",light.InnerConeRadians},{"outer",light.OuterConeRadians}});}
        WriteJson(root,path);
        return Result<void>::Success();
    } catch (...) {
        return Result<void>::Failure(Failure(path));
    }
}

Result<SceneArchive> LoadScene(const std::filesystem::path& path, AssetRegistry& registry) {
    try {
        const auto root = ReadJson(path); const auto version = UInt(root.at("version"));
        Require(detail::MatchesDocumentFormat(root.at("format"), "MinvexScene") && (version >= 1 && version <= 10),"Unsupported scene format/version.");
        const auto& assets = root.at("assets"); const auto& objects = root.at("objects");
        Require(assets.is_array() && assets.size() <= MaxAssets,"Invalid scene asset list.");
        Require(objects.is_array() && objects.size() <= MaxObjects,"Invalid scene object list.");
        SceneArchive archive;
        auto& scene = archive.World;
        std::unordered_map<std::uint64_t,ObjectId> ids;
        std::unordered_map<std::uint64_t,ModelHandle> models;
        std::unordered_map<std::uint64_t,std::size_t> assetIndices;
        for (std::size_t i = 0; i < assets.size(); ++i) {
            const auto id = UInt(assets[i].at("id"));
            Require(id && assetIndices.emplace(id,i).second,"Duplicate/zero asset ID.");
            (void)String(assets[i].at("path")); (void)Settings(assets[i].at("import"));
            if (version>=8) {Require(assets[i].at("import").contains("maxMorphTargets"),"Scene v8 is missing the morph budget.");}
            if (version>=4) {for (const auto* field:{"importAnimations","importMaterials","animationBakeRate","maxAnimationSamples","textureDirectory"}) {Require(assets[i].at("import").contains(field),"Scene v4 is missing Import settings.");}}
        }
        for (const auto& object : objects) {
            const auto id = UInt(object.at("id"));
            Require(id && !ids.contains(id),"Duplicate/zero object ID.");
            const auto runtimeId = scene.CreateObject(String(object.at("name")));
            ids.emplace(id,runtimeId);
            Transform t; t.Position = Vector(object.at("position")); t.Scale = Vector(object.at("scale"));
            const auto& rotation = object.at("rotation"); Require(rotation.is_array() && rotation.size() == 4,"Expected a quaternion.");
            t.Rotation = {Float(rotation[0]),Float(rotation[1]),Float(rotation[2]),Float(rotation[3])};
            scene.SetTransform(runtimeId,t); scene.SetVisible(runtimeId,object.at("visible").get<bool>());
            const auto asset = UInt(object.at("asset")); Require(!asset || assetIndices.contains(asset),"Object references an unknown asset.");
        }
        // Validate ancestry before assigning it, bounding adversarial chains/cycles.
        std::unordered_map<std::uint64_t,std::uint64_t> parents;
        for (const auto& object : objects) {
            const auto id = UInt(object.at("id")), parent = UInt(object.at("parent"));
            Require(!parent || ids.contains(parent),"Unknown parent ID."); parents.emplace(id,parent);
        }
        for (const auto& [id,parent] : parents) {
            std::size_t depth = 0;
            for (auto current = id; current; current = parents.at(current)) { Require(++depth <= MaxDepth,"Cyclic or too-deep scene hierarchy."); }
            if (parent) { scene.SetParent(ids.at(id),ids.at(parent)); }
        }
        const auto& c = root.at("camera");
        scene.MainCamera = {Vector(c.at("position")),Vector(c.at("target")),Vector(c.at("up")),Float(c.at("fov")),Float(c.at("near")),Float(c.at("far"))};
        if (version >= 5) {const auto projection=String(c.at("projection"));Require(projection == "perspective" || projection == "orthographic","Unknown camera projection.");scene.MainCamera.Projection=projection == "orthographic" ? CameraProjection::Orthographic:CameraProjection::Perspective;scene.MainCamera.OrthographicHeight=Float(c.at("orthographicHeight"));}
        const auto& l = root.at("light");
        scene.Sun = {Vector(l.at("direction")),Vector(l.at("color")),Float(l.at("intensity")),Float(l.at("ambient"))};
        if (version >= 3) {
            const auto& points=root.at("pointLights");const auto& spots=root.at("spotLights");Require(points.is_array() && spots.is_array() && points.size()+spots.size() <= MaxLocalLights,"Invalid local light arrays.");
            for (const auto& p:points) {scene.PointLights.push_back({Vector(p.at("position")),Vector(p.at("color")),Float(p.at("intensity")),Float(p.at("range"))});}
            for (const auto& p:spots) {scene.SpotLights.push_back({Vector(p.at("position")),Vector(p.at("direction")),Vector(p.at("color")),Float(p.at("intensity")),Float(p.at("range")),Float(p.at("inner")),Float(p.at("outer"))});}
        }
        ValidateScene(scene);
        const auto base = std::filesystem::absolute(path).parent_path();
        for (const auto& asset : assets) {
            const auto reference = detail::PathFromUtf8(String(asset.at("path")));
            Require(!reference.empty(),"Empty asset path.");
            auto settings=Settings(asset.at("import"));if (!settings.TextureDirectory.empty() && settings.TextureDirectory.is_relative()) {settings.TextureDirectory=base/settings.TextureDirectory;}
            auto model = registry.LoadModel(base/reference,settings);
            models.emplace(UInt(asset.at("id")),model);
            if (std::find(archive.Assets.begin(),archive.Assets.end(),model) == archive.Assets.end()) { archive.Assets.push_back(std::move(model)); }
        }
        Trees trees;
        if (version>=9) {const auto& saved=root.at("blendTrees");Require(saved.is_array() && saved.size()<=1024,"Invalid scene tree list.");for (const auto& entry:saved) {const auto id=UInt(entry.at("id")),asset=UInt(entry.at("model"));Require(id && !trees.contains(id) && models.contains(asset),"Invalid/duplicate tree ID or model reference.");trees.emplace(id,TreeDefinition(entry.at("definition"),*models.at(asset)));}}
        for (const auto& object : objects) {
            const auto asset = UInt(object.at("asset"));
            if (asset) { scene.SetModel(ids.at(UInt(object.at("id"))),models.at(asset)); }
            if (version >= 2) { Animation(object.at("animation"),scene,ids.at(UInt(object.at("id"))),version>=6,version>=8,version>=9 ? &trees:nullptr,version>=10); }
            if (version>=7) {Materials(object.at("materials"),scene,ids.at(UInt(object.at("id"))));}
        }
        ValidateScene(scene);
        return Result<SceneArchive>::Success(std::move(archive));
    } catch (...) { return Result<SceneArchive>::Failure(Failure(path)); }
}

Result<void> SavePose(const PoseAsset& pose, const SkeletonAsset& skeleton, const std::filesystem::path& path) {
    try {
        ValidateSkeleton(skeleton); auto root = Pose(pose,skeleton); root["format"] = "MinvexPose"; root["version"] = 1;
        WriteJson(root,path); return Result<void>::Success();
    } catch (...) { return Result<void>::Failure(Failure(path)); }
}
Result<PoseAsset> LoadPose(const std::filesystem::path& path, const SkeletonAsset& skeleton) {
    try {
        ValidateSkeleton(skeleton); const auto root = ReadJson(path);
        Require(detail::MatchesDocumentFormat(root.at("format"), "MinvexPose") && UInt(root.at("version")) == 1,"Unsupported pose format/version.");
        return Result<PoseAsset>::Success(Pose(root,skeleton));
    } catch (...) { return Result<PoseAsset>::Failure(Failure(path)); }
}

} // namespace lv
