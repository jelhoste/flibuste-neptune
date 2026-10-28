#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "neptune/core/Mixer.h"
#include "neptune/core/OrganImport.h"

using namespace neptune::core;
using namespace neptune::core::organimport;
using Catch::Approx;

namespace
{
std::string crlf (std::string s)
{
    std::string out;
    for (const char c : s) { if (c == '\n') out += "\r\n"; else out.push_back (c); }
    return out;
}

bool hasWarning (const Result& r, const std::string& part)
{
    for (const auto& w : r.warnings)
        if (w.find (part) != std::string::npos) return true;
    return false;
}

const combi::Memory* memoryOfGeneral (const Result& r, int index) { return r.combinations.general (index / combi::kPistons, index % combi::kPistons); }
double valueIn (const combi::Memory& m, ParamId id, double def = -1.0)
{
    for (const auto& v : m.values)
        if (v.first == id) return v.second;
    return def;
}

// Un petit orgue : un pédalier, deux manuels, des jeux à tuyaux directs et à rangs, des interrupteurs partagés, des accouplements, deux généraux, un divisionnel.
const char* const kOrgan = R"(; orgue de test
[Organ]
ChurchName=Test Organ
NumberOfManuals=2
HasPedals=Y
NumberOfGenerals=2
NumberOfTremulants=1
NumberOfEnclosures=1
NumberOfPanels=1
AmplitudeLevel=100

[Manual000]
Name=Pedal
NumberOfLogicalKeys=3
NumberOfAccessibleKeys=3
FirstAccessibleKeyLogicalKeyNumber=1
FirstAccessibleKeyMIDINoteNumber=36
NumberOfStops=1
Stop001=1

[Manual001]
Name=Great
NumberOfLogicalKeys=5
NumberOfAccessibleKeys=4
FirstAccessibleKeyLogicalKeyNumber=2
FirstAccessibleKeyMIDINoteNumber=48
NumberOfStops=2
Stop001=101
Stop002=102
NumberOfCouplers=2
Coupler001=1
Coupler002=2
NumberOfDivisionals=1

[Manual002]
Name=Swell
NumberOfLogicalKeys=4
NumberOfAccessibleKeys=4
FirstAccessibleKeyLogicalKeyNumber=1
FirstAccessibleKeyMIDINoteNumber=48
NumberOfStops=1
Stop001=201

[Stop001]
Name=Bourdon 16'
Function=And
SwitchCount=1
Switch001=1
NumberOfLogicalPipes=3
NumberOfAccessiblePipes=3
FirstAccessiblePipeLogicalKeyNumber=1
AmplitudeLevel=200
Pipe001=Ped\b1.wav
Pipe002=Ped\b2.wav
Pipe002PitchTuning=-25.5
Pipe003=Ped\b3.wav

[Stop101]
Name=Principal 8'
SwitchCount=1
Switch001=2
NumberOfLogicalPipes=5
NumberOfAccessiblePipes=5
FirstAccessiblePipeLogicalKeyNumber=1
Pipe001=Gt\p1.wav
Pipe002=Gt\p2.wav
Pipe003=Gt\p3.wav
Pipe004=Gt\p4.wav
Pipe005=Gt\p5.wav

[Stop102]
Name=Mixture
Function=And
SwitchCount=2
Switch001=3
Switch002=2
NumberOfRanks=1
Rank001=1
NumberOfAccessiblePipes=4
FirstAccessiblePipeLogicalKeyNumber=2

[Rank001]
Name=Mixture rank
NumberOfLogicalPipes=4
AmplitudeLevel=50
Pipe001=Mx\m1.wav
Pipe001PitchTuning=-1200
Pipe002=Mx\m2.wav
Pipe003=Mx\m3.wav
Pipe004=Mx\m4.wav

[Stop201]
Name=Flute
DefaultToEngaged=Y
NumberOfLogicalPipes=4
NumberOfAccessiblePipes=4
FirstAccessiblePipeLogicalKeyNumber=1
Pipe001=Sw\f1.wav
Pipe002=Sw\f2.wav
Pipe003=Sw\f3.wav
Pipe004=Sw\f4.wav
Pipe002AttackCount=3

[Coupler001]
Name=Great to Swell
SwitchCount=1
Switch001=10
DestinationManual=2
DestinationKeyshift=0

[Coupler002]
Name=Bass octave
SwitchCount=1
Switch001=2
DestinationManual=1
DestinationKeyshift=12
CouplerType=Bass

[Switch001]
Name=Bourdon drawstop
DefaultToEngaged=N

[Switch002]
Name=Principal drawstop
DefaultToEngaged=Y

[Switch003]
Name=Mixture drawstop

[Switch010]
Name=Great to Swell drawstop

[General001]
Name=Tutti
NumberOfSwitches=2
SwitchNumber001=1
SwitchNumber002=-2

[General002]
Name=Soft
NumberOfSwitches=1
SwitchNumber001=2

[Manual001Divisional001]
Name=Fonds
NumberOfSwitches=1
Switch001=2

[Tremulant001]
Name=Tremblant
)";
} // namespace

TEST_CASE ("organ import: the INI reader is tolerant (CRLF, comments, case, Latin-1, byte order mark)")
{
    const auto ini = IniFile::parse (crlf ("\xEF\xBB\xBF; commentaire\n[Organ]\nChurchName = Notre-Dame ; fin de ligne\nNumberOfManuals=2\nhasPedals=Y\nRatio=1,5\n# autre commentaire\n[stop001]\nName=Flûte 8'\nPipe001=Dossier\\fichier#1.wav\n"));
    CHECK (ini.hasSection ("ORGAN"));
    CHECK (ini.get ("organ", "churchname") == "Notre-Dame");
    CHECK (ini.getInt ("Organ", "NumberOfManuals", 0) == 2);
    CHECK (ini.getBool ("Organ", "HasPedals", false));
    CHECK (ini.getDouble ("Organ", "Ratio", 0.0) == Approx (1.5));
    CHECK (ini.getInt ("Organ", "Missing", 7) == 7);
    CHECK (ini.get ("Stop001", "Pipe001") == "Dossier\\fichier#1.wav");
    CHECK (ini.sections() == std::vector<std::string> ({ "Organ", "stop001" }));
    CHECK (IniFile::parse ("[A]\nN=0042 pipes\nM=-003\nK=abc\n").getInt ("A", "N", 0) == 42);
    CHECK (IniFile::parse ("[A]\nM=-003\n").getInt ("A", "M", 0) == -3);
    CHECK (IniFile::parse ("[A]\nK=abc\n").getInt ("A", "K", 9) == 9);

    // texte en Latin-1 (un « é » sur un octet) : converti en UTF-8
    const auto latin = IniFile::parse ("[Stop001]\nName=Fl\xFBte\n");
    CHECK (latin.get ("Stop001", "Name") == "Fl\xC3\xBBte");
    CHECK (ensureUtf8 ("d\xC3\xA9j\xC3\xA0") == "d\xC3\xA9j\xC3\xA0");   // déjà de l'UTF-8 : inchangé
    CHECK (numbered ("Stop", 7) == "Stop007");
    CHECK (numbered ("Manual", 12) == "Manual012");
}

TEST_CASE ("organ import: files that are not organs are refused")
{
    CHECK_FALSE (importOrgan ("[Something]\nA=1\n", {}).ok);
    CHECK_FALSE (importOrgan ("", {}).ok);
    CHECK_FALSE (importOrgan ("[Organ]\nChurchName=X\nNumberOfManuals=0\nHasPedals=N\n", {}).ok);   // sans clavier
    CHECK_FALSE (importOrganFile (std::filesystem::temp_directory_path() / "no_such_organ_file.organ").ok);
}

TEST_CASE ("organ import: keyboards, stops, pipes, switches, couplers and combinations of a small organ")
{
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 2;
    opt.firstChannel = 1;
    const auto r = importOrgan (crlf (kOrgan), opt);
    REQUIRE (r.ok);
    CHECK (r.name == "Test Organ");

    // Façade sommaire : une page par clavier (au moins un bouton de jeu lié à un paramètre, un clavier sur le bon canal), plus une page finale
    REQUIRE (r.facade.pages.size() == 3);
    {
        const auto& page = r.facade.pages[0];   // pages[0] = deuxième page = Great
        int buttons = 0, keyboards = 0;
        for (const auto& w : page.widgets)
        {
            if (w.type == facade::WidgetType::Button && w.param != 0) ++buttons;
            if (w.type == facade::WidgetType::Keyboard && w.channel == 1) ++keyboards;
        }
        CHECK (buttons > 0);
        CHECK (keyboards == 1);
    }

    // trois claviers, le pédalier d'abord ; canaux : les manuels à la suite, le pédalier après
    REQUIRE (r.keyboards.size() == 3);
    CHECK (r.keyboards[0].name == "Pedal");
    CHECK (r.keyboards[0].midiChannel == 3);
    CHECK (r.keyboards[1].name == "Great");
    CHECK (r.keyboards[1].midiChannel == 1);
    CHECK (r.keyboards[2].name == "Swell");
    CHECK (r.keyboards[2].midiChannel == 2);

    // pédalier : trois tuyaux sur les notes 36 à 38, à leur hauteur d'origine ; accord et volume venus du fichier
    const auto& ped = r.keyboards[0].instrument;
    REQUIRE (ped.zones.size() == 3);
    CHECK (ped.zones[0].keyLow == 36);
    CHECK (ped.zones[0].keyHigh == 36);
    CHECK (ped.zones[0].rootNote == 36);
    CHECK (ped.zones[1].keyLow == 37);
    CHECK (ped.zones[1].tuneCents == Approx (-25.5));
    CHECK (ped.zones[0].gainDb == Approx (6.0206).margin (0.001));              // AmplitudeLevel=200 : +6 dB
    CHECK (ped.zones[0].group == "Bourdon 16'");
    CHECK (ped.zones[0].samplePath.find ("Ped/b1.wav") != std::string::npos);   // séparateurs Windows convertis
    CHECK (ped.zones[0].mode == PlayMode::Loop);

    // grand orgue : le tuyau de la touche logique 1 n'est pas accessible ; les touches 2 à 5 sonnent de 48 à 51
    const auto& great = r.keyboards[1].instrument;
    int principal = 0, mixture = 0;
    for (const auto& z : great.zones)
    {
        if (z.group == "Principal 8'") { ++principal; CHECK (z.keyLow >= 48); CHECK (z.keyLow <= 51); }
        if (z.group == "Mixture") ++mixture;
    }
    CHECK (principal == 4);
    CHECK (mixture == 4);
    // un jeu à rang : accord et volume du rang (50 % = -6 dB)
    for (const auto& z : great.zones)
        if (z.group == "Mixture" && z.keyLow == 48) { CHECK (z.tuneCents == Approx (-1200.0)); CHECK (z.gainDb == Approx (-6.0206).margin (0.001)); }
    // interrupteurs : un modulateur par interrupteur, partagé entre les jeux d'un clavier ; un jeu à deux interrupteurs a les deux bits (ET)
    CHECK (great.modulators[0].name == "Principal drawstop");
    CHECK (great.modulators[1].name == "Mixture drawstop");
    CHECK (great.groups.at ("Principal 8'").modulators == 1u);
    CHECK (great.groups.at ("Mixture").modulators == 3u);
    CHECK (r.keyboards[1].stops == 2);

    // un jeu sans interrupteur est son propre tirant ; les variantes d'attaque sont signalées
    const auto& swell = r.keyboards[2].instrument;
    CHECK (swell.zones.size() == 4);
    CHECK (swell.groups.at ("Flute").modulators == 1u);

    // accouplements : le premier est lié à un tirant du routeur (aucun jeu n'utilise son interrupteur), le second au modulateur du Principal
    REQUIRE (r.router.rules.size() == 2);
    const auto& toSwell = r.router.rules[0];
    CHECK (toSwell.name == "Great to Swell");
    CHECK (toSwell.srcChannel == 1);
    CHECK (toSwell.dstChannel == 2);
    CHECK (toSwell.keyLo == 48);
    CHECK (toSwell.keyHi == 51);
    CHECK (toSwell.select == router::Select::All);
    REQUIRE (toSwell.terms.size() == 1);
    CHECK (toSwell.terms[0].param == router::switchParam (0));
    const auto& bass = r.router.rules[1];
    CHECK (bass.select == router::Select::Lowest);
    CHECK (bass.transpose == 12);
    CHECK (bass.dstChannel == 1);
    REQUIRE (bass.terms.size() == 1);
    CHECK (bass.terms[0].param == mixer::modulatorParam (3, 0));   // firstTrack 2 + le clavier d'indice 1 ; modulateur du Principal

    // état initial des interrupteurs
    auto initial = [&] (ParamId id) { for (const auto& v : r.initialValues) if (v.first == id) return v.second; return -1.0; };
    CHECK (initial (mixer::modulatorParam (3, 0)) == 100.0);              // Principal : DefaultToEngaged=Y
    CHECK (initial (mixer::modulatorParam (3, 1)) == 0.0);                // Mixture
    CHECK (initial (mixer::modulatorParam (2, 0)) == 0.0);                // Bourdon
    CHECK (initial (mixer::modulatorParam (4, 0)) == 100.0);              // Flute : tirant propre, engagé
    CHECK (initial (router::switchParam (0)) == 0.0);

    // généraux : tout l'état de la console
    const auto* tutti = memoryOfGeneral (r, 0);
    REQUIRE (tutti != nullptr);
    CHECK (tutti->name == "Tutti");
    CHECK (valueIn (*tutti, mixer::modulatorParam (2, 0)) == 100.0);      // Bourdon tiré
    CHECK (valueIn (*tutti, mixer::modulatorParam (3, 0)) == 0.0);        // Principal repoussé (négatif)
    CHECK (valueIn (*tutti, mixer::modulatorParam (4, 0)) == 0.0);        // le reste repoussé
    CHECK (valueIn (*tutti, router::switchParam (0)) == 0.0);
    const auto* soft = memoryOfGeneral (r, 1);
    REQUIRE (soft != nullptr);
    CHECK (valueIn (*soft, mixer::modulatorParam (3, 0)) == 100.0);
    CHECK (valueIn (*soft, mixer::modulatorParam (2, 0)) == 0.0);

    // divisionnel du grand orgue : seulement les modulateurs de ce clavier
    const auto* fonds = r.combinations.divisional (3, 0, 0);
    REQUIRE (fonds != nullptr);
    CHECK (fonds->name == "Fonds");
    CHECK (valueIn (*fonds, mixer::modulatorParam (3, 0)) == 100.0);
    CHECK (valueIn (*fonds, mixer::modulatorParam (3, 1)) == 0.0);
    CHECK (valueIn (*fonds, mixer::modulatorParam (2, 0)) == -1.0);       // pas ceux du pédalier
    CHECK (valueIn (*fonds, router::switchParam (0)) == -1.0);

    // ce qui n'est pas importé est dit
    CHECK (hasWarning (r, "tremulant"));
    CHECK (hasWarning (r, "expression box"));
    CHECK (hasWarning (r, "graphical panel"));
}

TEST_CASE ("organ import: loops and the release marker are read from the sample files")
{
    const auto dir = std::filesystem::temp_directory_path() / "neptune_organ_import_test";
    std::filesystem::remove_all (dir);
    std::filesystem::create_directories (dir / "Rank");
    {
        SF_INFO info {};
        info.samplerate = 22050; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        SNDFILE* f = sf_open ((dir / "Rank" / "Pipe1.WAV").string().c_str(), SFM_WRITE, &info);   // majuscules : la casse des noms est tolérée
        REQUIRE (f != nullptr);
        SF_INSTRUMENT inst {};
        inst.basenote = 60;
        inst.loop_count = 1;
        inst.loops[0].mode = SF_LOOP_FORWARD;
        inst.loops[0].start = 1000;
        inst.loops[0].end = 3999;     // fin incluse dans le chunk smpl
        REQUIRE (sf_command (f, SFC_SET_INSTRUMENT, &inst, sizeof inst) == SF_TRUE);
        SF_CUES cues {};
        cues.cue_count = 2;
        cues.cue_points[0].indx = 1; cues.cue_points[0].position = 0; cues.cue_points[0].fcc_chunk = 0x61746164; cues.cue_points[0].sample_offset = 500;
        cues.cue_points[1].indx = 2; cues.cue_points[1].position = 0; cues.cue_points[1].fcc_chunk = 0x61746164; cues.cue_points[1].sample_offset = 5000;
        REQUIRE (sf_command (f, SFC_SET_CUE, &cues, sizeof cues) == SF_TRUE);
        std::vector<short> data (8000, 1000);
        sf_writef_short (f, data.data(), static_cast<sf_count_t> (data.size()));
        sf_close (f);
    }
    const std::string odf = crlf (R"([Organ]
ChurchName=Marks
NumberOfManuals=1
HasPedals=N
[Manual001]
Name=Man
NumberOfLogicalKeys=2
NumberOfAccessibleKeys=2
FirstAccessibleKeyMIDINoteNumber=60
NumberOfStops=1
Stop001=1
[Stop001]
Name=Pipe
SwitchCount=1
Switch001=1
NumberOfAccessiblePipes=2
Pipe001=Rank\pipe1.wav
Pipe002=Rank\missing.wav
)");
    Options opt;
    opt.baseDir = dir;
    const auto r = importOrgan (odf, opt);
    REQUIRE (r.ok);
    REQUIRE (r.keyboards[0].instrument.zones.size() == 2);
    const auto& z = r.keyboards[0].instrument.zones[0];
    CHECK (z.mode == PlayMode::Loop);
    CHECK (z.loopStart == 1000);
    CHECK (z.loopEnd == 4000);                  // fin exclusive de Neptune
    CHECK (z.releaseStart == 5000);             // le plus grand repère : le début de la queue
    const auto& missing = r.keyboards[0].instrument.zones[1];
    CHECK (missing.mode == PlayMode::OneShot);  // fichier absent : pas de boucle supposée
    CHECK (hasWarning (r, "missing.wav"));

    // l'instrument se construit et la zone de queue implicite apparaît
    const auto built = buildInstrument (r.keyboards[0].instrument);
    REQUIRE (built != nullptr);
    CHECK (built->zones.size() == 2);           // la zone de la tuyau 1 + sa queue ; le fichier absent n'a pas de zone
    std::filesystem::remove_all (dir);
}

// Contrôle sur le vrai orgue de démonstration de GrandOrgue (facultatif) : NEPTUNE_DEMO_ORGAN_DIR doit désigner son dossier décompressé.
TEST_CASE ("organ import: the GrandOrgue demo organ (optional, NEPTUNE_DEMO_ORGAN_DIR)")
{
    const char* dir = std::getenv ("NEPTUNE_DEMO_ORGAN_DIR");
    if (dir == nullptr || ! std::filesystem::exists (std::filesystem::path (dir) / "demo.organ")) { SUCCEED ("skipped"); return; }
    const auto r = importOrganFile (std::filesystem::path (dir) / "demo.organ");
    INFO ("warnings: " << r.warnings.size());
    for (std::size_t i = 0; i < r.warnings.size() && i < 4; ++i) INFO ("  " << r.warnings[i]);
    if (std::getenv ("NEPTUNE_PRINT_WARNINGS") != nullptr)   // pour regarder ce que l'importeur signale
        for (const auto& w : r.warnings) std::fprintf (stderr, "[import] %s\n", w.c_str());
    REQUIRE (r.ok);
    CHECK (r.name == "GrandOrgue demo V1");
    REQUIRE (r.keyboards.size() == 3);
    int stops = 0, pipes = 0, loops = 0, tails = 0, zoneCount = 0;
    for (const auto& kb : r.keyboards)
    {
        stops += kb.stops;
        pipes += kb.pipes;
        zoneCount += static_cast<int> (kb.instrument.zones.size());
        for (const auto& z : kb.instrument.zones)
        {
            loops += z.mode == PlayMode::Loop && z.loopEnd > z.loopStart;
            tails += z.releaseStart > 0;
        }
    }
    CHECK (stops >= 40);
    CHECK (pipes > 1000);
    CHECK (zoneCount == pipes + 162);   // + 6 variantes d'attaque pour chacun des 27 tuyaux « bruits »
    CHECK (loops == zoneCount);   // chaque fichier a sa boucle
    CHECK (tails == zoneCount);   // et son repère de relâchement
    for (const auto& w : r.warnings) CHECK (w.find ("sample not found") == std::string::npos);
    CHECK (r.router.rules.size() == 5);
    CHECK (r.combinations.generals.size() == 6);
    // le premier clavier se construit tel quel
    std::vector<std::string> buildWarnings;
    const auto built = buildInstrument (r.keyboards[1].instrument, &buildWarnings);
    REQUIRE (built != nullptr);
    CHECK (buildWarnings.empty());
    CHECK (built->zones.size() > r.keyboards[1].instrument.zones.size());   // + les zones de queue implicites
}

TEST_CASE ("organ import: dump of the demo organ (only with NEPTUNE_DUMP_ORGAN=1 and NEPTUNE_DEMO_ORGAN_DIR)", "[.dump]")
{
    const char* dir = std::getenv ("NEPTUNE_DEMO_ORGAN_DIR");
    if (dir == nullptr) return;
    std::filesystem::path odf;
    for (const auto& e : std::filesystem::recursive_directory_iterator (dir))
        if (e.path().extension() == ".organ") { odf = e.path(); break; }
    Options opt;
    const auto r = importOrganFile (odf, opt);
    REQUIRE (r.ok);
    for (const auto& k : r.keyboards)
    {
        int lo = 999, hi = -1;
        std::set<std::string> groups;
        for (const auto& z : k.instrument.zones) { lo = std::min (lo, z.keyLow); hi = std::max (hi, z.keyHigh); groups.insert (z.group); }
        std::printf ("KB %s ch=%d zones=%zu keys=%d..%d groups=%zu mods:", k.name.c_str(), k.midiChannel, k.instrument.zones.size(), lo, hi, groups.size());
        int named = 0;
        for (const auto& m : k.instrument.modulators) if (! m.name.empty()) ++named;
        std::printf ("%d\n", named);
    }
    for (const auto& k : r.keyboards)
    {
        std::map<std::string, std::array<double, 4>> perGroup;   // zones, min gain, max gain, mask
        for (const auto& z : k.instrument.zones)
        {
            auto& g = perGroup.try_emplace (z.group, std::array<double, 4> { 0, 1e9, -1e9, 0 }).first->second;
            g[0] += 1; g[1] = std::min (g[1], z.gainDb); g[2] = std::max (g[2], z.gainDb); g[3] = static_cast<double> (z.modulators);
        }
        for (const auto& [name, g] : perGroup)
        {
            const auto it = k.instrument.groups.find (name);
            std::printf ("GRP %s | %s zones=%d gain=%.1f..%.1f zoneMask=%x groupMask=%x\n", k.name.c_str(), name.c_str(), static_cast<int> (g[0]), g[1], g[2], static_cast<unsigned> (g[3]), it == k.instrument.groups.end() ? 0u : static_cast<unsigned> (it->second.modulators));
        }
        for (const auto& [name, g] : perGroup)
        {
            const auto it = k.instrument.groups.find (name);
            if (it == k.instrument.groups.end()) continue;
            std::printf ("MASK %s | %s :", k.name.c_str(), name.c_str());
            for (int i = 0; i < kModulators; ++i)
                if (((it->second.modulators >> i) & 1u) != 0) std::printf (" [%d %s%s]", i, k.instrument.modulators[static_cast<std::size_t> (i)].name.c_str(), k.instrument.modulators[static_cast<std::size_t> (i)].continuous ? " cont" : "");
            std::printf ("\n");
        }
        std::printf ("TRACKMASK %s %x\n", k.name.c_str(), static_cast<unsigned> (k.instrument.trackModulators));
    }
    {
        std::vector<const facade::Facade*> pages { &r.facade };
        for (const auto& p : r.facade.pages) pages.push_back (&p);
        for (const auto* page : pages)
        {
            std::printf ("PAGE %s:", page->name.c_str());
            for (const auto& w : page->widgets)
                if (w.type == facade::WidgetType::Slider || w.type == facade::WidgetType::Button) std::printf (" %s%s(m%zu)", w.type == facade::WidgetType::Slider ? "SLIDER:" : "", w.text.c_str(), w.mirror.size());
            std::printf ("\n");
        }
    }
    for (const auto& rule : r.router.rules)
        std::printf ("RULE %s src=%d dst=%d keys=%d..%d replace=%d sel=%d terms=%zu\n", rule.name.c_str(), rule.srcChannel, rule.dstChannel, rule.keyLo, rule.keyHi, rule.replace ? 1 : 0, static_cast<int>(rule.select), rule.terms.size());
}

TEST_CASE ("organ import: engine playback of the demo organ (only with NEPTUNE_DEMO_ORGAN_DIR)", "[.dump]")
{
    const char* dir = std::getenv ("NEPTUNE_DEMO_ORGAN_DIR");
    if (dir == nullptr) return;
    std::filesystem::path odf;
    for (const auto& e : std::filesystem::recursive_directory_iterator (dir))
        if (e.path().extension() == ".organ") { odf = e.path(); break; }
    Options opt;
    opt.firstTrack = 1;
    const auto r = importOrganFile (odf, opt);
    REQUIRE (r.ok);
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    SampleCache cache;
    for (std::size_t k = 0; k < r.keyboards.size(); ++k)
    {
        const int t = 1 + static_cast<int> (k);
        const auto built = buildInstrument (r.keyboards[k].instrument, nullptr, &cache);
        REQUIRE (built != nullptr);
        engine.setTrackActive (t, true);
        engine.setTrackType (t, TrackType::Sampler);
        engine.publishInstrument (t, built);
        mixer::StripSettings strip;
        strip.midiChannel = r.keyboards[k].midiChannel;
        engine.setTrackSettings (t, strip);
    }
    engine.setRouter (r.router);
    for (const auto& [id, v] : r.initialValues) params.set (id, v);
    // tire tous les jeux (modulateurs nommés) de chaque clavier
    for (std::size_t k = 0; k < r.keyboards.size(); ++k)
        for (std::size_t i = 0; i < r.keyboards[k].instrument.modulators.size(); ++i)
            if (! r.keyboards[k].instrument.modulators[i].name.empty() && r.keyboards[k].instrument.modulators[i].name.find ("/") == std::string::npos && ! r.keyboards[k].instrument.modulators[i].continuous)
                params.set (mixer::modulatorParam (1 + static_cast<int> (k), static_cast<int> (i)), 100.0);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    float peak = 0.0f;
    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            for (const auto& buf : bufs) for (const float s : buf) peak = std::max (peak, std::abs (s));
        }
    };
    run (4);
    for (std::size_t k = 0; k < r.keyboards.size(); ++k)
    {
        peak = 0.0f;
        engine.noteOnKeyboard (-1, r.keyboards[k].midiChannel - 1, 60, 1.0f);
        run (20);
        std::printf ("PLAY %s ch=%d voices(t%zu)=%d peak=%.4f\n", r.keyboards[k].name.c_str(), r.keyboards[k].midiChannel, k + 1, engine.activeVoices (1 + static_cast<int> (k)), static_cast<double> (peak));
        engine.noteOffKeyboard (-1, r.keyboards[k].midiChannel - 1, 60);
        run (200);
    }
}

TEST_CASE ("organ import: expression boxes and tremulants become continuous modulators on the stops of their windchest")
{
    const char* odf =
        "[Organ]\nChurchName=Box\nNumberOfManuals=1\nHasPedals=N\nNumberOfEnclosures=1\nNumberOfTremulants=1\nNumberOfWindchestGroups=2\n"
        "[Manual001]\nName=Great\nNumberOfStops=2\nStop001=1\nStop002=2\nNumberOfLogicalKeys=2\nNumberOfAccessibleKeys=2\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=Open\nWindchestGroup=1\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\n"
        "[Stop002]\nName=Swell flute\nWindchestGroup=2\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\n"
        "[WindchestGroup001]\nNumberOfEnclosures=0\nNumberOfTremulants=0\n"
        "[WindchestGroup002]\nNumberOfEnclosures=1\nEnclosure001=1\nNumberOfTremulants=1\nTremulant001=1\n"
        "[Enclosure001]\nName=Swell\nAmpMinimumLevel=25\n"
        "[Tremulant001]\nName=Trem\nPeriod=200\nAmpModDepth=40\nSwitchCount=1\nSwitch001=1\n"
        "[Switch001]\nName=Tremblant\nDefaultToEngaged=Y\n";
    Options opt;
    opt.readSampleMarks = false;
    const auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    REQUIRE (r.keyboards.size() == 1);
    const auto& mods = r.keyboards[0].instrument.modulators;
    int boxIndex = -1, tremIndex = -1, gateIndex = -1;
    for (int i = 0; i < kModulators; ++i)
    {
        if (mods[static_cast<std::size_t> (i)].name == "Box Swell") boxIndex = i;
        if (mods[static_cast<std::size_t> (i)].name == "Vibrato Trem") tremIndex = i;
        if (mods[static_cast<std::size_t> (i)].name == "Tremblant") gateIndex = i;
    }
    REQUIRE (boxIndex >= 0);
    REQUIRE (tremIndex >= 0);
    REQUIRE (gateIndex >= 0);
    CHECK (mods[static_cast<std::size_t> (boxIndex)].continuous);
    CHECK (mods[static_cast<std::size_t> (boxIndex)].floorLevel == Approx (0.25));
    CHECK (mods[static_cast<std::size_t> (tremIndex)].tremPeriodMs == Approx (200.0));
    CHECK (mods[static_cast<std::size_t> (tremIndex)].tremDepth == Approx (0.4));
    CHECK (mods[static_cast<std::size_t> (tremIndex)].tremGate == gateIndex);
    // le jeu du sommier sans boîte reste sans ces modulateurs ; celui du sommier avec boîte les porte
    const auto& groups = r.keyboards[0].instrument.groups;
    REQUIRE (groups.count ("Open") == 1);
    REQUIRE (groups.count ("Swell flute") == 1);
    CHECK ((groups.at ("Open").modulators & ((1u << boxIndex) | (1u << tremIndex))) == 0u);
    CHECK ((groups.at ("Swell flute").modulators & (1u << boxIndex)) != 0u);
    CHECK ((groups.at ("Swell flute").modulators & (1u << tremIndex)) != 0u);
    // la profondeur du tremblant part de la valeur de l'orgue (40 %) et a son curseur
    {
        bool depthOk = false;
        for (const auto& [id, v] : r.initialValues) if (id == mixer::modulatorParam (0, tremIndex)) depthOk = v == 40.0;
        CHECK (depthOk);
        bool depthSlider = false;
        for (const auto& w : r.facade.widgets) if (w.type == facade::WidgetType::Slider && w.param == mixer::modulatorParam (0, tremIndex)) depthSlider = true;
        CHECK (depthSlider);
    }
    // la boîte est ouverte au départ ; la Façade lui donne un curseur
    bool opened = false;
    for (const auto& [id, v] : r.initialValues) if (id == mixer::modulatorParam (0, boxIndex)) opened = v == 100.0;
    CHECK (opened);
    bool slider = false;
    for (const auto& w : r.facade.widgets) if (w.type == facade::WidgetType::Slider && w.param == mixer::modulatorParam (0, boxIndex)) slider = true;
    CHECK (slider);
    for (const auto& w : r.facade.widgets)   // la boîte est un curseur, jamais un bouton marche/arrêt, et un tremblant n'a pas de bouton propre
        if (w.type == facade::WidgetType::Button) CHECK (w.param != mixer::modulatorParam (0, boxIndex));
    CHECK_FALSE (hasWarning (r, "expression box"));
}

TEST_CASE ("organ import: box level on the sustain and on the release tail of the demo organ (only with NEPTUNE_DEMO_ORGAN_DIR)", "[.dump]")
{
    const char* dir = std::getenv ("NEPTUNE_DEMO_ORGAN_DIR");
    if (dir == nullptr) return;
    std::filesystem::path odf;
    for (const auto& e : std::filesystem::recursive_directory_iterator (dir))
        if (e.path().extension() == ".organ") { odf = e.path(); break; }
    Options opt;
    opt.firstTrack = 1;
    const auto r = importOrganFile (odf, opt);
    REQUIRE (r.ok);
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    SampleCache cache;
    for (std::size_t k = 0; k < r.keyboards.size(); ++k)
    {
        const int t = 1 + static_cast<int> (k);
        const auto built = buildInstrument (r.keyboards[k].instrument, nullptr, &cache);
        REQUIRE (built != nullptr);
        engine.setTrackActive (t, true);
        engine.setTrackType (t, TrackType::Sampler);
        engine.publishInstrument (t, built);
        mixer::StripSettings strip;
        strip.midiChannel = r.keyboards[k].midiChannel;
        engine.setTrackSettings (t, strip);
    }
    engine.setRouter (r.router);
    for (const auto& [id, v] : r.initialValues) params.set (id, v);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    float peak = 0.0f;
    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            for (const auto& buf : bufs) for (const float s : buf) peak = std::max (peak, std::abs (s));
        }
    };
    for (std::size_t k = 1; k < r.keyboards.size(); ++k)
    {
        const auto& mods = r.keyboards[k].instrument.modulators;
        int box = -1;
        for (int i = 0; i < kModulators; ++i) if (mods[static_cast<std::size_t> (i)].name.rfind ("Box", 0) == 0) box = i;
        // un seul jeu tiré : le premier modulateur nommé qui n'est ni continu ni un interrupteur général
        int stop = -1;   // un jeu dont les zones dépendent de la boîte
        for (const auto& z : r.keyboards[k].instrument.zones)
            if (stop < 0 && box >= 0 && ((z.modulators >> box) & 1u) != 0)
                for (int i = 0; i < kModulators; ++i)
                    if (stop < 0 && ((z.modulators >> i) & 1u) != 0 && ! mods[static_cast<std::size_t> (i)].continuous) stop = i;
        if (stop < 0 && box >= 0)
            for (const auto& [name, g] : r.keyboards[k].instrument.groups)
                if (stop < 0 && ((g.modulators >> box) & 1u) != 0 && name.find ("noise") == std::string::npos)
                    for (int i = 0; i < kModulators; ++i)
                        if (stop < 0 && ((g.modulators >> i) & 1u) != 0 && ! mods[static_cast<std::size_t> (i)].continuous && mods[static_cast<std::size_t> (i)].name.find ("Sounds") == std::string::npos) stop = i;
        if (box < 0 || stop < 0) { std::printf ("BOX %s: no box (%d) or stop (%d)\n", r.keyboards[k].name.c_str(), box, stop); continue; }
        const int t = 1 + static_cast<int> (k);
        params.set (mixer::modulatorParam (t, stop), 100.0);
        if (std::getenv ("WARMUP") != nullptr) { run (4); engine.noteOnKeyboard (-1, r.keyboards[k].midiChannel - 1, 62, 1.0f); run (20); engine.noteOffKeyboard (-1, r.keyboards[k].midiChannel - 1, 62); run (300); }
        for (const double level : { 0.0, 100.0, 0.0, 100.0 })
        {
            params.set (mixer::modulatorParam (t, box), level);
            run (4);
            engine.noteOnKeyboard (-1, r.keyboards[k].midiChannel - 1, 60, 1.0f);
            if (std::getenv ("BLOCKS") != nullptr) { std::printf ("  blocks:"); for (int b = 0; b < 14; ++b) { peak = 0.0f; run (1); std::printf (" %.4f", static_cast<double> (peak)); } std::printf ("\n"); }
            for (int s = 0; s < 6; ++s) { peak = 0.0f; run (10); std::printf ("  t=%d peak=%.4f voices=%d modsOn=%d lastPicks=%d/%d\n", s * 10, static_cast<double> (peak), engine.activeVoices (t), engine.modsOn (t), engine.lastPicks (t), engine.lastKeyZones (t)); }
            peak = 0.0f;
            run (40);
            const float sustain = peak;
            engine.noteOffKeyboard (-1, r.keyboards[k].midiChannel - 1, 60);
            peak = 0.0f;
            run (40);
            const float tail = peak;
            run (800);
            std::printf ("BOX %s box=%g stop=%s sustain=%.4f tail=%.4f\n", r.keyboards[k].name.c_str(), level, mods[static_cast<std::size_t> (stop)].name.c_str(), static_cast<double> (sustain), static_cast<double> (tail));
        }
    }
}


TEST_CASE ("organ import: the first note after idle only plays the stops that are pulled (modulators are fresh on an idle Sampler track)")
{
    const char* odf =
        "[Organ]\nChurchName=Idle\nNumberOfManuals=1\nHasPedals=N\n"
        "[Manual001]\nName=Great\nNumberOfStops=2\nStop001=1\nStop002=2\nNumberOfLogicalKeys=1\nNumberOfAccessibleKeys=1\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=On\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\n"
        "[Stop002]\nName=Off\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    auto& inst = r.keyboards[0].instrument;
    REQUIRE (inst.zones.size() == 2);
    auto sine = std::make_shared<SampleData>();
    sine->channels = 1; sine->frames = 48000; sine->sampleRate = 48000.0;
    sine->pcm32.assign (48000, 0.5f);
    auto instrument = std::make_shared<Instrument>();
    for (std::size_t i = 0; i < inst.zones.size(); ++i)
    {
        auto z = makeFullKeyboardZone (sine, 60, PlayMode::OneShot);
        z.modMask = inst.groups.at (inst.zones[i].group).modulators;
        instrument->zones.push_back (std::move (z));
    }
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Sampler);
    engine.publishInstrument (0, instrument);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256); };
    // « On » tiré, « Off » repoussé : les paramètres sont réglés pendant que la piste est au repos
    for (const auto& [id, v] : r.initialValues) params.set (id, v);
    params.set (mixer::modulatorParam (0, 0), 100.0);
    params.set (mixer::modulatorParam (0, 1), 0.0);
    run (4);
    engine.noteOn (0, 60, 1.0f);
    CHECK (engine.lastPicks (0) == 1);   // seul « On » est choisi dès la première note
}

TEST_CASE ("organ import: a box or a switch shared by the pedal and a manual has one control, on the manual, mirrored onto the pedal")
{
    const char* odf =
        "[Organ]\nChurchName=Shared\nNumberOfManuals=1\nHasPedals=Y\nNumberOfEnclosures=1\nNumberOfTremulants=1\nNumberOfWindchestGroups=1\n"
        "[Manual000]\nName=Pedal\nNumberOfStops=1\nStop001=1\nNumberOfLogicalKeys=1\nNumberOfAccessibleKeys=1\nFirstAccessibleKeyMIDINoteNumber=36\n"
        "[Manual001]\nName=Swell\nNumberOfStops=1\nStop001=2\nNumberOfLogicalKeys=1\nNumberOfAccessibleKeys=1\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=Ped flute\nWindchestGroup=1\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\nSwitchCount=1\nSwitch001=2\n"
        "[Stop002]\nName=Swell flute\nWindchestGroup=1\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\nSwitchCount=1\nSwitch001=2\n"
        "[WindchestGroup001]\nNumberOfEnclosures=1\nEnclosure001=1\nNumberOfTremulants=1\nTremulant001=1\n"
        "[Enclosure001]\nName=Swell\nAmpMinimumLevel=20\n"
        "[Tremulant001]\nName=Trem\nPeriod=200\nAmpModDepth=15\nSwitchCount=1\nSwitch001=1\n"
        "[Switch001]\nName=Tremblant\n[Switch002]\nName=Flutes\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 4;
    const auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    REQUIRE (r.keyboards.size() == 2);
    // pages : pédalier (page principale), puis le manuel
    REQUIRE (r.facade.pages.size() >= 1);
    const auto count = [] (const facade::Facade& page, facade::WidgetType type) { int n = 0; for (const auto& w : page.widgets) if (w.type == type) ++n; return n; };
    CHECK (count (r.facade, facade::WidgetType::Slider) == 0);               // pas de boîte sur la page du pédalier
    CHECK (count (r.facade.pages[0], facade::WidgetType::Slider) == 3);      // la boîte, la profondeur et la vitesse du tremblant sont sur le manuel
    for (const auto& w : r.facade.pages[0].widgets)
        if (w.type == facade::WidgetType::Slider) CHECK (w.mirror.size() == 1);
    // « Tremblant » et « Flutes » : un seul bouton chacun (sur le manuel), qui commande aussi le pédalier
    int pedalButtons = 0, manualButtons = 0, mirrored = 0;
    for (const auto& w : r.facade.widgets) if (w.type == facade::WidgetType::Button) ++pedalButtons;
    for (const auto& w : r.facade.pages[0].widgets) if (w.type == facade::WidgetType::Button) { ++manualButtons; if (w.mirror.size() == 1) ++mirrored; }
    CHECK (pedalButtons == 0);
    CHECK (manualButtons == 2);
    CHECK (mirrored == 2);
}

TEST_CASE ("organ import: attack variants become random alternatives, cut by their minimum velocity")
{
    const char* odf =
        "[Organ]\nChurchName=Attacks\nNumberOfManuals=1\nHasPedals=N\n"
        "[Manual001]\nName=Great\nNumberOfStops=1\nStop001=1\nNumberOfLogicalKeys=2\nNumberOfAccessibleKeys=2\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=Noisy\nNumberOfLogicalPipes=2\nNumberOfAccessiblePipes=2\n"
        "Pipe001=main.wav\nPipe001AttackCount=4\nPipe001Attack001=a.wav\nPipe001Attack002=b.wav\nPipe001Attack002AttackVelocity=100\n"
        "Pipe001Attack003=quick.wav\nPipe001Attack003MaxTimeSinceLastRelease=200\nPipe001Attack004=trem.wav\nPipe001Attack004IsTremulant=1\n"
        "Pipe002=other.wav\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    const auto& zones = r.keyboards[0].instrument.zones;
    // première touche : velocités 1..99 -> principal + a (2 alternatives), 100..127 -> principal + a + b (3) ; deuxième touche : un seul fichier
    REQUIRE (zones.size() == 6);
    auto count = [&] (int key, int velLow, int velHigh)
    {
        std::vector<int> ranks;
        for (const auto& z : zones)
            if (z.keyLow == key && z.velLow == velLow && z.velHigh == velHigh) ranks.push_back (z.roundRobin);
        std::sort (ranks.begin(), ranks.end());
        return ranks;
    };
    CHECK (count (60, 1, 99) == std::vector<int> { 1, 2 });
    CHECK (count (60, 100, 127) == std::vector<int> { 1, 2, 3 });
    CHECK (count (61, 1, 127) == std::vector<int> { 0 });
    CHECK (zones[0].rrMode == static_cast<int> (RoundRobinMode::Random));
    CHECK (r.keyboards[0].pipes == 2);
    CHECK (hasWarning (r, "MaxTimeSinceLastRelease"));
    CHECK_FALSE (hasWarning (r, "not imported (only the main sample)"));
}

TEST_CASE ("continuous zone: an idle Sampler track still plays it through the engine, and its switch silences it")
{
    auto sine = std::make_shared<SampleData>();
    sine->channels = 1; sine->frames = 48000; sine->sampleRate = 48000.0;
    sine->pcm32.assign (48000, 0.5f);
    auto instrument = std::make_shared<Instrument>();
    auto z = makeFullKeyboardZone (sine, 60, PlayMode::Loop);
    z.trigger = TriggerCondition::Continuous;
    z.modMask = 1u;
    instrument->zones.push_back (std::move (z));
    instrument->finalize();
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Sampler);
    engine.publishInstrument (0, instrument);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    auto peak = [&] (int blocks)
    {
        float p = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            for (const float s : bufs[0]) p = std::max (p, std::fabs (s));
        }
        return p;
    };
    params.set (mixer::modulatorParam (0, 0), 100.0);   // interrupteur allumé : aucune note, le son vient seul
    CHECK (peak (80) > 0.01f);
    params.set (mixer::modulatorParam (0, 0), 0.0);
    peak (200);
    CHECK (peak (4) == 0.0f);
}

TEST_CASE ("organ import: separate release samples are chosen by how long the key was held")
{
    const char* odf =
        "[Organ]\nChurchName=Releases\nNumberOfManuals=1\nHasPedals=N\n"
        "[Manual001]\nName=Great\nNumberOfStops=1\nStop001=1\nNumberOfLogicalKeys=2\nNumberOfAccessibleKeys=2\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=Pipes\nNumberOfLogicalPipes=2\nNumberOfAccessiblePipes=2\n"
        "Pipe001=main.wav\nPipe001ReleaseCount=4\nPipe001Release001=r60.wav\nPipe001Release001MaxKeyPressTime=60\n"
        "Pipe001Release002=r280.wav\nPipe001Release002MaxKeyPressTime=280\nPipe001Release003=rall.wav\nPipe001Release003MaxKeyPressTime=-1\n"
        "Pipe001Release004=rtrem.wav\nPipe001Release004IsTremulant=1\n"
        "Pipe002=other.wav\nPipe002ReleaseCount=2\nPipe002Release001=a.wav\nPipe002Release001MaxKeyPressTime=100\nPipe002Release002=b.wav\nPipe002Release002MaxKeyPressTime=500\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    const auto& zones = r.keyboards[0].instrument.zones;
    // touche 60 : 1 attaque + 3 relâchements (le relâchement enregistré avec un tremblant est ignoré) ; touche 61 : 1 attaque + 2 relâchements
    REQUIRE (zones.size() == 7);
    auto bounds = [&] (int key)
    {
        std::vector<std::pair<int, int>> out;
        for (const auto& z : zones)
            if (z.keyLow == key && z.onRelease) out.emplace_back (z.heldMinMs, z.heldMaxMs);
        std::sort (out.begin(), out.end());
        return out;
    };
    CHECK (bounds (60) == std::vector<std::pair<int, int>> { { 0, 60 }, { 61, 280 }, { 281, 0 } });
    // aucun relâchement sans limite : le plus long vaut pour le reste de la durée d'appui
    CHECK (bounds (61) == std::vector<std::pair<int, int>> { { 0, 100 }, { 101, 0 } });
    // l'attaque reste jouée au Note On
    int attacks = 0;
    for (const auto& z : zones) attacks += ! z.onRelease;
    CHECK (attacks == 2);
}

TEST_CASE ("organ import: loops written in the organ file replace those of the sample file")
{
    const char* odf =
        "[Organ]\nChurchName=Loops\nNumberOfManuals=1\nHasPedals=N\n"
        "[Manual001]\nName=Great\nNumberOfStops=1\nStop001=1\nNumberOfLogicalKeys=1\nNumberOfAccessibleKeys=1\nFirstAccessibleKeyMIDINoteNumber=60\n"
        "[Stop001]\nName=Looped\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\n"
        "Pipe001=a.wav\nPipe001LoopCount=2\nPipe001Loop001Start=1000\nPipe001Loop001End=5000\nPipe001Loop002Start=6000\nPipe001Loop002End=9000\nPipe001LoopCrossfadeLength=200\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    const auto& z = r.keyboards[0].instrument.zones.at (0);
    CHECK (z.mode == PlayMode::Loop);
    CHECK (z.loopStart == 1000);
    CHECK (z.loopEnd == 5000);
    CHECK (z.loopCrossfade == 200);
    REQUIRE (z.extraLoops.size() == 1);
    CHECK (z.extraLoops[0].start == 6000);
    CHECK (z.extraLoops[0].end == 9000);
}

TEST_CASE ("organ import: DIAG dry read of an organ file without samples (NEPTUNE_DIAG_ORGAN)", "[.diag]")
{
    const char* path = std::getenv ("NEPTUNE_DIAG_ORGAN");
    if (path == nullptr) { SUCCEED ("skipped"); return; }
    std::ifstream in (path, std::ios::binary);
    std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    if (const char* base = std::getenv ("NEPTUNE_DIAG_BASE")) { opt.baseDir = base; opt.readSampleMarks = true; }   // lit aussi les repères des fichiers d'échantillons
    const auto t0 = std::chrono::steady_clock::now();
    auto r = importOrgan (text, opt);
    std::fprintf (stderr, "DIAG import took %.2f s\n", std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    std::fprintf (stderr, "DIAG ok=%d err=%s name=%s keyboards=%zu\n", r.ok, r.error.c_str(), r.name.c_str(), r.keyboards.size());
    for (const auto& kb : r.keyboards)
    {
        std::size_t rel = 0, loops = 0, attacks = 0;
        for (const auto& z : kb.instrument.zones) { rel += z.onRelease; loops += z.mode == PlayMode::Loop; attacks += ! z.onRelease; }
        std::fprintf (stderr, "DIAG kb %s stops %d pipes %d zones %zu (attacks %zu, releases %zu, loops %zu)\n", kb.name.c_str(), kb.stops, kb.pipes, kb.instrument.zones.size(), attacks, rel, loops);
    }
    for (const auto& w : r.warnings) std::fprintf (stderr, "DIAG warning: %s\n", w.c_str());
    {
        auto show = [] (const neptune::core::facade::Facade& f)
        {
            std::size_t buttons = 0, sliders = 0, keyboards = 0, actions = 0, mirrored = 0, styled = 0;
            for (const auto& w : f.widgets)
            {
                using WT = neptune::core::facade::WidgetType;
                buttons += w.type == WT::Button; sliders += w.type == WT::Slider; keyboards += w.type == WT::Keyboard; actions += w.type == WT::Action || w.type == WT::Set;
                mirrored += ! w.mirror.empty(); styled += w.style != 0;
            }
            std::fprintf (stderr, "DIAG page \"%s\": %zu widgets (buttons %zu, sliders %zu, keyboards %zu, actions %zu; mirrored %zu, with images %zu), %zu styles, background %s\n", f.name.c_str(), f.widgets.size(), buttons, sliders, keyboards, actions, mirrored, styled, f.styles.size(), f.image.empty() ? "none" : "yes");
        };
        show (r.facade);
        for (const auto& pg : r.facade.pages) show (pg);
    }
    if (std::getenv ("NEPTUNE_DIAG_BUILD") != nullptr)   // charge tous les claviers comme le plugin : durée et mémoire
    {
        SampleCache cache;
        std::size_t bytes = 0;
        for (const auto& kb : r.keyboards)
        {
            const auto t1 = std::chrono::steady_clock::now();
            std::vector<std::string> warnings;
            const auto built = buildInstrument (kb.instrument, &warnings, &cache);
            std::size_t kbBytes = 0;
            if (built != nullptr) for (const auto& z : built->zones) (void) z;
            std::fprintf (stderr, "DIAG build %s: %.1f s, %s, %zu warnings, cached files %zu\n", kb.name.c_str(), std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count(), built ? "ok" : "FAILED", warnings.size(), cache.size());
            for (std::size_t i = 0; i < warnings.size() && i < 3; ++i) std::fprintf (stderr, "DIAG   %s\n", warnings[i].c_str());
            (void) kbBytes;
        }
        for (const auto& [k, v] : cache) if (v) bytes += v->bytes();
        std::fprintf (stderr, "DIAG cache: %zu files, %.0f MB\n", cache.size(), static_cast<double> (bytes) / 1048576.0);
        if (std::FILE* st = std::fopen ("/proc/self/status", "r")) { char line[256]; while (std::fgets (line, sizeof line, st)) if (std::strncmp (line, "VmHWM", 5) == 0) std::fprintf (stderr, "DIAG %s", line); std::fclose (st); }
    }
}

TEST_CASE ("organ import: panels become Facade pages with the same positions, images and controls")
{
    const char* odf =
        "[Organ]\nChurchName=Panels\nNumberOfManuals=1\nHasPedals=N\nNumberOfPanels=1\nNumberOfGenerals=1\nNumberOfSwitches=2\n"
        "[Manual001]\nName=Great\nNumberOfStops=2\nStop001=1\nStop002=2\nNumberOfLogicalKeys=2\nNumberOfAccessibleKeys=2\nFirstAccessibleKeyMIDINoteNumber=48\n"
        "[Stop001]\nName=One\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\n"
        "[Stop002]\nName=Two\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\n"
        "[General001]\nName=G1\nNumberOfStops=1\nStop001=1\n"
        "[Panel000]\nName=Console\nDispScreenSizeHoriz=2000\nDispScreenSizeVert=1200\nNumberOfGUIElements=4\n"
        "[Panel000Element001]\nType=Stop\nStop=1\nPositionX=200\nPositionY=300\nImageOn=on.png\nImageOff=off.png\n"
        "[Panel000Element002]\nType=Stop\nStop=2\nPositionX=400\nPositionY=300\nDispLabelText=Two\n"
        "[Panel000Element003]\nType=Manual\nManual=1\nPositionX=100\nPositionY=800\nKey001Width=30\nKey002Width=30\n"
        "[Panel000Element004]\nType=Magic\nPositionX=0\nPositionY=0\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    // la console est la première page ; les pages générées suivent
    CHECK (r.facade.name == "Console");
    REQUIRE (r.facade.pages.size() >= 2);
    namespace fc = neptune::core::facade;
    const auto& page = r.facade;
    int buttons = 0, keyboards = 0;
    const fc::Widget* first = nullptr;
    for (const auto& w : page.widgets)
    {
        buttons += w.type == fc::WidgetType::Button;
        keyboards += w.type == fc::WidgetType::Keyboard;
        if (w.type == fc::WidgetType::Button && first == nullptr) first = &w;
    }
    CHECK (buttons == 2);
    CHECK (keyboards == 1);
    REQUIRE (first != nullptr);
    CHECK (first->style != 0);                       // ses images
    CHECK (first->param != 0);                       // un interrupteur de la piste
    // échelle uniforme 1000 / 2000 = 0,5 : le tirant en (200, 300) est en (100, 150) ; la hauteur 1200 -> 600 remplit le canevas
    CHECK (first->x == 100);
    CHECK (first->y == 150);
    CHECK (hasWarning (r, "magic"));
    CHECK_FALSE (hasWarning (r, "graphical panels are not imported"));
}

TEST_CASE ("organ import: a panel that is only partly positioned is left to the generated pages, keyboards are drawn on top")
{
    const char* odf =
        "[Organ]\nChurchName=Mixed\nNumberOfManuals=1\nHasPedals=N\nNumberOfPanels=1\n"
        "[Manual001]\nName=Great\nNumberOfStops=2\nStop001=1\nStop002=2\nNumberOfLogicalKeys=2\nNumberOfAccessibleKeys=2\nFirstAccessibleKeyMIDINoteNumber=48\n"
        "[Stop001]\nName=One\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\n"
        "[Stop002]\nName=Two\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\n"
        "[Panel000]\nName=Auto\nNumberOfGUIElements=3\n"
        "[Panel000Element001]\nType=Stop\nStop=1\nPositionX=10\nPositionY=10\n"
        "[Panel000Element002]\nType=Stop\nStop=2\nDispDrawstopCol=1\nDispDrawstopRow=1\n"
        "[Panel000Element003]\nType=GC\nPositionX=300\nPositionY=10\n"
        "[Panel001]\nName=Placed\nNumberOfGUIElements=3\n"
        "[Panel001Element001]\nType=Manual\nManual=1\nPositionX=0\nPositionY=0\nKey001Width=40\nKey002Width=40\n"
        "[Panel001Element002]\nType=Stop\nStop=1\nPositionX=10\nPositionY=10\n"
        "[Panel001Element003]\nType=Stop\nStop=2\nPositionX=60\nPositionY=10\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    namespace fc = neptune::core::facade;
    CHECK (r.facade.name == "Placed");   // le panneau à moitié automatique n'a pas de page
    REQUIRE (! r.facade.widgets.empty());
    CHECK (r.facade.widgets.back().type == fc::WidgetType::Keyboard);   // le clavier est dessiné en dernier, donc dessus
    CHECK (hasWarning (r, "place their elements automatically"));
}

TEST_CASE ("organ import: a manual whose element gives the width of only one key still gets a playable keyboard")
{
    const char* odf =
        "[Organ]\nChurchName=Narrow\nNumberOfManuals=1\nHasPedals=N\nNumberOfPanels=0\n"
        "[Manual001]\nName=Great\nNumberOfStops=2\nStop001=1\nStop002=2\nNumberOfLogicalKeys=61\nNumberOfAccessibleKeys=61\nFirstAccessibleKeyMIDINoteNumber=36\n"
        "[Stop001]\nName=One\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=a.wav\n"
        "[Stop002]\nName=Two\nNumberOfLogicalPipes=1\nNumberOfAccessiblePipes=1\nPipe001=b.wav\n"
        "[Panel000]\nName=Console\nDispScreenSizeHoriz=1000\nDispScreenSizeVert=600\nNumberOfGUIElements=3\n"
        "[Panel000Element001]\nType=Manual\nManual=1\nPositionX=100\nPositionY=300\nKey001Width=16\n"
        "[Panel000Element002]\nType=Stop\nStop=1\nPositionX=10\nPositionY=10\n"
        "[Panel000Element003]\nType=Stop\nStop=2\nPositionX=60\nPositionY=10\n";
    Options opt;
    opt.readSampleMarks = false;
    opt.firstTrack = 0;
    auto r = importOrgan (crlf (odf), opt);
    REQUIRE (r.ok);
    namespace fc = neptune::core::facade;
    const fc::Widget* keys = nullptr;
    for (const auto& w : r.facade.widgets) if (w.type == fc::WidgetType::Keyboard) keys = &w;
    REQUIRE (keys != nullptr);
    CHECK (keys->w >= 400);
}
