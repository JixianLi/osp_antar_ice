#pragma once

#include <string>
#include <vector>

#include "ospr/math.h"

namespace ospr {

// A ParaView colormap resampled onto a uniform lookup table. Keyframe blending
// needs a fixed-size table: control-point counts differ between maps, so raw
// control points cannot be paired up and interpolated.
struct ColorMap
{
    std::string name;
    std::vector<Vec3> colors;    // sRGB, resolution entries
    std::vector<float> opacities; // resolution entries, from the file's o attribute
};

// Restrict the map to a sub-range of its own domain before resampling, so a
// ramp whose far end is too dark can be cut back without editing the file.
struct ColorMapTrim
{
    float lo{0.0f};
    float hi{1.0f};
};

// Reads a ParaView colormap and resamples it to `resolution` entries. Dispatches
// on extension: .xml is <ColorMaps><ColorMap><Point x o r g b/>, .json is the
// [{"ColorSpace", "Name", "RGBPoints":[x,r,g,b,...]}] export. Both honour RGB
// and Lab/CIELAB interpolation and throw on any other space rather than
// silently interpolating in the wrong one.
ColorMap load_colormap(const std::string& path,
    const std::string& name = "",
    int resolution = 256,
    ColorMapTrim trim = {});

} // namespace ospr
