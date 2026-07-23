#include "ospr/keyframe.h"

#include <algorithm>
#include <cmath>

namespace ospr {
namespace {

constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;

float apply_ease(float t, Ease ease)
{
    return ease == Ease::Smooth ? t * t * (3.0f - 2.0f * t) : t;
}

struct Segment
{
    int index{0};
    float t{0.0f};
};

Segment locate(const std::vector<Keyframe>& keyframes, float u)
{
    const int last = static_cast<int>(keyframes.size()) - 1;
    const int index = std::clamp(static_cast<int>(std::floor(u)), 0, last - 1);
    const float raw = std::clamp(u - static_cast<float>(index), 0.0f, 1.0f);
    return {index, apply_ease(raw, keyframes[index].ease)};
}

Vec3 orthogonalize_up(Vec3 up, Vec3 forward)
{
    const Vec3 orthogonal = up - forward * dot(up, forward);
    return length(orthogonal) > 1e-6f ? normalize(orthogonal) : up;
}

Camera orbit_pose(
    float azimuth_degrees, float elevation_degrees, float radius, float fov, Vec3 center, Vec3 up)
{
    const float azimuth = azimuth_degrees * DEGREES_TO_RADIANS;
    const float elevation = elevation_degrees * DEGREES_TO_RADIANS;

    Camera camera;
    camera.target = center;
    camera.position = center
        + Vec3{radius * std::cos(elevation) * std::cos(azimuth),
              radius * std::cos(elevation) * std::sin(azimuth),
              radius * std::sin(elevation)};
    camera.fov_y_degrees = fov;
    camera.up = orthogonalize_up(up, normalize(camera.target - camera.position));
    return camera;
}

} // namespace

Camera camera_at(const std::vector<Keyframe>& keyframes, float u, Vec3 center, Vec3 up)
{
    if (keyframes.empty())
        return Camera{};
    if (keyframes.size() == 1)
        return orbit_pose(keyframes[0].azimuth_degrees,
            keyframes[0].elevation_degrees,
            keyframes[0].radius,
            keyframes[0].fov_y_degrees,
            center,
            up);

    const Segment segment = locate(keyframes, u);
    const Keyframe& from = keyframes[segment.index];
    const Keyframe& to = keyframes[segment.index + 1];
    const float t = segment.t;

    return orbit_pose(lerp(from.azimuth_degrees, to.azimuth_degrees, t),
        lerp(from.elevation_degrees, to.elevation_degrees, t),
        lerp(from.radius, to.radius, t),
        lerp(from.fov_y_degrees, to.fov_y_degrees, t),
        center,
        up);
}

OpacityCurve opacity_at(const std::vector<Keyframe>& keyframes, float u)
{
    if (keyframes.empty())
        return OpacityCurve{};
    if (keyframes.size() == 1)
        return keyframes[0].opacity;

    const Segment segment = locate(keyframes, u);
    return lerp(keyframes[segment.index].opacity,
        keyframes[segment.index + 1].opacity,
        segment.t);
}

} // namespace ospr
