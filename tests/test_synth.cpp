// Synthé soustractif (palier 1, premier jet) : oscillateurs à bande limitée, enveloppe, filtre, polyphonie, intégration au moteur,
// état du projet (type de piste, patch, pistes actives, schéma v6).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/State.h"
#include "neptune/core/Synth.h"
#include "neptune/core/SynthVoices.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

SynthDescription plain (Waveform w)
{
    SynthDescription d;
    d.waveform = w;
    d.filter.on = false;
    d.ampEnv.attack.ms = 0.0;      // démarrage immédiat : on compare à la formule dès le premier échantillon
    d.ampEnv.release.ms = 20.0;
    return d;
}

// Joue `frames` échantillons d'une note et rend le canal gauche.
std::vector<float> play (const SynthDescription& d, int key, int frames, float velocity = 1.0f,
                         const SynthVoices::Params& params = {})
{
    const SynthPatch patch (d);
    SynthVoices v;
    v.prepare (kRate);
    v.setPatch (&patch);
    v.noteOn (0, key, velocity);
    std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (l);
    for (int off = 0; off < frames; off += 256)
        v.render (l.data() + off, r.data() + off, std::min (256, frames - off), 1.0f, 1.0f, params);
    return l;
}

double hz (int key) { return 440.0 * std::pow (2.0, (key - 69) / 12.0); }

double naive (Waveform w, double t)
{
    switch (w)
    {
        case Waveform::Saw: return 2.0 * t - 1.0;
        case Waveform::Square: return t < 0.5 ? 1.0 : -1.0;
        case Waveform::Triangle: return 1.0 - 4.0 * std::abs (t - 0.5);
        default: return std::sin (2.0 * kPi * t);
    }
}

// Part de l'énergie qui n'est PAS sur un harmonique : le repliement (aliasing). `period` échantillons par cycle (31,5 : les repliements
// ne tombent jamais sur un harmonique), fenêtre de 32 cycles = 1008 échantillons, donc les harmoniques sont aux multiples de 32.
double aliasRatio (const std::vector<double>& x)
{
    const std::size_t n = x.size();
    double total = 0.0, alias = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = 2.0 * kPi * static_cast<double> (k * i % n) / static_cast<double> (n);
            re += x[i] * std::cos (a);
            im -= x[i] * std::sin (a);
        }
        const double e = re * re + im * im;
        total += e;
        if (k % 32 != 0) alias += e;
    }
    return alias / total;
}

double rms (const std::vector<float>& x, std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i) s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (x.size() - from));
}
} // namespace

TEST_CASE ("synth oscillators are band-limited: far less aliasing than the naive waveform")
{
    constexpr double period = 31.5;                 // échantillons par cycle : ~1524 Hz
    constexpr double f0 = kRate / period;
    SynthVoices::Params p;
    p.tuneCents = 1200.0 * std::log2 (f0 / 440.0);  // note 69 accordée exactement sur f0
    constexpr int start = 2000, window = 1008;
    for (const Waveform w : { Waveform::Saw, Waveform::Square, Waveform::Triangle })
    {
        const auto out = play (plain (w), 69, start + window, 1.0f, p);
        std::vector<double> synth (window), plainWave (window);
        for (int i = 0; i < window; ++i)
        {
            synth[static_cast<std::size_t> (i)] = out[static_cast<std::size_t> (start + i)];
            plainWave[static_cast<std::size_t> (i)] = naive (w, std::fmod ((start + i) * f0 / kRate, 1.0));
        }
        const double bl = aliasRatio (synth), nv = aliasRatio (plainWave);
        INFO (toString (w) << ": band-limited " << bl << ", naive " << nv);
        CHECK (bl < 0.5 * nv);
    }
}

TEST_CASE ("synth pitch: A4 is 440 Hz, the octave setting and the track tuning transpose")
{
    auto countCrossings = [] (const std::vector<float>& x)
    {
        int n = 0;
        for (std::size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;
        return n;
    };
    auto d = plain (Waveform::Sine);
    CHECK (std::abs (countCrossings (play (d, 69, 48000)) - 440) <= 1);
    d.octave = 1;
    CHECK (std::abs (countCrossings (play (d, 69, 48000)) - 880) <= 1);
    d.octave = 0;
    SynthVoices::Params p;
    p.tuneCents = 1200.0;
    CHECK (std::abs (countCrossings (play (d, 69, 48000, 1.0f, p)) - 880) <= 1);
}

TEST_CASE ("synth level follows velocity and leaves headroom")
{
    const auto d = plain (Waveform::Sine);
    const auto loud = play (d, 69, 4800, 1.0f);
    const auto soft = play (d, 69, 4800, 0.5f);
    CHECK_THAT (rms (soft) / rms (loud), WithinAbs (0.5, 0.01));
    double peak = 0.0;
    for (const float s : loud) peak = std::max (peak, static_cast<double> (std::abs (s)));
    CHECK (peak < 0.35);
    CHECK (peak > 0.25);
}

TEST_CASE ("synth filter: a low cutoff removes the highs, the envelope opens it")
{
    auto d = plain (Waveform::Saw);
    d.filter.on = true;
    d.filter.slopeDb = 24;
    SynthVoices::Params open, closed;
    closed.cutoffHz = 300.0;
    const auto a = play (d, 81, 9600, 1.0f, open);   // 880 Hz
    const auto b = play (d, 81, 9600, 1.0f, closed);
    CHECK (rms (b, 2000) < 0.2 * rms (a, 2000));
    CHECK (rms (b, 2000) > 0.0);

    // enveloppe de filtre : profondeur +4 octaves, attaque lente -> le début est plus sombre que la fin
    d.filter.envDepthOctaves = 4.0;
    d.filter.env.attack.ms = 400.0;
    const auto c = play (d, 81, 48000, 1.0f, closed);
    std::vector<float> early (c.begin() + 500, c.begin() + 5000), late (c.end() - 4500, c.end());
    CHECK (rms (late) > 2.0 * rms (early));
    for (const float s : c) REQUIRE (std::isfinite (s));
}

TEST_CASE ("synth polyphony: release ends voices, the limit steals, mono and legato keep one voice")
{
    SynthDescription d = plain (Waveform::Saw);
    const SynthPatch patch (d);
    SynthVoices v;
    v.prepare (kRate);
    v.setPatch (&patch);
    std::vector<float> l (256), r (256);
    auto run = [&] (int blocks) { for (int i = 0; i < blocks; ++i) v.render (l.data(), r.data(), 256, 1.0f, 1.0f, {}); };

    v.noteOn (0, 60, 1.0f);
    v.noteOn (0, 64, 1.0f);
    v.noteOn (0, 67, 1.0f);
    run (1);
    CHECK (v.activeVoices() == 3);
    v.noteOff (0, 60);
    v.noteOff (0, 64);
    v.noteOff (0, 67);
    CHECK (v.heldVoices() == 0);
    run (8); // 20 ms de relâchement = 960 échantillons
    CHECK (v.activeVoices() == 0);

    // limite de voix
    d.maxVoices = 2;
    const SynthPatch limited (d);
    v.setPatch (&limited);
    v.noteOn (0, 60, 1.0f);
    v.noteOn (0, 62, 1.0f);
    v.noteOn (0, 64, 1.0f);
    CHECK (v.heldVoices() == 2);
    run (4);
    CHECK (v.activeVoices() == 2);

    // mono : une seule voix, la dernière note
    d.maxVoices = 16;
    d.polyphony = PolyphonyMode::Mono;
    const SynthPatch mono (d);
    v.setPatch (&mono);
    v.noteOn (0, 60, 1.0f);
    v.noteOn (0, 65, 1.0f);
    run (4);
    CHECK (v.activeVoices() == 1);
    int key = -1;
    v.forEachVoice ([&key] (const SynthVoices::VoiceInfo& i) { key = i.key; });
    CHECK (key == 65);
    v.noteOff (0, 65); // la touche précédente est encore tenue : on y revient
    run (4);
    key = -1;
    v.forEachVoice ([&key] (const SynthVoices::VoiceInfo& i) { if (! i.fading) key = i.key; });
    CHECK (key == 60);

    // legato : la voix continue, sa hauteur change
    d.polyphony = PolyphonyMode::Legato;
    const SynthPatch legato (d);
    v.setPatch (&legato);
    v.noteOn (0, 48, 1.0f);
    run (1);
    v.noteOn (0, 55, 1.0f);
    run (2);
    CHECK (v.activeVoices() == 1);
    v.forEachVoice ([&key] (const SynthVoices::VoiceInfo& i) { key = i.key; });
    CHECK (key == 55);

    v.panic();
    run (4);
    CHECK (v.activeVoices() == 0);
}

TEST_CASE ("synth description: bounds and JSON round trip")
{
    SynthDescription d;
    d.waveform = Waveform::Square;
    d.octave = -2;
    d.ampEnv.attack.ms = 120.0;
    d.filter.model = FilterModel::Ladder;
    d.filter.envDepthOctaves = 2.5;
    d.polyphony = PolyphonyMode::Legato;
    d.maxVoices = 9;
    d.stealMode = VoiceStealMode::KeepLow;
    d.glideMs = 250;
    CHECK (synthFromJson (toJson (d)) == sanitised (d));

    SynthDescription wild;
    wild.octave = 99;
    wild.maxVoices = 100000;
    wild.glideMs = -4;
    const auto s = sanitised (wild);
    CHECK (s.octave == SynthDescription::maxOctave);
    CHECK (s.maxVoices == SynthDescription::maxVoicesLimit);
    CHECK (s.glideMs == 0);
    CHECK (synthFromJson ("not json") == SynthDescription {});
    CHECK (synthFromJson (R"({"waveform":"banana","octave":"x"})") == SynthDescription {});
}

// ---------------------------------------------------------------------------------------------------- moteur

namespace
{
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
        bufs.assign (4, std::vector<float> (256, 0.0f)); // 2 sorties stéréo : le master sur la sortie 1
        for (auto& b : bufs) ptrs.push_back (b.data());
    }
    // Joue des blocs ; rend la crête du canal gauche de la sortie 1 sur le dernier bloc.
    float run (int blocks = 8)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256);
            peak = 0.0f;
            for (const float s : bufs[0]) peak = std::max (peak, std::abs (s));
        }
        return peak;
    }
};
} // namespace

TEST_CASE ("engine: a synth track makes sound, an inactive track does not, switching the type cuts the voices")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    r.run (1); // le patch est adopté au début du bloc
    r.engine.noteOn (0, 60, 1.0f);
    const float on = r.run (8);
    CHECK (on > 0.05f);
    CHECK (r.engine.activeVoices() == 1);

    // le fader agit : -inf
    REQUIRE (r.params.set (probe::trackParam (0, probe::kVolume), -100.0));
    CHECK (r.run (8) == 0.0f);
    REQUIRE (r.params.set (probe::trackParam (0, probe::kVolume), 0.0));
    CHECK (r.run (8) > 0.05f);

    // changement de type : plus rien ne sonne sous l'ancien type
    r.engine.setTrackType (0, TrackType::Sampler);
    CHECK (r.run (4) == 0.0f);
    CHECK (r.engine.activeVoices() == 0);

    // piste inactive : elle ignore les notes
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.setTrackActive (0, false);
    r.run (1);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.run (8) == 0.0f);
    r.engine.setTrackActive (0, true);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.run (8) > 0.05f);
}

TEST_CASE ("engine: the synth follows the track gain, cutoff and note-off")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    r.run (1);
    r.engine.noteOn (0, 60, 1.0f);
    const float loud = r.run (8);
    REQUIRE (r.params.set (mixer::samplerParam (0, mixer::kGain), -12.0));
    const float quiet = r.run (8);
    CHECK_THAT (quiet / loud, WithinAbs (std::pow (10.0, -12.0 / 20.0), 0.03));
    REQUIRE (r.params.set (mixer::samplerParam (0, mixer::kGain), 0.0));

    r.engine.noteOff (0, 60);
    CHECK (r.run (200) == 0.0f); // 150 ms de relâchement par défaut : terminé
    CHECK (r.engine.activeVoices() == 0);
}

TEST_CASE ("engine: synth voices appear in the voice snapshot without a zone")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    r.run (1);
    r.engine.setVoiceMonitor (true);
    r.engine.noteOn (0, 72, 0.8f);
    r.run (2);
    Engine::VoiceInfo v[8];
    REQUIRE (r.engine.readVoices (v, 8) == 1);
    CHECK (v[0].key == 72);
    CHECK (v[0].sourceIndex == -1);
    CHECK (v[0].track == 0);
}

// ---------------------------------------------------------------------------------------------------- état

TEST_CASE ("mixer state: fresh project has one synth track, tracks can be added and removed")
{
    auto m = MixerState::freshProject();
    CHECK (m.activeCount() == 1);
    CHECK (m.tracks[0].active);
    CHECK (m.tracks[0].type == TrackType::Synth);
    CHECK (m.firstInactive() == 1);
    CHECK (mixerFromJson (toJson (m)) == m);

    m.tracks[1].active = true;
    m.tracks[1].strip.name = "Pad";
    m.tracks[1].synth.waveform = Waveform::Triangle;
    m.tracks[1].type = TrackType::Synth;
    m.selectedTrack = 1;
    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.tracks[1].synth.waveform == Waveform::Triangle);
    CHECK (back.activeCount() == 2);

    m.resetTrack (1);
    CHECK_FALSE (m.tracks[1].active);
    CHECK (m.tracks[1].type == TrackType::Sampler);
    CHECK (m.tracks[1].strip.name.empty());
    CHECK (m.tracks[1].strip.midiChannel == 2); // le canal d'origine de la piste 2
    CHECK (m.selectedTrack == 0);               // la piste sélectionnée a disparu : retour à la première active
}

TEST_CASE ("an empty track added by the user stays active after a round trip, and track 1 can be removed")
{
    MixerState m = MixerState::freshProject();
    m.tracks[4].active = true; // ajoutée, jamais touchée
    auto back = mixerFromJson (toJson (m));
    CHECK (back.tracks[4].active);
    CHECK (back == m);

    m.tracks[0].active = false;
    m.selectedTrack = 4;
    back = mixerFromJson (toJson (m));
    CHECK_FALSE (back.tracks[0].active);
    CHECK (back.tracks[4].active);
    CHECK (back == m);
}

TEST_CASE ("v5 projects: tracks with content stay active as Samplers, the empty ones are hidden")
{
    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    const std::string v5 = R"({"format":"neptune-state","schema_version":5,"parameters":{},
        "mixer":{"selected_track":2,"tracks":[
            {"instrument":{"zones":[{"sample":"/a.wav"}]}},
            {},
            {"name":"Basse","midi_channel":3,"routing":{"direct_out":5}},
            {}
        ]}})";
    const auto r = state::load (set, v5);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    const auto& m = *r.mixer;
    CHECK (m.tracks[0].active);
    CHECK_FALSE (m.tracks[1].active);
    CHECK (m.tracks[2].active);
    CHECK_FALSE (m.tracks[3].active);
    CHECK (m.activeCount() == 2);
    CHECK (m.selectedTrack == 2);
    for (const auto& t : m.tracks) CHECK (t.type == TrackType::Sampler);
    CHECK (r.fileVersion == 5);
}

TEST_CASE ("state: a synth track survives save and load")
{
    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    auto m = MixerState::freshProject();
    m.tracks[0].synth.waveform = Waveform::Square;
    m.tracks[0].synth.octave = 1;
    const auto text = state::save (set, "id", std::nullopt, &m);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    CHECK (*r.mixer == m);
    CHECK (r.mixer->tracks[0].type == TrackType::Synth);
}
