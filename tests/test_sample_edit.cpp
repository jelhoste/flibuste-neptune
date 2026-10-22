// Éditeur de sample : opérations (rognage, gain, DC), analyses (silence, transitoires, boucle), aimantation au zéro, fichiers dérivés.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>

#include "neptune/core/SampleEdit.h"

using namespace neptune::core::sampleedit;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 48000.0;
Buffer sine (double hz, std::int64_t frames, double amp = 0.5, int channels = 1)
{
    Buffer b;
    b.channels = channels;
    b.sampleRate = kSr;
    b.data.resize (static_cast<std::size_t> (frames * channels));
    for (std::int64_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            b.data[static_cast<std::size_t> (i * channels + c)] = static_cast<float> (amp * std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / kSr));
    return b;
}
}

TEST_CASE ("trimmed keeps exactly the region", "[sampleedit]")
{
    auto b = sine (440.0, 1000);
    const auto t = trimmed (b, 100, 400);
    REQUIRE (t.frames() == 300);
    REQUIRE (t.at (0, 0) == b.at (100, 0));
    REQUIRE (t.at (299, 0) == b.at (399, 0));
    REQUIRE (trimmed (b, 900, 5000).frames() == 100);   // fin au-delà : bornée
}

TEST_CASE ("gain and normalization", "[sampleedit]")
{
    auto b = sine (440.0, 4800, 0.25);
    REQUIRE_THAT (peakAmplitude (b), WithinAbs (0.25, 1e-3));
    bool limited = false;
    const double g = normalizeGainDb (b, 0, -1, NormalizeTarget::Peak, -1.0, &limited);
    applyGainDb (b, g);
    REQUIRE_THAT (toDb (peakAmplitude (b)), WithinAbs (-1.0, 0.05));
    REQUIRE_FALSE (limited);

    auto c = sine (440.0, 4800, 0.25);
    // RMS d'un sinus de crête 0,25 = -15 dB ; viser -3 dB RMS demanderait une crête > 0 dBFS : le gain est borné
    const double g2 = normalizeGainDb (c, 0, -1, NormalizeTarget::Rms, -3.0, &limited);
    REQUIRE (limited);
    applyGainDb (c, g2);
    REQUIRE (peakAmplitude (c) <= 1.0 + 1e-4);
    // la mesure ne regarde que la région
    auto d = sine (440.0, 4800, 0.1);
    for (std::int64_t i = 2400; i < 4800; ++i) d.data[static_cast<std::size_t> (i)] = 0.8f;
    REQUIRE_THAT (peakAmplitude (d, 0, 2400), WithinAbs (0.1, 1e-3));
}

TEST_CASE ("removeDc cancels a constant offset", "[sampleedit]")
{
    auto b = sine (440.0, 48000, 0.3);
    for (auto& v : b.data) v += 0.2f;
    REQUIRE (dcOffset (b) > 0.19);
    removeDc (b);
    REQUIRE (std::fabs (dcOffset (b, 4800)) < 0.01);
    REQUIRE_THAT (peakAmplitude (b, 4800), WithinAbs (0.3, 0.02));
}

TEST_CASE ("detectSilence finds the useful sound", "[sampleedit]")
{
    Buffer b;
    b.channels = 1; b.sampleRate = kSr;
    b.data.assign (10000, 0.0f);
    for (int i = 3000; i < 7000; ++i) b.data[static_cast<std::size_t> (i)] = 0.5f * std::sin (0.1f * static_cast<float> (i));
    b.data[100] = 0.001f;   // bruit sous le seuil
    const auto s = detectSilence (b, -40.0);
    REQUIRE (s.found);
    REQUIRE (s.start >= 3000);
    REQUIRE (s.start < 3100);
    REQUIRE (s.end > 6900);
    REQUIRE (s.end <= 7000);
    Buffer silent; silent.channels = 1; silent.data.assign (100, 0.0f);
    REQUIRE_FALSE (detectSilence (silent, -60.0).found);
}

TEST_CASE ("detectTransients finds separate hits", "[sampleedit]")
{
    Buffer b;
    b.channels = 1; b.sampleRate = kSr;
    b.data.assign (static_cast<std::size_t> (kSr * 2), 0.0f);
    const std::int64_t at[] = { 4800, 28800, 62400 };
    for (const auto start : at)
        for (int i = 0; i < 9600; ++i)
            b.data[static_cast<std::size_t> (start + i)] = static_cast<float> (0.8 * std::exp (-i / 1500.0) * std::sin (2.0 * 3.14159265 * 220.0 * i / kSr));
    const auto hits = detectTransients (b, 0.6, 80.0);
    REQUIRE (hits.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) REQUIRE (std::llabs (hits[i] - at[i]) < 600);
}

TEST_CASE ("snapToZero lands on a sign change", "[sampleedit]")
{
    auto b = sine (100.0, 4800);
    const auto f = snapToZero (b, 100, 400);
    REQUIRE (std::fabs (b.at (f, 0)) < 0.1);
    REQUIRE (std::llabs (f - 100) <= 400);
    // aucun passage par zéro dans le rayon : la position est rendue
    Buffer flat; flat.channels = 1; flat.sampleRate = kSr; flat.data.assign (1000, 0.5f);
    REQUIRE (snapToZero (flat, 500, 50) == 500);
}

TEST_CASE ("findLoopCandidates prefers a periodic seam", "[sampleedit]")
{
    // sinus pur de 100 Hz (480 échantillons par période) : tout multiple de la période est un raccord parfait
    auto b = sine (100.0, 48000);
    const auto c = findLoopCandidates (b, 0, -1, 5, 100.0);
    REQUIRE_FALSE (c.empty());
    REQUIRE (c.front().score > 0.9);
    REQUIRE (((c.front().end - c.front().start) % 480 <= 2 || 480 - (c.front().end - c.front().start) % 480 <= 2));
    REQUIRE (c.size() <= 5);
    for (std::size_t i = 1; i < c.size(); ++i) REQUIRE (c[i - 1].score >= c[i].score);
}

TEST_CASE ("write, read back and derived names", "[sampleedit]")
{
    const auto dir = std::filesystem::temp_directory_path() / "neptune-sampleedit-test";
    std::filesystem::remove_all (dir);
    auto b = sine (440.0, 2000, 0.4, 2);
    const auto path = derivedPath (dir, "/some/where/Kick.wav", "trim", b);
    REQUIRE (path == derivedPath (dir, "/some/where/Kick.wav", "trim", b));      // déterministe
    auto b2 = b; b2.data[10] += 0.1f;
    REQUIRE (path != derivedPath (dir, "/some/where/Kick.wav", "trim", b2));     // dépend du contenu
    std::string err;
    REQUIRE (writeWav (path, b, &err));
    Buffer r;
    REQUIRE (readFile (path, r, &err));
    REQUIRE (r.channels == 2);
    REQUIRE (r.frames() == 2000);
    REQUIRE_THAT (r.sampleRate, WithinAbs (kSr, 0.1));
    REQUIRE_THAT (r.at (1000, 1), WithinAbs (b.at (1000, 1), 1e-6));
    std::filesystem::remove_all (dir);
}

namespace
{
// attaque de bruit (100 ms), son tenu de 480 Hz (période exacte de 100 échantillons) de 0,5 à 1,6 s, puis extinction rapide
Buffer attackSustainTail()
{
    Buffer b;
    b.channels = 1; b.sampleRate = kSr;
    b.data.assign (static_cast<std::size_t> (kSr * 2.0), 0.0f);
    unsigned seed = 12345;
    for (std::int64_t i = 0; i < 4800; ++i) { seed = seed * 1664525u + 1013904223u; b.data[static_cast<std::size_t> (i)] = (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f) * 0.9f; }
    for (std::int64_t i = 4800; i < 76800; ++i) b.data[static_cast<std::size_t> (i)] = static_cast<float> (0.4 * std::sin (2.0 * 3.14159265358979 * 480.0 * static_cast<double> (i) / kSr));
    for (std::int64_t i = 76800; i < 96000; ++i)
        b.data[static_cast<std::size_t> (i)] = static_cast<float> (0.4 * std::exp (-static_cast<double> (i - 76800) / 2500.0) * std::sin (2.0 * 3.14159265358979 * 480.0 * static_cast<double> (i) / kSr));
    return b;
}
}

TEST_CASE ("period detection finds the exact period", "[sampleedit]")
{
    const auto b = attackSustainTail();
    REQUIRE_THAT (detectPeriod (b, 10000, 70000), WithinAbs (100.0, 0.5));
    Buffer noise; noise.channels = 1; noise.sampleRate = kSr; noise.data.resize (20000);
    unsigned seed = 7;
    for (auto& v : noise.data) { seed = seed * 1664525u + 1013904223u; v = static_cast<float> (seed >> 8) / 8388608.0f - 1.0f; }
    REQUIRE (detectPeriod (noise, 0, 20000) == 0.0);
}

TEST_CASE ("stable region skips the attack and the tail", "[sampleedit]")
{
    const auto b = attackSustainTail();
    const auto st = findStableRegion (b, 0, b.frames(), 200.0);
    REQUIRE (st.found);
    REQUIRE (st.start >= 4800 - 480);
    REQUIRE (st.start <= 4800 + 2000);
    REQUIRE (st.end <= 76800 + 960);
    REQUIRE (st.end >= 70000);
}

TEST_CASE ("auto loop uses the stable region and whole periods", "[sampleedit]")
{
    const auto b = attackSustainTail();
    const auto c = findLoopCandidates (b, 0, b.frames(), 5, 100.0);
    REQUIRE_FALSE (c.empty());
    for (const auto& cand : c)
    {
        REQUIRE (cand.start >= 4800);          // jamais dans l'attaque
        REQUIRE (cand.end <= 76800 + 960);     // jamais loin dans l'extinction
    }
    const auto len = c.front().end - c.front().start;
    REQUIRE ((len % 100 <= 1 || len % 100 >= 99));
    REQUIRE (c.front().score > 0.8);
}

TEST_CASE ("auto loop around the cursor keeps the start near it", "[sampleedit]")
{
    const auto b = attackSustainTail();
    LoopSearchOptions opt;
    opt.aroundCursor = true; opt.cursor = 30000; opt.radiusMs = 100.0;
    const auto c = findLoopCandidates (b, 0, b.frames(), 5, 100.0, opt);
    REQUIRE_FALSE (c.empty());
    for (const auto& cand : c) REQUIRE (std::llabs (cand.start - 30000) <= 4800 + 200);
    // sans les options, on retrouve le comportement d'origine (formes d'onde seulement)
    LoopSearchOptions plain;
    plain.period = false; plain.stableOnly = false; plain.spectral = false;
    REQUIRE_FALSE (findLoopCandidates (b, 0, b.frames(), 5, 100.0, plain).empty());
}
