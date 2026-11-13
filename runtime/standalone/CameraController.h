#pragma once

#include <minvex/Viewer.h>

namespace lv::detail {

void ValidateViewerOptions(const ViewerOptions& options);
Camera FitSceneCamera(const Scene& scene, float aspect, ObjectId focus = NoObject);
ObjectId PickSceneObject(const Scene& scene,float aspect,float ndcX,float ndcY); // Conservative world-AABB picking.

class CameraController {
public:
    // Returns true only if the camera changed. Throws for invalid numeric input.
    bool Process(Camera& camera, const ViewerInputEvent& event, const CameraControlOptions& options, int viewportHeight);
    void Reset() noexcept { left_ = right_ = middle_ = false; }
private:
    bool left_ = false, right_ = false, middle_ = false;
};

} // namespace lv::detail
