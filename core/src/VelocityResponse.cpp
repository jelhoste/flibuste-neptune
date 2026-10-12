#include "neptune/core/VelocityResponse.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace neptune::core
{
namespace
{
constexpr double kHalfPi = 1.57079632679489661923;

double shapePower (double shape) noexcept { return std::exp2 (3.0 * shape); }

// Pentes de Fritsch-Carlson pour des données monotones croissantes.
std::vector<double> pchipSlopes (const std::vector<double>& x, const std::vector<double>& y)
{
    const std::size_t n = x.size();
    std::vector<double> d (n - 1), m (n);
    for (std::size_t i = 0; i + 1 < n; ++i) d[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
    if (n == 2) { m[0] = m[1] = d[0]; return m; }
    for (std::size_t i = 1; i + 1 < n; ++i)
    {
        if (d[i - 1] <= 0.0 || d[i] <= 0.0) { m[i] = 0.0; continue; }
        const double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
        const double w1 = 2.0 * h1 + h0, w2 = h1 + 2.0 * h0;
        m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
    }
    auto endSlope = [] (double h0, double h1, double d0, double d1)
    {
        double s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (s * d0 <= 0.0) s = 0.0;
        else if (d0 * d1 <= 0.0 && std::abs (s) > 3.0 * std::abs (d0)) s = 3.0 * d0;
        return s;
    };
    m[0] = endSlope (x[1] - x[0], x[2] - x[1], d[0], d[1]);
    m[n - 1] = endSlope (x[n - 1] - x[n - 2], x[n - 2] - x[n - 3], d[n - 2], d[n - 3]);
    return m;
}
} // namespace

VelocityCurve sanitised (VelocityCurve c)
{
    if (! std::isfinite (c.shape)) c.shape = 0.0;
    c.shape = std::clamp (c.shape, -1.0, 1.0);
    std::vector<std::pair<double, double>> in;
    for (const auto& p : c.points)
        if (std::isfinite (p.first) && std::isfinite (p.second)) in.push_back ({ std::clamp (p.first, 0.0, 1.0), std::clamp (p.second, 0.0, 1.0) });
    std::stable_sort (in.begin(), in.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::pair<double, double>> out;
    double lastX = 0.0, lastY = 0.0;
    for (const auto& p : in)
    {
        if (out.size() >= VelocityCurve::maxPoints) break;
        if (p.first - lastX < VelocityCurve::minPointGap || 1.0 - p.first < VelocityCurve::minPointGap) continue;
        const double y = std::max (p.second, lastY); // jamais de recul
        out.push_back ({ p.first, y });
        lastX = p.first;
        lastY = y;
    }
    c.points = std::move (out);
    return c;
}

double evaluate (const VelocityCurve& curveIn, double x) noexcept
{
    if (! std::isfinite (x)) return 0.0;
    x = std::clamp (x, 0.0, 1.0);
    const VelocityCurve c = sanitised (curveIn);
    if (c.points.empty()) return std::clamp (std::pow (x, shapePower (c.shape)), 0.0, 1.0);

    std::vector<double> xs { 0.0 }, ys { 0.0 };
    for (const auto& p : c.points) { xs.push_back (p.first); ys.push_back (p.second); }
    xs.push_back (1.0);
    ys.push_back (1.0);
    const auto m = pchipSlopes (xs, ys);
    std::size_t i = 0;
    while (i + 2 < xs.size() && x > xs[i + 1]) ++i;
    const double h = xs[i + 1] - xs[i];
    const double t = (x - xs[i]) / h;
    const double t2 = t * t, t3 = t2 * t;
    const double v = (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
    return std::clamp (v, ys[i], ys[i + 1]); // monotone par construction ; la borne protège des arrondis
}

VelocityLut buildLut (const VelocityCurve& c)
{
    VelocityLut lut {};
    const VelocityCurve s = sanitised (c);
    for (int k = 0; k < 128; ++k) lut[static_cast<std::size_t> (k)] = static_cast<float> (evaluate (s, static_cast<double> (k) / 127.0));
    lut[0] = 0.0f;
    lut[127] = 1.0f;
    return lut;
}

float lookup (const VelocityLut& lut, float velocity) noexcept
{
    if (! (velocity > 0.0f)) return 0.0f;
    if (velocity >= 1.0f) return lut[127];
    const float pos = velocity * 127.0f;
    const int i = static_cast<int> (pos);
    const float f = pos - static_cast<float> (i);
    return lut[static_cast<std::size_t> (i)] + f * (lut[static_cast<std::size_t> (i) + 1] - lut[static_cast<std::size_t> (i)]);
}

VelocityCurve presetCurve (VelocityPreset p)
{
    VelocityCurve c;
    switch (p)
    {
        case VelocityPreset::Soft: c.shape = -0.35; break;
        case VelocityPreset::VerySoft: c.shape = -0.7; break;
        case VelocityPreset::Hard: c.shape = 0.35; break;
        case VelocityPreset::VeryHard: c.shape = 0.7; break;
        default: break;
    }
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------

std::vector<LayerFade> computeLayerFades (const std::vector<LayerKey>& zones, int width)
{
    std::vector<LayerFade> out (zones.size());
    width = std::max (0, width);

    // Famille -> couches distinctes (velLow, velHigh) -> indices de zones
    using Family = std::tuple<int, int, int, bool>;
    using Range = std::pair<int, int>;
    std::map<Family, std::map<Range, std::vector<std::size_t>>> families;
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const auto& z = zones[i];
        families[{ z.keyLow, z.keyHigh, z.group, z.onRelease }][{ z.velLow, z.velHigh }].push_back (i);
        out[i].activeLo = static_cast<float> (z.velLow) - 0.5f;
        out[i].activeHi = static_cast<float> (z.velHigh) + 0.5f;
    }

    for (auto& [family, layers] : families)
    {
        std::vector<Range> ranges;
        for (const auto& [r, idx] : layers) ranges.push_back (r);
        for (const auto& P : ranges)
        {
            const Range* lower = nullptr;
            const Range* upper = nullptr;
            for (const auto& Q : ranges)
            {
                if (Q.first < P.first && Q.second < P.second && Q.second >= P.first - 1 && (lower == nullptr || Q.second > lower->second)) lower = &Q;
                if (Q.first > P.first && Q.second > P.second && Q.first <= P.second + 1 && (upper == nullptr || Q.first < upper->first)) upper = &Q;
            }
            LayerFade f;
            f.activeLo = static_cast<float> (P.first) - 0.5f;
            f.activeHi = static_cast<float> (P.second) + 0.5f;
            const float wP = static_cast<float> (P.second - P.first + 1);
            if (lower != nullptr)
            {
                if (lower->second >= P.first) // chevauchement explicite : toute la zone commune
                {
                    f.hasIn = true;
                    f.inStart = static_cast<float> (P.first) - 0.5f;
                    f.inEnd = static_cast<float> (lower->second) + 0.5f;
                }
                else if (width > 0)           // jointives : fondu automatique centré sur la frontière
                {
                    const float wL = static_cast<float> (lower->second - lower->first + 1);
                    const float h = std::min ({ static_cast<float> (width) / 2.0f, wL / 2.0f, wP / 2.0f });
                    if (h > 0.0f)
                    {
                        f.hasIn = true;
                        f.inStart = static_cast<float> (P.first) - 0.5f - h;
                        f.inEnd = static_cast<float> (P.first) - 0.5f + h;
                        f.activeLo = f.inStart;
                    }
                }
            }
            if (upper != nullptr)
            {
                if (upper->first <= P.second)
                {
                    f.hasOut = true;
                    f.outStart = static_cast<float> (upper->first) - 0.5f;
                    f.outEnd = static_cast<float> (P.second) + 0.5f;
                }
                else if (width > 0)
                {
                    const float wU = static_cast<float> (upper->second - upper->first + 1);
                    const float h = std::min ({ static_cast<float> (width) / 2.0f, wU / 2.0f, wP / 2.0f });
                    if (h > 0.0f)
                    {
                        f.hasOut = true;
                        f.outStart = static_cast<float> (P.second) + 0.5f - h;
                        f.outEnd = static_cast<float> (P.second) + 0.5f + h;
                        f.activeHi = f.outEnd;
                    }
                }
            }
            for (const auto i : layers[P]) out[i] = f;
        }
    }
    return out;
}

float layerGain (const LayerFade& f, float v, bool linear) noexcept
{
    if (v < f.activeLo || v > f.activeHi) return 0.0f;
    float g = 1.0f;
    if (f.hasIn && v < f.inEnd)
    {
        const float t = std::clamp ((v - f.inStart) / std::max (1e-6f, f.inEnd - f.inStart), 0.0f, 1.0f);
        g *= linear ? t : static_cast<float> (std::sin (static_cast<double> (t) * kHalfPi));
    }
    if (f.hasOut && v > f.outStart)
    {
        const float t = std::clamp ((v - f.outStart) / std::max (1e-6f, f.outEnd - f.outStart), 0.0f, 1.0f);
        g *= linear ? 1.0f - t : static_cast<float> (std::cos (static_cast<double> (t) * kHalfPi));
    }
    return g;
}
} // namespace neptune::core
