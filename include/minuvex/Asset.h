#pragma once

#include <minuvex/Math.h>
#include <minuvex/Animation.h>
#include <minuvex/Mesh.h>

#include <filesystem>
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

struct ModelAsset {
    AssetId Id = 0;
    std::filesystem::path SourcePath;
    ImportSettings Settings;
    std::vector<std::filesystem::path> Dependencies;
    // FNV-1a of source and loaded resource bytes, for change tracking, not security.
    std::uint64_t ContentHash = 0;
    std::uint32_t ImportVersion = 3;
    std::vector<MeshAsset> Meshes;
    std::vector<MaterialAsset> Materials;
    std::vector<ImageAsset> Images;
    std::vector<TextureAsset> Textures;
    std::vector<ModelNode> Nodes;
    std::vector<ModelPart> Parts; // Only nodes reachable from the selected glTF scene.
    SkeletonAsset Skeleton;
    std::vector<SkinAsset> Skins;
    std::vector<AnimationClip> Animations;
    Bounds ModelBounds;
    std::vector<std::string> Warnings;
};

// CPU-only, synchronous, single-threaded registry. Strongly retains loaded assets
// until Clear() or reimport replacement; retained handles/instances stay valid.
class AssetRegistry {
public:
    ModelHandle LoadModel(const std::filesystem::path& path, const ImportSettings& settings = {});
    ModelHandle ReimportModel(AssetId id); // Stable ID, fresh immutable data; failure preserves the cache.
    ModelHandle Find(AssetId id) const noexcept;
    std::size_t Size() const noexcept { return models_.size(); }
    void Clear() noexcept { models_.clear(); }

private:
    std::vector<ModelHandle> models_;
};

} // namespace lv
