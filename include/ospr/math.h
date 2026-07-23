#pragma once

#include <cmath>

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

} // namespace ospr

// ospray::cpp::setParam and CopiedData dispatch on this trait; the binary
// release ships no rkcommon headers, so our own types register here instead.
namespace ospray {
OSPTYPEFOR_SPECIALIZATION(ospr::Vec3, OSP_VEC3F);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec4, OSP_VEC4F);
OSPTYPEFOR_SPECIALIZATION(ospr::Vec3ui, OSP_VEC3UI);
} // namespace ospray
