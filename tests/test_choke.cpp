// Groupes de choke et conditions de déclenchement « Premier » / « Legato » (palier 1, jalon 7).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
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
    // deux vues distinctes (Attaque / Legato) : jamais mélangées, donc jamais en « chevauchement » ni en round-robin
    const auto vs = mapping::views (d);
    REQUIRE (vs.size() == 2);
    CHECK (vs[1].category == mapping::View::Category::Legato);
    CHECK (mapping::places (d, vs[0]).size() == 1);
    CHECK (mapping::places (d, vs[1]).size() == 1);
    CHECK_FALSE (mapping::places (d, vs[0])[0].overlaps);

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

TEST_CASE ("legato transition zones play in mono (retrigger) mode: the held key is tracked, not the voices")
{
    Instrument inst;
    inst.zones = { zoneAt (0.2f, 48, 84, 0, 0, 0, 0, TriggerCondition::Legato), zoneAt (0.5f, 48, 84) };
    inst.polyphony = PolyphonyMode::Mono;
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    CHECK_THAT (r.settle(), WithinAbs (0.5, 1e-5));   // première note : zone normale
    r.v.noteOn (0, 64, 1.0f);                         // 60 tenue : la transition (et plus rien d'autre, c'est du mono)
    CHECK_THAT (r.settle(), WithinAbs (0.2, 1e-5));
    r.v.noteOff (0, 64);                              // retour à 60 (toujours tenue)
    r.v.noteOff (0, 60);
    r.v.noteOn (0, 67, 1.0f);                         // plus rien de tenu : zone normale
    CHECK_THAT (r.settle(), WithinAbs (0.5, 1e-5));
}

TEST_CASE ("a held key still counts when its sample has ended")
{
    Instrument inst;
    Zone first = zoneAt (0.5f, 48, 84);
    first.sample = constant (0.5f, 1000); // très court : fini presque tout de suite
    first.loopEnd = 1000;
    inst.zones = { zoneAt (0.2f, 48, 84, 0, 0, 0, 0, TriggerCondition::Legato), first };
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    r.settle (4096);                                  // l'échantillon est terminé, la touche est toujours enfoncée
    CHECK (r.v.activeVoices() == 0);
    r.v.noteOn (0, 64, 1.0f);
    CHECK_THAT (r.settle (256), WithinAbs (0.2, 1e-5)); // transition liée : une touche est tenue
}

TEST_CASE ("zone preview plays that zone at its root note and can be released or stopped")
{
    Instrument inst;
    Zone loop = zoneAt (0.4f, 60, 72);
    loop.mode = PlayMode::Loop;
    Zone shot = zoneAt (0.1f, 80, 90);
    loop.sourceIndex = 0;
    shot.sourceIndex = 1;
    inst.zones = { loop, shot };
    Rig r (std::move (inst));
    r.v.previewZone (1);
    CHECK (r.v.activeVoices() == 1);
    CHECK_THAT (r.settle(), WithinAbs (0.1 * (1 + 0.0), 0.2)); // un seul son : celui de la zone 1 (vélocité du milieu de plage)
    r.v.previewZone (0);                                      // remplace l'écoute précédente
    r.settle (4096);
    CHECK (r.v.activeVoices() == 1);
    r.v.releasePreview();                                     // boucle : relâchement
    r.settle (96000);
    CHECK (r.v.activeVoices() == 0);
    r.v.previewZone (1);
    r.v.stopPreview();
    r.settle (4096);
    CHECK (r.v.activeVoices() == 0);
    r.v.previewZone (99);                                     // indice invalide : ignoré
    CHECK (r.v.activeVoices() == 0);
}

TEST_CASE ("interface events reach the engine through the queue, in order, and a full queue refuses")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    auto inst = std::make_shared<Instrument>();
    inst->zones.push_back (zoneAt (0.5f, 0, 127));
    inst->zones.back().sourceIndex = 0;
    inst->finalize();
    engine.publishInstrument (inst);

    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    float* out[2] = { l.data(), r.data() };
    engine.process (out, 2, 256); // adopte l'instrument
    CHECK (engine.activeVoices() == 0);

    CHECK (engine.postUiEvent ({ Engine::UiEvent::Type::NoteOn, 0, 60, 1.0f }));
    engine.process (out, 2, 256);
    CHECK (engine.activeVoices() == 1);
    CHECK_THAT (l[200], WithinAbs (0.5, 1e-5));
    CHECK (engine.postUiEvent ({ Engine::UiEvent::Type::PreviewZone, 0, 0, 0.0f }));
    engine.process (out, 2, 256);
    CHECK (engine.activeVoices() == 2);

    int accepted = 0;
    for (int i = 0; i < 1000; ++i) accepted += engine.postUiEvent ({ Engine::UiEvent::Type::NoteOff, 0, 60, 0.0f }) ? 1 : 0;
    CHECK (accepted == 256); // la file est pleine : les suivants sont refusés sans rien casser
    engine.process (out, 2, 256);
    CHECK (engine.postUiEvent ({ Engine::UiEvent::Type::NoteOff, 0, 60, 0.0f })); // vidée : à nouveau acceptée
}

TEST_CASE ("conditional zones win over normal ones whatever the declaration order")
{
    // zone normale déclarée AVANT la zone Legato : la transition doit quand même être jouée quand une touche est tenue
    Instrument inst;
    inst.zones = { zoneAt (0.5f, 48, 84), zoneAt (0.2f, 48, 84, 0, 0, 0, 0, TriggerCondition::Legato) };
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);
    CHECK_THAT (r.settle(), WithinAbs (0.5, 1e-5));          // première note : normale
    r.v.noteOn (0, 64, 1.0f);
    CHECK_THAT (r.settle(), WithinAbs (0.5 + 0.2, 1e-5));    // transition (la note 60 tenue continue de sonner)
}

TEST_CASE ("legato beats first beats normal when several conditions hold")
{
    Instrument inst;
    inst.zones = { zoneAt (0.5f, 48, 84), zoneAt (0.3f, 48, 84, 0, 0, 0, 0, TriggerCondition::First),
                   zoneAt (0.2f, 48, 84, 0, 0, 0, 0, TriggerCondition::Legato) };
    Rig r (std::move (inst));
    r.v.noteOn (0, 60, 1.0f);   // rien de tenu : « Premier » gagne sur « normale »
    CHECK_THAT (r.settle(), WithinAbs (0.3, 1e-5));
    r.v.noteOn (0, 64, 1.0f);   // une touche tenue : « Legato » gagne
    CHECK_THAT (r.settle(), WithinAbs (0.3 + 0.2, 1e-5));
}

TEST_CASE ("played notes are traced for the interface with the velocity after the curve")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    auto inst = std::make_shared<Instrument>();
    inst->zones.push_back (zoneAt (0.5f, 0, 127));
    inst->finalize();
    engine.publishInstrument (inst);
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    float* out[2] = { l.data(), r.data() };
    engine.process (out, 2, 64);

    Engine::NoteTrace t;
    CHECK_FALSE (engine.popNoteTrace (t));
    engine.noteOn (0, 60, 100.0f / 127.0f);
    engine.noteOn (0, 72, 1.0f / 127.0f);
    REQUIRE (engine.popNoteTrace (t));
    CHECK (t.key == 60);
    CHECK (t.velocity == 100);
    REQUIRE (engine.popNoteTrace (t));
    CHECK (t.key == 72);
    CHECK (t.velocity == 1);
    CHECK_FALSE (engine.popNoteTrace (t));
}

TEST_CASE ("the voice monitor snapshot reports what sounds, with zone, key, velocity and state")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    auto inst = std::make_shared<Instrument>();
    Zone a = zoneAt (0.4f, 0, 63);
    a.sourceIndex = 7;
    Zone b = zoneAt (0.2f, 64, 127);
    b.sourceIndex = 9;
    b.mode = PlayMode::Loop;
    inst->zones = { a, b };
    inst->finalize();
    engine.publishInstrument (inst);
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    float* out[2] = { l.data(), r.data() };
    engine.process (out, 2, 256);
    Engine::VoiceInfo v[Engine::kVoiceSnapshot];
    CHECK (engine.readVoices (v, Engine::kVoiceSnapshot) == 0);

    engine.noteOn (0, 60, 100.0f / 127.0f);
    engine.noteOn (0, 70, 30.0f / 127.0f);
    engine.process (out, 2, 256);
    const int n = engine.readVoices (v, Engine::kVoiceSnapshot);
    REQUIRE (n == 2);
    int seen = 0;
    for (int i = 0; i < n; ++i)
    {
        if (v[i].key == 60)
        {
            CHECK (v[i].sourceIndex == 7);
            CHECK (v[i].velocity == 100);
            CHECK_FALSE (v[i].releasing);
            ++seen;
        }
        if (v[i].key == 70)
        {
            CHECK (v[i].sourceIndex == 9);
            CHECK (v[i].velocity == 30);
            ++seen;
        }
        CHECK (v[i].level > 0.0f);
    }
    CHECK (seen == 2);

    engine.noteOff (0, 70); // boucle : passe en relâchement
    engine.process (out, 2, 64);
    const int m = engine.readVoices (v, Engine::kVoiceSnapshot);
    bool releasing = false;
    for (int i = 0; i < m; ++i) releasing = releasing || (v[i].key == 70 && v[i].releasing);
    CHECK (releasing);
}
