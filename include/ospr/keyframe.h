#pragma once

#include <vector>

#include "ospr/camera.h"

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
    Ease ease{Ease::Smooth};
};

// Keyframes must be sorted by time_seconds. Times outside the keyframe range
// clamp to the first/last camera.
Camera camera_at(const std::vector<Keyframe>& keyframes, float time_seconds);

} // namespace ospr
