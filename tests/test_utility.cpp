// Modules stock : socle commun (trims, mix, bypass), Utilitaire (spec-modules §8), inserts du moteur, état.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/State.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Synth.h"
#include "neptune/core/UtilityModule.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Bench
{
    std::shared_ptr<InsertInstance> inst = createInsert ("utility", kRate, kBlock);
    ParameterSet& p() { return inst->runner.params(); }
    void set (ParamId id, double v) { REQUIRE (p().find (id) != nullptr); p().set (id, v); }
    float meter (int i) { return inst->runner.module().meter (i); }

    // Joue `blocks` blocs du même signal d'entrée (fonction du temps) ; rend le dernier bloc.
    template <typename F>
    std::pair<std::vector<float>, std::vector<float>> run (F signal, int blocks = 6, bool bypass = false)
    {
        std::vector<float> l (kBlock), r (kBlock);
        long t = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i, ++t) { const auto s = signal (t); l[static_cast<std::size_t> (i)] = s.first; r[static_cast<std::size_t> (i)] = s.second; }
            inst->runner.process (l.data(), r.data(), kBlock, bypass);
        }
        return { l, r };
    }
};

using Pair = std::pair<float, float>;
Pair sine (long t) { return { 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 440.0 * static_cast<double> (t) / kRate)),
                              0.25f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 660.0 * static_cast<double> (t) / kRate)) }; }
} // namespace

TEST_CASE ("utility: defaults pass the signal through unchanged")
{
    Bench b;
    REQUIRE (b.inst != nullptr);
    long base = 5 * kBlock;
    const auto [l, r] = b.run (sine);
    for (int i = 0; i < kBlock; ++i)
    {
        CHECK_THAT (l[static_cast<std::size_t> (i)], WithinAbs (sine (base + i).first, 1e-5));
        CHECK_THAT (r[static_cast<std::size_t> (i)], WithinAbs (sine (base + i).second, 1e-5));
    }
}

TEST_CASE ("utility: volume, trims, mix and bypass")
{
    Bench b;
    b.set (utility::volumeL, -6.0);
    auto [l, r] = b.run ([] (long) -> Pair { return { 0.5f, 0.5f }; });
    CHECK_THAT (l[10], WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1e-4));
    CHECK_THAT (r[10], WithinAbs (0.5 * std::pow (10.0, -6.0 / 20.0), 1e-4)); // en mode Stéréo, le volume de gauche vaut pour les deux

    b.set (utility::volumeL, 0.0);
    b.set (stock::inputTrim, 6.0);
    b.set (stock::outputTrim, -6.0);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.25f, 0.25f }; });
    CHECK_THAT (l[10], WithinAbs (0.25, 1e-3)); // +6 dB puis -6 dB

    b.set (stock::inputTrim, 0.0);
    b.set (stock::outputTrim, 0.0);
    b.set (utility::volumeL, -100.0); // -inf
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.5f, 0.5f }; });
    CHECK (std::abs (l[10]) < 1e-6);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.5f, 0.5f }; }, 6, true); // contourné : le signal passe
    CHECK_THAT (l[10], WithinAbs (0.5, 1e-5));
    b.set (stock::mix, 50.0);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.5f, 0.5f }; });
    CHECK_THAT (l[10], WithinAbs (0.25, 1e-4)); // moitié sec (0,5), moitié traité (0)
}

TEST_CASE ("utility: phase invert, swap, mono and width")
{
    Bench b;
    auto input = [] (long) -> Pair { return { 0.6f, 0.2f }; };
    b.set (utility::phaseL, 1.0);
    auto [l, r] = b.run (input);
    CHECK_THAT (l[10], WithinAbs (-0.6, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.2, 1e-5));
    b.set (utility::phaseL, 0.0);

    b.set (utility::swap, 1.0);
    std::tie (l, r) = b.run (input);
    CHECK_THAT (l[10], WithinAbs (0.2, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.6, 1e-5));
    b.set (utility::swap, 0.0);

    b.set (utility::mono, 1.0);
    std::tie (l, r) = b.run (input);
    CHECK_THAT (l[10], WithinAbs (0.4, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.4, 1e-5));
    b.set (utility::mono, 0.0);

    b.set (utility::width, 0.0); // largeur nulle = mono
    std::tie (l, r) = b.run (input);
    CHECK_THAT (l[10], WithinAbs (0.4, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.4, 1e-5));
    b.set (utility::width, 200.0); // le côté double : L = M + 2S
    std::tie (l, r) = b.run (input);
    CHECK_THAT (l[10], WithinAbs (0.4 + 2.0 * 0.2, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.4 - 2.0 * 0.2, 1e-5));
}

TEST_CASE ("utility: single and dual pan")
{
    Bench b;
    auto input = [] (long) -> Pair { return { 0.5f, 0.5f }; };
    b.set (utility::pan, 100.0); // loi Balance : le canal gauche est coupé
    auto [l, r] = b.run (input);
    CHECK (std::abs (l[10]) < 1e-5);
    CHECK_THAT (r[10], WithinAbs (0.5, 1e-5));
    b.set (utility::pan, 0.0);

    // pan double : par défaut L tout à gauche et R tout à droite = signal inchangé
    b.set (utility::panMode, 1.0);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.6f, 0.2f }; });
    CHECK_THAT (l[10], WithinAbs (0.6, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.2, 1e-5));

    // les deux canaux au centre : puissance constante, -3 dB chacun vers chaque sortie
    b.set (utility::panL, 0.0);
    b.set (utility::panR, 0.0);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.6f, 0.2f }; });
    const double c = std::sqrt (0.5);
    CHECK_THAT (l[10], WithinAbs (c * (0.6 + 0.2), 1e-4));
    CHECK_THAT (r[10], WithinAbs (c * (0.6 + 0.2), 1e-4));

    // les deux canaux échangés de côté
    b.set (utility::panL, 100.0);
    b.set (utility::panR, -100.0);
    std::tie (l, r) = b.run ([] (long) -> Pair { return { 0.6f, 0.2f }; });
    CHECK_THAT (l[10], WithinAbs (0.2, 1e-4));
    CHECK_THAT (r[10], WithinAbs (0.6, 1e-4));
}

TEST_CASE ("utility: stereo to M/S, M/S to stereo, and the round trip")
{
    Bench enc, dec;
    enc.set (utility::io, 1.0);
    dec.set (utility::io, 2.0);
    auto input = [] (long) -> Pair { return { 0.6f, 0.2f }; };
    auto [m, s] = enc.run (input);
    CHECK_THAT (m[10], WithinAbs (0.4, 1e-5)); // M = (L+R)/2
    CHECK_THAT (s[10], WithinAbs (0.2, 1e-5)); // S = (L-R)/2
    auto [l, r] = dec.run ([] (long) -> Pair { return { 0.4f, 0.2f }; });
    CHECK_THAT (l[10], WithinAbs (0.6, 1e-5));
    CHECK_THAT (r[10], WithinAbs (0.2, 1e-5));

    // un signal mono donne un côté nul, au même niveau dans M
    std::tie (m, s) = enc.run ([] (long) -> Pair { return { 0.5f, 0.5f }; });
    CHECK_THAT (m[10], WithinAbs (0.5, 1e-5));
    CHECK (std::abs (s[10]) < 1e-6);
}

TEST_CASE ("utility: delay, dual mono, DC filter")
{
    Bench b;
    b.set (utility::delayL, 1.0); // 1 ms = 48 échantillons
    std::vector<float> l (kBlock), r (kBlock);
    // une impulsion au bloc 3, le retard doit la déplacer de 48 échantillons
    std::vector<float> out;
    for (int blk = 0; blk < 5; ++blk)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (blk == 3) { l[10] = 1.0f; r[10] = 1.0f; }
        b.inst->runner.process (l.data(), r.data(), kBlock, false);
        if (blk >= 3) out.insert (out.end(), l.begin(), l.end());
    }
    int where = -1;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] > 0.5f) { where = static_cast<int> (i); break; }
    CHECK (where == 10 + 48);

    // double mono : retards distincts par canal
    Bench d;
    d.set (utility::mode, 1.0);
    d.set (utility::delayL, 0.0);
    d.set (utility::delayR, 2.0);
    int whereL = -1, whereR = -1;
    for (int blk = 0; blk < 4; ++blk)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (blk == 1) { l[5] = 1.0f; r[5] = 1.0f; }
        d.inst->runner.process (l.data(), r.data(), kBlock, false);
        for (int i = 0; i < kBlock; ++i)
        {
            if (l[static_cast<std::size_t> (i)] > 0.5f) whereL = blk * kBlock + i;
            if (r[static_cast<std::size_t> (i)] > 0.5f) whereR = blk * kBlock + i;
        }
    }
    CHECK (whereR - whereL == 96); // 2 ms

    // DC : un décalage continu disparaît
    Bench c;
    c.set (utility::dc, 1.0);
    long n = 0;
    std::vector<float> a (kBlock), bb (kBlock);
    for (int blk = 0; blk < 400; ++blk) // ~2 s
    {
        std::fill (a.begin(), a.end(), 0.3f);
        std::fill (bb.begin(), bb.end(), 0.3f);
        c.inst->runner.process (a.data(), bb.data(), kBlock, false);
        n += kBlock;
    }
    CHECK (std::abs (a[kBlock - 1]) < 0.01f);
}

TEST_CASE ("utility: correlation meter")
{
    Bench b;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    b.run ([&] (long) -> Pair { const float x = u (rng); return { x, x }; }, 300);
    CHECK (b.meter (utility::kCorrelation) > 0.95f);
    b.run ([&] (long) -> Pair { const float x = u (rng); return { x, -x }; }, 300);
    CHECK (b.meter (utility::kCorrelation) < -0.95f);
    b.run ([&] (long) -> Pair { return { u (rng), u (rng) }; }, 400);
    CHECK (std::abs (b.meter (utility::kCorrelation)) < 0.25f);
    CHECK (b.meter (utility::kInPeakL) > 0.3f);
}

TEST_CASE ("utility: parameters are bounded and the registry knows the module")
{
    CHECK (createInsert ("nope", kRate, kBlock) == nullptr);
    REQUIRE (availableModules().size() >= 1);
    CHECK (availableModules()[0].typeId == "utility");
    Bench b;
    b.p().set (utility::delayL, 250.0);
    CHECK (b.p().get (utility::delayL) <= 100.0); // borné à 100 ms
    // aucune valeur ne fait sortir de NaN
    b.set (utility::width, 200.0);
    b.set (utility::volumeL, 12.0);
    b.set (stock::inputTrim, 24.0);
    auto [l, r] = b.run (sine, 10);
    for (const float s : l) REQUIRE (std::isfinite (s));
    for (const float s : r) REQUIRE (std::isfinite (s));
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
        engine.prepare (kRate, kBlock);
        bufs.assign (4, std::vector<float> (kBlock, 0.0f));
        for (auto& b : bufs) ptrs.push_back (b.data());
        engine.setTrackType (0, TrackType::Synth);
        SynthDescription d;
        d.osc[0].wave = kWaveSine;
        engine.publishSynth (0, std::make_shared<SynthPatch> (d));
        run (1);
        engine.noteOn (0, 69, 1.0f);
    }
    float run (int blocks = 8)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (ptrs.data(), static_cast<int> (ptrs.size()), kBlock);
            peak = 0.0f;
            for (const float s : bufs[0]) peak = std::max (peak, std::abs (s));
        }
        return peak;
    }
};

std::shared_ptr<InsertInstance> utilityWith (double volumeDb)
{
    auto inst = createInsert ("utility", kRate, kBlock);
    inst->runner.params().set (utility::volumeL, volumeDb);
    return inst;
}
} // namespace

TEST_CASE ("engine: a track insert processes the signal, its bypass parameter is the host one")
{
    Rig r;
    const float open = r.run (8);
    REQUIRE (open > 0.05f);
    r.engine.setInsert (StripKind::Track, 0, 0, utilityWith (-100.0)); // coupe tout
    CHECK (r.run (8) == 0.0f);
    // bypass de l'emplacement 1 de la piste 1 (paramètre exposé à l'hôte)
    REQUIRE (r.params.set (probe::trackParam (0, probe::kInsertBypass), 1.0));
    CHECK (r.run (8) > 0.9f * open);
    REQUIRE (r.params.set (probe::trackParam (0, probe::kInsertBypass), 0.0));
    CHECK (r.run (8) == 0.0f);
    // retrait de l'insert
    r.engine.setInsert (StripKind::Track, 0, 0, nullptr);
    CHECK (r.run (8) > 0.9f * open);
}

TEST_CASE ("engine: inserts of a bus and of the master")
{
    Rig r;
    // la piste 1 envoie vers le bus 1 ET va au master
    mixer::StripSettings s;
    s.midiChannel = 0;
    s.sends[0].destination = 1;
    s.sends[0].tap = mixer::TapPoint::PostPan;
    s.routing.mainEnabled = false; // seulement par le bus
    r.engine.setTrackSettings (0, s);
    REQUIRE (r.params.set (probe::trackParam (0, probe::kSendLevel), 0.0));
    const float open = r.run (8);
    REQUIRE (open > 0.05f);
    r.engine.setInsert (StripKind::Bus, 0, 2, utilityWith (-6.0));
    const float half = r.run (8);
    CHECK_THAT (half / open, WithinAbs (0.5, 0.03));
    r.engine.setInsert (StripKind::Master, 0, 0, utilityWith (-100.0));
    CHECK (r.run (8) == 0.0f);
    REQUIRE (r.params.set (mixer::masterInsertBypassId (0), 1.0));
    CHECK (r.run (8) > 0.4f * open);
}

TEST_CASE ("engine: send tap points before and after the inserts differ")
{
    Rig r;
    // la sortie principale est coupée : on n'entend que l'envoi vers le bus 1
    auto strip = [] (mixer::TapPoint tap)
    {
        mixer::StripSettings s;
        s.midiChannel = 0;
        s.routing.mainEnabled = false;
        s.sends[0].destination = 1;
        s.sends[0].tap = tap;
        return s;
    };
    REQUIRE (r.params.set (probe::trackParam (0, probe::kSendLevel), 0.0));
    r.engine.setInsert (StripKind::Track, 0, 0, utilityWith (-100.0)); // l'insert coupe le signal
    r.engine.setTrackSettings (0, strip (mixer::TapPoint::PreInsert));
    CHECK (r.run (8) > 0.05f); // pré-inserts : le signal brut arrive au bus
    r.engine.setTrackSettings (0, strip (mixer::TapPoint::PostInsertPreFader));
    CHECK (r.run (8) == 0.0f); // post-inserts : coupé par l'insert
}

TEST_CASE ("mixer state: inserts survive a JSON round trip, including the master")
{
    MixerState m = MixerState::freshProject();
    m.tracks[0].strip.inserts[2].type = "utility";
    m.tracks[0].strip.inserts[2].values[utility::volumeL] = -3.5;
    m.tracks[0].strip.inserts[2].values[stock::mix] = 40.0;
    m.master.inserts[0].type = "utility";
    m.master.inserts[0].values[utility::width] = 150.0;
    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.tracks[0].strip.inserts[2].values.at (utility::volumeL) == -3.5);

    // des valeurs invalides sont ignorées, un type trop long aussi
    const auto wild = mixerFromJson (R"({"tracks":[{"inserts":[{"type":"utility","values":{"x":1,"102":"y","103":-2}},{"type":""}]}]})");
    CHECK (wild.tracks[0].strip.inserts[0].type == "utility");
    CHECK (wild.tracks[0].strip.inserts[0].values.size() == 1);
    CHECK (wild.tracks[0].strip.inserts[1].empty());

    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    const auto text = state::save (set, "id", std::nullopt, &m);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    CHECK (*r.mixer == m);
}
