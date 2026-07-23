#pragma once

#include <vector>

#include <ospray/ospray_cpp.h>

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

// Loads every object in the session once, then re-applies the peel each frame.
// The colour LUT is baked at construction; only opacity changes with time, so a
// frame costs a small LUT rebuild rather than a reload.
class Scene
{
public:
    explicit Scene(const Session& session);

    void apply_opacity(const OpacityCurve& curve);

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
    };

    void add_volume(const VolumeSpec& spec, float z_scale);
    void add_surface(const SurfaceSpec& spec, float z_scale);
    void build_world(const Session& session);

    std::vector<VolumeEntry> volumes_;
    std::vector<SurfaceEntry> surfaces_;
    std::vector<ospray::cpp::Light> lights_;
    ospray::cpp::World world_;
    Bounds bounds_;
    bool bounds_initialised_{false};
};

} // namespace ospr
