#pragma once

#include <minuvex/Math.h>
#include <minuvex/Animation.h>
#include <minuvex/Mesh.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lv {

using AssetId = std::uint64_t;

struct ImportSettings {
    bool GenerateMissingNormals = true;
    std::size_t MaxFileBytes = 256 * 1024 * 1024;
    std::size_t MaxResourceBytes = 512 * 1024 * 1024;
    std::size_t MaxVertices = 4'000'000;
    std::size_t MaxIndices = 12'000'000;
    std::size_t MaxNodes = 65'536;
    int MaxImageDimension = 8192;
    bool ImportAnimations = true;
    bool ImportMaterials = true;
    double AnimationBakeRate = 30; // FBX evaluation samples/second, in [1,240].
    std::size_t MaxAnimationSamples = 2'000'000; // Total TRS keys, all clips.
    std::filesystem::path TextureDirectory; // Optional explicit texture search directory.
    bool operator==(const ImportSettings&) const = default;
};

enum class ImportErrorCode { Io, InvalidData, UnsupportedFeature, ResourceLimit };

class ImportError : public std::runtime_error {
public:
    ImportError(ImportErrorCode code, const std::string& message) : std::runtime_error(message), Code(code) {}
    ImportErrorCode Code;
};

enum class TextureWrap { Repeat, ClampToEdge, MirroredRepeat };
enum class TextureFilter { Nearest, Linear };

struct ImageAsset {
    int Width = 0;
    int Height = 0;
    std::vector<std::uint8_t> Rgba;
};

struct TextureAsset {
    std::size_t ImageIndex = 0;
    TextureWrap WrapU = TextureWrap::Repeat;
    TextureWrap WrapV = TextureWrap::Repeat;
    TextureFilter MinFilter = TextureFilter::Linear;
    TextureFilter MagFilter = TextureFilter::Linear;
    TextureFilter MipFilter = TextureFilter::Linear;
    bool UseMipmaps = true;
};

struct MaterialAsset {
    std::string Name;
    std::array<float, 4> BaseColor{1, 1, 1, 1};
    std::optional<std::size_t> BaseColorTexture;
    bool DoubleSided = false;
    bool Unlit = false;
    // Negative cutoff means OPAQUE; otherwise MASK. BLEND is not yet supported.
    float AlphaCutoff = -1.0f;
    std::optional<std::size_t> NormalTexture; // Linear tangent-space data, TEXCOORD_0.
    float NormalScale = 1.0f;
};

struct MeshPrimitive {
    MeshData Geometry;
    std::size_t MaterialIndex = 0;
    bool HasSkinning = false;
};

struct MeshAsset {
    std::string Name;
    std::vector<MeshPrimitive> Primitives;
};

struct ModelNode {
    std::string Name;
    std::optional<std::size_t> Parent;
    std::optional<std::size_t> MeshIndex;
    Mat4 LocalTransform = Mat4::Identity();
    std::optional<std::size_t> SkinIndex;
};

struct ModelPart {
    std::size_t MeshIndex = 0;
    Mat4 Transform = Mat4::Identity();
    JointId Node = NoJoint;
    std::optional<std::size_t> SkinIndex;
};

enum class AssetFormat { Gltf, Obj, Fbx, Custom };
enum class AssetImportStatus { Ready, ReadyWithWarnings };

struct ModelAsset {
    AssetId Id = 0;
    std::filesystem::path SourcePath;
    ImportSettings Settings;
    std::vector<std::filesystem::path> Dependencies;
    // FNV-1a of source and loaded resource bytes, for change tracking, not security.
    std::uint64_t ContentHash = 0;
    std::uint32_t ImportVersion = 4;
    std::vector<MeshAsset> Meshes;
    std::vector<MaterialAsset> Materials;
    std::vector<ImageAsset> Images;
    std::vector<TextureAsset> Textures;
    std::vector<ModelNode> Nodes;
    std::vector<ModelPart> Parts; // Renderable instances in the imported model.
    SkeletonAsset Skeleton;
    std::vector<SkinAsset> Skins;
    std::vector<AnimationClip> Animations;
    Bounds ModelBounds;
    std::vector<std::string> Warnings;
    AssetFormat Format = AssetFormat::Gltf;
    AssetImportStatus Status = AssetImportStatus::Ready;
    std::vector<std::filesystem::path> MissingDependencies;
};

struct AssetInspection {
    std::size_t Meshes=0, Primitives=0, Vertices=0, Indices=0, Materials=0, Images=0;
    std::size_t Nodes=0, Bones=0, Skins=0, Animations=0, AnimationSamples=0;
    std::size_t GeometryBytes=0, ImageBytes=0;
};
// CPU-only validation. Throws invalid_argument for malformed data, ImportError for budgets.
void ValidateModel(const ModelAsset& model);
AssetInspection InspectModel(const ModelAsset& model);
const char* AssetFormatName(AssetFormat format) noexcept;

using ModelImporter = std::function<ModelAsset(const std::filesystem::path&,const ImportSettings&)>;
class ImporterRegistry {
public:
    ImporterRegistry(); // Built-in .gltf/.glb, .obj and .fbx importers.
    void Register(std::string extension,ModelImporter importer,AssetFormat format=AssetFormat::Custom);
    bool Supports(const std::filesystem::path& path) const;
    std::vector<std::string> Extensions() const;
    ModelHandle Import(const std::filesystem::path& path,const ImportSettings& settings={}) const;
private:
    struct Entry {std::string Extension;ModelImporter Importer;AssetFormat Format;};
    std::vector<Entry> entries_;
};
// Copies source and loaded dependencies into a fresh child directory. Does not register the model.
// Preserves relative dependency layout, validates the copy before publishing it; no overwrite.
ModelHandle CopyModelToProject(ModelHandle model,const std::filesystem::path& assetsDirectory);

// CPU-only, synchronous, single-threaded registry. Strongly retains loaded assets
// until Clear() or reimport replacement; retained handles/instances stay valid.
class AssetRegistry {
public:
    ImporterRegistry& Importers() noexcept {return importers_;}
    const ImporterRegistry& Importers() const noexcept {return importers_;}
    ModelHandle PrepareModel(const std::filesystem::path& path,const ImportSettings& settings={}) const;
    ModelHandle AddPrepared(ModelHandle model); // Validated immutable candidate, cache deduplication.
    ModelHandle LoadModel(const std::filesystem::path& path, const ImportSettings& settings = {});
    ModelHandle ReimportModel(AssetId id); // Stable ID, fresh immutable data; failure preserves the cache.
    ModelHandle Find(AssetId id) const noexcept;
    std::size_t Size() const noexcept { return models_.size(); }
    void Clear() noexcept { models_.clear(); }

private:
    ImporterRegistry importers_;
    std::vector<ModelHandle> models_;
};

} // namespace lv
