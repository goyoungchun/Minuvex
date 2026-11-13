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

Camera FitSceneCamera(const Scene& scene, float aspect,ObjectId focus) {
    auto camera = scene.MainCamera;
    (void)camera.ViewProjection(aspect);
    const auto items = scene.CollectRenderItems();
    if (items.empty()) { throw std::invalid_argument("There are no visible models to frame."); }
    double low[3]{}, high[3]{};
    bool first = true;
    if (focus) {(void)scene.GetObject(focus);}
    for (const auto& item : items) {
        if (focus) {auto ancestor=item.Object;while (ancestor && ancestor != focus) {ancestor=scene.GetObject(ancestor).Parent;}if (!ancestor) {continue;}}
        (void)item.WorldTransform.NormalMatrix();
        const auto bounds = scene.GetModelBounds(item.Object);
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
    if (first) {throw std::invalid_argument("The selected object/subtree has no visible models to focus.");}
    const double radius = std::max(std::hypot(high[0]-low[0], high[1]-low[1], high[2]-low[2]) * 0.5, 0.0001);
    const double vertical = camera.VerticalFovRadians * 0.5;
    const double horizontal = std::atan(std::tan(vertical) * aspect);
    const double distance = camera.Projection == CameraProjection::Orthographic ? radius*2:radius * 1.1 / std::sin(std::min(vertical, horizontal));
    if (camera.Projection == CameraProjection::Orthographic) {camera.OrthographicHeight=static_cast<float>(radius*2.2/std::min(1.0f,aspect));}
    const auto direction = Normalize(Subtract(camera.Position, camera.Target));
    camera.Target = {static_cast<float>((low[0]+high[0])*0.5), static_cast<float>((low[1]+high[1])*0.5), static_cast<float>((low[2]+high[2])*0.5)};
    camera.Position = Add(camera.Target, Scale(direction, distance));
    camera.NearPlane = static_cast<float>(std::max(radius * 0.001, distance-radius*1.5));
    camera.FarPlane = static_cast<float>(distance + radius*2);
    (void)camera.ViewProjection(aspect);
    return camera;
}

ObjectId PickSceneObject(const Scene& scene,float aspect,float x,float y) {
    const auto& camera=scene.MainCamera;(void)camera.ViewProjection(aspect);
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1) {throw std::invalid_argument("Picking coordinates must be in [-1,1].");}
    const auto forward=Normalize(Subtract(camera.Target,camera.Position));const auto right=Normalize(Cross(forward,camera.Up));const auto up=Cross(right,forward);const double tangent=std::tan(camera.VerticalFovRadians*.5);
    const bool ortho=camera.Projection == CameraProjection::Orthographic;
    const auto ray=ortho ? forward:Normalize(Add(forward,Add(Scale(right,x*tangent*aspect),Scale(up,y*tangent))));
    const auto eye=ortho ? Add(camera.Position,Add(Scale(right,x*camera.OrthographicHeight*aspect*.5),Scale(up,y*camera.OrthographicHeight*.5))):camera.Position;
    const double cosine=Dot(ray,forward),origin[]{eye.X,eye.Y,eye.Z},direction[]{ray.X,ray.Y,ray.Z};
    double nearest=camera.FarPlane/cosine;ObjectId selected=NoObject;
    for (const auto& item:scene.CollectRenderItems()) {
        const auto bounds=scene.GetModelBounds(item.Object);double low[]{INFINITY,INFINITY,INFINITY},high[]{-INFINITY,-INFINITY,-INFINITY};
        for (unsigned mask=0;mask<8;++mask) {const auto point=item.WorldTransform.TransformPoint({mask&1 ? bounds.Max.X:bounds.Min.X,mask&2 ? bounds.Max.Y:bounds.Min.Y,mask&4 ? bounds.Max.Z:bounds.Min.Z});const double p[]{point.X,point.Y,point.Z};for (int i=0;i<3;++i) {if (!std::isfinite(p[i])) {throw std::invalid_argument("Pick bounds exceed numeric range.");}low[i]=std::min(low[i],p[i]);high[i]=std::max(high[i],p[i]);}}
        double near=camera.NearPlane/cosine,far=nearest;bool hit=true;
        for (int axis=0;axis<3;++axis) {if (std::abs(direction[axis]) < 1e-12) {if (origin[axis] < low[axis] || origin[axis] > high[axis]) {hit=false;break;}}else {double a=(low[axis]-origin[axis])/direction[axis],b=(high[axis]-origin[axis])/direction[axis];if (a>b) {std::swap(a,b);}near=std::max(near,a);far=std::min(far,b);if (near>far) {hit=false;break;}}}
        if (hit && near < nearest) {nearest=near;selected=item.Object;}
    }
    return selected;
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
        if (camera.Projection == CameraProjection::Orthographic) {next.OrthographicHeight=static_cast<float>(std::clamp(camera.OrthographicHeight*factor,double(options.MinimumDistance),double(options.MaximumDistance)));}
        else {
        const double distance = std::clamp(radius*factor, double(options.MinimumDistance), double(options.MaximumDistance));
        next.Position = Add(camera.Target, Scale(offset, distance/radius));
        // Keep the orbit target inside the clipping range while zooming across
        // model scales; a fitted far plane must not make zoom-out go blank.
        next.NearPlane = std::min(camera.NearPlane,static_cast<float>(distance*0.001));
        next.FarPlane = std::max(camera.FarPlane,static_cast<float>(distance*2));
        }
    } else if (pan) {
        const auto forward = Normalize(Scale(offset,-1));
        const auto right = Normalize(Cross(forward, up));
        const auto cameraUp = Cross(right, forward);
        const double span=camera.Projection == CameraProjection::Orthographic ? camera.OrthographicHeight:2*radius*std::tan(camera.VerticalFovRadians*0.5);
        const double scale = span/height*options.PanSpeed;
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
        next.NearPlane == camera.NearPlane && next.FarPlane == camera.FarPlane && next.OrthographicHeight == camera.OrthographicHeight) { return false; }
    camera = next;
    return true;
}

} // namespace lv::detail
