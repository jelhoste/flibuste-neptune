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
