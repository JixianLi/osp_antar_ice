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

// Reads a ParaView <ColorMaps><ColorMap> XML file and resamples the first map
// (or the one named `name`, if given) to `resolution` entries. Honours
// space="RGB" and space="Lab"; anything else throws rather than silently
// interpolating in the wrong space.
ColorMap load_paraview_colormap(
    const std::string& path, const std::string& name = "", int resolution = 256);

} // namespace ospr
