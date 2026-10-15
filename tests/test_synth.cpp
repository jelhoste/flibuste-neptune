// Synthé soustractif (spec-modules §2) : catalogue de formes d'onde à bande limitée, oscillateurs, unisson, synchro dure,
// mixeur des sources, sous-oscillateur, bruit, enveloppe de pitch, 2 filtres, polyphonie, moteur, état du projet.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/State.h"
#include "neptune/core/Synth.h"
#include "neptune/core/SynthVoices.h"
#include "neptune/core/WaveCatalog.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

SynthDescription plain (int wave)
{
    SynthDescription d;
    d.osc[0].wave = wave;
    d.filters.filter[0].on = false;   // pas de filtre
    d.ampEnv.attack.ms = 0.0;         // démarrage immédiat
    d.ampEnv.release.ms = 20.0;
    return d;
}

struct Stereo { std::vector<float> l, r; };

// Joue `frames` échantillons d'une note et rend les deux canaux.
Stereo play (const SynthDescription& d, int key, int frames, float velocity = 1.0f, const SynthVoices::Params& params = {})
{
    const SynthPatch patch (d);
    SynthVoices v;
    v.prepare (kRate);
    v.setPatch (&patch);
    v.noteOn (0, key, velocity);
    Stereo out { std::vector<float> (static_cast<std::size_t> (frames), 0.0f), std::vector<float> (static_cast<std::size_t> (frames), 0.0f) };
    for (int off = 0; off < frames; off += 256)
        v.render (out.l.data() + off, out.r.data() + off, std::min (256, frames - off), 1.0f, 1.0f, params);
    return out;
}

double rms (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (to - from));
}

int crossings (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    if (to == 0 || to > x.size()) to = x.size();
    int n = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;
    return n;
}

// Part de l'énergie qui n'est PAS sur un harmonique (repliement). Cycle de 31,5 échantillons : fenêtre de 32 cycles = 1008
// échantillons, harmoniques aux multiples de 32 ; les repliements ne tombent jamais sur un harmonique.
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
    return total > 0.0 ? alias / total : 0.0;
}

// note 69 accordée exactement sur un cycle de `period` échantillons
SynthVoices::Params tunedTo (double period)
{
    SynthVoices::Params p;
    p.tuneCents = 1200.0 * std::log2 (kRate / period / 440.0);
    return p;
}

std::vector<double> window (const std::vector<float>& x, std::size_t start, std::size_t n)
{
    return std::vector<double> (x.begin() + static_cast<std::ptrdiff_t> (start), x.begin() + static_cast<std::ptrdiff_t> (start + n));
}
} // namespace

// ---------------------------------------------------------------------------------------------------- catalogue

TEST_CASE ("wave catalog: unique stable keys, three categories, finite normalised tables")
{
    CHECK (kNumWaves >= 30);
    std::set<std::string> keys;
    int perCategory[numWaveCategories] = { 0, 0, 0 };
    for (int w = 0; w < kNumWaves; ++w)
    {
        const auto& info = waveInfo (w);
        CHECK (info.id == w);
        CHECK (keys.insert (std::string (info.key)).second);
        CHECK (waveIdFromKey (info.key) == w);
        ++perCategory[static_cast<int> (info.category)];
    }
    for (const int n : perCategory) CHECK (n >= 5);
    CHECK (waveIdFromKey ("nope") == -1);

    const auto& tables = WaveTables::instance();
    for (int w = 0; w < kNumWaves; ++w)
    {
        const float* t = tables.table (w, 0);
        float peak = 0.0f;
        for (int i = 0; i <= WaveTables::kSize; ++i)
        {
            REQUIRE (std::isfinite (t[i]));
            peak = std::max (peak, std::abs (t[i]));
        }
        INFO (waveInfo (w).key);
        CHECK (peak > 0.45f);
        CHECK (peak < 1.3f);
        CHECK (t[WaveTables::kSize] == t[0]); // échantillon de garde
    }
    // plus la note est aiguë, plus la table est pauvre ; la plus grave garde tout
    CHECK (WaveTables::mipFor (0.0004) == 0);
    CHECK (WaveTables::mipFor (0.1) > WaveTables::mipFor (0.01));
    CHECK (WaveTables::mipFor (0.45) == WaveTables::kMips - 1);
}

TEST_CASE ("waves: the analytic ones and every table are band-limited (almost no aliasing)")
{
    constexpr double period = 31.5; // ~1524 Hz
    for (int w = 0; w < kNumWaves; ++w)
    {
        // le catalogue contient des formes volontairement très riches (bruit numérique) : on exige peu de repliement,
        // pas zéro, à une hauteur où 15 harmoniques seulement tiennent sous Nyquist
        const auto out = play (plain (w), 69, 2000 + 1008, 1.0f, tunedTo (period));
        const double ratio = aliasRatio (window (out.l, 2000, 1008));
        INFO (waveInfo (w).key << ": " << ratio);
        CHECK (ratio < 0.004);
    }
}

TEST_CASE ("synth pitch: A4 is 440 Hz, octave, semitone and track tuning transpose")
{
    auto d = plain (kWaveSine);
    CHECK (std::abs (crossings (play (d, 69, 48000).l) - 440) <= 1);
    d.osc[0].octave = 1;
    CHECK (std::abs (crossings (play (d, 69, 48000).l) - 880) <= 1);
    d.osc[0].octave = 0;
    d.osc[0].semitone = 12;
    CHECK (std::abs (crossings (play (d, 69, 48000).l) - 880) <= 1);
    d.osc[0].semitone = 0;
    SynthVoices::Params p;
    p.tuneCents = 1200.0;
    CHECK (std::abs (crossings (play (d, 69, 48000, 1.0f, p).l) - 880) <= 1);
    p.tuneCents = 0.0;
    p.fineCents[0] = 100.0; // un demi-ton
    CHECK (std::abs (crossings (play (d, 69, 48000, 1.0f, p).l) - 466) <= 1);
}

// ---------------------------------------------------------------------------------------------------- oscillateurs

TEST_CASE ("synth level follows velocity, leaves headroom, and the sources are mixed by level and mute")
{
    auto d = plain (kWaveSine);
    const auto loud = play (d, 69, 4800, 1.0f);
    const auto soft = play (d, 69, 4800, 0.5f);
    CHECK_THAT (rms (soft.l) / rms (loud.l), WithinAbs (0.5, 0.01));
    double peak = 0.0;
    for (const float s : loud.l) peak = std::max (peak, static_cast<double> (std::abs (s)));
    CHECK (peak < 0.175);
    CHECK (peak > 0.125);

    SynthVoices::Params p;
    p.level[0] = 0.0; // oscillateur 1 coupé, seul le 2 sonne
    p.level[1] = 100.0;
    d.osc[1].wave = kWaveSine;
    d.osc[1].octave = 1; // une octave plus haut
    const auto two = play (d, 69, 48000, 1.0f, p);
    CHECK (std::abs (crossings (two.l) - 880) <= 1);

    d.mix[1].mute = true;
    const auto muted = play (d, 69, 4800, 1.0f, p);
    CHECK (rms (muted.l) < 1e-9);

    // pan du mixeur : tout à gauche
    d.mix[1].mute = false;
    d.mix[1].pan = -1.0;
    const auto left = play (d, 69, 4800, 1.0f, p);
    CHECK (rms (left.l) > 0.05);
    CHECK (rms (left.r) < 1e-6);
}

TEST_CASE ("synth unison: stereo spread, detune curve and voice count")
{
    auto d = plain (kWaveSaw);
    const auto mono = play (d, 60, 9600);
    CHECK_THAT (rms (mono.l), WithinAbs (rms (mono.r), 1e-6)); // une seule voix : au centre

    d.osc[0].unison = 5;
    d.osc[0].spread = 1.0;
    SynthVoices::Params p;
    p.unisonDetune[0] = 30.0;
    const auto wide = play (d, 60, 9600, 1.0f, p);
    double diff = 0.0;
    for (std::size_t i = 4800; i < wide.l.size(); ++i) diff += std::abs (static_cast<double> (wide.l[i]) - wide.r[i]);
    CHECK (diff / 4800.0 > 0.01); // canaux différents : image stéréo
    // l'énergie reste du même ordre que la voix seule (normalisation par la racine du nombre de voix)
    CHECK (rms (wide.l) < 1.6 * rms (mono.l));
    CHECK (rms (wide.l) > 0.4 * rms (mono.l));

    d.osc[0].spread = 0.0; // pas d'écartement : mono
    const auto centred = play (d, 60, 9600, 1.0f, p);
    CHECK_THAT (rms (centred.l), WithinAbs (rms (centred.r), 1e-5));

    // courbe « supersaw » : un autre rendu que la courbe linéaire
    d.osc[0].curve = DetuneCurve::Supersaw;
    const auto curve = play (d, 60, 9600, 1.0f, p);
    double d2 = 0.0;
    for (std::size_t i = 0; i < curve.l.size(); ++i) d2 += std::abs (static_cast<double> (curve.l[i]) - centred.l[i]);
    CHECK (d2 > 1.0);
    for (const float s : curve.l) REQUIRE (std::isfinite (s));
}

TEST_CASE ("synth hard sync makes the slave periodic at the master's period")
{
    constexpr double period = 100.0;     // 480 Hz
    auto periodicity = [period] (bool sync)
    {
        auto d = plain (kWaveSaw);
        d.osc[1].wave = kWaveSaw;
        d.osc[1].octave = 1;
        d.osc[1].semitone = 5;           // rapport ~2,67 : sans synchro, pas périodique à 100 échantillons
        d.osc[1].sync = sync;
        auto p = tunedTo (period);
        p.level[0] = 0.0;                // l'oscillateur 1 est muet mais sert de maître
        p.level[1] = 100.0;
        const auto out = play (d, 69, 8000, 1.0f, p);
        double num = 0.0, den = 0.0;
        for (std::size_t i = 2000; i < 6000; ++i)
        {
            const double a = out.l[i], b = out.l[i + static_cast<std::size_t> (period)];
            num += a * b;
            den += a * a;
        }
        return num / den; // autocorrélation normalisée au retard d'une période
    };
    CHECK (periodicity (true) > 0.98);
    CHECK (periodicity (false) < 0.9);
}

TEST_CASE ("synth sub oscillator and noise")
{
    auto d = plain (kWaveSine);
    SynthVoices::Params p;
    p.level[0] = 0.0;
    p.level[kSourceSub] = 100.0;
    d.subWave = SubWave::Sine;
    d.subOctave = -1;
    CHECK (std::abs (crossings (play (d, 69, 48000, 1.0f, p).l) - 220) <= 1);
    d.subOctave = -2;
    CHECK (std::abs (crossings (play (d, 69, 48000, 1.0f, p).l) - 110) <= 1);
    d.subWave = SubWave::Square;
    const auto sq = play (d, 69, 4800, 1.0f, p);
    CHECK (rms (sq.l) > 0.1);

    p.level[kSourceSub] = 0.0;
    p.level[kSourceNoise] = 100.0;
    d.noise = NoiseType::White;
    const auto white = play (d, 69, 24000, 1.0f, p);
    d.noise = NoiseType::Pink;
    const auto pink = play (d, 69, 24000, 1.0f, p);
    CHECK (rms (white.l) > 0.02);
    CHECK (rms (pink.l) > 0.01);
    for (const float s : pink.l) REQUIRE (std::isfinite (s));
    // le bruit rose a plus de graves : moins de passages par zéro que le blanc
    CHECK (crossings (pink.l) < crossings (white.l));
}

TEST_CASE ("synth pitch envelope bends the note and the filters shape it")
{
    auto d = plain (kWaveSine);
    d.pitchEnv.attack.ms = 0.0;
    d.pitchEnv.decay.ms = 200.0;
    d.pitchEnv.sustain = 0.0;
    SynthVoices::Params p;
    p.pitchEnvDepth = 12.0; // une octave au début, qui retombe
    const auto out = play (d, 69, 48000, 1.0f, p);
    const int early = crossings (out.l, 0, 2400);       // 50 premières ms
    const int late = crossings (out.l, 36000, 48000);   // 250 ms : retombé à 440 Hz
    CHECK (early > 25);                                 // ~ 880 Hz : ~44 en 50 ms (au moins >> 22 de 440 Hz)
    CHECK (std::abs (late - 440 / 4) <= 3);

    // deux filtres : en série, la pente double ; en parallèle, un mélange
    auto s = plain (kWaveSaw);
    s.filters.filter[0].on = true;
    s.filters.filter[0].slopeDb = 12;
    SynthVoices::Params q;
    q.cutoffHz[0] = 300.0;
    const double one = rms (play (s, 81, 9600, 1.0f, q).l, 2000);
    s.filters.filter[1].on = true;
    s.filters.filter[1].slopeDb = 12;
    q.cutoffHz[1] = 300.0;
    const double two = rms (play (s, 81, 9600, 1.0f, q).l, 2000);
    CHECK (two < 0.6 * one);
    s.filters.parallel = true;
    s.filters.balance = 0.5;
    const auto par = play (s, 81, 9600, 1.0f, q);
    CHECK_THAT (rms (par.l, 2000) / one, WithinAbs (1.0, 0.2)); // deux filtres identiques en parallèle : même niveau qu'un seul
    for (const float v : par.l) REQUIRE (std::isfinite (v));
}

TEST_CASE ("synth polyphony: release ends voices, the limit steals, mono and legato keep one voice")
{
    SynthDescription d = plain (kWaveSaw);
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
    run (8);
    CHECK (v.activeVoices() == 0);

    d.maxVoices = 2;
    const SynthPatch limited (d);
    v.setPatch (&limited);
    v.noteOn (0, 60, 1.0f);
    v.noteOn (0, 62, 1.0f);
    v.noteOn (0, 64, 1.0f);
    CHECK (v.heldVoices() == 2);
    run (4);
    CHECK (v.activeVoices() == 2);

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

TEST_CASE ("synth worst case stays finite: 8 unison voices on 3 oscillators with sync and both filters")
{
    SynthDescription d;
    for (int o = 0; o < numOscillators; ++o)
    {
        d.osc[static_cast<std::size_t> (o)].wave = kWaveLadderSaw + o;
        d.osc[static_cast<std::size_t> (o)].unison = 8;
        d.osc[static_cast<std::size_t> (o)].curve = DetuneCurve::Supersaw;
    }
    d.osc[1].sync = d.osc[2].sync = true;
    d.filters.filter[1].on = true;
    d.filters.filter[0].envDepthOctaves = 4.0;
    d.pitchEnv.decay.ms = 100.0;
    SynthVoices::Params p;
    p.level[1] = p.level[2] = p.level[3] = p.level[4] = 100.0;
    p.pitchEnvDepth = 24.0;
    p.cutoffHz[0] = 800.0;
    p.resonance[0] = 0.9;
    const auto out = play (d, 100, 24000, 1.0f, p);
    for (const float s : out.l) REQUIRE (std::isfinite (s));
    for (const float s : out.r) REQUIRE (std::isfinite (s));
    CHECK (rms (out.l) > 0.0);
}

TEST_CASE ("synth description: bounds and JSON round trip")
{
    SynthDescription d;
    d.osc[0].wave = kWaveVoxA;
    d.osc[1].wave = kWavePulse;
    d.osc[1].unison = 6;
    d.osc[1].spread = 0.8;
    d.osc[1].curve = DetuneCurve::Supersaw;
    d.osc[2].sync = true;
    d.osc[2].retrigger = false;
    d.subWave = SubWave::Triangle;
    d.subOctave = -2;
    d.noise = NoiseType::Pink;
    d.mix[2].mute = true;
    d.mix[3].pan = -0.4;
    d.ampEnv.attack.ms = 120.0;
    d.pitchEnv.decay.ms = 80.0;
    d.filters.filter[1].on = true;
    d.filters.filter[1].model = FilterModel::Ladder;
    d.filters.parallel = true;
    d.filters.balance = 0.3;
    d.polyphony = PolyphonyMode::Legato;
    d.maxVoices = 9;
    d.stealMode = VoiceStealMode::KeepLow;
    d.glideMs = 250;
    CHECK (synthFromJson (toJson (d)) == sanitised (d));

    SynthDescription wild;
    wild.osc[1].unison = 99;
    wild.osc[0].octave = 99;
    wild.osc[0].wave = 9999;
    wild.osc[0].sync = true;
    wild.maxVoices = 100000;
    wild.glideMs = -4;
    wild.subOctave = 5;
    const auto s = sanitised (wild);
    CHECK (s.osc[1].unison == maxUnison);
    CHECK (s.osc[0].octave == SynthDescription::maxOctave);
    CHECK (s.osc[0].wave == kNumWaves - 1);
    CHECK_FALSE (s.osc[0].sync);
    CHECK (s.maxVoices == SynthDescription::maxVoicesLimit);
    CHECK (s.glideMs == 0);
    CHECK (s.subOctave == -1);
    CHECK (synthFromJson ("not json") == SynthDescription {});
    CHECK (synthFromJson (R"({"oscillators":[{"wave":"banana","unison":"x"}]})") == SynthDescription {});
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

SynthDescription sineSynth()
{
    SynthDescription d;
    d.osc[0].wave = kWaveSine;
    return d;
}
} // namespace

TEST_CASE ("engine: a synth track makes sound, an inactive track does not, switching the type cuts the voices")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (sineSynth()));
    r.run (1);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.run (8) > 0.05f);
    CHECK (r.engine.activeVoices() == 1);

    REQUIRE (r.params.set (probe::trackParam (0, probe::kVolume), -100.0));
    CHECK (r.run (8) == 0.0f);
    REQUIRE (r.params.set (probe::trackParam (0, probe::kVolume), 0.0));
    CHECK (r.run (8) > 0.05f);

    r.engine.setTrackType (0, TrackType::Sampler);
    CHECK (r.run (4) == 0.0f);
    CHECK (r.engine.activeVoices() == 0);

    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.setTrackActive (0, false);
    r.run (1);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.run (8) == 0.0f);
    r.engine.setTrackActive (0, true);
    r.engine.noteOn (0, 60, 1.0f);
    CHECK (r.run (8) > 0.05f);
}

TEST_CASE ("engine: the synth follows the track gain, source levels and note-off")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (sineSynth()));
    r.run (1);
    r.engine.noteOn (0, 60, 1.0f);
    const float loud = r.run (8);
    REQUIRE (r.params.set (mixer::samplerParam (0, mixer::kGain), -12.0));
    CHECK_THAT (r.run (8) / loud, WithinAbs (std::pow (10.0, -12.0 / 20.0), 0.03));
    REQUIRE (r.params.set (mixer::samplerParam (0, mixer::kGain), 0.0));

    // niveau de la source 1 (paramètre automatisable de la piste) : à zéro, plus rien
    REQUIRE (r.params.set (mixer::synthParam (0, mixer::kLevelOsc1), 0.0));
    CHECK (r.run (8) < 1e-6f);
    REQUIRE (r.params.set (mixer::synthParam (0, mixer::kLevelOsc1), 100.0));
    CHECK (r.run (8) > 0.05f);

    r.engine.noteOff (0, 60);
    CHECK (r.run (200) < 1e-6f);
    CHECK (r.engine.activeVoices() == 0);
}

TEST_CASE ("engine: synth parameters exist for every track with their documented ids and defaults")
{
    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    REQUIRE (probe::buildPool (set, mixer::numTracks));
    REQUIRE (mixer::addParams (set, mixer::numTracks, mixer::numBuses));
    for (int t = 0; t < mixer::numTracks; ++t)
        for (int k = 0; k < mixer::kSynthKinds; ++k)
            CHECK (set.find (mixer::synthParam (t, static_cast<mixer::SynthKind> (k))) != nullptr);
    CHECK (set.get (mixer::synthParam (0, mixer::kLevelOsc1)) == 100.0);
    CHECK (set.get (mixer::synthParam (0, mixer::kLevelOsc2)) == 0.0);
    CHECK (set.get (mixer::synthParam (3, mixer::kPulseOsc2)) == 50.0);
}

TEST_CASE ("engine: synth voices appear in the voice snapshot without a zone")
{
    Rig r;
    r.engine.setTrackType (0, TrackType::Synth);
    r.engine.publishSynth (0, std::make_shared<SynthPatch> (sineSynth()));
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
    m.tracks[1].synth.osc[0].wave = kWaveTriangle;
    m.tracks[1].synth.osc[1].unison = 7;
    m.tracks[1].type = TrackType::Synth;
    m.selectedTrack = 1;
    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.tracks[1].synth.osc[0].wave == kWaveTriangle);
    CHECK (back.activeCount() == 2);

    m.resetTrack (1);
    CHECK_FALSE (m.tracks[1].active);
    CHECK (m.tracks[1].type == TrackType::Sampler);
    CHECK (m.tracks[1].strip.name.empty());
    CHECK (m.tracks[1].strip.midiChannel == 2);
    CHECK (m.selectedTrack == 0);
}

TEST_CASE ("an empty track added by the user stays active after a round trip, and track 1 can be removed")
{
    MixerState m = MixerState::freshProject();
    m.tracks[4].active = true;
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
    m.tracks[0].synth.osc[0].wave = kWaveSquare;
    m.tracks[0].synth.osc[0].octave = 1;
    m.tracks[0].synth.filters.parallel = true;
    const auto text = state::save (set, "id", std::nullopt, &m);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    CHECK (*r.mixer == m);
    CHECK (r.mixer->tracks[0].type == TrackType::Synth);
}

TEST_CASE ("global voice limit steals the weakest voice across tracks", "[synth][voices]")
{
    Engine e;
    e.prepare (48000.0, 256);
    for (int t = 0; t < 3; ++t)
    {
        e.setTrackActive (t, true);
        e.setTrackType (t, TrackType::Synth);
        SynthDescription d;
        d.ampEnv.attack.ms = 0.0;
        d.ampEnv.release.ms = 500.0;
        e.publishSynth (t, std::make_shared<SynthPatch> (d));
    }
    mixer::MasterSettings m;
    m.maxVoices = 8;
    e.setMasterSettings (m);

    std::vector<float> l (256), r (256);
    float* out[2] = { l.data(), r.data() };
    // 3 pistes x 6 notes = 18 notes demandées : jamais plus de 8 voix qui sonnent hors fondu
    for (int n = 0; n < 6; ++n)
        for (int t = 0; t < 3; ++t)
        {
            e.noteOnTrack (t, 0, 48 + n * 2 + t, 0.5f + 0.05f * static_cast<float> (n));
            e.process (out, 2, 256);
            int sounding = 0;
            for (int i = 0; i < 3; ++i) sounding += e.activeVoices (i);
            CHECK (sounding <= 8 + 3); // + les voix volées encore en fondu de quelques millisecondes
        }
    for (int i = 0; i < 40; ++i) e.process (out, 2, 256); // les fondus sont finis
    int total = 0;
    for (int i = 0; i < 3; ++i) total += e.activeVoices (i);
    CHECK (total == 8);
}
