#include <minuvex/EditorDocument.h>
#include "TestHarness.h"
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
    auto j = valid; j["version"] = 3; rejects(j,"future scene versions rejected");
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
}
int main() {
    try { RoundTrip(); InvalidArchives(); }
    catch (const std::exception& e) { Check(false,e.what()); }
    return Finish();
}
