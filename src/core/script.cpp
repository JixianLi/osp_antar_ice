#include "ospr/script.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace ospr {
namespace {

using nlohmann::json;

constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;

Vec3 read_vec3(const json& node, const std::string& where)
{
    if (!node.is_array() || node.size() != 3)
        throw std::runtime_error(where + ": expected an array of 3 numbers");
    return {node[0].get<float>(), node[1].get<float>(), node[2].get<float>()};
}

Range read_range(const json& node, const std::string& where)
{
    if (!node.is_array() || node.size() != 2)
        throw std::runtime_error(where + ": expected an array of 2 numbers");
    return {node[0].get<float>(), node[1].get<float>()};
}

ColorMapTrim read_trim(const json& node, const std::string& where)
{
    const Range range = read_range(node, where);
    return {range.lo, range.hi};
}

Ease read_ease(const std::string& name, const std::string& where)
{
    if (name == "linear")
        return Ease::Linear;
    if (name == "smooth")
        return Ease::Smooth;
    throw std::runtime_error(where + ": unknown ease '" + name + "'");
}

Camera read_camera(const json& node, const std::string& where)
{
    Camera camera;
    if (node.contains("position"))
        camera.position = read_vec3(node.at("position"), where + ".position");
    if (node.contains("target"))
        camera.target = read_vec3(node.at("target"), where + ".target");
    if (node.contains("up"))
        camera.up = read_vec3(node.at("up"), where + ".up");
    if (node.contains("fovy"))
        camera.fov_y_degrees = node.at("fovy").get<float>();
    return camera;
}

OpacityCurve read_opacity(const json& node, const std::string& where)
{
    if (!node.is_array())
        throw std::runtime_error(where + ": expected an array of [layer, opacity] pairs");
    OpacityCurve curve;
    for (std::size_t index = 0; index < node.size(); ++index) {
        const json& pair = node[index];
        if (!pair.is_array() || pair.size() != 2)
            throw std::runtime_error(
                where + "[" + std::to_string(index) + "]: expected [layer, opacity]");
        curve.points.push_back({pair[0].get<float>(), pair[1].get<float>()});
    }
    if (curve.points.size() < 2)
        throw std::runtime_error(where + ": need at least 2 control points");
    std::stable_sort(curve.points.begin(),
        curve.points.end(),
        [](const OpacityPoint& a, const OpacityPoint& b) { return a.layer < b.layer; });
    return curve;
}

// Paths in the script are written relative to the script, not to the process's
// working directory, so a scene can be rendered from anywhere.
std::string resolve(const std::filesystem::path& base, const std::string& value)
{
    const std::filesystem::path path(value);
    return path.is_absolute() ? value : (base / path).lexically_normal().string();
}

VolumeSpec read_volume(
    const json& node, const std::filesystem::path& base, const std::string& where)
{
    VolumeSpec volume;
    if (!node.contains("path"))
        throw std::runtime_error(where + ": missing 'path'");
    volume.path = resolve(base, node.at("path").get<std::string>());
    if (node.contains("scalar"))
        volume.scalar = node.at("scalar").get<std::string>();
    if (node.contains("range"))
        volume.value_range = read_range(node.at("range"), where + ".range");
    if (node.contains("density_scale"))
        volume.density_scale = node.at("density_scale").get<float>();

    if (!node.contains("color"))
        throw std::runtime_error(where + ": missing 'color'");
    const json& color = node.at("color");
    if (!color.contains("map"))
        throw std::runtime_error(where + ".color: missing 'map'");
    volume.colormap_path = resolve(base, color.at("map").get<std::string>());
    if (color.contains("trim"))
        volume.trim = read_trim(color.at("trim"), where + ".color.trim");
    if (color.contains("undated"))
        volume.undated_color = read_vec3(color.at("undated"), where + ".color.undated");

    if (!color.contains("age_knots"))
        throw std::runtime_error(where + ".color: missing 'age_knots'");
    for (const json& knot : color.at("age_knots")) {
        if (!knot.is_array() || knot.size() != 2)
            throw std::runtime_error(where + ".color.age_knots: expected [layer, age] pairs");
        volume.age_knots.push_back({knot[0].get<float>(), knot[1].get<float>()});
    }
    if (volume.age_knots.size() < 2)
        throw std::runtime_error(where + ".color.age_knots: need at least 2 knots");
    std::stable_sort(volume.age_knots.begin(),
        volume.age_knots.end(),
        [](const AgeKnot& a, const AgeKnot& b) { return a.layer < b.layer; });
    return volume;
}

SurfaceSpec read_surface(
    const json& node, const std::filesystem::path& base, const std::string& where)
{
    SurfaceSpec surface;
    if (!node.contains("path"))
        throw std::runtime_error(where + ": missing 'path'");
    surface.path = resolve(base, node.at("path").get<std::string>());
    if (node.contains("color_by"))
        surface.color_by = node.at("color_by").get<std::string>();
    if (!node.contains("map"))
        throw std::runtime_error(where + ": missing 'map'");
    surface.colormap_path = resolve(base, node.at("map").get<std::string>());
    if (node.contains("trim"))
        surface.trim = read_trim(node.at("trim"), where + ".trim");
    if (node.contains("range"))
        surface.value_range = read_range(node.at("range"), where + ".range");
    if (node.contains("layer"))
        surface.layer = node.at("layer").is_null() ? -1.0f : node.at("layer").get<float>();
    if (node.contains("roughness"))
        surface.roughness = node.at("roughness").get<float>();
    return surface;
}

LightSpec read_light(const json& node, const std::string& where)
{
    LightSpec light;
    if (node.contains("type"))
        light.type = node.at("type").get<std::string>();
    if (node.contains("direction"))
        light.direction = read_vec3(node.at("direction"), where + ".direction");
    if (node.contains("color"))
        light.color = read_vec3(node.at("color"), where + ".color");
    if (node.contains("intensity"))
        light.intensity = node.at("intensity").get<float>();
    if (node.contains("angular_diameter"))
        light.angular_diameter = node.at("angular_diameter").get<float>();
    if (node.contains("visible"))
        light.visible = node.at("visible").get<bool>();
    return light;
}

} // namespace

Script load_script(const std::string& path)
{
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot open script: " + path);

    json root;
    try {
        stream >> root;
    } catch (const json::parse_error& error) {
        throw std::runtime_error("malformed JSON in " + path + ": " + error.what());
    }

    const std::filesystem::path base
        = std::filesystem::absolute(std::filesystem::path(path)).parent_path();

    Script script;

    if (!root.contains("session"))
        throw std::runtime_error(path + ": missing 'session'");
    const json& session = root.at("session");

    if (session.contains("z_scale"))
        script.session.z_scale = session.at("z_scale").get<float>();

    if (session.contains("renderer")) {
        const json& renderer = session.at("renderer");
        if (renderer.contains("type"))
            script.session.renderer.type = renderer.at("type").get<std::string>();
        if (renderer.contains("spp"))
            script.session.renderer.samples_per_pixel = renderer.at("spp").get<int>();
        if (renderer.contains("denoise"))
            script.session.renderer.denoise = renderer.at("denoise").get<bool>();
        if (renderer.contains("background")) {
            const json& background = renderer.at("background");
            if (background.is_array()) {
                const Vec3 flat = read_vec3(background, "session.renderer.background");
                script.session.renderer.background_top = flat;
                script.session.renderer.background_bottom = flat;
            } else {
                if (background.contains("top"))
                    script.session.renderer.background_top
                        = read_vec3(background.at("top"), "session.renderer.background.top");
                if (background.contains("bottom"))
                    script.session.renderer.background_bottom = read_vec3(
                        background.at("bottom"), "session.renderer.background.bottom");
            }
        }
    }

    if (session.contains("objects")) {
        const json& objects = session.at("objects");
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const std::string where = "session.objects[" + std::to_string(index) + "]";
            const json& object = objects[index];
            if (!object.contains("type"))
                throw std::runtime_error(where + ": missing 'type'");
            const std::string type = object.at("type").get<std::string>();
            if (type == "volume")
                script.session.volumes.push_back(read_volume(object, base, where));
            else if (type == "surface")
                script.session.surfaces.push_back(read_surface(object, base, where));
            else if (type == "tetrahedron") {
                TetrahedronSpec tetrahedron;
                if (object.contains("scale"))
                    tetrahedron.scale = object.at("scale").get<float>();
                script.session.tetrahedra.push_back(tetrahedron);
            }
            else
                throw std::runtime_error(where + ": unknown object type '" + type + "'");
        }
    }

    if (session.contains("lights")) {
        const json& lights = session.at("lights");
        for (std::size_t index = 0; index < lights.size(); ++index)
            script.session.lights.push_back(
                read_light(lights[index], "session.lights[" + std::to_string(index) + "]"));
    }

    if (root.contains("output")) {
        const json& output = root.at("output");
        if (output.contains("dir"))
            script.output.directory = resolve(base, output.at("dir").get<std::string>());
        if (output.contains("width"))
            script.output.width = output.at("width").get<int>();
        if (output.contains("height"))
            script.output.height = output.at("height").get<int>();
    }

    if (root.contains("timeline")) {
        const json& timeline = root.at("timeline");
        if (timeline.contains("fps"))
            script.timeline.frames_per_second = timeline.at("fps").get<float>();
        if (timeline.contains("duration"))
            script.timeline.duration_seconds = timeline.at("duration").get<float>();
    }

    if (root.contains("camera")) {
        const json& camera = root.at("camera");
        if (camera.contains("mode") && camera.at("mode").get<std::string>() == "orbit") {
            script.orbit.enabled = true;
            if (camera.contains("center")) {
                script.orbit.center = read_vec3(camera.at("center"), "camera.center");
                script.orbit.has_center = true;
            }
            if (camera.contains("radius")) {
                script.orbit.radius = camera.at("radius").get<float>();
                script.orbit.has_radius = true;
            }
            if (camera.contains("elevation"))
                script.orbit.elevation_degrees = camera.at("elevation").get<float>();
            if (camera.contains("azimuth_start"))
                script.orbit.azimuth_start_degrees = camera.at("azimuth_start").get<float>();
            if (camera.contains("revolutions"))
                script.orbit.revolutions = camera.at("revolutions").get<float>();
            if (camera.contains("fovy"))
                script.orbit.fov_y_degrees = camera.at("fovy").get<float>();
            if (camera.contains("up"))
                script.orbit.up = read_vec3(camera.at("up"), "camera.up");
        }
    }

    if (!root.contains("keyframes") || !root.at("keyframes").is_array())
        throw std::runtime_error(path + ": missing 'keyframes' array");

    const json& keyframes = root.at("keyframes");
    for (std::size_t index = 0; index < keyframes.size(); ++index) {
        const std::string where = "keyframes[" + std::to_string(index) + "]";
        const json& node = keyframes[index];

        Keyframe keyframe;
        if (!node.contains("t"))
            throw std::runtime_error(where + ": missing 't'");
        keyframe.time_seconds = node.at("t").get<float>();
        if (node.contains("camera"))
            keyframe.camera = read_camera(node.at("camera"), where + ".camera");
        if (node.contains("opacity"))
            keyframe.opacity = read_opacity(node.at("opacity"), where + ".opacity");
        if (node.contains("ease"))
            keyframe.ease = read_ease(node.at("ease").get<std::string>(), where);
        script.keyframes.push_back(keyframe);
    }

    if (script.keyframes.empty())
        throw std::runtime_error(path + ": 'keyframes' is empty");

    std::stable_sort(script.keyframes.begin(),
        script.keyframes.end(),
        [](const Keyframe& a, const Keyframe& b) { return a.time_seconds < b.time_seconds; });

    if (script.output.width <= 0 || script.output.height <= 0)
        throw std::runtime_error(path + ": output width and height must be positive");
    if (script.timeline.frames_per_second <= 0.0f)
        throw std::runtime_error(path + ": timeline.fps must be positive");

    return script;
}

int frame_count(const Script& script)
{
    const float exact = script.timeline.duration_seconds * script.timeline.frames_per_second;
    return std::max(1, static_cast<int>(std::lround(exact)));
}

float frame_time(const Script& script, int frame_index)
{
    return static_cast<float>(frame_index) / script.timeline.frames_per_second;
}

Camera camera_for(const Script& script, float time_seconds)
{
    if (!script.orbit.enabled)
        return camera_at(script.keyframes, time_seconds);

    const OrbitSpec& orbit = script.orbit;
    const float duration = std::max(script.timeline.duration_seconds, 1e-6f);
    const float turns = orbit.revolutions * (time_seconds / duration);
    const float azimuth = (orbit.azimuth_start_degrees + 360.0f * turns) * DEGREES_TO_RADIANS;
    const float elevation = orbit.elevation_degrees * DEGREES_TO_RADIANS;

    Camera camera;
    camera.target = orbit.center;
    camera.position = {orbit.center.x + orbit.radius * std::cos(elevation) * std::cos(azimuth),
        orbit.center.y + orbit.radius * std::cos(elevation) * std::sin(azimuth),
        orbit.center.z + orbit.radius * std::sin(elevation)};
    camera.up = orbit.up;
    camera.fov_y_degrees = orbit.fov_y_degrees;
    return camera;
}

} // namespace ospr
