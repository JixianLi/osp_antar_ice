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

struct Keyframe
{
    float time_seconds{0.0f};
    Camera camera;
    OpacityCurve opacity;
    Ease ease{Ease::Smooth};
};

// Keyframes must be sorted by time_seconds. Times outside the keyframe range
// clamp to the first/last camera.
Camera camera_at(const std::vector<Keyframe>& keyframes, float time_seconds);

// Blends the two bracketing peel curves. Holding a beat is two consecutive
// keyframes with the same curve.
OpacityCurve opacity_at(const std::vector<Keyframe>& keyframes, float time_seconds);

} // namespace ospr
