#include "ospr/opacity_curve.h"

#include <algorithm>

namespace ospr {

float OpacityCurve::at(float layer) const
{
    if (points.empty())
        return 0.0f;
    if (points.size() == 1 || layer <= points.front().layer)
        return points.front().opacity;
    if (layer >= points.back().layer)
        return points.back().opacity;

    std::size_t segment = 0;
    while (segment + 2 < points.size() && points[segment + 1].layer <= layer)
        ++segment;

    const OpacityPoint& from = points[segment];
    const OpacityPoint& to = points[segment + 1];
    const float span = to.layer - from.layer;
    if (span <= 0.0f)
        return from.opacity;

    const float t = (layer - from.layer) / span;
    return from.opacity + t * (to.opacity - from.opacity);
}

OpacityCurve lerp(const OpacityCurve& a, const OpacityCurve& b, float t, int resolution)
{
    if (a.points.empty())
        return b;
    if (b.points.empty())
        return a;

    // Curves keyframed at different times have different control points, so
    // they are blended through a common uniform sampling rather than pairwise.
    const float lo = std::min(a.points.front().layer, b.points.front().layer);
    const float hi = std::max(a.points.back().layer, b.points.back().layer);

    OpacityCurve blended;
    blended.points.resize(resolution);
    for (int index = 0; index < resolution; ++index) {
        const float layer
            = lo + (hi - lo) * (static_cast<float>(index) / (resolution - 1));
        blended.points[index]
            = {layer, a.at(layer) + (b.at(layer) - a.at(layer)) * t};
    }
    return blended;
}

} // namespace ospr
