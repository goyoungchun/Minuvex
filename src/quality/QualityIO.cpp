#include <minuvex/Quality.h>
#include "core/JsonFile.h"

#include <algorithm>
#include <limits>
#include <climits>

namespace lv {
namespace {
using Json=nlohmann::json;
constexpr std::size_t Limit=1024*1024;
int Integer(const Json& j) {
    if ((!j.is_number_integer() && !j.is_number_unsigned()) || (j.is_number_unsigned() && j.get<std::uint64_t>() > INT_MAX) ||
        (j.is_number_integer() && (j.get<std::int64_t>() < INT_MIN || j.get<std::int64_t>() > INT_MAX))) {throw std::invalid_argument("Expected an integer in the supported range.");}
    return j.get<int>();
}
template<typename Enum> Enum Choice(const Json& j,std::initializer_list<const char*> names) {
    const auto text=j.get<std::string>();int index=0;for (const auto* name:names) {if (text == name) {return static_cast<Enum>(index);}++index;}
    throw std::invalid_argument("Unknown quality option: "+text);
}
template<typename Enum> const char* Choice(Enum value,std::initializer_list<const char*> names) {return *(names.begin()+static_cast<int>(value));}
Error Failure(const std::filesystem::path& path) {
    try {throw;}
    catch (const std::bad_alloc& e) {return {ErrorCode::InsufficientResource,e.what(),"Reduce the profile size.",path};}
    catch (const Json::exception& e) {return {ErrorCode::InvalidData,e.what(),"Check the quality JSON schema and field types.",path};}
    catch (const std::invalid_argument& e) {return {ErrorCode::InvalidData,e.what(),"Check the quality JSON values and supported override names.",path};}
    catch (const std::exception& e) {return {ErrorCode::QualityIo,e.what(),"Check the profile path, permissions and storage.",path};}
}
}
Result<void> SaveQuality(const QualityProfile& p,const std::filesystem::path& path) {
    auto validated=ExpandQuality(p);if (!validated) {auto e=validated.Error();e.SourcePath=path;return Result<void>::Failure(std::move(e));}
    try {
        Json o=Json::object();const auto& a=p.Overrides;
#define LV_SAVE(field,key) if (a.field) {o[key]=*a.field;}
        LV_SAVE(ResolutionScale,"resolutionScale") LV_SAVE(FixedWidth,"fixedWidth") LV_SAVE(FixedHeight,"fixedHeight") LV_SAVE(MaxRenderHeight,"maxRenderHeight")
        LV_SAVE(TargetFps,"targetFps") LV_SAVE(MsaaSamples,"msaaSamples") LV_SAVE(Anisotropy,"anisotropy") LV_SAVE(FrustumCulling,"frustumCulling")
        LV_SAVE(ShadowResolution,"shadowResolution") LV_SAVE(Lod,"lod") LV_SAVE(LodBias,"lodBias") LV_SAVE(PostProcessing,"postProcessing")
        LV_SAVE(Instancing,"instancing") LV_SAVE(Pbr,"pbr")
        if (a.Compression) {o["textureCompression"]=*a.Compression == TextureCompression::None ? "none":*a.Compression == TextureCompression::Auto ? "auto":"bc3";}
#undef LV_SAVE
        if (a.Resolution) {o["resolution"]=Choice(*a.Resolution,{"relative","fixed"});}
        if (a.Textures) {o["textureQuality"]=Choice(*a.Textures,{"low","medium","high"});}
        if (a.Lighting) {o["lighting"]=Choice(*a.Lighting,{"unlit","basic","advanced"});}
        if (a.Shadow) {o["shadow"]=Choice(*a.Shadow,{"off","basic","pcf","pcss"});}
        if (a.OnDemand) {o["onDemandRendering"]=Choice(*a.OnDemand,{"off","on","auto"});}
        Json root{{"format","MinuvexQuality"},{"version",1},{"name",p.Name},{"baseProfile",Choice(p.Base,{"low","medium","high"})},{"allowFallback",p.AllowFallback},{"overrides",o}};
        detail::WriteJsonFile(root,path,Limit);return Result<void>::Success();
    } catch (...) {return Result<void>::Failure(Failure(path));}
}
Result<QualityProfile> LoadQuality(const std::filesystem::path& path) {
    try {
        const auto root=detail::ReadJsonFile(path,Limit);
        if ((root.contains("format") && root.at("format") != "MinuvexQuality") || Integer(root.at("version")) != 1) {throw std::invalid_argument("Unsupported quality format/version.");}
        QualityProfile p;p.Name=root.at("name").get<std::string>();p.Base=Choice<QualityPreset>(root.at("baseProfile"),{"low","medium","high"});
        if (root.contains("allowFallback")) {p.AllowFallback=root.at("allowFallback").get<bool>();}
        const auto& o=root.at("overrides");if (!o.is_object()) {throw std::invalid_argument("Quality overrides must be an object.");}
        auto& a=p.Overrides;
        for (const auto& [key,value]:o.items()) {
#define LV_INT(field,name) if (key == name) {a.field=Integer(value);continue;}
#define LV_FLOAT(field,name) if (key == name) {if (!value.is_number()) {throw std::invalid_argument("Expected numeric quality option.");} a.field=value.get<float>();continue;}
#define LV_BOOL(field,name) if (key == name) {a.field=value.get<bool>();continue;}
            LV_INT(FixedWidth,"fixedWidth") LV_INT(FixedHeight,"fixedHeight") LV_INT(MaxRenderHeight,"maxRenderHeight") LV_INT(TargetFps,"targetFps") LV_INT(MsaaSamples,"msaaSamples") LV_INT(Anisotropy,"anisotropy") LV_INT(ShadowResolution,"shadowResolution")
            LV_FLOAT(ResolutionScale,"resolutionScale") LV_FLOAT(LodBias,"lodBias") LV_BOOL(FrustumCulling,"frustumCulling") LV_BOOL(Lod,"lod") LV_BOOL(PostProcessing,"postProcessing")
            LV_BOOL(Instancing,"instancing") LV_BOOL(Pbr,"pbr")
#undef LV_INT
#undef LV_FLOAT
#undef LV_BOOL
            if (key == "textureCompression") {a.Compression=Choice<TextureCompression>(value,{"none","auto","bc3"});}
            else if (key == "resolution") {a.Resolution=Choice<ResolutionMode>(value,{"relative","fixed"});}
            else if (key == "textureQuality") {a.Textures=Choice<TextureQuality>(value,{"low","medium","high"});}
            else if (key == "lighting") {a.Lighting=Choice<LightingMode>(value,{"unlit","basic","advanced"});}
            else if (key == "shadow") {a.Shadow=Choice<ShadowMode>(value,{"off","basic","pcf","pcss"});}
            else if (key == "onDemandRendering") {a.OnDemand=value.is_boolean() ? (value.get<bool>() ? OnDemandMode::On : OnDemandMode::Off) : Choice<OnDemandMode>(value,{"off","on","auto"});}
            else {throw std::invalid_argument("Unknown quality override: "+key);}
        }
        const auto expanded=ExpandQuality(p);if (!expanded) {throw std::invalid_argument(expanded.Error().Message);}
        return Result<QualityProfile>::Success(std::move(p));
    } catch (...) {return Result<QualityProfile>::Failure(Failure(path));}
}
} // namespace lv
