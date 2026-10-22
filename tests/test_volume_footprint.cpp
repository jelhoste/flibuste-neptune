// Empreinte de volume : contours Pic / RMS / LUFS, extremum et pics locaux.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <functional>

#include "neptune/core/SampleEdit.h"
#include "neptune/core/LevelTracker.h"
#include "neptune/core/VolumeFootprint.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 48000.0;
std::filesystem::path writeTone (const char* name, double seconds, const std::function<double (double)>& amplitude, int channels = 2)
{
    sampleedit::Buffer b;
    b.channels = channels; b.sampleRate = kSr;
    const auto frames = static_cast<std::int64_t> (seconds * kSr);
    b.data.resize (static_cast<std::size_t> (frames * channels));
    for (std::int64_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double> (i) / kSr;
        const float v = static_cast<float> (amplitude (t) * std::sin (2.0 * 3.14159265358979 * 1000.0 * t));
        for (int c = 0; c < channels; ++c) b.data[static_cast<std::size_t> (i * channels + c)] = v;
    }
    const auto path = std::filesystem::temp_directory_path() / name;
    REQUIRE (sampleedit::writeWav (path, b));
    return path;
}
}

TEST_CASE ("footprint has one point per 10 ms and the right levels", "[footprint]")
{
    const auto path = writeTone ("neptune-fp-steady.wav", 2.0, [] (double) { return 0.5; });
    const auto fp = computeFootprint (path);
    REQUIRE (fp.ok);
    REQUIRE (fp.hopFrames == 480);
    REQUIRE (fp.peakDb.size() == 200);
    REQUIRE (fp.rmsDb.size() == 200);
    REQUIRE_THAT (fp.peakDb[100], WithinAbs (-6.02, 0.1));
    REQUIRE_THAT (fp.rmsDb[100], WithinAbs (-9.03, 0.3));      // sinus : crête -6 dB, RMS -9 dB
    if (! fp.hasLufs()) SUCCEED ("libebur128 not linked");
    else
    {
        REQUIRE (fp.lufs.size() == 200);
        // sinus de 1 kHz de crête 0,5 (RMS -9,03 dBFS) sur deux voies identiques : ~ -9,03 - 0,691 + 3,01 (deux voies) + pondération K de 1 kHz (~ +0,7) = ~ -6 LUFS
        REQUIRE (fp.lufs[100] > -8.0f);
        REQUIRE (fp.lufs[100] < -4.0f);
    }
    std::filesystem::remove (path);
}

TEST_CASE ("footprint follows a level change and navigation finds it", "[footprint]")
{
    // 1 s à -20 dBFS, une rafale forte de 100 ms à 1,0 s, puis 1 s à -20 dBFS
    const auto path = writeTone ("neptune-fp-burst.wav", 2.1, [] (double t) { return (t >= 1.0 && t < 1.1) ? 0.9 : 0.1; });
    const auto fp = computeFootprint (path);
    REQUIRE (fp.ok);
    const int loud = loudestPoint (fp.peakDb, 0, static_cast<int> (fp.peakDb.size()));
    REQUIRE (loud >= 99);
    REQUIRE (loud <= 111);
    REQUIRE (loudestPoint (fp.peakDb, 0, 50) < 50);
    REQUIRE (loudestPoint (fp.peakDb, 5, 5) == -1);
    const auto peaks = localPeaks (fp.rmsDb, 6.0, 100);
    REQUIRE (peaks.size() == 1);
    REQUIRE (std::abs (peaks[0] - 105) <= 8);
    REQUIRE (neighbourPeak (peaks, 0, +1) == peaks[0]);
    REQUIRE (neighbourPeak (peaks, peaks[0], +1) == -1);
    REQUIRE (neighbourPeak (peaks, 200, -1) == peaks[0]);
    REQUIRE (neighbourPeak (peaks, peaks[0], -1) == -1);
    std::filesystem::remove (path);
}

TEST_CASE ("footprint cancel and missing file", "[footprint]")
{
    REQUIRE_FALSE (computeFootprint ("/definitely/not/here.wav").ok);
    const auto path = writeTone ("neptune-fp-cancel.wav", 1.0, [] (double) { return 0.3; });
    std::atomic<bool> stop { true };
    REQUIRE_FALSE (computeFootprint (path, &stop).ok);
    std::filesystem::remove (path);
}

TEST_CASE ("localPeaks ignores ripples below the prominence", "[footprint]")
{
    std::vector<float> c (300, -40.0f);
    c[50] = -20.0f;                     // vrai pic
    c[150] = -37.0f;                    // ondulation de 3 dB
    c[250] = -10.0f;                    // vrai pic
    const auto peaks = localPeaks (c, 6.0, 100);
    REQUIRE (peaks == std::vector<int> { 50, 250 });
}

TEST_CASE ("measureLevelAt reads the causal level at a frame", "[footprint]")
{
    const auto path = writeTone ("neptune-fp-level.wav", 2.0, [] (double t) { return t < 1.0 ? 0.5 : 0.05; });
    double db = 0.0;
    REQUIRE (measureLevelAt (path, 24000, 0, db));            // pleine force : crête de 0,5
    REQUIRE_THAT (db, WithinAbs (-6.02, 0.2));
    REQUIRE (measureLevelAt (path, 90000, 0, db));            // 0,05 plus tard
    REQUIRE_THAT (db, WithinAbs (-26.02, 0.5));
    double l1 = 0.0, l2 = 0.0;
    REQUIRE (measureLevelAt (path, 40000, 2, l1));
    REQUIRE (measureLevelAt (path, 95000, 2, l2));
    REQUIRE (l1 - l2 > 15.0);                                  // la sonie baisse bien d'environ 20 dB
    REQUIRE_FALSE (measureLevelAt ("/nope.wav", 10, 0, db));
    std::filesystem::remove (path);
}
