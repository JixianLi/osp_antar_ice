#pragma once

#include <vector>

#include "ospr/camera.h"
#include "ospr/opacity_curve.h"

namespace ospr {

enum class Ease
{
    Linear,
    Smooth,
};

// The camera is stored as an orbit pose, because the motion is described that
// way: a revolution or a turn is an azimuth delta. Target is the scene center,
// up is the scene up. Azimuth interpolates linearly and may run past 360, so a
// full revolution is simply a keyframe pair 360 degrees apart.
struct Keyframe
{
    float azimuth_degrees{0.0f};
    float elevation_degrees{25.0f};
    float radius{3.0f};
    float fov_y_degrees{40.0f};
    OpacityCurve opacity;
    Ease ease{Ease::Smooth};
    // Interpolated frames to insert between this keyframe and the next; -1 means
    // use the timeline default. The value on the last keyframe is unused.
    int frames_after{-1};
};

// The pose an orbit names: azimuth and elevation are degrees about center on a
// z-up sphere, radius the distance to it. The preview's free camera and
// camera_at both go through this, so a pose the preview shows and the pose
// ospr_render builds from the same numbers cannot drift apart.
Camera orbit_pose(float azimuth_degrees, float elevation_degrees, float radius,
    float fov_y_degrees, Vec3 center, Vec3 up);

// u is a keyframe-index parameter in [0, keyframes.size() - 1]; an integer u
// lands exactly on a keyframe. The ease of the keyframe a segment leaves shapes
// that segment.
Camera camera_at(const std::vector<Keyframe>& keyframes, float u, Vec3 center, Vec3 up);
OpacityCurve opacity_at(const std::vector<Keyframe>& keyframes, float u);

} // namespace ospr
