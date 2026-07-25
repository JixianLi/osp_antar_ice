#pragma once

#include <array>
#include <string>
#include <vector>

#include "ospr/colormap.h"
#include "ospr/keyframe.h"
#include "ospr/math.h"

namespace ospr {

// The five isochrone surfaces the volume is colored by: L1, L5, L7, Basal, Bed.
inline constexpr int LAYER_COUNT = 5;

struct Range
{
    float lo{0.0f};
    float hi{1.0f};
};

struct VolumeSpec
{
    std::string path;
    std::string scalar{"layer_id"};
    Range value_range{0.0f, 5.0f};
    // Color is by layer_id directly: one flat color per isochrone surface,
    // keyed by layer_id rounded to the nearest of the five (see build_flat_lut).
    std::array<Vec3, LAYER_COUNT> layer_colors{{{0.75f, 0.87f, 0.95f},
        {0.30f, 0.70f, 0.80f}, {0.40f, 0.70f, 0.35f}, {0.85f, 0.55f, 0.25f},
        {0.35f, 0.22f, 0.15f}}};
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

// Built-in scene with no data files, so the smoke test that proves a build
// works still runs inside the render container and on a compute node.
struct TetrahedronSpec
{
    float scale{1.0f};
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
    // Off lights are simply left out of the world's light list.
    bool enabled{true};
};

struct RendererSpec
{
    std::string type{"pathtracer"};
    int samples_per_pixel{32};
    bool denoise{true};
    // Shadow and soft-light rays per hit, the path tracer's lightSamples. Named
    // for what it buys rather than for the OSPRay parameter, which is what the
    // preview slider calls it too.
    int light_samples{1};
    // Ambient-occlusion rays, scivis only. Each renderer ignores the other's
    // count, so one session file can carry both.
    int ao_samples{2};
    // Vertical backplate gradient, a straight RGB ramp. top is the top of the
    // frame, bottom the bottom; equal values give a flat background.
    Vec3 background_top{0.0f, 0.0f, 0.0f};
    Vec3 background_bottom{0.47f, 0.47f, 0.47f};
};

// Orbit fitting for the preview's free camera only; the timeline's camera comes
// from the keyframes. frame_scene fills center and radius from the scene bounds.
struct OrbitSpec
{
    bool has_center{false};
    bool has_radius{false};
    Vec3 center{0.0f, 0.0f, 0.0f};
    float radius{1.0f};
    float elevation_degrees{25.0f};
    float azimuth_start_degrees{0.0f};
    float fov_y_degrees{40.0f};
    Vec3 up{0.0f, 0.0f, 1.0f};
};

struct Session
{
    RendererSpec renderer;
    std::vector<VolumeSpec> volumes;
    std::vector<SurfaceSpec> surfaces;
    std::vector<TetrahedronSpec> tetrahedra;
    std::vector<LightSpec> lights;
};

struct OutputSpec
{
    std::string directory{"frames"};
    int width{1280};
    int height{720};
};

struct Script
{
    Session session;
    OutputSpec output;
    // Frames inserted between each consecutive keyframe pair, unless a keyframe
    // overrides it with frames_after. The camera and opacity of the frames in
    // between are interpolated; the keyframes themselves are the only poses the
    // user sets.
    int frames_between{20};
    Vec3 up{0.0f, 0.0f, 1.0f};
    std::vector<Keyframe> keyframes;
};

// Throws std::runtime_error on a missing file, malformed JSON, or a field of
// the wrong type. Paths inside the script resolve relative to the script's own
// directory.
Script load_script(const std::string& path);

// Section writers for the preview's per-section save buttons. Each re-reads the
// file, replaces only the fields it names, and writes it back with 2-space
// indent; every other value is preserved (formatting may normalize). They throw
// std::runtime_error if the file is missing, malformed, or lacks the target
// structure. The volume knobs address the first volume object; the color save
// too, since the scene has carried one volume throughout.
void save_quality(const std::string& path, int spp, int shadow_samples, int ao_samples);
void save_background(const std::string& path, Vec3 top, Vec3 bottom);
void save_lights(const std::string& path, const std::vector<LightSpec>& lights);
void save_density(const std::string& path, float density_scale);
void save_colors(const std::string& path, const std::array<Vec3, LAYER_COUNT>& layer_colors);
void save_frames_between(const std::string& path, int frames_between);
void save_opacity(const std::string& path, int keyframe_index, const OpacityCurve& opacity);
// Camera is global while the trajectory feature is hidden, so this writes the
// one pose to every keyframe, keeping them identical.
void save_camera(const std::string& path, float azimuth, float elevation, float fov, float radius);

// Frames the keyframes expand to, and the keyframe-index parameter u a given
// output frame maps to.
int frame_count(const Script& script);
float frame_to_param(const Script& script, int frame_index);

// The output frame index that lands exactly on keyframe k.
int keyframe_frame(const Script& script, int keyframe_index);

// The interpolated camera at keyframe parameter u. The scene is normalized
// about the origin, so the orbit target is (0, 0, 0).
Camera camera_for(const Script& script, float u);

} // namespace ospr
