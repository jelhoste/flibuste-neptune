// Tests de l'import de zones : lecture des noms, plan d'import, round-robin (palier 1, jalon 3).
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

#include "neptune/core/Instrument.h"
#include "neptune/core/SamplerVoices.h"
#include "neptune/core/ZoneImport.h"

using namespace neptune::core;
using namespace neptune::core::import;

namespace
{
FieldRule rule (Role role, int segment, int start, int minLen, int maxLen) { return FieldRule { role, segment, start, minLen, maxLen }; }

// Le fichier de l'utilisateur : « 048_C3v11 » = numéro de note 048, nom de note C3, « v » puis une couche sur 2 ou 3 chiffres.
Template positionalTemplate()
{
    Template t;
    t.delimiter = ""; // pas de segmentation : tout par position dans le nom
    t.fields = { rule (Role::NoteNumber, 0, 1, 3, 3), rule (Role::NoteName, 0, 5, 2, 2), rule (Role::VelocityLayer, 0, 8, 1, 3) };
    return t;
}

std::string wav (const std::string& stem) { return "/lib/" + stem + ".wav"; }
}

TEST_CASE ("note names follow the octave convention")
{
    NoteConvention c4; c4.middleCOctave = 4;
    NoteConvention c3; c3.middleCOctave = 3;
    CHECK (noteNameToMidi ("C4", c4) == 60);
    CHECK (noteNameToMidi ("C3", c4) == 48);   // le fichier de l'utilisateur : 048 = C3
    CHECK (noteNameToMidi ("C3", c3) == 60);   // autre système : le do central s'appelle C3
    CHECK (noteNameToMidi ("A0", c4) == 21);   // la plus basse note du piano
    CHECK (noteNameToMidi ("C-1", c4) == 0);
    CHECK (noteNameToMidi ("G9", c4) == 127);
    CHECK_FALSE (noteNameToMidi ("G#9", c4).has_value());  // 128
    CHECK (noteNameToMidi ("F#2", c4) == noteNameToMidi ("Gb2", c4));
    CHECK (noteNameToMidi ("c#4", c4) == 61);
    CHECK (noteNameToMidi ("Bb3", c4) == 58);
    CHECK_FALSE (noteNameToMidi ("H3", c4).has_value());
    CHECK_FALSE (noteNameToMidi ("C", c4).has_value());
    CHECK_FALSE (noteNameToMidi ("C3x", c4).has_value());
    CHECK (midiToNoteName (48, c4) == "C3");
    CHECK (midiToNoteName (48, c3) == "C2");
    for (int n = 0; n < 128; ++n) CHECK (noteNameToMidi (midiToNoteName (n, c4), c4) == n);
}

TEST_CASE ("the user's example name is read by character position")
{
    const auto p = parseName ("048_C3v11", positionalTemplate());
    CHECK (p.problems.empty());
    CHECK (p.note == 48);
    CHECK (p.noteFromName == 48);
    CHECK (p.layer == 11);
}

TEST_CASE ("a velocity of two or three characters starting at the eighth")
{
    const auto t = positionalTemplate();
    CHECK (parseName ("048_C3v7", t).layer == 7);
    CHECK (parseName ("048_C3v11", t).layer == 11);
    CHECK (parseName ("048_C3v127", t).layer == 127);
    CHECK_FALSE (parseName ("048_C3v", t).layer.has_value());
    CHECK_FALSE (parseName ("048_C3v", t).problems.empty());
}

TEST_CASE ("segments by delimiter, counted from the start or from the end")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::Group, 1, 1, 1, 0), rule (Role::NoteNumber, 2, 1, 1, 0), rule (Role::VelocityValue, -1, 1, 1, 0) };
    const auto p = parseName ("close_060_90", t);
    CHECK (p.group == "close");
    CHECK (p.note == 60);
    CHECK (p.velValue == 90);
    // une valeur optionnelle absente (pas de segment) n'est pas une erreur ; la note absente en est une
    CHECK (parseName ("close_060", t).problems.empty());
    CHECK (parseName ("close_060", t).velValue == 60); // le dernier segment (« 060 ») est lu comme vélocité
    Template two;
    two.delimiter = "_";
    two.fields = { rule (Role::NoteNumber, 4, 1, 1, 0) };
    CHECK_FALSE (parseName ("a_b", two).problems.empty());
}

TEST_CASE ("characters counted from the end of the name")
{
    Template t;
    t.delimiter = "";
    t.fields = { rule (Role::VelocityValue, 0, -2, 2, 2), rule (Role::NoteNumber, 0, 1, 2, 3) };
    const auto p = parseName ("60piano_vel85", t);
    CHECK (p.note == 60);
    CHECK (p.velValue == 85);
}

TEST_CASE ("round robin accepts numbers or letters, dynamics are ordered labels")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::RoundRobin, 1, 1, 1, 0), rule (Role::Dynamics, 2, 1, 1, 0) };
    CHECK (parseName ("2_mf", t).roundRobin == 2);
    CHECK (parseName ("c_FF", t).roundRobin == 3);
    CHECK (parseName ("c_FF", t).dynamicsRank == 6);
    CHECK (dynamicsRank ("pp") < dynamicsRank ("mp"));
    CHECK_FALSE (parseName ("1_loud", t).problems.empty());
}

TEST_CASE ("out of range values are reported, never clamped silently")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0), rule (Role::VelocityValue, 2, 1, 1, 0) };
    const auto p = parseName ("300_0", t);
    CHECK_FALSE (p.note.has_value());
    CHECK_FALSE (p.velValue.has_value());
    CHECK (p.problems.size() == 2);
}

TEST_CASE ("plan: layers split the velocity range evenly, layer count detected from the names")
{
    std::vector<std::string> files;
    for (int note : { 48, 55 })
        for (int layer = 1; layer <= 4; ++layer)
            files.push_back (wav ("0" + std::to_string (note) + "_C" + std::to_string (note == 48 ? 3 : 4) + "v" + std::to_string (layer)));
    const auto plan = planImport (files, positionalTemplate(), {});
    CHECK (plan.errors() == 0);
    CHECK (plan.detectedLayerCount == 4);
    REQUIRE (plan.zones.size() == 8);
    // 4 couches : 1-31, 32-63, 64-95, 96-127
    CHECK (plan.zones[0].zone.rootNote == 48);
    CHECK (plan.zones[0].zone.velLow == 1);
    CHECK (plan.zones[0].zone.velHigh == 31);
    CHECK (plan.zones[1].zone.velLow == 32);
    CHECK (plan.zones[1].zone.velHigh == 63);
    CHECK (plan.zones[3].zone.velLow == 96);
    CHECK (plan.zones[3].zone.velHigh == 127);
}

TEST_CASE ("plan: key range strategies - exact does nothing, midpoint and fixed on request")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0) };
    const std::vector<std::string> files = { wav ("48"), wav ("52"), wav ("60") };

    auto ranges = [&] (RangeStrategy s, int below = 0, int above = 0)
    {
        PlanOptions o;
        o.keys = { s, below, above };
        std::vector<std::pair<int, int>> r;
        for (const auto& z : planImport (files, t, o).zones) r.push_back ({ z.zone.keyLow, z.zone.keyHigh });
        return r;
    };
    using R = std::vector<std::pair<int, int>>;
    CHECK (ranges (RangeStrategy::Exact) == R { { 48, 48 }, { 52, 52 }, { 60, 60 } });
    CHECK (ranges (RangeStrategy::Midpoint) == R { { 0, 50 }, { 51, 56 }, { 57, 127 } });
    CHECK (ranges (RangeStrategy::Fixed, 1, 2) == R { { 47, 50 }, { 51, 54 }, { 59, 62 } });
}

TEST_CASE ("plan: ranges given in the file names win over the strategy")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0), rule (Role::KeyLow, 2, 1, 1, 0), rule (Role::KeyHigh, 3, 1, 1, 0),
                 rule (Role::VelocityLow, 4, 1, 1, 0), rule (Role::VelocityHigh, 5, 1, 1, 0) };
    PlanOptions o;
    o.keys.strategy = RangeStrategy::Midpoint;
    const auto plan = planImport ({ wav ("60_55_65_20_90") }, t, o);
    REQUIRE (plan.zones.size() == 1);
    CHECK (plan.zones[0].zone.keyLow == 55);
    CHECK (plan.zones[0].zone.keyHigh == 65);
    CHECK (plan.zones[0].zone.velLow == 20);
    CHECK (plan.zones[0].zone.velHigh == 90);
}

TEST_CASE ("plan: velocity values and dynamics")
{
    Template v;
    v.delimiter = "_";
    v.fields = { rule (Role::NoteNumber, 1, 1, 1, 0), rule (Role::VelocityValue, 2, 1, 1, 0) };
    PlanOptions mid;
    mid.velocities.strategy = RangeStrategy::Midpoint;
    const auto a = planImport ({ wav ("60_30"), wav ("60_70"), wav ("60_120") }, v, mid);
    REQUIRE (a.zones.size() == 3);
    CHECK (a.zones[0].zone.velLow == 1);
    CHECK (a.zones[0].zone.velHigh == 50);
    CHECK (a.zones[1].zone.velLow == 51);
    CHECK (a.zones[1].zone.velHigh == 95);
    CHECK (a.zones[2].zone.velHigh == 127);

    Template d;
    d.delimiter = "_";
    d.fields = { rule (Role::NoteNumber, 1, 1, 1, 0), rule (Role::Dynamics, 2, 1, 1, 0) };
    const auto b = planImport ({ wav ("60_pp"), wav ("60_mf"), wav ("60_ff") }, d, {});
    REQUIRE (b.zones.size() == 3);
    CHECK (b.detectedLayerCount == 3);
    CHECK (b.zones[0].zone.velLow == 1);   // pp -> couche 1 sur 3 : 1-42
    CHECK (b.zones[0].zone.velHigh == 42);
    CHECK (b.zones[2].zone.velHigh == 127);
}

TEST_CASE ("plan: round robin indices, duplicates and unreadable names are reported and excluded")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0), rule (Role::RoundRobin, 2, 1, 1, 0) };
    const auto plan = planImport ({ wav ("60_a"), wav ("60_b"), wav ("60_b_copy"), wav ("61_1"), wav ("x_1") }, t, {});
    int included = 0;
    for (const auto& z : plan.zones) included += z.included ? 1 : 0;
    CHECK (included == 3); // 60_a, 60_b, 61_1
    CHECK (plan.errors() == 2);      // doublon de 60_b, et « x » n'est pas une note
    bool sawDup = false, sawBad = false;
    for (const auto& d : plan.diagnostics)
    {
        sawDup = sawDup || d.message.find ("duplicate") != std::string::npos;
        sawBad = sawBad || d.file == "x_1";
    }
    CHECK (sawDup);
    CHECK (sawBad);
    const auto zones = includedZones (plan);
    REQUIRE (zones.size() == 3);
    CHECK (zones[0].roundRobin == 1);
    CHECK (zones[1].roundRobin == 2);
}

TEST_CASE ("plan: same place twice without a round robin field is an error, not a silent alternative")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0) };
    const auto plan = planImport ({ wav ("60_take1"), wav ("60_take2") }, t, {});
    CHECK (plan.errors() == 1);
    CHECK (includedZones (plan).size() == 1);
}

TEST_CASE ("plan: a number and a name that disagree are flagged and the number wins")
{
    const auto plan = planImport ({ wav ("049_C3v1") }, positionalTemplate(), {});
    REQUIRE (plan.zones.size() == 1);
    CHECK (plan.zones[0].zone.rootNote == 49);
    CHECK (plan.warnings() == 1);
}

TEST_CASE ("plan: groups have independent key ranges, release markers are kept apart")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::Group, 1, 1, 1, 0), rule (Role::NoteNumber, 2, 1, 1, 0), rule (Role::Release, 3, 1, 1, 0) };
    PlanOptions o;
    o.keys.strategy = RangeStrategy::Midpoint;
    const auto plan = planImport ({ wav ("close_40"), wav ("close_60"), wav ("room_50"), wav ("close_60_rel") }, t, o);
    CHECK (plan.errors() == 0);
    int release = 0;
    for (const auto& z : plan.zones)
    {
        if (z.zone.onRelease) ++release;
        if (z.zone.group == "room") { CHECK (z.zone.keyLow == 0); CHECK (z.zone.keyHigh == 127); } // seule note du groupe
    }
    CHECK (release == 1);
}

TEST_CASE ("template json round trip and tolerant reading")
{
    Template t = positionalTemplate();
    t.notes.middleCOctave = 3;
    CHECK (Template::fromJson (t.toJson()) == t);
    const auto bad = Template::fromJson (R"({"delimiter":"__________","middle_c_octave":99,"fields":[{"role":"nope"},{"role":"note_number","start":0,"min_length":0}]})");
    CHECK (bad.delimiter == "_");
    CHECK (bad.notes.middleCOctave == 8);
    REQUIRE (bad.fields.size() == 1);
    CHECK (bad.fields[0].minLength == 1);
    CHECK (Template::fromJson ("not json") == Template {});
}

TEST_CASE ("keywords: the same characters can mean RR3, SUS or REL")
{
    Template t = positionalTemplate();   // 048_C3v11 : note et couche par position
    KeywordRule rr;
    rr.keyword = "RR"; rr.valueRole = Role::RoundRobin; rr.digitsMin = 1; rr.digitsMax = 2;
    KeywordRule sus;
    sus.keyword = "SUS"; sus.trigger = Trigger::Attack;
    KeywordRule rel;
    rel.keyword = "REL"; rel.trigger = Trigger::Release;
    t.keywords = { rr, sus, rel };

    const auto a = parseName ("048_C3v11_RR3", t);
    CHECK (a.problems.empty());
    CHECK (a.roundRobin == 3);
    CHECK_FALSE (a.release);

    const auto b = parseName ("048_C3v11_SUS", t);
    CHECK (b.problems.empty());
    CHECK_FALSE (b.roundRobin.has_value());
    CHECK_FALSE (b.release);

    const auto c = parseName ("048_C3v11_rel", t); // insensible à la casse
    CHECK (c.problems.empty());
    CHECK (c.release);
}

TEST_CASE ("keywords at an exact position, or inside a segment")
{
    Template t;
    t.delimiter = "";
    t.fields = { rule (Role::NoteNumber, 0, 1, 3, 3) };
    KeywordRule rr;
    rr.keyword = "RR"; rr.scope = KeywordScope::AtPosition; rr.start = 11; rr.valueRole = Role::RoundRobin; rr.digitsMin = 1; rr.digitsMax = 1;
    KeywordRule rel;
    rel.keyword = "REL"; rel.scope = KeywordScope::AtPosition; rel.start = 11; rel.trigger = Trigger::Release;
    t.keywords = { rr, rel };

    // « 048_C3v11_RR2 » : le mot commence au 11e caractère (0,4,8,_,C,3,v,1,1,_ = 10 caractères avant)
    CHECK (parseName ("048_C3v11_RR2", t).roundRobin == 2);
    CHECK (parseName ("048_C3v11_REL", t).release);
    CHECK_FALSE (parseName ("048_C3v11_REL", t).roundRobin.has_value());
    // le mot à une autre position ne compte pas
    CHECK_FALSE (parseName ("048_C3v11x_RR2", t).roundRobin.has_value());

    Template seg;
    seg.delimiter = "_";
    seg.fields = { rule (Role::NoteNumber, 1, 1, 3, 3) };
    KeywordRule inSeg;
    inSeg.keyword = "REL"; inSeg.scope = KeywordScope::InSegment; inSeg.segment = -1; inSeg.trigger = Trigger::Release;
    seg.keywords = { inSeg };
    CHECK (parseName ("048_C3_REL", seg).release);
    CHECK_FALSE (parseName ("048_REL_C3", seg).release); // « REL » n'est pas dans le dernier segment
}

TEST_CASE ("keywords: digits after the word give a value, missing digits are reported")
{
    Template t;
    t.delimiter = "_";
    t.fields = { rule (Role::NoteNumber, 1, 1, 1, 0) };
    KeywordRule rel;
    rel.keyword = "REL"; rel.trigger = Trigger::Release; rel.valueRole = Role::VelocityValue; rel.digitsMin = 3; rel.digitsMax = 3;
    t.keywords = { rel };
    const auto ok = parseName ("48_REL064", t);
    CHECK (ok.release);
    CHECK (ok.velValue == 64);
    CHECK (ok.problems.empty());
    const auto bad = parseName ("48_REL6", t); // il en faut trois
    CHECK_FALSE (bad.problems.empty());
    CHECK (bad.release); // le mot est bien reconnu
}

TEST_CASE ("plan: sustain and release files of the same note are different places")
{
    Template t = positionalTemplate();
    KeywordRule rr;
    rr.keyword = "RR"; rr.valueRole = Role::RoundRobin; rr.digitsMin = 1; rr.digitsMax = 1;
    KeywordRule sus;
    sus.keyword = "SUS"; sus.trigger = Trigger::Attack;
    KeywordRule rel;
    rel.keyword = "REL"; rel.trigger = Trigger::Release;
    t.keywords = { rr, sus, rel };

    const auto plan = planImport ({ wav ("048_C3v1_RR1"), wav ("048_C3v1_RR2"), wav ("048_C3v1_REL"), wav ("048_C3v1_SUS") }, t, {});
    CHECK (plan.errors() == 0);
    int release = 0, attack = 0;
    for (const auto& z : plan.zones)
    {
        REQUIRE (z.included);
        (z.zone.onRelease ? release : attack)++;
    }
    CHECK (release == 1);
    CHECK (attack == 3); // RR1, RR2 et SUS : trois échantillons à la même place (SUS = principal, numéro 0)
}

TEST_CASE ("a file without round-robin number is the main sample, alternates can be left out")
{
    Template t = positionalTemplate();
    KeywordRule rr;
    rr.keyword = "RR"; rr.valueRole = Role::RoundRobin; rr.digitsMin = 1; rr.digitsMax = 1;
    t.keywords = { rr };
    const std::vector<std::string> files = { wav ("048_C3v1_RR1"), wav ("048_C3v1_RR2"), wav ("048_C3v1"), wav ("050_D3v1_RR2"), wav ("050_D3v1_RR3") };

    const auto all = planImport (files, t, {});
    CHECK (all.errors() == 0);
    CHECK (all.warnings() == 0);
    CHECK (includedZones (all).size() == 5);
    bool mainIsZero = false;
    for (const auto& z : all.zones)
        if (z.zone.rootNote == 48 && z.zone.roundRobin == 0) mainIsZero = true;
    CHECK (mainIsZero);

    PlanOptions light;
    light.mainRoundRobinOnly = true;
    const auto lite = planImport (files, t, light);
    CHECK (lite.errors() == 0);
    const auto zones = includedZones (lite);
    REQUIRE (zones.size() == 2);                 // une par place
    CHECK (zones[0].roundRobin == 0);            // 048 : le principal, sans numéro
    CHECK (zones[1].roundRobin == 2);            // 050 : pas de principal, le plus petit numéro
    int skipped = 0;
    for (const auto& z : lite.zones) skipped += z.skipped ? 1 : 0;
    CHECK (skipped == 3);
}

TEST_CASE ("two main samples at the same place are a duplicate")
{
    Template t = positionalTemplate();
    KeywordRule rr;
    rr.keyword = "RR"; rr.valueRole = Role::RoundRobin; rr.digitsMin = 1; rr.digitsMax = 1;
    t.keywords = { rr };
    const auto plan = planImport ({ wav ("048_C3v1"), wav ("048_C3v1_x") }, t, {});
    CHECK (plan.errors() == 1);
}

TEST_CASE ("template json keeps keyword rules")
{
    Template t = positionalTemplate();
    KeywordRule k;
    k.keyword = "REL"; k.scope = KeywordScope::AtPosition; k.start = 12; k.trigger = Trigger::Release;
    k.valueRole = Role::NoteNumber; k.digitsMin = 3; k.digitsMax = 3; k.group = "release";
    t.keywords.push_back (k);
    CHECK (Template::fromJson (t.toJson()) == t);
    CHECK (Template::fromJson (R"({"keywords":[{"keyword":""},{"keyword":"X","scope":"nope","trigger":"zzz"}]})").keywords.size() == 1);
}

// ---- round-robin et déclenchement au relâchement dans l'instrument ----

namespace
{
std::shared_ptr<const SampleData> constant (float v, std::int64_t frames = 4800)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1; s->frames = frames; s->sampleRate = 48000.0;
    s->pcm32.assign (static_cast<std::size_t> (frames), v);
    return s;
}

Zone zoneOf (std::shared_ptr<const SampleData> s, int rr, int velLow = 1, int velHigh = 127, bool release = false)
{
    Zone z = makeFullKeyboardZone (std::move (s), 60, PlayMode::OneShot);
    z.roundRobin = rr;
    z.velLow = velLow;
    z.velHigh = velHigh;
    z.onRelease = release;
    return z;
}
}

TEST_CASE ("round robin sequential cycles through the set, independently per velocity layer")
{
    Instrument inst;
    inst.zones = { zoneOf (constant (0.1f), 1, 1, 63), zoneOf (constant (0.2f), 2, 1, 63), zoneOf (constant (0.3f), 3, 1, 63),
                   zoneOf (constant (0.9f), 1, 64, 127), zoneOf (constant (0.8f), 2, 64, 127) };
    inst.finalize();
    auto level = [&] (float vel) { return inst.select (60, vel, false)->sample->pcm32[0]; };

    CHECK (level (0.2f) == 0.1f);
    CHECK (level (0.2f) == 0.2f);
    CHECK (level (0.9f) == 0.9f);  // la couche haute a son propre compteur
    CHECK (level (0.2f) == 0.3f);
    CHECK (level (0.2f) == 0.1f);  // retour au début
    CHECK (level (0.9f) == 0.8f);
}

TEST_CASE ("round robin random never picks outside the set, no-repeat never repeats")
{
    Instrument inst;
    inst.roundRobinMode = RoundRobinMode::RandomNoRepeat;
    inst.zones = { zoneOf (constant (0.1f), 1), zoneOf (constant (0.2f), 2), zoneOf (constant (0.3f), 3) };
    inst.finalize();
    float last = -1.0f;
    std::set<float> seen;
    for (int i = 0; i < 300; ++i)
    {
        const float v = inst.select (60, 0.5f, false)->sample->pcm32[0];
        REQUIRE (v != last);
        last = v;
        seen.insert (v);
    }
    CHECK (seen.size() == 3);

    inst.roundRobinMode = RoundRobinMode::Random;
    seen.clear();
    for (int i = 0; i < 300; ++i) seen.insert (inst.select (60, 0.5f, false)->sample->pcm32[0]);
    CHECK (seen.size() == 3);
}

TEST_CASE ("release zones play at note off with the velocity of the note on")
{
    Instrument inst;
    inst.zones = { zoneOf (constant (0.5f, 24000), 0), zoneOf (constant (0.25f, 24000), 0, 1, 127, true) };
    inst.finalize();
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (&inst);
    v.noteOn (0, 60, 0.8f);
    CHECK (v.activeVoices() == 1); // la zone de relâchement ne joue pas au Note On
    v.noteOff (0, 60);
    CHECK (v.activeVoices() == 2); // le one-shot continue, la zone de relâchement démarre
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    v.render (l.data(), r.data(), 256, 1.0f, 1.0f, 0.0);
    CHECK (l[10] == 0.5f * 0.8f + 0.25f * 0.8f);
}
