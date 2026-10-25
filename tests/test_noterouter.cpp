#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/NoteRouter.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/Synth.h"
#include "neptune/core/WaveCatalog.h"

using namespace neptune::core;
using namespace neptune::core::router;

namespace
{
ParameterSet makeSet()
{
    ParameterSet set;
    REQUIRE (addSwitchParams (set));
    return set;
}

Rule coupler (int src, int dst, int transpose, ParamId sw = 0)
{
    Rule r;
    r.srcChannel = src;
    r.dstChannel = dst;
    r.transpose = transpose;
    if (sw != 0) r.terms.push_back ({ sw, false });
    return r;
}

std::vector<Target> targetsOf (const Config& c, const ParameterSet& set, int channel, int key)
{
    const auto compiled = compile (c, set);
    std::array<Target, kMaxTargets> out;
    const int n = expand (*compiled, set, channel, key, out);
    return { out.begin(), out.begin() + n };
}
} // namespace

TEST_CASE ("router: JSON round trip, bounds and tolerant reading")
{
    Config c;
    Rule r = coupler (2, 1, 12, switchParam (3));
    r.name = "Récit à Grand-orgue 4'";
    r.keyLo = 100; r.keyHi = 20;      // inversés
    r.transpose = 99;                 // hors bornes
    r.combine = Combine::Any;
    r.terms.push_back ({ switchParam (4), true });
    c.rules.push_back (r);
    const auto back = fromJson (toJson (c));
    REQUIRE (back.rules.size() == 1);
    CHECK (back.rules[0].keyLo == 20);
    CHECK (back.rules[0].keyHi == 100);
    CHECK (back.rules[0].transpose == 48);
    CHECK (back.rules[0].name == "Récit à Grand-orgue 4'");
    CHECK (back.rules[0].combine == Combine::Any);
    REQUIRE (back.rules[0].terms.size() == 2);
    CHECK (back.rules[0].terms[1].negate);
    CHECK (fromJson ("not json").rules.empty());
    CHECK (fromJson (R"({"rules":[1,{"dst":99,"terms":[{"param":-1},{"param":15001}]}]})").rules.size() == 1);
    Config many;
    many.rules.assign (200, Rule {});
    CHECK (sanitised (many).rules.size() == static_cast<std::size_t> (kMaxRules));
}

TEST_CASE ("router: a coupler follows its switch, the original note always plays")
{
    auto set = makeSet();
    Config c;
    c.rules.push_back (coupler (2, 1, 0, switchParam (0)));      // clavier 2 -> clavier 1 quand le tirant 1 est tiré
    CHECK (targetsOf (c, set, 1, 60) == std::vector<Target> ({ { 1, 60 } }));        // canal 2 (indice 1) : l'origine seule
    set.set (switchParam (0), 1.0);
    CHECK (targetsOf (c, set, 1, 60) == std::vector<Target> ({ { 1, 60 }, { 0, 60 } }));
    CHECK (targetsOf (c, set, 0, 60) == std::vector<Target> ({ { 0, 60 } }));        // un autre canal n'est pas concerné
}

TEST_CASE ("router: octave couplers, key ranges and out-of-range keys")
{
    auto set = makeSet();
    Config c;
    auto super = coupler (1, 1, 12);
    super.keyLo = 36; super.keyHi = 96;
    c.rules.push_back (super);
    CHECK (targetsOf (c, set, 0, 60) == std::vector<Target> ({ { 0, 60 }, { 0, 72 } }));
    CHECK (targetsOf (c, set, 0, 20) == std::vector<Target> ({ { 0, 20 } }));        // hors de l'étendue
    c.rules[0].keyHi = 127;
    CHECK (targetsOf (c, set, 0, 120) == std::vector<Target> ({ { 0, 120 } }));      // 132 n'existe pas
    // unisson : la même destination n'est pas comptée deux fois
    c.rules.push_back (coupler (1, 1, 0));
    CHECK (targetsOf (c, set, 0, 60) == std::vector<Target> ({ { 0, 60 }, { 0, 72 } }));
}

TEST_CASE ("router: a split replaces the original note, conditions combine with logic")
{
    auto set = makeSet();
    Config c;
    Rule low = coupler (1, 2, 0);
    low.keyHi = 59; low.replace = true;
    Rule high = coupler (1, 3, 0);
    high.keyLo = 60; high.replace = true;
    c.rules = { low, high };
    CHECK (targetsOf (c, set, 0, 40) == std::vector<Target> ({ { 1, 40 } }));
    CHECK (targetsOf (c, set, 0, 72) == std::vector<Target> ({ { 2, 72 } }));

    Config logic;
    Rule r = coupler (1, 2, 0);
    r.terms = { { switchParam (0), false }, { switchParam (1), false } };
    logic.rules = { r };
    auto count = [&] { return targetsOf (logic, set, 0, 60).size(); };
    for (const auto combine : { Combine::All, Combine::Any, Combine::One, Combine::NotAll, Combine::None })
    {
        logic.rules[0].combine = combine;
        const bool expected[4][5] = {   // (a,b) = (0,0), (1,0), (0,1), (1,1) ; ET, OU, OU exclusif, NON ET, NON OU
            { false, false, false, true, true }, { false, true, true, true, false }, { false, true, true, true, false }, { true, true, false, false, false } };
        for (int ab = 0; ab < 4; ++ab)
        {
            set.set (switchParam (0), (ab & 1) ? 1.0 : 0.0);
            set.set (switchParam (1), (ab & 2) ? 1.0 : 0.0);
            CHECK ((count() == 2) == expected[ab][static_cast<int> (combine)]);
        }
    }
    // un terme inconnu écarte la règle ; un terme nié inverse l'interrupteur
    logic.rules[0].terms = { { 999999, false } };
    CHECK (count() == 1);
    logic.rules[0].combine = Combine::All;
    logic.rules[0].terms = { { switchParam (5), true } };
    CHECK (count() == 2);
}

TEST_CASE ("router: the engine plays a coupled note on both tracks and releases exactly what it started")
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
    auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) engine.process (ptrs.data(), static_cast<int> (ptrs.size()), 256); };

    SynthDescription d;
    d.osc[0].wave = kWaveSine;
    d.filters.filter[0].on = false;
    d.ampEnv.attack.ms = 0.0;
    d.ampEnv.release.ms = 20.0;
    for (const int t : { 0, 1 })
    {
        engine.setTrackActive (t, true);
        engine.setTrackType (t, TrackType::Synth);
        engine.publishSynth (t, std::make_shared<SynthPatch> (d));
        mixer::StripSettings strip;
        strip.midiChannel = t + 1;    // piste 1 : canal 1, piste 2 : canal 2
        engine.setTrackSettings (t, strip);
    }
    Config config;
    config.rules.push_back (coupler (2, 1, 0, switchParam (0)));   // le canal 2 joue aussi la piste 1 quand le tirant est tiré
    engine.setRouter (config);
    run (2);

    // sans le tirant : la piste 1 ne sonne pas quand on joue le canal 2
    engine.noteOn (1, 60, 1.0f);
    run (4);
    CHECK (engine.activeVoices() == 1);
    engine.noteOff (1, 60);
    run (30);
    CHECK (engine.activeVoices() == 0);

    // avec le tirant : les deux pistes sonnent
    params.set (switchParam (0), 1.0);
    engine.noteOn (1, 60, 1.0f);
    run (4);
    CHECK (engine.activeVoices() == 2);
    // le tirant est repoussé pendant que la touche est tenue : le Note Off défait quand même les deux notes
    params.set (switchParam (0), 0.0);
    engine.noteOff (1, 60);
    run (30);
    CHECK (engine.activeVoices() == 0);

    // une même destination tenue par deux sources ne se coupe qu'avec la dernière
    params.set (switchParam (0), 1.0);
    engine.noteOn (0, 60, 1.0f);    // la piste 1 jouée directement
    engine.noteOn (1, 60, 1.0f);    // et par l'accouplement : la voix de la piste 1 n'est pas rejouée
    run (4);
    CHECK (engine.activeVoices() == 2);
    engine.noteOff (1, 60);
    run (30);
    CHECK (engine.activeVoices() == 1);   // il reste la piste 1 tenue directement
    engine.noteOff (0, 60);
    run (30);
    CHECK (engine.activeVoices() == 0);
}

TEST_CASE ("router: the configuration lives in the mixer state")
{
    MixerState m;
    m.router.rules.push_back (coupler (2, 1, -12, switchParam (7)));
    const auto back = mixerFromJson (toJson (m));
    CHECK (back.router == m.router);
    CHECK (mixerFromJson (toJson (MixerState {})).router.rules.empty());
}

TEST_CASE ("router: the coupler switches are parameters of the mixer set")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    for (int i = 0; i < kSwitches; ++i) CHECK (params.indexOf (switchParam (i)) != ParameterSet::npos);
}
