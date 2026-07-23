#pragma once

#include <cmath>
#include <cstdint>

#include <ospray/ospray_cpp/Traits.h>

namespace ospr {

struct Vec3
{
    float x{0.0f}, y{0.0f}, z{0.0f};
};

struct Vec4
{
    float x{0.0f}, y{0.0f}, z{0.0f}, w{0.0f};
};

struct Vec3ui
{
    unsigned int x{0}, y{0}, z{0};
};

struct Vec2
{
    float x{0.0f}, y{0.0f};
};

// OSPRay infers a 2D Data array's shape (a texture) from a vec2ul.
struct Vec2ul
{
    unsigned long long x{0}, y{0};

    Vec2ul() = default;
    explicit Vec2ul(unsigned long long value) : x(value), y(value) {}
    Vec2ul(unsigned long long x_value, unsigned long long y_value) : x(x_value), y(y_value) {}
};

// OSPRay infers a 3D Data array's shape from a vec3ul, so volume dimensions
// have to be passed as this rather than as three separate integers. cpp::Data
// reinterprets the address of one as three contiguous elements and default-
// constructs a stride with DIM_T(0), hence the scalar constructor.
struct Vec3ul
{
    unsigned long long x{0}, y{0}, z{0};

    Vec3ul() = default;
    explicit Vec3ul(unsigned long long value) : x(value), y(value), z(value) {}
    Vec3ul(unsigned long long x_value, unsigned long long y_value, unsigned long long z_value)
        : x(x_value), y(y_value), z(z_value)
    {}
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(Vec3 a, float s)
{
    return {a.x * s, a.y * s, a.z * s};
}

inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 a)
{
    return std::sqrt(dot(a, a));
}

inline Vec3 normalize(Vec3 a)
{
    const float len = length(a);
    return len > 0.0f ? a * (1.0f / len) : a;
}

inline Vec3 lerp(Vec3 a, Vec3 b, float t)
{
    return a + (b - a) * t;
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// sRGB <-> HSV so a gradient interpolates through hue rather than desaturating
// across the RGB midpoint. Components are all in [0, 1]; hue wraps.
inline Vec3 rgb_to_hsv(Vec3 rgb)
{
    const float max = std::fmax(rgb.x, std::fmax(rgb.y, rgb.z));
    const float min = std::fmin(rgb.x, std::fmin(rgb.y, rgb.z));
    const float chroma = max - min;

    float hue = 0.0f;
    if (chroma > 0.0f) {
        if (max == rgb.x)
            hue = std::fmod((rgb.y - rgb.z) / chroma, 6.0f);
        else if (max == rgb.y)
            hue = (rgb.z - rgb.x) / chroma + 2.0f;
        else
            hue = (rgb.x - rgb.y) / chroma + 4.0f;
        hue /= 6.0f;
        if (hue < 0.0f)
            hue += 1.0f;
    }
    const float saturation = max > 0.0f ? chroma / max : 0.0f;
    return {hue, saturation, max};
}

inline Vec3 hsv_to_rgb(Vec3 hsv)
{
    const float hue = hsv.x * 6.0f;
    const float chroma = hsv.z * hsv.y;
    const float second = chroma * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
    const float match = hsv.z - chroma;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hue < 1.0f) { r = chroma; g = second; }
    else if (hue < 2.0f) { r = second; g = chroma; }
    else if (hue < 3.0f) { g = chroma; b = second; }
    else if (hue < 4.0f) { g = second; b = chroma; }
    else if (hue < 5.0f) { r = second; b = chroma; }
    else { r = chroma; b = second; }
    return {r + match, g + match, b + match};
}

// Interpolate two colours in HSV, taking the shorter way around the hue circle.
inline Vec3 lerp_hsv(Vec3 a, Vec3 b, float t)
{
    Vec3 from = rgb_to_hsv(a);
    Vec3 to = rgb_to_hsv(b);
    // A grey endpoint has no meaningful hue; borrow the other's so the ramp does
    // not swing through an arbitrary colour.
    if (from.y < 1e-4f)
        from.x = to.x;
    if (to.y < 1e-4f)
        to.x = from.x;
    float delta = to.x - from.x;
    if (delta > 0.5f)
        delta -= 1.0f;
    else if (delta < -0.5f)
        delta += 1.0f;
    float hue = from.x + delta * t;
    if (hue < 0.0f)
        hue += 1.0f;
    else if (hue > 1.0f)
        hue -= 1.0f;
    return hsv_to_rgb({hue, lerp(from.y, to.y, t), lerp(from.z, to.z, t)});
}

} // namespace ospr

// ospray::cpp::setParam and CopiedData dispatch on this trait; the binary
// release ships no rkcommon headers, so our own types register here instead.
namespace ospray {
OSPTYPEFOR_SPECIALIZATION(ospr::Vec3, OSP_VEC3F);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec4, OSP_VEC4F);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec3ui, OSP_VEC3UI);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec2, OSP_VEC2F);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec2ul, OSP_VEC2UL);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec3ul, OSP_VEC3UL);
} // namespace ospray
