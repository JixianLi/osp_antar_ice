#include "ospr/opacity_curve.h"

#include <algorithm>
#include <cmath>

namespace ospr {
namespace {

// Fritsch-Carlson tangents: start from the average of the neighbouring secant
// slopes, then rein in any tangent that would let the cubic overshoot. The
// (alpha, beta) circle of radius 3 is the monotonicity region from the paper.
std::vector<float> monotone_tangents(const std::vector<OpacityPoint>& points)
{
    const std::size_t count = points.size();
    std::vector<float> secant(count - 1);
    for (std::size_t index = 0; index + 1 < count; ++index) {
        const float span = points[index + 1].layer - points[index].layer;
        secant[index]
            = span > 0.0f ? (points[index + 1].opacity - points[index].opacity) / span : 0.0f;
    }

    std::vector<float> tangent(count);
    tangent.front() = secant.front();
    tangent.back() = secant.back();
    for (std::size_t index = 1; index + 1 < count; ++index)
        tangent[index] = 0.5f * (secant[index - 1] + secant[index]);

    for (std::size_t index = 0; index + 1 < count; ++index) {
        if (secant[index] == 0.0f) {
            tangent[index] = 0.0f;
            tangent[index + 1] = 0.0f;
            continue;
        }
        const float alpha = tangent[index] / secant[index];
        const float beta = tangent[index + 1] / secant[index];
        const float radius = alpha * alpha + beta * beta;
        if (radius > 9.0f) {
            const float scale = 3.0f / std::sqrt(radius);
            tangent[index] = scale * alpha * secant[index];
            tangent[index + 1] = scale * beta * secant[index];
        }
    }
    return tangent;
}

} // namespace

float OpacityCurve::at(float layer) const
{
    if (points.empty())
        return 0.0f;
    if (points.size() == 1 || layer <= points.front().layer)
        return points.front().opacity;
    if (layer >= points.back().layer)
        return points.back().opacity;

    const std::vector<float> tangent = monotone_tangents(points);

    std::size_t segment = 0;
    while (segment + 2 < points.size() && points[segment + 1].layer <= layer)
        ++segment;

    const OpacityPoint& from = points[segment];
    const OpacityPoint& to = points[segment + 1];
    const float span = to.layer - from.layer;
    if (span <= 0.0f)
        return from.opacity;

    const float t = (layer - from.layer) / span;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;

    return h00 * from.opacity + h10 * span * tangent[segment] + h01 * to.opacity
        + h11 * span * tangent[segment + 1];
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
