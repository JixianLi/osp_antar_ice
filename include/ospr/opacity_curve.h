#pragma once

#include <vector>

namespace ospr {

struct OpacityPoint
{
    float layer{0.0f};
    float opacity{0.0f};
};

// The peel is drawn as a curve, not parameterised, because the shape is an
// authoring decision. A whole curve is keyframed; blending two of them is a
// lerp of their sampled tables, which is well defined because opacity is scalar
// (unlike color, where blending two maps goes through muddy intermediates).
struct OpacityCurve
{
    std::vector<OpacityPoint> points;

    // Piecewise linear between control points, held flat at the endpoint values
    // outside [front, back]. The editor is control-point based, so the drawn
    // segments are exactly what renders -- no spline is interposed between what
    // the author places and the sampled 256-entry LUT.
    float at(float layer) const;
};

OpacityCurve lerp(const OpacityCurve& a, const OpacityCurve& b, float t, int resolution = 256);

} // namespace ospr
