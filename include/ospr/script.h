#pragma once

#include <string>
#include <vector>

#include "ospr/keyframe.h"
#include "ospr/math.h"

namespace ospr {

struct SceneSpec
{
    // "tetrahedron" is the built-in test scene; volume sources come later.
    std::string type{"tetrahedron"};
};

struct OutputSpec
{
    std::string directory{"frames"};
    int width{1280};
    int height{720};
    int samples_per_pixel{16};
    Vec3 background{0.05f, 0.05f, 0.07f};
};

struct TimelineSpec
{
    float frames_per_second{30.0f};
    float duration_seconds{4.0f};
};

struct Script
{
    SceneSpec scene;
    OutputSpec output;
    TimelineSpec timeline;
    std::vector<Keyframe> keyframes;
};

// Throws std::runtime_error on a missing file, malformed JSON, or a field of
// the wrong type. Keyframes are returned sorted by time.
Script load_script(const std::string& path);

int frame_count(const Script& script);
float frame_time(const Script& script, int frame_index);

} // namespace ospr
