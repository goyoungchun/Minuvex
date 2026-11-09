#pragma once

#include <minuvex/Math.h>
#include <minuvex/Mesh.h>
#include <minuvex/Scene.h>

#include <sokol_gfx.h>

namespace lv::detail {

struct alignas(16) ShaderVec4 { float X, Y, Z, W; };

// Internal mesh renderer. The caller owns the graphics context and pass.
// All calls and destruction must occur on the context's rendering thread.
class MeshRenderer final {
public:
    MeshRenderer() = default;
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    void Initialize(const MeshData& mesh, bool doubleSided = false);
    void Draw(const Mat4& model, const Mat4& viewProjection, const DirectionalLight& light = {},
              const MaterialAsset& material = {}, sg_view texture = {}, sg_sampler sampler = {}) const;
    void Shutdown() noexcept;

private:
    sg_buffer vertices_{};
    sg_buffer indices_{};
    sg_shader shader_{};
    sg_pipeline pipelines_[2]{};
    sg_image whiteImage_{};
    sg_view whiteView_{};
    sg_sampler whiteSampler_{};
    int indexCount_ = 0;
};

} // namespace lv::detail
