// AnalogSynth : le synthé de Neptune en plugin indépendant (paramètres, moteur, état, presets, presets livrés).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <set>
#include <vector>

#include "neptune/core/AnalogSynth.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/WaveCatalog.h"

using namespace neptune::core;
using namespace neptune::core::analogsynth;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;

struct Render
{
    std::vector<float> l, r;
    float peak() const
    {
        float p = 0.0f;
        for (auto v : l) p = std::max (p, std::fabs (v));
        for (auto v : r) p = std::max (p, std::fabs (v));
        return p;
    }
    bool finite() const
    {
        for (auto v : l) if (! std::isfinite (v)) return false;
        for (auto v : r) if (! std::isfinite (v)) return false;
        return true;
    }
};

// Joue une note puis rend `frames` échantillons par blocs de 128.
Render play (AnalogSynthEngine& e, int key, int frames, bool noteOn = true)
{
    e.prepare (kRate, 128);
    if (noteOn) e.noteOn (0, key, 0.8f);
    Render out;
    for (int done = 0; done < frames; done += 128)
    {
        std::vector<float> l (128), r (128);
        e.process (l.data(), r.data(), 128);
        out.l.insert (out.l.end(), l.begin(), l.end());
        out.r.insert (out.r.end(), r.begin(), r.end());
    }
    return out;
}
} // namespace

TEST_CASE ("analogsynth parameter set is complete and uses neptune track 0 ids", "[analogsynth]")
{
    ParameterSet set;
    std::string error;
    REQUIRE (addParams (set, &error));
    CHECK (set.size() == 2 + 7 + mixer::kSynthKinds);
    for (const auto& k : keyedParams())
        CHECK (set.indexOf (k.id) != ParameterSet::npos);
    CHECK (set.indexOf (mixer::synthParam (0, mixer::kPitchEnvDepth)) != ParameterSet::npos);
    CHECK (set.indexOf (mixer::samplerParam (0, mixer::kFilter2Resonance)) != ParameterSet::npos);
    CHECK (volume == 1000);
    CHECK (pan == 1001);

    std::set<std::string> keys;
    std::set<ParamId> ids;
    for (const auto& k : keyedParams()) { keys.insert (k.key); ids.insert (k.id); }
    CHECK (keys.size() == keyedParams().size());
    CHECK (ids.size() == keyedParams().size());
    CHECK (paramIdForKey ("osc2.detune") == mixer::synthParam (0, mixer::kDetuneOsc2));
    CHECK (paramIdForKey ("nope") == 0);
}

TEST_CASE ("analogsynth plays a note, releases and goes silent", "[analogsynth]")
{
    AnalogSynthEngine e;
    REQUIRE (e.ok());
    auto r = play (e, 69, 4800);
    CHECK (r.finite());
    CHECK (r.peak() > 0.05f);
    CHECK (e.activeVoices() == 1);
    CHECK (e.voiceCount() == 1); // le compteur publié pour l'interface

    e.noteOff (0, 69);
    for (int i = 0; i < 400; ++i)
    {
        std::vector<float> l (128), rr (128);
        e.process (l.data(), rr.data(), 128);
    }
    CHECK (e.activeVoices() == 0);
    CHECK (e.voiceCount() == 0);
}

TEST_CASE ("analogsynth volume and pan apply to the output", "[analogsynth]")
{
    AnalogSynthEngine loud, quiet, left;
    const float peakLoud = play (loud, 60, 9600).peak();
    quiet.params().set (volume, -20.0);
    const float peakQuiet = play (quiet, 60, 9600).peak();
    CHECK (peakQuiet < peakLoud * 0.2f);
    CHECK (peakQuiet > peakLoud * 0.05f);

    left.params().set (pan, -100.0);
    const auto r = play (left, 60, 9600);
    float lp = 0.0f, rp = 0.0f;
    for (std::size_t i = 4800; i < r.l.size(); ++i) { lp = std::max (lp, std::fabs (r.l[i])); rp = std::max (rp, std::fabs (r.r[i])); }
    CHECK (lp > 0.05f);
    CHECK (rp < 1.0e-4f);

    AnalogSynthEngine off;
    off.params().set (volume, -100.0);
    CHECK (play (off, 60, 4800).peak() < 1.0e-6f);
}

TEST_CASE ("analogsynth adopts a new patch published from the message thread", "[analogsynth]")
{
    AnalogSynthEngine e;
    e.prepare (kRate, 128);
    SynthDescription sine;
    sine.osc[0].wave = kWaveSine;
    e.setDescription (sine);
    CHECK (e.description().osc[0].wave == kWaveSine);
    e.noteOn (0, 69, 1.0f);
    std::vector<float> l (4800), r (4800);
    e.process (l.data(), r.data(), 4800);
    // un sinus pur à vélocité 1 : crête bien inférieure à une dent de scie (mêmes marges), signal propre
    float peak = 0.0f;
    for (auto v : l) peak = std::max (peak, std::fabs (v));
    CHECK (peak > 0.02f);
    CHECK (peak < 1.0f);
}

TEST_CASE ("analogsynth ui notes go through the lock-free queue", "[analogsynth]")
{
    AnalogSynthEngine e;
    e.prepare (kRate, 128);
    e.postNoteOn (64, 0.9f);
    std::vector<float> l (512), r (512);
    e.process (l.data(), r.data(), 512);
    CHECK (e.activeVoices() == 1);
    e.postNoteOff (64);
    e.postPanic();
    e.process (l.data(), r.data(), 512);
    for (int i = 0; i < 100; ++i) e.process (l.data(), r.data(), 512);
    CHECK (e.activeVoices() == 0);
    for (int i = 0; i < 1000; ++i) e.postNoteOn (60 + i % 12, 0.5f); // file pleine : jamais de blocage ni de dépassement
    e.process (l.data(), r.data(), 512);
    CHECK (e.activeVoices() > 0);
}

TEST_CASE ("analogsynth state round trip", "[analogsynth]")
{
    AnalogSynthEngine a;
    a.params().set (mixer::synthParam (0, mixer::kLevelOsc2), 63.0);
    a.params().set (volume, -7.5);
    SynthDescription d;
    d.osc[1].wave = kWaveWarmSaw;
    d.osc[0].unison = 6;
    d.glideMs = 120;
    d.polyphony = PolyphonyMode::Mono;
    const auto text = stateToJson (a.params(), d, "some-preset-id");

    ParameterSet set;
    REQUIRE (addParams (set));
    SynthDescription back;
    std::string presetId;
    REQUIRE (stateFromJson (text, set, back, presetId));
    CHECK (back == sanitised (d));
    CHECK (presetId == "some-preset-id");
    CHECK_THAT (set.get (mixer::synthParam (0, mixer::kLevelOsc2)), WithinAbs (63.0, 1e-9));
    CHECK_THAT (set.get (volume), WithinAbs (-7.5, 1e-9));

    CHECK_FALSE (stateFromJson ("{}", set, back, presetId));
    CHECK_FALSE (stateFromJson ("garbage", set, back, presetId));
    CHECK_FALSE (stateFromJson (R"({"format":"moussaillon-state"})", set, back, presetId));
}

TEST_CASE ("capturing a preset stores captured parameters only", "[analogsynth][preset]")
{
    ParameterSet set;
    REQUIRE (addParams (set));
    set.set (volume, -9.0);
    set.set (pan, 30.0);
    set.set (sampler::filter1Cutoff, 1234.0);
    SynthDescription d;
    d.glideMs = 80;
    const auto cfg = defaultConfig();
    const auto p = capturePreset (set, d, cfg, "Mon son", "Lead");
    CHECK (p.module == moduleId);
    CHECK (p.name == "Mon son");
    CHECK (p.type == "Lead");
    CHECK (p.parameters.count ("filter1.cutoff") == 1);
    CHECK (p.parameters.at ("filter1.cutoff").number == 1234.0);
    CHECK (p.parameters.count ("output.volume") == 0); // libre : jamais dans le preset
    CHECK (p.parameters.count ("output.pan") == 0);
    CHECK_FALSE (p.state.empty());
}

TEST_CASE ("applying a preset leaves free parameters and glide alone", "[analogsynth][preset]")
{
    ParameterSet set;
    REQUIRE (addParams (set));
    SynthDescription source;
    source.osc[0].wave = kWaveSquare;
    source.glideMs = 300;
    set.set (sampler::filter1Cutoff, 900.0);
    const auto cfg = defaultConfig();
    auto p = capturePreset (set, source, cfg, "Cible");

    // l'utilisateur joue autre chose : volume, pan, glissando et coupure changent
    set.set (volume, -12.0);
    set.set (pan, -50.0);
    set.set (sampler::filter1Cutoff, 15000.0);
    SynthDescription current;
    current.glideMs = 10;
    REQUIRE (applyPreset (p, set, current, cfg));
    CHECK_THAT (set.get (sampler::filter1Cutoff), WithinAbs (900.0, 1e-9)); // capturé : restauré
    CHECK_THAT (set.get (volume), WithinAbs (-12.0, 1e-9));                 // libre : inchangé
    CHECK_THAT (set.get (pan), WithinAbs (-50.0, 1e-9));
    CHECK (current.osc[0].wave == kWaveSquare);
    CHECK (current.glideMs == 10);                                          // glissando libre : inchangé

    // exception du preset : le volume devient capturé pour CE preset
    p.captureOverrides["output.volume"] = preset::Capture::Captured;
    p.parameters["output.volume"] = preset::ParamValue::ofNumber (-3.0);
    REQUIRE (applyPreset (p, set, current, cfg));
    CHECK_THAT (set.get (volume), WithinAbs (-3.0, 1e-9));

    // preset d'un autre module : refusé
    p.module = "hammond";
    CHECK_FALSE (applyPreset (p, set, current, cfg));
}

TEST_CASE ("factory presets are valid, stable and audible", "[analogsynth][preset]")
{
    const auto list = factoryPresets();
    REQUIRE (list.size() >= 6);
    std::set<std::string> ids;
    for (const auto& p : list)
    {
        CHECK (p.module == moduleId);
        CHECK_FALSE (p.name.empty());
        CHECK (ids.insert (p.id).second);
        for (const auto& [key, value] : p.parameters) CHECK (paramIdForKey (key) != 0);

        AnalogSynthEngine e;
        SynthDescription d = e.description();
        REQUIRE (applyPreset (p, e.params(), d, defaultConfig()));
        e.setDescription (d);
        const auto r = play (e, 57, 14400);
        INFO (p.name);
        CHECK (r.finite());
        CHECK (r.peak() > 0.01f);
        CHECK (r.peak() < 1.5f);
    }
    // identifiants stables d'un appel à l'autre (les banques les référencent)
    const auto again = factoryPresets();
    for (std::size_t i = 0; i < list.size(); ++i) CHECK (again[i].id == list[i].id);
}

TEST_CASE ("analogsynth default config frees volume, pan and glide", "[analogsynth][preset]")
{
    const auto c = defaultConfig();
    preset::Preset p;
    CHECK (preset::captureOf ("output.volume", p, c) == preset::Capture::Free);
    CHECK (preset::captureOf ("output.pan", p, c) == preset::Capture::Free);
    CHECK (preset::captureOf (glideKey, p, c) == preset::Capture::Free);
    CHECK (preset::captureOf ("filter1.cutoff", p, c) == preset::Capture::Captured);
}

TEST_CASE ("saving over a preset keeps its identity, metadata and capture exceptions", "[analogsynth][preset]")
{
    ParameterSet set;
    REQUIRE (addParams (set));
    const auto cfg = defaultConfig();
    preset::Preset base;
    base.id = "keep-this-id";
    base.name = "Mon pad";
    base.type = "Pad";
    base.rating = 3;
    base.characteristics = { "doux" };
    base.customFields = { { "Concert", "Noel" } };
    base.derivedFrom = "factory-analogsynth-03";
    base.captureOverrides["output.volume"] = preset::Capture::Captured; // exception : le volume fait partie de CE preset

    set.set (volume, -4.0);
    set.set (sampler::filter1Cutoff, 2500.0);
    const auto saved = capturePreset (set, SynthDescription(), cfg, "ignored", "ignored", &base);
    CHECK (saved.id == "keep-this-id");
    CHECK (saved.name == "Mon pad");
    CHECK (saved.rating == 3);
    CHECK (saved.derivedFrom == "factory-analogsynth-03");
    CHECK (saved.parameters.at ("output.volume").number == -4.0); // capturé par exception
    CHECK (saved.parameters.at ("filter1.cutoff").number == 2500.0);
    CHECK (saved.parameters.count ("output.pan") == 0);           // toujours libre
}
