#include "neptune/core/TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "neptune/core/SpectrumAnalyzer.h"

namespace neptune::core::sampleedit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kOrder = 12, kN = 1 << kOrder, kHa = 512;   // fenêtre de 4096, pas d'analyse de 512 (recouvrement 8x à vitesse neutre)

double wrap (double x) noexcept { return x - 2.0 * kPi * std::round (x / (2.0 * kPi)); }

// Vocodeur de phase sur un canal : sortie de longueur ~ len * alpha.
std::vector<float> phaseVocoder (const std::vector<float>& x, double alpha, const Fft& fft, const std::atomic<bool>* cancel)
{
    const auto len = static_cast<std::int64_t> (x.size());
    const int Hs = std::max (1, static_cast<int> (std::lround (kHa * alpha)));
    const double ratio = static_cast<double> (Hs) / kHa;
    // entrée avec N de silence de part et d'autre
    std::vector<float> in (static_cast<std::size_t> (len + 2 * kN), 0.0f);
    std::copy (x.begin(), x.end(), in.begin() + kN);
    std::vector<float> win (kN);
    for (int i = 0; i < kN; ++i) win[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * i / kN));

    const std::int64_t frames = (len + kN) / kHa + 1;
    std::vector<float> out (static_cast<std::size_t> (frames * Hs + kN + kN), 0.0f), norm (out.size(), 0.0f);
    std::vector<float> re (kN), im (kN);
    std::vector<double> prevPhase (kN / 2 + 1, 0.0), synPhase (kN / 2 + 1, 0.0), mag (kN / 2 + 1), phase (kN / 2 + 1), newPhase (kN / 2 + 1);
    std::vector<int> peakOf (kN / 2 + 1);
    for (std::int64_t m = 0; m < frames; ++m)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed)) return {};
        const std::int64_t a = m * kHa;
        for (int i = 0; i < kN; ++i) { re[static_cast<std::size_t> (i)] = in[static_cast<std::size_t> (a + i)] * win[static_cast<std::size_t> (i)]; im[static_cast<std::size_t> (i)] = 0.0f; }
        fft.transform (re.data(), im.data());
        for (int k = 0; k <= kN / 2; ++k)
        {
            mag[static_cast<std::size_t> (k)] = std::hypot (re[static_cast<std::size_t> (k)], im[static_cast<std::size_t> (k)]);
            phase[static_cast<std::size_t> (k)] = std::atan2 (im[static_cast<std::size_t> (k)], re[static_cast<std::size_t> (k)]);
        }
        // pics du spectre d'amplitude, et région d'influence de chaque pic (jusqu'au creux le plus proche entre deux pics)
        std::vector<int> peaks;
        for (int k = 1; k < kN / 2; ++k)
            if (mag[static_cast<std::size_t> (k)] > mag[static_cast<std::size_t> (k - 1)] && mag[static_cast<std::size_t> (k)] >= mag[static_cast<std::size_t> (k + 1)]) peaks.push_back (k);
        if (peaks.empty()) peaks.push_back (1);
        int pi = 0;
        for (int k = 0; k <= kN / 2; ++k)
        {
            while (pi + 1 < static_cast<int> (peaks.size()) && k > (peaks[static_cast<std::size_t> (pi)] + peaks[static_cast<std::size_t> (pi + 1)]) / 2) ++pi;
            peakOf[static_cast<std::size_t> (k)] = peaks[static_cast<std::size_t> (pi)];
        }
        if (m == 0)
        {
            for (int k = 0; k <= kN / 2; ++k) synPhase[static_cast<std::size_t> (k)] = phase[static_cast<std::size_t> (k)];
        }
        else
        {
            for (const int pk : peaks)
            {
                const double omega = 2.0 * kPi * pk / kN;
                const double dphi = wrap (phase[static_cast<std::size_t> (pk)] - prevPhase[static_cast<std::size_t> (pk)] - omega * kHa);
                const double freq = omega + dphi / kHa;   // fréquence instantanée (rad/échantillon)
                newPhase[static_cast<std::size_t> (pk)] = synPhase[static_cast<std::size_t> (pk)] + freq * Hs;
            }
            for (int k = 0; k <= kN / 2; ++k)
            {
                const int pk = peakOf[static_cast<std::size_t> (k)];
                synPhase[static_cast<std::size_t> (k)] = newPhase[static_cast<std::size_t> (pk)] + (phase[static_cast<std::size_t> (k)] - phase[static_cast<std::size_t> (pk)]);
            }
        }
        prevPhase = phase;
        // synthèse : spectre hermitien -> FFT inverse (par permutation re/im)
        for (int k = 0; k <= kN / 2; ++k)
        {
            const double mg = mag[static_cast<std::size_t> (k)], ph = synPhase[static_cast<std::size_t> (k)];
            const float r = static_cast<float> (mg * std::cos (ph)), i = static_cast<float> (mg * std::sin (ph));
            re[static_cast<std::size_t> (k)] = r; im[static_cast<std::size_t> (k)] = i;
            if (k > 0 && k < kN / 2) { re[static_cast<std::size_t> (kN - k)] = r; im[static_cast<std::size_t> (kN - k)] = -i; }
        }
        std::swap (re, im);                      // IFFT(x) = swap(FFT(swap(x))) / N
        fft.transform (re.data(), im.data());
        const std::int64_t o = m * Hs;
        for (int i = 0; i < kN; ++i)
        {
            const float w = win[static_cast<std::size_t> (i)];
            out[static_cast<std::size_t> (o + i)] += im[static_cast<std::size_t> (i)] / kN * w;   // après le swap, la partie réelle du résultat est dans `im`
            norm[static_cast<std::size_t> (o + i)] += w * w;
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = norm[i] > 1.0e-3f ? out[i] / norm[i] : 0.0f;
    // retire le silence ajouté en tête (N échantillons d'entrée = N * ratio de sortie) et ramène à la longueur voulue
    const auto start = static_cast<std::int64_t> (std::llround (kN * ratio));
    const auto want = static_cast<std::int64_t> (std::llround (static_cast<double> (len) * ratio));
    std::vector<float> res (static_cast<std::size_t> (want), 0.0f);
    for (std::int64_t i = 0; i < want; ++i)
        if (start + i < static_cast<std::int64_t> (out.size())) res[static_cast<std::size_t> (i)] = out[static_cast<std::size_t> (start + i)];
    return res;
}

// Ré-échantillonnage par sinus cardinal fenêtré : lit `x` au pas `step` (> 1 : plus aigu et plus court). Coupure sous Nyquist quand step > 1.
std::vector<float> resample (const std::vector<float>& x, double step, const std::atomic<bool>* cancel)
{
    const auto n = static_cast<std::int64_t> (x.size());
    const auto outLen = static_cast<std::int64_t> (std::floor (static_cast<double> (n) / step));
    std::vector<float> out (static_cast<std::size_t> (std::max<std::int64_t> (0, outLen)), 0.0f);
    const double fc = std::min (1.0, 1.0 / step);   // coupure relative
    constexpr int kHalf = 24;
    for (std::int64_t j = 0; j < outLen; ++j)
    {
        if ((j & 0xFFFF) == 0 && cancel != nullptr && cancel->load (std::memory_order_relaxed)) return {};
        const double pos = static_cast<double> (j) * step;
        const auto base = static_cast<std::int64_t> (std::floor (pos));
        const double frac = pos - static_cast<double> (base);
        double acc = 0.0;
        for (int t = -kHalf + 1; t <= kHalf; ++t)
        {
            const std::int64_t idx = base + t;
            if (idx < 0 || idx >= n) continue;
            const double d = static_cast<double> (t) - frac;
            const double sinc = std::fabs (d) < 1.0e-9 ? fc : std::sin (kPi * fc * d) / (kPi * d);
            const double w = 0.42 + 0.5 * std::cos (kPi * d / kHalf) + 0.08 * std::cos (2.0 * kPi * d / kHalf);   // Blackman
            acc += static_cast<double> (x[static_cast<std::size_t> (idx)]) * sinc * w;
        }
        out[static_cast<std::size_t> (j)] = static_cast<float> (acc);
    }
    return out;
}
} // namespace

Buffer stretchAndShift (const Buffer& in, double timeRatio, double semitones, const std::atomic<bool>* cancel)
{
    timeRatio = std::clamp (timeRatio, 0.25, 4.0);
    semitones = std::clamp (semitones, -24.0, 24.0);
    const double p = std::pow (2.0, semitones / 12.0);
    const double alpha = timeRatio * p;   // étirement du vocodeur ; le ré-échantillonnage au pas p ramène la durée à timeRatio
    Buffer out;
    out.channels = in.channels;
    out.sampleRate = in.sampleRate;
    if (in.frames() < 2) { out = in; return out; }
    if (std::fabs (alpha - 1.0) < 1.0e-6 && std::fabs (p - 1.0) < 1.0e-9) return in;

    static const Fft fft (kOrder);   // tables en lecture seule : partagées
    std::vector<std::vector<float>> chans (static_cast<std::size_t> (in.channels));
    for (int c = 0; c < in.channels; ++c)
    {
        std::vector<float> x (static_cast<std::size_t> (in.frames()));
        for (std::int64_t i = 0; i < in.frames(); ++i) x[static_cast<std::size_t> (i)] = in.at (i, c);
        if (std::fabs (alpha - 1.0) > 1.0e-6) x = phaseVocoder (x, alpha, fft, cancel);
        if (x.empty() && (cancel != nullptr && cancel->load())) return {};
        if (std::fabs (p - 1.0) > 1.0e-9) x = resample (x, p, cancel);
        if (x.empty() && (cancel != nullptr && cancel->load())) return {};
        chans[static_cast<std::size_t> (c)] = std::move (x);
    }
    std::size_t frames = chans[0].size();
    for (const auto& c : chans) frames = std::min (frames, c.size());
    out.data.resize (frames * static_cast<std::size_t> (in.channels));
    for (std::size_t i = 0; i < frames; ++i)
        for (int c = 0; c < in.channels; ++c) out.data[i * static_cast<std::size_t> (in.channels) + static_cast<std::size_t> (c)] = chans[static_cast<std::size_t> (c)][i];
    return out;
}
} // namespace neptune::core::sampleedit
