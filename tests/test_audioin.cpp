// Pistes « Entrée audio » : envois reçus des autres pistes, entrée de l'hôte, ordre de traitement sans cycle.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
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
    std::vector<float> hostL = std::vector<float> (256, 0.0f), hostR = std::vector<float> (256, 0.0f);
    bool hostConnected = false;

    Rig()
    {
        REQUIRE (sampler::addParams (params));
        REQUIRE (probe::buildPool (params, mixer::numTracks));
        REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
        engine.attachParameters (&params);
        engine.prepare (kRate, 256);
        bufs.assign (16, std::vector<float> (256, 0.0f));
        for (auto& b : bufs) ptrs.push_back (b.data());
    }
    std::vector<float> run (int blocks = 6)
    {
        for (int b = 0; b < blocks; ++b)
        {
            if (hostConnected) engine.setHostInput (hostL.data(), hostR.data(), 256);
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
        }
        std::vector<float> last;
        for (auto& b : bufs) last.push_back (b[255]);
        return last;
    }
    void set (ParamId id, double v) { REQUIRE (params.set (id, v)); }
    // piste `from` : envoi 1 vers la piste Entrée audio `to`, rien en direct
    void sendTo (int from, int to, bool direct = false)
    {
        mixer::StripSettings s;
        s.routing.mainEnabled = direct;
        s.sends[0].destination = mixer::firstTrackDestination + to;
        s.sends[0].tap = mixer::TapPoint::PostPan;
        engine.setTrackSettings (from, s);
        set (probe::trackParam (from, probe::kSendLevel), 0.0);
    }
    void makeAudioIn (int track, bool hostInput = false)
    {
        mixer::StripSettings s;
        s.hostInput = hostInput;
        engine.setTrackSettings (track, s);
        engine.setTrackActive (track, true);
        engine.setTrackType (track, TrackType::AudioIn);
    }
};
} // namespace

TEST_CASE ("audio in: a track receives the send of another track and keeps its own chain")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.4f));
    r.sendTo (0, 2);
    r.makeAudioIn (2);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);
    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.4, 1e-4));               // piste 1 -> envoi -> piste 3 (Entrée audio) -> Master

    r.set (probe::trackParam (2, probe::kVolume), -6.0206);   // le fader de l'Entrée audio agit
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.2, 1e-3));
    r.set (probe::trackParam (2, probe::kVolume), 0.0);
    r.set (probe::trackParam (2, probe::kMute), 1.0);
    out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-5));
}

TEST_CASE ("audio in: the trim of the track is its instrument gain")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.4f));
    r.sendTo (0, 1);
    r.makeAudioIn (1);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);
    r.set (mixer::samplerParam (1, mixer::kGain), -6.0206);
    CHECK_THAT (r.run()[0], WithinAbs (0.2, 1e-3));
}

TEST_CASE ("audio in: a send to a track that is not an Audio In track is ignored")
{
    Rig r;
    r.engine.publishInstrument (0, constantInstrument (0.4f));
    r.sendTo (0, 2);                                         // la piste 3 reste un Sampler
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK_THAT (r.run()[0], WithinAbs (0.0, 1e-6));

    r.engine.setTrackActive (2, true);
    r.engine.setTrackType (2, TrackType::AudioIn);          // elle le devient : le signal arrive
    CHECK_THAT (r.run()[0], WithinAbs (0.4, 1e-4));
}

TEST_CASE ("audio in: the host input feeds the track, switchable per track")
{
    Rig r;
    r.makeAudioIn (1, true);
    r.engine.process (r.ptrs.data(), 16, 256);
    std::fill (r.hostL.begin(), r.hostL.end(), 0.3f);
    std::fill (r.hostR.begin(), r.hostR.end(), 0.1f);
    r.hostConnected = true;
    auto out = r.run();
    CHECK_THAT (out[0], WithinAbs (0.3, 1e-4));
    CHECK_THAT (out[1], WithinAbs (0.1, 1e-4));

    r.makeAudioIn (1, false);                                // entrée de l'hôte coupée pour cette piste
    CHECK_THAT (r.run()[0], WithinAbs (0.0, 1e-6));

    r.makeAudioIn (1, true);
    r.hostConnected = false;                                 // l'hôte ne fournit rien : silence, aucun accident
    CHECK_THAT (r.run()[0], WithinAbs (0.0, 1e-6));
}

TEST_CASE ("audio in: an Audio In track feeds only a higher one, never itself or a lower one")
{
    Rig r;
    r.makeAudioIn (1, true);
    r.makeAudioIn (3, false);
    r.makeAudioIn (2, false);
    r.engine.process (r.ptrs.data(), 16, 256);
    std::fill (r.hostL.begin(), r.hostL.end(), 0.5f);
    std::fill (r.hostR.begin(), r.hostR.end(), 0.5f);
    r.hostConnected = true;

    r.sendTo (1, 3);                                         // 2 -> 4 : permis (indice supérieur)
    r.makeAudioIn (1, true);
    mixer::StripSettings a;
    a.routing.mainEnabled = false;
    a.sends[0].destination = mixer::firstTrackDestination + 3;
    r.engine.setTrackSettings (1, a);                        // (le niveau d'envoi est déjà à 0 dB)
    CHECK_THAT (r.run()[0], WithinAbs (0.5, 1e-4));

    a.sends[0].destination = mixer::firstTrackDestination + 1;   // vers soi-même : ignoré
    r.engine.setTrackSettings (1, a);
    CHECK_THAT (r.run()[0], WithinAbs (0.0, 1e-6));

    mixer::StripSettings b;                                  // 4 -> 2 : vers une piste d'indice inférieur, ignoré
    b.routing.mainEnabled = false;
    b.sends[0].destination = mixer::firstTrackDestination + 1;
    r.engine.setTrackSettings (3, b);
    r.set (probe::trackParam (3, probe::kSendLevel), 0.0);
    a.sends[0].destination = mixer::firstTrackDestination + 3;
    a.routing.mainEnabled = true;
    r.engine.setTrackSettings (1, a);
    r.run();                                                  // aucune explosion, aucun cycle
}

TEST_CASE ("audio in: notes are ignored by an Audio In track and a bus cannot send to a track")
{
    Rig r;
    r.makeAudioIn (0);
    r.engine.process (r.ptrs.data(), 16, 256);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.engine.activeVoices (0) == 0);
    r.engine.noteOff (0, 60);

    mixer::StripSettings bus;
    bus.sends[0].destination = mixer::firstTrackDestination;
    CHECK (mixer::sanitisedStrip (bus, true, 0).sends[0].destination == 0);
    CHECK (mixer::sanitisedStrip (bus, false, 0).sends[0].destination == mixer::firstTrackDestination);
}

TEST_CASE ("audio in: the track type and the host-input switch round trip in the mixer state")
{
    auto m = MixerState::freshProject();
    m.tracks[2].type = TrackType::AudioIn;
    m.tracks[2].active = true;
    m.tracks[2].strip.hostInput = false;
    m.tracks[0].strip.sends[0].destination = mixer::firstTrackDestination + 2;
    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.tracks[2].type == TrackType::AudioIn);
    CHECK_FALSE (back.tracks[2].strip.hostInput);
    CHECK (back.tracks[0].strip.sends[0].destination == mixer::firstTrackDestination + 2);
    CHECK (mixerFromJson (toJson (MixerState::freshProject())).tracks[2].strip.hostInput);
}
