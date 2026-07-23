#include "ospr/keyframe.h"

#include <algorithm>

namespace ospr {
namespace {

// Uniform Catmull-Rom spline (Catmull & Rom 1974) at tension 1/2, evaluated on
// the segment p1..p2 with p0/p3 as the outer control points:
//
//   p(t) = 1/2 * ( 2*p1 + (p2 - p0)*t
//                + (2*p0 - 5*p1 + 4*p2 - p3)*t^2
//                + (3*p1 - p0 - 3*p2 + p3)*t^3 )
//
// The spline passes through every control point but may overshoot between
// them, which is what gives a camera path its natural arc. Parameterisation is
// per-segment, so unevenly spaced keyframes produce uneven speed.
Vec3 catmull_rom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const Vec3 a = p1 * 2.0f;
    const Vec3 b = (p2 - p0) * t;
    const Vec3 c = (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2;
    const Vec3 d = (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3;
    return (a + b + c + d) * 0.5f;
}

float apply_ease(float t, Ease ease)
{
    return ease == Ease::Smooth ? t * t * (3.0f - 2.0f * t) : t;
}

// Remove any component of up that lies along the view direction, so the camera
// basis stays orthogonal after the up vectors have been blended.
Vec3 orthogonalize_up(Vec3 up, Vec3 forward)
{
    const Vec3 orthogonal = up - forward * dot(up, forward);
    return length(orthogonal) > 1e-6f ? normalize(orthogonal) : up;
}

} // namespace

Camera camera_at(const std::vector<Keyframe>& keyframes, float time_seconds)
{
    if (keyframes.empty())
        return Camera{};
    if (keyframes.size() == 1 || time_seconds <= keyframes.front().time_seconds)
        return keyframes.front().camera;
    if (time_seconds >= keyframes.back().time_seconds)
        return keyframes.back().camera;

    const int last = static_cast<int>(keyframes.size()) - 1;
    int segment = 0;
    while (segment < last - 1 && keyframes[segment + 1].time_seconds <= time_seconds)
        ++segment;

    const Keyframe& from = keyframes[segment];
    const Keyframe& to = keyframes[segment + 1];
    const float span = to.time_seconds - from.time_seconds;
    const float raw = span > 0.0f ? (time_seconds - from.time_seconds) / span : 0.0f;
    const float t = apply_ease(std::clamp(raw, 0.0f, 1.0f), from.ease);

    const Camera& before = keyframes[std::max(segment - 1, 0)].camera;
    const Camera& after = keyframes[std::min(segment + 2, last)].camera;

    Camera result;
    result.position = catmull_rom(
        before.position, from.camera.position, to.camera.position, after.position, t);
    result.target = catmull_rom(
        before.target, from.camera.target, to.camera.target, after.target, t);
    // Field of view is interpolated linearly: a spline could overshoot past the
    // valid (0, 180) range on a sharp zoom.
    result.fov_y_degrees = lerp(from.camera.fov_y_degrees, to.camera.fov_y_degrees, t);

    const Vec3 forward = normalize(result.target - result.position);
    result.up = orthogonalize_up(lerp(from.camera.up, to.camera.up, t), forward);
    return result;
}

} // namespace ospr
