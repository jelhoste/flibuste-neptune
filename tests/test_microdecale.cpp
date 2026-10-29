// Microdécale : décaleur à raccords (hauteur exacte, propreté, continuité), retards, feedback, niveaux et panoramiques, mode mono, modulation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/MicrodecaleModule.h"
#include "neptune/core/MicrodecalePresets.h"
#include "neptune/core/SpliceShifter.h"

using namespace neptune::core;
using Catch::Approx;
namespace md = neptune::core::microdecale;

namespace
{
constexpr double kRate = 48000.0;
std::size_t ms (double t) { return static_cast<std::size_t> (std::lround (t * 0.001 * kRate)); }

struct MicroRig
{
    ModuleRunner runner { std::make_unique<MicrodecaleModule>() };
    MicroRig()
    {
        REQUIRE (runner.ok());
        runner.prepare (kRate, 512);
        std::vector<float> a (512, 0.0f), b (512, 0.0f);
        runner.process (a.data(), b.data(), 512, false);   // mix à 100 % par défaut : rien à attendre
    }
    void set (ParamId id, double v) { runner.params().set (id, v); }
    std::pair<std::vector<float>, std::vector<float>> play (const std::vector<float>& l, const std::vector<float>& r, std::size_t total)
    {
        std::vector<float> a (total, 0.0f), b (total, 0.0f);
        for (std::size_t i = 0; i < l.size() && i < total; ++i) { a[i] = l[i]; b[i] = r[i]; }
        for (std::size_t off = 0; off < total; off += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, total - off));
            runner.process (a.data() + off, b.data() + off, n, false);
        }
        return { a, b };
    }
    std::pair<std::vector<float>, std::vector<float>> playMono (const std::vector<float>& in, std::size_t total) { return play (in, in, total); }
};
std::vector<float> sine (double f, std::size_t n, float amp = 0.5f)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float> (std::sin (2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate));
    return v;
}
// fréquence estimée par les passages à zéro montants sur [from, to)
double frequencyOf (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    int crossings = 0;
    double first = -1.0, last = -1.0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (-v[i - 1]) / static_cast<double> (v[i] - v[i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++crossings;
        }
    return crossings < 2 ? 0.0 : static_cast<double> (crossings - 1) * kRate / (last - first);
}
// part de l'énergie qui n'est PAS à la fréquence f (projection sur sinus et cosinus de f) : 0 = sinus pur de fréquence f
double impurity (const std::vector<float>& v, std::size_t from, std::size_t to, double f)
{
    double re = 0.0, im = 0.0, energy = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        const double a = 2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate;
        re += v[i] * std::cos (a);
        im += v[i] * std::sin (a);
        energy += static_cast<double> (v[i]) * v[i];
    }
    const double n = static_cast<double> (to - from);
    const double tone = 2.0 * (re * re + im * im) / (n * n) * n;   // énergie de la composante f (amplitude 2|c|/n, énergie n/2 x amplitude^2)
    return std::max (0.0, 1.0 - tone / std::max (energy, 1.0e-12));
}
} // namespace

TEST_CASE ("microdecale: declares its parameters with the starting setting and 100 % wet", "[microdecale]")
{
    ModuleRunner r (std::make_unique<MicrodecaleModule>());
    REQUIRE (r.ok());
    auto& p = r.params();
    CHECK (p.get (stock::mix) == Approx (100.0));
    CHECK (p.get (md::shiftL) == Approx (9.0));
    CHECK (p.get (md::shiftR) == Approx (-9.0));
    CHECK (p.get (md::delayL) == Approx (0.0));
    CHECK (p.get (md::delayR) == Approx (25.0));
    CHECK (p.get (md::levelShiftL) == Approx (100.0));
    CHECK (p.get (md::levelDryL) == Approx (0.0));
    CHECK (p.get (md::panShiftL) == Approx (-100.0));
    CHECK (p.get (md::panShiftR) == Approx (100.0));
    CHECK (p.find (md::splice)->automatable == false);
    CHECK (p.find (md::shiftL)->min == Approx (-1200.0));
    CHECK (p.find (md::delayR)->max == Approx (1400.0));
    CHECK (r.latencySamples() == 0);
}

TEST_CASE ("microdecale: the pitch is shifted by the requested number of cents", "[microdecale]")
{
    for (const double cents : { 100.0, -100.0, 50.0, 9.0, 700.0 })
    {
        MicroRig r;
        r.set (md::shiftL, cents);
        r.set (md::shiftR, cents);
        r.set (md::delayR, 0.0);
        const auto in = sine (1000.0, ms (3000));
        const auto [l, rr] = r.playMono (in, ms (3000));
        (void) rr;
        const double expected = 1000.0 * std::pow (2.0, cents / 1200.0);
        INFO ("cents " << cents);
        CHECK (frequencyOf (l, ms (1000), ms (3000)) == Approx (expected).epsilon (0.004));
    }
}

TEST_CASE ("microdecale: a shifted sine stays clean and continuous across the splices", "[microdecale]")
{
    MicroRig r;
    r.set (md::shiftL, 60.0);
    r.set (md::shiftR, 60.0);
    r.set (md::delayR, 0.0);
    r.set (md::panShiftR, -100.0);                   // les deux voies à gauche : on regarde le canal gauche seul
    r.set (md::levelShiftR, 0.0);
    const auto in = sine (440.0, ms (4000));
    const auto [l, rr] = r.playMono (in, ms (4000));
    (void) rr;
    const double f = 440.0 * std::pow (2.0, 60.0 / 1200.0);
    CHECK (impurity (l, ms (500), ms (900), f) < 0.08);            // un seul raccord au plus dans cette fenêtre : très peu d'énergie hors de la fréquence
    // l'amplitude ne s'effondre jamais pendant un raccord (fondu entre deux copies alignées par corrélation)
    float minEnv = 1.0f;
    for (std::size_t s = ms (500); s + ms (10) <= ms (4000); s += ms (10))
    {
        float m = 0.0f;
        for (std::size_t i = s; i < s + ms (10); ++i) m = std::max (m, std::fabs (l[i]));
        minEnv = std::min (minEnv, m);
    }
    CHECK (minEnv > 0.35f);                                         // 0,5 en entrée
}

TEST_CASE ("microdecale: left and right have their own delay, and the right one is later", "[microdecale]")
{
    MicroRig r;
    r.set (md::shiftL, 0.0);
    r.set (md::shiftR, 0.0);
    r.set (md::delayL, 0.0);
    r.set (md::delayR, 25.0);
    std::vector<float> imp (1, 1.0f);
    const auto [l, rr] = r.playMono (imp, ms (120));
    auto peak = [] (const std::vector<float>& v) { std::size_t b = 0; for (std::size_t i = 0; i < v.size(); ++i) if (std::fabs (v[i]) > std::fabs (v[b])) b = i; return b; };
    const double base = MicrodecaleModule::shifterDelayMs (30.0) * 0.001 * kRate;
    CHECK (static_cast<double> (peak (l)) == Approx (base).margin (8.0));
    CHECK (static_cast<double> (peak (rr)) == Approx (base + static_cast<double> (ms (25.0))).margin (8.0));
}

TEST_CASE ("microdecale: pans send each voice to its side; negative level inverts the phase", "[microdecale]")
{
    MicroRig r;
    r.set (md::shiftL, 0.0);
    r.set (md::shiftR, 0.0);
    r.set (md::delayR, 0.0);
    const auto in = sine (500.0, ms (400));
    const auto [l, rr] = r.playMono (in, ms (400));
    float leftOnly = 0.0f, rightOnly = 0.0f;
    // chaque voie est seule de son côté : le gauche et le droit ont le même niveau, sans mélange
    for (std::size_t i = ms (200); i < ms (400); ++i) { leftOnly = std::max (leftOnly, std::fabs (l[i])); rightOnly = std::max (rightOnly, std::fabs (rr[i])); }
    CHECK (leftOnly == Approx (0.5f).margin (0.03f));
    CHECK (rightOnly == Approx (0.5f).margin (0.03f));
    MicroRig c;                                         // les deux voies au centre : somme ; niveau droit inversé : annulation
    for (const auto id : { md::panShiftL, md::panShiftR }) c.set (id, 0.0);
    c.set (md::shiftL, 0.0);
    c.set (md::shiftR, 0.0);
    c.set (md::delayR, 0.0);
    c.set (md::levelShiftR, -100.0);
    c.runner.reset();                                   // repart des réglages (les décaleurs gardent sinon le retard accumulé pendant la transition +9 / -9 cents)
    const auto [cl, cr] = c.playMono (sine (500.0, ms (800)), ms (800));
    (void) cr;
    float cancelled = 0.0f;
    for (std::size_t i = ms (300); i < ms (800); ++i) cancelled = std::max (cancelled, std::fabs (cl[i]));
    CHECK (cancelled < 0.02f);
}

TEST_CASE ("microdecale: mono mode feeds both voices with the sum of the inputs", "[microdecale]")
{
    MicroRig st;
    st.set (md::shiftL, 0.0);
    st.set (md::shiftR, 0.0);
    st.set (md::delayR, 0.0);
    const auto in = sine (500.0, ms (400));
    const std::vector<float> silence (in.size(), 0.0f);
    const auto [sl, sr] = st.play (in, silence, ms (400));          // entrée gauche seule, mode stéréo : la voie droite reste muette
    float rightLevel = 0.0f;
    for (std::size_t i = ms (200); i < ms (400); ++i) rightLevel = std::max (rightLevel, std::fabs (sr[i]));
    CHECK (rightLevel < 0.01f);
    (void) sl;
    MicroRig mono;
    mono.set (md::mode, 1.0);
    mono.set (md::shiftL, 0.0);
    mono.set (md::shiftR, 0.0);
    mono.set (md::delayR, 0.0);
    const auto [ml, mr] = mono.play (in, silence, ms (400));
    float monoRight = 0.0f;
    for (std::size_t i = ms (200); i < ms (400); ++i) monoRight = std::max (monoRight, std::fabs (mr[i]));
    CHECK (monoRight > 0.2f);                                       // la somme (moitié du gauche) arrive aux deux voies
    (void) ml;
}

TEST_CASE ("microdecale: feedback repeats through the shifter, signed, and stays bounded", "[microdecale]")
{
    MicroRig r;
    r.set (md::shiftL, 0.0);
    r.set (md::shiftR, 0.0);
    r.set (md::delayL, 40.0);
    r.set (md::feedbackL, 50.0);
    std::vector<float> imp (1, 1.0f);
    const auto [l, rr] = r.playMono (imp, ms (400));
    (void) rr;
    const double period = MicrodecaleModule::shifterDelayMs (30.0) + 40.0;   // ms entre deux répétitions
    auto at = [&] (int k) { float m = 0.0f; const std::size_t c = ms (period * k); for (std::size_t i = c - 40; i < c + 40 && i < l.size(); ++i) m = std::max (m, std::fabs (l[i])); return m; };
    CHECK (at (2) == Approx (0.5f * at (1)).margin (0.06f));
    MicroRig e;
    e.set (md::feedbackL, 90.0);
    e.set (md::feedbackR, -90.0);
    e.set (md::shiftL, 100.0);
    e.set (md::shiftR, -100.0);
    const auto [el, er] = e.playMono (sine (300.0, ms (1000), 0.8f), ms (4000));
    float peak = 0.0f;
    for (std::size_t i = 0; i < el.size(); ++i) { REQUIRE (std::isfinite (el[i])); REQUIRE (std::isfinite (er[i])); peak = std::max ({ peak, std::fabs (el[i]), std::fabs (er[i]) }); }
    CHECK (peak < 12.0f);
}

TEST_CASE ("microdecale: modulation moves the pitch around its centre and stays finite", "[microdecale]")
{
    MicroRig r;
    r.set (md::shiftL, 0.0);
    r.set (md::shiftR, 0.0);
    r.set (md::delayR, 0.0);
    r.set (md::mod, 100.0);
    r.set (md::modRate, 2.0);
    const auto [l, rr] = r.playMono (sine (1000.0, ms (3000)), ms (3000));
    (void) rr;
    double fMin = 1.0e9, fMax = 0.0;
    for (std::size_t s = ms (500); s + ms (100) <= ms (3000); s += ms (50))
    {
        const double f = frequencyOf (l, s, s + ms (100));
        if (f > 0.0) { fMin = std::min (fMin, f); fMax = std::max (fMax, f); }
    }
    CHECK (fMax > fMin * 1.003);                                    // la hauteur bouge (25 cents au plus = 1,45 %)
    CHECK (fMax < 1000.0 * 1.03);
    CHECK (fMin > 1000.0 / 1.03);
    MicroRig rnd;
    rnd.set (md::mod, 50.0);
    rnd.set (md::modWave, 1.0);
    const auto [rl, rrr] = rnd.playMono (sine (500.0, ms (2000)), ms (2000));
    for (const auto v : rl) REQUIRE (std::isfinite (v));
    (void) rrr;
}

TEST_CASE ("microdecale: factory presets apply cleanly and Default restores the starting setting", "[microdecale][presets]")
{
    MicroRig r;
    const auto& list = md::factoryPresets();
    REQUIRE (list.size() >= 3);
    CHECK (std::string (list[0].name) == "Default");
    for (const auto& p : list)
        for (const auto& [id, v] : p.values)
        {
            REQUIRE (r.runner.params().find (id) != nullptr);
            INFO (p.name << " id " << id);
            CHECK (r.runner.params().find (id)->clamp (v) == Approx (v));
        }
    CHECK (md::applyPreset (r.runner.params(), list[1]) > 0);
    CHECK (r.runner.params().get (md::delayR) == Approx (90.0));
    md::applyPreset (r.runner.params(), list[0]);
    CHECK (r.runner.params().get (md::delayR) == Approx (25.0));
    CHECK (r.runner.params().get (stock::mix) == Approx (100.0));
}

TEST_CASE ("microdecale: the splice search is spread over many samples and the delay stays inside its range", "[microdecale]")
{
    for (const double cents : { 60.0, -60.0, 1200.0, -1200.0 })
    {
        SpliceShifter sh;
        sh.prepare (kRate);
        sh.setSplice (30.0);
        const double ratio = std::pow (2.0, cents / 1200.0);
        const auto in = sine (440.0, static_cast<std::size_t> (kRate * 12.0));
        double lo = 1.0e9, hi = 0.0;
        long maxPerSample = 0;
        int jumps = 0;
        double prev = sh.currentDelaySamples();
        for (const float x : in)
        {
            sh.process (x, ratio);
            const double d = sh.currentDelaySamples();
            lo = std::min (lo, d);
            hi = std::max (hi, d);
            maxPerSample = std::max (maxPerSample, sh.candidatesPerSample());
            if (std::fabs (d - prev) > 500.0) ++jumps;
            prev = d;
        }
        INFO ("cents " << cents << " delay " << lo << " .. " << hi);
        CHECK (lo >= 3.0);   // la recherche étalée laisse le pointeur dériver un peu sous le bas de sa course (384 échantillons) : jamais sous 3
        CHECK (hi < 1824.0 + 288.0 + 8.0 + 1.0);
        CHECK (jumps >= 1);   // au moins un raccord a eu lieu
        if (std::fabs (cents) < 100.0) { CHECK (maxPerSample == 1); CHECK (sh.searchStride() >= 4); }   // petit écart : un candidat tous les quelques échantillons (577 positions sur 4096 échantillons)
    }
}
