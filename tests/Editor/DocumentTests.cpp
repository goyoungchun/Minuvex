#include <minuvex/EditorDocument.h>
#include "TestHarness.h"
#include "EditorPreferences.h"
#include "CameraController.h"
#include <json.hpp>

#include <limits>

using namespace test;
using Json = nlohmann::json;
namespace {
void RoundTrip() {
    TemporaryDirectory temp;
    const auto original = temp.Path/"original";
    std::filesystem::create_directories(original/"assets");
    const auto modelPath = original/"assets"/std::filesystem::path(L"공유 모델.glb");
    std::filesystem::copy_file(Fixture("SharedBox.glb"),modelPath);
    lv::EditorDocument doc;
    Check(!doc.IsDirty() && !doc.Selection() && doc.Path().empty(),"new editor document starts clean");
    auto asset = doc.ImportModel(modelPath); Check(bool(asset) && doc.Assets().size() == 1 && doc.IsDirty(),"import registers library asset without placing it");
    const auto scenePath = original/"World.mvxscene.json";
    Check(bool(doc.Save(scenePath)) && !doc.IsDirty(),"save library-only Scene");
    auto same = doc.ImportModel(modelPath); Check(same && same.Value() == asset.Value() && !doc.IsDirty(),"cached import is idempotent and keeps clean state");
    lv::ImportSettings settings; settings.GenerateMissingNormals = false; settings.MaxVertices = 10000;
    auto unused = doc.ImportModel(modelPath,settings); Check(unused && unused.Value() != asset.Value() && doc.Assets().size() == 2,"distinct import settings retained in library");
    auto group = doc.CreateObject("계층 group"); Check(bool(group),"create empty group");
    lv::Transform t; t.Position = {2,3,4}; t.Rotation = {0,0.5f,0,0.8660254f}; t.Scale = {-2,3,0.5f};
    auto instance = doc.PlaceModel(asset.Value(),t,group.Value()); Check(bool(instance) && doc.Selection() == instance.Value(),"place selects instance under parent");
    auto second = doc.PlaceModel(asset.Value()); Check(bool(second),"same asset placed again");
    Check(bool(doc.SetParent(group.Value(),second.Value())),"reparent to a later-created object");
    Check(bool(doc.RenameObject(instance.Value(),"첫 번째 box")) && bool(doc.SetVisible(group.Value(),false)),"rename and inherited visibility edits");
    Check(!doc.SetParent(second.Value(),instance.Value()),"cycle rejected");
    Check(doc.GetScene().GetObject(second.Value()).Parent == lv::NoObject,"failed reparent preserves hierarchy");
    Check(bool(doc.Select(lv::NoObject)),"clear selection");
    auto badTransform = t; badTransform.Scale.X = 0;
    Check(!doc.SetTransform(instance.Value(),badTransform) && Near(doc.GetScene().GetObject(instance.Value()).LocalTransform.Scale.X,-2),"invalid scale preserves transform");
    Check(!doc.PlaceModel(0) && !doc.PlaceModel(asset.Value(),{},UINT64_MAX),"invalid asset and foreign parent rejected");
    Check(!doc.Select(UINT64_MAX) && !doc.RenameObject(UINT64_MAX,"bad"),"invalid selection and rename rejected");
    auto camera = doc.GetScene().MainCamera; camera.Position = {4,5,6};
    Check(bool(doc.SetCamera(camera)),"edit camera");
    auto badCamera = camera; badCamera.NearPlane = 0;
    Check(!doc.SetCamera(badCamera) && Near(doc.GetScene().MainCamera.NearPlane,camera.NearPlane),"invalid camera rejected transactionally");
    auto light = doc.GetScene().Sun; light.Intensity = 1.25f; light.Color = {0.2f,0.4f,0.6f};
    Check(bool(doc.SetLight(light)),"edit light");
    auto badLight = light; badLight.Direction = {};
    Check(!doc.SetLight(badLight) && Near(doc.GetScene().Sun.Intensity,1.25f),"invalid light preserves previous light");
    Check(bool(doc.Save(scenePath)) && !doc.IsDirty(),"save full document clears dirty state");
    const auto bytes = Read(scenePath); const auto json = Json::parse(bytes);
    Check(json["assets"].size() == 2 && json["assets"][0]["path"].get<std::string>().starts_with("assets/"),"library paths are relative, unused asset preserved");
    Check(json["objects"][1]["rotation"].size() == 4,"quaternion serialized without Euler loss");
    Check(bool(doc.Select(instance.Value())) && !doc.IsDirty(),"selection does not dirty document");
    const auto moved = temp.Path/"moved";
    std::filesystem::rename(original,moved);
    lv::EditorDocument reopened; Check(bool(reopened.Open(moved/scenePath.filename())),"relative archive survives moving project directory");
    Check(reopened.GetScene().Size() == 3 && reopened.Assets().size() == 2 && !reopened.IsDirty(),"full Scene and unused library round trip");
    const auto ids = reopened.GetScene().ObjectIds();
    const auto& loaded = reopened.GetScene().GetObject(ids[1]);
    Check(ids[1] != instance.Value() && loaded.Name == "첫 번째 box" && loaded.Parent == ids[0],"runtime IDs remapped while names and hierarchy preserved");
    Check(reopened.GetScene().GetObject(ids[0]).Parent == ids[2] && !reopened.GetScene().GetObject(ids[0]).Visible,"forward parent and hidden group survive");
    Check(loaded.Model == reopened.GetScene().GetObject(ids[2]).Model,"loaded model instances share asset handle");
    Check(Near(loaded.LocalTransform.Position.Z,4) && Near(loaded.LocalTransform.Scale.X,-2) && Near(loaded.LocalTransform.Rotation.Y,0.5f),"TRS including reflection survives");
    Check(reopened.Assets()[1]->Settings == settings,"import settings round trip");
    Check(Near(reopened.GetScene().MainCamera.Position.X,4) && Near(reopened.GetScene().Sun.Color.Z,0.6f),"camera and light round trip");
    Check(bool(reopened.Select(ids[1])) && bool(reopened.RemoveObject(ids[2])) && reopened.GetScene().Size() == 0 && !reopened.Selection(),"deleting ancestor removes subtree and clears selected descendant");
    Check(reopened.Assets().size() == 2,"deleting instances keeps imported library");
    const auto oldPath = reopened.Path();
    Check(!reopened.Open(temp.File("missing.json")) && reopened.GetScene().Size() == 0 && reopened.IsDirty() && reopened.Path() == oldPath,"failed Open preserves current document and path");
    Check(!reopened.Save(temp.File("missing-parent")/"scene.json") && reopened.IsDirty() && reopened.Path() == oldPath,"failed Save preserves dirty state and previous path");
    reopened.New(); Check(!reopened.IsDirty() && reopened.Assets().empty() && reopened.Path().empty(),"New releases document, library and path");
}
void InvalidArchives() {
    TemporaryDirectory temp; const auto file = temp.File("scene.json"); lv::Scene scene;
    auto parent = scene.CreateObject("root"), child = scene.CreateObject("child",parent); (void)child;
    Check(bool(lv::SaveScene(scene,file)),"empty objects can be saved");
    const auto bytes = Read(file); const auto valid = Json::parse(bytes);
    lv::AssetRegistry registry;
    const auto rejects = [&](Json j,const char* label) { Write(file,j.dump()); auto result = lv::LoadScene(file,registry); Check(!result && result.Error().Code == lv::ErrorCode::InvalidData,label); };
    auto j = valid; j["version"] = 8; rejects(j,"future scene versions rejected");
    j = valid; j["version"] = 1.0; rejects(j,"noninteger schema version rejected");
    j = valid; j["objects"][1]["id"] = j["objects"][0]["id"]; rejects(j,"duplicate object IDs rejected");
    j = valid; j["objects"][0]["id"] = 0; rejects(j,"zero object IDs rejected");
    j = valid; j["objects"][1]["parent"] = 999; rejects(j,"foreign parent rejected");
    j = valid; j["objects"][0]["parent"] = 2; rejects(j,"cyclic hierarchy rejected");
    j = valid; j["objects"][0]["asset"] = 1; rejects(j,"foreign asset rejected");
    j = valid; j["objects"][0]["id"] = -1; rejects(j,"negative IDs rejected");
    j = valid; j["objects"][0]["scale"] = {0,1,1}; rejects(j,"zero scale rejected");
    j = valid; j["objects"][0]["rotation"] = {0,0,0,0}; rejects(j,"zero quaternion rejected");
    j = valid; j["objects"][0]["position"] = {0,1}; rejects(j,"wrong vector width rejected");
    j = valid; j["objects"][0]["visible"] = 1; rejects(j,"nonboolean visibility rejected");
    j = valid; j["objects"][0]["name"] = std::string("bad\0name",8); rejects(j,"NUL in names rejected");
    j = valid; j["camera"]["near"] = 0; rejects(j,"invalid serialized camera rejected");
    j = valid; j["camera"]["far"] = 1e100; rejects(j,"float overflow rejected");
    j = valid; j["light"]["intensity"] = -1; rejects(j,"invalid serialized light rejected");
    j = valid; j.erase("camera"); rejects(j,"missing fields rejected");
    Write(file,"{\"format\":\"wrong\",\"format\":\"MinuvexScene\"}"); Check(!lv::LoadScene(file,registry),"duplicate JSON keys rejected");
    Write(file,std::string(100,'[')+std::string(100,']')); Check(!lv::LoadScene(file,registry),"deep JSON rejected before stack exhaustion");
    Write(file,std::string(16*1024*1024+1,' ')); Check(!lv::LoadScene(file,registry),"oversized archive rejected");
    Write(file,"{"); Check(!lv::LoadScene(file,registry),"truncated JSON rejected");
    Write(file,bytes); scene.MainCamera.Position.X = std::numeric_limits<float>::infinity();
    Check(!lv::SaveScene(scene,file) && Read(file) == bytes,"failed serialization keeps existing file byte-for-byte");
    scene.MainCamera = {}; Check(bool(lv::SaveScene(scene,file)),"save atomically replaces an existing file");
    auto folder = temp.File("directory"); std::filesystem::create_directory(folder);
    Check(!lv::SaveScene(scene,folder) && std::filesystem::is_directory(folder),"failed replacement preserves destination directory");
    bool temporaryLeft = false; for (const auto& entry : std::filesystem::directory_iterator(temp.Path)) { temporaryLeft |= entry.path().filename().string().find(".tmp-") != std::string::npos; }
    Check(!temporaryLeft,"failed save cleans temporary file");
    lv::Scene deep; lv::ObjectId last = 0; for (int i = 0; i < 257; ++i) { last = deep.CreateObject("deep",last); }
    Check(!lv::SaveScene(deep,file),"archive hierarchy depth limit enforced");
    lv::EditorDocument doc; auto asset = doc.ImportModel(Fixture("SharedBox.glb")); Check(bool(asset),"asset imported for invalid reference tests");
    Check(bool(doc.Save(file)),"asset-only document saved for validation");
    auto assetJson = Json::parse(Read(file));
    j = assetJson; j["assets"].push_back(j["assets"][0]); rejects(j,"duplicate asset IDs rejected");
    j = assetJson; j["assets"][0]["import"]["maxVertices"] = 4000001; rejects(j,"archive cannot raise import resource ceiling");
    j = assetJson; j["assets"][0]["import"]["maxFileBytes"] = 0; rejects(j,"zero resource budget rejected");
    j = assetJson; j["assets"][0]["path"] = "missing.glb"; Write(file,j.dump());
    auto missing = lv::LoadScene(file,registry); Check(!missing && missing.Error().Code == lv::ErrorCode::AssetLoad,"missing referenced model returns actionable asset error");
    Check(!doc.ImportModel(temp.File("missing.glb")) && doc.Assets().size() == 1,"failed import does not change library");
    lv::ImportSettings limited; limited.MaxVertices = 1;
    auto resource = doc.ImportModel(Fixture("SharedBox.glb"),limited);
    Check(!resource && resource.Error().Code == lv::ErrorCode::InsufficientResource,"import resource error classification preserved");
    Write(temp.File("broken.gltf"),"{}"); auto malformed = doc.ImportModel(temp.File("broken.gltf"));
    Check(!malformed && malformed.Error().Code == lv::ErrorCode::InvalidData,"import data error classification preserved");
    auto placed = doc.PlaceModel(asset.Value()); Check(bool(placed),"place existing object before failed document replacement");
    j = assetJson; auto missingAsset = j["assets"][0]; missingAsset["id"] = 2; missingAsset["path"] = "missing.glb";
    j["assets"].push_back(missingAsset); Write(file,j.dump());
    const auto savedPath = doc.Path();
    Check(!doc.Open(file) && doc.GetScene().Size() == 1 && doc.Selection() == placed.Value() && doc.Assets().size() == 1 && doc.Path() == savedPath && doc.IsDirty(),"partial asset-load failure preserves Scene, library, selection, path and dirty state");
}
void QuickOpenAndPreferences() {
    TemporaryDirectory temp;
    const auto source = temp.Path/std::filesystem::path(L"한글 model.glb");
    std::filesystem::copy_file(Fixture("SharedBox.glb"),source);
    const auto bytes = Read(source);
    lv::EditorDocument doc;
    auto opened = doc.OpenModel(source,{},1.5f);
    Check(opened && doc.GetScene().Size() == 1 && doc.Assets().size() == 1 && doc.Selection() == opened.Value() && doc.IsDirty() && doc.Path().empty(),"Quick Open creates a selected, unsaved one-model Scene without a project");
    Check(lv::detail::PickSceneObject(doc.GetScene(),1.5f,0,0) == opened.Value(),"Quick Open fits the camera onto the model");
    const auto scene = temp.File("view.mvxscene.json");
    Check(bool(doc.Save(scene)),"Quick Open can be saved as a Scene");
    const auto preserved = doc.Selection();
    Write(temp.File("bad.gltf"),"{}");
    for (const auto& file : {temp.File("missing.glb"),temp.File("bad.gltf"),temp.File("unsupported.obj")}) {
        auto failed = doc.OpenModel(file);
        Check(!failed && failed.Error().SourcePath == file && doc.Selection() == preserved && doc.GetScene().Size() == 1 && doc.Assets().size() == 1 && doc.Path() == scene && !doc.IsDirty(),"failed Quick Open preserves the entire document");
    }
    Check(!doc.OpenModel(source,{},0) && doc.Selection() == preserved && doc.Path() == scene && !doc.IsDirty(),"camera-fit failure also preserves the previous document");
    Check(!doc.Save(source) && Read(source) == bytes && doc.Path() == scene && !doc.IsDirty(),"Scene Save cannot overwrite the referenced original model");
    // Focus and picking account for hierarchy, visibility and nearest geometry.
    lv::Scene world; const auto group = world.CreateObject("group");
    lv::Transform t; t.Position.X=-2; const auto left=world.AddModel(doc.Assets().front(),t,group);
    t.Position.X=2; const auto right=world.AddModel(doc.Assets().front(),t);
    world.MainCamera.Position={0,0,6}; world.MainCamera.Target={};
    Check(lv::detail::PickSceneObject(world,1,-.58f,0) == left && lv::detail::PickSceneObject(world,1,.58f,0) == right && !lv::detail::PickSceneObject(world,1,0,.95f),"viewport picking selects independent instances and clears on empty space");
    world.SetVisible(group,false);
    Check(!lv::detail::PickSceneObject(world,1,-.58f,0),"hidden ancestors exclude their models from picking");
    const auto& bounds=doc.Assets().front()->ModelBounds;
    const auto center=(bounds.Min.X+bounds.Max.X)*.5f;
    Check(Near(lv::detail::FitSceneCamera(world,1,right).Target.X,2+center),"focus frames only the selected object");
    Rejects([&] {(void)lv::detail::FitSceneCamera(world,1,group);},"focus on an invisible subtree is rejected");
    world.SetVisible(group,true);
    Check(Near(lv::detail::FitSceneCamera(world,1,group).Target.X,-2+center),"focus frames the selected subtree");
    t.Position={2,0,1}; const auto nearer=world.AddModel(doc.Assets().front(),t);
    Check(lv::detail::PickSceneObject(world,1,.58f,0) == nearer,"nearest visible bounds win overlapping picks");
    Rejects([&] {(void)lv::detail::PickSceneObject(world,1,2,0);},"invalid pick coordinates rejected");
    using namespace lv::editor;
    EditorPreferences preferences;
    preferences.Touch(source,RecentKind::Model);preferences.Touch(scene,RecentKind::Scene);preferences.Touch(source,RecentKind::Model);
    Check(preferences.Recent.size() == 2 && preferences.Recent.front().Path == source,"recents deduplicate and put the latest file first");
    const auto settings=temp.File("preferences.json");Check(bool(preferences.Save(settings)),"preferences save UTF-8 paths atomically");
    EditorPreferences loaded;Check(bool(loaded.Load(settings)) && loaded.Recent.size() == 2 && loaded.Recent.front().Path == source && loaded.Recent.back().Kind == RecentKind::Scene && loaded.Directory == source.parent_path(),"recent kind and Unicode paths round trip");
    const auto valid=Read(settings);Write(settings,"{");
    Check(!loaded.Load(settings) && loaded.Recent.size() == 2 && loaded.Recent.front().Path == source,"corrupt preferences retain prior session state");
    Write(settings,valid);loaded.Directory.clear();
    Check(!loaded.Save(settings) && Read(settings) == valid,"invalid preferences cannot replace an existing settings file");
    for (int i=0;i<15;++i) {preferences.Touch(temp.File(("missing-"+std::to_string(i)+".glb").c_str()),RecentKind::Model);}
    Check(preferences.Recent.size() == 12 && preferences.Save(settings) && loaded.Load(settings) && loaded.Recent.size() == 12,"recent history is bounded and missing files remain reopenable entries");
}
void CompatibilityDocument() {
    TemporaryDirectory temp;lv::EditorDocument doc;
    auto sentinel=doc.CreateObject("retain");Check(bool(sentinel),"create document before preview");
    auto preview=doc.PreviewModel(Fixture("Compatibility.obj"));
    Check(preview && doc.Assets().empty() && doc.GetScene().Size() == 1 && doc.Selection() == sentinel.Value() && doc.IsDirty(),"preparing/cancelling OBJ preview preserves the previous document");
    Check(bool(doc.CommitImport(preview.Value(),true)) && doc.GetScene().Size() == 2 && doc.Assets().size() == 1,"prepared OBJ can be committed and placed without reparsing");
    const auto before=doc.Selection();auto bad=std::make_shared<lv::ModelAsset>(*preview.Value());bad->Parts.front().MeshIndex=999;
    Check(!doc.CommitImport(bad,true) && doc.Selection() == before && doc.GetScene().Size() == 2 && doc.Assets().size() == 1,"invalid prepared import preserves Scene, selection and library");
    lv::ImportSettings settings;settings.ImportAnimations=false;settings.ImportMaterials=false;settings.AnimationBakeRate=24;settings.MaxAnimationSamples=1000;
    auto fbx=doc.PreviewModel(Fixture("Compatibility.fbx"),settings);Check(bool(fbx) && bool(doc.OpenPreparedModel(fbx.Value())) && doc.Path().empty() && doc.IsDirty(),"FBX preview can replace the document through Quick Open");
    const auto file=temp.File("modern.mvxscene.json");Check(bool(doc.Save(file)),"Scene v4 saves extended import settings");
    auto json=Json::parse(Read(file));Check(json["version"] == 7 && json["assets"][0]["import"]["importAnimations"] == false,"Scene v7 retains explicit import flags");
    lv::EditorDocument reopened;Check(bool(reopened.Open(file)) && reopened.Assets().front()->Settings == settings && reopened.Assets().front()->Animations.empty(),"FBX import settings round trip without enabling unwanted clips/materials");
    json["version"]=3;for (const auto* field:{"importAnimations","importMaterials","animationBakeRate","maxAnimationSamples","textureDirectory"}) {json["assets"][0]["import"].erase(field);}Write(file,json.dump());
    Check(bool(reopened.Open(file)) && reopened.Assets().front()->Settings.ImportAnimations && !reopened.Assets().front()->Animations.empty(),"legacy Scene v3 restores default import options");
    const auto project=temp.File("project"),moved=temp.File("moved");std::filesystem::create_directories(project/"scenes");
    const auto copied=lv::CopyModelToProject(preview.Value(),project/"assets");doc.New();Check(bool(doc.CommitImport(copied,true)) && bool(doc.Save(project/"scenes"/"Model.mvxscene.json")),"project copied OBJ saves as relative Scene references");
    std::filesystem::rename(project,moved);
    Check(bool(reopened.Open(moved/"scenes"/"Model.mvxscene.json")) && reopened.GetScene().Size() == 1 && reopened.Assets().front()->Materials[0].BaseColorTexture,"moved project resolves OBJ, MTL and texture references independently");
}
}
void HistoryAndOrthographic() {
    TemporaryDirectory temp;lv::EditorDocument doc;Check(!doc.CanUndo() && !doc.Undo(),"empty history is rejected");
    const auto parent=doc.CreateObject("Parent").Value(),child=doc.CreateObject("Child",parent).Value();lv::Transform t;t.Position={2,3,4};t.Scale={2,3,-4};t.Rotation={0,0,.38268343f,.92387953f};Check(bool(doc.SetTransform(parent,t)),"parent TRS is editable");
    const auto world=doc.GetScene().GetWorldTransform(child);auto moved=world;moved.Elements[12]+=1;
    Check(bool(doc.SetWorldTransform(child,moved)) && Near(doc.GetScene().GetWorldTransform(child).Elements[12],moved.Elements[12]),"world-space gizmo edits preserve parent TRS");
    const auto identity=world.InverseAffine()*world;bool invertible=true;for (int i=0;i<16;++i) {invertible&=Near(identity.Elements[i],i%5 == 0 ? 1:0);}Check(invertible,"affine inverse handles translation, rotation, nonuniform and negative scale");
    const auto file=temp.File("History.mvxscene.json");Check(bool(doc.Save(file)) && !doc.IsDirty(),"history savepoint is clean");const auto saved=doc.GetScene().GetObject(child).LocalTransform;
    Check(bool(doc.BeginEdit()) && !doc.BeginEdit() && !doc.Save(file),"edit groups reject nesting and save before completion");
    auto edited=saved;edited.Position.X+=2;Check(bool(doc.SetTransform(child,edited)),"group first transform applies");edited.Position.Y+=3;Check(bool(doc.SetTransform(child,edited)) && bool(doc.CommitEdit()) && doc.CanUndo(),"group commits multiple changes as one step");
    Check(bool(doc.Undo()) && !doc.IsDirty() && Near(doc.GetScene().GetObject(child).LocalTransform.Position.X,saved.Position.X),"one undo restores group and savepoint");
    Check(bool(doc.Redo()) && doc.IsDirty() && Near(doc.GetScene().GetObject(child).LocalTransform.Position.Y,edited.Position.Y),"redo restores group");
    Check(bool(doc.BeginEdit()) && bool(doc.SetVisible(parent,false)) && bool(doc.CancelEdit()) && doc.GetScene().GetObject(parent).Visible,"cancel restores pre-edit Scene");
    Check(bool(doc.Undo()) && bool(doc.RenameObject(child,"Branch")) && !doc.CanRedo(),"new edit after undo invalidates redo branch");
    const auto stable=doc.GetScene().GetObject(child).LocalTransform;Check(bool(doc.BeginEdit()) && bool(doc.SetVisible(child,false)),"start group before rejected transform");lv::Transform invalid;invalid.Scale.X=0;
    Check(!doc.SetTransform(child,invalid) && !doc.IsEditing() && doc.GetScene().GetObject(child).Visible && Near(doc.GetScene().GetObject(child).LocalTransform.Position.X,stable.Position.X),"failed grouped mutation rolls back the entire group");
    Check(bool(doc.RemoveObject(parent)) && !doc.GetScene().Contains(child) && bool(doc.Undo()) && doc.GetScene().Contains(parent) && doc.GetScene().Contains(child),"undo deletion restores subtree IDs and selection");
    lv::Camera camera;camera.Position={0,0,10};camera.Target={};camera.Projection=lv::CameraProjection::Orthographic;camera.OrthographicHeight=4;Check(bool(doc.SetCamera(camera)) && bool(doc.Save(file)),"orthographic Scene saves");
    lv::EditorDocument reopened;Check(bool(reopened.Open(file)) && reopened.GetScene().MainCamera.Projection == lv::CameraProjection::Orthographic && Near(reopened.GetScene().MainCamera.OrthographicHeight,4),"Scene v5 restores orthographic camera");
    auto json=Json::parse(Read(file));json["version"]=4;Write(file,json.dump());Check(bool(reopened.Open(file)) && reopened.GetScene().MainCamera.Projection == lv::CameraProjection::Perspective,"Scene v4 camera remains perspective");
    json["version"]=5;json["camera"]["near"]=0;Write(file,json.dump());const auto count=reopened.GetScene().Size();Check(!reopened.Open(file) && reopened.GetScene().Size() == count,"invalid loaded camera cannot replace the document");
    lv::EditorDocument pick;auto asset=pick.ImportModel(Fixture("SharedBox.glb")).Value();const auto object=pick.PlaceModel(asset).Value();Check(bool(pick.SetCamera(camera)) && lv::detail::PickSceneObject(pick.GetScene(),1,0,0) == object && !lv::detail::PickSceneObject(pick.GetScene(),1,.95f,.95f),"orthographic picking uses parallel rays with offset origins");
    const bool dirty=doc.IsDirty();Check(bool(doc.ClearUndoHistory()) && !doc.CanUndo() && !doc.CanRedo() && doc.IsDirty() == dirty && doc.GetScene().Contains(child),"clearing history releases snapshots without changing the document or savepoint");
    doc.New();Check(!doc.CanUndo() && !doc.CanRedo() && !doc.IsEditing() && !doc.IsDirty(),"new document clears all history");
}
void MaterialOverrides() {
    TemporaryDirectory temp;lv::EditorDocument doc;const auto asset=doc.ImportModel(Fixture("SharedBox.gltf")).Value();const auto a=doc.PlaceModel(asset).Value(),b=doc.PlaceModel(asset).Value();
    const auto original=doc.GetScene().GetMaterial(a,0);auto material=original;material.Shading=lv::ShadingModel::Pbr;material.MetallicFactor=.8f;material.RoughnessFactor=.2f;material.DoubleSided=true;
    Check(bool(doc.SetMaterial(a,0,material)) && doc.GetScene().GetMaterial(a,0).RoughnessFactor == .2f && doc.GetScene().GetMaterial(b,0).RoughnessFactor == original.RoughnessFactor,"material overrides are independent across shared instances");
    Check(bool(doc.Undo()) && doc.GetScene().GetObject(a).MaterialOverrides.empty() && bool(doc.Redo()) && doc.GetScene().GetMaterial(a,0).DoubleSided,"material edits support Undo/Redo");
    const auto path=temp.File("Pbr.mvxscene.json");Check(bool(doc.Save(path)),"material Scene saves");const auto saved=Json::parse(Read(path));lv::EditorDocument loaded;
    Check(saved["version"] == 7 && bool(loaded.Open(path)),"Scene v7 material overrides load");const auto first=loaded.GetScene().ObjectIds()[0];
    Check(loaded.GetScene().GetMaterial(first,0).MetallicFactor == .8f && loaded.GetScene().GetMaterial(first,0).RoughnessFactor == .2f && loaded.GetScene().GetMaterial(first,0).DoubleSided,"PBR shading/factors/rasterization round-trip");
    auto bad=material;bad.MetallicFactor=2;Check(!loaded.SetMaterial(first,0,bad) && !loaded.IsDirty() && loaded.GetScene().GetMaterial(first,0).MetallicFactor == .8f,"failed material edit preserves state and savepoint");
    auto corrupt=saved;corrupt["objects"][0]["materials"][0]["sourceName"]="stale";Write(path,corrupt.dump());Check(!loaded.Open(path) && loaded.GetScene().GetMaterial(first,0).RoughnessFactor == .2f,"stale material identity cannot replace document");
    corrupt=saved;corrupt["objects"][0]["materials"].push_back(corrupt["objects"][0]["materials"][0]);Write(path,corrupt.dump());Check(!loaded.Open(path),"duplicate material override rejected");
    auto legacy=saved;legacy["version"]=6;for (auto& object:legacy["objects"]) {object.erase("materials");}Write(path,legacy.dump());Check(bool(loaded.Open(path)) && loaded.GetScene().GetObject(loaded.GetScene().ObjectIds()[0]).MaterialOverrides.empty(),"Scene v6 remains readable without material overrides");
    Check(bool(doc.ReimportModel(asset)) && doc.GetScene().GetMaterial(a,0).RoughnessFactor == .2f,"compatible reimport retains material overrides");
    Check(bool(doc.ResetMaterial(a,0)) && doc.GetScene().GetObject(a).MaterialOverrides.empty(),"ResetMaterial restores shared source material");
}
int main() {
    try { RoundTrip(); InvalidArchives(); QuickOpenAndPreferences(); CompatibilityDocument();HistoryAndOrthographic();MaterialOverrides(); }
    catch (const std::exception& e) { Check(false,e.what()); }
    return Finish();
}
