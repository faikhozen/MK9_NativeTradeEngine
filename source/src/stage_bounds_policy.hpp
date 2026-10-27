#pragma once
#include <cmath>
namespace MK9TradeFix { namespace StageBounds {
struct Interval { float center; float halfWidth; };
// Native boundary projection is measured from Point A, not the spawn offset.
inline bool Resolve(float minimum, float maximum, Interval& result) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) return false;
    const float span = maximum - minimum;
    if (!std::isfinite(span) || span < 800.0f || span > 20000.0f) return false;
    result.center = minimum + span * 0.5f;
    result.halfWidth = span * 0.5f;
    return true;
}
} }
