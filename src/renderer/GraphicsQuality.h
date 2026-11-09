#pragma once
#include <minuvex/Quality.h>
#include <minuvex/Scene.h>

namespace lv::detail {
QualityCapabilities QueryQualityCapabilities(); // Current D3D11/Metal context, rendering thread only.
std::uint64_t SceneRenderSignature(const Scene& scene);
bool SceneHasActiveAnimation(const Scene& scene);
}
