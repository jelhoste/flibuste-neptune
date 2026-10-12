// Tests du Sampler : instrument, voix, remise au thread audio, intégration au moteur (palier 1).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "neptune/core/Engine.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/InstrumentSlot.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/SamplerVoices.h"
#include "neptune/core/State.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Sinus de `freq` Hz (fréquence d'échantillonnage `rate`), `frames` échantillons, mono, amplitude 0.5.
std::shared_ptr<const SampleData> makeSine (double freq, double rate, std::int64_t frames, bool int16 = false, int channels = 1)
{
    auto s = std::make_shared<SampleData>();
    s->channels = channels;
    s->frames = frames;
    s->sampleRate = rate;
    s->name = "sine";
    if (int16) s->pcm16.resize (static_cast<std::size_t> (frames * channels));
    else s->pcm32.resize (static_cast<std::size_t> (frames * channels));
    for (int c = 0; c < channels; ++c)
        for (std::int64_t i = 0; i < frames; ++i)
        {
            const double v = 0.5 * std::sin (2.0 * kPi * freq * static_cast<double> (i) / rate) * (c == 1 ? -1.0 : 1.0);
            const auto idx = static_cast<std::size_t> (c * frames + i);
            if (int16) s->pcm16[idx] = static_cast<std::int16_t> (std::lround (v * 32767.0));
            else s->pcm32[idx] = static_cast<float> (v);
        }
    return s;
}

std::shared_ptr<const Instrument> oneZone (std::shared_ptr<const SampleData> s, int root, PlayMode mode, double releaseSeconds = 0.05)
{
    auto inst = std::make_shared<Instrument>();
    auto z = makeFullKeyboardZone (std::move (s), root, mode);
    z.releaseSeconds = releaseSeconds;
    inst->zones.push_back (std::move (z));
    return inst;
}

// Fréquence estimée par comptage des passages par zéro montants.
double estimateFrequency (const std::vector<float>& x, double rate, std::size_t from, std::size_t to)
{
    int crossings = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++crossings;
    return static_cast<double> (crossings) * rate / static_cast<double> (to - from);
}

struct Render
{
    std::vector<float> l, r;
};

Render renderVoices (SamplerVoices& v, int frames, int block = 256)
{
    Render out;
    out.l.assign (static_cast<std::size_t> (frames), 0.0f);
    out.r.assign (static_cast<std::size_t> (frames), 0.0f);
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        v.render (out.l.data() + pos, out.r.data() + pos, n, 1.0f, 1.0f, 0.0);
    }
    return out;
}

float peak (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < to && i < x.size(); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}
} // namespace

TEST_CASE ("a note at the root plays the sample unchanged")
{
    const auto inst = oneZone (makeSine (440.0, 48000.0, 48000), 69, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 69, 1.0f);
    const auto out = renderVoices (v, 24000);
    CHECK_THAT (estimateFrequency (out.l, 48000.0, 1000, 23000), WithinAbs (440.0, 2.0));
    CHECK_THAT (peak (out.l, 1000, 23000), WithinAbs (0.5, 0.01));
}

TEST_CASE ("transposition follows the key: octave up doubles the frequency, a fifth is 3:2")
{
    const auto inst = oneZone (makeSine (220.0, 48000.0, 96000), 57, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 69, 1.0f);
    const auto up = renderVoices (v, 24000);
    CHECK_THAT (estimateFrequency (up.l, 48000.0, 1000, 23000), WithinAbs (440.0, 3.0));

    v.allSoundOff();
    v.noteOn (0, 64, 1.0f); // +7 demi-tons
    const auto fifth = renderVoices (v, 24000);
    CHECK_THAT (estimateFrequency (fifth.l, 48000.0, 1000, 23000), WithinAbs (220.0 * std::pow (2.0, 7.0 / 12.0), 3.0));
}

TEST_CASE ("file sample rate is compensated: 44.1 kHz file at 48 kHz engine keeps its pitch")
{
    const auto inst = oneZone (makeSine (441.0, 44100.0, 44100), 60, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    const auto out = renderVoices (v, 24000);
    CHECK_THAT (estimateFrequency (out.l, 48000.0, 1000, 23000), WithinAbs (441.0, 3.0));
}

TEST_CASE ("one-shot plays to the end, ignores note off, then frees the voice")
{
    const auto inst = oneZone (makeSine (440.0, 48000.0, 4800), 69, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 69, 1.0f);
    v.noteOff (0, 69);
    auto out = renderVoices (v, 2400);
    CHECK (peak (out.l, 2000, 2400) > 0.3f); // toujours en train de jouer malgré le Note Off
    out = renderVoices (v, 4800);
    CHECK (v.activeVoices() == 0);
}

TEST_CASE ("loop sustains beyond the sample length until note off, then fades out")
{
    // 480 échantillons = exactement 4.4 périodes de 440 Hz : on boucle sur une fraction, pas besoin de continuité parfaite.
    const auto inst = oneZone (makeSine (480.0, 48000.0, 4800), 69, PlayMode::Loop, 0.020);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 69, 1.0f);
    auto out = renderVoices (v, 48000); // 1 s, le sample dure 0.1 s
    CHECK (v.activeVoices() == 1);
    CHECK (peak (out.l, 40000, 48000) > 0.3f);

    v.noteOff (0, 69);
    out = renderVoices (v, 4800, 128);
    CHECK (peak (out.l, 0, 128) > 0.2f);        // début du fondu
    CHECK (peak (out.l, 1200, 4800) == 0.0f);   // 20 ms = 960 échantillons : silence ensuite
    CHECK (v.activeVoices() == 0);
}

TEST_CASE ("loop points are honoured")
{
    // Sample : première moitié = 0.5 constant, seconde moitié = -0.5 constant ; boucle sur la seconde moitié seulement.
    auto s = std::make_shared<SampleData>();
    s->channels = 1; s->frames = 2000; s->sampleRate = 48000.0;
    s->pcm32.assign (2000, 0.5f);
    for (int i = 1000; i < 2000; ++i) s->pcm32[static_cast<std::size_t> (i)] = -0.5f;
    auto inst = std::make_shared<Instrument>();
    auto z = makeFullKeyboardZone (s, 60, PlayMode::Loop);
    z.loopStart = 1000;
    z.loopEnd = 2000;
    inst->zones.push_back (z);

    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    const auto out = renderVoices (v, 6000);
    CHECK_THAT (out.l[500], WithinAbs (0.5, 1e-4));      // partie avant la boucle, jouée une fois
    CHECK_THAT (out.l[1500], WithinAbs (-0.5, 1e-4));
    CHECK_THAT (out.l[5000], WithinAbs (-0.5, 1e-4));    // tourne dans la boucle, ne revient jamais au 0.5
    CHECK (peak (out.l, 2000, 6000) <= 0.5001f);
    for (std::size_t i = 2000; i < 6000; ++i) REQUIRE (out.l[i] < 0.0f);
}

TEST_CASE ("velocity scales the level, stereo samples keep their channels, mono feeds both")
{
    SamplerVoices v;
    v.prepare (48000.0);
    const auto stereo = oneZone (makeSine (440.0, 48000.0, 48000, false, 2), 69, PlayMode::OneShot);
    v.setInstrument (stereo.get());
    v.noteOn (0, 69, 0.5f);
    auto out = renderVoices (v, 4800);
    CHECK_THAT (peak (out.l, 100, 4800), WithinAbs (0.25, 0.01));
    float dot = 0.0f;
    for (std::size_t i = 100; i < 4800; ++i) dot += out.l[i] * out.r[i];
    CHECK (dot < 0.0f); // R est l'opposé de L

    v.allSoundOff();
    const auto mono = oneZone (makeSine (440.0, 48000.0, 48000), 69, PlayMode::OneShot);
    v.setInstrument (mono.get());
    v.noteOn (0, 69, 1.0f);
    out = renderVoices (v, 4800);
    for (std::size_t i = 0; i < 4800; ++i) REQUIRE (out.l[i] == out.r[i]);
}

TEST_CASE ("int16 and float storage sound the same")
{
    const auto a = oneZone (makeSine (330.0, 48000.0, 9600, true), 60, PlayMode::OneShot);
    const auto b = oneZone (makeSine (330.0, 48000.0, 9600, false), 60, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (a.get());
    v.noteOn (0, 64, 1.0f);
    const auto oa = renderVoices (v, 4800);
    v.setInstrument (b.get());
    v.noteOn (0, 64, 1.0f);
    const auto ob = renderVoices (v, 4800);
    for (std::size_t i = 0; i < 4800; ++i) REQUIRE_THAT (oa.l[i], WithinAbs (ob.l[i], 2e-4));
}

TEST_CASE ("zones are selected by key and velocity")
{
    auto lo = makeSine (220.0, 48000.0, 480);
    auto hi = makeSine (880.0, 48000.0, 480);
    Instrument inst;
    Zone a = makeFullKeyboardZone (lo, 60, PlayMode::OneShot);
    a.keyLow = 0; a.keyHigh = 59;
    Zone b = makeFullKeyboardZone (hi, 72, PlayMode::OneShot);
    b.keyLow = 60; b.keyHigh = 127; b.velLow = 64; b.velHigh = 127;
    inst.zones = { a, b };

    CHECK (inst.find (40, 0.8f)->sample == lo);
    CHECK (inst.find (80, 0.9f)->sample == hi);
    CHECK (inst.find (80, 0.2f) == nullptr);  // vélocité trop basse pour la zone haute
    CHECK (inst.find (59, 1.0f)->sample == lo);
}

TEST_CASE ("polyphony: oldest held voice is stolen with a short fade, never beyond the limit")
{
    const auto inst = oneZone (makeSine (440.0, 48000.0, 480000), 69, PlayMode::Loop);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    for (int k = 0; k < SamplerVoices::kMaxVoices; ++k) v.noteOn (0, k, 0.5f);
    CHECK (v.heldVoices() == SamplerVoices::kMaxVoices);

    v.noteOn (0, 100, 0.5f); // 65e note : vole la plus ancienne (touche 0)
    CHECK (v.heldVoices() == SamplerVoices::kMaxVoices);
    CHECK (v.activeVoices() == SamplerVoices::kMaxVoices + 1); // la volée s'éteint en fondu

    renderVoices (v, 480); // 10 ms > fondu de 3 ms
    CHECK (v.activeVoices() == SamplerVoices::kMaxVoices);

    // Rafale : jamais plus de kMaxVoices tenues, jamais de dépassement de capacité.
    for (int k = 0; k < 500; ++k) v.noteOn (0, k % 128, 0.5f);
    CHECK (v.heldVoices() <= SamplerVoices::kMaxVoices);
    CHECK (v.activeVoices() <= SamplerVoices::kMaxVoices + SamplerVoices::kFadeSlots);
}

TEST_CASE ("a stolen voice fades instead of cutting")
{
    // Voix tenue à pleine amplitude (DC 0.5), volée : la sortie doit décroître sans saut de plus de 0.5/ (3 ms).
    auto s = std::make_shared<SampleData>();
    s->channels = 1; s->frames = 480000; s->sampleRate = 48000.0;
    s->pcm32.assign (480000, 0.5f);
    const auto inst = oneZone (s, 60, PlayMode::Loop);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    for (int k = 0; k < SamplerVoices::kMaxVoices; ++k) v.noteOn (0, 60, 1.0f);
    renderVoices (v, 256);
    v.noteOn (0, 61, 1.0f);
    const auto out = renderVoices (v, 512, 512);
    // Chaque voix vaut 0.5 ; la volée passe de 0.5 à 0 en 144 échantillons : variation par échantillon <= 0.5/144 + marge.
    for (std::size_t i = 1; i < out.l.size(); ++i)
        REQUIRE (std::abs (out.l[i] - out.l[i - 1]) < 0.5f / 100.0f + 0.5f / 144.0f * 0.0f + 0.005f);
}

TEST_CASE ("replacing the instrument stops the voices and plays the new one")
{
    const auto a = oneZone (makeSine (220.0, 48000.0, 48000), 57, PlayMode::OneShot);
    const auto b = oneZone (makeSine (880.0, 48000.0, 48000), 81, PlayMode::OneShot);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (a.get());
    v.noteOn (0, 57, 1.0f);
    CHECK (v.activeVoices() == 1);
    v.setInstrument (b.get());
    CHECK (v.activeVoices() == 0);
    v.noteOn (0, 81, 1.0f);
    const auto out = renderVoices (v, 24000);
    CHECK_THAT (estimateFrequency (out.l, 48000.0, 1000, 23000), WithinAbs (880.0, 5.0));
}

TEST_CASE ("instrument slot hands over instruments and returns the old ones")
{
    InstrumentSlot slot;
    CHECK (slot.current() == nullptr);
    CHECK_FALSE (slot.adoptPending());

    auto a = std::make_shared<Instrument>();
    auto b = std::make_shared<Instrument>();
    auto c = std::make_shared<Instrument>();
    std::weak_ptr<const Instrument> wa = a, wb = b, wc = c;

    slot.publish (a);
    a.reset();
    CHECK (slot.adoptPending());
    CHECK (slot.current() == wa.lock().get());

    slot.publish (b); // remplacé avant d'être adopté : jamais vu par l'audio
    b.reset();
    slot.publish (c);
    CHECK (wb.expired()); // détruit par le thread de message (b), jamais par l'audio
    c.reset();
    CHECK (slot.adoptPending());
    CHECK (slot.current() == wc.lock().get());
    CHECK_FALSE (wa.expired()); // en attente de collecte, jamais détruit par l'audio
    slot.collect();
    CHECK (wa.expired());
}

TEST_CASE ("instrument description round trips through json and is sanitised")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "/tmp/a b/é.wav";
    z.keyLow = 36; z.keyHigh = 60; z.velLow = 10; z.velHigh = 100; z.rootNote = 48;
    z.mode = PlayMode::Loop; z.releaseMs = 120.0; z.tuneCents = -12.5; z.gainDb = -3.0; z.loopStart = 100; z.loopEnd = 900;
    d.zones.push_back (z);
    CHECK (instrumentFromJson (toJson (d)) == d);

    const auto bad = instrumentFromJson (R"({"zones":[{"sample":"x.wav","key_low":200,"key_high":-5,"root_note":999,"mode":"warp","release_ms":-4,"loop_start":50,"loop_end":10},42]})");
    REQUIRE (bad.zones.size() == 1);
    CHECK (bad.zones[0].keyLow == 0);
    CHECK (bad.zones[0].keyHigh == 127);
    CHECK (bad.zones[0].rootNote == 127);
    CHECK (bad.zones[0].mode == PlayMode::OneShot);
    CHECK (bad.zones[0].releaseMs == 1.0);
    CHECK (bad.zones[0].loopEnd == -1);
    CHECK (instrumentFromJson ("not json").empty());
    CHECK (instrumentFromJson (R"({"zones":"x"})").empty());
}

TEST_CASE ("engine plays through output 1, gain and tuning parameters apply, other outputs stay silent")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    Engine e;
    e.attachParameters (&params);
    e.prepare (48000.0, 512);
    e.publishInstrument (oneZone (makeSine (440.0, 48000.0, 48000), 69, PlayMode::OneShot));

    std::vector<std::vector<float>> bufs (4, std::vector<float> (512, 1.0f));
    float* chans[] = { bufs[0].data(), bufs[1].data(), bufs[2].data(), bufs[3].data() };

    e.process (chans, 4, 512); // adopte l'instrument, rien ne joue encore
    CHECK (e.activeVoices() == 0);
    e.noteOn (0, 69, 1.0f);
    std::vector<float> l, r;
    for (int b = 0; b < 20; ++b)
    {
        e.process (chans, 4, 512);
        l.insert (l.end(), bufs[0].begin(), bufs[0].end());
        r.insert (r.end(), bufs[1].begin(), bufs[1].end());
        for (float s : bufs[2]) REQUIRE (s == 0.0f);
        for (float s : bufs[3]) REQUIRE (s == 0.0f);
    }
    CHECK (peak (l, 1000, l.size()) > 0.4f);
    CHECK_THAT (estimateFrequency (l, 48000.0, 1000, 9000), WithinAbs (440.0, 4.0));

    // Gain -6 dB et accordage +12 demi-tons
    params.set (sampler::gain, -6.0206);
    params.set (sampler::tuneSemitones, 12.0);
    e.publishInstrument (oneZone (makeSine (440.0, 48000.0, 48000), 69, PlayMode::OneShot)); // repart d'une voix neuve
    e.process (chans, 4, 512);
    CHECK (e.activeVoices() == 0);
    e.noteOn (0, 69, 1.0f);
    l.clear();
    for (int b = 0; b < 20; ++b)
    {
        e.process (chans, 2, 512);
        l.insert (l.end(), bufs[0].begin(), bufs[0].end());
    }
    CHECK_THAT (peak (l, 2000, l.size()), WithinAbs (0.25, 0.02)); // -6 dB sur une amplitude de 0.5
    CHECK_THAT (estimateFrequency (l, 48000.0, 2000, 9000), WithinAbs (880.0, 8.0));
}

TEST_CASE ("engine without an instrument is silent and notes are harmless")
{
    Engine e;
    e.prepare (48000.0, 256);
    e.noteOn (0, 60, 1.0f);
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    float* chans[] = { l.data(), r.data() };
    e.process (chans, 2, 256);
    for (float s : l) REQUIRE (s == 0.0f);
    CHECK (e.activeVoices() == 0);
}

TEST_CASE ("instrument is saved in the project state; older documents have none")
{
    ParameterSet set;
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "/samples/organ_c4.flac";
    z.rootNote = 60; z.mode = PlayMode::Loop; z.releaseMs = 200.0;
    d.zones.push_back (z);

    const auto text = state::save (set, {}, std::nullopt, &d);
    const auto r = state::load (set, text);
    REQUIRE (r.ok());
    REQUIRE (r.instrument.has_value());
    CHECK (*r.instrument == d);

    CHECK_FALSE (state::load (set, state::save (set)).instrument.has_value());
    CHECK_FALSE (state::load (set, R"({"format":"neptune-state","schema_version":2,"parameters":{}})").instrument.has_value());
}

TEST_CASE ("building an instrument reuses the cache and reports missing files")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "/definitely/not/here.wav";
    d.zones.push_back (z);
    std::vector<std::string> warnings;
    SampleCache cache;
    const auto inst = buildInstrument (d, &warnings, &cache);
    CHECK (inst->zones.empty());
    REQUIRE (warnings.size() == 1);
    CHECK (warnings[0].find ("here.wav") != std::string::npos);
    CHECK (cache.empty());
}
