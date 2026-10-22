// Métadonnées Neptune dans les fichiers sample : chunk WAV / AIFF, fichier voisin, fidélité du reste du fichier.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sndfile.h>

#include "neptune/core/SampleEdit.h"
#include "neptune/core/SampleLoader.h"
#include "neptune/core/SampleMeta.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
std::filesystem::path makeFile (const char* name, int format, bool withInstrument = false)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove (path);
    SF_INFO info {};
    info.samplerate = 44100; info.channels = 2; info.format = format;
    SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    if (withInstrument)
    {
        SF_INSTRUMENT inst {};
        inst.basenote = 60; inst.gain = 1; inst.velocity_lo = 0; inst.velocity_hi = 127; inst.key_lo = 0; inst.key_hi = 127; inst.loop_count = 1;
        inst.loops[0].mode = SF_LOOP_FORWARD; inst.loops[0].start = 100; inst.loops[0].end = 900; inst.loops[0].count = 0;
        sf_command (f, SFC_SET_INSTRUMENT, &inst, sizeof inst);
    }
    std::vector<float> data (2 * 4410);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = 0.5f * std::sin (0.01f * static_cast<float> (i));
    sf_writef_float (f, data.data(), 4410);
    sf_close (f);
    return path;
}
std::vector<char> slurp (const std::filesystem::path& p) { std::ifstream f (p, std::ios::binary); return { std::istreambuf_iterator<char> (f), {} }; }
}

TEST_CASE ("meta JSON round trip", "[meta]")
{
    SampleMeta m; m.hasVolumeMarker = true; m.volumeMarkerMs = 123.5;
    SampleMeta back;
    REQUIRE (sampleMetaFromJson (sampleMetaToJson (m), back));
    CHECK (back == m);
    CHECK_FALSE (sampleMetaFromJson ("not json", back));
    REQUIRE (sampleMetaFromJson (sampleMetaToJson (SampleMeta {}), back));
    CHECK_FALSE (back.hasVolumeMarker);
}

TEST_CASE ("a WAV carries the marker in a chunk and keeps everything else", "[meta]")
{
    const auto path = makeFile ("neptune-meta.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_24, true);
    sampleedit::Buffer before;
    REQUIRE (sampleedit::readFile (path, before));
    const auto sizeBefore = std::filesystem::file_size (path);
    SampleMeta none;
    CHECK_FALSE (readSampleMeta (path, none));

    SampleMeta m; m.hasVolumeMarker = true; m.volumeMarkerMs = 42.25;
    MetaStore where = MetaStore::None;
    std::string err;
    REQUIRE (writeSampleMeta (path, m, &where, &err));
    CHECK (where == MetaStore::Chunk);
    CHECK (std::filesystem::file_size (path) > sizeBefore);
    CHECK_FALSE (std::filesystem::exists (std::filesystem::path (path.string() + ".neptune.json")));

    SampleMeta read;
    MetaStore found = MetaStore::None;
    REQUIRE (readSampleMeta (path, read, &found));
    CHECK (found == MetaStore::Chunk);
    CHECK (read == m);

    // l'audio est identique et le fichier est toujours lu comme avant ; le chunk d'instrument (smpl) est conservé
    sampleedit::Buffer after;
    REQUIRE (sampleedit::readFile (path, after));
    CHECK (after.frames() == before.frames());
    CHECK (after.data == before.data);
    SF_INFO info {};
    SNDFILE* f = sf_open (path.string().c_str(), SFM_READ, &info);
    REQUIRE (f != nullptr);
    SF_INSTRUMENT inst {};
    CHECK (sf_command (f, SFC_GET_INSTRUMENT, &inst, sizeof inst) == SF_TRUE);
    CHECK (inst.loop_count == 1);
    CHECK (inst.loops[0].start == 100);
    sf_close (f);

    // remplacer, puis retirer
    m.volumeMarkerMs = 7.0;
    REQUIRE (writeSampleMeta (path, m));
    REQUIRE (readSampleMeta (path, read));
    CHECK_THAT (read.volumeMarkerMs, WithinAbs (7.0, 1e-9));
    const auto sizeWith = std::filesystem::file_size (path);
    REQUIRE (writeSampleMeta (path, SampleMeta {}));
    CHECK_FALSE (readSampleMeta (path, read));
    CHECK (std::filesystem::file_size (path) == sizeBefore);   // revenu exactement au fichier d'origine
    CHECK (sizeWith > sizeBefore);
    std::filesystem::remove (path);
}

TEST_CASE ("an AIFF works the same way", "[meta]")
{
    const auto path = makeFile ("neptune-meta.aif", SF_FORMAT_AIFF | SF_FORMAT_PCM_16);
    sampleedit::Buffer before;
    REQUIRE (sampleedit::readFile (path, before));
    SampleMeta m; m.hasVolumeMarker = true; m.volumeMarkerMs = 1000.0;
    MetaStore where = MetaStore::None;
    REQUIRE (writeSampleMeta (path, m, &where));
    CHECK (where == MetaStore::Chunk);
    SampleMeta read;
    REQUIRE (readSampleMeta (path, read, &where));
    CHECK (where == MetaStore::Chunk);
    CHECK (read == m);
    sampleedit::Buffer after;
    REQUIRE (sampleedit::readFile (path, after));
    CHECK (after.data == before.data);
    std::filesystem::remove (path);
}

TEST_CASE ("a format without a simple chunk gets a neighbour file", "[meta]")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-meta-other.bin";
    { std::ofstream f (path, std::ios::binary); f << "this is not an audio container"; }
    const auto side = std::filesystem::path (path.string() + ".neptune.json");
    std::filesystem::remove (side);
    SampleMeta m; m.hasVolumeMarker = true; m.volumeMarkerMs = 5.0;
    MetaStore where = MetaStore::None;
    REQUIRE (writeSampleMeta (path, m, &where));
    CHECK (where == MetaStore::Sidecar);
    CHECK (std::filesystem::exists (side));
    SampleMeta read;
    REQUIRE (readSampleMeta (path, read, &where));
    CHECK (where == MetaStore::Sidecar);
    CHECK (read == m);
    REQUIRE (writeSampleMeta (path, SampleMeta {}));
    CHECK_FALSE (std::filesystem::exists (side));
    CHECK_FALSE (writeSampleMeta ("/no/such/file.wav", m));
    std::filesystem::remove (path);
}

TEST_CASE ("loops, region and root note travel in the metadata", "[meta]")
{
    const auto path = makeFile ("neptune-meta-loops.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16);
    SampleMeta m;
    m.sampleRate = 44100.0;
    m.hasRegion = true; m.startFrame = 120; m.endFrame = 4000;
    m.hasRoot = true; m.rootNote = 57; m.tuneCents = -12.5;
    m.hasVolumeMarker = true; m.volumeMarkerMs = 33.0;
    m.loops = { { 500, 1500, 20, 3 }, { 2000, 3000, 0, 0 } };
    REQUIRE (writeSampleMeta (path, m));
    SampleMeta back;
    REQUIRE (readFileMarks (path, back));
    CHECK (back.loops == m.loops);
    CHECK (back.hasRegion);
    CHECK (back.startFrame == 120);
    CHECK (back.endFrame == 4000);
    CHECK (back.rootNote == 57);
    CHECK_THAT (back.tuneCents, WithinAbs (-12.5, 1e-9));
    CHECK (back.hasVolumeMarker);
    // un fichier qui ne porte rien
    REQUIRE (writeSampleMeta (path, SampleMeta {}));
    CHECK_FALSE (readFileMarks (path, back));
    std::filesystem::remove (path);
}

TEST_CASE ("a standard smpl chunk gives loops and root note", "[meta]")
{
    const auto path = makeFile ("neptune-meta-smpl.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, true);   // boucle 100..900 (incluse), note 60
    SampleMeta read;
    REQUIRE (readFileMarks (path, read));
    REQUIRE (read.loops.size() == 1);
    CHECK (read.loops[0].start == 100);
    CHECK (read.loops[0].end == 901);   // fin exclusive : le dernier échantillon de la boucle du smpl est inclus
    CHECK (read.hasRoot);
    CHECK (read.rootNote == 60);
    // les valeurs de Neptune l'emportent sur celles du smpl
    SampleMeta ours;
    ours.sampleRate = 44100.0; ours.hasRoot = true; ours.rootNote = 48;
    ours.loops = { { 200, 700, 0, 0 } };
    REQUIRE (writeSampleMeta (path, ours));
    REQUIRE (readFileMarks (path, read));
    CHECK (read.rootNote == 48);
    REQUIRE (read.loops.size() == 1);
    CHECK (read.loops[0].start == 200);
    std::filesystem::remove (path);
}

TEST_CASE ("positions follow a file whose sample rate changed", "[meta]")
{
    const auto path = makeFile ("neptune-meta-rate.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16);
    SampleMeta m;
    m.sampleRate = 22050.0;                     // écrit pour un fichier deux fois plus lent que celui-ci (44100)
    m.loops = { { 100, 500, 10, 0 } };
    REQUIRE (writeSampleMeta (path, m));
    SampleMeta back;
    REQUIRE (readFileMarks (path, back));
    REQUIRE (back.loops.size() == 1);
    CHECK (back.loops[0].start == 200);
    CHECK (back.loops[0].end == 1000);
    CHECK (back.loops[0].crossfade == 20);
    std::filesystem::remove (path);
}
