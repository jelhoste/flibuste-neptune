#pragma once
// Filtres du second ordre (RBJ) et cascade Butterworth, en précision double, partagés par les modules stock (détection du sidechain,
// égaliseurs...). Aucune allocation.
#include <algorithm>
#include <array>
#include <cmath>

namespace neptune::core
{
constexpr double kBiquadPi = 3.14159265358979323846;   // M_PI n'existe pas partout (MSVC)
struct BiquadCoefs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   // a0 normalisé à 1
    static BiquadCoefs peaking (double fs, double f, double gainDb, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, al = std::sin (w) / (2.0 * std::max (q, 0.05));
        const double c = std::cos (w), a0 = 1.0 + al / A;
        return { (1.0 + al * A) / a0, -2.0 * c / a0, (1.0 - al * A) / a0, -2.0 * c / a0, (1.0 - al / A) / a0 };
    }
    static BiquadCoefs lowpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        return { (1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    static BiquadCoefs highpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    // Étagères RBJ (gain en dB, pente fixée par q), filtre coupe-bande et passe-bande (gain 0 dB au centre).
    static BiquadCoefs lowShelf (double fs, double f, double gainDb, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * std::max (q, 0.05));
        const double sq = 2.0 * std::sqrt (A) * al, a0 = (A + 1.0) + (A - 1.0) * c + sq;
        return { A * ((A + 1.0) - (A - 1.0) * c + sq) / a0, 2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0, A * ((A + 1.0) - (A - 1.0) * c - sq) / a0,
                 -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0, ((A + 1.0) + (A - 1.0) * c - sq) / a0 };
    }
    static BiquadCoefs highShelf (double fs, double f, double gainDb, double q) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * std::max (q, 0.05));
        const double sq = 2.0 * std::sqrt (A) * al, a0 = (A + 1.0) - (A - 1.0) * c + sq;
        return { A * ((A + 1.0) + (A - 1.0) * c + sq) / a0, -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0, A * ((A + 1.0) + (A - 1.0) * c - sq) / a0,
                 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0, ((A + 1.0) - (A - 1.0) * c - sq) / a0 };
    }
    static BiquadCoefs notch (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * std::max (q, 0.05)), a0 = 1.0 + al;
        return { 1.0 / a0, -2.0 * c / a0, 1.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    static BiquadCoefs bandpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kBiquadPi * std::min (f, 0.49 * fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0 * std::max (q, 0.05)), a0 = 1.0 + al;
        return { al / a0, 0.0, -al / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    // Premier ordre (transformée bilinéaire), pour les pentes de 6 dB/oct.
    static BiquadCoefs lowpass1 (double fs, double f) noexcept
    {
        const double k = std::tan (kBiquadPi * std::min (f, 0.49 * fs) / fs), n = 1.0 / (1.0 + k);
        return { k * n, k * n, 0.0, (k - 1.0) * n, 0.0 };
    }
    static BiquadCoefs highpass1 (double fs, double f) noexcept
    {
        const double k = std::tan (kBiquadPi * std::min (f, 0.49 * fs) / fs), n = 1.0 / (1.0 + k);
        return { n, -n, 0.0, (k - 1.0) * n, 0.0 };
    }
    // Module de la réponse à la fréquence f (pour tracer les courbes : mêmes coefficients que le DSP).
    double magnitude (double fs, double f) const noexcept
    {
        const double w = 2.0 * kBiquadPi * f / fs, c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di));
    }
};

class Biquad
{
public:
    void set (const BiquadCoefs& c) noexcept { c_ = c; }
    void reset() noexcept { z1_ = z2_ = 0.0; }
    float process (float x) noexcept
    {
        const double in = x, y = c_.b0 * in + z1_;
        z1_ = c_.b1 * in - c_.a1 * y + z2_;
        z2_ = c_.b2 * in - c_.a2 * y;
        return static_cast<float> (y);
    }
    const BiquadCoefs& coefs() const noexcept { return c_; }
private:
    BiquadCoefs c_;
    double z1_ = 0.0, z2_ = 0.0;
};

// Filtre passe-haut ou passe-bas de Butterworth d'ordre slope / 6 (pente de 6 à 48 dB/octave) : une section du premier ordre si l'ordre
// est impair, puis des sections du second ordre.
class ButterworthCascade
{
public:
    static constexpr int kMaxSections = 9;   // ordre 16 (96 dB/octave) au plus
    void design (bool highpass, double fs, double f, int slopeDbPerOct) noexcept
    {
        const int order = std::clamp (slopeDbPerOct / 6, 1, 16);
        count_ = 0;
        if (order % 2 == 1) sections_[static_cast<std::size_t> (count_++)].set (highpass ? BiquadCoefs::highpass1 (fs, f) : BiquadCoefs::lowpass1 (fs, f));
        for (int k = 1; k <= order / 2; ++k)
        {
            // pôles à kπ/N (ordre impair : un pôle réel, traité par la section du premier ordre) ou (k - 1/2)π/N (ordre pair), mesurés depuis l'axe réel négatif
            const double theta = (order % 2 == 1 ? k : k - 0.5) * kBiquadPi / order;
            const double q = 1.0 / (2.0 * std::cos (theta));
            sections_[static_cast<std::size_t> (count_++)].set (highpass ? BiquadCoefs::highpass (fs, f, q) : BiquadCoefs::lowpass (fs, f, q));
        }
    }
    void reset() noexcept { for (auto& s : sections_) s.reset(); }
    float process (float x) noexcept
    {
        for (int i = 0; i < count_; ++i) x = sections_[static_cast<std::size_t> (i)].process (x);
        return x;
    }
    double magnitude (double fs, double f) const noexcept
    {
        double m = 1.0;
        for (int i = 0; i < count_; ++i) m *= sections_[static_cast<std::size_t> (i)].coefs().magnitude (fs, f);
        return m;
    }
private:
    std::array<Biquad, kMaxSections> sections_;
    int count_ = 0;
};
} // namespace neptune::core
