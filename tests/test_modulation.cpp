#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/Control.h"
#include "neptune/core/Engine.h"
#include "neptune/core/FmVoices.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Synth.h"
#include "neptune/core/WaveCatalog.h"
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
    auto fixed = ParamSpec::stepped (503, "Fixed", Unit::None, 0, 8, 0);
    fixed.modulatable = false;
    set.add (fixed);
    control::addMacroParams (set);
    return set;
}
}

TEST_CASE ("modulation: matrix JSON round trip and bounds")
{
    Matrix m;
    m.lfos[1].shape = LfoShape::Square; m.lfos[1].rateHz = 4.5; m.lfos[1].phase = 0.25;
    m.envelopes[2].env.attack.ms = 40; m.envelopes[2].env.sustain = 0.3; m.envelopes[2].env.delay.on = true; m.envelopes[2].env.delay.ms = 15; m.envelopes[2].env.decay.curve.shape = 0.4;
    Route r;
    r.source = { SourceKind::ControlChange, 74, 3 };
    r.dest = kDest; r.amount = -0.4; r.centred = true;
    m.routes.push_back (r);
    CHECK (fromJson (toJson (m)) == m);

    Matrix wild;
    wild.lfos[0].rateHz = 9999.0; wild.lfos[0].phase = -3.0;
    wild.envelopes[0].env.sustain = 5.0;
    Route bad1; bad1.source = { SourceKind::None, 0, 0 }; bad1.dest = kDest;   // sans source
    Route bad2; bad2.source = { SourceKind::Lfo, 99, 0 }; bad2.dest = 0;         // sans destination
    Route ok; ok.source = { SourceKind::Lfo, 99, 7 }; ok.dest = kDest; ok.amount = 9.0;
    wild.routes = { bad1, bad2, ok };
    const auto clean = sanitised (wild);
    CHECK (clean.lfos[0].rateHz == 50.0);
    CHECK (clean.lfos[0].phase == 0.0);
    CHECK (clean.envelopes[0].env.sustain == 1.0);
    REQUIRE (clean.routes.size() == 1);
    CHECK (clean.routes[0].source.index == kLfos - 1);
    CHECK (clean.routes[0].source.channel == 0);     // un LFO n'a pas de canal
    CHECK (clean.routes[0].amount == 1.0);
    CHECK (fromJson ("not json") == Matrix {});

    Matrix many;
    for (int i = 0; i < 100; ++i) { Route x; x.source = { SourceKind::Lfo, 0, 0 }; x.dest = kDest; many.routes.push_back (x); }
    CHECK (sanitised (many).routes.size() == kMaxRoutes);
}

TEST_CASE ("modulation: continuous and stepped modulatable parameters are compiled")
{
    auto set = makeSet();
    Matrix m;
    for (const ParamId dest : { kDest, ParamId (501), ParamId (502), ParamId (503), ParamId (99999) })
    {
        Route r; r.source = { SourceKind::Lfo, 0, 0 }; r.dest = dest;
        m.routes.push_back (r);
    }
    Route macro; macro.source = { SourceKind::Macro, 3, 0 }; macro.dest = kDest;
    m.routes.push_back (macro);
    const auto c = compile (m, set);
    REQUIRE (c->routes.size() == 4);                    // continue, par pas, interrupteur (pas la destination non modulable ni l'inconnue), et la macro
    CHECK (c->routes[0].dest == set.indexOf (kDest));
    CHECK (c->routes[1].dest == set.indexOf (501));
    CHECK (c->routes[2].dest == set.indexOf (502));
    CHECK (c->routes[3].macro == set.indexOf (control::macroParamId (3)));
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
    m.envelopes[0].env.attack.ms = 20; m.envelopes[0].env.decay.ms = 20; m.envelopes[0].env.sustain = 0.5; m.envelopes[0].env.release.ms = 100;
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

TEST_CASE ("modulation: the first envelope format (attack, decay, sustain, release) is still read")
{
    const auto m = fromJson (R"({"envelopes":[{"a":30,"d":120,"s":0.4,"r":250}],"routes":[]})");
    CHECK (m.envelopes[0].env.attack.ms == 30.0);
    CHECK (m.envelopes[0].env.decay.ms == 120.0);
    CHECK (m.envelopes[0].env.sustain == 0.4);
    CHECK (m.envelopes[0].env.release.ms == 250.0);
    CHECK (! m.envelopes[0].env.delay.on);
    CHECK (! m.envelopes[0].env.hold.on);
}

TEST_CASE ("modulation: an envelope delay holds the output at zero before the attack")
{
    auto set = makeSet();
    Matrix m;
    auto& e = m.envelopes[0].env;
    e.delay.on = true; e.delay.ms = 50; e.attack.ms = 1; e.decay.on = false; e.sustain = 1.0;
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
    auto run = [&]
    {
        for (const auto t : touched) set.setModOffsetAt (t, 0.0f);
        touched.clear();
        mod.process (480, true, midi, set, touched);
        return set.modOffsetAt (idx);
    };
    CHECK (run() == Approx (0.0f).margin (0.001f));   // 10 ms : encore dans le délai
    for (int i = 0; i < 8; ++i) run();                // 90 ms : passé le délai et l'attaque
    CHECK (run() == Approx (1.0f).margin (0.01f));
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

TEST_CASE ("modulation: a route reaches the synth oscillator level through the engine")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    auto peakOf = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            peak = 0.0f;
            for (const float s : bufs[0]) peak = std::max (peak, std::abs (s));
        }
        return peak;
    };
    SynthDescription d;
    d.osc[0].wave = kWaveSine;
    d.filters.filter[0].on = false;
    d.ampEnv.attack.ms = 0.0;
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (d));
    mixer::StripSettings strip;
    Route r;
    r.source = { SourceKind::ControlChange, 1, 0 };
    r.dest = mixer::synthParam (0, mixer::kLevelOsc1);
    r.amount = -1.0;                                             // à fond : le niveau de l'oscillateur 1 tombe à zéro
    strip.modulation.routes = { r };
    engine.setTrackSettings (0, strip);
    peakOf (1);
    engine.noteOn (0, 60, 1.0f);
    CHECK (peakOf (8) > 0.05f);                                  // CC à zéro : l'oscillateur sonne
    midi::Message cc;
    cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 1; cc.data2 = 127;
    engine.midiController (cc);
    CHECK (peakOf (16) < 0.005f);                                // CC à fond : silence
    CHECK (params.getAt (params.indexOf (r.dest)) == Approx (100.0));   // le potard n'a pas bougé
    cc.data2 = 0;
    engine.midiController (cc);
    CHECK (peakOf (16) > 0.05f);                                 // et ça revient
}

TEST_CASE ("modulation: a route reaches the FM operator levels through the engine")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    float* out[2] = { l.data(), r.data() };
    auto peakOf = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (out, 2, 256);
            peak = 0.0f;
            for (const float s : l) peak = std::max (peak, std::abs (s));
        }
        return peak;
    };
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Fm);
    engine.publishFm (0, std::make_shared<fm::FmDescription>());
    mixer::StripSettings strip;
    for (int op = 0; op < 6; ++op)
    {
        Route r2;
        r2.source = { SourceKind::ControlChange, 1, 0 };
        r2.dest = mixer::fmParam (0, static_cast<mixer::FmKind> (mixer::kFmLevelOp1 + op));
        r2.amount = -0.5;                                        // décalage de -50 sur chaque opérateur
        strip.modulation.routes.push_back (r2);
    }
    engine.setTrackSettings (0, strip);
    peakOf (1);
    engine.noteOnTrack (0, 0, 60, 0.8f);
    const float open = peakOf (30);
    CHECK (open > 0.01f);
    midi::Message cc;
    cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 1; cc.data2 = 127;
    engine.midiController (cc);
    const float moved = peakOf (30);
    INFO ("open " << open << " moved " << moved);
    CHECK (std::abs (moved - open) > open * 0.2f);               // les niveaux d'opérateurs ont bougé : le son a changé
}

TEST_CASE ("modulation: a route reaches a parameter of an insert, and the knob value stays put")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    auto peakOf = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            peak = 0.0f;
            for (const float s : bufs[0]) peak = std::max (peak, std::abs (s));
        }
        return peak;
    };
    SynthDescription d;
    d.osc[0].wave = kWaveSine;
    d.filters.filter[0].on = false;
    d.ampEnv.attack.ms = 0.0;
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (d));
    auto insert = createInsert ("utility", 48000.0, 256);
    REQUIRE (insert != nullptr);
    engine.setInsert (StripKind::Track, 0, 2, insert);           // l'emplacement 3
    mixer::StripSettings strip;
    Route r;
    r.source = { SourceKind::ControlChange, 1, 0 };
    r.dest = insertDest (2, stock::outputTrim);
    r.amount = -1.0;                                             // le trim de sortie tombe de 0 dB à -24 dB
    strip.modulation.routes = { r };
    engine.setTrackSettings (0, strip);
    peakOf (1);
    engine.noteOn (0, 60, 1.0f);
    const float open = peakOf (16);
    CHECK (open > 0.05f);
    midi::Message cc;
    cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 1; cc.data2 = 127;
    engine.midiController (cc);
    const float closed = peakOf (30);
    CHECK (closed < open * 0.2f);
    CHECK (closed > open * 0.02f);                               // -24 dB, pas le silence
    const auto trim = insert->runner.params().indexOf (stock::outputTrim);
    CHECK (insert->runner.params().getAt (trim) == Approx (0.0));        // le réglage du module n'a pas bougé
    CHECK (insert->runner.params().modOffsetAt (trim) < -0.4f);          // et la modulation est lisible (anneau du potard)
    cc.data2 = 0;
    engine.midiController (cc);
    CHECK (peakOf (30) == Approx (open).epsilon (0.05));         // elle disparaît avec la source

    // un autre module sur l'emplacement : l'ancien décalage ne le suit pas
    auto other = createInsert ("utility", 48000.0, 256);
    engine.setInsert (StripKind::Track, 0, 2, other);
    engine.midiController (cc);
    CHECK (peakOf (30) == Approx (open).epsilon (0.05));
}

TEST_CASE ("modulation: a route reaches a switch (track mute) and flips it past the middle, the base value stays put")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    auto peakOf = [&] (int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            peak = 0.0f;
            for (const float s : bufs[0]) peak = std::max (peak, std::abs (s));
        }
        return peak;
    };
    SynthDescription d;
    d.osc[0].wave = kWaveSine;
    d.filters.filter[0].on = false;
    d.ampEnv.attack.ms = 0.0;
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (d));
    mixer::StripSettings strip;
    Route r;
    r.source = { SourceKind::ControlChange, 1, 0 };
    r.dest = probe::trackParam (0, probe::kMute);
    r.amount = 1.0;
    strip.modulation.routes = { r };
    engine.setTrackSettings (0, strip);
    peakOf (1);
    engine.noteOn (0, 60, 1.0f);
    const float open = peakOf (16);
    CHECK (open > 0.05f);
    midi::Message cc;
    cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 1; cc.data2 = 127;
    engine.midiController (cc);
    CHECK (peakOf (30) < open * 0.02f);                          // coupée par la modulation
    CHECK (params.get (probe::trackParam (0, probe::kMute)) == Approx (0.0));   // l'interrupteur réglé n'a pas bougé
    cc.data2 = 0;
    engine.midiController (cc);
    CHECK (peakOf (30) == Approx (open).epsilon (0.05));         // et revient avec la source
}

TEST_CASE ("modulation: the track type and the FM algorithm cannot be modulated")
{
    ParameterSet params;
    REQUIRE (probe::buildPool (params, 2));
    CHECK_FALSE (params.spec (params.indexOf (probe::trackParam (0, probe::kType))).modulatable);
    CHECK_FALSE (params.spec (params.indexOf (probe::algorithm)).modulatable);
}

TEST_CASE ("modulation: a large host buffer is evaluated in short pieces, the LFO keeps its pace")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 4096);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (4096, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    engine.setTrackActive (0, true);
    mixer::StripSettings strip;
    strip.modulation.lfos[0].rateHz = 5.0;
    Route r;
    r.source = { SourceKind::Lfo, 0, 0 };
    r.dest = probe::trackParam (0, probe::kPan);
    r.amount = 0.5;
    strip.modulation.routes = { r };
    engine.setTrackSettings (0, strip);
    engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 4096);
    // 5 Hz : un tour = 9600 échantillons. Évalué par morceaux de 64, le LFO a avancé de 64 pas par tampon : sa phase suit l'horloge audio.
    // (Évalué une fois par tampon de 4096, il n'aurait avancé que de deux pas.)
    engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 4096);
    const auto pan = params.indexOf (probe::trackParam (0, probe::kPan));
    const double phase = std::fmod (5.0 * 8192.0 / 48000.0, 1.0);
    CHECK (params.modOffsetAt (pan) == Approx (0.5 * std::sin (2.0 * 3.14159265358979323846 * phase)).margin (0.02));
}
