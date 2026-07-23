#pragma once

#include "ospr/math.h"

namespace ospr {

struct Camera
{
    Vec3 position{0.0f, 0.0f, 5.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fov_y_degrees{45.0f};
};

} // namespace ospr
