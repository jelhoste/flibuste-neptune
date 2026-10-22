#pragma once
// Niveau « entendu » d'un signal, pour le volume de relâchement lié au niveau (spec §3) : crête, RMS et énergie pondérée K (LUFS, BS.1770-4),
// suivis échantillon par échantillon sans allocation. Les mêmes calculs servent à mesurer, hors temps réel, le niveau de référence d'un fichier à un instant donné.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace neptune::core
{
struct LevelCoefs
{
    double sb0 = 1, sb1 = 0, sb2 = 0, sa1 = 0, sa2 = 0;   // étage 1 : étagère aiguë
    double hb0 = 1, hb1 = -2, hb2 = 1, ha1 = 0, ha2 = 0;  // étage 2 : passe-haut
    float peakDecay = 0.999f, rmsCoef = 0.001f, lufsCoef = 0.0005f;

    void prepare (double sampleRate) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double f1 = 1681.974450955533, g1 = 3.999843853973347, q1 = 0.7071752369554196;
        const double k1 = std::tan (kPi * f1 / sr), vh = std::pow (10.0, g1 / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k1 / q1 + k1 * k1;
        sb0 = (vh + vb * k1 / q1 + k1 * k1) / a0; sb1 = 2.0 * (k1 * k1 - vh) / a0; sb2 = (vh - vb * k1 / q1 + k1 * k1) / a0;
        sa1 = 2.0 * (k1 * k1 - 1.0) / a0; sa2 = (1.0 - k1 / q1 + k1 * k1) / a0;
        const double f2 = 38.13547087602444, q2 = 0.5003270373238773;
        const double k2 = std::tan (kPi * f2 / sr);
        const double c0 = 1.0 + k2 / q2 + k2 * k2;
        hb0 = 1.0; hb1 = -2.0; hb2 = 1.0;
        ha1 = 2.0 * (k2 * k2 - 1.0) / c0; ha2 = (1.0 - k2 / q2 + k2 * k2) / c0;
        peakDecay = static_cast<float> (std::exp (-1.0 / (0.05 * sr)));
        rmsCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.05 * sr)));
        lufsCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.1 * sr)));
    }
};

struct LevelTracker
{
    float peak = 0.0f, ms = 0.0f, msK = 0.0f;
    float s[2][2] {};   // forme transposée II : deux étages, deux valeurs

    void process (const LevelCoefs& c, float xl, float xr) noexcept
    {
        const float m = 0.70710678f * (xl + xr);   // somme en puissance pour des voies semblables (comme LUFS)
        peak = std::max (std::max (std::fabs (xl), std::fabs (xr)), peak * c.peakDecay);
        ms += c.rmsCoef * (std::max (xl * xl, xr * xr) - ms);   // RMS de la voie la plus forte
        const double y1 = c.sb0 * m + s[0][0];
        s[0][0] = static_cast<float> (c.sb1 * m - c.sa1 * y1 + s[0][1]);
        s[0][1] = static_cast<float> (c.sb2 * m - c.sa2 * y1);
        const double y2 = c.hb0 * y1 + s[1][0];
        s[1][0] = static_cast<float> (c.hb1 * y1 - c.ha1 * y2 + s[1][1]);
        s[1][1] = static_cast<float> (c.hb2 * y1 - c.ha2 * y2);
        msK += c.lufsCoef * (static_cast<float> (y2 * y2) - msK);
    }
    // Niveau en dB : meter 0 = crête, 1 = RMS, 2 = LUFS (pondération K).
    static double toDb (int meter, float peak, float ms, float msK) noexcept
    {
        return meter == 0 ? 20.0 * std::log10 (std::max (1.0e-9f, peak))
             : meter == 1 ? 10.0 * std::log10 (std::max (1.0e-12f, ms))
                          : -0.691 + 10.0 * std::log10 (std::max (1.0e-12f, msK));
    }
    double db (int meter) const noexcept { return toDb (meter, peak, ms, msK); }
};

// Niveau causal (ce que le suivi des voix lirait à cet instant) d'un fichier à `frame`, en dB ; faux si illisible ou silencieux (< -90 dB).
bool measureLevelAt (const std::filesystem::path&, std::int64_t frame, int meter, double& db);
} // namespace neptune::core
