// Mirador : mesures de niveau, de stéréo et de sonie (cœur, sans interface).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "neptune/core/Mirador.h"

using namespace neptune::core;
using namespace neptune::core::mirador;
using Catch::Approx;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Noise
{
    std::uint32_t s;
    float next() noexcept   // -1..1 (xorshift32)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float> (static_cast<std::int32_t> (s)) * (1.0f / 2147483648.0f);
    }
};

// Passe `seconds` de signal dans le moteur par blocs de 480 échantillons. `gain` : gauche, droite (la droite peut être inversée par un gain négatif).
void runSine (MiradorEngine& e, double amplitude, double gainL, double gainR, double seconds, bool playing = true)
{
    constexpr int block = 480;
    std::vector<float> l (block), r (block);
    double phase = 0.0;
    const double inc = 2.0 * kPi * 997.0 / kRate;
    const int blocks = static_cast<int> (seconds * kRate / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double x = amplitude * std::sin (phase);
            phase += inc;
            l[static_cast<std::size_t> (i)] = static_cast<float> (gainL * x);
            r[static_cast<std::size_t> (i)] = static_cast<float> (gainR * x);
        }
        e.process (l.data(), r.data(), block, playing);
    }
}

void runSilence (MiradorEngine& e, double seconds, bool playing = true)
{
    constexpr int block = 480;
    std::vector<float> z (block, 0.0f);
    const int blocks = static_cast<int> (seconds * kRate / block);
    for (int b = 0; b < blocks; ++b) e.process (z.data(), z.data(), block, playing);
}

} // namespace

TEST_CASE ("mirador declares valid parameters", "[mirador]")
{
    MiradorEngine e;
    REQUIRE (e.ok());
    CHECK (e.params().indexOf (target) != ParameterSet::npos);
    CHECK (e.params().indexOf (autoReset) != ParameterSet::npos);
}

TEST_CASE ("mirador targets, PLR / PSR and target state", "[mirador]")
{
    CHECK (targetLufs (kTargetEbu, -99.0) == Approx (-23.0));
    CHECK (targetLufs (kTargetAtsc, -99.0) == Approx (-24.0));
    CHECK (targetLufs (kTargetPodcast, -99.0) == Approx (-16.0));
    CHECK (targetLufs (kTargetStreaming, -99.0) == Approx (-14.0));
    CHECK (targetLufs (kTargetCustom, -18.5) == Approx (-18.5));

    LoudnessMeter::Reading r;
    CHECK (plr (r) == Approx (kNoValue));
    CHECK (psr (r) == Approx (kNoValue));
    r.truePeakDb = -1.0f; r.integrated = -14.0f; r.shortTerm = -12.0f;
    CHECK (plr (r) == Approx (13.0f));
    CHECK (psr (r) == Approx (11.0f));

    CHECK (targetState (-200.0f, -23.0, 1.0) == TargetState::Unknown);
    CHECK (targetState (-23.5f, -23.0, 1.0) == TargetState::OnTarget);
    CHECK (targetState (-21.0f, -23.0, 1.0) == TargetState::Over);
    CHECK (targetState (-25.0f, -23.0, 1.0) == TargetState::Under);
}

TEST_CASE ("mirador measures peak, RMS and crest factor of a sine", "[mirador]")
{
    MiradorEngine e;
    e.prepare (kRate, 480);
    runSine (e, 0.5, 1.0, 1.0, 1.5);
    const auto v = e.levels();
    CHECK (v.peakL == Approx (-6.02f).margin (0.15f));
    CHECK (v.holdR == Approx (-6.02f).margin (0.15f));
    CHECK (v.rmsL == Approx (-9.03f).margin (0.15f));
    CHECK (v.rmsR == Approx (-9.03f).margin (0.15f));
    CHECK (v.crest == Approx (3.01f).margin (0.2f));
    CHECK_FALSE (v.clipL);
    CHECK_FALSE (v.clipR);
}

TEST_CASE ("mirador peak falls at the set rate and the hold lasts", "[mirador]")
{
    MiradorEngine e;
    e.prepare (kRate, 480);
    runSine (e, 0.5, 1.0, 1.0, 0.5);
    runSilence (e, 1.0);
    const auto v = e.levels();
    CHECK (v.peakL == Approx (-6.02f - 20.0f).margin (1.0f));   // 20 dB par seconde par défaut
    CHECK (v.holdL == Approx (-6.02f).margin (0.15f));          // 2 s de maintien par défaut
    runSilence (e, 2.5);
    CHECK (e.levels().holdL < -40.0f);                           // le maintien est lâché et suit la retombée
}

TEST_CASE ("mirador flags clipping and clears it on reset", "[mirador]")
{
    MiradorEngine e;
    e.prepare (kRate, 480);
    runSine (e, 0.9, 1.0, 1.0, 0.2);
    CHECK_FALSE (e.levels().clipL);
    runSine (e, 1.0, 1.2, 0.5, 0.2);   // la gauche dépasse 0 dBFS, la droite non
    CHECK (e.levels().clipL);
    CHECK_FALSE (e.levels().clipR);
    e.resetAll();
    runSilence (e, 0.05);
    CHECK_FALSE (e.levels().clipL);
}

TEST_CASE ("mirador stereo: correlation, balance and width", "[mirador]")
{
    {   // deux voies identiques : mono
        MiradorEngine e;
    e.prepare (kRate, 480);
        runSine (e, 0.5, 1.0, 1.0, 1.5);
        const auto v = e.levels();
        CHECK (v.correlation == Approx (1.0f).margin (0.01f));
        CHECK (v.balance == Approx (0.0f).margin (0.01f));
        CHECK (v.width == Approx (0.0f).margin (0.01f));
    }
    {   // voie droite inversée : opposition de phase
        MiradorEngine e;
    e.prepare (kRate, 480);
        runSine (e, 0.5, 1.0, -1.0, 1.5);
        const auto v = e.levels();
        CHECK (v.correlation == Approx (-1.0f).margin (0.01f));
        CHECK (v.width == Approx (1.0f).margin (0.01f));
    }
    {   // gauche seule
        MiradorEngine e;
    e.prepare (kRate, 480);
        runSine (e, 0.5, 1.0, 0.0, 1.5);
        const auto v = e.levels();
        CHECK (v.balance == Approx (-1.0f).margin (0.01f));
        CHECK (v.correlation == Approx (0.0f).margin (0.01f));
    }
    {   // deux bruits indépendants : corrélation proche de zéro
        MiradorEngine e;
    e.prepare (kRate, 480);
        Noise a { 2463534242u }, b { 123456789u };
        std::vector<float> l (480), r (480);
        for (int blk = 0; blk < 200; ++blk)
        {
            for (std::size_t i = 0; i < 480; ++i) { l[i] = 0.3f * a.next(); r[i] = 0.3f * b.next(); }
            e.process (l.data(), r.data(), 480, true);
        }
        CHECK (std::fabs (e.levels().correlation) < 0.1f);
    }
}

TEST_CASE ("mirador leaves the audio untouched and copes with a mono input and silence", "[mirador]")
{
    MiradorEngine e;
    e.prepare (kRate, 480);
    std::vector<float> l (256), r (256);
    for (std::size_t i = 0; i < 256; ++i) { l[i] = 0.001f * static_cast<float> (i); r[i] = -0.002f * static_cast<float> (i); }
    const auto l0 = l, r0 = r;
    e.process (l.data(), r.data(), 256, true);
    CHECK (l == l0);
    CHECK (r == r0);

    e.process (l.data(), nullptr, 256, true);   // entrée mono : pas de plantage, les deux voies sont identiques
    e.process (nullptr, nullptr, 256, true);
    e.process (l.data(), r.data(), 0, true);

    MiradorEngine s;
    s.prepare (kRate, 480);
    runSilence (s, 0.5);
    const auto v = s.levels();
    CHECK (v.peakL == Approx (kNoValue));
    CHECK (v.rmsL == Approx (kNoValue));
    CHECK (v.correlation == Approx (0.0f).margin (1.0e-6f));
    CHECK (v.crest == Approx (0.0f).margin (1.0e-6f));
}

TEST_CASE ("loudness history keeps the newest points in order", "[mirador]")
{
    LoudnessHistory h (4);
    CHECK (h.size() == 0);
    CHECK (h.at (0).momentary == Approx (kNoValue));
    for (int i = 1; i <= 3; ++i) h.add (static_cast<float> (-i), static_cast<float> (-10 * i));
    REQUIRE (h.size() == 3);
    CHECK (h.at (0).momentary == Approx (-1.0f));
    CHECK (h.at (2).shortTerm == Approx (-30.0f));
    for (int i = 4; i <= 6; ++i) h.add (static_cast<float> (-i), static_cast<float> (-10 * i));
    REQUIRE (h.size() == 4);   // plein : les plus anciens sont perdus
    CHECK (h.at (0).momentary == Approx (-3.0f));
    CHECK (h.at (3).momentary == Approx (-6.0f));

    h.setCapacity (2);   // garde les deux plus récents
    REQUIRE (h.size() == 2);
    CHECK (h.at (0).momentary == Approx (-5.0f));
    CHECK (h.at (1).momentary == Approx (-6.0f));
    h.add (-7.0f, -70.0f);
    CHECK (h.at (0).momentary == Approx (-6.0f));
    CHECK (h.at (1).momentary == Approx (-7.0f));

    h.setCapacity (10);   // agrandir garde tout
    REQUIRE (h.size() == 2);
    CHECK (h.at (1).momentary == Approx (-7.0f));
    h.clear();
    CHECK (h.size() == 0);
}

TEST_CASE ("mirador measures -23 LUFS, derives PLR and resets on play", "[mirador]")
{
    if (! LoudnessMeter::available()) { SUCCEED ("libebur128 non compilée"); return; }
    MiradorEngine e;
    e.prepare (kRate, 2400);

    const double amp = std::pow (10.0, -23.0 / 20.0);
    constexpr int chunk = 2400;
    std::vector<float> l (chunk), r (chunk);
    double phase = 0.0;
    const double inc = 2.0 * kPi * 997.0 / kRate;
    for (int c = 0; c < 80; ++c)   // 4 s
    {
        for (std::size_t i = 0; i < static_cast<std::size_t> (chunk); ++i)
        {
            l[i] = r[i] = static_cast<float> (amp * std::sin (phase));
            phase += inc;
        }
        e.process (l.data(), r.data(), chunk, false);
        std::this_thread::sleep_for (std::chrono::milliseconds (15));
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    auto r1 = e.loudness();
    CHECK (std::fabs (r1.integrated + 23.0f) < 0.4f);
    CHECK (std::fabs (plr (r1)) < 0.9f);                    // sinus : crête vraie ≈ sonie intégrée
    CHECK (r1.range >= 0.0f);
    CHECK (r1.range < 1.0f);                                // niveau constant : plage presque nulle
    CHECK (targetState (r1.integrated, targetLufs (kTargetEbu, 0.0), 1.0) == TargetState::OnTarget);
    CHECK (targetState (r1.integrated, targetLufs (kTargetStreaming, 0.0), 1.0) == TargetState::Under);

    // départ du transport : l'intégrée repart de « -inf » (option « Reset On Play » active par défaut)
    runSilence (e, 0.01, false);
    runSilence (e, 0.05, true);
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    CHECK (e.loudness().integrated < -100.0f);
    e.stop();
}
