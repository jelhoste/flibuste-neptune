#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "neptune/core/Control.h"
#include "neptune/core/Engine.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/Modulation.h"

using namespace neptune::core;
using namespace neptune::core::modulation;
using Catch::Approx;

namespace
{
constexpr ParamId kDest = 500;

ParameterSet makeSet()
{
    ParameterSet set;
    set.add (ParamSpec::continuous (kDest, "Dest", Unit::None, 0.0, 100.0, 50.0));
    set.add (ParamSpec::stepped (501, "Steps", Unit::None, 0, 8, 0));
    set.add (ParamSpec::toggle (502, "Toggle"));
    control::addMacroParams (set);
    return set;
}
}

TEST_CASE ("modulation: matrix JSON round trip and bounds")
{
    Matrix m;
    m.lfos[1].shape = LfoShape::Square; m.lfos[1].rateHz = 4.5; m.lfos[1].phase = 0.25;
    m.envelopes[2].attackMs = 40; m.envelopes[2].sustain = 0.3;
    Route r;
    r.source = { SourceKind::ControlChange, 74, 3 };
    r.dest = kDest; r.amount = -0.4; r.centred = true;
    m.routes.push_back (r);
    CHECK (fromJson (toJson (m)) == m);

    Matrix wild;
    wild.lfos[0].rateHz = 9999.0; wild.lfos[0].phase = -3.0;
    wild.envelopes[0].sustain = 5.0;
    Route bad1; bad1.source = { SourceKind::None, 0, 0 }; bad1.dest = kDest;   // sans source
    Route bad2; bad2.source = { SourceKind::Lfo, 99, 0 }; bad2.dest = 0;         // sans destination
    Route ok; ok.source = { SourceKind::Lfo, 99, 7 }; ok.dest = kDest; ok.amount = 9.0;
    wild.routes = { bad1, bad2, ok };
    const auto clean = sanitised (wild);
    CHECK (clean.lfos[0].rateHz == 50.0);
    CHECK (clean.lfos[0].phase == 0.0);
    CHECK (clean.envelopes[0].sustain == 1.0);
    REQUIRE (clean.routes.size() == 1);
    CHECK (clean.routes[0].source.index == kLfos - 1);
    CHECK (clean.routes[0].source.channel == 0);     // un LFO n'a pas de canal
    CHECK (clean.routes[0].amount == 1.0);
    CHECK (fromJson ("not json") == Matrix {});

    Matrix many;
    for (int i = 0; i < 100; ++i) { Route x; x.source = { SourceKind::Lfo, 0, 0 }; x.dest = kDest; many.routes.push_back (x); }
    CHECK (sanitised (many).routes.size() == kMaxRoutes);
}

TEST_CASE ("modulation: only continuous modulatable parameters are compiled")
{
    auto set = makeSet();
    Matrix m;
    for (const ParamId dest : { kDest, ParamId (501), ParamId (502), ParamId (99999) })
    {
        Route r; r.source = { SourceKind::Lfo, 0, 0 }; r.dest = dest;
        m.routes.push_back (r);
    }
    Route macro; macro.source = { SourceKind::Macro, 3, 0 }; macro.dest = kDest;
    m.routes.push_back (macro);
    const auto c = compile (m, set);
    REQUIRE (c->routes.size() == 2);                    // la destination continue, et celle de la macro
    CHECK (c->routes[0].dest == set.indexOf (kDest));
    CHECK (c->routes[1].macro == set.indexOf (control::macroParamId (3)));
}

TEST_CASE ("parameter set: an offset moves the effective value without touching the base")
{
    auto set = makeSet();
    const auto i = set.indexOf (kDest);
    CHECK (set.effectiveAt (i) == Approx (50.0));
    set.setModOffsetAt (i, 0.25f);
    CHECK (set.getAt (i) == Approx (50.0));                    // la valeur réglée ne bouge pas
    CHECK (set.effectiveAt (i) == Approx (75.0));
    CHECK (set.effectiveNormalizedAt (i) == Approx (0.75));
    set.setModOffsetAt (i, 0.9f);
    CHECK (set.effectiveAt (i) == Approx (100.0));             // borné à la plage
    set.setModOffsetAt (i, -2.0f);
    CHECK (set.effectiveAt (i) == Approx (0.0));
    set.setModOffsetAt (i, 0.0f);
    CHECK (set.effectiveAt (i) == Approx (50.0));
}

TEST_CASE ("modulation: an LFO swings the destination, its offset adds up over several routes")
{
    auto set = makeSet();
    Matrix m;
    m.lfos[0].shape = LfoShape::Sine; m.lfos[0].rateHz = 1.0;
    Route r; r.source = { SourceKind::Lfo, 0, 0 }; r.dest = kDest; r.amount = 0.5;
    m.routes = { r, r };                                         // deux routes identiques : les décalages s'ajoutent
    const auto compiled = compile (m, set);
    Modulator mod;
    mod.prepare (48000.0);
    mod.setCompiled (compiled.get());
    const auto idx = set.indexOf (kDest);
    midi::State midi;
    std::vector<std::size_t> touched;
    touched.reserve (64);
    float lo = 10.0f, hi = -10.0f;
    for (int block = 0; block < 100; ++block)                   // 1 s en blocs de 10 ms
    {
        for (const auto t : touched) set.setModOffsetAt (t, 0.0f);
        touched.clear();
        mod.process (480, false, midi, set, touched);
        lo = std::min (lo, set.modOffsetAt (idx));
        hi = std::max (hi, set.modOffsetAt (idx));
    }
    CHECK (hi == Approx (1.0f).margin (0.02f));                  // 2 × 0,5 × sinus
    CHECK (lo == Approx (-1.0f).margin (0.02f));
    CHECK (touched.size() == 2);
}

TEST_CASE ("modulation: an envelope follows the gate (attack, sustain, release)")
{
    auto set = makeSet();
    Matrix m;
    m.envelopes[0].attackMs = 20; m.envelopes[0].decayMs = 20; m.envelopes[0].sustain = 0.5; m.envelopes[0].releaseMs = 100;
    Route r; r.source = { SourceKind::Envelope, 0, 0 }; r.dest = kDest; r.amount = 1.0;
    m.routes = { r };
    const auto compiled = compile (m, set);
    Modulator mod;
    mod.prepare (48000.0);
    mod.setCompiled (compiled.get());
    const auto idx = set.indexOf (kDest);
    midi::State midi;
    std::vector<std::size_t> touched;
    touched.reserve (64);
    auto run = [&] (bool gate)
    {
        for (const auto t : touched) set.setModOffsetAt (t, 0.0f);
        touched.clear();
        mod.process (480, gate, midi, set, touched);
        return set.modOffsetAt (idx);
    };
    CHECK (run (false) == Approx (0.0f));
    const float a1 = run (true);                                 // 10 ms : la moitié de l'attaque de 20 ms
    CHECK (a1 == Approx (0.5f).margin (0.05f));
    for (int i = 0; i < 20; ++i) run (true);                     // attaque, retombée, puis tenue
    CHECK (run (true) == Approx (0.5f).margin (0.01f));          // sustain
    const float r1 = run (false);                                // 10 ms de relâchement de 100 ms
    CHECK (r1 < 0.5f);
    CHECK (r1 > 0.0f);
    for (int i = 0; i < 20; ++i) run (false);
    CHECK (run (false) == Approx (0.0f).margin (0.001f));
}

TEST_CASE ("modulation: MIDI sources read the controller state, bipolar and centred modes")
{
    auto set = makeSet();
    Matrix m;
    Route cc; cc.source = { SourceKind::ControlChange, 74, 0 }; cc.dest = kDest; cc.amount = 0.4;
    m.routes = { cc };
    auto compiled = compile (m, set);
    Modulator mod;
    mod.prepare (48000.0);
    mod.setCompiled (compiled.get());
    const auto idx = set.indexOf (kDest);
    midi::State state;
    std::vector<std::size_t> touched;
    touched.reserve (64);
    auto run = [&] ()
    {
        for (const auto t : touched) set.setModOffsetAt (t, 0.0f);
        touched.clear();
        mod.process (480, false, state, set, touched);
        return set.modOffsetAt (idx);
    };
    CHECK (run() == Approx (0.0f));
    midi::Message msg;
    msg.kind = midi::MsgKind::ControlChange; msg.channel = 2; msg.data1 = 74; msg.data2 = 127;
    state.update (msg);
    CHECK (run() == Approx (0.4f));                              // n'importe quel canal
    m.routes[0].source.channel = 1;                              // le canal 1 seulement : la valeur vient du canal 3 (indice 2), pas du canal 1
    compiled = compile (m, set);
    mod.setCompiled (compiled.get());
    CHECK (run() == Approx (0.0f));
    m.routes[0].source = { SourceKind::PitchBend, 0, 0 };
    m.routes[0].amount = 1.0;
    compiled = compile (m, set);
    mod.setCompiled (compiled.get());
    CHECK (run() == Approx (0.0f));                              // pitch bend au repos : 64 = centre
    msg.kind = midi::MsgKind::PitchBend; msg.channel = 0; msg.data2 = 127;
    state.update (msg);
    CHECK (run() == Approx (63.0f / 64.0f).margin (0.001f));
    msg.data2 = 0;
    state.update (msg);
    CHECK (run() == Approx (-1.0f));

    // une source unipolaire « centrée » : la moitié de la source ne module pas
    m.routes[0].source = { SourceKind::Aftertouch, 0, 0 };
    m.routes[0].centred = true;
    compiled = compile (m, set);
    mod.setCompiled (compiled.get());
    CHECK (run() == Approx (-1.0f));                             // pression 0 -> -1
    msg.kind = midi::MsgKind::ChannelPressure; msg.channel = 0; msg.data2 = 127;
    state.update (msg);
    CHECK (run() == Approx (1.0f));
}

TEST_CASE ("modulation: the engine applies a track's matrix to the effective volume and keeps the base value")
{
    ParameterSet set;
    std::string error;
    REQUIRE (probe::buildPool (set, 2, &error));
    REQUIRE (sampler::addParams (set, &error));
    Engine engine;
    engine.prepare (48000.0, 256);
    engine.attachParameters (&set);
    engine.setTrackActive (0, true);
    mixer::StripSettings strip;
    Route r;
    r.source = { SourceKind::ControlChange, 1, 0 };
    r.dest = probe::trackParam (0, probe::kPan);
    r.amount = 0.25;
    strip.modulation.routes = { r };
    engine.setTrackSettings (0, strip);
    std::vector<float> l (256, 0.0f), rr (256, 0.0f);
    float* out[2] = { l.data(), rr.data() };
    midi::Message cc;
    cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 1; cc.data2 = 127;
    engine.midiController (cc);
    engine.process (out, 2, 256);                                // le thread audio adopte la matrice
    engine.process (out, 2, 256);                                // et l'applique au bloc suivant
    const auto pan = set.indexOf (probe::trackParam (0, probe::kPan));
    CHECK (set.getAt (pan) == Approx (0.0));                     // la valeur réglée n'a pas bougé
    CHECK (set.modOffsetAt (pan) == Approx (0.25f));             // CC 1 à fond × 0,25 de la course
    CHECK (set.effectiveAt (pan) == Approx (50.0));              // pan -100..100 : le centre (0,5 normalisé) + 0,25 = 0,75 -> +50
    cc.data2 = 0;
    engine.midiController (cc);
    engine.process (out, 2, 256);
    CHECK (set.modOffsetAt (pan) == Approx (0.0f));
}
