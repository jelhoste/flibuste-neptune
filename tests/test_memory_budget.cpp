// Estimation de la mémoire d'un instrument et garde-fou RAM disponible (palier 1).
#include <catch2/catch_test_macros.hpp>
#include <sndfile.h>

#include <filesystem>
#include <string>
#include <vector>

#include "neptune/core/MemoryBudget.h"

using namespace neptune::core;
using namespace neptune::core::memory;

namespace
{
struct TempDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "neptune_memory_budget_test";
    TempDir() { std::filesystem::remove_all (path); std::filesystem::create_directories (path); }
    ~TempDir() { std::filesystem::remove_all (path); }
};

std::string writeWav (const std::filesystem::path& file, int channels, int subformat, int frames)
{
    SF_INFO info {};
    info.channels = channels;
    info.samplerate = 44100;
    info.format = SF_FORMAT_WAV | subformat;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    std::vector<float> data (static_cast<std::size_t> (frames * channels), 0.1f);
    sf_writef_float (f, data.data(), frames);
    sf_close (f);
    return file.string();
}

ZoneDescription zone (const std::string& path, int key, int rr = 0, bool release = false, const std::string& group = {})
{
    ZoneDescription z;
    z.samplePath = path;
    z.keyLow = z.keyHigh = z.rootNote = key;
    z.roundRobin = rr;
    z.onRelease = release;
    z.group = group;
    return z;
}
} // namespace

TEST_CASE ("instrument estimate follows what is loaded and counts a shared file once")
{
    TempDir d;
    const auto a = writeWav (d.path / "a.wav", 1, SF_FORMAT_PCM_16, 1000);   // 2 000 octets
    const auto b = writeWav (d.path / "b.wav", 2, SF_FORMAT_PCM_24, 1000);   // 8 000 octets
    const auto c = writeWav (d.path / "c.wav", 1, SF_FORMAT_PCM_16, 500);    // 1 000 octets
    const auto rel = writeWav (d.path / "rel.wav", 1, SF_FORMAT_PCM_16, 250); //   500 octets

    InstrumentDescription desc;
    desc.zones = { zone (a, 60, 0), zone (c, 60, 1), zone (b, 62), zone (rel, 60, 0, true, {}), zone (b, 64) /* même fichier */ };

    FileInfoCache cache;
    int bad = -1;
    CHECK (estimateInstrumentBytes (desc, cache, &bad) == 2000 + 1000 + 8000 + 500);
    CHECK (bad == 0);

    desc.mainRoundRobinOnly = true; // c (alternative) n'est plus chargé
    CHECK (estimateInstrumentBytes (desc, cache) == 2000 + 8000 + 500);

    desc.loadReleaseSamples = false;
    CHECK (estimateInstrumentBytes (desc, cache) == 2000 + 8000);
}

TEST_CASE ("unreadable files are counted apart and add nothing")
{
    TempDir d;
    InstrumentDescription desc;
    desc.zones = { zone ((d.path / "missing.wav").string(), 60) };
    FileInfoCache cache;
    int bad = 0;
    CHECK (estimateInstrumentBytes (desc, cache, &bad) == 0);
    CHECK (bad == 1);
}

TEST_CASE ("limits come from the available memory, with fixed fallback when it is unknown")
{
    const std::uint64_t gb = 1024ull * 1024 * 1024;
    const auto l = limitsFor (8 * gb);
    CHECK (l.fromAvailable);
    CHECK (l.warnBytes == 2 * gb);
    CHECK (l.refuseBytes == 4 * gb);
    CHECK (judge (1 * gb, l) == Verdict::Ok);
    CHECK (judge (3 * gb, l) == Verdict::Warn);
    CHECK (judge (5 * gb, l) == Verdict::Refuse);

    const auto f = limitsFor (0);
    CHECK_FALSE (f.fromAvailable);
    CHECK (f.warnBytes == fallbackWarnBytes);
    CHECK (f.refuseBytes == fallbackRefuseBytes);
}

TEST_CASE ("available memory is reported on this platform")
{
#if defined (__linux__) || defined (_WIN32)
    CHECK (availablePhysicalBytes() > 0);
#else
    CHECK (availablePhysicalBytes() >= 0);
#endif
}
