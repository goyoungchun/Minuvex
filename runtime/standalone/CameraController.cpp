#include "CameraController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace lv::detail {
namespace {
Vec3 Add(Vec3 a, Vec3 b) { return {a.X+b.X, a.Y+b.Y, a.Z+b.Z}; }
Vec3 Subtract(Vec3 a, Vec3 b) { return {a.X-b.X, a.Y-b.Y, a.Z-b.Z}; }
Vec3 Scale(Vec3 a, double s) { return {static_cast<float>(a.X*s), static_cast<float>(a.Y*s), static_cast<float>(a.Z*s)}; }
double Dot(Vec3 a, Vec3 b) { return double(a.X)*b.X + double(a.Y)*b.Y + double(a.Z)*b.Z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.Y*b.Z-a.Z*b.Y, a.Z*b.X-a.X*b.Z, a.X*b.Y-a.Y*b.X}; }
double Length(Vec3 a) { return std::hypot(double(a.X), double(a.Y), double(a.Z)); }
Vec3 Normalize(Vec3 a) {
    const auto length = Length(a);
    if (!std::isfinite(length) || length == 0) { throw std::invalid_argument("Camera direction is degenerate."); }
    return Scale(a, 1.0/length);
}
Vec3 Rotate(Vec3 v, Vec3 axis, double radians) {
    return Add(Add(Scale(v, std::cos(radians)), Scale(Cross(axis,v), std::sin(radians))), Scale(axis, Dot(axis,v)*(1-std::cos(radians))));
}
bool ValidUtf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) { continue; }
        unsigned remaining = 0, code = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { remaining = 1; code = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { remaining = 2; code = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { remaining = 3; code = first & 0x07; minimum = 0x10000; }
        else { return false; }
        if (remaining > text.size()-i) { return false; }
        while (remaining--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) { return false; }
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { return false; }
    }
    return true;
}
}

void ValidateViewerOptions(const ViewerOptions& options) {
    if (options.Width <= 0 || options.Height <= 0 || options.Width > 16384 || options.Height > 16384 ||
        options.Title.empty() || options.Title.size() > 126 || options.Title.find('\0') != std::string::npos || !ValidUtf8(options.Title)) {
        throw std::invalid_argument("Viewer requires dimensions in 1..16384 and a nonempty UTF-8 title of at most 126 bytes.");
    }
    for (float c : options.ClearColor) { if (!std::isfinite(c) || c < 0 || c > 1) { throw std::invalid_argument("ClearColor components must be in [0, 1]."); } }
    const auto& c = options.CameraControls;
    if (!std::isfinite(c.OrbitSpeed) || c.OrbitSpeed < 0 || !std::isfinite(c.PanSpeed) || c.PanSpeed < 0 ||
        !std::isfinite(c.ZoomSpeed) || c.ZoomSpeed < 0 || !std::isfinite(c.MinimumDistance) || c.MinimumDistance <= 0 ||
        !std::isfinite(c.MaximumDistance) || c.MaximumDistance < c.MinimumDistance) {
        throw std::invalid_argument("Camera speeds must be finite/nonnegative and 0 < minimum distance <= maximum distance.");
    }
}

Camera FitSceneCamera(const Scene& scene, float aspect) {
    auto camera = scene.MainCamera;
    (void)camera.ViewProjection(aspect);
    const auto items = scene.CollectRenderItems();
    if (items.empty()) { throw std::invalid_argument("There are no visible models to frame."); }
    double low[3]{}, high[3]{};
    bool first = true;
    for (const auto& item : items) {
        (void)item.WorldTransform.NormalMatrix();
        const auto& bounds = item.Model->ModelBounds;
        for (unsigned mask = 0; mask < 8; ++mask) {
            const auto point = item.WorldTransform.TransformPoint({mask & 1 ? bounds.Max.X : bounds.Min.X,
                mask & 2 ? bounds.Max.Y : bounds.Min.Y, mask & 4 ? bounds.Max.Z : bounds.Min.Z});
            const double values[]{point.X, point.Y, point.Z};
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(values[i])) { throw std::invalid_argument("Scene bounds exceed the supported numeric range."); }
                if (first) { low[i] = high[i] = values[i]; }
                else { low[i] = std::min(low[i], values[i]); high[i] = std::max(high[i], values[i]); }
            }
            first = false;
        }
    }
    const double radius = std::max(std::hypot(high[0]-low[0], high[1]-low[1], high[2]-low[2]) * 0.5, 0.0001);
    const double vertical = camera.VerticalFovRadians * 0.5;
    const double horizontal = std::atan(std::tan(vertical) * aspect);
    const double distance = radius * 1.1 / std::sin(std::min(vertical, horizontal));
    const auto direction = Normalize(Subtract(camera.Position, camera.Target));
    camera.Target = {static_cast<float>((low[0]+high[0])*0.5), static_cast<float>((low[1]+high[1])*0.5), static_cast<float>((low[2]+high[2])*0.5)};
    camera.Position = Add(camera.Target, Scale(direction, distance));
    camera.NearPlane = static_cast<float>(std::max(radius * 0.001, distance-radius*1.5));
    camera.FarPlane = static_cast<float>(distance + radius*2);
    (void)camera.ViewProjection(aspect);
    return camera;
}

bool CameraController::Process(Camera& camera, const ViewerInputEvent& event, const CameraControlOptions& options, int height) {
    if (!std::isfinite(event.DeltaX) || !std::isfinite(event.DeltaY) || !std::isfinite(event.ScrollY)) {
        throw std::invalid_argument("Input deltas must be finite.");
    }
    if (event.Type == ViewerInputType::FocusLost) { Reset(); return false; }
    if (event.Type == ViewerInputType::MouseDown || event.Type == ViewerInputType::MouseUp) {
        const bool down = event.Type == ViewerInputType::MouseDown;
        if (event.Button == ViewerMouseButton::Left) { left_ = down; }
        if (event.Button == ViewerMouseButton::Right) { right_ = down; }
        if (event.Button == ViewerMouseButton::Middle) { middle_ = down; }
        return false;
    }
    const bool orbit = event.Type == ViewerInputType::MouseMove && left_ && !event.Shift;
    const bool pan = event.Type == ViewerInputType::MouseMove && (right_ || middle_ || (left_ && event.Shift));
    const bool zoom = event.Type == ViewerInputType::Scroll && event.ScrollY != 0;
    if (!orbit && !pan && !zoom) { return false; }
    if ((zoom && options.ZoomSpeed == 0) || (pan && options.PanSpeed == 0) ||
        (orbit && !pan && options.OrbitSpeed == 0) || (!zoom && event.DeltaX == 0 && event.DeltaY == 0)) { return false; }
    if (pan && height <= 0) { throw std::invalid_argument("Panning requires a positive viewport height."); }
    (void)camera.ViewProjection(1);
    auto next = camera;
    const auto offset = Subtract(camera.Position, camera.Target);
    const double radius = Length(offset);
    const auto up = Normalize(camera.Up);
    if (zoom) {
        const double factor = std::exp(std::clamp(-double(event.ScrollY)*options.ZoomSpeed, -100.0, 100.0));
        const double distance = std::clamp(radius*factor, double(options.MinimumDistance), double(options.MaximumDistance));
        next.Position = Add(camera.Target, Scale(offset, distance/radius));
        // Keep the orbit target inside the clipping range while zooming across
        // model scales; a fitted far plane must not make zoom-out go blank.
        next.NearPlane = std::min(camera.NearPlane,static_cast<float>(distance*0.001));
        next.FarPlane = std::max(camera.FarPlane,static_cast<float>(distance*2));
    } else if (pan) {
        const auto forward = Normalize(Scale(offset,-1));
        const auto right = Normalize(Cross(forward, up));
        const auto cameraUp = Cross(right, forward);
        const double scale = 2*radius*std::tan(camera.VerticalFovRadians*0.5)/height*options.PanSpeed;
        const auto shift = Add(Scale(right, -double(event.DeltaX)*scale), Scale(cameraUp, double(event.DeltaY)*scale));
        next.Position = Add(next.Position, shift); next.Target = Add(next.Target, shift);
    } else {
        const auto direction = Normalize(offset);
        const double oldPitch = std::asin(std::clamp(Dot(direction,up), -1.0, 1.0));
        const double pitch = std::clamp(oldPitch + double(event.DeltaY)*options.OrbitSpeed,
            -std::numbers::pi/2+0.01, std::numbers::pi/2-0.01);
        auto horizontal = Normalize(Subtract(direction, Scale(up, Dot(direction,up))));
        horizontal = Rotate(horizontal, up, -double(event.DeltaX)*options.OrbitSpeed);
        next.Position = Add(camera.Target, Scale(Add(Scale(horizontal,std::cos(pitch)), Scale(up,std::sin(pitch))), radius));
    }
    (void)next.ViewProjection(1);
    if (next.Position.X == camera.Position.X && next.Position.Y == camera.Position.Y && next.Position.Z == camera.Position.Z &&
        next.Target.X == camera.Target.X && next.Target.Y == camera.Target.Y && next.Target.Z == camera.Target.Z &&
        next.NearPlane == camera.NearPlane && next.FarPlane == camera.FarPlane) { return false; }
    camera = next;
    return true;
}

} // namespace lv::detail
