#include "ImportCommon.h"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace lv {
const char* AssetFormatName(AssetFormat format) noexcept {
    switch (format) {case AssetFormat::Gltf:return "glTF / GLB";case AssetFormat::Obj:return "OBJ";case AssetFormat::Fbx:return "FBX";case AssetFormat::Custom:return "Custom";}return "Unknown";
}
AssetInspection InspectModel(const ModelAsset& model) {
    AssetInspection info;info.Meshes=model.Meshes.size();info.Materials=model.Materials.size();info.Images=model.Images.size();info.Nodes=model.Nodes.size();info.Skins=model.Skins.size();info.Animations=model.Animations.size();
    for (const auto& mesh:model.Meshes) {for (const auto& part:mesh.Primitives) {++info.Primitives;info.Vertices+=part.Geometry.Vertices.size();info.Indices+=part.Geometry.Indices.size();}}
    for (const auto& joint:model.Skeleton.Joints) {info.Bones+=joint.IsJoint;}
    for (const auto& image:model.Images) {info.ImageBytes+=image.Rgba.size();}
    for (const auto& clip:model.Animations) {for (const auto& track:clip.Tracks) {info.AnimationSamples+=track.Times.size();}}
    for (const auto& mesh:model.Meshes) {for (const auto& p:mesh.Primitives) {for (const auto& lod:p.Lods) {info.LodIndices+=lod.Indices.size();}}}
    info.GeometryBytes=info.Vertices*sizeof(Vertex)+(info.Indices+info.LodIndices)*sizeof(std::uint32_t);return info;
}
void ValidateModel(const ModelAsset& model) {
    detail::ValidateImportSettings(model.Settings);
    const auto require=[](bool valid,const char* message) {if (!valid) {throw std::invalid_argument(message);}};
    const auto info=InspectModel(model);const auto& settings=model.Settings;
    if (info.Vertices>settings.MaxVertices || info.Indices>settings.MaxIndices || info.Nodes>settings.MaxNodes || info.AnimationSamples>settings.MaxAnimationSamples || info.GeometryBytes>settings.MaxResourceBytes || info.ImageBytes>settings.MaxResourceBytes-info.GeometryBytes) {detail::ImportLimit("Converted model exceeds its import budget.");}
    require(!model.Parts.empty() && !model.Meshes.empty(),"Model contains no renderable geometry.");
    require(model.Skeleton.Joints.size() == model.Nodes.size(),"Model node and skeleton tables must match.");
    require(model.Skins.size()<=256 && model.Animations.size()<=256,"Model exceeds 256 skins/clips.");
    ValidateSkeleton(model.Skeleton);
    for (std::size_t i=0;i<model.Nodes.size();++i) {
        const auto& n=model.Nodes[i];const auto& j=model.Skeleton.Joints[i];
        require(!n.Parent || *n.Parent<model.Nodes.size(),"Invalid node parent.");
        require(j.Parent == (n.Parent ? static_cast<JointId>(*n.Parent):NoJoint),"Node/skeleton parents differ.");
        require(!n.MeshIndex || *n.MeshIndex<model.Meshes.size(),"Invalid node mesh reference.");
        require(!n.SkinIndex || *n.SkinIndex<model.Skins.size(),"Invalid node skin reference.");
        (void)n.LocalTransform.NormalMatrix();
        std::size_t depth=0;for (auto id=static_cast<JointId>(i);id!=NoJoint;id=model.Skeleton.Joints[id].Parent) {require(++depth<=256,"Model hierarchy exceeds 256 levels.");}
    }
    for (const auto& image:model.Images) {if (image.Width == 0 && image.Height == 0 && image.Rgba.empty()) {continue;}require(image.Width>0 && image.Height>0,"Image dimensions must be positive.");if (image.Width>settings.MaxImageDimension || image.Height>settings.MaxImageDimension) {detail::ImportLimit("Image exceeds MaxImageDimension.");}require(std::uint64_t(image.Width)*image.Height*4 == image.Rgba.size(),"Image pixels do not match dimensions.");}
    for (const auto& texture:model.Textures) {require(texture.ImageIndex<model.Images.size(),"Invalid texture image reference.");}
    for (const auto& material:model.Materials) {
        for (auto color:material.BaseColor) {require(std::isfinite(color) && color>=0,"Invalid material color.");}
        require(std::isfinite(material.AlphaCutoff) && std::isfinite(material.NormalScale),"Nonfinite material data.");
        require(!material.BaseColorTexture || *material.BaseColorTexture<model.Textures.size(),"Invalid base-color texture.");
        require(!material.NormalTexture || *material.NormalTexture<model.Textures.size(),"Invalid normal texture.");
        for (const auto texture:{material.BaseColorTexture,material.NormalTexture}) {if (texture) {require(!model.Images.at(model.Textures[*texture].ImageIndex).Rgba.empty(),"Material references an undecoded image.");}}
    }
    for (const auto& mesh:model.Meshes) {require(!mesh.Primitives.empty(),"Empty mesh.");for (const auto& primitive:mesh.Primitives) {ValidateMesh(primitive.Geometry);require(primitive.MaterialIndex<model.Materials.size(),"Invalid primitive material.");}}
    for (const auto& mesh:model.Meshes) {for (const auto& p:mesh.Primitives) {require(p.Lods.size()<=3 && (!p.HasSkinning || p.Lods.empty()),"Only rigid LOD1..3 are supported.");auto count=p.Geometry.Indices.size();for (const auto& lod:p.Lods) {require(!lod.Indices.empty() && lod.Indices.size()%3==0 && lod.Indices.size()<count && std::isfinite(lod.Error) && lod.Error>=0,"Invalid LOD topology/error.");count=lod.Indices.size();for (auto index:lod.Indices) {require(index<p.Geometry.Vertices.size(),"Invalid LOD vertex reference.");}}}}
    for (const auto& skin:model.Skins) {require(!skin.Joints.empty() && skin.Joints.size()<=MaxSkinJoints && skin.Joints.size() == skin.InverseBindMatrices.size(),"Invalid skin palette.");for (std::size_t i=0;i<skin.Joints.size();++i) {require(skin.Joints[i]<model.Nodes.size(),"Invalid skin joint.");(void)skin.InverseBindMatrices[i].NormalMatrix();}}
    for (const auto& part:model.Parts) {
        require(part.MeshIndex<model.Meshes.size() && (part.Node == NoJoint || part.Node<model.Nodes.size()) && (!part.SkinIndex || *part.SkinIndex<model.Skins.size()),"Invalid model part reference.");(void)part.Transform.NormalMatrix();
        for (const auto& primitive:model.Meshes[part.MeshIndex].Primitives) {if (part.SkinIndex) {require(primitive.HasSkinning,"Skinned part is missing joint attributes.");for (const auto& vertex:primitive.Geometry.Vertices) {for (auto joint:vertex.Joints) {require(joint<model.Skins[*part.SkinIndex].Joints.size(),"Vertex joint is outside the skin palette.");}}}}
    }
    for (const auto& clip:model.Animations) {ValidateAnimation(model.Skeleton,clip);}
    const auto& b=model.ModelBounds;require(std::isfinite(b.Min.X) && std::isfinite(b.Min.Y) && std::isfinite(b.Min.Z) && std::isfinite(b.Max.X) && std::isfinite(b.Max.Y) && std::isfinite(b.Max.Z) && b.Min.X<=b.Max.X && b.Min.Y<=b.Max.Y && b.Min.Z<=b.Max.Z,"Invalid model bounds.");
}
} // namespace lv

namespace lv::detail {
void ValidateImportSettings(const ImportSettings& s) {
    if (!s.MaxFileBytes || !s.MaxResourceBytes || !s.MaxVertices || !s.MaxIndices || !s.MaxNodes || s.MaxImageDimension<=0 || s.MaxFileBytes>INT_MAX || s.MaxVertices>UINT32_MAX || s.MaxIndices>INT_MAX || s.MaxNodes>UINT32_MAX || !s.MaxAnimationSamples || !std::isfinite(s.AnimationBakeRate) || s.AnimationBakeRate<1 || s.AnimationBakeRate>240) {throw std::invalid_argument("Invalid model import limits or FBX bake rate (1..240).");}
}
void ImportContext::Reserve(std::size_t bytes) {if (bytes>Model.Settings.MaxResourceBytes-Bytes) {ImportLimit("Model exceeds MaxResourceBytes.");}Bytes+=bytes;}
void ImportContext::Geometry(std::size_t vertices,std::size_t indices) {
    if (vertices>Model.Settings.MaxVertices-Vertices || indices>Model.Settings.MaxIndices-Indices) {ImportLimit("Model exceeds MaxVertices/MaxIndices.");}
    Reserve(vertices*sizeof(Vertex)+indices*sizeof(std::uint32_t));Vertices+=vertices;Indices+=indices;
}
void ImportContext::Hash(std::span<const std::uint8_t> bytes) {for (auto byte:bytes) {Model.ContentHash^=byte;Model.ContentHash*=1099511628211ULL;}}
std::vector<std::uint8_t> ImportContext::Read(const std::filesystem::path& path) {
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if (error) {throw ImportError(ImportErrorCode::Io,"Cannot read model resource: "+PathText(path));}
    if (size>Model.Settings.MaxFileBytes) {ImportLimit("Source/dependency exceeds MaxFileBytes.");}Reserve(static_cast<std::size_t>(size));
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));std::ifstream input(path,std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) {throw ImportError(ImportErrorCode::Io,"Cannot read resource: "+PathText(path));}
    const auto canonical=std::filesystem::canonical(path);if (std::find(Model.Dependencies.begin(),Model.Dependencies.end(),canonical) == Model.Dependencies.end()) {Model.Dependencies.push_back(canonical);}Hash(bytes);return bytes;
}
std::optional<std::size_t> ImportContext::Texture(const std::string& name,std::span<const std::uint8_t> embedded,const std::filesystem::path& directory) {
    if (!Model.Settings.ImportMaterials) {return {};}
    auto normalized=name;std::replace(normalized.begin(),normalized.end(),'\\','/');
    if (normalized.find('\0')!=std::string::npos || normalized.find("://")!=std::string::npos) {ImportInvalid("Invalid/network texture path.");}
    const auto requested=std::filesystem::path(std::u8string(normalized.begin(),normalized.end()));
    std::vector<std::uint8_t> encoded;
    if (embedded.empty()) {
        std::vector<std::filesystem::path> candidates;
        if (!requested.is_absolute() && !requested.has_root_path() && normalized.find(':') == std::string::npos) {if (!directory.empty()) {candidates.push_back(directory/requested);}candidates.push_back(Model.SourcePath.parent_path()/requested);}
        if (!requested.filename().empty()) {candidates.push_back(Model.SourcePath.parent_path()/requested.filename());if (!Model.Settings.TextureDirectory.empty()) {candidates.push_back(Model.Settings.TextureDirectory/requested.filename());}}
        for (const auto& path:candidates) {std::error_code ec;if (std::filesystem::is_regular_file(path,ec)) {encoded=Read(path);break;}}
        if (encoded.empty()) {Model.MissingDependencies.push_back(requested);Model.Warnings.push_back("Missing texture: "+name+" (using material color).");return {};}
        embedded=encoded;
    } else {Reserve(embedded.size());Hash(embedded);}
    if (embedded.size()>INT_MAX || embedded.size()>Model.Settings.MaxFileBytes) {ImportLimit("Encoded texture exceeds MaxFileBytes.");}
    int width=0,height=0,channels=0;
    if (!stbi_info_from_memory(embedded.data(),static_cast<int>(embedded.size()),&width,&height,&channels)) {ImportInvalid("Texture is not a supported PNG/JPEG: "+name);}
    if (width<=0 || height<=0 || width>Model.Settings.MaxImageDimension || height>Model.Settings.MaxImageDimension) {ImportLimit("Texture exceeds MaxImageDimension.");}
    const auto size=static_cast<std::size_t>(width)*height*4;Reserve(size);
    std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> rgba(stbi_load_from_memory(embedded.data(),static_cast<int>(embedded.size()),&width,&height,&channels,4),stbi_image_free);
    if (!rgba) {ImportInvalid("Cannot decode texture: "+name);}
    Model.Images.push_back({width,height,{rgba.get(),rgba.get()+size}});TextureAsset texture;texture.ImageIndex=Model.Images.size()-1;Model.Textures.push_back(texture);return Model.Textures.size()-1;
}
void FinishImportedModel(ModelAsset& model) {
    model.Skeleton.Signature=SkeletonSignature(model.Skeleton);
    bool first=true;for (const auto& part:model.Parts) {for (const auto& primitive:model.Meshes.at(part.MeshIndex).Primitives) {for (const auto& vertex:primitive.Geometry.Vertices) {const auto p=part.Transform.TransformPoint({vertex.Position[0],vertex.Position[1],vertex.Position[2]});if (first) {model.ModelBounds={p,p};first=false;}auto& b=model.ModelBounds;b.Min={std::min(b.Min.X,p.X),std::min(b.Min.Y,p.Y),std::min(b.Min.Z,p.Z)};b.Max={std::max(b.Max.X,p.X),std::max(b.Max.Y,p.Y),std::max(b.Max.Z,p.Z)};}}}
    if (first) {ImportInvalid("Model contains no renderable triangles.");}
    ValidateModel(model);
    if (!model.Skins.empty()) {AnimationPlayer player;player.Bind(std::make_shared<const ModelAsset>(model));model.ModelBounds=player.GetBounds();}
    model.Status=model.Warnings.empty() ? AssetImportStatus::Ready:AssetImportStatus::ReadyWithWarnings;
}
} // namespace lv::detail
