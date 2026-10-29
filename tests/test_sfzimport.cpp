// Import d'instruments SFZ : analyse du texte (héritage, #define, #include, commentaires), conversion en zones, fichiers manquants.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "neptune/core/SfzImport.h"

using namespace neptune::core;
using namespace neptune::core::sfzimport;
using Catch::Approx;

namespace
{
struct TempDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "neptune_sfz_test";
    TempDir() { std::filesystem::remove_all (path); std::filesystem::create_directories (path / "Samples"); }
    ~TempDir() { std::filesystem::remove_all (path); }
};

void writeWav (const std::filesystem::path& file, bool withLoop = false)
{
    SF_INFO info {};
    info.channels = 1;
    info.samplerate = 44100;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    if (withLoop)
    {
        SF_INSTRUMENT inst {};
        inst.basenote = 64;
        inst.loop_count = 1;
        inst.loops[0].mode = SF_LOOP_FORWARD;
        inst.loops[0].start = 100;
        inst.loops[0].end = 500;
        sf_command (f, SFC_SET_INSTRUMENT, &inst, sizeof inst);
    }
    const std::vector<short> data (1000, 0);
    sf_write_short (f, data.data(), static_cast<sf_count_t> (data.size()));
    sf_close (f);
}

Options noFiles()
{
    Options o;
    o.readSampleFiles = false;
    return o;
}
} // namespace

TEST_CASE ("sfz: note names and numbers")
{
    CHECK (noteFromText ("c4") == 60);
    CHECK (noteFromText ("C4") == 60);
    CHECK (noteFromText ("a0") == 21);
    CHECK (noteFromText ("f#3") == 54);
    CHECK (noteFromText ("db4") == 61);
    CHECK (noteFromText ("bb3") == 58);
    CHECK (noteFromText ("b4") == 71);
    CHECK (noteFromText ("c-1") == 0);
    CHECK (noteFromText ("60") == 60);
    CHECK (noteFromText ("g9") == 127);
    CHECK (noteFromText ("h4") == -1);
    CHECK (noteFromText ("") == -1);
    CHECK (noteFromText ("200") == -1);
}

TEST_CASE ("sfz: regions inherit global, master and group, a region wins")
{
    const auto r = importSfz (R"(
// un commentaire
<control> default_path=Samples/
<global> volume=-3 /* encore un */ lovel=10
<master> pan=50
<group> lokey=c3 hikey=b3 pitch_keycenter=c3
<region> sample=low note.wav
<region> sample=high.wav hikey=c4 volume=-6 pitch_keycenter=62
<group> key=72 tune=15 transpose=2
<region> sample=top.wav
)", noFiles());
    REQUIRE (r.ok);
    REQUIRE (r.instrument.zones.size() == 3);
    const auto& a = r.instrument.zones[0];
    CHECK (a.samplePath.find ("Samples/low note.wav") != std::string::npos);   // l'espace du nom est gardé
    CHECK (a.keyLow == 48);
    CHECK (a.keyHigh == 59);
    CHECK (a.rootNote == 48);
    CHECK (a.gainDb == Approx (-3.0));
    CHECK (a.pan == Approx (0.5));
    CHECK (a.velLow == 10);
    const auto& b = r.instrument.zones[1];
    CHECK (b.keyHigh == 60);                       // la région l'emporte sur son groupe
    CHECK (b.rootNote == 62);
    CHECK (b.gainDb == Approx (-6.0));
    const auto& c = r.instrument.zones[2];
    CHECK (c.keyLow == 72);
    CHECK (c.keyHigh == 72);
    CHECK (c.rootNote == 72);
    CHECK (c.tuneCents == Approx (215.0));         // tune + 100 x transpose
    CHECK (c.pan == Approx (0.5));
}

TEST_CASE ("sfz: #define, #include and backslash paths")
{
    TempDir dir;
    {
        std::ofstream inc (dir.path / "kit.sfz");
        inc << "<region> sample=Samples\\$NAME.wav key=$KEY\n";
    }
    Options o = noFiles();
    o.baseDir = dir.path;
    const auto r = importSfz ("#define $NAME snare\n#define $KEY d1\n#include \"kit.sfz\"\n", o);
    REQUIRE (r.ok);
    REQUIRE (r.instrument.zones.size() == 1);
    CHECK (r.instrument.zones[0].samplePath.find ("snare.wav") != std::string::npos);
    CHECK (r.instrument.zones[0].keyLow == 26);   // d1
}

TEST_CASE ("sfz: loops, region bounds, envelope, filter, triggers and choke groups")
{
    const auto r = importSfz (R"(
<region> sample=a.wav key=60 loop_mode=loop_continuous loop_start=100 loop_end=499 offset=10 end=900
<region> sample=b.wav key=61 loop_mode=one_shot ampeg_attack=0.25 ampeg_release=1.5 ampeg_sustain=40 amp_veltrack=50
<region> sample=c.wav key=62 cutoff=1200 resonance=20 fil_type=hpf_4p fil_keytrack=50
<region> sample=d.wav key=63 trigger=release
<region> sample=e.wav key=64 trigger=first group=3 off_by=3
)", noFiles());
    REQUIRE (r.ok);
    REQUIRE (r.instrument.zones.size() == 5);
    const auto& a = r.instrument.zones[0];
    CHECK (a.mode == PlayMode::Loop);
    CHECK (a.loopStart == 100);
    CHECK (a.loopEnd == 500);                       // la fin de boucle de SFZ est inclusive, celle de Neptune exclusive
    CHECK (a.startFrame == 10);
    CHECK (a.endFrame == 901);
    const auto& b = r.instrument.zones[1];
    CHECK (b.mode == PlayMode::OneShot);
    CHECK_FALSE (b.releaseCuts);                    // one_shot : joué en entier quoi que fasse la touche
    CHECK (b.envelopeOverride);
    CHECK (b.envelope.attack.ms == Approx (250.0));
    CHECK (b.envelope.release.ms == Approx (1500.0));
    CHECK (b.envelope.sustain == Approx (0.4));
    CHECK (b.velTrackPct == Approx (50.0));
    const auto& c = r.instrument.zones[2];
    CHECK (c.filterOverride);
    CHECK (c.filters.filter[0].on);
    CHECK (c.filters.filter[0].mode == FilterMode::HighPass);
    CHECK (c.filters.filter[0].slopeDb == 24);
    CHECK (c.filters.filter[0].cutoffHz == Approx (1200.0));
    CHECK (c.filters.filter[0].resonance == Approx (0.5));
    CHECK (c.filters.filter[0].keyTrack == Approx (0.5));
    CHECK (r.instrument.zones[3].onRelease);
    CHECK_FALSE (r.instrument.zones[3].releaseCuts);
    const auto& e = r.instrument.zones[4];
    CHECK (e.trigger == TriggerCondition::First);
    CHECK (e.chokeCuts == 3);
    CHECK (e.chokeCutBy == 3);
}

TEST_CASE ("sfz: sequential and random round robin sets")
{
    const auto r = importSfz (R"(
<group> key=60 seq_length=2
<region> sample=a1.wav seq_position=1
<region> sample=a2.wav seq_position=2
<group> key=62
<region> sample=r1.wav lorand=0 hirand=0.5
<region> sample=r2.wav lorand=0.5 hirand=1
<region> sample=solo.wav key=64 lorand=0 hirand=0.3
)", noFiles());
    REQUIRE (r.ok);
    REQUIRE (r.instrument.zones.size() == 5);
    CHECK (r.instrument.zones[0].roundRobin == 1);
    CHECK (r.instrument.zones[1].roundRobin == 2);
    CHECK (r.instrument.zones[0].rrMode == 0);
    CHECK (r.instrument.zones[2].roundRobin == 1);
    CHECK (r.instrument.zones[3].roundRobin == 2);
    CHECK (r.instrument.zones[2].rrMode == 1);           // au hasard
    CHECK (r.instrument.zones[4].roundRobin == 0);       // seul de son étendue : pas d'alternance
}

TEST_CASE ("sfz: what is not converted is reported")
{
    const auto r = importSfz (R"(
<global> width=100 position=0
<region> sample=a.wav key=60 sw_last=36 locc64=64 hicc64=127 width=50
<region> sample=*sine key=61
<curve> curve_index=1 v000=0 v127=1
)", noFiles());
    REQUIRE (r.ok);
    REQUIRE (r.instrument.zones.size() == 1);            // l'oscillateur interne est écarté
    std::string all;
    for (const auto& w : r.warnings) all += w + "\n";
    CHECK (all.find ("sw_last") != std::string::npos);
    CHECK (all.find ("locc64") != std::string::npos);
    CHECK (all.find ("<curve>") != std::string::npos);
    CHECK (all.find ("position") == std::string::npos);     // la valeur par défaut de SFZ n'est pas signalée
    CHECK (all.find ("width") != std::string::npos);        // 50 l'est : la largeur stéréo n'est pas convertie
    CHECK (all.find ("oscillators") != std::string::npos);
}

TEST_CASE ("sfz: files are checked and read")
{
    TempDir dir;
    writeWav (dir.path / "Samples" / "Tone.wav", true);
    writeWav (dir.path / "Samples" / "plain.wav");
    {
        std::ofstream f (dir.path / "inst.sfz");
        f << "<control> default_path=samples/\n"                      // casse différente du dossier : tolérée
             "<region> sample=tone.wav key=c4\n"                       // la boucle et la note racine viennent du fichier
             "<region> sample=plain.wav key=d4\n"
             "<region> sample=gone.wav key=e4\n";
    }
    const auto r = importSfzFile (dir.path / "inst.sfz");
    REQUIRE (r.ok);
    CHECK (r.name == "inst");
    CHECK (r.regions == 3);
    REQUIRE (r.instrument.zones.size() == 2);            // le fichier manquant écarte sa région
    const auto& tone = r.instrument.zones[0];
    CHECK (tone.mode == PlayMode::Loop);
    CHECK (tone.loopEnd > tone.loopStart);
    CHECK (r.instrument.zones[1].mode == PlayMode::OneShot);
    bool missing = false;
    for (const auto& w : r.warnings) missing = missing || w.find ("gone.wav") != std::string::npos;
    CHECK (missing);
    CHECK (r.instrument.zones[1].releaseCuts);           // sans boucle : joué en entier, mais la touche relâchée l'interrompt
    CHECK_FALSE (tone.releaseCuts);                       // en boucle : le relâchement habituel
}

TEST_CASE ("sfz: a file without region fails cleanly")
{
    CHECK_FALSE (importSfz ("<global> volume=0\n", noFiles()).ok);
    CHECK_FALSE (importSfz ("", noFiles()).ok);
    CHECK_FALSE (importSfzFile ("/nonexistent/none.sfz").ok);
}

// Essai à la main sur un vrai instrument : NEPTUNE_SFZ=/chemin/instrument.sfz neptune_core_tests "[.sfz-real]"
TEST_CASE ("sfz: a real instrument, from NEPTUNE_SFZ", "[.sfz-real]")
{
    const char* path = std::getenv ("NEPTUNE_SFZ");
    if (path == nullptr) { SUCCEED ("NEPTUNE_SFZ not set"); return; }
    const auto r = importSfzFile (path);
    INFO (r.error);
    REQUIRE (r.ok);
    int loops = 0, cuts = 0, oneShots = 0;
    for (const auto& z : r.instrument.zones)
    {
        if (z.mode == PlayMode::Loop) ++loops;
        else if (z.releaseCuts) ++cuts;
        else ++oneShots;
    }
    std::printf ("SFZ %s: %d regions, %d zones (%d loops, %d cut on release, %d one-shot)\n", r.name.c_str(), r.regions, r.imported, loops, cuts, oneShots);
    for (const auto& w : r.warnings) std::printf ("  warning: %s\n", w.c_str());
    const auto& z0 = r.instrument.zones.front();
    std::printf ("  first zone: keys %d-%d root %d vel %d-%d env release %.0f ms override %d loop %lld-%lld\n", z0.keyLow, z0.keyHigh, z0.rootNote, z0.velLow, z0.velHigh,
                 z0.envelope.release.ms, z0.envelopeOverride ? 1 : 0, static_cast<long long> (z0.loopStart), static_cast<long long> (z0.loopEnd));
}
