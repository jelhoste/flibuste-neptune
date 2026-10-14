#include "neptune/core/WaveCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>

namespace neptune::core
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

#define W(id, key, cat, analytic) { id, key, "wave." key, WaveCategory::cat, analytic }
const WaveInfo kInfos[kNumWaves] = {
    W (kWaveSine, "sine", Digital, true),
    W (kWaveTriangle, "triangle", Digital, true),
    W (kWaveSaw, "saw", Digital, true),
    W (kWaveSquare, "square", Digital, true),
    W (kWavePulse, "pulse", Digital, true),
    W (kWaveHalfSine, "half_sine", Digital, false),
    W (kWaveFullRect, "full_rectified", Digital, false),
    W (kWaveStaircase, "staircase", Digital, false),
    W (kWaveParabola, "parabola", Digital, false),
    W (kWaveTrapezoid, "trapezoid", Digital, false),
    W (kWaveLadderSaw, "ladder_saw", Analog, false),
    W (kWaveSoftSquare, "soft_square", Analog, false),
    W (kWaveDoubleSaw, "double_saw", Analog, false),
    W (kWaveReverseSaw, "reverse_saw", Analog, false),
    W (kWaveWarmSaw, "warm_saw", Analog, false),
    W (kWaveNarrowPulse, "narrow_pulse", Analog, false),
    W (kWaveTriSaw, "tri_saw", Analog, false),
    W (kWaveSineStack, "sine_stack", Analog, false),
    W (kWaveOrgan8, "organ_8", Complex, false),
    W (kWaveOrganFull, "organ_full", Complex, false),
    W (kWaveVoxA, "vox_a", Complex, false),
    W (kWaveVoxE, "vox_e", Complex, false),
    W (kWaveVoxO, "vox_o", Complex, false),
    W (kWaveReed, "reed", Complex, false),
    W (kWaveBrass, "brass", Complex, false),
    W (kWaveStrings, "strings", Complex, false),
    W (kWaveGlass, "glass", Complex, false),
    W (kWaveHollow, "hollow", Complex, false),
    W (kWaveDigitalNoise, "digital_noise", Complex, false),
    W (kWaveOctaves, "octaves", Complex, false),
    W (kWaveFifths, "fifths", Complex, false),
    W (kWaveComb, "comb", Complex, false),
};
#undef W

using Spectrum = std::array<std::complex<double>, WaveTables::kMaxHarmonics + 1>; // harmonique h : a_h cos + b_h sin

// FFT radix-2 en place (construction des tables, hors temps réel).
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / static_cast<double> (len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Spectre d'une forme définie dans le temps (échantillonnée finement puis analysée) : a_h = coefficient du cosinus, b_h du sinus.
Spectrum fromTime (const std::function<double (double)>& f)
{
    constexpr std::size_t n = 16384; // sur-échantillonné : le repliement de l'analyse reste sous les harmoniques utiles
    std::vector<std::complex<double>> x (n);
    for (std::size_t i = 0; i < n; ++i) x[i] = f (static_cast<double> (i) / static_cast<double> (n));
    fft (x, false);
    Spectrum s {};
    for (int h = 1; h <= WaveTables::kMaxHarmonics; ++h)
    {
        const auto c = x[static_cast<std::size_t> (h)] * (2.0 / static_cast<double> (n)); // = a - i.b
        s[static_cast<std::size_t> (h)] = { c.real(), -c.imag() };
    }
    return s;
}

// Spectre défini par amplitudes de sinus (phase nulle) : b_h = amp(h).
Spectrum fromSines (const std::function<double (int)>& amp)
{
    Spectrum s {};
    for (int h = 1; h <= WaveTables::kMaxHarmonics; ++h) s[static_cast<std::size_t> (h)] = { 0.0, amp (h) };
    return s;
}

double gaussian (double x, double centre, double width) { const double d = (x - centre) / width; return std::exp (-0.5 * d * d); }

double sawTime (double t) { return 2.0 * t - 1.0; }
double triTime (double t) { return 1.0 - 4.0 * std::abs (t - 0.5); }

Spectrum spectrumOf (int wave, double pulseWidth)
{
    auto sawAmp = [] (int h) { return -(2.0 / kPi) / h; };
    switch (wave)
    {
        case kWaveSine: return fromSines ([] (int h) { return h == 1 ? 1.0 : 0.0; });
        case kWaveTriangle: return fromTime (triTime);
        case kWaveSaw: return fromTime (sawTime);
        case kWaveSquare: return fromTime ([] (double t) { return t < 0.5 ? 1.0 : -1.0; });
        case kWavePulse: return fromTime ([pulseWidth] (double t) { return t < pulseWidth ? 1.0 : -1.0; });
        case kWaveHalfSine: return fromTime ([] (double t) { return std::max (0.0, std::sin (2.0 * kPi * t)) * 2.0 - 1.0; });
        case kWaveFullRect: return fromTime ([] (double t) { return std::abs (std::sin (2.0 * kPi * t)) * 2.0 - 1.0; });
        case kWaveStaircase: return fromTime ([] (double t) { return std::floor (t * 8.0) / 3.5 - 1.0; });
        case kWaveParabola: return fromTime ([] (double t) { const double x = 2.0 * t - 1.0; return 1.0 - 2.0 * x * x; });
        case kWaveTrapezoid: return fromTime ([] (double t) { return std::clamp (triTime (t) * 1.6, -1.0, 1.0); });

        case kWaveLadderSaw: // dent de scie passée dans un passe-bas résonant du 2e ordre (coupure vers l'harmonique 14)
            return fromSines ([&] (int h)
            {
                const double r = h / 14.0;
                const double mag = 1.0 / std::sqrt ((1.0 - r * r) * (1.0 - r * r) + (r / 1.6) * (r / 1.6));
                return sawAmp (h) * mag;
            });
        case kWaveSoftSquare: return fromSines ([] (int h) { return h % 2 == 1 ? 1.0 / std::pow (static_cast<double> (h), 1.7) : 0.0; });
        case kWaveDoubleSaw: return fromTime ([] (double t) { return 0.5 * (sawTime (t) + sawTime (std::fmod (t + 0.27, 1.0))); });
        case kWaveReverseSaw: return fromTime ([] (double t) { return -sawTime (t); });
        case kWaveWarmSaw: return fromSines ([&] (int h) { return -(2.0 / kPi) / std::pow (static_cast<double> (h), 1.35); });
        case kWaveNarrowPulse: return fromTime ([] (double t) { return t < 0.125 ? 1.0 : -1.0; });
        case kWaveTriSaw: return fromTime ([] (double t) { return 0.5 * (triTime (t) + sawTime (t)); });
        case kWaveSineStack: return fromSines ([] (int h) { return h == 1 ? 1.0 : h == 2 ? 0.55 : h == 4 ? 0.3 : 0.0; });

        case kWaveOrgan8: return fromSines ([] (int h)
        {
            switch (h) { case 1: return 1.0; case 2: return 0.9; case 3: return 0.7; case 4: return 0.55; case 6: return 0.35; case 8: return 0.25; default: return 0.0; }
        });
        case kWaveOrganFull: return fromSines ([] (int h) { return h <= 9 ? 1.0 / std::sqrt (static_cast<double> (h)) : 0.0; });
        // voyelles : enveloppe spectrale (formants) sur les harmoniques d'un fondamental de référence de 110 Hz
        case kWaveVoxA: return fromSines ([] (int h) { const double f = h * 110.0; return (gaussian (f, 800, 130) + 0.7 * gaussian (f, 1150, 150) + 0.35 * gaussian (f, 2900, 300) + 0.05) / h * 4.0; });
        case kWaveVoxE: return fromSines ([] (int h) { const double f = h * 110.0; return (gaussian (f, 350, 90) + 0.6 * gaussian (f, 2000, 250) + 0.4 * gaussian (f, 2800, 300) + 0.04) / h * 4.0; });
        case kWaveVoxO: return fromSines ([] (int h) { const double f = h * 110.0; return (gaussian (f, 450, 100) + 0.55 * gaussian (f, 800, 120) + 0.1 * gaussian (f, 2830, 300) + 0.03) / h * 4.0; });
        case kWaveReed: return fromSines ([] (int h) { return (h % 2 == 1 ? 1.0 : 0.45) / std::pow (static_cast<double> (h), 1.1) * (h == 3 ? 1.6 : 1.0); });
        case kWaveBrass: return fromSines ([] (int h) { return h * std::exp (-h / 5.5) * 0.9; });
        case kWaveStrings: return fromSines ([] (int h) { return (h % 4 == 0 ? 0.25 : 1.0) / h; });
        case kWaveGlass: return fromSines ([] (int h) { return (h % 2 == 1 && h % 3 != 0) ? 1.0 / std::sqrt (static_cast<double> (h)) : 0.0; });
        case kWaveHollow: return fromSines ([] (int h) { return (h % 4 == 1) ? 1.0 / h : 0.0; });
        case kWaveDigitalNoise:
        {
            Spectrum s {};
            std::uint32_t r = 0x2F6E2B1u;
            auto next = [&r] { r = r * 1664525u + 1013904223u; return static_cast<double> (r >> 8) / static_cast<double> (1u << 24); };
            for (int h = 1; h <= 96; ++h)
            {
                const double a = (0.4 + 0.6 * next()) / std::sqrt (static_cast<double> (h)), ph = 2.0 * kPi * next();
                s[static_cast<std::size_t> (h)] = { a * std::cos (ph), a * std::sin (ph) };
            }
            return s;
        }
        case kWaveOctaves: return fromSines ([] (int h) { return (h == 1 || h == 2 || h == 4 || h == 8 || h == 16) ? 1.0 : 0.0; });
        case kWaveFifths: return fromSines ([] (int h) { return (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 12) ? 1.0 / std::sqrt (static_cast<double> (h)) : 0.0; });
        case kWaveComb: return fromSines ([] (int h) { return h % 3 == 0 ? 1.0 / std::sqrt (static_cast<double> (h)) : 0.0; });
        default: return fromSines ([] (int h) { return h == 1 ? 1.0 : 0.0; });
    }
}

// Un cycle de `n` échantillons avec les `maxHarmonics` premières harmoniques du spectre.
void synthesise (const Spectrum& s, int maxHarmonics, std::vector<double>& out, std::size_t n)
{
    std::vector<std::complex<double>> x (n, { 0.0, 0.0 });
    const int top = std::min (maxHarmonics, static_cast<int> (n / 2) - 1);
    for (int h = 1; h <= top; ++h)
    {
        // x[t] = sum a cos + b sin = Re( (a - i.b) e^{i w} ) ; on remplit le demi-spectre (a - i.b)/2 et son conjugué
        const auto c = std::complex<double> (s[static_cast<std::size_t> (h)].real(), -s[static_cast<std::size_t> (h)].imag()) * 0.5;
        x[static_cast<std::size_t> (h)] = c;
        x[n - static_cast<std::size_t> (h)] = std::conj (c);
    }
    fft (x, true);
    out.resize (n);
    for (std::size_t i = 0; i < n; ++i) out[i] = x[i].real();
}
} // namespace

const WaveInfo& waveInfo (int id) noexcept { return kInfos[std::clamp (id, 0, static_cast<int> (kNumWaves) - 1)]; }

int waveIdFromKey (std::string_view key) noexcept
{
    for (const auto& w : kInfos)
        if (w.key == key) return w.id;
    return -1;
}

bool isAnalyticWave (int id) noexcept { return waveInfo (id).analytic; }

int WaveTables::mipFor (double dt) noexcept
{
    const double allowed = 0.5 / std::max (dt, 1e-9); // harmoniques qui tiennent sous Nyquist
    int mip = 0;
    while (mip < kMips - 1 && static_cast<double> (kMaxHarmonics >> mip) > allowed) ++mip;
    return mip;
}

WaveTables::WaveTables() : data_ (static_cast<std::size_t> (kNumWaves) * kMips * (kSize + 1), 0.0f)
{
    std::vector<double> cycle;
    for (int w = 0; w < kNumWaves; ++w)
    {
        const Spectrum s = spectrumOf (w, 0.25);
        // normalisation : le pic de la table la plus riche vaut 1 (les autres tables gardent la même échelle)
        synthesise (s, kMaxHarmonics, cycle, kSize);
        double peak = 1e-9;
        for (const double v : cycle) peak = std::max (peak, std::abs (v));
        const double scale = 1.0 / peak;
        for (int m = 0; m < kMips; ++m)
        {
            synthesise (s, kMaxHarmonics >> m, cycle, kSize);
            float* t = data_.data() + (static_cast<std::size_t> (w) * kMips + static_cast<std::size_t> (m)) * (kSize + 1);
            double mean = 0.0;
            for (const double v : cycle) mean += v;
            mean /= static_cast<double> (kSize);
            for (int i = 0; i < kSize; ++i) t[i] = static_cast<float> ((cycle[static_cast<std::size_t> (i)] - mean) * scale);
            t[kSize] = t[0];
        }
    }
}

const WaveTables& WaveTables::instance()
{
    static const WaveTables tables; // initialisation thread-safe (C++11)
    return tables;
}

void renderWaveCycle (int wave, float* out, int n, double pulseWidth)
{
    if (out == nullptr || n <= 0) return;
    const Spectrum s = spectrumOf (std::clamp (wave, 0, static_cast<int> (kNumWaves) - 1), std::clamp (pulseWidth, 0.02, 0.98));
    std::vector<double> cycle;
    synthesise (s, 64, cycle, 2048);
    double peak = 1e-9, mean = 0.0;
    for (const double v : cycle) { peak = std::max (peak, std::abs (v)); mean += v; }
    mean /= static_cast<double> (cycle.size());
    peak = 0.0;
    for (const double v : cycle) peak = std::max (peak, std::abs (v - mean));
    for (int i = 0; i < n; ++i)
    {
        const double pos = static_cast<double> (i) / static_cast<double> (n) * static_cast<double> (cycle.size());
        out[i] = static_cast<float> ((cycle[static_cast<std::size_t> (pos) % cycle.size()] - mean) / std::max (peak, 1e-9));
    }
}
} // namespace neptune::core
