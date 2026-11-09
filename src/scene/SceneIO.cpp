#include <minuvex/SceneIO.h>
#include "core/Utf8Path.h"

#include <json.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#undef GetObject
#endif

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
float Float(const Json& value) {
    Require(value.is_number(),"Expected a numeric value."); const auto number = value.get<double>();
    Require(std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(),"Scene number is nonfinite or too large.");
    return static_cast<float>(number);
}
Vec3 Vector(const Json& value) {
    Require(value.is_array() && value.size() == 3,"Expected a three-component vector.");
    return {Float(value[0]),Float(value[1]),Float(value[2])};
}
Json Vector(Vec3 value) {
    Require(std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z),"Cannot serialize a nonfinite vector.");
    return Json::array({value.X,value.Y,value.Z});
}
Json Settings(const ImportSettings& s) {
    return {{"generateMissingNormals",s.GenerateMissingNormals},{"maxFileBytes",s.MaxFileBytes},
        {"maxResourceBytes",s.MaxResourceBytes},{"maxVertices",s.MaxVertices},{"maxIndices",s.MaxIndices},
        {"maxNodes",s.MaxNodes},{"maxImageDimension",s.MaxImageDimension}};
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
    return s;
}
void ValidateScene(const Scene& scene) {
    Require(scene.Size() <= MaxObjects,"Scene object limit exceeded.");
    (void)scene.MainCamera.ViewProjection(1); scene.Sun.Validate();
    for (auto id : scene.ObjectIds()) {
        std::size_t depth = 0;
        for (auto current = id; current; current = scene.GetObject(current).Parent) {
            Require(++depth <= MaxDepth,"Scene hierarchy exceeds 256 levels.");
        }
        (void)scene.GetWorldTransform(id).NormalMatrix();
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
    catch (const std::invalid_argument& e) { return {ErrorCode::InvalidData,e.what(),"Check the version-1 Minuvex scene schema and supported limits.",path}; }
    catch (const Json::exception& e) { return {ErrorCode::InvalidData,e.what(),"Check the scene JSON field types, UTF-8 encoding and required fields.",path}; }
    catch (const std::exception& e) { return {ErrorCode::SceneIo,e.what(),"Check the scene path, permissions and available storage.",path}; }
}
} // namespace

Result<void> SaveScene(const Scene& scene, const std::filesystem::path& path, std::span<const ModelHandle> library) {
    std::filesystem::path temporary;
    try {
        Require(!path.empty(),"Scene path is empty."); ValidateScene(scene);
        const auto destination = std::filesystem::absolute(path).lexically_normal();
        Json root{{"format","MinuvexScene"},{"version",1},{"assets",Json::array()},{"objects",Json::array()}};
        std::unordered_map<const ModelAsset*,std::size_t> assetIds;
        const auto addAsset = [&](const ModelHandle& model) {
            Require(bool(model),"Asset library contains an empty model handle.");
            if (assetIds.contains(model.get())) { return; }
            Require(assetIds.size() < MaxAssets,"Scene asset limit exceeded.");
            const auto settings = Settings(model->Settings); (void)Settings(settings);
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
            const Json rotation = Json::array({t.Rotation.X,t.Rotation.Y,t.Rotation.Z,t.Rotation.W});
            root["objects"].push_back({{"id",ids.at(id)},{"parent",object.Parent ? ids.at(object.Parent) : 0},
                {"name",String(object.Name)},{"visible",object.Visible},{"asset",object.Model ? assetIds.at(object.Model.get()) : 0},
                {"position",Vector(t.Position)},{"rotation",rotation},{"scale",Vector(t.Scale)}});
        }
        const auto& c = scene.MainCamera; const auto& l = scene.Sun;
        root["camera"] = {{"position",Vector(c.Position)},{"target",Vector(c.Target)},{"up",Vector(c.Up)},
            {"fov",c.VerticalFovRadians},{"near",c.NearPlane},{"far",c.FarPlane}};
        root["light"] = {{"direction",Vector(l.Direction)},{"color",Vector(l.Color)},{"intensity",l.Intensity},{"ambient",l.Ambient}};
        const auto bytes = root.dump(2) + '\n'; Require(bytes.size() <= MaxBytes,"Scene JSON exceeds 16 MiB.");
        static std::atomic_uint64_t sequence{0};
        temporary = destination;
        temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence.fetch_add(1));
        std::ofstream stream(temporary,std::ios::binary | std::ios::trunc);
        if (!stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()))) { throw std::runtime_error("Cannot write temporary scene file."); }
        stream.close(); if (!stream) { throw std::runtime_error("Cannot close temporary scene file."); }
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace scene file (Windows error " + std::to_string(GetLastError()) + ").");
        }
#else
        std::filesystem::rename(temporary,destination);
#endif
        return Result<void>::Success();
    } catch (...) {
        if (!temporary.empty()) { std::error_code error; std::filesystem::remove(temporary,error); }
        return Result<void>::Failure(Failure(path));
    }
}

Result<SceneArchive> LoadScene(const std::filesystem::path& path, AssetRegistry& registry) {
    try {
        Require(!path.empty(),"Scene path is empty.");
        std::ifstream stream(path,std::ios::binary | std::ios::ate);
        if (!stream) { throw std::runtime_error("Cannot open scene file."); }
        const auto size = stream.tellg(); Require(size > 0 && size <= static_cast<std::streamoff>(MaxBytes),"Scene JSON is empty or exceeds 16 MiB.");
        std::string bytes(static_cast<std::size_t>(size),'\0'); stream.seekg(0);
        if (!stream.read(bytes.data(),size)) { throw std::runtime_error("Cannot read complete scene file."); }
        std::unordered_map<int,std::unordered_set<std::string>> keys;
        const auto root = Json::parse(bytes,[&](int depth, Json::parse_event_t event, Json& value) {
            Require(depth <= 32,"Scene JSON nesting limit exceeded.");
            if (event == Json::parse_event_t::object_start) { keys[depth].clear(); }
            if (event == Json::parse_event_t::key) { Require(keys[depth-1].insert(value.get<std::string>()).second,"Duplicate JSON key."); }
            return true;
        });
        Require(root.at("format") == "MinuvexScene" && UInt(root.at("version")) == 1,"Unsupported scene format/version.");
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
        const auto& l = root.at("light");
        scene.Sun = {Vector(l.at("direction")),Vector(l.at("color")),Float(l.at("intensity")),Float(l.at("ambient"))};
        ValidateScene(scene);
        const auto base = std::filesystem::absolute(path).parent_path();
        for (const auto& asset : assets) {
            const auto reference = detail::PathFromUtf8(String(asset.at("path")));
            Require(!reference.empty(),"Empty asset path.");
            auto model = registry.LoadModel(base/reference,Settings(asset.at("import")));
            models.emplace(UInt(asset.at("id")),model);
            if (std::find(archive.Assets.begin(),archive.Assets.end(),model) == archive.Assets.end()) { archive.Assets.push_back(std::move(model)); }
        }
        for (const auto& object : objects) {
            const auto asset = UInt(object.at("asset"));
            if (asset) { scene.SetModel(ids.at(UInt(object.at("id"))),models.at(asset)); }
        }
        return Result<SceneArchive>::Success(std::move(archive));
    } catch (...) { return Result<SceneArchive>::Failure(Failure(path)); }
}

} // namespace lv
