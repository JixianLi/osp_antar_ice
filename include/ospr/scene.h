#pragma once

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
// The colour LUT is baked at construction; only opacity changes with time, so a
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
    // Because the scene is normalised on its longest side (x, unaffected by
    // z_scale), a z_scale change is a uniform scaling of every z about the
    // origin -- no re-read, no re-normalise, just multiply z by the ratio.
    void set_z_scale(float z_scale);
    float z_scale() const { return z_scale_; }

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
        // Current normalised grid placement, so set_z_scale can rescale z
        // without recomputing from the source header.
        Vec3 grid_origin;
        Vec3 grid_spacing;
    };

    struct SurfaceEntry
    {
        SurfaceSpec spec;
        ospray::cpp::Material material;
        ospray::cpp::GeometricModel model;
        // Kept so the preview can re-range the colour ramp without re-reading
        // the file; the geometry and its BVH are untouched by a range change.
        ospray::cpp::Geometry mesh;
        std::vector<float> field;
        ColorMap colormap;
        // Retained so a z_scale change can rescale z and re-upload positions
        // without re-reading the .vts.
        std::vector<Vec3> positions;
    };

    void add_volume(const ImageData& data, const VolumeSpec& spec, float z_scale);
    void add_surface(const StructuredGrid& grid, const SurfaceSpec& spec, float z_scale);
    void add_tetrahedron(const TetrahedronSpec& spec);
    void build_world(const Session& session);

    // Every object is rescaled so the whole scene's longest side spans [-1, 1]
    // about the origin, aspect preserved. Keeps camera distance, near clip and
    // volume densityScale all O(1) instead of scattered across 1e6 metres.
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
    float z_scale_{1.0f};
};

} // namespace ospr
