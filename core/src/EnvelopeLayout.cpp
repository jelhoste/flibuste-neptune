#include "neptune/core/EnvelopeLayout.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
namespace
{
double unitsOf (double ms) { return std::log (1.0 + ms / envelopeTimeConstantMs); }

struct Seg { bool active; double units; };

double totalWidth (const Seg (&s)[5], double scale)
{
    double w = std::max (envelopeMinPlateauPx, scale * envelopePlateauUnits);
    for (const auto& g : s)
        if (g.active) w += std::max (envelopeMinSegmentPx, scale * g.units);
    return w;
}
} // namespace

EnvelopeLayout computeEnvelopeLayout (const EnvelopeDescription& e, double availableWidth, double frozenScale) noexcept
{
    const EnvelopeSegment* segs[5] = { &e.delay, &e.attack, &e.hold, &e.decay, &e.release };
    Seg s[5];
    for (int i = 0; i < 5; ++i)
    {
        s[i].active = segs[i]->on && segs[i]->ms > 0.0;
        s[i].units = s[i].active ? unitsOf (std::min (segs[i]->ms, envelopeMaxMs)) : 0.0;
    }

    double scale = frozenScale;
    if (scale <= 0.0)
    {
        if (totalWidth (s, envelopeMaxPxPerUnit) <= availableWidth) scale = envelopeMaxPxPerUnit;
        else if (totalWidth (s, 0.0) >= availableWidth) scale = 0.0; // même les largeurs minimales ne tiennent pas
        else
        {
            double lo = 0.0, hi = envelopeMaxPxPerUnit;
            for (int i = 0; i < 40; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                (totalWidth (s, mid) <= availableWidth ? lo : hi) = mid;
            }
            scale = lo;
        }
    }

    EnvelopeLayout l;
    l.scale = scale;
    double x = 0.0;
    for (int i = 0; i < 5; ++i)
    {
        if (i == 4) // le sustain se place entre le decay (3) et le relâchement (4)
        {
            l.plateauX0 = x;
            x += std::max (envelopeMinPlateauPx, scale * envelopePlateauUnits);
            l.plateauX1 = x;
        }
        l.active[i] = s[i].active;
        l.x0[i] = x;
        if (s[i].active) x += std::max (envelopeMinSegmentPx, scale * s[i].units);
        l.x1[i] = x;
    }
    l.endX = x;
    return l;
}

double envelopeTimeFromWidth (double widthPx, double scale) noexcept
{
    if (scale <= 0.0 || widthPx < envelopeMinSegmentPx * 0.5) return 0.0;
    const double w = std::max (widthPx, envelopeMinSegmentPx);
    return std::clamp (envelopeTimeConstantMs * (std::exp (w / scale) - 1.0), 0.0, envelopeMaxMs);
}

double envelopeScrubTime (double ms, double dxPx) noexcept
{
    const double u = std::max (0.0, unitsOf (std::clamp (ms, 0.0, envelopeMaxMs)) + dxPx / 20.0);
    return std::clamp (envelopeTimeConstantMs * (std::exp (u) - 1.0), 0.0, envelopeMaxMs);
}
} // namespace neptune::core
