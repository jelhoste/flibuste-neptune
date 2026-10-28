// Lecture d'une zone : région début / fin, fondu enchaîné de boucle, fondus d'entrée et de sortie, lecture inversée, délai de lecture.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
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
    CHECK (back.analysisMs == 100.0);
    d.analysisMs = 350.0;
    CHECK (instrumentFromJson (toJson (d)).analysisMs == 350.0);
    auto wild = toJson (d);
    wild = wild.substr (0, wild.rfind ('}')) + ",\"analysis_ms\":99999}";
    CHECK (instrumentFromJson (wild).analysisMs == 2000.0);
    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).compensateOnReplace);
}

TEST_CASE ("a level marker matches the release sample to the attack level")
{
    auto run = [] (float attackAmp, float releaseAmp, float offsetDb)
    {
        auto inst = std::make_shared<Instrument>();
        auto attack = makeFullKeyboardZone (makeConstant (96000, attackAmp), 60, PlayMode::Loop);
        attack.panL = 1.0f; attack.panR = 0.0f;
        auto rel = makeFullKeyboardZone (makeConstant (96000, releaseAmp), 60, PlayMode::OneShot);
        rel.onRelease = true; rel.panL = 0.0f; rel.panR = 1.0f;
        rel.releaseFollowsLevel = true; rel.releaseMeter = 0;
        rel.releaseRefDb = static_cast<float> (20.0 * std::log10 (releaseAmp));   // le marqueur tombe sur un son constant : son niveau de crête
        rel.releaseOffsetDb = offsetDb;
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
        return r[1500];
    };
    // attaque à 0,5 : le relâchement (0,25 au marqueur) est remonté pour sonner à 0,5
    CHECK_THAT (run (0.5f, 0.25f, 0.0f), WithinAbs (0.5, 0.02));
    // une attaque plus faible donne un relâchement plus faible, au même rapport
    CHECK_THAT (run (0.1f, 0.25f, 0.0f), WithinAbs (0.1, 0.01));
    // le décalage s'ajoute : +6,02 dB double le volume
    CHECK_THAT (run (0.25f, 0.25f, 6.0206f), WithinAbs (0.5, 0.02));
}

TEST_CASE ("level markers survive JSON")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "r.wav"; z.onRelease = true; z.releaseFollowsLevel = true; z.releaseRefFrame = 12345; z.releaseOffsetDb = -3.5;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.zones[0] == sanitised (z));
    CHECK (back.zones[0].releaseRefFrame == 12345);
}

TEST_CASE ("preview sources play the raw sample, the region, a point or a loop")
{
    auto run = [] (SamplerVoices::PreviewSpec spec, int frames)
    {
        auto inst = std::make_shared<Instrument>();
        auto z = makeFullKeyboardZone (makeRamp (8000), 60, PlayMode::Loop);
        z.startFrame = 1000; z.endFrame = 6000;
        z.loopStart = 2000; z.loopEnd = 3000;
        z.moreLoops[0].start = 4000; z.moreLoops[0].end = 5000; z.moreLoopCount = 1; z.loopRepeats = 1;
        z.sourceIndex = 0;
        inst->zones.push_back (std::move (z));
        inst->finalize();
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        v.previewZone (0, spec);
        std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (l);
        for (int pos = 0; pos < frames; pos += 256) v.render (l.data() + pos, r.data() + pos, std::min (256, frames - pos), 1.0f, 1.0f, 0.0);
        std::vector<double> out;
        for (const float x : l) out.push_back (static_cast<double> (x) * 8000.0 * 254.0 / 128.0);   // l'écoute joue à la vélocité moyenne de la zone
        return out;
    };
    using S = SamplerVoices::PreviewSpec::Source;
    {   // sample brut : du premier au dernier échantillon, une fois, sans boucle
        const auto o = run ({ S::Raw, -1, 0 }, 9000);
        CHECK_THAT (o[0], WithinAbs (0.0, 1.0));
        CHECK_THAT (o[2500], WithinAbs (2500.0, 1.0));   // pas de retour de boucle
        CHECK_THAT (o[7990], WithinAbs (7990.0, 1.0));
        CHECK (o[8500] == 0.0);                           // fini
    }
    {   // région : de START à END, une fois
        const auto o = run ({ S::Region, -1, 0 }, 7000);
        CHECK_THAT (o[0], WithinAbs (1000.0, 1.0));
        CHECK_THAT (o[2500], WithinAbs (3500.0, 1.0));
        CHECK_THAT (o[4990], WithinAbs (5990.0, 1.0));
        CHECK (o[6500] == 0.0);
    }
    {   // depuis un point
        const auto o = run ({ S::FromFrame, 3500, 0 }, 4000);
        CHECK_THAT (o[0], WithinAbs (3500.0, 1.0));
        CHECK_THAT (o[2000], WithinAbs (5500.0, 1.0));
    }
    {   // boucle 2, une seule fois
        const auto o = run ({ S::LoopOnce, -1, 1 }, 2500);
        CHECK_THAT (o[0], WithinAbs (4000.0, 1.0));
        CHECK_THAT (o[990], WithinAbs (4990.0, 1.0));
        CHECK (o[1500] == 0.0);
    }
    {   // boucle 1 telle qu'entendue : un retour, puis la lecture continue vers la boucle 2, qui tourne sans fin
        const auto o = run ({ S::LoopHeard, -1, 0 }, 9000);
        CHECK_THAT (o[0], WithinAbs (2000.0, 1.0));
        CHECK_THAT (o[999], WithinAbs (2999.0, 1.0));
        CHECK_THAT (o[1000], WithinAbs (2000.0, 1.0));    // le retour
        CHECK_THAT (o[2000], WithinAbs (3000.0, 1.0));    // plus de retour : on avance
        CHECK_THAT (o[3000], WithinAbs (4000.0, 1.0));
        CHECK_THAT (o[4000], WithinAbs (4000.0, 1.0));    // la dernière boucle revient sans fin
        CHECK_THAT (o[8999], WithinAbs (4000.0 + std::fmod (8999.0 - 3000.0, 1000.0), 1.0));
    }
}

TEST_CASE ("the file pool is saved with the instrument")
{
    InstrumentDescription d;
    d.pool = { "/samples/a.wav", "/samples/b é.flac" };
    ZoneDescription z; z.samplePath = "/samples/a.wav";
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.pool == d.pool);
    CHECK (back == d);
    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).pool.empty());
}

TEST_CASE ("velocity tracking and key tracking shape the volume of a zone")
{
    auto level = [] (float velTrack, float dbPerOct, int key, float velocity)
    {
        auto inst = std::make_shared<Instrument>();
        auto z = makeFullKeyboardZone (makeConstant (48000, 0.5f), 60, PlayMode::Loop);
        z.velTrack = velTrack; z.keyTrackDbPerOct = dbPerOct; z.keyTrackCenter = 60;
        inst->zones.push_back (std::move (z));
        inst->finalize();
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        v.noteOn (0, key, velocity);
        std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
        v.render (l.data(), r.data(), 1024, 1.0f, 1.0f, 0.0);
        return static_cast<double> (l[800]);
    };
    // 100 % (défaut) : le volume suit la vélocité comme avant
    CHECK_THAT (level (1.0f, 0.0f, 60, 1.0f), WithinAbs (0.5, 1e-3));
    CHECK_THAT (level (1.0f, 0.0f, 60, 0.5f), WithinAbs (0.25, 2e-3));
    // 0 % : volume constant
    CHECK_THAT (level (0.0f, 0.0f, 60, 0.2f), WithinAbs (0.5, 1e-3));
    CHECK_THAT (level (0.0f, 0.0f, 60, 1.0f), WithinAbs (0.5, 1e-3));
    // -100 % : plus fort quand on joue doucement (2 - vélocité, borné par la vélocité de la couche)
    CHECK (level (-1.0f, 0.0f, 60, 0.2f) > level (-1.0f, 0.0f, 60, 1.0f));
    // 50 % : à mi-chemin
    CHECK_THAT (level (0.5f, 0.0f, 60, 0.0f + 0.2f), WithinAbs (0.5 * (0.5 + 0.5 * 0.2), 2e-3));
    // clavier : +6 dB par octave autour du do central -> une octave plus haut, le double ; une octave plus bas, la moitié
    CHECK_THAT (level (0.0f, 6.0206f, 72, 1.0f), WithinAbs (1.0, 5e-3));
    CHECK_THAT (level (0.0f, 6.0206f, 48, 1.0f), WithinAbs (0.25, 3e-3));
    CHECK_THAT (level (0.0f, 6.0206f, 60, 1.0f), WithinAbs (0.5, 1e-3));
}

TEST_CASE ("a zone can require a controller value at the moment the note starts")
{
    auto inst = std::make_shared<Instrument>();
    // même place, deux zones : l'une ne joue que pédale enfoncée (CC 64 >= 64), l'autre pédale levée
    midi::FilterDescription down, up;
    down.enabled = up.enabled = true;
    midi::Rule leaf;
    leaf.op = midi::Op::Leaf; leaf.condition.kind = midi::MsgKind::ControlChange; leaf.condition.lo1 = leaf.condition.hi1 = 64;
    leaf.condition.lo2 = 64; leaf.condition.hi2 = 127;
    down.root.children = { leaf };
    leaf.condition.lo2 = 0; leaf.condition.hi2 = 63;
    up.root.children = { leaf };
    auto zA = makeFullKeyboardZone (makeConstant (48000, 0.8f), 60, PlayMode::Loop);
    auto zB = makeFullKeyboardZone (makeConstant (48000, 0.2f), 60, PlayMode::Loop);
    zA.midiCond = std::make_shared<const midi::CompiledFilter> (down);
    zB.midiCond = std::make_shared<const midi::CompiledFilter> (up);
    inst->zones.push_back (std::move (zA));
    inst->zones.push_back (std::move (zB));
    inst->finalize();
    midi::State state;
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.setMidiState (&state);
    auto play = [&] ()
    {
        v.noteOn (0, 60, 1.0f);
        std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
        v.render (l.data(), r.data(), 1024, 1.0f, 1.0f, 0.0);
        v.panic();
        std::vector<float> t (4096, 0.0f), u (4096, 0.0f);
        v.render (t.data(), u.data(), 4096, 1.0f, 1.0f, 0.0);   // laisse finir le fondu de coupure
        return static_cast<double> (l[800]);
    };
    midi::Message cc; cc.kind = midi::MsgKind::ControlChange; cc.channel = 0; cc.data1 = 64;
    cc.data2 = 0; state.update (cc);
    for (int i = 0; i < 4; ++i) CHECK_THAT (play(), WithinAbs (0.2, 5e-3));   // pédale levée : toujours la zone B, malgré le round-robin
    cc.data2 = 127; state.update (cc);
    for (int i = 0; i < 4; ++i) CHECK_THAT (play(), WithinAbs (0.8, 5e-3));
    // sans état fourni, une condition sur un contrôleur est jugée sur l'état vide (pédale levée)
    v.setMidiState (nullptr);
    CHECK_THAT (play(), WithinAbs (0.2, 5e-3));
}

TEST_CASE ("tracking and MIDI condition settings survive JSON")
{
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = "a.wav";
    z.velTrackPct = -40.0; z.keyTrackDbPerOct = 3.5; z.keyTrackCenter = 48;
    z.midiCondition.enabled = true;
    midi::Rule leaf;
    leaf.op = midi::Op::Leaf; leaf.condition.kind = midi::MsgKind::ControlChange; leaf.condition.lo1 = leaf.condition.hi1 = 64; leaf.condition.lo2 = 64;
    z.midiCondition.root.children = { leaf };
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.zones[0] == sanitised (z));
    CHECK (back.zones[0].midiCondition.enabled);
    CHECK (back.zones[0].keyTrackCenter == 48);
    const auto built = ZoneDescription {};
    CHECK (built.velTrackPct == 100.0);
}

TEST_CASE ("humanize varies pitch, volume and start within the set amounts, and zones can opt out")
{
    auto make = [] (float amount)
    {
        auto inst = std::make_shared<Instrument>();
        auto z = makeFullKeyboardZone (makeRamp (96000), 60, PlayMode::Loop);
        z.humanizeAmount = amount;
        inst->zones.push_back (std::move (z));
        inst->humanizePitchCents = 50.0f; inst->humanizeVolumeDb = 6.0f; inst->humanizeTimingMs = 30.0f;
        inst->finalize();
        return inst;
    };
    struct Take { double ratio, level; int start; };
    auto takes = [&] (float amount)
    {
        auto inst = make (amount);
        std::vector<Take> out;
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        for (int n = 0; n < 40; ++n)
        {
            v.noteOn (0, 60, 1.0f);
            std::vector<float> l (3000, 0.0f), r (3000, 0.0f);
            v.render (l.data(), r.data(), 3000, 1.0f, 1.0f, 0.0);
            int start = 0;
            while (start < 3000 && l[static_cast<std::size_t> (start)] == 0.0f) ++start;
            // rampe : valeur = position / 96000 ; position = (indice - départ) * rapport
            const double pos = static_cast<double> (l[2500]) * 96000.0;
            out.push_back ({ 0.0, 0.0, start });
            out.back().level = pos;   // mémorise la valeur lue (amplitude et position mêlées)
            v.panic();
            std::vector<float> t (4096, 0.0f), u (4096, 0.0f);
            v.render (t.data(), u.data(), 4096, 1.0f, 1.0f, 0.0);
        }
        return out;
    };
    const auto human = takes (1.0f);
    int minStart = 99999, maxStart = 0;
    double minValue = 1e9, maxValue = 0.0;
    for (const auto& t : human) { minStart = std::min (minStart, t.start); maxStart = std::max (maxStart, t.start); minValue = std::min (minValue, t.level); maxValue = std::max (maxValue, t.level); }
    CHECK (maxStart - minStart > 200);          // des départs différents
    CHECK (maxStart <= 1441);                   // jamais plus de 30 ms
    CHECK (maxValue > minValue * 1.15);         // des valeurs différentes (volume, hauteur et départ mêlés)
    // une zone à 0 % ne bouge pas : toutes les prises sont identiques
    const auto steady = takes (0.0f);
    for (const auto& t : steady) { CHECK (t.start == steady.front().start); CHECK_THAT (t.level, WithinAbs (steady.front().level, 1e-3)); }
}

TEST_CASE ("humanize settings survive JSON and are bounded")
{
    InstrumentDescription d;
    d.humanizePitchCents = 12.5; d.humanizeVolumeDb = 2.0; d.humanizeTimingMs = 8.0;
    ZoneDescription z; z.samplePath = "a.wav"; z.humanizePct = 40.0;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back == d);
    CHECK (back.zones[0].humanizePct == 40.0);
    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).humanizePitchCents == 0.0);
    ZoneDescription bad; bad.humanizePct = 500.0;
    CHECK (sanitised (bad).humanizePct == 100.0);
}

TEST_CASE ("envelope and filters are inherited instrument -> group -> zone, block by block")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-inherit.wav";
    {
        SF_INFO info {};
        info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
        REQUIRE (f != nullptr);
        std::vector<float> data (4800, 0.3f);
        sf_writef_float (f, data.data(), static_cast<sf_count_t> (data.size()));
        sf_close (f);
    }
    auto envWithAttack = [] (double ms) { EnvelopeDescription e; e.attack.on = true; e.attack.ms = ms; return e; };
    FilterBlock lowpass;
    lowpass.filter[0].on = true;
    InstrumentDescription d;
    d.defaultEnvelope = envWithAttack (100.0);
    GroupSettings a;
    a.envelopeOverride = true; a.envelope = envWithAttack (200.0);
    a.filterOverride = true; a.filters = lowpass;
    d.groups["A"] = a;
    auto zone = [&] (const char* group, bool own) { ZoneDescription z; z.samplePath = path.string(); z.group = group; if (own) { z.envelopeOverride = true; z.envelope = envWithAttack (300.0); } return z; };
    d.zones = { zone ("", false), zone ("A", false), zone ("A", true), zone ("B", false) };
    const auto inst = buildInstrument (d);
    REQUIRE (inst != nullptr);
    auto attackMs = [&] (std::size_t i) { return EnvelopeTiming::of (inst->zones[i].env).attackSec * 1000.0; };
    CHECK_THAT (attackMs (0), WithinAbs (100.0, 1e-6));   // sans Groupe : l'instrument
    CHECK_THAT (attackMs (1), WithinAbs (200.0, 1e-6));   // Groupe A personnalisé
    CHECK_THAT (attackMs (2), WithinAbs (300.0, 1e-6));   // la zone se détache
    CHECK_THAT (attackMs (3), WithinAbs (100.0, 1e-6));   // Groupe B sans réglage propre : l'instrument
    // filtres : seul le Groupe A a les siens (les autres n'ont aucun filtre actif : instrument sans filtre)
    CHECK (inst->zones[1].filters != nullptr);
    CHECK (inst->zones[3].filters == nullptr);
    CHECK (inst->zones[0].filters == nullptr);
    // une zone qui personnalise ses filtres l'emporte sur le Groupe
    d.zones[1].filterOverride = true;
    const auto inst2 = buildInstrument (d);
    CHECK (inst2->zones[1].filters == nullptr);   // ses filtres à elle : aucun actif

    const auto back = instrumentFromJson (toJson (d));
    CHECK (back == d);
    CHECK (back.groups.count ("A") == 1);
    CHECK (back.defaultEnvelope.attack.ms == 100.0);
    // projets d'avant l'héritage : une zone qui porte son enveloppe la garde (personnalisée), une zone neuve suit l'instrument
    const auto legacy = instrumentFromJson (R"({"zones":[{"sample":"a.wav","envelope":{"attack":{"on":true,"ms":50.0}}},{"sample":"b.wav"}]})");
    REQUIRE (legacy.zones.size() == 2);
    CHECK (legacy.zones[0].envelopeOverride);
    CHECK_FALSE (legacy.zones[1].envelopeOverride);
    std::filesystem::remove (path);
}

TEST_CASE ("round-robin: each velocity layer has its own counter, and a short layer can borrow from its neighbour")
{
    auto build = [] (bool borrow, int modeA)
    {
        auto inst = std::make_shared<Instrument>();
        auto make = [] (int velLow, int velHigh, int rr, float level)
        {
            auto z = makeFullKeyboardZone (makeConstant (4800, level), 60, PlayMode::OneShot);
            z.keyLow = z.keyHigh = 60; z.velLow = velLow; z.velHigh = velHigh; z.roundRobin = rr;
            return z;
        };
        // couche douce (1-60) : 2 alternatives ; couche forte (61-127) : 4 alternatives
        for (int i = 0; i < 2; ++i) inst->zones.push_back (make (1, 60, i + 1, 0.1f + 0.01f * static_cast<float> (i)));
        for (int i = 0; i < 4; ++i) inst->zones.push_back (make (61, 127, i + 1, 0.5f + 0.01f * static_cast<float> (i)));
        inst->zones[0].rrMode = modeA;
        inst->rrBorrow = borrow;
        inst->finalize();
        return inst;
    };
    auto picks = [] (const std::shared_ptr<Instrument>& inst, float velocity, int count)
    {
        std::vector<const Zone*> out;
        for (int i = 0; i < count; ++i)
        {
            Instrument::Pick p[Instrument::maxPicks];
            const int n = inst->selectLayers (60, velocity, false, p, Instrument::maxPicks);
            if (n > 0) out.push_back (p[0].zone);
        }
        return out;
    };
    auto distinct = [] (const std::vector<const Zone*>& v) { std::set<const Zone*> s (v.begin(), v.end()); return s.size(); };
    // sans emprunt : 2 alternatives douces, 4 fortes, compteurs indépendants
    {
        auto inst = build (false, -1);
        CHECK (distinct (picks (inst, 0.3f, 8)) == 2);
        CHECK (distinct (picks (inst, 0.9f, 8)) == 4);
        // jouer une couche n'avance pas le compteur de l'autre
        const auto a1 = picks (inst, 0.3f, 1)[0];
        picks (inst, 0.9f, 3);
        const auto a2 = picks (inst, 0.3f, 1)[0];
        CHECK (a1 != a2);   // la couche douce a continué sa propre alternance
    }
    // avec emprunt : la couche douce utilise aussi les alternatives de la voisine, corrigées en volume
    {
        auto inst = build (true, -1);
        CHECK (distinct (picks (inst, 0.3f, 12)) == 4);
        for (int i = 0; i < 12; ++i)
        {
            Instrument::Pick p[Instrument::maxPicks];
            if (inst->selectLayers (60, 0.3f, false, p, Instrument::maxPicks) < 1) continue;
            const bool borrowed = p[0].zone->velLow == 61;
            const float g = p[0].gain;
            if (borrowed) CHECK_THAT (static_cast<double> (g), WithinAbs (30.5 / 94.0, 0.05));   // vélocités moyennes : 30,5 contre 94
        }
        CHECK (distinct (picks (inst, 0.9f, 12)) == 4);   // la couche forte n'emprunte rien à la douce (moins d'alternatives)
    }
    // mode par jeu : aléatoire pour la couche douce (la première zone du jeu), séquentiel ailleurs
    {
        auto inst = build (false, 1);
        const auto soft = picks (inst, 0.3f, 40);
        int repeats = 0;
        for (std::size_t i = 1; i < soft.size(); ++i) repeats += soft[i] == soft[i - 1] ? 1 : 0;
        CHECK (repeats > 0);   // un tirage au sort répète parfois, l'alternance séquentielle jamais
        const auto loud = picks (inst, 0.9f, 8);
        for (std::size_t i = 1; i < loud.size(); ++i) CHECK (loud[i] != loud[i - 1]);
    }
}

TEST_CASE ("round-robin settings survive JSON")
{
    InstrumentDescription d;
    d.rrBorrow = true;
    ZoneDescription z; z.samplePath = "a.wav"; z.rrMode = 2;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back == d);
    CHECK (back.zones[0].rrMode == 2);
    CHECK_FALSE (instrumentFromJson (toJson (InstrumentDescription {})).rrBorrow);
    ZoneDescription bad; bad.rrMode = 9;
    CHECK (sanitised (bad).rrMode == 2);
}

TEST_CASE ("modulators gate groups, zones and the whole track, discrete or continuous, and keyswitches flip them")
{
    auto build = [] (std::uint16_t zoneMaskA, std::uint16_t zoneMaskB, bool continuousFirst)
    {
        auto inst = std::make_shared<Instrument>();
        auto a = makeFullKeyboardZone (makeConstant (48000, 0.4f), 60, PlayMode::Loop);
        auto b = makeFullKeyboardZone (makeConstant (48000, 0.2f), 60, PlayMode::Loop);
        a.modMask = zoneMaskA; b.modMask = zoneMaskB;
        a.panL = 1.0f; a.panR = 0.0f; b.panL = 0.0f; b.panR = 1.0f;   // A à gauche, B à droite : on les mesure séparément
        inst->zones.push_back (std::move (a));
        inst->zones.push_back (std::move (b));
        inst->modulators[0].continuous = continuousFirst;
        inst->modulators[1].used = true; inst->modulators[1].note = 24;
        inst->finalize();
        return inst;
    };
    auto play = [] (SamplerVoices& v, int note, int frames = 2048)
    {
        v.noteOn (0, note, 1.0f);
        std::vector<float> l (static_cast<std::size_t> (frames), 0.0f), r (l);
        v.render (l.data(), r.data(), frames, 1.0f, 1.0f, 0.0);
        return std::make_pair (static_cast<double> (l[static_cast<std::size_t> (frames) - 10]), static_cast<double> (r[static_cast<std::size_t> (frames) - 10]));
    };
    {
        // deux zones à la même place, A commandée par le modulateur 0, B par le modulateur 1 : un modulateur coupé les écarte du déclenchement
        auto inst = build (1u << 0, 1u << 1, false);
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        float values[kModulators];
        std::fill (values, values + kModulators, 1.0f);
        v.setModulators (values);
        auto both = play (v, 60);
        CHECK_THAT (both.first, WithinAbs (0.4, 5e-3));   // A et B jouent ensemble (même famille, jeux distincts ? ici, même place : alternatives)
        v.panic();
        std::vector<float> t (4096, 0.0f), u (4096, 0.0f);
        v.render (t.data(), u.data(), 4096, 1.0f, 1.0f, 0.0);
        values[0] = 0.0f; v.setModulators (values);       // modulateur 0 coupé : A ne se déclenche plus
        const auto onlyB = play (v, 60);
        CHECK_THAT (onlyB.first, WithinAbs (0.0, 1e-4));
        CHECK_THAT (onlyB.second, WithinAbs (0.2, 5e-3));
        // un modulateur coupé en cours de note fait disparaître le son en fondu (le volume suit en direct) ; remis, la note tenue reprend
        std::vector<float> l (2048), r (2048);
        auto block = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); v.render (l.data(), r.data(), 2048, 1.0f, 1.0f, 0.0); };
        values[1] = 0.0f; v.setModulators (values);
        block();   // le bloc de la rampe de fondu
        CHECK (r[2000] < r[10]);
        block();
        CHECK_THAT (static_cast<double> (r[2000]), WithinAbs (0.0, 1e-4));
        values[1] = 1.0f; v.setModulators (values);
        block();
        block();
        CHECK_THAT (static_cast<double> (r[2000]), WithinAbs (0.2, 5e-3));
    }
    {
        // modulateur continu : le volume suit la valeur
        auto inst = build (1u << 0, 0, true);
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        float values[kModulators];
        std::fill (values, values + kModulators, 1.0f);
        values[0] = 0.25f;
        v.setModulators (values);
        const auto quarter = play (v, 60);
        CHECK_THAT (quarter.first, WithinAbs (0.4 * 0.25, 5e-3));
    }
    {
        // la touche 24 est une touche de keyswitch ; pas la 25 ; le canal compte
        auto inst = build (0, 0, false);
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        CHECK (v.isKeyswitch (0, 24));
        CHECK_FALSE (v.isKeyswitch (0, 25));
    }
}

TEST_CASE ("modulator settings and masks survive JSON and reach the zones")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-mods.wav";
    {
        SF_INFO info {};
        info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
        REQUIRE (f != nullptr);
        std::vector<float> data (4800, 0.3f);
        sf_writef_float (f, data.data(), static_cast<sf_count_t> (data.size()));
        sf_close (f);
    }
    InstrumentDescription d;
    d.modulators[2].name = "Flute 8";
    d.modulators[2].momentary = true; d.modulators[2].continuous = true;
    d.modulators[2].keyswitchNote = 30; d.modulators[2].keyswitchChannel = 3; d.modulators[2].radio = 2;
    d.trackModulators = 1u << 5;
    GroupSettings g; g.modulators = 1u << 1;
    d.groups["stops"] = g;
    ZoneDescription z; z.samplePath = path.string(); z.group = "stops"; z.modulators = 1u << 2;
    d.zones.push_back (z);
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back == d);
    CHECK (back.modulators[2].name == "Flute 8");
    CHECK (back.groups.count ("stops") == 1);
    const auto inst = buildInstrument (back);
    REQUIRE (inst != nullptr);
    CHECK (inst->zones[0].modMask == ((1u << 5) | (1u << 1) | (1u << 2)));   // piste + Groupe + zone
    CHECK (inst->modulators[2].used);
    CHECK (inst->modulators[2].note == 30);
    CHECK (inst->modulators[2].continuous);
    std::filesystem::remove (path);
}

#include "neptune/core/Engine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

TEST_CASE ("engine: a keyswitch note flips its modulator instead of playing, latch / momentary / exclusive group")
{
    ParameterSet params;
    REQUIRE (sampler::addParams (params));
    REQUIRE (probe::buildPool (params, mixer::numTracks));
    REQUIRE (mixer::addParams (params, mixer::numTracks, mixer::numBuses));
    Engine engine;
    engine.attachParameters (&params);
    engine.prepare (48000.0, 256);
    std::vector<std::pair<ParamId, double>> writes;
    engine.setModulatorWriter ([&] (ParamId id, double v) { writes.emplace_back (id, v); params.setAt (params.indexOf (id), v); });

    auto inst = std::make_shared<Instrument>();
    auto z = makeFullKeyboardZone (makeConstant (48000, 0.5f), 60, PlayMode::Loop);
    inst->zones.push_back (std::move (z));
    auto& m0 = inst->modulators[0]; m0.used = true; m0.note = 20; m0.radio = 1;                        // bascule, exclusif
    auto& m1 = inst->modulators[1]; m1.used = true; m1.note = 21; m1.radio = 1;
    auto& m2 = inst->modulators[2]; m2.used = true; m2.note = 22; m2.momentary = true;                 // momentané
    inst->finalize();
    engine.publishInstrument (0, inst);
    std::vector<std::vector<float>> bufs (4, std::vector<float> (256, 0.0f));
    std::vector<float*> ptrs;
    for (auto& b : bufs) ptrs.push_back (b.data());
    engine.process (ptrs.data(), 4, 256);   // adopte l'instrument

    const auto id = [] (int i) { return mixer::modulatorParam (0, i); };
    CHECK (params.get (id (0)) == 100.0);   // actifs par défaut
    engine.noteOn (0, 20, 1.0f);
    engine.noteOff (0, 20);
    CHECK (params.get (id (0)) == 0.0);     // bascule : éteint
    CHECK (engine.activeVoices() == 0);     // et aucun son
    engine.noteOn (0, 20, 1.0f);
    CHECK (params.get (id (0)) == 100.0);   // rallumé
    CHECK (params.get (id (1)) == 0.0);     // ... et, du même groupe d'exclusion, le 1 s'éteint
    engine.noteOn (0, 21, 1.0f);
    CHECK (params.get (id (1)) == 100.0);   // allumer le 1 éteint le 0
    CHECK (params.get (id (0)) == 0.0);
    engine.noteOn (0, 22, 1.0f);
    CHECK (params.get (id (2)) == 100.0);   // momentané : actif tant que la touche est tenue
    engine.noteOff (0, 22);
    CHECK (params.get (id (2)) == 0.0);
    CHECK (engine.activeVoices() == 0);
    engine.noteOn (0, 60, 1.0f);            // une vraie note joue toujours
    CHECK (engine.activeVoices() == 1);
}

#include "neptune/core/PreviewSample.h"

TEST_CASE ("a free preview plays a sample that belongs to no zone, once or in a loop, with or without an instrument")
{
    auto sample = makeRamp (6000);
    SamplerVoices v;
    v.prepare (48000.0);
    // sans instrument du tout : l'écoute libre joue quand même
    {
        const auto p = makePreviewSample (sample, 60, false);
        REQUIRE (p != nullptr);
        v.previewObject (*p);
        std::vector<float> l (8000, 0.0f), r (l);
        v.render (l.data(), r.data(), 8000, 1.0f, 1.0f, 0.0);
        CHECK_THAT (static_cast<double> (l[3000]) * 6000.0 * 254.0 / 254.0 / 0.8, WithinAbs (3000.0, 8.0));   // rampe lue à la vélocité 0,8
        CHECK (l[7000] == 0.0f);   // un coup : fini
    }
    // en boucle sur [1000, 2000)
    {
        v.panic();
        std::vector<float> flush (4096, 0.0f), flush2 (4096, 0.0f);
        v.render (flush.data(), flush2.data(), 4096, 1.0f, 1.0f, 0.0);
        const auto p = makePreviewSample (sample, 60, true, 1000, 2000);
        v.previewObject (*p);
        std::vector<float> l (9000, 0.0f), r (l);
        v.render (l.data(), r.data(), 9000, 1.0f, 1.0f, 0.0);
        for (const int i : { 1200, 4200, 8800 })
        {
            const double pos = static_cast<double> (l[static_cast<std::size_t> (i)]) * 6000.0 / 0.8;
            CHECK (pos >= 995.0);
            CHECK (pos <= 2005.0);   // la lecture reste dans la boucle (après la première passe de 0 à 1000)
        }
    }
    CHECK (makePreviewSample (nullptr) == nullptr);
}

TEST_CASE ("release zones are chosen by how long the key was held")
{
    // trois bruits de relâchement pour la même touche : court (jusqu'à 300 ms), moyen (301 ms à 1,5 s), long (plus de 1,5 s)
    auto run = [] (int holdMs)
    {
        auto inst = std::make_shared<Instrument>();
        auto attack = makeFullKeyboardZone (makeConstant (96000, 0.0f), 60, PlayMode::Loop);
        inst->zones.push_back (std::move (attack));
        const struct { float level; int minMs, maxMs; } releases[] = { { 0.2f, 0, 300 }, { 0.5f, 301, 1500 }, { 0.9f, 1501, 0 } };
        for (const auto& r : releases)
        {
            auto z = makeFullKeyboardZone (makeConstant (96000, r.level), 60, PlayMode::OneShot);
            z.onRelease = true; z.panL = 0.0f; z.panR = 1.0f;   // le relâchement sort à droite seulement
            z.heldMinMs = r.minMs; z.heldMaxMs = r.maxMs;
            inst->zones.push_back (std::move (z));
        }
        inst->finalize();
        SamplerVoices v;
        v.prepare (48000.0);
        v.setInstrument (inst.get());
        v.noteOn (0, 60, 1.0f);
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        for (int done = 0; done < holdMs * 48; done += 256)
        {
            const int n = std::min (256, holdMs * 48 - done);
            if (n > 0) v.render (l.data(), r.data(), n, 1.0f, 1.0f, 0.0);
        }
        v.noteOff (0, 60);
        std::fill (r.begin(), r.end(), 0.0f);
        v.render (l.data(), r.data(), 2048, 1.0f, 1.0f, 0.0);
        return r[1500];
    };
    CHECK_THAT (run (100), WithinAbs (0.2, 0.02));
    CHECK_THAT (run (299), WithinAbs (0.2, 0.02));
    CHECK_THAT (run (800), WithinAbs (0.5, 0.02));
    CHECK_THAT (run (1500), WithinAbs (0.5, 0.02));
    CHECK_THAT (run (3000), WithinAbs (0.9, 0.02));
    CHECK_THAT (run (20000), WithinAbs (0.9, 0.02));
}

TEST_CASE ("a release zone with a held-time window that no press fits stays silent, and the window survives JSON")
{
    auto inst = std::make_shared<Instrument>();
    inst->zones.push_back (makeFullKeyboardZone (makeConstant (96000, 0.0f), 60, PlayMode::Loop));
    auto z = makeFullKeyboardZone (makeConstant (96000, 1.0f), 60, PlayMode::OneShot);
    z.onRelease = true; z.panL = 0.0f; z.panR = 1.0f; z.heldMinMs = 2000;   // seulement après 2 s d'appui
    inst->zones.push_back (std::move (z));
    inst->finalize();
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
    for (int done = 0; done < 24000; done += 256) v.render (l.data(), r.data(), 256, 1.0f, 1.0f, 0.0);   // 0,5 s
    v.noteOff (0, 60);
    std::fill (r.begin(), r.end(), 0.0f);
    v.render (l.data(), r.data(), 2048, 1.0f, 1.0f, 0.0);
    CHECK_THAT (r[1500], WithinAbs (0.0, 1e-6));

    InstrumentDescription d;
    ZoneDescription rel;
    rel.samplePath = "/x/rel.wav";
    rel.onRelease = true;
    rel.heldMinMs = 200;
    rel.heldMaxMs = 1000;
    ZoneDescription attackZone;
    attackZone.samplePath = "/x/att.wav";
    attackZone.heldMinMs = 500;   // sans effet sur une zone d'attaque
    ZoneDescription swapped = rel;
    swapped.heldMinMs = 900;
    swapped.heldMaxMs = 100;      // fenêtre à l'envers : la borne haute remonte au minimum
    d.zones = { rel, attackZone, swapped };
    const auto back = instrumentFromJson (toJson (d));
    REQUIRE (back.zones.size() == 3);
    CHECK (back.zones[0].heldMinMs == 200);
    CHECK (back.zones[0].heldMaxMs == 1000);
    CHECK (back.zones[1].heldMinMs == 0);
    CHECK (back.zones[1].heldMaxMs == 0);
    CHECK (back.zones[2].heldMinMs == 900);
    CHECK (back.zones[2].heldMaxMs == 900);
    CHECK (instrumentFromJson ("{\"zones\":[{\"sample\":\"/a.wav\",\"on_release\":true,\"held_min_ms\":99999999}]}").zones[0].heldMinMs == maxHeldMs);
}

TEST_CASE ("a looped zone with a recorded release tail plays the tail from the marker after Note Off")
{
    // un fichier de 2 s : la partie tenue (0,3) jusqu'à 1 s, puis la queue enregistrée (0,8) de 1 s à 2 s
    const auto path = std::filesystem::temp_directory_path() / "neptune-release-tail.wav";
    {
        SF_INFO info {};
        info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
        REQUIRE (f != nullptr);
        std::vector<float> data (96000, 0.3f);
        std::fill (data.begin() + 48000, data.end(), 0.8f);
        sf_writef_float (f, data.data(), static_cast<sf_count_t> (data.size()));
        sf_close (f);
    }
    InstrumentDescription d;
    ZoneDescription z;
    z.samplePath = path.string();
    z.mode = PlayMode::Loop;
    z.rootNote = 60;
    z.loopStart = 1000; z.loopEnd = 40000;
    z.releaseStart = 48000;
    d.zones.push_back (z);
    CHECK (instrumentFromJson (toJson (d)).zones[0].releaseStart == 48000);   // la queue passe par le JSON
    const auto inst = buildInstrument (d);
    REQUIRE (inst != nullptr);
    REQUIRE (inst->zones.size() == 2);                                         // la zone en boucle + sa zone de relâchement implicite
    CHECK (inst->zones[0].hasTail);
    CHECK (inst->zones[1].onRelease);
    CHECK (inst->zones[1].startFrame == 48000);

    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (inst.get());
    v.noteOn (0, 60, 1.0f);
    std::vector<float> l (96000, 0.0f), r (96000, 0.0f);
    int pos = 0;
    auto run = [&] (int frames) { for (int done = 0; done < frames; done += 256) { const int n = std::min (256, frames - done); v.render (l.data() + pos, r.data() + pos, n, 1.0f, 1.0f, 0.0); pos += n; } };
    run (24000);                                                               // 0,5 s tenue : la boucle sonne
    CHECK_THAT (l[20000], WithinAbs (0.3, 0.02));
    v.noteOff (0, 60);
    run (4800);                                                                // 0,1 s après : la queue (0,8) a pris le relais, la boucle est éteinte
    CHECK_THAT (l[24000 + 4000], WithinAbs (0.8, 0.02));
    run (24000);
    CHECK_THAT (l[24000 + 20000], WithinAbs (0.8, 0.02));                      // la queue joue encore (elle dure 1 s)
    run (30000);                                                               // au-delà de la fin du fichier : silence, la voix est terminée
    CHECK_THAT (l[pos - 100], WithinAbs (0.0, 1e-4));
    CHECK (v.activeVoices() == 0);

    // une zone sans queue garde le relâchement par l'enveloppe, et sans « charger les relâchements » il n'y a pas de zone de queue
    d.zones[0].releaseStart = -1;
    CHECK (buildInstrument (d)->zones.size() == 1);
    d.zones[0].releaseStart = 48000;
    d.loadReleaseSamples = false;
    CHECK (buildInstrument (d)->zones.size() == 1);
    std::filesystem::remove (path);
}

TEST_CASE ("organ modulators: a box keeps its floor level, a tremulant makes the volume oscillate only while its gate is on")
{
    auto renderBlocks = [] (Rig& rig, std::vector<float>& out, int blocks)
    {
        std::vector<float> l (256), r (256);
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            rig.voices.render (l.data(), r.data(), 256, 1.0f, 1.0f, 0.0);
            out.insert (out.end(), l.begin(), l.end());
        }
    };
    auto tail = [] (const std::vector<float>& v, std::size_t n) { return std::vector<float> (v.end() - static_cast<std::ptrdiff_t> (n), v.end()); };
    SECTION ("expression box: closed = the floor, open = full")
    {
        Rig rig (makeConstant (480000, 1.0f), PlayMode::OneShot, [] (Zone& z) { z.modMask = 1u; });
        rig.inst->modulators[0].continuous = true;
        rig.inst->modulators[0].floorLevel = 0.25f;
        float closed[kModulators], open[kModulators];
        for (int i = 0; i < kModulators; ++i) closed[i] = open[i] = 1.0f;
        closed[0] = 0.0f;
        rig.voices.setModulators (closed);
        std::vector<float> out;
        renderBlocks (rig, out, 1);
        rig.voices.noteOn (0, 60, 1.0f);
        renderBlocks (rig, out, 20);
        CHECK_THAT (out.back(), WithinAbs (0.25, 0.01));
        rig.voices.setModulators (open);
        renderBlocks (rig, out, 20);
        CHECK_THAT (out.back(), WithinAbs (1.0, 0.01));
    }
    SECTION ("tremulant")
    {
        Rig rig (makeConstant (480000, 1.0f), PlayMode::OneShot, [] (Zone& z) { z.modMask = 2u; });
        rig.inst->modulators[1].continuous = true;
        rig.inst->modulators[1].tremPeriodMs = 100.0;
        rig.inst->modulators[1].tremDepth = 0.5f;
        rig.inst->modulators[1].tremGate = 2;
        float off[kModulators], on[kModulators];
        for (int i = 0; i < kModulators; ++i) off[i] = on[i] = 1.0f;
        off[2] = 0.0f;
        off[1] = 0.0f;   // la valeur réglée du tremblant ne coupe pas le son
        rig.voices.setModulators (off);
        std::vector<float> out;
        renderBlocks (rig, out, 1);
        rig.voices.noteOn (0, 60, 1.0f);
        renderBlocks (rig, out, 40);
        const auto still = tail (out, 4800);
        CHECK (*std::min_element (still.begin(), still.end()) > 0.99f);   // interrupteur coupé : pas d'oscillation
        rig.voices.setModulators (on);
        out.clear();
        renderBlocks (rig, out, 60);
        const auto moving = tail (out, 4800);
        CHECK (*std::min_element (moving.begin(), moving.end()) < 0.65f);
        CHECK (*std::max_element (moving.begin(), moving.end()) > 0.95f);
    }
    SECTION ("tremulant speed: 50 % is the organ's period, 100 % is four times faster")
    {
        auto crossings = [&] (float speed)
        {
            Rig rig (makeConstant (480000, 1.0f), PlayMode::OneShot, [] (Zone& z) { z.modMask = 2u; });
            rig.inst->modulators[1].continuous = true;
            rig.inst->modulators[1].tremPeriodMs = 100.0;
            rig.inst->modulators[1].tremDepth = 0.5f;
            rig.inst->modulators[1].tremSpeed = 3;
            float v[kModulators];
            for (int i = 0; i < kModulators; ++i) v[i] = 1.0f;
            v[3] = speed;
            rig.voices.setModulators (v);
            std::vector<float> out;
            std::vector<float> l (256), r (256);
            auto block = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); rig.voices.render (l.data(), r.data(), 256, 1.0f, 1.0f, 0.0); out.insert (out.end(), l.begin(), l.end()); };
            block();
            rig.voices.noteOn (0, 60, 1.0f);
            for (int b = 0; b < 40; ++b) block();
            out.clear();
            for (int b = 0; b < 75; ++b) block();   // 0,4 s
            int n = 0;
            for (std::size_t i = 1; i < out.size(); ++i) if ((out[i - 1] < 0.75f) != (out[i] < 0.75f)) ++n;
            return n;
        };
        const int normal = crossings (0.5f), fast = crossings (1.0f), slow = crossings (0.0f);
        CHECK (fast > normal * 2);
        CHECK (slow < normal);
    }
}
