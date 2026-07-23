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

struct VolumeSpec
{
    std::string path;
    std::string scalar{"layer_id"};
    Range value_range{0.0f, 5.0f};
    // Colour is by layer_id directly. The ice colourmap covers [1, split]
    // (dated ice, L1..L7), the rock colourmap covers [split, 5] (Basal, Bed).
    std::string ice_colormap_path;
    ColorMapTrim ice_trim;
    std::string rock_colormap_path;
    ColorMapTrim rock_trim;
    float split{3.0f};
    float density_scale{1.0f};
    // Extend the bed (deepest valid layer_id) straight down to the grid floor,
    // so the model sits on a solid rock base instead of a thin bed surface.
    bool fill_base{false};
    // Adds one constant depth to each of the four ice bands so the thin
    // isochrones are visible, sized so the thickest band reaches layer_fill
    // times the bed's relief. Every surface keeps its shape (it is a rigid
    // shift) but band ratios are distorted, and the grid deepens to fit.
    // 0 disables it, and so does anything below thickest_band / bed_relief
    // (about 0.25 for the Singh data), where the target is already shorter than
    // the thickest band.
    float layer_fill{0.0f};
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
};

struct RendererSpec
{
    std::string type{"pathtracer"};
    int samples_per_pixel{32};
    bool denoise{true};
    // Vertical backplate gradient, interpolated in HSV. top is the top of the
    // frame, bottom the bottom; equal values give a flat background.
    Vec3 background_top{0.0f, 0.0f, 0.0f};
    Vec3 background_bottom{0.47f, 0.47f, 0.47f};
};

// Orbit fitting for the preview's free camera only; the timeline's camera comes
// from the keyframes. frame_scene fills centre and radius from the scene bounds.
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
    float z_scale{35.0f};
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

// Frames the keyframes expand to, and the keyframe-index parameter u a given
// output frame maps to.
int frame_count(const Script& script);
float frame_to_param(const Script& script, int frame_index);

// The output frame index that lands exactly on keyframe k.
int keyframe_frame(const Script& script, int keyframe_index);

// The interpolated camera at keyframe parameter u. The scene is normalised
// about the origin, so the orbit target is (0, 0, 0).
Camera camera_for(const Script& script, float u);

} // namespace ospr
