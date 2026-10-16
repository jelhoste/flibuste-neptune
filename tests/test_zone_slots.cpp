// Emplacements assignables du Sampler : calcul des effets, description (JSON, retrait de zones), effet sur les voix.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "neptune/core/Instrument.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerVoices.h"
#include "neptune/core/ZoneSlots.h"

using namespace neptune::core;
using Catch::Approx;

namespace
{
SlotRuntimes runtimes (std::initializer_list<std::pair<int, SlotRuntime>> list)
{
    SlotRuntimes r;
    for (const auto& [i, s] : list) r[static_cast<std::size_t> (i)] = s;
    return r;
}

std::shared_ptr<SampleData> sine (std::int64_t frames)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = frames;
    s->sampleRate = 48000.0;
    s->name = "sine";
    s->pcm32.resize (static_cast<std::size_t> (frames));
    for (std::int64_t i = 0; i < frames; ++i) s->pcm32[static_cast<std::size_t> (i)] = 0.5f * std::sin (2.0f * 3.14159265f * 480.0f * static_cast<float> (i) / 48000.0f);
    return s;
}
} // namespace

TEST_CASE ("slot mods: neutral at zero, times scale by powers of ten, level and tune are offsets", "[slots]")
{
    float a[kZoneSlots] {};
    const auto slots = runtimes ({ { 0, { SlotParam::Release, false, 3, 0 } }, { 1, { SlotParam::Level, true, -1, 2 } },
                                   { 2, { SlotParam::Tune, false, 3, 0 } }, { 3, { SlotParam::Sustain, false, 3, 0 } } });
    CHECK_FALSE (resolveSlotMods (slots, a, 3, 2).any == false); // des emplacements visent la zone 3 : "any" vrai même à 0
    auto m = resolveSlotMods (slots, a, 3, 2);
    CHECK (m.release == Approx (1.0f));
    CHECK (m.gain == Approx (1.0f));
    CHECK (m.tuneCents == Approx (0.0f));

    a[0] = -1.0f;      // x0,1
    a[1] = 0.5f;       // niveau du Groupe 2 : +12 dB
    a[2] = 0.25f;      // +25 cents
    a[3] = -1.0f;      // sustain x0
    m = resolveSlotMods (slots, a, 3, 2);
    CHECK (m.release == Approx (0.1f));
    CHECK (m.gain == Approx (std::pow (10.0f, 12.0f / 20.0f)));
    CHECK (m.tuneCents == Approx (25.0f));
    CHECK (m.sustain == Approx (0.0f));

    // une autre zone, hors du Groupe : rien
    m = resolveSlotMods (slots, a, 9, 7);
    CHECK_FALSE (m.any);
    CHECK (m.release == 1.0f);
    // une zone du Groupe 2, pas la zone 3 : seul le niveau
    m = resolveSlotMods (slots, a, 9, 2);
    CHECK (m.gain > 1.0f);
    CHECK (m.release == 1.0f);
}

TEST_CASE ("instrument description: slots round trip, are dropped with their zones and follow the shift", "[slots]")
{
    InstrumentDescription d;
    for (int i = 0; i < 5; ++i)
    {
        ZoneDescription z;
        z.samplePath = "/x/" + std::to_string (i) + ".wav";
        z.group = i == 4 ? "OH" : "";
        d.zones.push_back (z);
    }
    d.slots[0] = { SlotParam::Release, false, 3, "" };
    d.slots[1] = { SlotParam::Level, true, -1, "OH" };
    d.slots[5] = { SlotParam::Tune, false, 1, "" };
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.slots == d.slots);

    d.removeZones ({ 1, 3 });                          // zone 1 retirée (emplacement 5 libéré), zone 3 retirée (emplacement 0 libéré)
    CHECK (d.zones.size() == 3);
    CHECK_FALSE (d.slots[0].assigned());
    CHECK_FALSE (d.slots[5].assigned());
    CHECK (d.slots[1].assigned());                     // le Groupe suit son nom, pas un indice

    InstrumentDescription e;
    e.zones.resize (4);
    e.slots[2] = { SlotParam::Attack, false, 3, "" };
    e.removeZones ({ 0 });                             // une zone avant la cible : la cible passe de 3 à 2
    CHECK (e.slots[2].zone == 2);
    e.slots[4] = { SlotParam::Attack, false, 99, "" }; // visée inexistante
    e.pruneSlots();
    CHECK_FALSE (e.slots[4].assigned());
}

TEST_CASE ("a slot on the release time shortens only the zone it targets, read at note off", "[slots][sampler]")
{
    auto inst = std::make_shared<Instrument>();
    for (int z = 0; z < 2; ++z)
    {
        auto zone = makeFullKeyboardZone (sine (4800), z == 0 ? 60 : 72, PlayMode::Loop);
        zone.keyLow = z == 0 ? 0 : 61;
        zone.keyHigh = z == 0 ? 60 : 127;
        EnvelopeDescription e;
        e.release.ms = 500.0;
        zone.env = EnvelopeRuntime (e);
        zone.sourceIndex = z;
        inst->zones.push_back (std::move (zone));
    }
    inst->slots[0] = { SlotParam::Release, false, 1, 0 };   // la zone 1 seulement
    inst->finalize();

    auto tail = [&inst] (int note, float amount)
    {
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        float amounts[kZoneSlots] {};
        amounts[0] = amount;
        v.setSlotAmounts (amounts);
        v.noteOn (0, note, 1.0f);
        std::vector<float> l (480), r (480);
        for (int i = 0; i < 20; ++i) v.render (l.data(), r.data(), 480, 1.0f, 1.0f, 0.0);
        v.noteOff (0, note);
        int blocks = 0;
        while (v.activeVoices() > 0 && blocks < 400) { v.render (l.data(), r.data(), 480, 1.0f, 1.0f, 0.0); ++blocks; }
        return blocks * 480 / 48000.0; // durée du relâchement en secondes
    };
    CHECK (tail (72, 0.0f) == Approx (0.5).margin (0.03));   // neutre : 500 ms
    CHECK (tail (72, -1.0f) == Approx (0.05).margin (0.03)); // x0,1 : 50 ms
    CHECK (tail (60, -1.0f) == Approx (0.5).margin (0.03));  // l'autre zone n'est pas visée
}

TEST_CASE ("a slot on the level and the tune applies to the zone at note on", "[slots][sampler]")
{
    auto inst = std::make_shared<Instrument>();
    auto zone = makeFullKeyboardZone (sine (48000), 69, PlayMode::Loop);
    zone.sourceIndex = 0;
    inst->zones.push_back (std::move (zone));
    inst->slots[0] = { SlotParam::Level, false, 0, 0 };
    inst->slots[1] = { SlotParam::Tune, false, 0, 0 };
    inst->finalize();
    auto render = [&inst] (float level, float tune)
    {
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        float amounts[kZoneSlots] {};
        amounts[0] = level;
        amounts[1] = tune;
        v.setSlotAmounts (amounts);
        v.noteOn (0, 69, 1.0f);
        std::vector<float> l (4800), r (4800);
        v.render (l.data(), r.data(), 4800, 1.0f, 1.0f, 0.0);
        float peak = 0.0f;
        int crossings = 0;
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            peak = std::max (peak, std::abs (l[i]));
            if (i > 0 && l[i - 1] < 0.0f && l[i] >= 0.0f) ++crossings;
        }
        return std::pair<float, int> { peak, crossings };
    };
    const auto base = render (0.0f, 0.0f);
    const auto loud = render (0.5f, 0.0f);      // +12 dB
    CHECK (loud.first / base.first == Approx (std::pow (10.0f, 12.0f / 20.0f)).epsilon (0.03));
    const auto sharp = render (0.0f, 1.0f);     // +100 cents
    CHECK (static_cast<double> (sharp.second) / base.second == Approx (std::pow (2.0, 100.0 / 1200.0)).epsilon (0.06));
}

TEST_CASE ("slot parameters exist for every track", "[slots]")
{
    ParameterSet set;
    REQUIRE (mixer::addZoneSlotParams (set, 0, ""));
    REQUIRE (mixer::addZoneSlotParams (set, 15, ""));
    CHECK (set.size() == 32);
    CHECK (set.get (mixer::zoneSlotParam (15, 15)) == 0.0);
}

#include "neptune/core/InstrumentEdit.h"

TEST_CASE ("undoing a slot assignment restores the previous one", "[slots]")
{
    InstrumentDescription current;
    current.zones.resize(2);
    InstrumentDescription before = current;
    InstrumentDescription after = current;
    after.slots[3] = { SlotParam::Release, false, 1, "" };
    InstrumentEditCommand cmd (before, after, [&current] { return current; }, [&current] (const InstrumentDescription& d) { current = d; }, "Slots", "slots");
    current = after;
    cmd.undo();
    CHECK_FALSE (current.slots[3].assigned());
    cmd.redo();
    CHECK (current.slots[3].assigned());
}
