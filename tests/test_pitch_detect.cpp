// Détection de la hauteur (YIN) : sons harmoniques, accordage, sons sans hauteur.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>

#include "neptune/core/PitchDetect.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 48000.0;
sampleedit::Buffer harmonic (double hz, double seconds, int channels = 1, bool decay = true)
{
    sampleedit::Buffer b;
    b.channels = channels; b.sampleRate = kSr;
    const auto frames = static_cast<std::int64_t> (seconds * kSr);
    b.data.resize (static_cast<std::size_t> (frames * channels));
    for (std::int64_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        double v = 0.0;
        for (int h = 1; h <= 5; ++h) v += std::sin (2.0 * 3.14159265358979 * hz * h * t) / h;   // fondamentale + harmoniques
        v *= 0.3 * (decay ? std::exp (-t * 1.5) : 1.0);
        for (int c = 0; c < channels; ++c) b.data[static_cast<std::size_t> (i * channels + c)] = static_cast<float> (v);
    }
    return b;
}
}

TEST_CASE ("pitch detection finds notes across the range", "[pitch]")
{
    for (const double hz : { 110.0, 261.6256, 440.0, 880.0, 1760.0 })
    {
        const auto r = detectPitch (harmonic (hz, 1.5));
        REQUIRE (r.found);
        CHECK_THAT (r.hz / hz, WithinAbs (1.0, 0.004));
        CHECK (r.confidence > 0.9);
    }
    const auto a4 = detectPitch (harmonic (440.0, 1.0));
    CHECK (a4.note == 69);
    CHECK_THAT (a4.cents, WithinAbs (0.0, 3.0));
}

TEST_CASE ("pitch detection reports the tuning error in cents", "[pitch]")
{
    const double flat = 440.0 * std::pow (2.0, -23.0 / 1200.0);   // 23 cents sous le la
    const auto r = detectPitch (harmonic (flat, 1.5, 2));
    REQUIRE (r.found);
    CHECK (r.note == 69);
    CHECK_THAT (r.cents, WithinAbs (-23.0, 4.0));
}

TEST_CASE ("noise and clicks have no pitch", "[pitch]")
{
    sampleedit::Buffer noise;
    noise.channels = 1; noise.sampleRate = kSr;
    noise.data.resize (static_cast<std::size_t> (kSr * 1.5));
    unsigned seed = 99;
    for (auto& v : noise.data) { seed = seed * 1664525u + 1013904223u; v = 0.5f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f); }
    CHECK_FALSE (detectPitch (noise).found);
    sampleedit::Buffer click;
    click.channels = 1; click.sampleRate = kSr; click.data.assign (static_cast<std::size_t> (kSr), 0.0f);
    for (int i = 0; i < 200; ++i) click.data[static_cast<std::size_t> (i)] = std::exp (-i / 30.0f);
    CHECK_FALSE (detectPitch (click).found);
    sampleedit::Buffer silence;
    silence.channels = 1; silence.sampleRate = kSr; silence.data.assign (static_cast<std::size_t> (kSr), 0.0f);
    CHECK_FALSE (detectPitch (silence).found);
    CHECK_FALSE (detectPitchOfFile ("/no/such/file.wav").found);
}

TEST_CASE ("pitch detection works on a file", "[pitch]")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-pitch.wav";
    REQUIRE (sampleedit::writeWav (path, harmonic (329.6276, 2.0, 2)));
    const auto r = detectPitchOfFile (path);
    REQUIRE (r.found);
    CHECK (r.note == 64);   // mi
    std::filesystem::remove (path);
}
