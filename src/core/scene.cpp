#include "ospr/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ospr/colormap.h"
#include "ospr/image.h"
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

void grow(Bounds& bounds, bool& initialized, Vec3 lo, Vec3 hi)
{
    if (!initialized) {
        bounds = {lo, hi};
        initialized = true;
        return;
    }
    bounds.lo = {std::min(bounds.lo.x, lo.x),
        std::min(bounds.lo.y, lo.y),
        std::min(bounds.lo.z, lo.z)};
    bounds.hi = {std::max(bounds.hi.x, hi.x),
        std::max(bounds.hi.y, hi.y),
        std::max(bounds.hi.z, hi.z)};
}

// No vertical exaggeration: the data already carries whatever transform it needs,
// so bounds and every built object treat z exactly like x and y.
Vec3 volume_corner(const ImageData& data, int side)
{
    return {static_cast<float>(data.origin[0] + (side ? data.spacing[0] * (data.dims[0] - 1) : 0.0)),
        static_cast<float>(data.origin[1] + (side ? data.spacing[1] * (data.dims[1] - 1) : 0.0)),
        static_cast<float>(data.origin[2] + (side ? data.spacing[2] * (data.dims[2] - 1) : 0.0))};
}

} // namespace

Scene::Scene(const Session& session)
{
    // Load everything first so the combined bounds are known before any object
    // is built: the normalization depends on the whole scene's extent.
    std::vector<ImageData> volume_data;
    std::vector<StructuredGrid> surface_data;
    std::vector<PolyLines> curve_data;
    std::vector<StructuredGrid> flag_data;
    volume_data.reserve(session.volumes.size());
    surface_data.reserve(session.surfaces.size());
    curve_data.reserve(session.curves.size());
    flag_data.reserve(session.flags.size());

    Bounds raw;
    bool initialized = false;

    for (const VolumeSpec& spec : session.volumes) {
        ImageData data = read_vti(spec.path);
        if (data.find(spec.scalar) == nullptr)
            throw std::runtime_error(
                spec.path + ": no point array named '" + spec.scalar + "'");
        grow(raw, initialized, volume_corner(data, 0), volume_corner(data, 1));
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
            const Vec3 scaled{point.x, point.y, point.z};
            if (!finite(scaled))
                continue;
            lo = {std::min(lo.x, scaled.x), std::min(lo.y, scaled.y), std::min(lo.z, scaled.z)};
            hi = {std::max(hi.x, scaled.x), std::max(hi.y, scaled.y), std::max(hi.z, scaled.z)};
        }
        grow(raw, initialized, lo, hi);
        surface_data.push_back(std::move(grid));
    }

    for (const CurveSpec& spec : session.curves) {
        PolyLines lines = read_vtp(spec.path);
        for (const Vec3& point : lines.points)
            if (finite(point))
                grow(raw, initialized, point, point);
        curve_data.push_back(std::move(lines));
    }

    for (const FlagSpec& spec : session.flags) {
        StructuredGrid grid = read_vts(spec.path);
        if (grid.find("texcoord") == nullptr)
            throw std::runtime_error(spec.path + ": flag needs a 'texcoord' point array");
        for (const Vec3& point : grid.points)
            if (finite(point))
                grow(raw, initialized, point, point);
        flag_data.push_back(std::move(grid));
    }

    for (const TetrahedronSpec& spec : session.tetrahedra)
        grow(raw, initialized, {-spec.scale, -spec.scale, -spec.scale},
            {spec.scale, spec.scale, spec.scale});

    center_ = initialized ? raw.center() : Vec3{};
    const float longest = std::max(
        {raw.hi.x - raw.lo.x, raw.hi.y - raw.lo.y, raw.hi.z - raw.lo.z});
    scale_ = (initialized && longest > 0.0f) ? 2.0f / longest : 1.0f;
    bounds_ = {to_normalized(raw.lo), to_normalized(raw.hi)};

    for (std::size_t index = 0; index < session.volumes.size(); ++index)
        add_volume(volume_data[index], session.volumes[index]);
    for (std::size_t index = 0; index < session.surfaces.size(); ++index)
        add_surface(surface_data[index], session.surfaces[index]);
    for (std::size_t index = 0; index < session.curves.size(); ++index)
        add_curve(curve_data[index], session.curves[index]);
    for (std::size_t index = 0; index < session.flags.size(); ++index)
        add_flag(flag_data[index], session.flags[index]);
    for (const TetrahedronSpec& tetrahedron : session.tetrahedra)
        add_tetrahedron(tetrahedron);
    build_world(session);
}

namespace {

// One color per layer. Entry i is colored by the layer_id it samples, rounded
// to the nearest surface (1..5), so each color owns the material within half a
// layer of its surface and the boundaries fall at the midpoints. All five appear
// even though the data stops short of 5 at voxel centers, because 4.5..5 still
// rounds to 5.
std::vector<Vec3> build_flat_lut(
    const std::array<Vec3, LAYER_COUNT>& layer_colors, Range value_range)
{
    std::vector<Vec3> colors(LUT_SIZE);
    for (int index = 0; index < LUT_SIZE; ++index) {
        const float value = lerp(value_range.lo,
            value_range.hi,
            static_cast<float>(index) / (LUT_SIZE - 1));
        const int layer = std::clamp(static_cast<int>(std::lround(value)), 1, LAYER_COUNT);
        colors[index] = layer_colors[layer - 1];
    }
    return colors;
}

} // namespace

void Scene::add_volume(const ImageData& data, const VolumeSpec& spec)
{
    const DataArray* scalar = data.find(spec.scalar);
    const std::vector<Vec3> colors = build_flat_lut(spec.layer_colors, spec.value_range);

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
        ospray::cpp::CopiedData(scalar->values.data(),
            Vec3ul{static_cast<unsigned long long>(data.dims[0]),
                static_cast<unsigned long long>(data.dims[1]),
                static_cast<unsigned long long>(data.dims[2])}));
    // gridOrigin is a point (center-subtract then scale); gridSpacing is a step
    // vector (scale only). No vertical exaggeration: z is treated like x and y,
    // so the data must carry any transform it needs before it reaches here.
    const Vec3 grid_origin = to_normalized({static_cast<float>(data.origin[0]),
        static_cast<float>(data.origin[1]), static_cast<float>(data.origin[2])});
    const Vec3 grid_spacing = Vec3{static_cast<float>(data.spacing[0]),
                                 static_cast<float>(data.spacing[1]),
                                 static_cast<float>(data.spacing[2])}
        * scale_;
    volume.setParam("gridOrigin", grid_origin);
    volume.setParam("gridSpacing", grid_spacing);
    volume.commit();

    ospray::cpp::VolumetricModel model(volume);
    model.setParam("transferFunction", transfer);
    model.setParam("densityScale", spec.density_scale);
    model.commit();

    bounds_.lo.z = std::min(bounds_.lo.z, grid_origin.z);
    volumes_.push_back(VolumeEntry{spec, transfer, model, volume});
}

void Scene::add_surface(const StructuredGrid& grid, const SurfaceSpec& spec)
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
        positions[index] = to_normalized({point.x, point.y, point.z});
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

    surfaces_.push_back({spec, material, model, mesh, field->values, colormap});
}

void Scene::add_curve(const PolyLines& lines, const CurveSpec& spec)
{
    const DataArray* color = lines.find("color");
    const bool per_vertex_color = color != nullptr && color->components == 3;

    // OSPRay's linear curve segment i runs from vertex index[i] to index[i]+1 --
    // it connects *consecutive positions*, not arbitrary connectivity. So each
    // polyline's points are re-emitted contiguously in traversal order, and index
    // holds the segment starts within that layout (all but each polyline's last).
    std::vector<Vec3> positions;
    std::vector<Vec4> colors;
    std::vector<unsigned int> indices;
    for (const std::vector<unsigned int>& line : lines.lines) {
        const unsigned int base = static_cast<unsigned int>(positions.size());
        for (const unsigned int vertex : line) {
            positions.push_back(to_normalized(lines.points[vertex]));
            if (per_vertex_color)
                colors.push_back({color->values[vertex * 3], color->values[vertex * 3 + 1],
                    color->values[vertex * 3 + 2], 1.0f});
        }
        for (std::size_t point = 0; point + 1 < line.size(); ++point)
            indices.push_back(base + static_cast<unsigned int>(point));
    }
    if (indices.empty())
        throw std::runtime_error(spec.path + ": curve has no segments");

    ospray::cpp::Geometry curve("curve");
    curve.setParam("vertex.position", ospray::cpp::CopiedData(positions));
    if (per_vertex_color)
        curve.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    curve.setParam("index", ospray::cpp::CopiedData(indices));
    curve.setParam("radius", spec.radius);
    curve.setParam("type", static_cast<unsigned char>(OSP_ROUND));
    curve.setParam("basis", static_cast<unsigned char>(OSP_LINEAR));
    curve.commit();

    // With per-vertex color a white base lets the vertex color through; without
    // it the flat spec color is the tube's albedo.
    ospray::cpp::Material material("obj");
    material.setParam("kd", per_vertex_color ? Vec3{1.0f, 1.0f, 1.0f} : spec.color);
    material.setParam("ns", std::max(2.0f, 100.0f * (1.0f - spec.roughness)));
    material.commit();

    ospray::cpp::GeometricModel model(curve);
    model.setParam("material", material);
    model.commit();

    curves_.push_back({spec, material, model});
}

namespace {

float srgb_to_linear(float channel)
{
    return channel <= 0.04045f ? channel / 12.92f
                               : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void Scene::add_flag(const StructuredGrid& grid, const FlagSpec& spec)
{
    const DataArray* texcoord = grid.find("texcoord");
    if (texcoord == nullptr || texcoord->components != 2)
        throw std::runtime_error(spec.path + ": flag needs a 2-component 'texcoord' array");

    std::vector<Vec3> positions(grid.points.size());
    std::vector<Vec2> uv(grid.points.size());
    for (std::size_t index = 0; index < grid.points.size(); ++index) {
        positions[index] = to_normalized(grid.points[index]);
        uv[index] = {texcoord->values[index * 2], texcoord->values[index * 2 + 1]};
    }

    const int width = grid.dims[0];
    const int height = grid.dims[1];
    std::vector<Vec3ui> indices;
    for (int row = 0; row + 1 < height; ++row)
        for (int column = 0; column + 1 < width; ++column) {
            const unsigned int a = static_cast<unsigned int>(row * width + column);
            const unsigned int b = a + 1;
            const unsigned int c = a + width;
            const unsigned int d = c + 1;
            indices.push_back({a, c, b});
            indices.push_back({b, c, d});
        }
    if (indices.empty())
        throw std::runtime_error(spec.path + ": flag has no renderable cells");

    ospray::cpp::Geometry mesh("mesh");
    mesh.setParam("vertex.position", ospray::cpp::CopiedData(positions));
    mesh.setParam("vertex.texcoord", ospray::cpp::CopiedData(uv));
    mesh.setParam("index", ospray::cpp::CopiedData(indices));
    mesh.commit();

    // The logo file is sRGB; upload it as linear float so the path tracer lights
    // it correctly and the framebuffer's sRGB encode restores it on the way out.
    const Image image = read_image(spec.texture);
    std::vector<Vec3> texels(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t index = 0; index < texels.size(); ++index)
        texels[index] = {srgb_to_linear(image.rgba[index * 4] / 255.0f),
            srgb_to_linear(image.rgba[index * 4 + 1] / 255.0f),
            srgb_to_linear(image.rgba[index * 4 + 2] / 255.0f)};

    ospray::cpp::Texture texture("texture2d");
    texture.setParam("format", OSP_TEXTURE_RGB32F);
    texture.setParam("filter", OSP_TEXTURE_FILTER_LINEAR);
    texture.setParam("data",
        ospray::cpp::CopiedData(texels.data(),
            Vec2ul{static_cast<unsigned long long>(image.width),
                static_cast<unsigned long long>(image.height)}));
    texture.commit();

    ospray::cpp::Material material("obj");
    material.setParam("map_kd", texture);
    material.commit();

    ospray::cpp::GeometricModel model(mesh);
    model.setParam("material", material);
    model.commit();

    if (flag_models_.empty())
        flag_pivot_ = to_normalized({spec.pole_x, spec.pole_y, 0.0f});
    flag_models_.push_back(model);
}

// Regular tetrahedron on alternating corners of the cube, one color per vertex
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
    surfaces_.push_back({spec_placeholder, material, model, mesh, {}, ColorMap{}});
}

namespace {

// distant is OSPRay's directional light; sunSky also takes a direction. Off
// lights never reach here -- build_world and set_lights skip them.
ospray::cpp::Light make_light(const LightSpec& spec)
{
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
    return light;
}

} // namespace

void Scene::build_world(const Session& session)
{
    for (const LightSpec& spec : session.lights)
        if (spec.enabled)
            lights_.push_back(make_light(spec));

    ospray::cpp::Group group;
    group_ = group;
    if (!volumes_.empty()) {
        std::vector<ospray::cpp::VolumetricModel> models;
        for (const VolumeEntry& entry : volumes_)
            models.push_back(entry.model);
        group.setParam("volume", ospray::cpp::CopiedData(models));
    }
    // Surfaces and curve tubes are both geometry in the static instance.
    std::vector<ospray::cpp::GeometricModel> geometry;
    for (const SurfaceEntry& entry : surfaces_)
        geometry.push_back(entry.model);
    for (const CurveEntry& entry : curves_)
        geometry.push_back(entry.model);
    if (!geometry.empty())
        group.setParam("geometry", ospray::cpp::CopiedData(geometry));
    group.commit();

    ospray::cpp::Instance instance(group);
    instance.commit();
    instance_ = instance;

    std::vector<ospray::cpp::Instance> instances{instance_};
    has_flag_ = !flag_models_.empty();
    if (has_flag_) {
        ospray::cpp::Group flag_group;
        flag_group.setParam("geometry", ospray::cpp::CopiedData(flag_models_));
        flag_group.commit();
        flag_instance_ = ospray::cpp::Instance(flag_group);
        flag_instance_.commit();
        instances.push_back(flag_instance_);
    }

    world_ = ospray::cpp::World();
    world_.setParam("instance", ospray::cpp::CopiedData(instances));
    if (!lights_.empty())
        world_.setParam("light", ospray::cpp::CopiedData(lights_));
    world_.commit();
}

void Scene::orient_flag(const Camera& camera)
{
    if (!has_flag_)
        return;
    // The baked flag faces +x; rotate it by the angle that turns +x onto the
    // horizontal pole-to-camera direction, about the vertical axis at the pole.
    const float angle = std::atan2(
        camera.position.y - flag_pivot_.y, camera.position.x - flag_pivot_.x);
    flag_instance_.setParam("transform", rotate_about_z(angle, flag_pivot_));
    flag_instance_.commit();
    world_.commit();
}

void Scene::set_lights(const std::vector<LightSpec>& specs)
{
    lights_.clear();
    for (const LightSpec& spec : specs)
        if (spec.enabled)
            lights_.push_back(make_light(spec));
    if (lights_.empty())
        world_.removeParam("light");
    else
        world_.setParam("light", ospray::cpp::CopiedData(lights_));
    world_.commit();
}

// Color lives entirely in the transfer function LUT keyed by layer_id, so a
// recolor is a 256-entry rebuild and a re-commit, not a volume rebuild.
void Scene::set_flat_colors(
    std::size_t index, const std::array<Vec3, LAYER_COUNT>& layer_colors)
{
    VolumeEntry& entry = volumes_[index];
    const std::vector<Vec3> colors = build_flat_lut(layer_colors, entry.spec.value_range);
    entry.transfer.setParam("color", ospray::cpp::CopiedData(colors));
    entry.transfer.commit();
    entry.model.commit();
    entry.spec.layer_colors = layer_colors;
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

    for (CurveEntry& entry : curves_) {
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
