// Analyseur de spectre (porté de ZL Equalizer) : FFT, normalisation, canaux, lissage, mélange des résolutions, retombée, Makima.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/SpectrumAnalyzer.h"

using namespace neptune::core;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Rig
{
    AnalyzerRing ring { 1, 2 };
    SpectrumAnalyzer an;
    std::uint64_t readPos = 0;
    double fs;
    explicit Rig (double rate = 48000.0, bool hq = false) : fs (rate) { an.prepare (rate, hq); }
    void feed (const std::vector<float>& l, const std::vector<float>& r)
    {
        const float* ch[2] = { l.data(), r.data() };
        ring.push (ch, static_cast<int> (l.size()));
    }
    void run (int source = 0)
    {
        an.pull (source, ring, 0, readPos);
        an.analyze (source, 30.0);
    }
    float at (double hz, int source = 0) const
    {
        const auto& f = an.frequencies();
        std::size_t best = 0;
        for (std::size_t i = 0; i < f.size(); ++i) if (std::fabs (f[i] - hz) < std::fabs (f[best] - hz)) best = i;
        return an.spectrumDb (source)[best];
    }
    std::size_t peakBin (int source = 0) const
    {
        const auto& s = an.spectrumDb (source);
        std::size_t best = 0;
        for (std::size_t i = 1; i < s.size(); ++i) if (s[i] > s[best]) best = i;
        return best;
    }
};

SpectrumSettings plain()
{
    SpectrumSettings s;
    s.tiltIdx = 0;                 // pas de pente
    s.smoothType = 0; s.smoothOct = 0;   // lissage minimal (1/24 d'octave)
    s.speedIdx = 0;
    return s;
}

std::vector<float> sine (double hz, double amp, double fs, int n, double phase = 0.0)
{
    std::vector<float> v (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i) v[static_cast<std::size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * i / fs + phase));
    return v;
}

std::vector<float> noise (int n, std::uint32_t seed, float amp)
{
    std::vector<float> v (static_cast<std::size_t> (n));
    for (auto& x : v)
    {
        seed = seed * 1664525u + 1013904223u;
        x = amp * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
    }
    return v;
}
} // namespace

TEST_CASE ("fft: matches a naive DFT", "[spectrum]")
{
    const int order = 5;
    Fft fft (order);
    const std::size_t n = fft.size();
    std::vector<float> re (n), im (n), xr (n), xi (n);
    for (std::size_t i = 0; i < n; ++i) { xr[i] = re[i] = std::sin (0.7f * static_cast<float> (i)) + 0.3f * static_cast<float> (i % 5); xi[i] = im[i] = std::cos (1.3f * static_cast<float> (i)); }
    fft.transform (re.data(), im.data());
    for (std::size_t k = 0; k < n; ++k)
    {
        double sr = 0.0, si = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = -2.0 * kPi * static_cast<double> (k * i) / static_cast<double> (n);
            sr += xr[i] * std::cos (a) - xi[i] * std::sin (a);
            si += xr[i] * std::sin (a) + xi[i] * std::cos (a);
        }
        CHECK (re[k] == Approx (sr).margin (1e-3));
        CHECK (im[k] == Approx (si).margin (1e-3));
    }
}

TEST_CASE ("spectrum: a sine peaks at its frequency and the level scales with the amplitude", "[spectrum]")
{
    Rig a, b;
    a.an.setSettings (plain());
    b.an.setSettings (plain());
    for (int k = 0; k < 4; ++k)
    {
        a.feed (sine (1000.0, 0.5, a.fs, 2048, k * 0.3), sine (1000.0, 0.5, a.fs, 2048, k * 0.3));
        a.run();
        b.feed (sine (1000.0, 0.25, b.fs, 2048, k * 0.3), sine (1000.0, 0.25, b.fs, 2048, k * 0.3));
        b.run();
    }
    const auto& f = a.an.frequencies();
    CHECK (f[a.peakBin()] == Approx (1000.0).margin (15.0));
    CHECK (a.at (1000.0) - b.at (1000.0) == Approx (6.02).margin (0.3));          // moitié d'amplitude : -6 dB
    CHECK (a.at (1000.0) > -12.0f);                                                 // deux voies de 0,5 : -3 dB en théorie, moins l'écart de la raie au centre d'une case et le lissage
    CHECK (a.at (1000.0) < 1.0f);
    CHECK (a.at (6000.0) < a.at (1000.0) - 40.0f);                                  // loin de la raie : très bas
}

TEST_CASE ("spectrum: channel selection (stereo, left, right, mid, side)", "[spectrum]")
{
    auto level = [] (SpectrumChannels ch, float l, float r)
    {
        Rig rig;
        auto s = plain();
        s.channels = ch;
        rig.an.setSettings (s);
        for (int k = 0; k < 4; ++k)
        {
            auto sl = sine (1000.0, l, rig.fs, 2048, k * 0.3), sr = sine (1000.0, r, rig.fs, 2048, k * 0.3);
            rig.feed (sl, sr);
            rig.run();
        }
        return rig.at (1000.0);
    };
    const float stereo = level (SpectrumChannels::Stereo, 0.5f, 0.5f), left = level (SpectrumChannels::Left, 0.5f, 0.0f), right = level (SpectrumChannels::Right, 0.0f, 0.5f);
    CHECK (left == Approx (right).margin (0.1));
    CHECK (stereo - left == Approx (3.01).margin (0.2));                          // les puissances des deux voies s'additionnent
    const float mid = level (SpectrumChannels::Mid, 0.5f, 0.5f), side = level (SpectrumChannels::Side, 0.5f, 0.5f);
    CHECK (mid - left == Approx (3.01).margin (0.2));                             // (L + R) / racine de 2
    CHECK (side < mid - 60.0f);                                                   // signal identique des deux côtés : pas de côtés
    CHECK (level (SpectrumChannels::Side, 0.5f, -0.5f) == Approx (mid).margin (0.2));
}

TEST_CASE ("spectrum: the DC blocker removes a constant offset", "[spectrum]")
{
    Rig rig;
    rig.an.setSettings (plain());
    std::vector<float> dc (2048, 0.5f);
    for (int k = 0; k < 8; ++k) { rig.feed (dc, dc); rig.run(); }
    CHECK (rig.at (20.0) < -40.0f);
}

TEST_CASE ("spectrum: high quality blends three resolutions at the same white-noise level", "[spectrum]")
{
    Rig rig (48000.0, true);
    auto s = plain();
    s.smoothType = 1; s.smoothErb = 2;     // lissage ERB de 1
    rig.an.setSettings (s);
    CHECK (rig.an.highQuality());
    CHECK (rig.an.historySize() == 16384);
    CHECK (rig.an.numPoints() > 500);
    double lo = 0.0, mid = 0.0, hi = 0.0;
    const int frames = 12;
    for (int k = 0; k < frames; ++k)
    {
        rig.feed (noise (2048, 100u + static_cast<std::uint32_t> (k), 0.3f), noise (2048, 900u + static_cast<std::uint32_t> (k), 0.3f));
        rig.run();
        if (k >= 8) { lo += rig.at (150.0); mid += rig.at (1000.0); hi += rig.at (8000.0); }
    }
    lo /= 4; mid /= 4; hi /= 4;
    CHECK (lo == Approx (mid).margin (3.0));
    CHECK (hi == Approx (mid).margin (3.0));
}

TEST_CASE ("spectrum: peaks rise at once and fall gradually", "[spectrum]")
{
    Rig rig;
    auto s = plain();
    s.speedIdx = 4;       // très lent
    rig.an.setSettings (s);
    for (int k = 0; k < 4; ++k) { rig.feed (sine (1000.0, 0.8, rig.fs, 2048, k * 0.3), sine (1000.0, 0.8, rig.fs, 2048, k * 0.3)); rig.run(); }
    const float loud = rig.at (1000.0);
    std::vector<float> silence (2048, 0.0f);
    rig.feed (silence, silence);
    rig.run();
    const float afterOne = rig.at (1000.0);
    CHECK (afterOne < loud);                       // retombe...
    CHECK (afterOne > loud - 20.0f);               // ...mais pas d'un coup
    for (int k = 0; k < 40; ++k) { rig.feed (silence, silence); rig.run(); }
    CHECK (rig.at (1000.0) < loud - 40.0f);
}

TEST_CASE ("spectrum: the tilt rises by its slope per octave around 1 kHz", "[spectrum]")
{
    Rig flat, tilted;
    auto sf = plain(), st = plain();
    st.tiltIdx = 3;       // 4,5 dB/oct
    flat.an.setSettings (sf);
    tilted.an.setSettings (st);
    for (int k = 0; k < 4; ++k)
    {
        for (auto* r : { &flat, &tilted }) { r->feed (sine (4000.0, 0.5, r->fs, 2048, k * 0.3), sine (4000.0, 0.5, r->fs, 2048, k * 0.3)); r->run(); }
    }
    CHECK (tilted.at (4000.0) - flat.at (4000.0) == Approx (9.0).margin (0.3));     // deux octaves au-dessus de 1 kHz
}

TEST_CASE ("makima: interpolates the nodes and reproduces a straight line", "[spectrum]")
{
    std::vector<float> x { 0, 1, 2, 4, 7, 8, 10 }, y;
    for (auto v : x) y.push_back (2.0f * v + 1.0f);
    Makima m;
    m.prepare (x.data(), y.data(), x.size(), 2.0f, 2.0f);
    std::vector<float> xo { 0.0f, 0.5f, 1.0f, 3.0f, 5.5f, 7.5f, 9.0f, 10.0f }, yo (xo.size());
    m.eval (xo.data(), yo.data(), xo.size());
    for (std::size_t i = 0; i < xo.size(); ++i) CHECK (yo[i] == Approx (2.0f * xo[i] + 1.0f).margin (1e-4));
}
