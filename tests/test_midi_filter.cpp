// Filtre MIDI avancé : conditions, logique booléenne, bornes, JSON, forme compilée, intégration au moteur.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/MidiFilter.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/State.h"
#include "neptune/core/SynthVoices.h"

using namespace neptune::core;
using namespace neptune::core::midi;

namespace
{
Message note (bool on, int channel, int key, int vel)
{
    Message m;
    m.kind = on ? MsgKind::NoteOn : MsgKind::NoteOff;
    m.channel = channel;
    m.data1 = key;
    m.data2 = vel;
    return m;
}

Message cc (int channel, int number, int value)
{
    Message m;
    m.kind = MsgKind::ControlChange;
    m.channel = channel;
    m.data1 = number;
    m.data2 = value;
    return m;
}

Rule leaf (MsgKind k, int lo1 = 0, int hi1 = 127, int lo2 = 0, int hi2 = 127, int channel = 0)
{
    Rule r;
    r.op = Op::Leaf;
    r.condition.kind = k;
    r.condition.lo1 = lo1;
    r.condition.hi1 = hi1;
    r.condition.lo2 = lo2;
    r.condition.hi2 = hi2;
    r.condition.channel = channel;
    return r;
}

Rule group (Op op, std::vector<Rule> kids)
{
    Rule r;
    r.op = op;
    r.children = std::move (kids);
    return r;
}

FilterDescription with (Rule root)
{
    FilterDescription d;
    d.enabled = true;
    d.root = std::move (root);
    return d;
}
} // namespace

TEST_CASE ("midi condition matches by kind, channel and ranges", "[midi]")
{
    Condition c;
    c.kind = MsgKind::Note;
    c.lo1 = 36;
    c.hi1 = 60;
    c.lo2 = 40;
    c.hi2 = 127;
    CHECK (c.matches (note (true, 0, 48, 100)));
    CHECK (c.matches (note (false, 0, 48, 0) ) == false); // note off : vélocité 0 hors de 40..127
    CHECK_FALSE (c.matches (note (true, 0, 61, 100)));
    CHECK_FALSE (c.matches (note (true, 0, 48, 20)));
    CHECK_FALSE (c.matches (cc (0, 48, 100)));

    c.channel = 3;
    CHECK_FALSE (c.matches (note (true, 0, 48, 100)));
    CHECK (c.matches (note (true, 2, 48, 100))); // canal 3 = indice 2

    Condition cc64;
    cc64.kind = MsgKind::ControlChange;
    cc64.lo1 = cc64.hi1 = 64;
    cc64.lo2 = 64;
    CHECK (cc64.matches (cc (0, 64, 127)));
    CHECK_FALSE (cc64.matches (cc (0, 64, 10)));
    CHECK_FALSE (cc64.matches (cc (0, 65, 127)));

    Condition on;
    on.kind = MsgKind::NoteOn;
    CHECK (on.matches (note (true, 0, 1, 1)));
    CHECK_FALSE (on.matches (note (false, 0, 1, 0)));
}

TEST_CASE ("boolean operators", "[midi]")
{
    const Message m = note (true, 0, 50, 100);
    auto yes = [] { return leaf (MsgKind::Note, 0, 127); };
    auto no = [] { return leaf (MsgKind::Note, 100, 127); }; // note 50 hors de 100..127
    auto test = [&m] (Op op, std::vector<Rule> kids) { return CompiledFilter (with (group (op, std::move (kids)))).passes (m); };

    CHECK (test (Op::And, { yes(), yes() }));
    CHECK_FALSE (test (Op::And, { yes(), no() }));
    CHECK (test (Op::Or, { no(), yes() }));
    CHECK_FALSE (test (Op::Or, { no(), no() }));
    CHECK (test (Op::Xor, { yes(), no() }));
    CHECK_FALSE (test (Op::Xor, { yes(), yes() })); // exactement un
    CHECK_FALSE (test (Op::Xor, { no(), no() }));
    CHECK (test (Op::Nand, { yes(), no() }));
    CHECK_FALSE (test (Op::Nand, { yes(), yes() }));
    CHECK (test (Op::Nor, { no(), no() }));
    CHECK_FALSE (test (Op::Nor, { no(), yes() }));
    CHECK (test (Op::Not, { no() }));
    CHECK_FALSE (test (Op::Not, { yes() }));
    // groupe vide : jamais vrai (ne laisse rien passer par accident)
    CHECK_FALSE (test (Op::And, {}));
    CHECK_FALSE (test (Op::Or, {}));
}

TEST_CASE ("nested rules: a split keyboard with a sustain pedal", "[midi]")
{
    // « notes 36..59 de vélocité >= 20, ET PAS le canal 10 » OU « CC 64 »
    auto root = group (Op::Or, { group (Op::And, { leaf (MsgKind::Note, 36, 59, 20, 127),
                                                    group (Op::Not, { leaf (MsgKind::Note, 0, 127, 0, 127, 10) }) }),
                                 leaf (MsgKind::ControlChange, 64, 64) });
    const CompiledFilter f (with (root));
    CHECK (f.passes (note (true, 0, 40, 90)));
    CHECK_FALSE (f.passes (note (true, 0, 70, 90)));   // trop haut
    CHECK_FALSE (f.passes (note (true, 0, 40, 10)));   // trop doux
    CHECK_FALSE (f.passes (note (true, 9, 40, 90)));   // canal 10
    CHECK (f.passes (cc (5, 64, 127)));                // la pédale passe
    CHECK_FALSE (f.passes (cc (5, 1, 127)));           // pas la molette
}

TEST_CASE ("disabled or default filter lets everything through", "[midi]")
{
    CHECK (CompiledFilter().passes (note (true, 0, 1, 1)));
    FilterDescription d;
    CHECK_FALSE (d.enabled);
    CHECK (CompiledFilter (d).passes (cc (0, 1, 1)));
    d.enabled = true; // activé mais sans aucune règle : un groupe ET vide ne laisse rien passer (choix prudent et documenté)
    CHECK_FALSE (CompiledFilter (d).passes (cc (0, 1, 1)));
}

TEST_CASE ("sanitising bounds the tree and the ranges", "[midi]")
{
    // arbre trop profond, trop large, plages inversées
    Rule deep = leaf (MsgKind::Note);
    for (int i = 0; i < 20; ++i) deep = group (Op::Not, { deep });
    auto d = sanitised (with (deep));
    CHECK (countNodes (d.root) <= maxNodes);

    std::vector<Rule> many;
    for (int i = 0; i < 100; ++i) many.push_back (leaf (MsgKind::Note));
    d = sanitised (with (group (Op::Or, many)));
    CHECK (countNodes (d.root) <= maxNodes);
    CHECK (d.root.children.size() <= static_cast<std::size_t> (maxChildren));

    d = sanitised (with (group (Op::And, { leaf (MsgKind::Note, 90, 10, 200, -5) })));
    const auto& c = d.root.children[0].condition;
    CHECK (c.lo1 == 10);
    CHECK (c.hi1 == 90);
    CHECK (c.lo2 == 0);
    CHECK (c.hi2 == 127);

    // NON : un seul enfant ; une feuille à la racine est enveloppée
    d = sanitised (with (group (Op::Not, { leaf (MsgKind::Note), leaf (MsgKind::Note) })));
    CHECK (d.root.children.size() == 1);
    d = sanitised (with (leaf (MsgKind::NoteOn)));
    CHECK (d.root.op == Op::And);
    CHECK (d.root.children.size() == 1);
}

TEST_CASE ("midi filter json round trip and tolerant reading", "[midi]")
{
    auto d = with (group (Op::Or, { group (Op::Not, { leaf (MsgKind::NoteOn, 10, 20, 30, 40, 4) }), leaf (MsgKind::PitchBend, 0, 127, 64, 127) }));
    d = sanitised (d);
    CHECK (filterFromJson (toJson (d)) == d);
    CHECK_FALSE (filterFromJson ("garbage").enabled);
    CHECK (filterFromJson ("{}") == FilterDescription());
    const auto weird = filterFromJson (R"({"enabled":true,"root":{"op":"and","children":[{"op":"leaf","kind":"cc","lo1":"x"},5,{"op":"bogus"}]}})");
    CHECK (weird.enabled);
    CHECK (countNodes (weird.root) <= maxNodes);
    CHECK (describe (d.root).find ("OR") != std::string::npos);
}

TEST_CASE ("engine routes notes through the per-track midi filter", "[midi][engine]")
{
    Engine e;
    e.prepare (48000.0, 128);
    for (int t = 0; t < 2; ++t)
    {
        e.setTrackActive (t, true);
        e.setTrackType (t, TrackType::Synth);
        SynthDescription sd;
        e.publishSynth (t, std::make_shared<SynthPatch> (sd));
        mixer::StripSettings s;
        s.midiChannel = 0; // tous les canaux
        e.setTrackSettings (t, s);
    }
    // piste 0 : seulement les notes graves ; piste 1 : seulement les notes aiguës fortes
    e.setTrackMidiFilter (0, std::make_shared<CompiledFilter> (with (group (Op::And, { leaf (MsgKind::Note, 0, 59) }))));
    e.setTrackMidiFilter (1, std::make_shared<CompiledFilter> (with (group (Op::And, { leaf (MsgKind::Note, 60, 127, 80, 127) }))));

    std::vector<float> l (128), r (128);
    float* out[2] = { l.data(), r.data() };
    e.process (out, 2, 128); // adopte les filtres et les patchs

    e.noteOn (0, 48, 0.9f);   // grave : piste 0 seulement
    e.noteOn (0, 72, 0.9f);   // aigu fort : piste 1 seulement
    e.noteOn (0, 74, 0.2f);   // aigu doux : aucune des deux
    e.process (out, 2, 128);
    CHECK (e.activeVoices (0) == 1);
    CHECK (e.activeVoices (1) == 1);

    // un note off passe toujours si le note on correspondant a passé (pas de note bloquée)
    e.noteOff (0, 48);
    e.noteOff (0, 72);
    for (int i = 0; i < 400; ++i) e.process (out, 2, 128);
    CHECK (e.activeVoices (0) == 0);
    CHECK (e.activeVoices (1) == 0);
}

TEST_CASE ("midi filter is saved with the strip", "[midi][state]")
{
    MixerState m = MixerState::freshProject();
    m.tracks[0].strip.midiFilter = sanitised (with (group (Op::And, { leaf (MsgKind::Note, 24, 48), group (Op::Not, { leaf (MsgKind::ControlChange, 1, 1) }) })));
    // aller-retour par le JSON d'état du mixeur
    const auto back = mixerFromJson (toJson (m));
    CHECK (back.tracks[0].strip.midiFilter == m.tracks[0].strip.midiFilter);
    CHECK (back.tracks[1].strip.midiFilter == FilterDescription());
}
