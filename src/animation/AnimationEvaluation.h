#pragma once
#include <minuvex/Animation.h>

namespace lv::detail {
// Player samples immutable clips validated at Play/RestoreBlend; do not rescan every key each frame.
PoseBuffer SampleAnimation(const SkeletonAsset& skeleton, const AnimationClip* clip, double seconds);
PoseBuffer SampleBlendTree(const ModelAsset& model, const BlendTreeAsset& tree, std::span<const float> weights, double phase);
}
