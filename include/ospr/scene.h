#pragma once

#include <vector>

#include <ospray/ospray_cpp.h>

#include "ospr/colormap.h"
#include "ospr/opacity_curve.h"
#include "ospr/script.h"

namespace ospr {

struct Bounds
{
    Vec3 lo;
    Vec3 hi;
    Vec3 center() const { return (lo + hi) * 0.5f; }
    float diagonal() const { return length(hi - lo); }
};

// Points the orbit at the scene and pulls the camera back far enough that the
// bounding sphere fits the vertical field of view. Only fills in what the
// script left unspecified.
void frame_scene(OrbitSpec& orbit, const Bounds& bounds);

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
    };

    void add_volume(const VolumeSpec& spec, float z_scale);
    void add_surface(const SurfaceSpec& spec, float z_scale);
    void build_world(const Session& session);

    std::vector<VolumeEntry> volumes_;
    std::vector<SurfaceEntry> surfaces_;
    std::vector<ospray::cpp::Light> lights_;
    ospray::cpp::Group group_;
    ospray::cpp::Instance instance_;
    ospray::cpp::World world_;
    Bounds bounds_;
    bool bounds_initialised_{false};
};

} // namespace ospr
