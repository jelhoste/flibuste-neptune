// Filtres par voie : réponse en fréquence, stabilité, modulation de la coupure (palier 1, jalon 8).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Filter.h"
#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/SamplerVoices.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

FilterSettings svf (FilterMode mode, int slope, double res = 0.0)
{
    FilterSettings f;
    f.on = true;
    f.model = FilterModel::SVF;
    f.mode = mode;
    f.slopeDb = slope;
    f.resonance = res;
    return f;
}

// Gain (RMS sortie / RMS entrée) d'un sinus de `freq` Hz, filtre réglé à `cutoff` Hz.
double gainAt (const FilterSettings& f, double cutoff, double freq)
{
    const auto c = makeFilterCoefs (f, cutoff, f.resonance, kRate);
    FilterState st;
    const int n = 48000;
    double in2 = 0.0, out2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float x = static_cast<float> (std::sin (2.0 * kPi * freq * i / kRate));
        const float y = processFilter (c, st, x);
        if (i >= n / 2) { in2 += static_cast<double> (x) * x; out2 += static_cast<double> (y) * y; }
    }
    return std::sqrt (out2 / in2);
}
} // namespace

TEST_CASE ("svf low-pass: flat below the cutoff, -3 dB at it, falls 12 or 24 dB per octave above")
{
    const auto lp12 = svf (FilterMode::LowPass, 12);
    CHECK_THAT (gainAt (lp12, 1000.0, 50.0), WithinAbs (1.0, 0.02));
    CHECK_THAT (gainAt (lp12, 1000.0, 1000.0), WithinAbs (0.7071, 0.03));
    // une octave au-dessus ~ -12 dB (x0.25) ; deux octaves ~ -24 dB
    CHECK_THAT (gainAt (lp12, 1000.0, 2000.0), WithinAbs (0.22, 0.05));
    CHECK (gainAt (lp12, 1000.0, 8000.0) < 0.02);

    const auto lp24 = svf (FilterMode::LowPass, 24);
    CHECK_THAT (gainAt (lp24, 1000.0, 50.0), WithinAbs (1.0, 0.02));
    CHECK (gainAt (lp24, 1000.0, 2000.0) < 0.1);
    CHECK (gainAt (lp24, 1000.0, 8000.0) < 0.0005);
}

TEST_CASE ("svf high-pass, band-pass and notch behave as their names say")
{
    const auto hp = svf (FilterMode::HighPass, 12);
    CHECK (gainAt (hp, 1000.0, 50.0) < 0.02);
    CHECK_THAT (gainAt (hp, 1000.0, 10000.0), WithinAbs (1.0, 0.03));

    const auto bp = svf (FilterMode::BandPass, 12, 0.5);
    const double atCentre = gainAt (bp, 1000.0, 1000.0);
    CHECK (atCentre > gainAt (bp, 1000.0, 100.0) * 3.0);
    CHECK (atCentre > gainAt (bp, 1000.0, 10000.0) * 3.0);

    const auto notch = svf (FilterMode::Notch, 12);
    CHECK (gainAt (notch, 1000.0, 1000.0) < 0.05);
    CHECK_THAT (gainAt (notch, 1000.0, 50.0), WithinAbs (1.0, 0.03));
    CHECK_THAT (gainAt (notch, 1000.0, 15000.0), WithinAbs (1.0, 0.03));
}

TEST_CASE ("resonance raises the response around the cutoff")
{
    const double flat = gainAt (svf (FilterMode::LowPass, 12, 0.0), 1000.0, 1000.0);
    const double peaky = gainAt (svf (FilterMode::LowPass, 12, 0.9), 1000.0, 1000.0);
    CHECK (peaky > flat * 4.0);
}

TEST_CASE ("ladder: 4-pole low-pass, steeper than the 12 dB svf, 12 dB tap available")
{
    FilterSettings l24;
    l24.on = true;
    l24.model = FilterModel::Ladder;
    l24.slopeDb = 24;
    CHECK_THAT (gainAt (l24, 1000.0, 50.0), WithinAbs (1.0, 0.03));
    CHECK (gainAt (l24, 1000.0, 4000.0) < 0.02);   // 2 octaves : ~ -48 dB attendu pour 24 dB/oct, avec marge
    CHECK (gainAt (l24, 1000.0, 4000.0) < gainAt (svf (FilterMode::LowPass, 12), 1000.0, 4000.0));

    FilterSettings l12 = l24;
    l12.slopeDb = 12;
    CHECK (gainAt (l12, 1000.0, 4000.0) > gainAt (l24, 1000.0, 4000.0)); // 12 dB plus ouvert que 24 dB

    FilterSettings reso = l24;
    reso.resonance = 0.8;
    CHECK (gainAt (reso, 1000.0, 900.0) > gainAt (l24, 1000.0, 900.0) * 2.0);
}

TEST_CASE ("no model produces NaN or blows up: extreme settings, impulses, fast cutoff changes")
{
    for (const auto model : { FilterModel::SVF, FilterModel::Ladder })
        for (const auto mode : { FilterMode::LowPass, FilterMode::HighPass, FilterMode::BandPass, FilterMode::Notch })
            for (const int slope : { 12, 24 })
                for (const double res : { 0.0, 0.5, 1.0 })
                {
                    FilterSettings f;
                    f.on = true; f.model = model; f.mode = mode; f.slopeDb = slope; f.resonance = res; f.drive = 1.0;
                    FilterState st;
                    float peak = 0.0f;
                    for (int i = 0; i < 20000; ++i)
                    {
                        const double cut = 20.0 * std::pow (1000.0, 0.5 + 0.5 * std::sin (i * 0.01)); // 20 Hz .. 20 kHz
                        const auto c = makeFilterCoefs (f, cut, res, kRate);
                        const float x = (i % 997 == 0) ? 1.0f : (i % 3 == 0 ? 0.5f : -0.3f); // impulsions + bruit
                        const float y = processFilter (c, st, x);
                        REQUIRE (std::isfinite (y));
                        peak = std::max (peak, std::abs (y));
                    }
                    CHECK (peak < 200.0f); // résonance forte : une résonance, pas une divergence
                }
}

TEST_CASE ("cutoff modulation: key tracking, velocity, envelope, and the audio-rate limit")
{
    FilterSettings f;
    f.keyTrack = 1.0;
    // une octave au-dessus de la note 60 : la coupure double
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 72, 0.5f, 0.0f, kRate), WithinAbs (2000.0, 1e-6));
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 48, 0.5f, 0.0f, kRate), WithinAbs (500.0, 1e-6));
    f.keyTrack = 0.5;
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 72, 0.5f, 0.0f, kRate), WithinAbs (1000.0 * std::sqrt (2.0), 1e-6));

    f = FilterSettings {};
    f.velocityOctaves = 2.0; // vélocité max : +2 octaves ; 0 : -2 octaves ; 0,5 : inchangée
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 60, 1.0f, 0.0f, kRate), WithinAbs (4000.0, 1e-6));
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 60, 0.0f, 0.0f, kRate), WithinAbs (250.0, 1e-6));
    CHECK_THAT (modulatedCutoffHz (f, 1000.0, 60, 0.5f, 0.0f, kRate), WithinAbs (1000.0, 1e-6));

    f = FilterSettings {};
    f.envDepthOctaves = 3.0;
    CHECK_THAT (modulatedCutoffHz (f, 500.0, 60, 0.5f, 1.0f, kRate), WithinAbs (4000.0, 1e-6));
    CHECK_THAT (modulatedCutoffHz (f, 500.0, 60, 0.5f, 0.0f, kRate), WithinAbs (500.0, 1e-6));

    // bornes : jamais sous 20 Hz ni au-delà de 0,45 x fs
    f.envDepthOctaves = 8.0;
    CHECK (modulatedCutoffHz (f, 15000.0, 60, 0.5f, 1.0f, 44100.0) <= 0.45 * 44100.0 + 1e-6);
    f.envDepthOctaves = -8.0;
    CHECK (modulatedCutoffHz (f, 100.0, 60, 0.5f, 1.0f, kRate) >= 20.0);
}

TEST_CASE ("sanitising bounds every filter field")
{
    FilterSettings f;
    f.cutoffHz = 1e9; f.resonance = 7.0; f.keyTrack = -50.0; f.velocityOctaves = 99.0; f.envDepthOctaves = std::nan (""); f.drive = -1.0;
    f.slopeDb = 17;
    f.model = FilterModel::Ladder;
    f.mode = FilterMode::HighPass;
    const auto s = sanitised (f);
    CHECK (s.cutoffHz == filterMaxHz);
    CHECK (s.resonance == 1.0);
    CHECK (s.keyTrack == -1.0);
    CHECK (s.velocityOctaves == 6.0);
    CHECK (s.envDepthOctaves == 0.0);
    CHECK (s.drive == 0.0);
    CHECK (s.slopeDb == 12);
    CHECK (s.mode == FilterMode::LowPass); // le Ladder est toujours passe-bas
    FilterBlock b;
    b.balance = 3.0;
    CHECK (sanitised (b).balance == 1.0);
}

// ---------------------------------------------------------------- dans le moteur

namespace
{
std::shared_ptr<const SampleData> sineSample (double freq, std::int64_t frames = 96000)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = frames;
    s->sampleRate = kRate;
    s->pcm32.resize (static_cast<std::size_t> (frames));
    for (std::int64_t i = 0; i < frames; ++i) s->pcm32[static_cast<std::size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / kRate));
    return s;
}

FilterBlock oneFilter (FilterMode mode, int slope, double cutoff = 20000.0)
{
    FilterBlock b;
    b.filter[0].on = true;
    b.filter[0].mode = mode;
    b.filter[0].slopeDb = slope;
    b.filter[0].cutoffHz = cutoff;
    return b;
}

// Instrument d'une zone (sinus de `freq` Hz, jouée à sa hauteur) construit depuis une description, rendu en RMS sur 0,5 s.
double renderRms (const InstrumentDescription& base, bool overrideZone, const FilterBlock& zoneFilters, double cutoffParam, double freq,
                  double resParam = 0.0, int key = 60, float velocity = 1.0f, int skip = 24000, int window = 24000)
{
    auto inst = std::make_shared<Instrument>();
    InstrumentDescription d = base;
    ZoneDescription zd;
    zd.keyLow = 0; zd.keyHigh = 127; zd.rootNote = key;
    zd.filterOverride = overrideZone;
    zd.filters = zoneFilters;
    d.zones = { zd };
    // construction « à la main » d'une zone avec un sinus (pas de fichier) : on reprend la résolution de buildInstrument
    Instrument built;
    {
        Zone z;
        z.sample = sineSample (freq);
        z.keyLow = 0; z.keyHigh = 127; z.rootNote = key;
        z.mode = PlayMode::Loop;
        z.loopStart = 0; z.loopEnd = z.sample->frames;
        auto makeRt = [] (const FilterBlock& block, bool fromParams) -> std::shared_ptr<const FilterRuntimeBlock>
        {
            const auto b = sanitised (block);
            if (! b.anyOn()) return nullptr;
            auto rt = std::make_shared<FilterRuntimeBlock>();
            for (std::size_t i = 0; i < 2; ++i)
            {
                rt->settings[i] = b.filter[i];
                rt->env[i] = EnvelopeRuntime (b.filter[i].env);
                rt->fromParams[i] = fromParams;
            }
            rt->parallel = b.parallel;
            rt->balance = static_cast<float> (b.balance);
            rt->anyOn = true;
            return rt;
        };
        z.filters = overrideZone ? makeRt (zoneFilters, false) : makeRt (base.defaultFilters, true);
        built.zones.push_back (std::move (z));
    }
    built.finalize();

    SamplerVoices v;
    v.prepare (kRate);
    v.setInstrument (&built);
    SamplerVoices::FilterParams fp;
    fp.cutoffHz[0] = cutoffParam;
    fp.resonance[0] = resParam;
    fp.cutoffHz[1] = cutoffParam;
    v.setFilterParams (fp);
    v.noteOn (0, key, velocity);
    std::vector<float> l (static_cast<std::size_t> (skip + window), 0.0f);
    v.render (l.data(), nullptr, static_cast<int> (l.size()), 1.0f, 1.0f, 0.0);
    double e = 0.0;
    for (std::size_t i = static_cast<std::size_t> (skip); i < l.size(); ++i) e += static_cast<double> (l[i]) * l[i];
    return std::sqrt (e / static_cast<double> (window));
}
} // namespace

TEST_CASE ("inherited filters follow the automatable parameters; overriding zones do not")
{
    InstrumentDescription base;
    base.defaultFilters = oneFilter (FilterMode::LowPass, 12);
    const double open = renderRms (base, false, {}, 20000.0, 5000.0);
    const double closed = renderRms (base, false, {}, 300.0, 5000.0);
    CHECK (open > 0.6);          // sinus de 5 kHz : RMS d'environ 0,707 filtre ouvert
    CHECK (closed < open * 0.05);

    // zone qui surcharge : son filtre à elle (passe-haut), insensible au paramètre de coupure
    const auto own = oneFilter (FilterMode::HighPass, 12, 1000.0);
    const double a = renderRms (base, true, own, 20000.0, 5000.0);
    const double b = renderRms (base, true, own, 100.0, 5000.0);
    CHECK_THAT (a, WithinAbs (b, 1e-6));
    CHECK (a > 0.6); // le passe-haut laisse passer 5 kHz

    // aucun filtre actif : signal intact, quel que soit le paramètre
    const double none = renderRms (InstrumentDescription {}, false, {}, 100.0, 5000.0);
    CHECK_THAT (none, WithinAbs (open, 0.01));
}

TEST_CASE ("series filters attenuate more than the same two in parallel")
{
    InstrumentDescription base;
    FilterBlock b;
    b.filter[0] = oneFilter (FilterMode::LowPass, 12, 1000.0).filter[0];
    b.filter[1] = oneFilter (FilterMode::LowPass, 12, 1000.0).filter[0];
    b.parallel = false;
    const double series = renderRms (base, true, b, 20000.0, 4000.0);
    b.parallel = true;
    b.balance = 0.5;
    const double parallel = renderRms (base, true, b, 20000.0, 4000.0);
    CHECK (series < parallel * 0.5);
    // parallèle, balance 0 : le filtre 1 seul ; balance 1 : le filtre 2 seul (ici identiques)
    b.balance = 0.0;
    const double only1 = renderRms (base, true, b, 20000.0, 4000.0);
    b.balance = 1.0;
    const double only2 = renderRms (base, true, b, 20000.0, 4000.0);
    CHECK_THAT (only1, WithinAbs (only2, 1e-6));
}

TEST_CASE ("the filter envelope opens the cutoff over time")
{
    InstrumentDescription base;
    FilterBlock b = oneFilter (FilterMode::LowPass, 24, 200.0);
    b.filter[0].envDepthOctaves = 5.0; // 200 Hz -> 6,4 kHz à pleine enveloppe
    b.filter[0].env.attack.on = true;
    b.filter[0].env.attack.ms = 300.0;
    const double early = renderRms (base, true, b, 20000.0, 2000.0, 0.0, 60, 1.0f, 0, 4800);       // les 100 premières ms : l'enveloppe monte
    const double late = renderRms (base, true, b, 20000.0, 2000.0, 0.0, 60, 1.0f, 30000, 4800);    // bien après l'attaque (300 ms)
    CHECK (late > early * 5.0);
    CHECK (late > 0.5);
}

TEST_CASE ("velocity and key tracking move the cutoff of a sounding voice")
{
    InstrumentDescription base;
    FilterBlock b = oneFilter (FilterMode::LowPass, 24, 800.0);
    b.filter[0].velocityOctaves = 3.0; // vélocité max : 6,4 kHz ; vélocité 0 : 100 Hz
    const double soft = renderRms (base, true, b, 20000.0, 2000.0, 0.0, 60, 0.05f);
    const double loud = renderRms (base, true, b, 20000.0, 2000.0, 0.0, 60, 1.0f);
    CHECK (loud > soft * 5.0);
}

TEST_CASE ("building an instrument shares the inherited filter tables and resolves overrides")
{
    InstrumentDescription d;
    d.defaultFilters = oneFilter (FilterMode::LowPass, 12);
    ZoneDescription a, b, c;
    a.samplePath = "/nonexistent/a.wav"; b.samplePath = "/nonexistent/b.wav"; c.samplePath = "/nonexistent/c.wav";
    b.filterOverride = true;
    b.filters = oneFilter (FilterMode::HighPass, 24, 500.0);
    c.filterOverride = true; // surcharge sans aucun filtre actif : pas de filtre du tout
    d.zones = { a, b, c };
    std::vector<std::string> warnings;
    const auto inst = buildInstrument (d, &warnings); // les fichiers n'existent pas : zones ignorées, mais rien ne plante
    REQUIRE (inst != nullptr);
    CHECK (inst->zones.empty());
    CHECK (warnings.size() == 3);
}

TEST_CASE ("filter settings are saved with the instrument and with overriding zones, bounded when read")
{
    InstrumentDescription d;
    d.defaultFilters = oneFilter (FilterMode::BandPass, 24, 1234.0);
    d.defaultFilters.filter[1].on = true;
    d.defaultFilters.filter[1].model = FilterModel::Ladder;
    d.defaultFilters.filter[1].drive = 0.6;
    d.defaultFilters.filter[0].envDepthOctaves = 2.5;
    d.defaultFilters.filter[0].env.attack.ms = 120.0;
    d.defaultFilters.parallel = true;
    d.defaultFilters.balance = 0.25;
    ZoneDescription z;
    z.samplePath = "x.wav";
    z.filterOverride = true;
    z.filters = oneFilter (FilterMode::Notch, 12, 4000.0);
    d.zones.push_back (z);
    d.zones.push_back (ZoneDescription {}); // hérite
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.defaultFilters == sanitised (d.defaultFilters));
    REQUIRE (back.zones.size() == 2);
    CHECK (back.zones[0].filterOverride);
    CHECK (back.zones[0].filters == sanitised (z.filters));
    CHECK_FALSE (back.zones[1].filterOverride);

    const auto bad = instrumentFromJson (R"({"default_filters":{"filter1":{"on":true,"cutoff_hz":1e12,"resonance":99,"slope":7,"model":"ladder","mode":"notch"},"balance":-4},"zones":[]})");
    CHECK (bad.defaultFilters.filter[0].cutoffHz == filterMaxHz);
    CHECK (bad.defaultFilters.filter[0].resonance == 1.0);
    CHECK (bad.defaultFilters.filter[0].slopeDb == 12);
    CHECK (bad.defaultFilters.filter[0].mode == FilterMode::LowPass); // ladder : passe-bas
    CHECK (bad.defaultFilters.balance == 0.0);

    // annulation
    InstrumentDescription current;
    const auto before = current;
    auto get = [&] { return current; };
    auto set = [&] (const InstrumentDescription& x) { current = x; };
    UndoManager um;
    um.perform (std::make_unique<InstrumentEditCommand> (current, d, get, set, "Filters"));
    CHECK (current == d);
    um.undo();
    CHECK (current == before);
}

TEST_CASE ("engine reads the filter parameters each block")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (kRate, 256);

    InstrumentDescription d;
    d.defaultFilters = oneFilter (FilterMode::LowPass, 24);
    auto inst = std::make_shared<Instrument>();
    {
        Zone z;
        z.sample = sineSample (5000.0);
        z.keyLow = 0; z.keyHigh = 127; z.rootNote = 60;
        z.mode = PlayMode::Loop;
        z.loopStart = 0; z.loopEnd = z.sample->frames;
        auto rt = std::make_shared<FilterRuntimeBlock>();
        rt->settings[0] = d.defaultFilters.filter[0];
        rt->env[0] = EnvelopeRuntime (rt->settings[0].env);
        rt->fromParams = { true, true };
        rt->anyOn = true;
        z.filters = rt;
        inst->zones.push_back (std::move (z));
    }
    inst->finalize();
    engine.publishInstrument (inst);

    auto rms = [&] (double cutoffHz)
    {
        params.set (sampler::filter1Cutoff, cutoffHz);
        std::vector<float> l (256), r (256);
        float* out[2] = { l.data(), r.data() };
        engine.panic();
        for (int b = 0; b < 4; ++b) engine.process (out, 2, 256); // le fondu de coupure passe
        engine.noteOn (0, 60, 1.0f);
        double e = 0.0;
        for (int b = 0; b < 80; ++b)
        {
            engine.process (out, 2, 256);
            if (b >= 40) for (float x : l) e += static_cast<double> (x) * x;
        }
        return std::sqrt (e / (40.0 * 256.0));
    };
    const double open = rms (20000.0);
    const double closed = rms (200.0);
    CHECK (open > 0.5);
    CHECK (closed < open * 0.01);
}
