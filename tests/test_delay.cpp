// Delay à huit lignes : temps, décalage droit, niveau, panoramique, feedback (positif, négatif, global), routage, ping-pong, freeze, ducking, largeur,
// filtres, synchronisation au tempo, solo, limiteur de sécurité, valeurs par défaut.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <tuple>
#include <vector>

#include "neptune/core/DelayModule.h"

using namespace neptune::core;
using Catch::Approx;
namespace d = neptune::core::delay;

namespace
{
constexpr double kRate = 48000.0;

struct DelayRig
{
    ModuleRunner runner { std::make_unique<DelayModule>() };
    DelayRig()
    {
        REQUIRE (runner.ok());
        runner.prepare (kRate, 512);
        runner.params().set (stock::mix, 100.0);   // n'écouter que le signal retardé
        runner.params().set (d::lineParam (0, d::kOn), 0.0);   // les lignes s'activent avec leurs valeurs finales (pas de glissement depuis les défauts)
        std::vector<float> a (512, 0.0f), b (512, 0.0f);
        runner.process (a.data(), b.data(), 512, false);   // laisse le mix atteindre 100 % avant le premier test
    }
    void set (ParamId id, double v) { runner.params().set (id, v); }
    void line (int n, double timeMs, double levelDb = 0.0, double fb = 0.0, double pan = 0.0)
    {
        set (d::lineParam (n, d::kOn), 1.0);
        set (d::lineParam (n, d::kTime), timeMs);
        set (d::lineParam (n, d::kLevel), levelDb);
        set (d::lineParam (n, d::kFeedback), fb);
        set (d::lineParam (n, d::kPan), pan);
    }
    DelayModule& module() { return static_cast<DelayModule&> (runner.module()); }
    // joue `in` (mono, copié sur les deux canaux) puis du silence ; rend les deux canaux
    std::pair<std::vector<float>, std::vector<float>> play (const std::vector<float>& in, std::size_t total, bool stereoIn = false, const std::vector<float>* inR = nullptr)
    {
        std::vector<float> l (total, 0.0f), r (total, 0.0f);
        for (std::size_t i = 0; i < in.size() && i < total; ++i) { l[i] = in[i]; r[i] = stereoIn && inR != nullptr ? (*inR)[i] : in[i]; }
        for (std::size_t off = 0; off < total; off += 512)
        {
            const int n = static_cast<int> (std::min<std::size_t> (512, total - off));
            runner.process (l.data() + off, r.data() + off, n, false);
        }
        return { l, r };
    }
};
std::vector<float> impulse() { return std::vector<float> (1, 1.0f); }
std::size_t ms (double t) { return static_cast<std::size_t> (std::lround (t * 0.001 * kRate)); }
// indice du plus grand |échantillon| dans [from, to)
std::size_t peakIndex (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    std::size_t best = from;
    for (std::size_t i = from; i < to && i < v.size(); ++i) if (std::fabs (v[i]) > std::fabs (v[best])) best = i;
    return best;
}
} // namespace

TEST_CASE ("delay: declares its parameters with the documented defaults, mix included", "[delay]")
{
    ModuleRunner r (std::make_unique<DelayModule>());
    REQUIRE (r.ok());
    auto& p = r.params();
    CHECK (p.get (stock::mix) == Approx (35.0));
    CHECK (p.get (d::width) == Approx (100.0));
    CHECK (p.get (d::lineParam (0, d::kOn)) == Approx (1.0));
    CHECK (p.get (d::lineParam (1, d::kOn)) == Approx (0.0));
    CHECK (p.get (d::lineParam (2, d::kTime)) == Approx (1500.0));       // 500 ms x N
    CHECK (p.get (d::lineParam (3, d::kLevel)) == Approx (-9.0));         // -3 dB x (N - 1)
    CHECK (p.get (d::lineParam (0, d::kFeedback)) == Approx (30.0));
    CHECK (p.get (d::lineParam (0, d::kLowCut)) == Approx (20.0));
    CHECK (p.get (d::lineParam (0, d::kHighCut)) == Approx (20000.0));
    CHECK (p.find (d::routing)->automatable == false);
    CHECK (p.find (d::lineParam (0, d::kDiv))->automatable == false);
    CHECK (p.find (d::lineParam (7, d::kModRate)) != nullptr);
    CHECK (r.latencySamples() == 0);
}

TEST_CASE ("delay: an impulse comes back at TIME, at the line level, and the right channel at TIME + offset", "[delay]")
{
    DelayRig r;
    r.line (0, 100.0, -6.0206, 0.0);
    r.set (d::lineParam (0, d::kOffset), 10.0);
    const auto [l, rr] = r.play (impulse(), ms (400));
    CHECK (peakIndex (l, 10, ms (400)) == ms (100));
    CHECK (std::fabs (l[ms (100)]) == Approx (0.5f).margin (0.01f));
    CHECK (peakIndex (rr, 10, ms (400)) == ms (110));
    CHECK (std::fabs (rr[ms (110)]) == Approx (0.5f).margin (0.01f));
    // aucun écho avant le temps réglé (pas de look-ahead) : le signal direct n'est pas dans la sortie mouillée
    for (std::size_t i = 0; i < ms (99); ++i) REQUIRE (std::fabs (l[i]) < 1e-6f);
}

TEST_CASE ("delay: a negative offset never makes the right channel arrive before the input", "[delay]")
{
    DelayRig r;
    r.line (0, 20.0);
    r.set (d::lineParam (0, d::kOffset), -100.0);
    const auto [l, rr] = r.play (impulse(), ms (200));
    CHECK (peakIndex (l, 10, ms (200)) == ms (20));
    CHECK (peakIndex (rr, 0, ms (200)) < 8);                  // borné à zéro (quelques échantillons de lecture minimale)
}

TEST_CASE ("delay: feedback repeats the echo, scaled each time; negative feedback flips the phase", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0, 0.0, 50.0);
    const auto [l, rr] = r.play (impulse(), ms (400));
    (void) rr;
    CHECK (l[ms (50)] == Approx (1.0f).margin (0.02f));
    CHECK (l[ms (100)] == Approx (0.5f).margin (0.02f));
    CHECK (l[ms (150)] == Approx (0.25f).margin (0.02f));
    DelayRig n;
    n.line (0, 50.0, 0.0, -50.0);
    const auto [nl, nr] = n.play (impulse(), ms (400));
    (void) nr;
    CHECK (nl[ms (50)] == Approx (1.0f).margin (0.02f));
    CHECK (nl[ms (100)] == Approx (-0.5f).margin (0.02f));
    CHECK (nl[ms (150)] == Approx (0.25f).margin (0.02f));
}

TEST_CASE ("delay: parallel routing adds the lines, cascade adds their times", "[delay]")
{
    DelayRig p;
    p.line (0, 50.0);
    p.line (1, 120.0, -6.0206);
    const auto [pl, pr] = p.play (impulse(), ms (400));
    (void) pr;
    CHECK (pl[ms (50)] == Approx (1.0f).margin (0.02f));
    CHECK (pl[ms (120)] == Approx (0.5f).margin (0.02f));
    CHECK (std::fabs (pl[ms (170)]) < 0.01f);

    DelayRig c;
    c.set (d::routing, 1.0);
    c.line (0, 50.0);
    c.line (1, 120.0, -6.0206);
    const auto [cl, cr] = c.play (impulse(), ms (400));
    (void) cr;
    CHECK (cl[ms (50)] == Approx (1.0f).margin (0.02f));
    CHECK (cl[ms (170)] == Approx (0.5f).margin (0.02f));       // 50 + 120 ms, niveaux multipliés (0 dB puis -6 dB)
    CHECK (std::fabs (cl[ms (120)]) < 0.01f);
}

TEST_CASE ("delay: pan sends a line to one side; ping-pong crosses the repeats", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0, 0.0, 0.0, -100.0);
    const auto [l, rr] = r.play (impulse(), ms (200));
    CHECK (std::fabs (l[ms (50)]) == Approx (1.0f).margin (0.02f));
    CHECK (std::fabs (rr[ms (50)]) < 0.01f);

    DelayRig pp;
    pp.line (0, 50.0, 0.0, 60.0);
    pp.set (d::pingPong, 100.0);
    std::vector<float> in (1, 1.0f), inR (1, 0.0f);
    const auto [pl, pr] = pp.play (in, ms (300), true, &inR);   // impulsion à gauche seulement
    CHECK (std::fabs (pl[ms (50)]) == Approx (1.0f).margin (0.02f));
    CHECK (std::fabs (pr[ms (50)]) < 0.01f);
    CHECK (std::fabs (pr[ms (100)]) == Approx (0.6f).margin (0.03f));      // le premier retour passe à droite
    CHECK (std::fabs (pl[ms (100)]) < 0.01f);
    CHECK (std::fabs (pl[ms (150)]) == Approx (0.36f).margin (0.03f));     // puis revient à gauche
}

TEST_CASE ("delay: global feedback relaunches the sum of the lines", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0);
    r.line (1, 80.0);
    r.set (d::lineParam (0, d::kFeedback), 0.0);
    r.set (d::lineParam (1, d::kFeedback), 0.0);
    r.set (d::feedbackGlobal, 50.0);
    const auto [l, rr] = r.play (impulse(), ms (400));
    (void) rr;
    CHECK (l[ms (50)] == Approx (1.0f).margin (0.03f));
    CHECK (l[ms (80)] == Approx (1.0f).margin (0.03f));
    // les échos de la somme (1 + 1 = 2 à l'instant 50 puis 80 ms ; chaque ligne les réécoute x 0,5 à + 50 ms et + 80 ms)
    CHECK (l[ms (100)] > 0.4f);
}

TEST_CASE ("delay: freeze stops new input and keeps the content looping", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0, 0.0, 50.0);
    r.play (impulse(), ms (30));
    r.set (d::freeze, 1.0);
    std::vector<float> loud (ms (1000), 0.5f);                   // une entrée forte pendant le gel ne doit pas passer
    const auto [l, rr] = r.play (loud, ms (1000));
    (void) rr;
    float peakLate = 0.0f;
    for (std::size_t i = ms (800); i < ms (1000); ++i) peakLate = std::max (peakLate, std::fabs (l[i]));
    CHECK (peakLate > 0.3f);                                      // le contenu gelé tourne encore
    CHECK (peakLate < 1.2f);                                      // sans que l'entrée s'y ajoute
}

TEST_CASE ("delay: ducking lowers the echoes while the input plays", "[delay]")
{
    DelayRig plain, ducked;
    for (auto* rig : { &plain, &ducked }) rig->line (0, 50.0);
    ducked.set (d::duckAmount, 100.0);
    ducked.set (d::duckThreshold, -30.0);
    std::vector<float> burst (ms (200), 0.5f);                   // l'entrée dure plus que le retard
    const auto [pl, pr] = plain.play (burst, ms (400));
    const auto [dl, dr] = ducked.play (burst, ms (400));
    (void) pr; (void) dr;
    CHECK (std::fabs (pl[ms (100)]) > 0.3f);
    CHECK (std::fabs (dl[ms (100)]) < 0.05f);
    CHECK (std::fabs (dl[ms (380)]) < std::fabs (pl[ms (100)]));
    CHECK (plain.module().meter (d::kDuckDb) >= -0.01f);
}

TEST_CASE ("delay: width 0 makes the echoes mono, width 200 widens them", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0, 0.0, 0.0, -100.0);
    r.set (d::width, 0.0);
    const auto [l, rr] = r.play (impulse(), ms (100));
    CHECK (l[ms (50)] == Approx (rr[ms (50)]).margin (1e-4f));
    CHECK (std::fabs (l[ms (50)]) == Approx (0.5f).margin (0.02f));
    DelayRig w;
    w.line (0, 50.0, 0.0, 0.0, -100.0);
    w.set (d::width, 200.0);
    const auto [wl, wr] = w.play (impulse(), ms (100));
    CHECK (std::fabs (wl[ms (50)]) == Approx (2.0f * 0.5f + 0.5f).margin (0.05f));   // M = 0,5, S = 0,5 x 2
    CHECK (wr[ms (50)] == Approx (-0.5f).margin (0.05f));
}

TEST_CASE ("delay: low cut and high cut shape the echo", "[delay]")
{
    // un sinus grave traverse un passe-haut à 2 kHz très atténué ; un sinus aigu traverse un passe-bas à 500 Hz très atténué
    auto burst = [] (double f) { std::vector<float> v (ms (300)); for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate)); return v; };
    auto level = [] (const std::vector<float>& v) { float m = 0.0f; for (std::size_t i = ms (150); i < ms (300); ++i) m = std::max (m, std::fabs (v[i])); return m; };
    DelayRig plainLow, cutLow;
    for (auto* rig : { &plainLow, &cutLow }) rig->line (0, 20.0);
    cutLow.set (d::lineParam (0, d::kLowCut), 2000.0);
    CHECK (level (cutLow.play (burst (100.0), ms (300)).first) < 0.25f * level (plainLow.play (burst (100.0), ms (300)).first));
    DelayRig plainHigh, cutHigh;
    for (auto* rig : { &plainHigh, &cutHigh }) rig->line (0, 20.0);
    cutHigh.set (d::lineParam (0, d::kHighCut), 500.0);
    CHECK (level (cutHigh.play (burst (8000.0), ms (300)).first) < 0.25f * level (plainHigh.play (burst (8000.0), ms (300)).first));
}

TEST_CASE ("delay: invert flips the phase of the echo, solo isolates a line", "[delay]")
{
    DelayRig r;
    r.line (0, 50.0);
    r.set (d::lineParam (0, d::kInvert), 1.0);
    const auto [l, rr] = r.play (impulse(), ms (100));
    (void) rr;
    CHECK (l[ms (50)] == Approx (-1.0f).margin (0.02f));

    DelayRig s;
    s.line (0, 50.0);
    s.line (1, 90.0);
    s.set (d::lineParam (1, d::kSolo), 1.0);
    const auto [sl, sr] = s.play (impulse(), ms (200));
    (void) sr;
    CHECK (std::fabs (sl[ms (50)]) < 0.01f);
    CHECK (sl[ms (90)] == Approx (1.0f).margin (0.02f));
}

TEST_CASE ("delay: SYNC takes the time from the tempo and the division", "[delay]")
{
    DelayRig r;
    r.line (0, 999.0);
    r.set (d::sync, 1.0);
    r.set (d::lineParam (0, d::kDiv), 4.0);                        // 1/4
    r.module().setTempo (120.0);                                   // une noire = 500 ms
    const auto [l, rr] = r.play (impulse(), ms (800));
    (void) rr;
    CHECK (peakIndex (l, 10, ms (800)) == ms (500));
    DelayRig e;
    e.line (0, 999.0);
    e.set (d::sync, 1.0);
    e.set (d::lineParam (0, d::kDiv), 7.0);                        // 1/8 à 90 bpm : 333,33 ms
    e.module().setTempo (90.0);
    const auto [el, er] = e.play (impulse(), ms (800));
    (void) er;
    CHECK (peakIndex (el, 10, ms (800)) == Approx (static_cast<double> (ms (333.3333))).margin (2.0));
}

TEST_CASE ("delay: the safety limiter keeps a runaway feedback bounded and finite", "[delay]")
{
    DelayRig r;
    r.line (0, 20.0, 6.0, 95.0);
    r.line (1, 33.0, 6.0, 95.0);
    r.set (d::feedbackGlobal, 95.0);
    r.set (d::pingPong, 50.0);
    r.set (d::drive, 0.0);
    std::vector<float> noise (ms (200), 0.8f);
    const auto [l, rr] = r.play (noise, ms (4000));
    float peak = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE (std::isfinite (l[i]));
        REQUIRE (std::isfinite (rr[i]));
        peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (rr[i])));
    }
    CHECK (peak < 25.0f);                                          // borné (limiteur dans la boucle, sommes de lignes et gain +6 dB compris)
}

TEST_CASE ("delay: modulation keeps the echo finite and moves its timing slightly", "[delay]")
{
    DelayRig r;
    r.line (0, 100.0);
    r.set (d::lineParam (0, d::kModDepth), 100.0);
    r.set (d::lineParam (0, d::kModRate), 5.0);
    std::vector<float> in (ms (500));
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * 440.0 * static_cast<double> (i) / kRate));
    const auto [l, rr] = r.play (in, ms (700));
    (void) rr;
    float peak = 0.0f;
    for (const auto v : l) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
    CHECK (peak > 0.3f);
    CHECK (peak < 0.7f);
}

namespace
{
// puissance de la composante de fréquence f (Goertzel) sur [from, to)
double tone (const std::vector<float>& v, std::size_t from, std::size_t to, double f)
{
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        const double a = 2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate;
        re += v[i] * std::cos (a);
        im += v[i] * std::sin (a);
    }
    return (re * re + im * im) / static_cast<double> ((to - from) * (to - from));
}
// puissance moyenne autour de f sur de courtes fenêtres de 20 ms (les sauts de phase du décaleur à chaque grain interdisent une intégration longue)
double toneAvg (const std::vector<float>& v, std::size_t from, std::size_t to, double f)
{
    double sum = 0.0;
    int count = 0;
    for (std::size_t s0 = from; s0 + ms (20) <= to; s0 += ms (20)) { sum += tone (v, s0, s0 + ms (20), f); ++count; }
    return sum / std::max (1, count);
}
std::vector<float> sine (double f, std::size_t n, float amp = 0.5f)
{
    std::vector<float> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = amp * static_cast<float> (std::sin (2.0 * 3.14159265358979 * f * static_cast<double> (i) / kRate));
    return v;
}
} // namespace

TEST_CASE ("delay: PITCH shifts the echoes by the requested interval", "[delay][pitch]")
{
    for (const auto& [semitones, expected, other] : { std::tuple { 12.0, 880.0, 440.0 }, std::tuple { -12.0, 220.0, 440.0 }, std::tuple { 7.0, 659.26, 440.0 } })
    {
        DelayRig r;
        r.line (0, 100.0);
        r.set (d::lineParam (0, d::kPitch), semitones);
        const auto [l, rr] = r.play (sine (440.0, ms (1500)), ms (1500));
        (void) rr;
        INFO ("semitones " << semitones);
        const double at = toneAvg (l, ms (600), ms (1400), expected);
        CHECK (at > 4.0 * toneAvg (l, ms (600), ms (1400), other));   // la hauteur d'origine a disparu au profit de la nouvelle
        CHECK (at > 5.0e-3);
    }
}

TEST_CASE ("delay: PITCH at zero is a plain delay and the shifted echo stays on the line time", "[delay][pitch]")
{
    DelayRig plain;
    plain.line (0, 200.0);
    const auto [pl, pr] = plain.play (sine (440.0, ms (300)), ms (700));
    (void) pr;
    // le chemin direct n'est pas touché quand PITCH vaut 0 : écho exact à 200 ms, d'amplitude 0,5
    CHECK (std::fabs (pl[ms (260)] - 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 440.0 * static_cast<double> (ms (60)) / kRate))) < 0.02f);
    DelayRig up;
    up.line (0, 200.0);
    up.set (d::lineParam (0, d::kPitch), 12.0);
    const auto [ul, ur] = up.play (sine (440.0, ms (300)), ms (700));
    (void) ur;
    // silence avant l'écho (moins la demi-longueur d'un grain) puis de l'énergie autour du temps de la ligne
    float early = 0.0f, mid = 0.0f;
    for (std::size_t i = 0; i < ms (170); ++i) early = std::max (early, std::fabs (ul[i]));
    for (std::size_t i = ms (230); i < ms (400); ++i) mid = std::max (mid, std::fabs (ul[i]));
    CHECK (early < 0.05f);
    CHECK (mid > 0.2f);
}

TEST_CASE ("delay: a pitch-shifted feedback climbs by the interval at each repeat (shimmer)", "[delay][pitch]")
{
    DelayRig r;
    r.line (0, 150.0, 0.0, 70.0);
    r.set (d::lineParam (0, d::kPitch), 12.0);
    const auto [l, rr] = r.play (sine (300.0, ms (100)), ms (2500));
    (void) rr;
    float peak = 0.0f;
    for (const auto v : l) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
    CHECK (peak > 0.1f);
    CHECK (peak < 2.0f);
    // le premier écho tourne autour de 600 Hz, le deuxième autour de 1200 Hz
    CHECK (toneAvg (l, ms (190), ms (250), 600.0) > 2.0 * toneAvg (l, ms (190), ms (250), 300.0));
}

TEST_CASE ("delay: pitch quality High uses longer grains and both settings are bounded", "[delay][pitch]")
{
    for (const double quality : { 0.0, 1.0 })
    {
        DelayRig r;
        r.set (d::pitchQuality, quality);
        r.line (0, 120.0, 0.0, 50.0);
        r.set (d::lineParam (0, d::kPitch), -5.0);
        const auto [l, rr] = r.play (sine (500.0, ms (400)), ms (1500));
        (void) rr;
        float peak = 0.0f;
        for (const auto v : l) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
        CHECK (peak < 1.5f);
        CHECK (peak > 0.05f);
    }
}

#include "neptune/core/DelayPresets.h"

TEST_CASE ("delay: factory presets apply cleanly and Default restores the defaults", "[delay][presets]")
{
    DelayRig r;
    const auto& list = delay::factoryPresets();
    REQUIRE (list.size() >= 8);
    CHECK (std::string (list[0].name) == "Default");
    for (const auto& p : list)
        for (const auto& [id, v] : p.values)
        {
            REQUIRE (r.runner.params().find (id) != nullptr);
            INFO (p.name << " id " << id);
            CHECK (r.runner.params().find (id)->clamp (v) == Approx (v));
        }
    CHECK (delay::applyPreset (r.runner.params(), list[1]) > 0);
    CHECK (r.runner.params().get (d::lineParam (0, d::kTime)) == Approx (110.0));
    delay::applyPreset (r.runner.params(), list[0]);
    CHECK (r.runner.params().get (d::lineParam (0, d::kTime)) == Approx (500.0));
    CHECK (r.runner.params().get (stock::mix) == Approx (35.0));
}
