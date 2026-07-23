#include "ospr/scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "ospr/colormap.h"
#include "ospr/vtk_xml.h"

namespace ospr {
namespace {

constexpr int LUT_SIZE = 256;

// Piecewise-linear age(layer_id) from the script's knots. Below the first knot
// the ice is above L1; above the last it is the undated Basal/Bed column, which
// the caller paints flat rather than extrapolating an age nobody measured.
float age_at(const std::vector<AgeKnot>& knots, float layer, bool& dated)
{
    dated = true;
    if (layer <= knots.front().layer)
        return knots.front().age;
    if (layer >= knots.back().layer) {
        dated = layer <= knots.back().layer;
        return knots.back().age;
    }
    for (std::size_t index = 0; index + 1 < knots.size(); ++index) {
        const AgeKnot& from = knots[index];
        const AgeKnot& to = knots[index + 1];
        if (layer <= to.layer) {
            const float span = to.layer - from.layer;
            const float t = span > 0.0f ? (layer - from.layer) / span : 0.0f;
            return lerp(from.age, to.age, t);
        }
    }
    return knots.back().age;
}

Vec3 sample(const std::vector<Vec3>& table, float t)
{
    const float position = std::clamp(t, 0.0f, 1.0f) * (table.size() - 1);
    const std::size_t index = static_cast<std::size_t>(position);
    const std::size_t next = std::min(index + 1, table.size() - 1);
    return lerp(table[index], table[next], position - index);
}

bool finite(Vec3 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

} // namespace

Scene::Scene(const Session& session)
{
    for (const VolumeSpec& volume : session.volumes)
        add_volume(volume, session.z_scale);
    for (const SurfaceSpec& surface : session.surfaces)
        add_surface(surface, session.z_scale);
    build_world(session);
}

void Scene::add_volume(const VolumeSpec& spec, float z_scale)
{
    const ImageData data = read_vti(spec.path);
    const DataArray* scalar = data.find(spec.scalar);
    if (scalar == nullptr)
        throw std::runtime_error(spec.path + ": no point array named '" + spec.scalar + "'");

    const ColorMap colormap
        = load_colormap(spec.colormap_path, "", LUT_SIZE, spec.trim);

    // Colour is baked once: entry i corresponds to layer_id lerped across the
    // value range, mapped through age(layer_id) into the age colour scale.
    const float age_lo = spec.age_knots.front().age;
    const float age_hi = spec.age_knots.back().age;
    std::vector<Vec3> colors(LUT_SIZE);
    for (int index = 0; index < LUT_SIZE; ++index) {
        const float layer = lerp(spec.value_range.lo,
            spec.value_range.hi,
            static_cast<float>(index) / (LUT_SIZE - 1));
        bool dated = true;
        const float age = age_at(spec.age_knots, layer, dated);
        colors[index] = dated
            ? sample(colormap.colors, (age - age_lo) / std::max(age_hi - age_lo, 1e-6f))
            : spec.undated_color;
    }

    // The transfer function and the volumetric model must be committed before
    // the Group is: Group::commit() asks each VolumetricModel for its Embree
    // geometry handle, and an uncommitted one leaves OSPRay spinning on garbage
    // bounds rather than reporting an error. apply_opacity() supplies the real
    // curve; this placeholder only makes the object valid.
    ospray::cpp::TransferFunction transfer("piecewiseLinear");
    transfer.setParam("color", ospray::cpp::CopiedData(colors));
    transfer.setParam("opacity", ospray::cpp::CopiedData(std::vector<float>(LUT_SIZE, 0.0f)));
    transfer.setParam("value", Vec2{spec.value_range.lo, spec.value_range.hi});
    transfer.commit();

    ospray::cpp::Volume volume("structuredRegular");
    volume.setParam("data",
        ospray::cpp::CopiedData(scalar->values.data(),
            Vec3ul{static_cast<unsigned long long>(data.dims[0]),
                static_cast<unsigned long long>(data.dims[1]),
                static_cast<unsigned long long>(data.dims[2])}));
    volume.setParam("gridOrigin",
        Vec3{static_cast<float>(data.origin[0]),
            static_cast<float>(data.origin[1]),
            static_cast<float>(data.origin[2] * z_scale)});
    volume.setParam("gridSpacing",
        Vec3{static_cast<float>(data.spacing[0]),
            static_cast<float>(data.spacing[1]),
            static_cast<float>(data.spacing[2] * z_scale)});
    volume.commit();

    ospray::cpp::VolumetricModel model(volume);
    model.setParam("transferFunction", transfer);
    model.setParam("densityScale", spec.density_scale);
    model.commit();

    const Vec3 lo{static_cast<float>(data.origin[0]),
        static_cast<float>(data.origin[1]),
        static_cast<float>(data.origin[2] * z_scale)};
    const Vec3 hi{lo.x + static_cast<float>(data.spacing[0] * (data.dims[0] - 1)),
        lo.y + static_cast<float>(data.spacing[1] * (data.dims[1] - 1)),
        lo.z + static_cast<float>(data.spacing[2] * z_scale * (data.dims[2] - 1))};
    if (!bounds_initialised_) {
        bounds_ = {lo, hi};
        bounds_initialised_ = true;
    } else {
        bounds_.lo = {std::min(bounds_.lo.x, lo.x),
            std::min(bounds_.lo.y, lo.y),
            std::min(bounds_.lo.z, lo.z)};
        bounds_.hi = {std::max(bounds_.hi.x, hi.x),
            std::max(bounds_.hi.y, hi.y),
            std::max(bounds_.hi.z, hi.z)};
    }

    volumes_.push_back({spec, transfer, model});
}

void Scene::add_surface(const SurfaceSpec& spec, float z_scale)
{
    const StructuredGrid grid = read_vts(spec.path);
    const DataArray* field = grid.find(spec.color_by);
    if (field == nullptr)
        throw std::runtime_error(
            spec.path + ": no point array named '" + spec.color_by + "'");

    const ColorMap colormap = load_colormap(spec.colormap_path, "", LUT_SIZE, spec.trim);

    std::vector<Vec3> positions(grid.points.size());
    std::vector<Vec4> colors(grid.points.size());
    const float span = std::max(spec.value_range.hi - spec.value_range.lo, 1e-6f);
    for (std::size_t index = 0; index < grid.points.size(); ++index) {
        const Vec3& point = grid.points[index];
        positions[index] = {point.x, point.y, point.z * z_scale};
        const Vec3 color
            = sample(colormap.colors, (field->values[index] - spec.value_range.lo) / span);
        colors[index] = {color.x, color.y, color.z, 1.0f};
    }

    // The layer surfaces are single-slice grids, so the mesh is the i-j lattice
    // triangulated in place. Cells touching a non-finite corner are dropped
    // rather than handed to Embree as degenerates.
    const int width = grid.dims[0];
    const int height = grid.dims[1];
    std::vector<Vec3ui> indices;
    indices.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 2);
    for (int row = 0; row + 1 < height; ++row) {
        for (int column = 0; column + 1 < width; ++column) {
            const unsigned int a = static_cast<unsigned int>(row * width + column);
            const unsigned int b = a + 1;
            const unsigned int c = a + width;
            const unsigned int d = c + 1;
            if (!finite(positions[a]) || !finite(positions[b]) || !finite(positions[c])
                || !finite(positions[d]))
                continue;
            indices.push_back({a, c, b});
            indices.push_back({b, c, d});
        }
    }
    if (indices.empty())
        throw std::runtime_error(spec.path + ": surface has no renderable cells");

    ospray::cpp::Geometry mesh("mesh");
    mesh.setParam("vertex.position", ospray::cpp::CopiedData(positions));
    mesh.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    mesh.setParam("index", ospray::cpp::CopiedData(indices));
    mesh.commit();

    ospray::cpp::Material material("obj");
    material.setParam("kd", Vec3{1.0f, 1.0f, 1.0f});
    material.setParam("ns", std::max(2.0f, 100.0f * (1.0f - spec.roughness)));
    material.commit();

    ospray::cpp::GeometricModel model(mesh);
    model.setParam("material", material);
    model.commit();

    Vec3 lo{std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};
    Vec3 hi{-lo.x, -lo.y, -lo.z};
    for (const Vec3& point : positions) {
        if (!finite(point))
            continue;
        lo = {std::min(lo.x, point.x), std::min(lo.y, point.y), std::min(lo.z, point.z)};
        hi = {std::max(hi.x, point.x), std::max(hi.y, point.y), std::max(hi.z, point.z)};
    }
    if (!bounds_initialised_) {
        bounds_ = {lo, hi};
        bounds_initialised_ = true;
    } else {
        bounds_.lo = {std::min(bounds_.lo.x, lo.x),
            std::min(bounds_.lo.y, lo.y),
            std::min(bounds_.lo.z, lo.z)};
        bounds_.hi = {std::max(bounds_.hi.x, hi.x),
            std::max(bounds_.hi.y, hi.y),
            std::max(bounds_.hi.z, hi.z)};
    }

    surfaces_.push_back({spec, material, model, mesh, field->values, colormap});
}

void Scene::build_world(const Session& session)
{
    for (const LightSpec& spec : session.lights) {
        ospray::cpp::Light light(spec.type);
        light.setParam("color", spec.color);
        light.setParam("intensity", spec.intensity);
        if (spec.type == "distant" || spec.type == "sunSky") {
            light.setParam("direction", spec.direction);
            if (spec.type == "distant")
                light.setParam("angularDiameter", spec.angular_diameter);
        }
        light.commit();
        lights_.push_back(light);
    }

    ospray::cpp::Group group;
    group_ = group;
    if (!volumes_.empty()) {
        std::vector<ospray::cpp::VolumetricModel> models;
        for (const VolumeEntry& entry : volumes_)
            models.push_back(entry.model);
        group.setParam("volume", ospray::cpp::CopiedData(models));
    }
    if (!surfaces_.empty()) {
        std::vector<ospray::cpp::GeometricModel> models;
        for (const SurfaceEntry& entry : surfaces_)
            models.push_back(entry.model);
        group.setParam("geometry", ospray::cpp::CopiedData(models));
    }
    group.commit();

    ospray::cpp::Instance instance(group);
    instance.commit();
    instance_ = instance;

    world_ = ospray::cpp::World();
    world_.setParam("instance", ospray::cpp::CopiedData(instance));
    if (!lights_.empty())
        world_.setParam("light", ospray::cpp::CopiedData(lights_));
    world_.commit();
}

void Scene::set_lights(const std::vector<LightSpec>& specs)
{
    lights_.clear();
    for (const LightSpec& spec : specs) {
        ospray::cpp::Light light(spec.type);
        light.setParam("color", spec.color);
        light.setParam("intensity", spec.intensity);
        if (spec.type == "distant" || spec.type == "sunSky") {
            light.setParam("direction", spec.direction);
            if (spec.type == "distant")
                light.setParam("angularDiameter", spec.angular_diameter);
        }
        light.commit();
        lights_.push_back(light);
    }
    if (lights_.empty())
        world_.removeParam("light");
    else
        world_.setParam("light", ospray::cpp::CopiedData(lights_));
    world_.commit();
}

void Scene::set_density_scale(std::size_t index, float density_scale)
{
    VolumeEntry& entry = volumes_[index];
    entry.spec.density_scale = density_scale;
    entry.model.setParam("densityScale", density_scale);
    entry.model.commit();
    world_.commit();
}

void Scene::set_surface_range(std::size_t index, Range range)
{
    SurfaceEntry& entry = surfaces_[index];
    entry.spec.value_range = range;

    const float span = std::max(range.hi - range.lo, 1e-6f);
    std::vector<Vec4> colors(entry.field.size());
    for (std::size_t point = 0; point < entry.field.size(); ++point) {
        const Vec3 color
            = sample(entry.colormap.colors, (entry.field[point] - range.lo) / span);
        colors[point] = {color.x, color.y, color.z, 1.0f};
    }
    entry.mesh.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    entry.mesh.commit();
    entry.model.commit();
    group_.commit();
    instance_.commit();
    world_.commit();
}

void Scene::apply_opacity(const OpacityCurve& curve)
{
    for (VolumeEntry& entry : volumes_) {
        std::vector<float> opacity(LUT_SIZE);
        for (int index = 0; index < LUT_SIZE; ++index) {
            const float layer = lerp(entry.spec.value_range.lo,
                entry.spec.value_range.hi,
                static_cast<float>(index) / (LUT_SIZE - 1));
            // layer_id is exactly 0 outside the resampled domain and exactly
            // [1, 5] inside it, with nothing in between, so the whole (0, 1)
            // gap is free space for the domain edge to fade through.
            opacity[index]
                = layer < 1.0f ? 0.0f : std::clamp(curve.at(layer), 0.0f, 1.0f);
        }
        entry.transfer.setParam("opacity", ospray::cpp::CopiedData(opacity));
        entry.transfer.commit();
        entry.model.commit();
    }

    for (SurfaceEntry& entry : surfaces_) {
        const float opacity = entry.spec.layer < 0.0f
            ? 1.0f
            : std::clamp(curve.at(entry.spec.layer), 0.0f, 1.0f);
        entry.material.setParam("d", opacity);
        entry.material.commit();
        entry.model.commit();
    }

    world_.commit();
}

} // namespace ospr
