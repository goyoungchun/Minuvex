#pragma once

#include <minvex/Result.h>
#include <minvex/Scene.h>

#include <span>

namespace lv {

// Writes version 10 with per-player Additive state; reads versions 1..10.
// External model references, library, hierarchy, TRS, camera, lights, animation/pose.
// Runtime ObjectId/AssetId values are remapped when opening an archive.
struct SceneArchive {
    Scene World;
    std::vector<ModelHandle> Assets; // Includes imported assets without instances.
};

// CPU-only, synchronous, caller-thread operations. 16 MiB JSON, 65536 objects,
// 4096 assets, 256 hierarchy levels. Assets remain external files.
// Save replaces the destination only after a complete temporary file is closed.
Result<void> SaveScene(const Scene& scene, const std::filesystem::path& path,
                       std::span<const ModelHandle> library = {});
// Failure returns no partial Scene; the supplied registry may cache loaded assets.
Result<SceneArchive> LoadScene(const std::filesystem::path& path, AssetRegistry& registry);

} // namespace lv
