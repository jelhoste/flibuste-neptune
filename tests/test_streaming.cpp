// Streaming disque (palier 1, jalon 4) : préchargement partiel, lecteur séquentiel, thread disque, rendu identique au
// mode tout-RAM, sous-alimentation, estimation de mémoire.
#include <catch2/catch_test_macros.hpp>
#include <sndfile.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/MemoryBudget.h"
#include "neptune/core/SampleLoader.h"
#include "neptune/core/SamplerVoices.h"

using namespace neptune::core;

namespace
{
struct TempDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "neptune_streaming_test";
    TempDir() { std::filesystem::remove_all (path); std::filesystem::create_directories (path); }
    ~TempDir() { std::filesystem::remove_all (path); }
};

// Stéréo ou mono, rampe + sinus, pour que chaque frame soit reconnaissable.
std::string writeWav (const std::filesystem::path& file, int channels, int subformat, int frames, double rate = 44100.0)
{
    SF_INFO info {};
    info.channels = channels;
    info.samplerate = static_cast<int> (rate);
    info.format = SF_FORMAT_WAV | subformat;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    std::vector<float> data (static_cast<std::size_t> (frames * channels));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            data[static_cast<std::size_t> (i * channels + c)] =
                0.6f * std::sin (0.05f * static_cast<float> (i) * static_cast<float> (c + 1)) * (1.0f - static_cast<float> (i) / static_cast<float> (frames));
    sf_writef_float (f, data.data(), frames);
    sf_close (f);
    return file.string();
}

template <typename Pred>
bool waitFor (Pred p, int timeoutMs = 3000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    while (std::chrono::steady_clock::now() < end)
    {
        if (p()) return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return p();
}

ZoneDescription zoneOf (const std::string& path, int key, PlayMode mode = PlayMode::OneShot)
{
    ZoneDescription z;
    z.samplePath = path;
    z.keyLow = z.keyHigh = z.rootNote = key;
    z.mode = mode;
    return z;
}
} // namespace

TEST_CASE ("partial load keeps only the start in memory and the full length")
{
    TempDir d;
    const auto file = writeWav (d.path / "a.wav", 2, SF_FORMAT_PCM_24, 10000);
    const auto full = loadSampleFile (file);
    const auto part = loadSampleFile (file, 1000);
    REQUIRE (full.ok());
    REQUIRE (part.ok());
    CHECK (part.sample->frames == 10000);
    CHECK (part.sample->resident() == 1000);
    CHECK (part.sample->isStreamed());
    CHECK_FALSE (full.sample->isStreamed());
    CHECK (part.sample->bytes() == 1000u * 2u * 4u);
    CHECK (part.sample->path == file);
    for (int c = 0; c < 2; ++c)
        for (int k : { 0, 1, 500, 999 })
            CHECK (part.sample->at (c, k) == full.sample->at (c, k));
    CHECK (part.sample->at (0, 1000) == 0.0f); // au-delà du préchargement : hors de la partie résidente

    // demande >= longueur : tout est résident
    CHECK_FALSE (loadSampleFile (file, 10000).sample->isStreamed());
}

TEST_CASE ("stream reader returns the same values as a full load, from any position")
{
    TempDir d;
    for (const int sub : { SF_FORMAT_PCM_16, SF_FORMAT_PCM_24 })
    {
        const auto file = writeWav (d.path / ("r" + std::to_string (sub) + ".wav"), 2, sub, 5000);
        const auto full = loadSampleFile (file);
        REQUIRE (full.ok());
        AudioStreamReader r;
        REQUIRE (r.open (file));
        CHECK (r.channels() == 2);
        CHECK (r.totalFrames() == 5000);
        REQUIRE (r.seek (1234));
        std::vector<float> buf (200 * 2);
        REQUIRE (r.read (buf.data(), 200) == 200);
        for (int i = 0; i < 200; ++i)
            for (int c = 0; c < 2; ++c)
                CHECK (buf[static_cast<std::size_t> (i * 2 + c)] == full.sample->at (c, 1234 + i));
    }
    AudioStreamReader bad;
    CHECK_FALSE (bad.open (d.path / "missing.wav"));
}

TEST_CASE ("disk streamer fills the ring with the data that follows the preload")
{
    TempDir d;
    const auto file = writeWav (d.path / "s.wav", 2, SF_FORMAT_PCM_24, 12000);
    const auto full = loadSampleFile (file);
    const auto part = loadSampleFile (file, 1000);
    REQUIRE (full.ok());
    REQUIRE (part.ok());

    DiskStreamer streamer;
    CHECK_FALSE (streamer.claim (0, *part.sample)); // pas démarré : refusé
    streamer.start();
    REQUIRE (streamer.claim (3, *part.sample));
    const auto gen = streamer.generation (3);
    REQUIRE (waitFor ([&] { return streamer.view (3, gen).ready && streamer.view (3, gen).writePos >= 12000; }));
    const auto v = streamer.view (3, gen);
    for (std::int64_t k = 1000; k < 12000; k += 7)
    {
        const auto idx = static_cast<std::size_t> (k & (DiskStreamer::kRingFrames - 1));
        REQUIRE (v.left[idx] == full.sample->at (0, k));
        REQUIRE (v.right[idx] == full.sample->at (1, k));
    }
    streamer.release (3);
    CHECK_FALSE (streamer.view (3, gen).ready);
}

TEST_CASE ("a streamed voice renders exactly what the all-RAM voice renders")
{
    TempDir d;
    const auto file = writeWav (d.path / "v.wav", 2, SF_FORMAT_PCM_24, 40000);
    const auto full = loadSampleFile (file);
    const auto part = loadSampleFile (file, 3000);
    REQUIRE (full.ok());
    REQUIRE (part.ok());

    auto make = [] (std::shared_ptr<const SampleData> s)
    {
        auto inst = std::make_shared<Instrument>();
        inst->zones.push_back (makeFullKeyboardZone (std::move (s), 60, PlayMode::OneShot));
        return inst;
    };
    const auto ramInst = make (full.sample);
    const auto strInst = make (part.sample);

    DiskStreamer streamer;
    streamer.start();
    SamplerVoices ram, str;
    ram.prepare (44100.0);
    str.prepare (44100.0);
    str.setStreamer (&streamer);
    ram.setInstrument (ramInst.get());
    str.setInstrument (strInst.get());
    ram.noteOn (0, 60, 1.0f);
    str.noteOn (0, 60, 1.0f);

    const int block = 256;
    const auto gen = streamer.generation (0); // la première voix prend l'emplacement 0
    std::vector<float> aL (block), aR (block), bL (block), bR (block);
    int blocks = 0;
    while (ram.activeVoices() > 0 && blocks < 400)
    {
        // le test va plus vite que le temps réel : on laisse le disque avoir de l'avance (en vrai, c'est le tampon)
        const std::int64_t needed = std::min<std::int64_t> (40000, static_cast<std::int64_t> (blocks + 1) * block + 6000);
        REQUIRE (waitFor ([&] { return streamer.view (0, gen).ready && streamer.view (0, gen).writePos >= needed; }));
        std::fill (aL.begin(), aL.end(), 0.0f); std::fill (aR.begin(), aR.end(), 0.0f);
        std::fill (bL.begin(), bL.end(), 0.0f); std::fill (bR.begin(), bR.end(), 0.0f);
        ram.render (aL.data(), aR.data(), block, 1.0f, 1.0f, 0.0);
        str.render (bL.data(), bR.data(), block, 1.0f, 1.0f, 0.0);
        for (int i = 0; i < block; ++i)
        {
            REQUIRE (aL[static_cast<std::size_t> (i)] == bL[static_cast<std::size_t> (i)]);
            REQUIRE (aR[static_cast<std::size_t> (i)] == bR[static_cast<std::size_t> (i)]);
        }
        ++blocks;
    }
    CHECK (blocks > 100);
    CHECK (str.activeVoices() == 0);
    CHECK (streamer.underruns() == 0);
    CHECK_FALSE (streamer.view (0, gen).ready); // l'emplacement a été rendu à la fin de la voix
}

TEST_CASE ("underrun: a streamed voice fades out and stops, and the incident is counted")
{
    TempDir d;
    const auto file = writeWav (d.path / "u.wav", 1, SF_FORMAT_PCM_16, 30000);
    const auto part = loadSampleFile (file, 2000);
    REQUIRE (part.ok());
    std::filesystem::remove (file); // le thread disque ne pourra pas ouvrir le fichier

    auto inst = std::make_shared<Instrument>();
    inst->zones.push_back (makeFullKeyboardZone (part.sample, 60, PlayMode::OneShot));

    DiskStreamer streamer;
    streamer.start();
    SamplerVoices v;
    v.prepare (44100.0);
    v.setStreamer (&streamer);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);

    std::vector<float> l (128);
    float peak = 0.0f;
    for (int b = 0; b < 40 && v.activeVoices() > 0; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        v.render (l.data(), nullptr, 128, 1.0f, 1.0f, 0.0);
        for (float x : l) { REQUIRE (std::isfinite (x)); peak = std::max (peak, std::abs (x)); }
    }
    CHECK (v.activeVoices() == 0);
    CHECK (streamer.underruns() >= 1);
    CHECK (peak > 0.0f); // le début (préchargement) a bien joué avant l'extinction
}

TEST_CASE ("streaming description: resident frames follow the preload and loop end")
{
    InstrumentDescription d;
    d.streamFromDisk = true;
    d.preloadKB = 128;
    auto oneShot = zoneOf ("x.wav", 60);
    // 128 Ko : 16 bits stéréo = 4 octets/frame -> 32 768 frames ; 24 bits stéréo (float32) = 8 octets -> 16 384
    CHECK (residentFramesNeeded (d, oneShot, 2, true, 1000000) == 32768);
    CHECK (residentFramesNeeded (d, oneShot, 2, false, 1000000) == 16384);
    CHECK (residentFramesNeeded (d, oneShot, 1, true, 1000000) == 65536);
    CHECK (residentFramesNeeded (d, oneShot, 2, true, 100) == 100); // fichier plus court que le préchargement
    auto loop = zoneOf ("x.wav", 60, PlayMode::Loop);
    loop.loopStart = 100; loop.loopEnd = 5000;
    CHECK (residentFramesNeeded (d, loop, 2, true, 1000000) == 5004);
    d.streamFromDisk = false;
    CHECK (residentFramesNeeded (d, oneShot, 2, true, 1000000) == 1000000);
}

TEST_CASE ("memory estimate matches the built streamed instrument")
{
    TempDir d;
    const auto a = writeWav (d.path / "a.wav", 2, SF_FORMAT_PCM_24, 20000); // float32 stéréo : 8 octets/frame
    const auto b = writeWav (d.path / "b.wav", 1, SF_FORMAT_PCM_16, 50000); // int16 mono : 2 octets/frame
    const auto c = writeWav (d.path / "c.wav", 1, SF_FORMAT_PCM_16, 9000);  // boucle
    InstrumentDescription desc;
    desc.streamFromDisk = true;
    desc.preloadKB = 16; // 16 384 octets
    desc.zones = { zoneOf (a, 60), zoneOf (b, 62), zoneOf (c, 64, PlayMode::Loop), zoneOf (a, 66) };
    desc.zones[2].loopStart = 0; desc.zones[2].loopEnd = 4000;

    std::vector<std::string> warnings;
    const auto inst = buildInstrument (desc, &warnings);
    REQUIRE (inst != nullptr);
    CHECK (warnings.empty());
    std::set<const SampleData*> seen;
    std::uint64_t real = 0;
    for (const auto& z : inst->zones)
        if (seen.insert (z.sample.get()).second) real += z.sample->bytes();

    memory::FileInfoCache cache;
    CHECK (memory::estimateInstrumentBytes (desc, cache) == real + memory::streamBufferBytes());
    // a : 16384 / 8 = 2048 frames ; b : 16384 / 2 = 8192 frames ; c (boucle) : 4004 frames
    CHECK (real == 2048u * 8u + 8192u * 2u + 4004u * 2u);

    desc.streamFromDisk = false; // tout en RAM : les tailles complètes, sans tampons de streaming
    CHECK (memory::estimateInstrumentBytes (desc, cache) == 20000u * 8u + 50000u * 2u + 9000u * 2u);
}

TEST_CASE ("streaming settings are saved with the instrument and bounded when read")
{
    InstrumentDescription d;
    d.streamFromDisk = true;
    d.preloadKB = 512;
    d.zones.push_back (zoneOf ("x.wav", 60));
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.streamFromDisk);
    CHECK (back.preloadKB == 512);

    CHECK_FALSE (instrumentFromJson (toJson (InstrumentDescription {})).streamFromDisk);
    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).preloadKB == InstrumentDescription::defaultPreloadKB);
    CHECK (instrumentFromJson (R"({"preload_kb": 1, "zones": []})").preloadKB == InstrumentDescription::minPreloadKB);
    CHECK (instrumentFromJson (R"({"preload_kb": 99999999, "zones": []})").preloadKB == InstrumentDescription::maxPreloadKB);
}
