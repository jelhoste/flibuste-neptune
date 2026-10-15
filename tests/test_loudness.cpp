// Sonie du Master : libebur128 sur un thread d'analyse (EBU Tech 3341 : un sinus stéréo de -23 dBFS mesure -23 LUFS).
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "neptune/core/Loudness.h"

using namespace neptune::core;

namespace
{
// Pousse `seconds` de sinus stéréo par paquets en laissant le thread d'analyse suivre.
void feedSine (LoudnessMeter& m, double rate, double hz, double amplitude, double seconds)
{
    constexpr int chunk = 2400;
    std::vector<float> l (chunk), r (chunk);
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979323846 * hz / rate;
    const int chunks = static_cast<int> (seconds * rate / chunk);
    for (int c = 0; c < chunks; ++c)
    {
        for (int i = 0; i < chunk; ++i)
        {
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = static_cast<float> (amplitude * std::sin (phase));
            phase += inc;
        }
        m.push (l.data(), r.data(), chunk);
        std::this_thread::sleep_for (std::chrono::milliseconds (15));
    }
}
} // namespace

TEST_CASE ("loudness meter measures a -23 dBFS stereo sine as -23 LUFS", "[loudness]")
{
    if (! LoudnessMeter::available()) { SUCCEED ("libebur128 non compilée"); return; }
    LoudnessMeter m;
    m.prepare (48000.0);
    const double amp = std::pow (10.0, -23.0 / 20.0);
    feedSine (m, 48000.0, 997.0, amp, 4.0);
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    const auto r = m.read();
    CHECK (std::fabs (r.momentary + 23.0f) < 0.4f);
    CHECK (std::fabs (r.shortTerm + 23.0f) < 0.6f);
    CHECK (std::fabs (r.integrated + 23.0f) < 0.4f);
    CHECK (std::fabs (r.truePeakDb + 23.0f) < 0.8f);

    // remise à zéro : l'intégrée repart de « -inf »
    m.reset();
    std::this_thread::sleep_for (std::chrono::milliseconds (350));
    CHECK (m.read().integrated < -100.0f);
}

TEST_CASE ("loudness meter stays at minus infinity on silence and never blocks the writer", "[loudness]")
{
    LoudnessMeter m;
    m.prepare (44100.0);
    std::vector<float> zeros (512, 0.0f);
    for (int i = 0; i < 500; ++i) m.push (zeros.data(), zeros.data(), 512); // plus que la capacité du tampon : surplus perdu, sans blocage
    std::this_thread::sleep_for (std::chrono::milliseconds (250));
    const auto r = m.read();
    CHECK (r.momentary <= -100.0f);
    CHECK (r.integrated <= -100.0f);
    m.stop();
}
