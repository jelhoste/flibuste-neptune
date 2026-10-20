// Chorus : moteur standard (retard, profondeur, voix, largeur, feedback, filtres, BBD, synchronisation) et mode TAL (deux unités I et II).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/ChorusModule.h"
#include "neptune/core/ChorusPresets.h"

using namespace neptune::core;
using Catch::Approx;
namespace ch = neptune::core::chorus;

namespace
{
constexpr double kRate = 48000.0;
std::size_t ms (double t) { return static_cast<std::size_t> (std::lround (t * 0.001 * kRate)); }

struct ChorusRig
{
    ModuleRunner runner { std::make_unique<ChorusModule>() };
    ChorusRig()
    {
        REQUIRE (runner.ok());
        runner.prepare (kRate, 512);
        runner.params().set (stock::mix, 100.0);
        std::vector<float> a (512, 0.0f), b (512, 0.0f);
        runner.process (a.data(), b.data(), 512, false);   // laisse le mix atteindre 100 % avant le premier test
    }
    void set (ParamId id, double v) { runner.params().set (id, v); }
    ChorusModule& module() { return static_cast<ChorusModule&> (runner.module()); }
    // laisse les réglages lissés (retard sur 50 ms, profondeur et largeur sur 20 ms) atteindre leur valeur avant de mesurer
    void settle() { play (std::vector<float> (1, 0.0f), ms (600)); }
    std::pair<std::vector<float>, std::vector<float>> play (const std::vector<float>& in, std::size_t total)
    {
        std::vector<float> l (total, 0.0f), r (total, 0.0f);
        for (std::size_t i = 0; i < in.size() && i < total; ++i) l[i] = r[i] = in[i];
        for (std::size_t off = 0; off < total; off += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, total - off));
            runner.process (l.data() + off, r.data() + off, n, false);
        }
        return { l, r };
    }
};
std::size_t peakIndex (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    std::size_t best = from;
    for (std::size_t i = from; i < to && i < v.size(); ++i) if (std::fabs (v[i]) > std::fabs (v[best])) best = i;
    return best;
}
std::vector<float> impulse() { return std::vector<float> (1, 1.0f); }
std::vector<float> sine (double f, std::size_t n, float amp = 0.5f)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float> (std::sin (2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate));
    return v;
}
float level (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    float m = 0.0f;
    for (std::size_t i = from; i < to && i < v.size(); ++i) m = std::max (m, std::fabs (v[i]));
    return m;
}
} // namespace

TEST_CASE ("chorus: declares its parameters with the documented defaults", "[chorus]")
{
    ModuleRunner r (std::make_unique<ChorusModule>());
    REQUIRE (r.ok());
    auto& p = r.params();
    CHECK (p.get (stock::mix) == Approx (50.0));
    CHECK (p.get (ch::mode) == Approx (0.0));
    CHECK (p.get (ch::rate) == Approx (0.5));
    CHECK (p.get (ch::depth) == Approx (50.0));
    CHECK (p.get (ch::delayMs) == Approx (7.0));
    CHECK (p.get (ch::width) == Approx (100.0));
    CHECK (p.get (ch::talRate1) == Approx (0.5));
    CHECK (p.get (ch::talRate2) == Approx (0.83));
    CHECK (p.find (ch::mode)->automatable == false);
    CHECK (p.find (ch::lfoShape)->automatable == false);
    CHECK (r.latencySamples() == 0);
}

TEST_CASE ("chorus: with no depth the wet signal is a plain delay of DELAY ms", "[chorus]")
{
    ChorusRig r;
    r.set (ch::depth, 0.0);
    r.set (ch::delayMs, 10.0);
    r.settle();
    const auto [l, rr] = r.play (impulse(), ms (50));
    (void) rr;
    CHECK (peakIndex (l, 5, ms (50)) == ms (10));
    CHECK (std::fabs (l[ms (10)]) == Approx (1.0f).margin (0.02f));
    for (std::size_t i = 0; i < ms (9.5); ++i) REQUIRE (std::fabs (l[i]) < 1e-5f);
}

TEST_CASE ("chorus: the modulated delay follows the LFO and the right channel is in opposition", "[chorus]")
{
    // à t = 0 la phase est 0 : triangle à -1 à gauche (retard minimal) ; à droite, largeur 100 % = demi-cycle = +1 (retard maximal)
    ChorusRig r;
    r.set (ch::depth, 50.0);
    r.set (ch::delayMs, 10.0);
    r.set (ch::rate, 0.05);                                   // LFO très lent : le retard ne bouge pas pendant l'écho
    r.settle();
    const auto [l, rr] = r.play (impulse(), ms (50));
    CHECK (peakIndex (l, 5, ms (50)) == Approx (static_cast<double> (ms (5.0))).margin (static_cast<double> (ms (1.5))));      // environ 10 x (1 - 0,5)
    CHECK (peakIndex (rr, 5, ms (50)) == Approx (static_cast<double> (ms (15.0))).margin (static_cast<double> (ms (1.5))));    // environ 10 x (1 + 0,5)
    ChorusRig mono;
    mono.set (ch::depth, 50.0);
    mono.set (ch::delayMs, 10.0);
    mono.set (ch::rate, 0.05);
    mono.set (ch::width, 0.0);                                // largeur nulle : les deux canaux modulent ensemble
    mono.settle();
    const auto [ml, mr] = mono.play (impulse(), ms (50));
    CHECK (peakIndex (ml, 5, ms (50)) == peakIndex (mr, 5, ms (50)));
}

TEST_CASE ("chorus: voices split the level and spread the echoes", "[chorus]")
{
    ChorusRig r;
    r.set (ch::voices, 3);                                    // 4 voix
    r.set (ch::depth, 50.0);
    r.set (ch::delayMs, 10.0);
    r.set (ch::rate, 0.05);
    r.settle();
    const auto [l, rr] = r.play (impulse(), ms (60));
    (void) rr;
    int echoes = 0;
    for (std::size_t i = 5; i < ms (60); ++i) if (std::fabs (l[i]) > 0.15f) ++echoes;
    CHECK (echoes >= 3);                                      // plusieurs échos distincts, chacun au quart du niveau
    CHECK (level (l, 5, ms (60)) < 0.4f);
}

TEST_CASE ("chorus: feedback repeats the echo, sign kept", "[chorus]")
{
    ChorusRig r;
    r.set (ch::depth, 0.0);
    r.set (ch::delayMs, 10.0);
    r.set (ch::feedback, 50.0);
    r.settle();
    const auto [l, rr] = r.play (impulse(), ms (60));
    (void) rr;
    CHECK (l[ms (10)] == Approx (1.0f).margin (0.03f));
    CHECK (l[ms (20)] == Approx (0.5f).margin (0.03f));
    CHECK (l[ms (30)] == Approx (0.25f).margin (0.03f));
    ChorusRig n;
    n.set (ch::depth, 0.0);
    n.set (ch::delayMs, 10.0);
    n.set (ch::feedback, -50.0);
    n.settle();
    const auto [nl, nr] = n.play (impulse(), ms (60));
    (void) nr;
    CHECK (nl[ms (20)] == Approx (-0.5f).margin (0.03f));
}

TEST_CASE ("chorus: low cut, high cut and BBD shape the wet signal", "[chorus]")
{
    auto steady = [] (bool low, bool high, double bbdAmount, double f)
    {
        ChorusRig r;
        r.set (ch::depth, 0.0);
        r.set (ch::delayMs, 5.0);
        if (low) { r.set (ch::lowCutOn, 1.0); r.set (ch::lowCut, 1000.0); }
        if (high) { r.set (ch::highCutOn, 1.0); r.set (ch::highCut, 1000.0); }
        r.set (ch::bbd, bbdAmount);
        r.settle();
        const auto [l, rr] = r.play (sine (f, ms (400)), ms (400));
        (void) rr;
        return level (l, ms (200), ms (400));
    };
    CHECK (steady (true, false, 0.0, 100.0) < 0.25f * steady (false, false, 0.0, 100.0));
    CHECK (steady (false, true, 0.0, 10000.0) < 0.1f * steady (false, false, 0.0, 10000.0));
    CHECK (steady (false, false, 100.0, 10000.0) < 0.5f * steady (false, false, 0.0, 10000.0));       // BBD : aigus adoucis
    CHECK (steady (false, false, 100.0, 200.0) > 0.35f);                                                 // les graves passent
}

TEST_CASE ("chorus: SYNC sets the LFO speed from the tempo and the division", "[chorus]")
{
    ChorusRig r;
    r.set (ch::sync, 1.0);
    r.set (ch::syncDiv, 4.0);                                 // 1/4 à 120 bpm : un cycle en 0,5 s, soit 2 Hz
    r.module().setTempo (120.0);
    r.play (std::vector<float> (1, 0.0f), ms (250));
    const double ph = r.module().meter (ch::kLfoPhase);
    CHECK (ph == Approx (0.5).margin (0.05));
}

TEST_CASE ("chorus: TAL mode is dry plus 1.4 x wet, off when both units are off", "[chorus][tal]")
{
    ChorusRig off;
    off.set (ch::mode, 1.0);
    const auto in = sine (440.0, ms (200));
    const auto [ol, orr] = off.play (in, ms (200));
    (void) orr;
    for (std::size_t i = 0; i < in.size(); ++i) REQUIRE (ol[i] == Approx (in[i]).margin (1e-6f));
    ChorusRig on;
    on.set (ch::mode, 1.0);
    on.set (ch::talI, 1.0);
    const auto [nl, nr] = on.play (in, ms (200));
    (void) nr;
    float diff = 0.0f;
    for (std::size_t i = ms (100); i < ms (200); ++i) diff = std::max (diff, std::fabs (nl[i] - in[i]));
    CHECK (diff > 0.1f);                                      // le signal traité s'ajoute
}

TEST_CASE ("chorus: TAL unit delays sweep 0.7 to 4.9 ms and left and right are opposite", "[chorus][tal]")
{
    ChorusRig r;
    r.set (ch::mode, 1.0);
    r.set (ch::talI, 1.0);
    r.set (ch::talRate1, 0.1);
    const auto [l, rr] = r.play (impulse(), ms (20));
    // l'impulsion directe est à 0 ; on cherche l'écho traité (le signal passé par le passe-bas et le filtre anti-continu) autour de 4,9 ms (gauche) et 0,7 ms (droite)
    auto echo = [&] (const std::vector<float>& v, std::size_t from) { std::size_t best = from; for (std::size_t i = from; i < ms (12); ++i) if (v[i] > v[best]) best = i; return best; };
    CHECK (echo (l, ms (2.0)) == Approx (static_cast<double> (ms (4.9))).margin (static_cast<double> (ms (0.6))));
    CHECK (echo (rr, 3) == Approx (static_cast<double> (ms (0.7))).margin (static_cast<double> (ms (0.6))));
}

TEST_CASE ("chorus: TAL rates are adjustable and drive the LFO", "[chorus][tal]")
{
    ChorusRig r;
    r.set (ch::mode, 1.0);
    r.set (ch::talI, 1.0);
    r.set (ch::talRate1, 1.0);
    r.play (std::vector<float> (1, 0.0f), ms (250));
    const double ph = r.module().meter (ch::kLfoPhase);        // phase gauche de l'unité I : 0,5 + 0,25 cycle
    CHECK (ph == Approx (0.75).margin (0.02));
}

TEST_CASE ("chorus: LFO start resets the phase when the transport starts", "[chorus]")
{
    ChorusRig r;
    r.set (ch::lfoStart, 1.0);
    r.module().setPlaying (false);
    r.play (std::vector<float> (1, 0.0f), ms (300));
    const double before = r.module().meter (ch::kLfoPhase);
    CHECK (before > 0.05);
    r.module().setPlaying (true);
    r.play (std::vector<float> (1, 0.0f), 512);
    CHECK (r.module().meter (ch::kLfoPhase) < before);        // repartie de zéro
}

TEST_CASE ("chorus: output stays finite with extreme settings", "[chorus]")
{
    ChorusRig r;
    r.set (ch::depth, 100.0);
    r.set (ch::delayMs, 30.0);
    r.set (ch::feedback, 90.0);
    r.set (ch::voices, 3);
    r.set (ch::rate, 10.0);
    r.set (ch::bbd, 100.0);
    r.set (ch::lowCutOn, 1.0);
    r.set (ch::highCutOn, 1.0);
    r.set (ch::highCut, 3000.0);
    const auto [l, rr] = r.play (sine (330.0, ms (1500), 0.8f), ms (3000));
    float peak = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i) { REQUIRE (std::isfinite (l[i])); REQUIRE (std::isfinite (rr[i])); peak = std::max (peak, std::fabs (l[i])); }
    CHECK (peak < 20.0f);
}

TEST_CASE ("chorus: factory presets apply cleanly and Default restores the defaults", "[chorus][presets]")
{
    ChorusRig r;
    const auto& list = ch::factoryPresets();
    REQUIRE (list.size() >= 9);
    CHECK (std::string (list[0].name) == "Default");
    for (const auto& p : list)
        for (const auto& [id, v] : p.values)
        {
            REQUIRE (r.runner.params().find (id) != nullptr);
            INFO (p.name << " id " << id);
            CHECK (r.runner.params().find (id)->clamp (v) == Approx (v));
        }
    CHECK (ch::applyPreset (r.runner.params(), list[3]) > 0);
    CHECK (r.runner.params().get (ch::mode) == Approx (1.0));
    CHECK (r.runner.params().get (ch::talII) == Approx (1.0));
    ch::applyPreset (r.runner.params(), list[0]);
    CHECK (r.runner.params().get (ch::mode) == Approx (0.0));
    CHECK (r.runner.params().get (stock::mix) == Approx (50.0));
}
