#include "neptune/core/Filter.h"

#include <algorithm>

namespace neptune::core
{
FilterSettings sanitised (FilterSettings f)
{
    auto fin = [] (double v, double def) { return std::isfinite (v) ? v : def; };
    f.cutoffHz = std::clamp (fin (f.cutoffHz, filterMaxHz), filterMinHz, filterMaxHz);
    f.resonance = std::clamp (fin (f.resonance, 0.0), 0.0, 1.0);
    f.keyTrack = std::clamp (fin (f.keyTrack, 0.0), -1.0, 2.0);
    f.velocityOctaves = std::clamp (fin (f.velocityOctaves, 0.0), -6.0, 6.0);
    f.envDepthOctaves = std::clamp (fin (f.envDepthOctaves, 0.0), -8.0, 8.0);
    f.drive = std::clamp (fin (f.drive, 0.0), 0.0, 1.0);
    f.slopeDb = f.slopeDb >= 24 ? 24 : 12;
    if (f.model == FilterModel::Ladder) f.mode = FilterMode::LowPass;
    f.env = sanitised (f.env);
    return f;
}

FilterBlock sanitised (FilterBlock b)
{
    for (auto& f : b.filter) f = sanitised (f);
    if (! std::isfinite (b.balance)) b.balance = 0.5;
    b.balance = std::clamp (b.balance, 0.0, 1.0);
    return b;
}

double modulatedCutoffHz (const FilterSettings& f, double baseCutoffHz, int key, float velocity, float envValue, double sampleRate) noexcept
{
    const double octaves = f.keyTrack * static_cast<double> (key - 60) / 12.0
                         + f.velocityOctaves * (static_cast<double> (velocity) - 0.5) * 2.0
                         + f.envDepthOctaves * static_cast<double> (envValue);
    const double hz = baseCutoffHz * std::exp2 (std::clamp (octaves, -12.0, 12.0));
    const double top = std::min (filterMaxHz, 0.45 * sampleRate);
    return std::clamp (hz, filterMinHz, std::max (filterMinHz, top));
}

FilterCoefs makeFilterCoefs (const FilterSettings& f, double cutoffHz, double resonance, double sampleRate) noexcept
{
    FilterCoefs c;
    c.model = f.model;
    c.mode = f.model == FilterModel::Ladder ? FilterMode::LowPass : f.mode;
    c.stages = f.slopeDb >= 24 ? 2 : 1;
    const double fc = std::clamp (cutoffHz, filterMinHz, std::max (filterMinHz, 0.45 * sampleRate));
    const double res = std::clamp (resonance, 0.0, 1.0);
    const double g = std::tan (3.14159265358979323846 * fc / sampleRate);

    if (f.model == FilterModel::SVF)
    {
        const double k = 1.4142135 * (1.0 - res) + 0.04 * res; // de Butterworth à une résonance forte mais stable (Q ~ 25)
        c.k = static_cast<float> (k);
        c.a1 = static_cast<float> (1.0 / (1.0 + g * (g + k)));
        c.a2 = static_cast<float> (g) * c.a1;
        c.a3 = static_cast<float> (g) * c.a2;
        const double kb = 1.4142135;
        c.kb = static_cast<float> (kb);
        c.a1b = static_cast<float> (1.0 / (1.0 + g * (g + kb)));
        c.a2b = static_cast<float> (g) * c.a1b;
        c.a3b = static_cast<float> (g) * c.a2b;
        // coefficients de sortie : LP = v2 ; BP = v1 ; HP = x - k.v1 - v2 ; notch = x - k.v1
        switch (c.mode)
        {
            case FilterMode::LowPass:  c.m0 = 0; c.m1 = 0; c.m2 = 1; c.m0b = 0; c.m1b = 0; c.m2b = 1; break;
            case FilterMode::BandPass: c.m0 = 0; c.m1 = 1; c.m2 = 0; c.m0b = 0; c.m1b = 1; c.m2b = 0; break;
            case FilterMode::HighPass: c.m0 = 1; c.m1 = -c.k; c.m2 = -1; c.m0b = 1; c.m1b = -c.kb; c.m2b = -1; break;
            case FilterMode::Notch:    c.m0 = 1; c.m1 = -c.k; c.m2 = 0; c.m0b = 1; c.m1b = -c.kb; c.m2b = 0; break;
        }
    }
    else
    {
        const double G = g / (1.0 + g);
        c.G = static_cast<float> (G);
        c.g4 = static_cast<float> (G * G * G * G);
        c.kFeedback = static_cast<float> (res * 3.95); // < 4 : à 4 la boucle auto-oscille
        c.invDen = 1.0f / (1.0f + c.kFeedback * c.g4);
        c.drive = static_cast<float> (std::clamp (f.drive, 0.0, 1.0));
    }
    return c;
}
} // namespace neptune::core
