// Mixeur multi-pistes : pistes, canaux MIDI, fader / pan / mute / solo, bus, envois, sorties directes, master (palier 1, jalon 9).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/State.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;

std::shared_ptr<const Instrument> constantInstrument (float level)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = 480000;
    s->sampleRate = kRate;
    s->pcm32.assign (static_cast<std::size_t> (s->frames), level);
    auto inst = std::make_shared<Instrument>();
    inst->zones.push_back (makeFullKeyboardZone (std::move (s), 60, PlayMode::Loop));
    inst->finalize();
    return inst;
}

struct Rig
{
    ParameterSet params;
    Engine engine;
    std::vector<std::vector<float>> bufs;
    std::vector<float*> ptrs;

    Rig()
    {
        REQUIRE (sampler::addParams (params));
        REQUIRE (probe::buildPool (params, mixer::numTracks));
        REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
        engine.attachParameters (&params);
        engine.prepare (kRate, 256);
        bufs.assign (16, std::vector<float> (256, 0.0f)); // 8 sorties stéréo
        for (auto& b : bufs) ptrs.push_back (b.data());
    }

    // Joue `blocks` blocs et rend le dernier échantillon des canaux demandés.
    std::vector<float> run (int blocks = 6)
    {
        for (int b = 0; b < blocks; ++b) engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
        std::vector<float> last;
        for (auto& b : bufs) last.push_back (b[255]);
        return last;
    }
    void set (ParamId id, double v) { REQUIRE (params.set (id, v)); }
};

mixer::StripSettings strip (int channel = 0)
{
    mixer::StripSettings s;
    s.midiChannel = channel;
    return s;
}
} // namespace

TEST_CASE ("parameter ids of tracks, buses and master are all distinct and declared")
{
    ParameterSet set;
    std::string err;
    REQUIRE (sampler::addParams (set, &err));
    REQUIRE (probe::buildPool (set, mixer::numTracks, &err));
    REQUIRE (mixer::addParams (set, mixer::numTracks, mixer::numBuses, &err));
    // chaque piste a ses paramètres d'instrument ; la piste 0 garde les identifiants historiques
    CHECK (set.find (mixer::samplerParam (0, mixer::kGain)) != nullptr);
    CHECK (mixer::samplerParam (0, mixer::kGain) == sampler::gain);
    for (int t = 0; t < mixer::numTracks; ++t)
        for (int k = 0; k < mixer::kSamplerKinds; ++k)
            CHECK (set.find (mixer::samplerParam (t, static_cast<mixer::SamplerKind> (k))) != nullptr);
    CHECK (set.find (mixer::busParam (15, mixer::kBusSendLevel + 7)) != nullptr);
    CHECK (set.find (mixer::masterTrim) != nullptr);
    // refus d'un doublon
    CHECK_FALSE (mixer::addParams (set, mixer::numTracks, mixer::numBuses));
}

TEST_CASE ("tracks play their own instrument, routed by MIDI channel")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.1f));
    r.engine.publishInstrument (1, constantInstrument (0.2f));
    r.engine.setTrackSettings (0, strip (1));  // piste 1 : canal 1
    r.engine.setTrackSettings (1, strip (2));  // piste 2 : canal 2
    r.engine.process (r.ptrs.data(), 16, 256); // adopte les instruments

    r.engine.noteOn (0, 60, 1.0f);             // canal MIDI 1
    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.1, 1e-5));
    CHECK (r.engine.activeVoices (0) == 1);
    CHECK (r.engine.activeVoices (1) == 0);

    r.engine.panic();
    r.run (20);
    r.engine.noteOn (1, 60, 1.0f);             // canal MIDI 2
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.2, 1e-5));
    CHECK (r.engine.activeVoices (1) == 1);

    // omni : une piste réglée sur « tous » reçoit tout
    r.engine.panic();
    r.run (20);
    r.engine.setTrackSettings (0, strip (0));
    r.engine.noteOn (1, 60, 1.0f);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.1 + 0.2, 1e-5));
}

TEST_CASE ("fader, pan law, mute and solo")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.5f));
    r.engine.publishInstrument (1, constantInstrument (0.25f));
    r.engine.setTrackSettings (0, strip (0));
    r.engine.setTrackSettings (1, strip (0)); // les deux reçoivent tout
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);

    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.75, 1e-5));
    CHECK_THAT (out[1], WithinAbs (0.75, 1e-5));

    r.set (probe::trackParam (0, probe::kVolume), -6.0206); // moitié
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25 + 0.25, 1e-3));

    r.set (probe::trackParam (0, probe::kVolume), 0.0);
    r.set (probe::trackParam (0, probe::kPan), -100.0); // balance : tout à gauche
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5 + 0.25, 1e-5));
    CHECK_THAT (out[1], WithinAbs (0.0 + 0.25, 1e-5));

    // pan vrai, au centre : -3 dB par canal
    auto s = strip (0);
    s.panMode = mixer::PanMode::True;
    r.engine.setTrackSettings (0, s);
    r.set (probe::trackParam (0, probe::kPan), 0.0);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5 * 0.70710678 + 0.25, 1e-4));

    r.engine.setTrackSettings (0, strip (0));
    r.set (probe::trackParam (0, probe::kMute), 1.0);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-5));

    r.set (probe::trackParam (0, probe::kMute), 0.0);
    r.set (probe::trackParam (0, probe::kSolo), 1.0); // solo : la piste 2 est coupée
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5, 1e-5));
    r.set (probe::trackParam (1, probe::kSolo), 1.0); // solo additif
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.75, 1e-5));
}

TEST_CASE ("direct outputs are additive with the send to master")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.4f));
    auto s = strip (0);
    s.routing.directOut = 3; // sortie physique 3 : canaux 4 et 5
    r.engine.setTrackSettings (0, s);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);

    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.4, 1e-5)); // Master (sortie 1)
    CHECK_THAT (out[4], WithinAbs (0.4, 1e-5)); // directe, en plus
    CHECK_THAT (out[5], WithinAbs (0.4, 1e-5));

    s.routing.mainEnabled = false;              // isoler sur la sortie directe
    r.engine.setTrackSettings (0, s);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-5));
    CHECK_THAT (out[4], WithinAbs (0.4, 1e-5));

    // le master peut aller sur une autre sortie physique
    mixer::MasterSettings m;
    m.output = 2;
    r.engine.setMasterSettings (m);
    s.routing.mainEnabled = true;
    s.routing.directOut = 0;
    r.engine.setTrackSettings (0, s);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-5));
    CHECK_THAT (out[2], WithinAbs (0.4, 1e-5));
}

TEST_CASE ("buses: sends with tap points, bus fader, subgroup routing, no cycles")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.5f));
    auto s = strip (0);
    s.routing.mainEnabled = false;          // la piste ne va nulle part en direct : tout passe par l'envoi
    s.sends[0].destination = 1;             // bus 1
    s.sends[0].tap = mixer::TapPoint::PostPan;
    r.engine.setTrackSettings (0, s);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);
    r.set (probe::trackParam (0, probe::kSendLevel + 0), 0.0);

    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5, 1e-5));          // piste -> envoi -> bus 1 -> master

    r.set (mixer::busParam (0, mixer::kBusVolume), -6.0206);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-3));

    // point de captation : pré-fader de piste, fader de piste à -inf : l'envoi continue
    r.set (mixer::busParam (0, mixer::kBusVolume), 0.0);
    r.set (probe::trackParam (0, probe::kVolume), -100.0);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-5));           // post-pan : plus rien
    s.sends[0].tap = mixer::TapPoint::PreInsert;
    r.engine.setTrackSettings (0, s);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5, 1e-5));           // pré-fader : le signal brut

    // sous-groupe : bus 1 -> bus 2 -> master ; un bus ne route que vers un bus d'indice supérieur
    r.set (probe::trackParam (0, probe::kVolume), 0.0);
    mixer::StripSettings bus1;
    bus1.routing.mainDest = 2;
    r.engine.setBusSettings (0, bus1);
    r.set (mixer::busParam (1, mixer::kBusVolume), -6.0206);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-3));

    mixer::StripSettings bus2;
    bus2.routing.mainDest = 1; // vers un bus d'indice inférieur : ignoré (aucun cycle possible), retombe sur le Master
    r.engine.setBusSettings (1, bus2);
    out = r.run();
    CHECK (std::isfinite (out[0]));
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-3));
}

TEST_CASE ("master: trim, fader, mute, mono reduction, meters")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.5f));
    auto s = strip (0);
    s.panMode = mixer::PanMode::Balance;
    r.engine.setTrackSettings (0, s);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);

    r.set (probe::trackParam (0, probe::kPan), -100.0); // tout à gauche : L 0.5, R 0
    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5, 1e-5));
    CHECK_THAT (out[1], WithinAbs (0.0, 1e-5));

    r.set (mixer::masterMono, 1.0);                      // réduction mono : (L+R)/2 sur les deux canaux
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-5));
    CHECK_THAT (out[1], WithinAbs (0.25, 1e-5));
    r.set (mixer::masterMono, 0.0);

    r.set (probe::masterVolume, -6.0206);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.25, 1e-3));
    r.set (mixer::masterTrim, 6.0206);                   // +6 dB : remonte à l'original
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.5, 1e-3));

    const auto trackMeter = r.engine.trackMeter (0);
    CHECK_THAT (trackMeter.peakL, WithinAbs (0.5, 1e-3));
    CHECK_THAT (trackMeter.peakR, WithinAbs (0.0, 1e-5));
    const auto master = r.engine.masterMeter();
    CHECK_THAT (master.peakL, WithinAbs (0.5, 1e-3));

    r.set (probe::masterMute, 1.0);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-5));
}

TEST_CASE ("an idle engine with no instrument stays silent and cheap, and settings are bounded")
{
    Rig r;
    auto out = r.run();
    for (const float x : out) CHECK (x == 0.0f);

    mixer::StripSettings wild;
    wild.midiChannel = 99;
    wild.routing.directOut = 9999;
    wild.routing.mainDest = -3;
    wild.sends[0].destination = 500;
    wild.sends[0].pan = std::nan ("");
    const auto clean = mixer::sanitisedStrip (wild, false, 0);
    CHECK (clean.midiChannel == 16);
    CHECK (clean.routing.directOut == mixer::numOutputs);
    CHECK (clean.routing.mainDest == 0);
    CHECK (clean.sends[0].destination == mixer::numBuses);
    CHECK (clean.sends[0].pan == 0.0);

    // un bus ne peut cibler qu'un bus d'indice supérieur
    mixer::StripSettings bus;
    bus.routing.mainDest = 3;
    CHECK (mixer::sanitisedStrip (bus, true, 4).routing.mainDest == 0); // bus 5 -> bus 3 : refusé
    CHECK (mixer::sanitisedStrip (bus, true, 1).routing.mainDest == 3); // bus 2 -> bus 3 : permis
}

TEST_CASE ("mixer state: defaults, JSON round trip, bounds")
{
    const MixerState def;
    CHECK (def.tracks[0].strip.midiChannel == 0);   // piste 1 : tous les canaux
    CHECK (def.tracks[4].strip.midiChannel == 5);   // piste n : canal n
    CHECK (def.master.output == 1);

    MixerState m;
    m.selectedTrack = 3;
    m.tracks[2].active = true;
    m.tracks[3].active = true;
    m.tracks[2].strip.name = "Piano";
    m.tracks[2].strip.color = 5;
    m.tracks[2].strip.midiChannel = 7;
    m.tracks[2].strip.routing.directOut = 12;
    m.tracks[2].strip.routing.mainEnabled = false;
    m.tracks[2].strip.panMode = mixer::PanMode::True;
    m.tracks[2].strip.sends[1].destination = 4;
    m.tracks[2].strip.sends[1].tap = mixer::TapPoint::PostFaderPrePan;
    m.tracks[2].strip.sends[1].pan = -0.5;
    m.tracks[2].strip.sends[1].mute = true;
    ZoneDescription z;
    z.samplePath = "/x/a.wav";
    z.chokeCuts = 2;
    m.tracks[2].instrument.zones.push_back (z);
    m.tracks[2].instrument.maxVoices = 128;
    m.buses[0].name = "Drums";
    m.buses[0].routing.mainDest = 5;
    m.master.output = 4;
    m.master.panMode = mixer::PanMode::True;

    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);

    // un document vide ou invalide rend l'état par défaut ; les valeurs hors bornes sont ramenées
    CHECK (mixerFromJson ("") == MixerState {});
    CHECK (mixerFromJson ("[1,2]") == MixerState {});
    const auto wild = mixerFromJson (R"({"selected_track": 99, "tracks": [{"midi_channel": 50, "routing": {"direct_out": 999}}],
                                         "buses": [{"routing": {"main_dest": 1}}], "master": {"output": 0}})");
    CHECK (wild.selectedTrack == 0);               // la piste 16 n'est pas active : la sélection revient à la première active
    CHECK (wild.tracks[0].strip.midiChannel == 16);
    CHECK (wild.tracks[0].strip.routing.directOut == mixer::numOutputs);
    CHECK (wild.buses[0].routing.mainDest == 0);   // bus 1 -> bus 1 : un bus ne route que vers un bus supérieur
    CHECK (wild.master.output == 1);
}

TEST_CASE ("project state carries the mixer, and an older single-instrument state becomes track 1")
{
    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    MixerState m;
    m.tracks[1].strip.name = "Basse";
    m.tracks[1].instrument.zones.push_back (ZoneDescription {});
    const auto text = state::save (set, "id", std::nullopt, &m);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    CHECK (*r.mixer == m);
    CHECK (r.fileVersion == state::currentSchemaVersion);

    // projet v4 : un seul instrument à la racine
    const std::string v4 = R"({"format":"neptune-state","schema_version":4,"parameters":{},
                               "instrument":{"zones":[{"sample":"/old/a.wav","key_low":10,"key_high":20}],"max_voices":32}})";
    const auto old = state::load (set, v4);
    REQUIRE (old.ok());
    REQUIRE (old.mixer.has_value());
    CHECK (old.mixer->selectedTrack == 0);
    REQUIRE (old.mixer->tracks[0].instrument.zones.size() == 1);
    CHECK (old.mixer->tracks[0].instrument.zones[0].samplePath == "/old/a.wav");
    CHECK (old.mixer->tracks[0].instrument.maxVoices == 32);
    CHECK (old.mixer->tracks[1].instrument.empty());
    CHECK (old.mixer->tracks[0].strip.midiChannel == 0); // la piste 1 reste « tous les canaux » : le comportement d'avant

    // sans instrument ni mixeur : aucun mixeur dans le résultat (l'existant n'est pas touché)
    CHECK_FALSE (state::load (set, R"({"format":"neptune-state","schema_version":4,"parameters":{}})").mixer.has_value());
}
