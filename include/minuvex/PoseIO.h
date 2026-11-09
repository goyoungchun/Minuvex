#pragma once
#include <minuvex/Animation.h>
#include <minuvex/Result.h>

namespace lv {
// Independent .mvxpose.json files, validated against the reference skeleton.
// Full/partial poses share one schema. Same synchronous/atomic IO as SceneIO.
Result<void> SavePose(const PoseAsset& pose, const SkeletonAsset& skeleton, const std::filesystem::path& path);
Result<PoseAsset> LoadPose(const std::filesystem::path& path, const SkeletonAsset& skeleton);
} // namespace lv
