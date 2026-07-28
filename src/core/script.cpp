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
    if (!color.contains("layers"))
        throw std::runtime_error(where + ".color: needs 'layers'");
    const json& layers = color.at("layers");
    if (!layers.is_array() || layers.size() != LAYER_COUNT)
        throw std::runtime_error(
            where + ".color.layers: expected " + std::to_string(LAYER_COUNT) + " [r, g, b] entries");
    for (int layer = 0; layer < LAYER_COUNT; ++layer)
        volume.layer_colors[layer] = read_vec3(
            layers[layer], where + ".color.layers[" + std::to_string(layer) + "]");
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

CurveSpec read_curve(
    const json& node, const std::filesystem::path& base, const std::string& where)
{
    CurveSpec curve;
    if (!node.contains("path"))
        throw std::runtime_error(where + ": missing 'path'");
    curve.path = resolve(base, node.at("path").get<std::string>());
    if (node.contains("radius"))
        curve.radius = node.at("radius").get<float>();
    if (node.contains("color"))
        curve.color = read_vec3(node.at("color"), where + ".color");
    if (node.contains("layer"))
        curve.layer = node.at("layer").is_null() ? -1.0f : node.at("layer").get<float>();
    if (node.contains("roughness"))
        curve.roughness = node.at("roughness").get<float>();
    return curve;
}

FlagSpec read_flag(
    const json& node, const std::filesystem::path& base, const std::string& where)
{
    FlagSpec flag;
    if (!node.contains("path"))
        throw std::runtime_error(where + ": missing 'path'");
    flag.path = resolve(base, node.at("path").get<std::string>());
    if (!node.contains("texture"))
        throw std::runtime_error(where + ": missing 'texture'");
    flag.texture = resolve(base, node.at("texture").get<std::string>());
    if (node.contains("pole")) {
        const json& pole = node.at("pole");
        if (!pole.is_array() || pole.size() != 2)
            throw std::runtime_error(where + ".pole: expected an array of 2 numbers [x, y]");
        flag.pole_x = pole[0].get<float>();
        flag.pole_y = pole[1].get<float>();
    }
    return flag;
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
    if (node.contains("follow_camera"))
        light.follow_camera = node.at("follow_camera").get<bool>();
    if (node.contains("enabled"))
        light.enabled = node.at("enabled").get<bool>();
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

    if (session.contains("renderer")) {
        const json& renderer = session.at("renderer");
        if (renderer.contains("type"))
            script.session.renderer.type = renderer.at("type").get<std::string>();
        if (renderer.contains("spp"))
            script.session.renderer.samples_per_pixel = renderer.at("spp").get<int>();
        if (renderer.contains("denoise"))
            script.session.renderer.denoise = renderer.at("denoise").get<bool>();
        if (renderer.contains("shadow_samples"))
            script.session.renderer.light_samples
                = renderer.at("shadow_samples").get<int>();
        if (renderer.contains("ao_samples"))
            script.session.renderer.ao_samples = renderer.at("ao_samples").get<int>();
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
            else if (type == "curve")
                script.session.curves.push_back(read_curve(object, base, where));
            else if (type == "flag")
                script.session.flags.push_back(read_flag(object, base, where));
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
        if (timeline.contains("center"))
            script.center = read_vec3(timeline.at("center"), "timeline.center");
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
    return camera_at(script.keyframes, u, script.center, script.up);
}

bool aim_follow_lights(std::vector<LightSpec>& lights, const Camera& camera)
{
    const Vec3 forward = normalize(camera.target - camera.position);
    bool changed = false;
    for (LightSpec& light : lights) {
        if (!light.follow_camera || light.type != "distant")
            continue;
        if (light.direction.x == forward.x && light.direction.y == forward.y
            && light.direction.z == forward.z)
            continue;
        light.direction = forward;
        changed = true;
    }
    return changed;
}

namespace {

using nlohmann::ordered_json;

// Assigning a float widens to double, whose shortest round-trip text can carry
// the widening noise (0.35f -> "0.3499999940395355"). Round to 1e-6 first: finer
// than any knob these buttons drive, coarse enough to serialise cleanly.
double clean(float value)
{
    return std::round(static_cast<double>(value) * 1e6) / 1e6;
}

ordered_json clean3(Vec3 value)
{
    return ordered_json{clean(value.x), clean(value.y), clean(value.z)};
}

ordered_json read_document(const std::string& path)
{
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot open script for saving: " + path);
    try {
        return ordered_json::parse(stream);
    } catch (const ordered_json::parse_error& error) {
        throw std::runtime_error("malformed JSON in " + path + ": " + error.what());
    }
}

void write_document(const std::string& path, const ordered_json& document)
{
    std::ofstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot write script: " + path);
    stream << document.dump(2) << "\n";
}

ordered_json& session_of(ordered_json& document, const std::string& path)
{
    if (!document.contains("session"))
        throw std::runtime_error(path + ": missing 'session'");
    return document["session"];
}

ordered_json& first_volume(ordered_json& session, const std::string& path)
{
    if (session.contains("objects"))
        for (ordered_json& object : session["objects"])
            if (object.value("type", std::string()) == "volume")
                return object;
    throw std::runtime_error(path + ": no volume object to save into");
}

} // namespace

void save_quality(const std::string& path, int spp, int shadow_samples, int ao_samples)
{
    ordered_json document = read_document(path);
    ordered_json& renderer = session_of(document, path)["renderer"];
    renderer["spp"] = spp;
    renderer["shadow_samples"] = shadow_samples;
    renderer["ao_samples"] = ao_samples;
    write_document(path, document);
}

void save_background(const std::string& path, Vec3 top, Vec3 bottom)
{
    ordered_json document = read_document(path);
    session_of(document, path)["renderer"]["background"]
        = ordered_json{{"top", clean3(top)}, {"bottom", clean3(bottom)}};
    write_document(path, document);
}

void save_lights(const std::string& path, const std::vector<LightSpec>& lights)
{
    // Rewritten whole rather than patched in place: the UI can now add and toggle
    // lights, so the array's length and membership change. LightSpec carries
    // every field the loader reads, so nothing is lost by rebuilding.
    ordered_json document = read_document(path);
    ordered_json array = ordered_json::array();
    for (const LightSpec& light : lights) {
        ordered_json entry;
        entry["type"] = light.type;
        entry["intensity"] = clean(light.intensity);
        entry["color"] = clean3(light.color);
        if (light.type == "distant" || light.type == "sunSky") {
            entry["direction"] = clean3(light.direction);
            if (light.type == "distant") {
                entry["angular_diameter"] = clean(light.angular_diameter);
                entry["follow_camera"] = light.follow_camera;
            }
        }
        entry["visible"] = light.visible;
        entry["enabled"] = light.enabled;
        array.push_back(entry);
    }
    session_of(document, path)["lights"] = array;
    write_document(path, document);
}

void save_density(const std::string& path, float density_scale)
{
    ordered_json document = read_document(path);
    first_volume(session_of(document, path), path)["density_scale"] = clean(density_scale);
    write_document(path, document);
}

void save_colors(const std::string& path, const std::array<Vec3, LAYER_COUNT>& layer_colors)
{
    ordered_json document = read_document(path);
    ordered_json layers = ordered_json::array();
    for (const Vec3& color : layer_colors)
        layers.push_back(clean3(color));
    first_volume(session_of(document, path), path)["color"]["layers"] = layers;
    write_document(path, document);
}

void save_frames_between(const std::string& path, int frames_between)
{
    ordered_json document = read_document(path);
    if (!document.contains("timeline"))
        document["timeline"] = ordered_json::object();
    document["timeline"]["frames_between"] = frames_between;
    write_document(path, document);
}

void save_keyframes(const std::string& path, const std::vector<Keyframe>& keyframes)
{
    ordered_json document = read_document(path);
    ordered_json array = ordered_json::array();
    for (const Keyframe& keyframe : keyframes) {
        ordered_json entry;
        entry["azimuth"] = clean(keyframe.azimuth_degrees);
        entry["elevation"] = clean(keyframe.elevation_degrees);
        entry["fov"] = clean(keyframe.fov_y_degrees);
        entry["radius"] = clean(keyframe.radius);
        ordered_json points = ordered_json::array();
        for (const OpacityPoint& point : keyframe.opacity.points)
            points.push_back(ordered_json{clean(point.layer), clean(point.opacity)});
        entry["opacity"] = points;
        entry["ease"] = keyframe.ease == Ease::Smooth ? "smooth" : "linear";
        // -1 means "use the timeline default", which is spelled by leaving the
        // key out rather than by writing the sentinel.
        if (keyframe.frames_after >= 0)
            entry["frames_after"] = keyframe.frames_after;
        array.push_back(entry);
    }
    document["keyframes"] = array;
    write_document(path, document);
}

void save_center(const std::string& path, Vec3 center)
{
    ordered_json document = read_document(path);
    document["timeline"]["center"] = clean3(center);
    write_document(path, document);
}

} // namespace ospr
