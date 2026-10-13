// Groupes de choke et conditions de déclenchement « Premier » / « Legato » (palier 1, jalon 7).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <vector>

#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/MappingEdit.h"
#include "neptune/core/SamplerVoices.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
std::shared_ptr<const SampleData> constant (float v, std::int64_t frames = 96000)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = frames;
    s->sampleRate = 48000.0;
    s->pcm32.assign (static_cast<std::size_t> (frames), v);
    return s;
}

Zone zoneAt (float level, int keyLow, int keyHigh, int cuts = 0, int cutBy = 0, int rr = 0, int group = 0,
             TriggerCondition trig = TriggerCondition::Always)
{
    Zone z;
    z.sample = constant (level);
    z.keyLow = keyLow; z.keyHigh = keyHigh; z.rootNote = keyLow;
    z.loopStart = 0; z.loopEnd = z.sample->frames;
    z.chokeCuts = cuts; z.chokeCutBy = cutBy;
    z.roundRobin = rr;
    z.groupId = group;
    z.trigger = trig;
    return z;
}

struct Rig
{
    Instrument inst;
    SamplerVoices v;
    explicit Rig (Instrument i) : inst (std::move (i))
    {
        inst.finalize();
        v.prepare (48000.0);
        v.setInstrument (&inst);
    }
    float settle (int frames = 2048)
    {
        std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (static_cast<std::size_t> (frames), 0.0f);
        v.render (l.data(), r.data(), frames, 1.0f, 1.0f, 0.0);
        return l[static_cast<std::size_t> (frames - 1)];
    }
};
} // namespace

TEST_CASE ("symmetric choke: a zone of the group cuts the voices of the others")
{
    Instrument inst;
    inst.zones = { zoneAt (0.1f, 60, 60, 1, 1), zoneAt (0.2f, 62, 62, 1, 1), zoneAt (0.4f, 64, 64, 2, 2) };
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    r.v.noteOn (0, 64, 1.0f); // autre groupe : ne coupe pas 60
    CHECK_THAT (r.settle(), WithinAbs (0.1 + 0.4, 1e-5));
    r.v.noteOn (0, 62, 1.0f); // groupe 1 : coupe 60
    CHECK_THAT (r.settle(), WithinAbs (0.2 + 0.4, 1e-5));
    CHECK (r.v.activeVoices() == 2);
}

TEST_CASE ("cuts / cut-by are independent: one-way choke")
{
    // A (60) coupe le groupe 1 sans pouvoir être coupée ; B (62) peut être coupée par le groupe 1 et ne coupe rien
    Instrument inst;
    inst.zones = { zoneAt (0.1f, 60, 60, 1, 0), zoneAt (0.2f, 62, 62, 0, 1) };
    {
        Rig r (inst);
        r.v.noteOn (0, 62, 1.0f);
        r.v.noteOn (0, 60, 1.0f); // A coupe B
        CHECK_THAT (r.settle(), WithinAbs (0.1, 1e-5));
    }
    {
        Rig r (inst);
        r.v.noteOn (0, 60, 1.0f);
        r.v.noteOn (0, 62, 1.0f); // B ne coupe rien
        CHECK_THAT (r.settle(), WithinAbs (0.1 + 0.2, 1e-5));
    }
}

TEST_CASE ("same place is spared unless the instrument asks to cut it too")
{
    // un seul son (une place) avec deux alternatives round-robin, groupe 1 symétrique
    Instrument inst;
    inst.zones = { zoneAt (0.1f, 60, 60, 1, 1, 1), zoneAt (0.1f, 60, 60, 1, 1, 2) };
    inst.sameKey = SameKeyMode::Stack;
    {
        Rig r (inst);
        r.v.noteOn (0, 60, 1.0f);
        r.v.noteOn (0, 60, 1.0f);
        CHECK (r.v.activeVoices() == 2);
        CHECK_THAT (r.settle(), WithinAbs (0.2, 1e-5)); // par défaut la même place n'est pas coupée
    }
    {
        inst.chokeSameZone = true;
        Rig r (inst);
        r.v.noteOn (0, 60, 1.0f);
        r.v.noteOn (0, 60, 1.0f);
        CHECK_THAT (r.settle(), WithinAbs (0.1, 1e-5)); // coupée : une seule voix à la fois
    }
}

TEST_CASE ("release zones can choke too")
{
    Instrument inst;
    Zone rel = zoneAt (0.3f, 60, 60, 1, 0);
    rel.onRelease = true;
    inst.zones = { zoneAt (0.1f, 60, 60, 0, 1), zoneAt (0.1f, 64, 64, 0, 1), rel };
    Rig r (std::move (inst));
    r.v.noteOn (0, 64, 1.0f);
    r.v.noteOn (0, 60, 1.0f);
    r.v.noteOff (0, 60); // la zone de relâchement démarre et coupe tout le groupe 1 (60 et 64)
    CHECK_THAT (r.settle(), WithinAbs (0.3, 1e-5));
}

TEST_CASE ("legato trigger plays only when another note of the Group is held, else falls through")
{
    Instrument inst;
    inst.zones = { zoneAt (0.2f, 48, 84, 0, 0, 0, 0, TriggerCondition::Legato), // transition liée (déclarée en premier)
                   zoneAt (0.5f, 48, 84) };                                       // note normale
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    CHECK_THAT (r.settle(), WithinAbs (0.5, 1e-5)); // première note : normale
    r.v.noteOn (0, 64, 1.0f);                       // 60 est tenue : transition liée
    CHECK_THAT (r.settle(), WithinAbs (0.5 + 0.2, 1e-5));
    r.v.noteOff (0, 60);
    r.v.noteOff (0, 64);
    r.v.allSoundOff();
    r.v.noteOn (0, 67, 1.0f);                       // plus rien de tenu : normale
    CHECK_THAT (r.settle(), WithinAbs (0.5, 1e-5));
}

TEST_CASE ("first trigger plays only when no other note of the Group is held")
{
    Instrument inst;
    inst.zones = { zoneAt (0.3f, 48, 84, 0, 0, 0, 0, TriggerCondition::First), zoneAt (0.5f, 48, 84) };
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    CHECK_THAT (r.settle(), WithinAbs (0.3, 1e-5)); // première note : zone « Premier »
    r.v.noteOn (0, 64, 1.0f);                       // 60 tenue : zone normale
    CHECK_THAT (r.settle(), WithinAbs (0.3 + 0.5, 1e-5));
}

TEST_CASE ("conditions are per Group: a held note of another Group does not count")
{
    Instrument inst;
    inst.zones = { zoneAt (0.2f, 48, 84, 0, 0, 0, /*group*/ 1, TriggerCondition::Legato), zoneAt (0.5f, 48, 84, 0, 0, 0, 1),
                   zoneAt (0.1f, 90, 100, 0, 0, 0, /*group*/ 2) };
    Rig r (std::move (inst));
    r.v.noteOn (0, 95, 1.0f);   // une note du Groupe 2 est tenue
    r.v.noteOn (0, 60, 1.0f);   // Groupe 1 : rien de tenu dans CE Groupe : normale (pas de legato)
    CHECK_THAT (r.settle(), WithinAbs (0.1 + 0.5, 1e-5));
}

TEST_CASE ("places and layer fades keep different trigger conditions apart")
{
    InstrumentDescription d;
    ZoneDescription a, b;
    a.samplePath = "a.wav"; b.samplePath = "b.wav";
    a.keyLow = b.keyLow = 60; a.keyHigh = b.keyHigh = 62;
    b.trigger = TriggerCondition::Legato;
    d.zones = { a, b };
    CHECK (mapping::places (d, mapping::View {}).size() == 2); // pas des alternatives round-robin l'une de l'autre

    const std::vector<LayerKey> keys = { { 0, 127, 1, 40, 0, false, 0 }, { 0, 127, 41, 127, 0, false, 1 } };
    for (const auto& f : computeLayerFades (keys, 20)) { CHECK_FALSE (f.hasIn); CHECK_FALSE (f.hasOut); }
}

TEST_CASE ("choke and trigger settings are saved, bounded, and undoable")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "x.wav";
    z.chokeCuts = 3; z.chokeCutBy = 5;
    z.trigger = TriggerCondition::Legato;
    d.zones.push_back (z);
    d.chokeSameZone = true;
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back == d);
    CHECK (back.zones[0].trigger == TriggerCondition::Legato);
    CHECK (back.zones[0].chokeCuts == 3);
    CHECK (back.zones[0].chokeCutBy == 5);
    CHECK (back.chokeSameZone);

    const auto bad = instrumentFromJson (R"({"zones":[{"sample":"x.wav","choke_cuts":999,"choke_cut_by":-4,"trigger":"nope"},
                                                       {"sample":"y.wav","on_release":true,"trigger":"legato"}]})");
    REQUIRE (bad.zones.size() == 2);
    CHECK (bad.zones[0].chokeCuts == maxChokeGroup);
    CHECK (bad.zones[0].chokeCutBy == 0);
    CHECK (bad.zones[0].trigger == TriggerCondition::Always);
    CHECK (bad.zones[1].trigger == TriggerCondition::Always); // la condition ne concerne pas le relâchement

    InstrumentDescription current = d;
    auto edited = d;
    edited.zones[0].chokeCuts = 7;
    edited.chokeSameZone = false;
    auto get = [&] { return current; };
    auto set = [&] (const InstrumentDescription& x) { current = x; };
    UndoManager um;
    um.perform (std::make_unique<InstrumentEditCommand> (current, edited, get, set, "Choke"));
    CHECK (current == edited);
    um.undo();
    CHECK (current == d);
}
