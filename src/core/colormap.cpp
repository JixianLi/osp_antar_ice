#include "ospr/colormap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

} // namespace

ColorMap load_paraview_colormap(
    const std::string& path, const std::string& name, int resolution)
{
    if (resolution < 2)
        throw std::runtime_error("colormap resolution must be at least 2");

    pugi::xml_document document;
    const pugi::xml_parse_result parsed = document.load_file(path.c_str());
    if (!parsed)
        throw std::runtime_error(path + ": XML parse error: " + parsed.description());

    pugi::xml_node map;
    const pugi::xml_node root = document.child("ColorMaps");
    const pugi::xml_node first = root ? root.child("ColorMap") : document.child("ColorMap");
    if (name.empty()) {
        map = first;
    } else {
        for (pugi::xml_node candidate = first; candidate;
             candidate = candidate.next_sibling("ColorMap")) {
            if (name == candidate.attribute("name").value()) {
                map = candidate;
                break;
            }
        }
    }
    if (!map)
        throw std::runtime_error(name.empty()
                ? path + ": no <ColorMap> element"
                : path + ": no <ColorMap> named \"" + name + "\"");

    const std::string space = map.attribute("space").value();
    const bool in_lab = space == "Lab";
    if (!in_lab && space != "RGB" && !space.empty())
        throw std::runtime_error(
            path + ": unsupported colour space \"" + space + "\"; expected RGB or Lab");

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
    if (points.size() < 2)
        throw std::runtime_error(path + ": colormap needs at least 2 <Point> elements");

    std::stable_sort(points.begin(),
        points.end(),
        [](const ControlPoint& a, const ControlPoint& b) { return a.x < b.x; });

    const float domain_lo = points.front().x;
    const float domain_hi = points.back().x;
    if (!(domain_hi > domain_lo))
        throw std::runtime_error(path + ": colormap control points span zero range");

    ColorMap result;
    result.name = map.attribute("name").value();
    result.colors.resize(resolution);
    result.opacities.resize(resolution);

    std::size_t segment = 0;
    for (int index = 0; index < resolution; ++index) {
        const float x = domain_lo
            + (domain_hi - domain_lo) * (static_cast<float>(index) / (resolution - 1));
        while (segment + 2 < points.size() && points[segment + 1].x < x)
            ++segment;

        const ControlPoint& from = points[segment];
        const ControlPoint& to = points[segment + 1];
        const float span = to.x - from.x;
        const float t = span > 0.0f ? std::clamp((x - from.x) / span, 0.0f, 1.0f) : 0.0f;

        result.colors[index] = blend(from.color, to.color, t, in_lab);
        result.opacities[index] = lerp(from.opacity, to.opacity, t);
    }
    return result;
}

} // namespace ospr
