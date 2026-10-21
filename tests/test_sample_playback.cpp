// Lecture d'une zone : région début / fin, fondu enchaîné de boucle, fondus d'entrée et de sortie, lecture inversée, délai de lecture.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "neptune/core/Instrument.h"
#include "neptune/core/SamplerVoices.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
// Rampe : la valeur de l'échantillon k est k / frames (mono) : on lit directement la position jouée.
std::shared_ptr<SampleData> makeRamp (std::int64_t frames)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = frames;
    s->sampleRate = 48000.0;
    s->pcm32.resize (static_cast<std::size_t> (frames));
    for (std::int64_t i = 0; i < frames; ++i) s->pcm32[static_cast<std::size_t> (i)] = static_cast<float> (i) / static_cast<float> (frames);
    return s;
}

std::shared_ptr<SampleData> makeConstant (std::int64_t frames, float v)
{
    auto s = makeRamp (frames);
    std::fill (s->pcm32.begin(), s->pcm32.end(), v);
    return s;
}

struct Rig
{
    std::shared_ptr<Instrument> inst = std::make_shared<Instrument>();
    SamplerVoices voices;
    Rig (std::shared_ptr<SampleData> sample, PlayMode mode, const std::function<void (Zone&)>& tweak)
    {
        auto z = makeFullKeyboardZone (std::move (sample), 60, mode);
        tweak (z);
        inst->zones.push_back (std::move (z));
        voices.prepare (48000.0);
        voices.setInstrument (inst.get());
    }
    std::vector<float> play (int frames)
    {
        voices.noteOn (0, 60, 1.0f);
        std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (l);
        for (int pos = 0; pos < frames; pos += 256)
        {
            const int n = std::min (256, frames - pos);
            voices.render (l.data() + pos, r.data() + pos, n, 1.0f, 1.0f, 0.0);
        }
        return l;
    }
};
} // namespace

TEST_CASE ("playback starts at the start marker and stops at the end marker")
{
    Rig rig (makeRamp (4000), PlayMode::OneShot, [] (Zone& z) { z.startFrame = 1000; z.endFrame = 2000; });
    const auto out = rig.play (3000);
    CHECK_THAT (out[0], WithinAbs (1000.0 / 4000.0, 1e-3));
    CHECK_THAT (out[500], WithinAbs (1500.0 / 4000.0, 1e-3));
    CHECK_THAT (out[999], WithinAbs (1999.0 / 4000.0, 1e-3));
    for (std::size_t i = 1100; i < 3000; ++i) REQUIRE (out[i] == 0.0f);   // la région est finie : silence
}

TEST_CASE ("reverse plays the region backwards")
{
    Rig rig (makeRamp (4000), PlayMode::OneShot, [] (Zone& z) { z.startFrame = 1000; z.endFrame = 2000; z.reverse = true; });
    const auto out = rig.play (2000);
    CHECK_THAT (out[0], WithinAbs (1999.0 / 4000.0, 1e-3));
    CHECK_THAT (out[500], WithinAbs (1499.0 / 4000.0, 1e-3));
    CHECK_THAT (out[998], WithinAbs (1001.0 / 4000.0, 1e-3));
    for (std::size_t i = 1100; i < 2000; ++i) REQUIRE (out[i] == 0.0f);
}

TEST_CASE ("fade in and fade out ramp the gain")
{
    Rig rig (makeConstant (4800, 1.0f), PlayMode::OneShot, [] (Zone& z) { z.fadeInMs = 10.0; z.fadeOutMs = 10.0; });
    const auto out = rig.play (4800);
    CHECK_THAT (out[0], WithinAbs (0.0, 1e-3));
    CHECK_THAT (out[240], WithinAbs (0.5, 0.02));     // 5 ms = la moitié du fondu de 10 ms (480 échantillons)
    CHECK_THAT (out[1000], WithinAbs (1.0, 1e-3));
    CHECK_THAT (out[4800 - 240], WithinAbs (0.5, 0.03));
    CHECK (out[4799] < 0.01f);
}

TEST_CASE ("playback delay holds the sound back")
{
    Rig rig (makeConstant (4800, 1.0f), PlayMode::OneShot, [] (Zone& z) { z.delayMs = 10.0; });
    const auto out = rig.play (2000);
    for (std::size_t i = 0; i < 479; ++i) REQUIRE (out[i] == 0.0f);   // 10 ms = 480 échantillons de silence
    CHECK_THAT (out[600], WithinAbs (1.0, 1e-3));
}

TEST_CASE ("loop crossfade removes the click at the loop point")
{
    // Sinus de 1 kHz (période de 48 échantillons) : une boucle de 2000 échantillons n'est pas un nombre entier de périodes, le raccord fait donc un saut de phase.
    auto s = std::make_shared<SampleData>();
    s->channels = 1; s->frames = 3000; s->sampleRate = 48000.0;
    s->pcm32.resize (3000);
    for (int i = 0; i < 3000; ++i) s->pcm32[static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0));
    auto maxStep = [] (const std::vector<float>& x, std::size_t from)
    {
        float m = 0.0f;
        for (std::size_t i = from + 1; i < x.size(); ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    };
    Rig hard (s, PlayMode::Loop, [] (Zone& z) { z.loopStart = 1000; z.loopEnd = 3000; });
    Rig soft (s, PlayMode::Loop, [] (Zone& z) { z.loopStart = 1000; z.loopEnd = 3000; z.loopCrossfade = 480; });
    const auto h = hard.play (12000), f = soft.play (12000);
    CHECK (maxStep (h, 4000) > 0.3f);    // saut de phase au raccord
    CHECK (maxStep (f, 4000) < 0.12f);   // le sinus lui-même fait des pas d'environ 0,065 : le raccord est continu
}

TEST_CASE ("a loop never wraps back before the start marker")
{
    // Instrument construit depuis une description : début de lecture à 1000, boucle laissée par défaut (début 0) : le retour se fait au point START.
    const auto path = std::filesystem::temp_directory_path() / "neptune_loop_start.wav";
    {
        SF_INFO info {};
        info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
        REQUIRE (f != nullptr);
        std::vector<float> x (3000);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = i < 1000 ? -0.5f : 0.5f;   // avant START : -0,5 ; ensuite +0,5
        sf_writef_float (f, x.data(), static_cast<sf_count_t> (x.size()));
        sf_close (f);
    }
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = path.string();
    z.mode = PlayMode::Loop;
    z.startFrame = 1000;
    z.fadeInMs = 0.0;
    d.zones.push_back (z);
    std::vector<std::string> warnings;
    const auto inst = buildInstrument (d, &warnings);
    REQUIRE (inst != nullptr);
    CHECK (inst->zones[0].loopStart == 1000);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    std::vector<float> l (20000, 0.0f), r (l);
    for (int pos = 0; pos < 20000; pos += 256) v.render (l.data() + pos, r.data() + pos, std::min (256, 20000 - pos), 1.0f, 1.0f, 0.0);
    for (std::size_t i = 0; i < l.size(); ++i) REQUIRE (l[i] > 0.4f);   // jamais de retour sur la partie négative d'avant START
    std::filesystem::remove (path);
}

TEST_CASE ("zone description round-trips the new playback settings")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "a.wav";
    z.startFrame = 100; z.endFrame = 900; z.loopCrossfade = 50; z.fadeInMs = 5.0; z.fadeOutMs = 7.0; z.reverse = true; z.delayMs = 12.0;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    REQUIRE (back.zones.size() == 1);
    CHECK (back.zones[0] == sanitised (z));
    ZoneDescription bad;
    bad.startFrame = -5; bad.endFrame = 0; bad.fadeInMs = -1.0; bad.delayMs = 1e12;
    const auto clean = sanitised (bad);
    CHECK (clean.startFrame == 0);
    CHECK (clean.endFrame == -1);
    CHECK (clean.delayMs == ZoneDescription::maxDelayMs);
}

TEST_CASE ("several loops play in sequence")
{
    // boucle 1 : [1000, 2000) jouée trois fois (deux retours), puis la lecture continue jusqu'à la boucle 2 : [3000, 4000), sans fin
    Rig rig (makeRamp (8000), PlayMode::Loop, [] (Zone& z)
    {
        z.loopStart = 1000; z.loopEnd = 2000;
        z.moreLoops[0].start = 3000; z.moreLoops[0].end = 4000;
        z.moreLoopCount = 1;
        z.loopRepeats = 2;
    });
    const auto out = rig.play (8200);
    auto at = [&] (std::size_t i) { return static_cast<double> (out[i]) * 8000.0; };
    CHECK_THAT (at (1999), WithinAbs (1999.0, 1.0));
    CHECK_THAT (at (2000), WithinAbs (1000.0, 1.0));   // premier retour
    CHECK_THAT (at (3000), WithinAbs (1000.0, 1.0));   // second retour
    CHECK_THAT (at (3999), WithinAbs (1999.0, 1.0));
    CHECK_THAT (at (4000), WithinAbs (2000.0, 1.0));   // plus de retour : la lecture continue
    CHECK_THAT (at (5999), WithinAbs (3999.0, 1.0));
    CHECK_THAT (at (6000), WithinAbs (3000.0, 1.0));   // la dernière boucle revient sans fin
    CHECK_THAT (at (7999), WithinAbs (3999.0, 1.0));
    CHECK_THAT (at (8000), WithinAbs (3000.0, 1.0));
}

TEST_CASE ("a zone with a single loop still wraps forever")
{
    Rig rig (makeRamp (8000), PlayMode::Loop, [] (Zone& z) { z.loopStart = 1000; z.loopEnd = 2000; });
    const auto out = rig.play (6000);
    CHECK_THAT (static_cast<double> (out[5500]) * 8000.0, WithinAbs (1000.0 + std::fmod (5500.0 - 1000.0, 1000.0), 1.0));
}

TEST_CASE ("zone pan is a balance with unity at the centre")
{
    auto render = [] (float panL, float panR)
    {
        auto sample = makeConstant (4000, 0.5f);
        auto inst = std::make_shared<Instrument>();
        auto z = makeFullKeyboardZone (sample, 60, PlayMode::Loop);
        z.panL = panL; z.panR = panR;
        inst->zones.push_back (std::move (z));
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        v.noteOn (0, 60, 1.0f);
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        v.render (l.data(), r.data(), 512, 1.0f, 1.0f, 0.0);
        return std::make_pair (l[300], r[300]);
    };
    const auto centre = render (1.0f, 1.0f);
    CHECK_THAT (centre.first, WithinAbs (0.5, 1e-3));
    CHECK_THAT (centre.second, WithinAbs (0.5, 1e-3));
    const auto right = render (0.0f, 1.0f);
    CHECK_THAT (right.first, WithinAbs (0.0, 1e-4));
    CHECK_THAT (right.second, WithinAbs (0.5, 1e-3));
}

TEST_CASE ("extra loops and pan survive JSON and are built into the instrument")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-multiloop.wav";
    {
        SF_INFO info {};
        info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
        REQUIRE (f != nullptr);
        std::vector<float> data (10000, 0.3f);
        sf_writef_float (f, data.data(), static_cast<sf_count_t> (data.size()));
        sf_close (f);
    }
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = path.string();
    z.mode = PlayMode::Loop;
    z.loopStart = 1000; z.loopEnd = 2000; z.loopRepeats = 3;
    z.extraLoops = { { 3000, 4000, 100, 2 }, { 5000, 6000, 0, 0 }, { 5500, 5800, 0, 0 } };   // la troisième finit avant la deuxième : ignorée
    z.pan = 0.5;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    REQUIRE (back.zones.size() == 1);
    CHECK (back.zones[0] == sanitised (z));
    CHECK (back.zones[0].extraLoops.size() == 3);
    const auto inst = buildInstrument (back);
    REQUIRE (inst != nullptr);
    const auto& rt = inst->zones[0];
    CHECK (rt.moreLoopCount == 2);
    CHECK (rt.loopRepeats == 3);
    CHECK (rt.moreLoops[0].start == 3000);
    CHECK (rt.moreLoops[0].repeats == 2);
    CHECK (rt.moreLoops[1].repeats == 0);
    CHECK_THAT (rt.panL, WithinAbs (std::cos (0.25 * 3.14159265358979), 1e-4));
    CHECK (rt.panR == 1.0f);
    std::filesystem::remove (path);
}

TEST_CASE ("a release zone can follow the level of the interrupted voice")
{
    auto run = [] (bool follow, int meter, float attackAmp, bool)
    {
        auto inst = std::make_shared<Instrument>();
        auto attack = makeFullKeyboardZone (makeConstant (96000, attackAmp), 60, PlayMode::Loop);
        attack.panL = 1.0f; attack.panR = 0.0f;                       // l'attaque sort à gauche seulement
        auto rel = makeFullKeyboardZone (makeConstant (96000, 1.0f), 60, PlayMode::OneShot);
        rel.onRelease = true; rel.panL = 0.0f; rel.panR = 1.0f;       // le relâchement sort à droite seulement
        rel.releaseFollowsLevel = follow; rel.releaseMeter = meter;
        inst->zones.push_back (std::move (attack));
        inst->zones.push_back (std::move (rel));
        inst->finalize();
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        v.noteOn (0, 60, 1.0f);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        for (int pos = 0; pos < 24000; pos += 256) v.render (l.data() + pos, r.data() + pos, std::min (256, 24000 - pos), 1.0f, 1.0f, 0.0);
        v.noteOff (0, 60);
        std::fill (r.begin(), r.end(), 0.0f);
        v.render (l.data(), r.data(), 2048, 1.0f, 1.0f, 0.0);
        return r[1500];   // quelques ms après le Note Off, le relâchement sonne seul à droite
    };
    // sans option : le relâchement joue à plein volume
    CHECK_THAT (run (false, 0, 0.5f, true), WithinAbs (1.0, 0.02));
    // crête de 0,5 = -6,02 dB, plancher -48 dB : (-6,02 + 48) / 48 = 0,8746
    CHECK_THAT (run (true, 0, 0.5f, true), WithinAbs (0.8746, 0.02));
    // une attaque plus faible donne un relâchement plus faible
    CHECK (run (true, 0, 0.1f, true) < run (true, 0, 0.5f, true) - 0.2f);
    // RMS d'une constante 0,5 : -6,02 dB également ; LUFS : plus bas (la pondération K coupe le continu), donc un relâchement plus faible
    CHECK_THAT (run (true, 1, 0.5f, true), WithinAbs (0.8746, 0.03));
    CHECK (run (true, 2, 0.5f, true) < 0.8f);
}

TEST_CASE ("a release zone with nothing to follow stays silent")
{
    auto inst = std::make_shared<Instrument>();
    auto rel = makeFullKeyboardZone (makeConstant (96000, 1.0f), 60, PlayMode::OneShot);
    rel.onRelease = true; rel.releaseFollowsLevel = true;
    inst->zones.push_back (std::move (rel));
    inst->finalize();
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    v.noteOff (0, 60);
    std::vector<float> l (2048, 0.0f), r (l);
    v.render (l.data(), r.data(), 2048, 1.0f, 1.0f, 0.0);
    for (std::size_t i = 0; i < l.size(); ++i) REQUIRE (l[i] == 0.0f);
}

TEST_CASE ("release-level settings and the replace option survive JSON")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "r.wav"; z.onRelease = true; z.releaseFollowsLevel = true; z.releaseMeter = 1; z.releaseFloorDb = -40.0; z.releaseCurve = 2.0;
    d.zones.push_back (z);
    d.compensateOnReplace = false;
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.zones[0] == sanitised (z));
    CHECK_FALSE (back.compensateOnReplace);
    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).compensateOnReplace);
}
