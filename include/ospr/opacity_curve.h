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
// (unlike colour, where blending two maps goes through muddy intermediates).
struct OpacityCurve
{
    std::vector<OpacityPoint> points;

    // Samples with monotone cubic Hermite interpolation (Fritsch & Carlson
    // 1980). A Catmull-Rom spline through a knee as steep as the peel front
    // overshoots past 0 and 1 on either side of it; clamping that would leave a
    // bright ring above the front. The monotone scheme provably stays within
    // the range spanned by its control points.
    float at(float layer) const;
};

OpacityCurve lerp(const OpacityCurve& a, const OpacityCurve& b, float t, int resolution = 256);

} // namespace ospr
