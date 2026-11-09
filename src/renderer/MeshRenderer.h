#pragma once

#include <minuvex/Math.h>
#include <minuvex/Mesh.h>

#include <sokol_gfx.h>

namespace lv::detail {

// Internal Phase 1 renderer. The caller owns the graphics context and pass.
// All calls and destruction must occur on the context's rendering thread.
class MeshRenderer final {
public:
    MeshRenderer() = default;
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    void Initialize(const MeshData& mesh);
    void Draw(const Mat4& model, const Mat4& viewProjection) const;
    void Shutdown() noexcept;

private:
    sg_buffer vertices_{};
    sg_buffer indices_{};
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    int indexCount_ = 0;
};

} // namespace lv::detail
