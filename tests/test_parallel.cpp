// Pistes parallèles (spec-modules §1) : arbre de pistes, points de captation, somme en fin de chaîne du parent, solo « en place »,
// état du projet.
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
#include "neptune/core/StockModule.h"
#include "neptune/core/Synth.h"
#include "neptune/core/UtilityModule.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

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
    // joue des blocs ; rend la crête du dernier bloc (sortie 1, canal gauche)
    float run (int blocks = 10)
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
    void parallel (int p, int parent, mixer::TapPoint tap = mixer::TapPoint::PostPan)
    {
        engine.setParallel (p, true, parent, tap);
        mixer::StripSettings s;
        engine.setParallelSettings (p, s);
    }
    void set (ParamId id, double v) { REQUIRE (params.find (id) != nullptr); params.set (id, v); }
};

std::shared_ptr<InsertInstance> makeUtility (double volumeDb, bool invert = false)
{
    auto inst = createInsert ("utility", kRate, kBlock);
    inst->runner.params().set (utility::volumeL, volumeDb);
    if (invert)
    {
        inst->runner.params().set (utility::phaseL, 1.0);
        inst->runner.params().set (utility::phaseR, 1.0);
    }
    return inst;
}
} // namespace

TEST_CASE ("parallel: a parallel track is summed into its parent at the end of the chain")
{
    Rig r;
    const float alone = r.run();
    REQUIRE (alone > 0.05f);
    r.parallel (0, 0);                      // copie du signal propre de la piste, au niveau 0 dB
    CHECK_THAT (r.run() / alone, WithinAbs (2.0, 0.05));
    r.set (mixer::parallelParam (0, mixer::kParVolume), -6.0206); // moitié de l'amplitude
    CHECK_THAT (r.run() / alone, WithinAbs (1.5, 0.05));
    r.set (mixer::parallelParam (0, mixer::kParMute), 1.0);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
    r.set (mixer::parallelParam (0, mixer::kParMute), 0.0);

    // la parallèle supprimée (désactivée) ne contribue plus
    r.engine.setParallel (0, false, 0, mixer::TapPoint::PostPan);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
}

TEST_CASE ("parallel: inserts of a parallel track, phase inversion cancels the parent")
{
    Rig r;
    const float alone = r.run();
    r.parallel (0, 0);
    r.engine.setInsert (StripKind::Parallel, 0, 0, makeUtility (0.0, true)); // phase inversée : somme nulle
    CHECK (r.run() < 0.02f * alone);
    REQUIRE (r.params.set (mixer::parallelParam (0, mixer::kParInsertBypass), 1.0)); // insert contourné : on retombe à 2x
    CHECK_THAT (r.run() / alone, WithinAbs (2.0, 0.05));
}

TEST_CASE ("parallel: capture points in the parent's chain")
{
    Rig r;
    const float alone = r.run();
    // l'insert de la piste coupe tout : post-inserts, rien ; pré-inserts, le signal brut
    r.engine.setInsert (StripKind::Track, 0, 0, makeUtility (-100.0));
    CHECK (r.run() < 1e-6f);
    r.parallel (0, 0, mixer::TapPoint::PreInsert);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
    r.parallel (0, 0, mixer::TapPoint::PostInsertPreFader);
    CHECK (r.run() < 1e-6f);
    r.parallel (0, 0, mixer::TapPoint::PostPan);
    CHECK (r.run() < 1e-6f);

    // fader de la piste à -inf : la captation pré-fader entend encore, la post-fader non
    r.engine.setInsert (StripKind::Track, 0, 0, nullptr);
    r.set (probe::trackParam (0, probe::kVolume), -100.0);
    r.parallel (0, 0, mixer::TapPoint::PostInsertPreFader);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
    r.parallel (0, 0, mixer::TapPoint::PostFaderPrePan);
    CHECK (r.run() < 1e-6f);
}

TEST_CASE ("parallel: nesting, a grand-child captures from its direct parent only")
{
    Rig r;
    const float alone = r.run();
    r.parallel (0, 0);                                   // P1 : parent = piste 1
    r.parallel (1, mixer::numTracks + 0);                // P2 : parent = P1
    CHECK_THAT (r.run() / alone, WithinAbs (3.0, 0.08)); // piste + P1 + P2 (P2 copie la sortie propre de P1)
    r.engine.setInsert (StripKind::Parallel, 1, 0, makeUtility (0.0, true)); // P2 inversée : annule P1
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.06)); // piste + (P1 + P2 inversée de P1) = piste
}

TEST_CASE ("parallel: solo in place keeps the parent feeding it but silent")
{
    Rig r;
    const float alone = r.run();
    r.parallel (0, 0);
    r.set (mixer::parallelParam (0, mixer::kParVolume), -6.0206);
    CHECK_THAT (r.run() / alone, WithinAbs (1.5, 0.05));
    r.set (mixer::parallelParam (0, mixer::kParSolo), 1.0);
    CHECK_THAT (r.run() / alone, WithinAbs (0.5, 0.05)); // on n'entend que la parallèle (-6 dB), le parent est muet
    r.set (mixer::parallelParam (0, mixer::kParSolo), 0.0);
    CHECK_THAT (r.run() / alone, WithinAbs (1.5, 0.05));

    // le solo de la piste garde ses parallèles audibles
    r.set (probe::trackParam (0, probe::kSolo), 1.0);
    CHECK_THAT (r.run() / alone, WithinAbs (1.5, 0.05));
}

TEST_CASE ("parallel: orphans and loops are never processed")
{
    Rig r;
    const float alone = r.run();
    r.parallel (0, mixer::numTracks + 0);                // son propre parent : boucle
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
    r.parallel (1, 5);                                   // parent = piste 6, inactive de fait (aucun instrument) : rien à entendre
    r.parallel (2, mixer::numTracks + 3);                // P3 -> P4 -> P3 : boucle entre deux parallèles
    r.parallel (3, mixer::numTracks + 2);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
}

TEST_CASE ("parallel: sends of a parallel track reach a bus")
{
    Rig r;
    const float alone = r.run();
    // la piste n'a plus de sortie principale ; la parallèle envoie au bus 1 (post-pan), le bus va au master
    mixer::StripSettings s;
    s.routing.mainEnabled = false;
    r.engine.setTrackSettings (0, s);
    CHECK (r.run() < 1e-6f);
    r.parallel (0, 0);
    mixer::StripSettings ps;
    ps.routing.mainEnabled = false;       // elle ne se somme pas dans le parent
    ps.sends[0].destination = 1;
    ps.sends[0].tap = mixer::TapPoint::PostPan;
    r.engine.setParallelSettings (0, ps);
    r.set (mixer::parallelParam (0, mixer::kParSendLevel), 0.0);
    CHECK_THAT (r.run() / alone, WithinAbs (1.0, 0.05));
}

TEST_CASE ("mixer state: parallel tracks survive JSON, depth and cascade removal")
{
    auto m = MixerState::freshProject();
    m.tracks[1].active = true;
    m.parallels[0].active = true;
    m.parallels[0].parent = 0;
    m.parallels[0].tap = mixer::TapPoint::PostFaderPrePan;
    m.parallels[0].strip.name = "Room";
    m.parallels[0].strip.inserts[1].type = "utility";
    m.parallels[1].active = true;
    m.parallels[1].parent = mixer::numTracks + 0;
    m.parallels[2].active = true;
    m.parallels[2].parent = 1;
    CHECK (m.parallelDepth (0) == 1);
    CHECK (m.parallelDepth (1) == 2);
    CHECK (m.parallelDepth (2) == 1);
    CHECK (m.parallelChildren (0).size() == 1);
    const auto back = mixerFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.parallels[0].strip.name == "Room");
    CHECK (back.parallels[1].parent == mixer::numTracks);

    // retirer une parallèle retire ses descendants
    auto removed = m.removeParallel (0);
    CHECK (removed.size() == 2);
    CHECK_FALSE (m.parallels[0].active);
    CHECK_FALSE (m.parallels[1].active);
    CHECK (m.parallels[2].active);
    // retirer une piste retire ses parallèles
    m.resetTrack (1);
    CHECK_FALSE (m.parallels[2].active);

    // des parents invalides sont détachés à la lecture
    const auto wild = mixerFromJson (R"({"parallels":[{"active":true,"parent":16},{"active":true,"parent":30},{"active":true,"parent":3}]})");
    CHECK (wild.parallels[0].parent == 0);  // son propre parent : ramené à la piste 1
    CHECK_FALSE (wild.parallels[1].active); // parent inexistant
    CHECK_FALSE (wild.parallels[2].active); // piste 4 inactive

    ParameterSet set;
    REQUIRE (sampler::addParams (set));
    auto n = MixerState::freshProject();
    n.parallels[4].active = true;
    n.parallels[4].parent = 0;
    const auto text = state::save (set, "id", std::nullopt, &n);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.mixer.has_value());
    CHECK (*r.mixer == n);
}
