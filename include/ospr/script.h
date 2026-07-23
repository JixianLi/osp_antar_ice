#pragma once

#include <string>
#include <vector>

#include "ospr/colormap.h"
#include "ospr/keyframe.h"
#include "ospr/math.h"

namespace ospr {

struct Range
{
    float lo{0.0f};
    float hi{1.0f};
};

// layer_id -> age_ka. volume.py interpolates both fields over the same vertical
// parameter, so age is piecewise linear in layer_id: measured spread within a
// layer_id bin is 0.36 ka against a 68 ka range. That is what lets one scalar
// field drive age colour and layer opacity at once.
struct AgeKnot
{
    float layer{0.0f};
    float age{0.0f};
};

struct VolumeSpec
{
    std::string path;
    std::string scalar{"layer_id"};
    Range value_range{0.0f, 5.0f};
    std::string colormap_path;
    ColorMapTrim trim;
    std::vector<AgeKnot> age_knots;
    // Basal and Bed are undated: age_ka is NaN above the last knot, so that
    // part of the LUT takes a flat colour instead of an extrapolated age.
    Vec3 undated_color{0.30f, 0.28f, 0.30f};
    float density_scale{1.0f};
};

struct SurfaceSpec
{
    std::string path;
    std::string color_by{"depth"};
    std::string colormap_path;
    ColorMapTrim trim;
    Range value_range{0.0f, 4000.0f};
    // Position in the layer stack, so one peel curve drives the whole scene.
    // Negative means pinned opaque -- the bed is the floor and never fades.
    float layer{-1.0f};
    float roughness{0.7f};
};

struct LightSpec
{
    std::string type{"distant"};
    Vec3 direction{-0.4f, -0.7f, -0.6f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    float angular_diameter{0.53f};
    // OSPRay lights are visible to camera rays by default, so an ambient light
    // paints itself over backgroundColor. Fill light almost never wants that.
    bool visible{true};
};

struct RendererSpec
{
    std::string type{"pathtracer"};
    int samples_per_pixel{32};
    bool denoise{true};
    Vec3 background{0.02f, 0.02f, 0.03f};
};

// A circular camera path, because three revolutions as explicit keyframes is
// about thirty-six entries and a Catmull-Rom through points on a circle bulges
// between them -- a slow orbit visibly breathes.
struct OrbitSpec
{
    bool enabled{false};
    // Left unset, these are resolved from the loaded scene's bounds: the centre
    // of a 1050 x 770 km domain is not something to type in by hand, and a
    // hand-picked radius silently crops the scene.
    bool has_center{false};
    bool has_radius{false};
    Vec3 center{0.0f, 0.0f, 0.0f};
    float radius{1.0f};
    float elevation_degrees{25.0f};
    float azimuth_start_degrees{0.0f};
    float revolutions{1.0f};
    float fov_y_degrees{40.0f};
    Vec3 up{0.0f, 0.0f, 1.0f};
};

struct Session
{
    float z_scale{35.0f};
    RendererSpec renderer;
    std::vector<VolumeSpec> volumes;
    std::vector<SurfaceSpec> surfaces;
    std::vector<LightSpec> lights;
};

struct OutputSpec
{
    std::string directory{"frames"};
    int width{1280};
    int height{720};
};

struct TimelineSpec
{
    float frames_per_second{30.0f};
    float duration_seconds{4.0f};
};

struct Script
{
    Session session;
    OutputSpec output;
    TimelineSpec timeline;
    OrbitSpec orbit;
    std::vector<Keyframe> keyframes;
};

// Throws std::runtime_error on a missing file, malformed JSON, or a field of
// the wrong type. Keyframes are returned sorted by time. Paths inside the
// script resolve relative to the script's own directory.
Script load_script(const std::string& path);

int frame_count(const Script& script);
float frame_time(const Script& script, int frame_index);

// Position on the orbit at the given time, or the interpolated keyframe camera
// when the orbit is disabled.
Camera camera_for(const Script& script, float time_seconds);

} // namespace ospr
