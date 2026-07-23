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

void frame_scene(OrbitSpec& orbit, const Bounds& bounds, float aspect)
{
    if (!orbit.has_center)
        orbit.center = bounds.center();
    if (orbit.has_radius)
        return;

    constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;
    const float tan_y = std::tan(
        std::max(orbit.fov_y_degrees * 0.5f * DEGREES_TO_RADIANS, 1e-3f));
    const float tan_x = tan_y * aspect;

    const Vec3 c = orbit.center;
    const Vec3 corner[8] = {{bounds.lo.x, bounds.lo.y, bounds.lo.z},
        {bounds.hi.x, bounds.lo.y, bounds.lo.z},
        {bounds.lo.x, bounds.hi.y, bounds.lo.z},
        {bounds.hi.x, bounds.hi.y, bounds.lo.z},
        {bounds.lo.x, bounds.lo.y, bounds.hi.z},
        {bounds.hi.x, bounds.lo.y, bounds.hi.z},
        {bounds.lo.x, bounds.hi.y, bounds.hi.z},
        {bounds.hi.x, bounds.hi.y, bounds.hi.z}};

    const float elevation = orbit.elevation_degrees * DEGREES_TO_RADIANS;
    float required = 0.0f;

    // The slab's silhouette changes as it turns, so sweep the orbit and keep the
    // radius that contains the widest view; a fixed orbit must fit its worst case.
    for (int step = 0; step < 72; ++step) {
        const float azimuth = static_cast<float>(step) * 5.0f * DEGREES_TO_RADIANS;
        const Vec3 direction{std::cos(elevation) * std::cos(azimuth),
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation)};
        const Vec3 forward = direction * -1.0f;
        Vec3 right = cross(forward, orbit.up);
        if (length(right) < 1e-6f)
            right = {1.0f, 0.0f, 0.0f};
        right = normalize(right);
        const Vec3 up = normalize(cross(right, forward));

        for (const Vec3& point : corner) {
            const Vec3 rel = point - c;
            const float horizontal = std::abs(dot(rel, right));
            const float vertical = std::abs(dot(rel, up));
            required = std::max(required,
                std::max(horizontal / tan_x, vertical / tan_y));
        }
    }
    orbit.radius = 1.15f * required;
}

namespace {

void grow(Bounds& bounds, bool& initialised, Vec3 lo, Vec3 hi)
{
    if (!initialised) {
        bounds = {lo, hi};
        initialised = true;
        return;
    }
    bounds.lo = {std::min(bounds.lo.x, lo.x),
        std::min(bounds.lo.y, lo.y),
        std::min(bounds.lo.z, lo.z)};
    bounds.hi = {std::max(bounds.hi.x, hi.x),
        std::max(bounds.hi.y, hi.y),
        std::max(bounds.hi.z, hi.z)};
}

// z_scale is applied here, so bounds and every built object share one convention:
// exaggerate depth first, then normalise the exaggerated scene.
Vec3 volume_corner(const ImageData& data, float z_scale, int side)
{
    return {static_cast<float>(data.origin[0] + (side ? data.spacing[0] * (data.dims[0] - 1) : 0.0)),
        static_cast<float>(data.origin[1] + (side ? data.spacing[1] * (data.dims[1] - 1) : 0.0)),
        static_cast<float>(
            (data.origin[2] + (side ? data.spacing[2] * (data.dims[2] - 1) : 0.0)) * z_scale)};
}

} // namespace

Scene::Scene(const Session& session)
{
    // Load everything first so the combined bounds are known before any object
    // is built: the normalisation depends on the whole scene's extent.
    std::vector<ImageData> volume_data;
    std::vector<StructuredGrid> surface_data;
    volume_data.reserve(session.volumes.size());
    surface_data.reserve(session.surfaces.size());

    Bounds raw;
    bool initialised = false;

    for (const VolumeSpec& spec : session.volumes) {
        ImageData data = read_vti(spec.path);
        if (data.find(spec.scalar) == nullptr)
            throw std::runtime_error(
                spec.path + ": no point array named '" + spec.scalar + "'");
        grow(raw, initialised, volume_corner(data, session.z_scale, 0),
            volume_corner(data, session.z_scale, 1));
        volume_data.push_back(std::move(data));
    }

    for (const SurfaceSpec& spec : session.surfaces) {
        StructuredGrid grid = read_vts(spec.path);
        if (grid.find(spec.color_by) == nullptr)
            throw std::runtime_error(
                spec.path + ": no point array named '" + spec.color_by + "'");
        Vec3 lo{std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity()};
        Vec3 hi{-lo.x, -lo.y, -lo.z};
        for (const Vec3& point : grid.points) {
            const Vec3 scaled{point.x, point.y, point.z * session.z_scale};
            if (!finite(scaled))
                continue;
            lo = {std::min(lo.x, scaled.x), std::min(lo.y, scaled.y), std::min(lo.z, scaled.z)};
            hi = {std::max(hi.x, scaled.x), std::max(hi.y, scaled.y), std::max(hi.z, scaled.z)};
        }
        grow(raw, initialised, lo, hi);
        surface_data.push_back(std::move(grid));
    }

    for (const TetrahedronSpec& spec : session.tetrahedra)
        grow(raw, initialised, {-spec.scale, -spec.scale, -spec.scale},
            {spec.scale, spec.scale, spec.scale});

    center_ = initialised ? raw.center() : Vec3{};
    const float longest = std::max(
        {raw.hi.x - raw.lo.x, raw.hi.y - raw.lo.y, raw.hi.z - raw.lo.z});
    scale_ = (initialised && longest > 0.0f) ? 2.0f / longest : 1.0f;
    z_scale_ = session.z_scale;
    bounds_ = {to_normalized(raw.lo), to_normalized(raw.hi)};

    for (std::size_t index = 0; index < session.volumes.size(); ++index)
        add_volume(volume_data[index], session.volumes[index], session.z_scale);
    for (std::size_t index = 0; index < session.surfaces.size(); ++index)
        add_surface(surface_data[index], session.surfaces[index], session.z_scale);
    for (const TetrahedronSpec& tetrahedron : session.tetrahedra)
        add_tetrahedron(tetrahedron);
    build_world(session);
}

namespace {

std::size_t voxel_index(const int dims[3], int i, int j, int k)
{
    return static_cast<std::size_t>(i) + dims[0] * (static_cast<std::size_t>(j) + dims[1] * k);
}

// k=0 is the grid floor (deepest). Within each i-j column the valid layer_id
// runs from the bed (lowest k with data) up to the surface; below the bed it is
// zero. Copy the bed's value down through that empty span so the column bottoms
// out on solid rock at the floor.
std::vector<float> fill_base_below_bed(std::vector<float> values, const int dims[3])
{
    const int nz = dims[2];
    for (int j = 0; j < dims[1]; ++j) {
        for (int i = 0; i < dims[0]; ++i) {
            int bed = -1;
            for (int k = 0; k < nz; ++k) {
                if (values[voxel_index(dims, i, j, k)] > 0.5f) {
                    bed = k;
                    break;
                }
            }
            if (bed <= 0)
                continue;
            const float rock = values[voxel_index(dims, i, j, bed)];
            for (int k = 0; k < bed; ++k)
                values[voxel_index(dims, i, j, k)] = rock;
        }
    }
    return values;
}

// The isochrone bands are physically thin and uneven, so during the peel some
// layers barely appear. Blend each column's layer_id toward a version that is
// linear in voxel height, which -- the grid being uniform in z -- makes every
// band an equal real thickness. factor 0 keeps true depths, 1 equalises fully.
// This moves isochrone depths: a deliberate stylisation, like the z exaggeration.
std::vector<float> equalize_layer_thickness(
    std::vector<float> values, const int dims[3], float factor)
{
    const int nz = dims[2];
    for (int j = 0; j < dims[1]; ++j) {
        for (int i = 0; i < dims[0]; ++i) {
            int bed = -1;
            int surface = -1;
            for (int k = 0; k < nz; ++k) {
                if (values[voxel_index(dims, i, j, k)] > 0.5f) {
                    if (bed < 0)
                        bed = k;
                    surface = k;
                }
            }
            if (bed < 0 || surface <= bed)
                continue;
            const float span = static_cast<float>(surface - bed);
            for (int k = bed; k <= surface; ++k) {
                const float height = static_cast<float>(k - bed) / span; // 0 bed, 1 surface
                const float equalized = 5.0f - 4.0f * height;            // 5 bed .. 1 surface
                const std::size_t index = voxel_index(dims, i, j, k);
                values[index] = lerp(values[index], equalized, factor);
            }
        }
    }
    return values;
}

// Equalise the ice bands first (on the true column), then extend the bed to the
// floor. The order matters: fill_base reads the deepest valid value as rock.
std::vector<float> process_scalar(
    std::vector<float> values, const int dims[3], float equalize, bool fill_base)
{
    if (equalize > 0.0f)
        values = equalize_layer_thickness(std::move(values), dims, equalize);
    if (fill_base)
        values = fill_base_below_bed(std::move(values), dims);
    return values;
}

} // namespace

void Scene::add_volume(const ImageData& data, const VolumeSpec& spec, float z_scale)
{
    const DataArray* scalar = data.find(spec.scalar);
    // Equalise the ice bands first (operates on the true column), then extend the
    // bed to the floor.
    const std::vector<float> filled
        = process_scalar(scalar->values, data.dims, spec.layer_equalize, spec.fill_base);

    const ColorMap ice = load_colormap(spec.ice_colormap_path, "", LUT_SIZE, spec.ice_trim);
    const ColorMap rock = load_colormap(spec.rock_colormap_path, "", LUT_SIZE, spec.rock_trim);

    // Colour is by layer_id directly: entry i is the layer_id it will be sampled
    // at, mapped through the ice ramp over [1, split] and the rock ramp over
    // [split, 5]. No age conversion.
    std::vector<Vec3> colors(LUT_SIZE);
    for (int index = 0; index < LUT_SIZE; ++index) {
        const float layer = lerp(spec.value_range.lo,
            spec.value_range.hi,
            static_cast<float>(index) / (LUT_SIZE - 1));
        colors[index] = layer <= spec.split
            ? sample(ice.colors, (layer - 1.0f) / std::max(spec.split - 1.0f, 1e-6f))
            : sample(rock.colors, (layer - spec.split) / std::max(5.0f - spec.split, 1e-6f));
    }

    // The transfer function and the volumetric model must be committed before
    // the Group is: Group::commit() asks each VolumetricModel for its Embree
    // geometry handle, and an uncommitted one leaves OSPRay spinning on garbage
    // bounds rather than reporting an error. apply_opacity() supplies the real
    // curve; this placeholder only makes the object valid.
    ospray::cpp::TransferFunction transfer("piecewiseLinear");
    transfer.setParam("color", ospray::cpp::CopiedData(colors));
    transfer.setParam("opacity", ospray::cpp::CopiedData(std::vector<float>(LUT_SIZE, 0.0f)));
    transfer.setParam("value", Box1f{spec.value_range.lo, spec.value_range.hi});
    transfer.commit();

    ospray::cpp::Volume volume("structuredRegular");
    volume.setParam("data",
        ospray::cpp::CopiedData(filled.data(),
            Vec3ul{static_cast<unsigned long long>(data.dims[0]),
                static_cast<unsigned long long>(data.dims[1]),
                static_cast<unsigned long long>(data.dims[2])}));
    // gridOrigin is a point (centre-subtract then scale); gridSpacing is a step
    // vector (scale only). A voxel step in normalised space is the metre step
    // times scale_.
    const Vec3 grid_origin = to_normalized({static_cast<float>(data.origin[0]),
        static_cast<float>(data.origin[1]),
        static_cast<float>(data.origin[2] * z_scale)});
    const Vec3 grid_spacing = Vec3{static_cast<float>(data.spacing[0]),
                                 static_cast<float>(data.spacing[1]),
                                 static_cast<float>(data.spacing[2] * z_scale)}
        * scale_;
    volume.setParam("gridOrigin", grid_origin);
    volume.setParam("gridSpacing", grid_spacing);
    volume.commit();

    ospray::cpp::VolumetricModel model(volume);
    model.setParam("transferFunction", transfer);
    model.setParam("densityScale", spec.density_scale);
    model.commit();

    VolumeEntry entry{spec, transfer, model, volume, grid_origin, grid_spacing};
    entry.source_scalar = scalar->values;
    entry.dims[0] = data.dims[0];
    entry.dims[1] = data.dims[1];
    entry.dims[2] = data.dims[2];
    entry.fill_base = spec.fill_base;
    entry.layer_equalize = spec.layer_equalize;
    volumes_.push_back(std::move(entry));
}

void Scene::add_surface(const StructuredGrid& grid, const SurfaceSpec& spec, float z_scale)
{
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
        positions[index] = to_normalized({point.x, point.y, point.z * z_scale});
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

    surfaces_.push_back({spec, material, model, mesh, field->values, colormap, positions});
}

// Regular tetrahedron on alternating corners of the cube, one colour per vertex
// so the result shows barycentric interpolation. Windings are counter-clockwise
// seen from outside.
void Scene::add_tetrahedron(const TetrahedronSpec& spec)
{
    const float s = spec.scale;
    const std::vector<Vec3> positions{to_normalized({s, s, s}),
        to_normalized({s, -s, -s}),
        to_normalized({-s, s, -s}),
        to_normalized({-s, -s, s})};
    const std::vector<Vec4> colors{{0.90f, 0.15f, 0.15f, 1.0f},
        {0.15f, 0.80f, 0.25f, 1.0f},
        {0.20f, 0.35f, 0.95f, 1.0f},
        {0.95f, 0.85f, 0.15f, 1.0f}};
    const std::vector<Vec3ui> indices{{0, 1, 2}, {0, 2, 3}, {0, 3, 1}, {1, 3, 2}};

    ospray::cpp::Geometry mesh("mesh");
    mesh.setParam("vertex.position", ospray::cpp::CopiedData(positions));
    mesh.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    mesh.setParam("index", ospray::cpp::CopiedData(indices));
    mesh.commit();

    ospray::cpp::Material material("obj");
    material.setParam("kd", Vec3{1.0f, 1.0f, 1.0f});
    material.commit();

    ospray::cpp::GeometricModel model(mesh);
    model.setParam("material", material);
    model.commit();

    SurfaceSpec spec_placeholder;
    spec_placeholder.layer = -1.0f;
    surfaces_.push_back({spec_placeholder, material, model, mesh, {}, ColorMap{}, positions});
}

void Scene::build_world(const Session& session)
{
    for (const LightSpec& spec : session.lights) {
        ospray::cpp::Light light(spec.type);
        light.setParam("color", spec.color);
        light.setParam("intensity", spec.intensity);
        light.setParam("visible", spec.visible);
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
        light.setParam("visible", spec.visible);
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

void Scene::set_z_scale(float z_scale)
{
    if (z_scale == z_scale_ || z_scale_ <= 0.0f)
        return;
    const float ratio = z_scale / z_scale_;
    z_scale_ = z_scale;

    for (VolumeEntry& entry : volumes_) {
        entry.grid_origin.z *= ratio;
        entry.grid_spacing.z *= ratio;
        entry.volume.setParam("gridOrigin", entry.grid_origin);
        entry.volume.setParam("gridSpacing", entry.grid_spacing);
        entry.volume.commit();
        entry.model.commit();
    }

    for (SurfaceEntry& entry : surfaces_) {
        for (Vec3& point : entry.positions)
            point.z *= ratio;
        entry.mesh.setParam("vertex.position", ospray::cpp::CopiedData(entry.positions));
        entry.mesh.commit();
        entry.model.commit();
    }

    bounds_.lo.z *= ratio;
    bounds_.hi.z *= ratio;

    group_.commit();
    instance_.commit();
    world_.commit();
}

float Scene::layer_equalize() const
{
    return volumes_.empty() ? 0.0f : volumes_.front().layer_equalize;
}

void Scene::set_layer_equalize(float factor)
{
    for (VolumeEntry& entry : volumes_) {
        entry.layer_equalize = factor;
        const std::vector<float> values = process_scalar(
            entry.source_scalar, entry.dims, factor, entry.fill_base);
        entry.volume.setParam("data",
            ospray::cpp::CopiedData(values.data(),
                Vec3ul{static_cast<unsigned long long>(entry.dims[0]),
                    static_cast<unsigned long long>(entry.dims[1]),
                    static_cast<unsigned long long>(entry.dims[2])}));
        entry.volume.commit();
        entry.model.commit();
    }
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
