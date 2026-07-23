#include "ospr/script.h"

#include <algorithm>
#include <cmath>
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

    Script script;

    if (root.contains("scene")) {
        const json& scene = root.at("scene");
        if (scene.contains("type"))
            script.scene.type = scene.at("type").get<std::string>();
    }

    if (root.contains("output")) {
        const json& output = root.at("output");
        if (output.contains("dir"))
            script.output.directory = output.at("dir").get<std::string>();
        if (output.contains("width"))
            script.output.width = output.at("width").get<int>();
        if (output.contains("height"))
            script.output.height = output.at("height").get<int>();
        if (output.contains("spp"))
            script.output.samples_per_pixel = output.at("spp").get<int>();
        if (output.contains("background"))
            script.output.background = read_vec3(output.at("background"), "output.background");
    }

    if (root.contains("timeline")) {
        const json& timeline = root.at("timeline");
        if (timeline.contains("fps"))
            script.timeline.frames_per_second = timeline.at("fps").get<float>();
        if (timeline.contains("duration"))
            script.timeline.duration_seconds = timeline.at("duration").get<float>();
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

} // namespace ospr
