#include "ospr/colormap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

namespace ospr {
namespace {

struct ControlPoint
{
    float x{0.0f};
    Vec3 color;
    float opacity{1.0f};
};

// sRGB <-> CIELAB via CIEXYZ, D65 white point, matching vtkMath::RGBToLab so
// that a space="Lab" map interpolates the way ParaView draws it.
//
//   IEC 61966-2-1 for the sRGB transfer function,
//   CIE 15:2004 for the Lab equations,
//   D65 white point Xn, Yn, Zn = 0.9505, 1.0000, 1.0890.

constexpr double WHITE_X = 0.9505;
constexpr double WHITE_Y = 1.0000;
constexpr double WHITE_Z = 1.0890;

double srgb_to_linear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double channel)
{
    return channel <= 0.0031308 ? channel * 12.92
                                : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
}

// The Lab forward and inverse nonlinearity, with the linear segment near zero
// that keeps the derivative finite.
double lab_f(double t)
{
    constexpr double EPSILON = 216.0 / 24389.0;
    constexpr double KAPPA = 24389.0 / 27.0;
    return t > EPSILON ? std::cbrt(t) : (KAPPA * t + 16.0) / 116.0;
}

double lab_f_inverse(double t)
{
    constexpr double EPSILON_CBRT = 6.0 / 29.0;
    constexpr double KAPPA = 24389.0 / 27.0;
    return t > EPSILON_CBRT ? t * t * t : (116.0 * t - 16.0) / KAPPA;
}

Vec3 rgb_to_lab(Vec3 rgb)
{
    const double r = srgb_to_linear(rgb.x);
    const double g = srgb_to_linear(rgb.y);
    const double b = srgb_to_linear(rgb.z);

    const double x = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    const double z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;

    const double fx = lab_f(x / WHITE_X);
    const double fy = lab_f(y / WHITE_Y);
    const double fz = lab_f(z / WHITE_Z);

    return {static_cast<float>(116.0 * fy - 16.0),
        static_cast<float>(500.0 * (fx - fy)),
        static_cast<float>(200.0 * (fy - fz))};
}

Vec3 lab_to_rgb(Vec3 lab)
{
    const double fy = (lab.x + 16.0) / 116.0;
    const double fx = fy + lab.y / 500.0;
    const double fz = fy - lab.z / 200.0;

    const double x = WHITE_X * lab_f_inverse(fx);
    const double y = WHITE_Y * lab_f_inverse(fy);
    const double z = WHITE_Z * lab_f_inverse(fz);

    const double r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
    const double g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
    const double b = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;

    return {static_cast<float>(std::clamp(linear_to_srgb(r), 0.0, 1.0)),
        static_cast<float>(std::clamp(linear_to_srgb(g), 0.0, 1.0)),
        static_cast<float>(std::clamp(linear_to_srgb(b), 0.0, 1.0))};
}

Vec3 blend(Vec3 a, Vec3 b, float t, bool in_lab)
{
    if (!in_lab)
        return lerp(a, b, t);
    return lab_to_rgb(lerp(rgb_to_lab(a), rgb_to_lab(b), t));
}

// Shared tail: sort, validate, resample the control points onto a uniform LUT.
ColorMap resample(std::vector<ControlPoint> points,
    const std::string& name,
    bool in_lab,
    int resolution,
    ColorMapTrim trim,
    const std::string& path)
{
    if (points.size() < 2)
        throw std::runtime_error(path + ": colormap needs at least 2 control points");
    if (resolution < 2)
        throw std::runtime_error("colormap resolution must be at least 2");
    if (!(trim.hi > trim.lo) || trim.lo < 0.0f || trim.hi > 1.0f)
        throw std::runtime_error(path + ": trim must satisfy 0 <= lo < hi <= 1");

    std::stable_sort(points.begin(),
        points.end(),
        [](const ControlPoint& a, const ControlPoint& b) { return a.x < b.x; });

    const float domain_lo = points.front().x;
    const float domain_hi = points.back().x;
    if (!(domain_hi > domain_lo))
        throw std::runtime_error(path + ": colormap control points span zero range");

    // trim is expressed as a fraction of the map's own domain
    const float span = domain_hi - domain_lo;
    const float from = domain_lo + span * trim.lo;
    const float to = domain_lo + span * trim.hi;

    ColorMap result;
    result.name = name;
    result.colors.resize(resolution);
    result.opacities.resize(resolution);

    std::size_t segment = 0;
    for (int index = 0; index < resolution; ++index) {
        const float x
            = from + (to - from) * (static_cast<float>(index) / (resolution - 1));
        while (segment + 2 < points.size() && points[segment + 1].x < x)
            ++segment;
        while (segment > 0 && points[segment].x > x)
            --segment;

        const ControlPoint& a = points[segment];
        const ControlPoint& b = points[segment + 1];
        const float width = b.x - a.x;
        const float t = width > 0.0f ? std::clamp((x - a.x) / width, 0.0f, 1.0f) : 0.0f;

        result.colors[index] = blend(a.color, b.color, t, in_lab);
        result.opacities[index] = lerp(a.opacity, b.opacity, t);
    }
    return result;
}

bool space_is_lab(const std::string& space, const std::string& path)
{
    if (space == "Lab" || space == "CIELAB")
        return true;
    if (space == "RGB" || space.empty())
        return false;
    throw std::runtime_error(
        path + ": unsupported colour space \"" + space + "\"; expected RGB, Lab or CIELAB");
}

ColorMap load_xml_colormap(
    const std::string& path, const std::string& name, int resolution, ColorMapTrim trim)
{
    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_file(path.c_str());
    if (!parsed)
        throw std::runtime_error(path + ": XML parse error: " + parsed.description());

    const pugi::xml_node root = document.child("ColorMaps");
    const pugi::xml_node first = root ? root.child("ColorMap") : document.child("ColorMap");
    pugi::xml_node map;
    if (name.empty()) {
        map = first;
    } else {
        for (pugi::xml_node candidate = first; candidate;
             candidate = candidate.next_sibling("ColorMap"))
            if (name == candidate.attribute("name").value()) {
                map = candidate;
                break;
            }
    }
    if (!map)
        throw std::runtime_error(name.empty()
                ? path + ": no <ColorMap> element"
                : path + ": no <ColorMap> named \"" + name + "\"");

    const bool in_lab = space_is_lab(map.attribute("space").value(), path);

    std::vector<ControlPoint> points;
    for (pugi::xml_node node : map.children("Point")) {
        ControlPoint point;
        point.x = node.attribute("x").as_float();
        point.color = {node.attribute("r").as_float(),
            node.attribute("g").as_float(),
            node.attribute("b").as_float()};
        point.opacity = node.attribute("o").as_float(1.0f);
        points.push_back(point);
    }
    return resample(
        std::move(points), map.attribute("name").value(), in_lab, resolution, trim, path);
}

ColorMap load_json_colormap(
    const std::string& path, const std::string& name, int resolution, ColorMapTrim trim)
{
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("cannot open " + path);

    nlohmann::json root;
    try {
        stream >> root;
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error(path + ": malformed JSON: " + error.what());
    }

    const nlohmann::json* chosen = nullptr;
    if (root.is_array()) {
        for (const auto& entry : root) {
            if (name.empty() || (entry.contains("Name") && entry["Name"] == name)) {
                chosen = &entry;
                break;
            }
        }
    } else {
        chosen = &root;
    }
    if (chosen == nullptr)
        throw std::runtime_error(path + ": no colormap named \"" + name + "\"");

    const auto& map = *chosen;
    if (!map.contains("RGBPoints"))
        throw std::runtime_error(path + ": colormap has no RGBPoints");

    const std::string space
        = map.contains("ColorSpace") ? map["ColorSpace"].get<std::string>() : "RGB";
    const bool in_lab = space_is_lab(space, path);

    const auto& flat = map["RGBPoints"];
    if (flat.size() % 4 != 0)
        throw std::runtime_error(path + ": RGBPoints length is not a multiple of 4");

    std::vector<ControlPoint> points;
    for (std::size_t index = 0; index + 3 < flat.size(); index += 4)
        points.push_back({flat[index].get<float>(),
            {flat[index + 1].get<float>(),
                flat[index + 2].get<float>(),
                flat[index + 3].get<float>()},
            1.0f});

    return resample(std::move(points),
        map.contains("Name") ? map["Name"].get<std::string>() : "",
        in_lab,
        resolution,
        trim,
        path);
}

} // namespace

ColorMap load_colormap(
    const std::string& path, const std::string& name, int resolution, ColorMapTrim trim)
{
    const bool is_json
        = path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0;
    return is_json ? load_json_colormap(path, name, resolution, trim)
                   : load_xml_colormap(path, name, resolution, trim);
}

} // namespace ospr
