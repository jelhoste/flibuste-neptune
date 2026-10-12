// Tests de l'enveloppe d'amplitude (palier 1).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/Envelope.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/SamplerVoices.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 48000.0;

std::vector<float> run (const EnvelopeDescription& d, int frames, int noteOffAt = -1)
{
    const EnvelopeRuntime rt (d);
    EnvelopeRunner r;
    r.begin (rt, kSr);
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (frames));
    for (int i = 0; i < frames; ++i)
    {
        if (i == noteOffAt) r.noteOff (rt, kSr);
        out.push_back (r.next (rt, kSr));
    }
    return out;
}

EnvelopeDescription neutral()
{
    EnvelopeDescription e;
    e.release.on = false;
    return e;
}
} // namespace

TEST_CASE ("neutral envelope is a constant 1, not silence")
{
    const auto v = run (neutral(), 2000);
    for (float x : v) REQUIRE (x == 1.0f);
}

TEST_CASE ("attack rises from 0 to 1 over its time then stays at 1")
{
    auto d = neutral();
    d.attack = { true, 10.0, {} }; // 480 échantillons
    const auto v = run (d, 1000);
    CHECK (v[0] == 0.0f);
    CHECK_THAT (v[240], WithinAbs (0.5, 0.01)); // linéaire
    CHECK_THAT (v[479], WithinAbs (1.0, 0.01));
    CHECK (v[500] == 1.0f);
    for (std::size_t i = 1; i < 480; ++i) REQUIRE (v[i] >= v[i - 1]);
}

TEST_CASE ("curve shape: convex rises faster than linear, concave slower")
{
    auto lin = neutral(), convex = neutral(), concave = neutral();
    lin.attack = { true, 20.0, { 0.0, false } };
    convex.attack = { true, 20.0, { 0.8, false } };
    concave.attack = { true, 20.0, { -0.8, false } };
    const auto a = run (lin, 1000), b = run (convex, 1000), c = run (concave, 1000);
    CHECK (b[240] > a[240] + 0.1f);
    CHECK (c[240] < a[240] - 0.1f);
    CHECK_THAT (b[958], WithinAbs (1.0, 0.02)); // même point d'arrivée
    CHECK_THAT (c[958], WithinAbs (1.0, 0.02));
}

TEST_CASE ("db attack is exponential in amplitude: equal dB steps in equal times")
{
    auto d = neutral();
    d.attack = { true, 100.0, { 0.0, true } }; // 4800 échantillons
    const auto v = run (d, 6000);
    CHECK (v[0] == 0.0f);
    CHECK_THAT (v[4799], WithinAbs (1.0, 0.01));
    // mi-parcours : -30 dB sur un plancher de -60 dB -> amplitude 10^-1.5 ≈ 0.0316 (au plancher rescalé près)
    CHECK_THAT (v[2400], WithinAbs ((std::pow (10.0, -1.5) - 0.001) / 0.999, 0.003));
    // le quart de la course est 15 dB plus bas que la mi-course... : rapport constant entre pas égaux
    const double r1 = v[2400] / v[1200], r2 = v[3600] / v[2400];
    CHECK_THAT (r1, WithinAbs (r2, 0.15 * r2));
}

TEST_CASE ("hold keeps the peak, decay goes down to the sustain level and stays")
{
    EnvelopeDescription d = neutral();
    d.attack = { true, 1.0, {} };       // 48
    d.hold = { true, 10.0, {} };        // 480
    d.decay = { true, 20.0, {} };       // 960
    d.sustain = 0.4;
    const auto v = run (d, 3000);
    CHECK_THAT (v[48 + 100], WithinAbs (1.0, 1e-6));            // pendant le hold
    CHECK_THAT (v[48 + 480 + 480], WithinAbs (0.7, 0.01));      // mi-decay
    CHECK_THAT (v[2500], WithinAbs (0.4, 1e-6));                // tenu
    CHECK_THAT (v[2999], WithinAbs (0.4, 1e-6));
}

TEST_CASE ("disabled segments are skipped: only the enabled ones shape the sound")
{
    EnvelopeDescription d = neutral();
    d.attack = { false, 500.0, {} };
    d.decay = { false, 500.0, {} };
    d.sustain = 0.2; // sans decay actif, le sustain ne joue pas
    const auto v = run (d, 500);
    for (float x : v) REQUIRE (x == 1.0f);
}

TEST_CASE ("delay holds silence before the attack starts")
{
    EnvelopeDescription d = neutral();
    d.delay = { true, 10.0, {} };   // 480
    d.attack = { true, 10.0, {} };  // 480
    const auto v = run (d, 1200);
    CHECK (v[0] == 0.0f);
    CHECK (v[479] == 0.0f);
    CHECK_THAT (v[480 + 240], WithinAbs (0.5, 0.01));
    CHECK (v[1100] == 1.0f);
}

TEST_CASE ("release starts from the current level and ends at zero")
{
    EnvelopeDescription d = neutral();
    d.release = { true, 10.0, {} }; // 480
    const auto v = run (d, 1500, 500);
    CHECK (v[499] == 1.0f);
    CHECK_THAT (v[500 + 240], WithinAbs (0.5, 0.02));
    CHECK (v[500 + 479] < 0.01f);
    CHECK (v[1200] == 0.0f);

    // Note Off pendant l'attaque : on part du niveau atteint, sans saut
    EnvelopeDescription a = neutral();
    a.attack = { true, 20.0, {} };
    a.release = { true, 10.0, {} };
    const auto w = run (a, 2000, 480);
    const float atOff = w[479];
    CHECK_THAT (w[480], WithinAbs (atOff, 0.01));
    CHECK (w[480] < 0.6f);
}

TEST_CASE ("release disabled still declicks in 2 ms")
{
    const auto v = run (neutral(), 1000, 100);
    CHECK (v[99] == 1.0f);
    CHECK (v[100 + 96] == 0.0f); // 2 ms = 96 échantillons
    for (std::size_t i = 101; i < 200; ++i) REQUIRE (v[i] <= v[i - 1]);
    CHECK ((v[100] - v[101]) < 0.02f); // pas de saut
}

TEST_CASE ("description is sanitised and presets are what they claim")
{
    EnvelopeDescription e;
    e.attack = { true, -5.0, { 7.0, false } };
    e.decay = { true, 1e9, { -9.0, false } };
    e.sustain = 3.0;
    e.release.ms = 0.0;
    const auto s = sanitised (e);
    CHECK (s.attack.ms == 0.0);
    CHECK (s.attack.curve.shape == 1.0);
    CHECK (s.decay.ms == envelopeMaxMs);
    CHECK (s.decay.curve.shape == -1.0);
    CHECK (s.sustain == 1.0);
    CHECK (s.release.ms == 1.0);

    const auto sus = presetEnvelope (EnvelopePreset::Sustained);
    CHECK_FALSE (sus.attack.on);
    CHECK (sus.release.on);
    const auto pa = presetEnvelope (EnvelopePreset::PercussionAttack);
    CHECK (pa.attack.on);
    CHECK_FALSE (pa.hold.on);
    CHECK_FALSE (pa.decay.on);
    CHECK_FALSE (pa.release.on);
    const auto pd = presetEnvelope (EnvelopePreset::PercussionAttackDecay);
    CHECK (pd.attack.on);
    CHECK (pd.hold.on);
    CHECK (pd.decay.on);
    CHECK (pd.sustain == 0.0);
    for (int i = 0; i < numEnvelopePresets; ++i)
        CHECK (std::string (presetI18nKey (static_cast<EnvelopePreset> (i))).rfind ("envelope.preset_", 0) == 0);
}

TEST_CASE ("time <-> display position mapping is monotonic and invertible")
{
    CHECK (envelopeTimeFromUnit (0.0) == 0.0);
    CHECK_THAT (envelopeTimeFromUnit (1.0), WithinAbs (envelopeMaxMs, 1e-9));
    double prev = -1.0;
    for (int i = 0; i <= 100; ++i)
    {
        const double u = i / 100.0;
        const double t = envelopeTimeFromUnit (u);
        CHECK (t >= prev);
        prev = t;
        CHECK_THAT (envelopeUnitFromTime (t), WithinAbs (u, 1e-9));
    }
    CHECK (envelopeTimeFromUnit (0.1) < 20.0); // précision sur les temps courts
}

// ---- dans les voix ----

namespace
{
std::shared_ptr<const Instrument> constantInstrument (const EnvelopeDescription& env, PlayMode mode)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1; s->frames = 96000; s->sampleRate = kSr;
    s->pcm32.assign (96000, 0.5f);
    auto inst = std::make_shared<Instrument>();
    Zone z = makeFullKeyboardZone (s, 60, mode);
    z.env = EnvelopeRuntime (env);
    inst->zones.push_back (std::move (z));
    return inst;
}
}

TEST_CASE ("one-shot with a decay to zero ends the voice early and ignores note off")
{
    EnvelopeDescription d = neutral();
    d.decay = { true, 50.0, {} }; // 2400 échantillons
    d.sustain = 0.0;
    const auto inst = constantInstrument (d, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (kSr);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    v.noteOff (0, 60); // ignoré
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    v.render (l.data(), r.data(), 4800, 1.0f, 1.0f, 0.0);
    CHECK_THAT (l[0], WithinAbs (0.5, 0.01));
    CHECK_THAT (l[1200], WithinAbs (0.25, 0.01));
    CHECK (l[2600] == 0.0f);
    CHECK (v.activeVoices() == 0); // libérée bien avant la fin du sample (2 s)
}

TEST_CASE ("attack-only shaping leaves the rest of the sample untouched")
{
    EnvelopeDescription d = neutral();
    d.attack = { true, 10.0, {} };
    const auto inst = constantInstrument (d, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (kSr);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
    v.render (l.data(), r.data(), 4800, 1.0f, 1.0f, 0.0);
    CHECK (l[0] < 0.01f);
    CHECK_THAT (l[240], WithinAbs (0.25, 0.01));
    CHECK_THAT (l[2000], WithinAbs (0.5, 1e-5));
    CHECK (v.activeVoices() == 1);
}

TEST_CASE ("editing the instrument keeps playing voices alive with the new settings")
{
    EnvelopeDescription d = neutral();
    const auto a = constantInstrument (d, PlayMode::Loop);
    SamplerVoices v;
    v.prepare (kSr);
    v.setInstrument (a.get());
    v.noteOn (0, 60, 1.0f);
    std::vector<float> l (480, 0.0f), r (480, 0.0f);
    v.render (l.data(), r.data(), 480, 1.0f, 1.0f, 0.0);
    CHECK_THAT (l[479], WithinAbs (0.5, 1e-5));

    // Nouvel instrument qui réutilise le MÊME sample (réglage de zone modifié : gain)
    auto b = std::make_shared<Instrument>();
    Zone z = a->zones[0];
    z.gain = 0.5f;
    b->zones.push_back (z);
    v.setInstrument (b.get());
    CHECK (v.activeVoices() == 1);
    std::fill (l.begin(), l.end(), 0.0f);
    v.render (l.data(), r.data(), 480, 1.0f, 1.0f, 0.0);
    CHECK_THAT (l[10], WithinAbs (0.25, 1e-5)); // la voix suit les réglages de zone (gain, enveloppe, hauteur)

    // Un autre sample : la voix s'arrête
    auto other = constantInstrument (d, PlayMode::Loop);
    v.setInstrument (other.get());
    CHECK (v.activeVoices() == 0);
}
