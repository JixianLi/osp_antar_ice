#pragma once

#include <array>
#include <vector>

#include <ospray/ospray_cpp.h>

#include "ospr/colormap.h"
#include "ospr/opacity_curve.h"
#include "ospr/script.h"
#include "ospr/vtk_xml.h"

namespace ospr {

struct Bounds
{
    Vec3 lo;
    Vec3 hi;
    Vec3 center() const { return (lo + hi) * 0.5f; }
    float diagonal() const { return length(hi - lo); }
};

// Points the orbit at the scene and pulls the camera back just far enough that
// the projected bounding box fits the frame at every azimuth. Fitting the
// bounding sphere instead wastes most of the frame on a flat slab seen at an
// angle. Only fills in what the script left unspecified. aspect is width/height.
void frame_scene(OrbitSpec& orbit, const Bounds& bounds, float aspect);

// Loads every object in the session once, then re-applies the peel each frame.
// The color LUT is baked at construction; only opacity changes with time, so a
// frame costs a small LUT rebuild rather than a reload.
class Scene
{
public:
    explicit Scene(const Session& session);

    void apply_opacity(const OpacityCurve& curve);

    // Live edits from the preview. Each re-commits only what it touched; the
    // volume data and surface geometry are never reloaded.
    void set_lights(const std::vector<LightSpec>& lights);
    void set_density_scale(std::size_t index, float density_scale);
    void set_surface_range(std::size_t index, Range range);
    // Paints each layer a single color, keyed by layer_id rounded to the
    // nearest surface, so all five colors appear and boundaries fall at the
    // midpoints between isochrones. Cheap: color is by layer_id through the
    // transfer function, a 256-entry LUT rebuild, so nothing about the volume
    // moves.
    void set_flat_colors(std::size_t index, const std::array<Vec3, LAYER_COUNT>& layer_colors);

    std::size_t volume_count() const { return volumes_.size(); }
    std::size_t surface_count() const { return surfaces_.size(); }
    const SurfaceSpec& surface_spec(std::size_t index) const { return surfaces_[index].spec; }
    const VolumeSpec& volume_spec(std::size_t index) const { return volumes_[index].spec; }

    const ospray::cpp::World& world() const { return world_; }
    const Bounds& bounds() const { return bounds_; }

private:
    struct VolumeEntry
    {
        VolumeSpec spec;
        ospray::cpp::TransferFunction transfer;
        ospray::cpp::VolumetricModel model;
        ospray::cpp::Volume volume;
    };

    struct SurfaceEntry
    {
        SurfaceSpec spec;
        ospray::cpp::Material material;
        ospray::cpp::GeometricModel model;
        // Kept so the preview can re-range the color ramp without re-reading
        // the file; the geometry and its BVH are untouched by a range change.
        ospray::cpp::Geometry mesh;
        std::vector<float> field;
        ColorMap colormap;
    };

    void add_volume(const ImageData& data, const VolumeSpec& spec);
    void add_surface(const StructuredGrid& grid, const SurfaceSpec& spec);
    void add_tetrahedron(const TetrahedronSpec& spec);
    void build_world(const Session& session);

    // Every object is rescaled so the whole scene's longest side spans [-1, 1]
    // about the origin, aspect preserved. Keeps camera distance, near clip and
    // volume densityScale all O(1) instead of scattered across 1e6 meters.
    Vec3 to_normalized(Vec3 world) const { return (world - center_) * scale_; }

    std::vector<VolumeEntry> volumes_;
    std::vector<SurfaceEntry> surfaces_;
    std::vector<ospray::cpp::Light> lights_;
    ospray::cpp::Group group_;
    ospray::cpp::Instance instance_;
    ospray::cpp::World world_;
    Bounds bounds_;
    Vec3 center_;
    float scale_{1.0f};
};

} // namespace ospr
