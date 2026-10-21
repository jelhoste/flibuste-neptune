// Étirement temporel et décalage de hauteur hors temps réel.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>

#include "neptune/core/TimeStretch.h"

using namespace neptune::core::sampleedit;

namespace
{
constexpr double kSr = 48000.0;
Buffer tone (double hz, std::int64_t frames, int channels = 1)
{
    Buffer b;
    b.channels = channels; b.sampleRate = kSr;
    b.data.resize (static_cast<std::size_t> (frames * channels));
    for (std::int64_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) b.data[static_cast<std::size_t> (i * channels + c)] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / kSr));
    return b;
}
// fréquence estimée par les passages par zéro montants, sur le tiers central
double frequency (const Buffer& b)
{
    const std::int64_t from = b.frames() / 3, to = 2 * b.frames() / 3;
    int crossings = 0;
    for (std::int64_t i = from + 1; i < to; ++i) if (b.at (i - 1, 0) <= 0.0f && b.at (i, 0) > 0.0f) ++crossings;
    return crossings / (static_cast<double> (to - from) / kSr);
}
}

TEST_CASE ("neutral ratios return a copy", "[stretch]")
{
    const auto in = tone (440.0, 5000);
    const auto out = stretchAndShift (in, 1.0, 0.0);
    REQUIRE (out.frames() == in.frames());
    REQUIRE (out.data == in.data);
}

TEST_CASE ("time stretch changes length and keeps pitch", "[stretch]")
{
    const auto in = tone (440.0, 48000);
    for (const double r : { 0.5, 1.5, 2.0 })
    {
        const auto out = stretchAndShift (in, r, 0.0);
        REQUIRE (std::fabs (static_cast<double> (out.frames()) / static_cast<double> (in.frames()) - r) < 0.01);
        REQUIRE (std::fabs (frequency (out) - 440.0) < 8.0);
    }
}

TEST_CASE ("pitch shift changes pitch and keeps length", "[stretch]")
{
    const auto in = tone (440.0, 48000);
    const auto up = stretchAndShift (in, 1.0, 12.0);
    REQUIRE (std::llabs (up.frames() - in.frames()) < 600);
    REQUIRE (std::fabs (frequency (up) - 880.0) < 15.0);
    const auto down = stretchAndShift (in, 1.0, -12.0);
    REQUIRE (std::llabs (down.frames() - in.frames()) < 600);
    REQUIRE (std::fabs (frequency (down) - 220.0) < 8.0);
}

TEST_CASE ("both ratios are independent", "[stretch]")
{
    const auto in = tone (440.0, 48000);
    const auto out = stretchAndShift (in, 2.0, 7.0);   // deux fois plus long, une quinte plus haut
    REQUIRE (std::fabs (static_cast<double> (out.frames()) / static_cast<double> (in.frames()) - 2.0) < 0.02);
    REQUIRE (std::fabs (frequency (out) - 440.0 * std::pow (2.0, 7.0 / 12.0)) < 12.0);
}

TEST_CASE ("stereo is processed per channel and level is preserved", "[stretch]")
{
    const auto in = tone (330.0, 48000, 2);
    const auto out = stretchAndShift (in, 1.25, 0.0);
    REQUIRE (out.channels == 2);
    const auto mid = out.frames() / 2;
    float peak = 0.0f;
    for (std::int64_t i = mid; i < mid + 2000; ++i) peak = std::max (peak, std::fabs (out.at (i, 1)));
    REQUIRE (peak > 0.4f);
    REQUIRE (peak < 0.6f);
}

TEST_CASE ("cancel returns an empty buffer", "[stretch]")
{
    const auto in = tone (440.0, 48000);
    std::atomic<bool> stop { true };
    REQUIRE (stretchAndShift (in, 2.0, 0.0, &stop).frames() == 0);
}
