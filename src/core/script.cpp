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
        if (timeline.contains("frames_between"))
            script.frames_between = timeline.at("frames_between").get<int>();
        if (timeline.contains("up"))
            script.up = read_vec3(timeline.at("up"), "timeline.up");
    }

    if (!root.contains("keyframes") || !root.at("keyframes").is_array())
        throw std::runtime_error(path + ": missing 'keyframes' array");

    const json& keyframes = root.at("keyframes");
    for (std::size_t index = 0; index < keyframes.size(); ++index) {
        const std::string where = "keyframes[" + std::to_string(index) + "]";
        const json& node = keyframes[index];

        Keyframe keyframe;
        if (node.contains("azimuth"))
            keyframe.azimuth_degrees = node.at("azimuth").get<float>();
        if (node.contains("elevation"))
            keyframe.elevation_degrees = node.at("elevation").get<float>();
        if (node.contains("radius"))
            keyframe.radius = node.at("radius").get<float>();
        if (node.contains("fov"))
            keyframe.fov_y_degrees = node.at("fov").get<float>();
        if (node.contains("opacity"))
            keyframe.opacity = read_opacity(node.at("opacity"), where + ".opacity");
        if (node.contains("ease"))
            keyframe.ease = read_ease(node.at("ease").get<std::string>(), where);
        if (node.contains("frames_after"))
            keyframe.frames_after = node.at("frames_after").get<int>();
        script.keyframes.push_back(keyframe);
    }

    if (script.keyframes.empty())
        throw std::runtime_error(path + ": 'keyframes' is empty");
    if (script.frames_between < 0)
        throw std::runtime_error(path + ": timeline.frames_between must be >= 0");
    if (script.output.width <= 0 || script.output.height <= 0)
        throw std::runtime_error(path + ": output width and height must be positive");

    return script;
}

namespace {

int frames_in_gap(const Script& script, std::size_t gap)
{
    const int override_frames = script.keyframes[gap].frames_after;
    return override_frames >= 0 ? override_frames : script.frames_between;
}

} // namespace

int frame_count(const Script& script)
{
    if (script.keyframes.size() <= 1)
        return static_cast<int>(script.keyframes.size());
    int total = 1;
    for (std::size_t gap = 0; gap + 1 < script.keyframes.size(); ++gap)
        total += frames_in_gap(script, gap) + 1;
    return total;
}

float frame_to_param(const Script& script, int frame_index)
{
    if (script.keyframes.size() <= 1)
        return 0.0f;

    int offset = 0;
    for (std::size_t gap = 0; gap + 1 < script.keyframes.size(); ++gap) {
        const int steps = frames_in_gap(script, gap) + 1;
        if (frame_index <= offset + steps)
            return static_cast<float>(gap)
                + static_cast<float>(frame_index - offset) / static_cast<float>(steps);
        offset += steps;
    }
    return static_cast<float>(script.keyframes.size() - 1);
}

int keyframe_frame(const Script& script, int keyframe_index)
{
    int offset = 0;
    const int clamped = std::clamp(
        keyframe_index, 0, static_cast<int>(script.keyframes.size()) - 1);
    for (int gap = 0; gap < clamped; ++gap)
        offset += frames_in_gap(script, static_cast<std::size_t>(gap)) + 1;
    return offset;
}

Camera camera_for(const Script& script, float u)
{
    return camera_at(script.keyframes, u, Vec3{0.0f, 0.0f, 0.0f}, script.up);
}

} // namespace ospr
