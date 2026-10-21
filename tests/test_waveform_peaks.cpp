// Enveloppe de forme d'onde : cases min / max d'un fichier réel, à plusieurs niveaux de zoom.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

#include <sndfile.h>

#include "neptune/core/WaveformPeaks.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
std::filesystem::path writeWav (const char* name, int channels, const std::vector<float>& interleaved)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    SF_INFO info {};
    info.samplerate = 48000;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    sf_writef_float (f, interleaved.data(), static_cast<sf_count_t> (interleaved.size() / static_cast<std::size_t> (channels)));
    sf_close (f);
    return path;
}
} // namespace

TEST_CASE ("peaks cover the whole file and the requested region")
{
    std::vector<float> x (4000);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = i < 1000 ? 0.25f : (i < 2000 ? -0.5f : (i < 3000 ? 0.75f : 0.0f));
    const auto path = writeWav ("neptune_peaks_mono.wav", 1, x);
    const auto all = computePeaks (path, 0, -1, 4);
    REQUIRE (all.ok);
    CHECK (all.channels == 1);
    CHECK (all.totalFrames == 4000);
    CHECK_THAT (all.high (0, 0), WithinAbs (0.25, 1e-6));
    CHECK_THAT (all.low (0, 1), WithinAbs (-0.5, 1e-6));
    CHECK_THAT (all.high (0, 2), WithinAbs (0.75, 1e-6));
    CHECK_THAT (all.high (0, 3), WithinAbs (0.0, 1e-6));
    const auto zoom = computePeaks (path, 900, 1100, 2);   // la frontière à 1000 coupe la région en deux cases
    REQUIRE (zoom.ok);
    CHECK_THAT (zoom.high (0, 0), WithinAbs (0.25, 1e-6));
    CHECK_THAT (zoom.low (0, 1), WithinAbs (-0.5, 1e-6));
    std::filesystem::remove (path);
}

TEST_CASE ("peaks handle stereo, more bins than frames, bad files and cancellation")
{
    std::vector<float> x;
    for (int i = 0; i < 10; ++i) { x.push_back (0.5f); x.push_back (-0.25f); }
    const auto path = writeWav ("neptune_peaks_stereo.wav", 2, x);
    const auto p = computePeaks (path, 0, -1, 64);
    REQUIRE (p.ok);
    CHECK (p.channels == 2);
    for (int i = 0; i < 64; ++i)
    {
        REQUIRE_THAT (p.high (0, i), WithinAbs (0.5, 1e-6));
        REQUIRE_THAT (p.low (1, i), WithinAbs (-0.25, 1e-6));
    }
    const std::atomic<bool> stop { true };
    CHECK_FALSE (computePeaks (path, 0, -1, 8, &stop).ok);
    CHECK_FALSE (computePeaks ("/nonexistent/file.wav", 0, -1, 8).ok);
    CHECK_FALSE (computePeaks (path, 5, 5, 8).ok);
    std::filesystem::remove (path);
}
