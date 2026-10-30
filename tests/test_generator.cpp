// Diapason : générateur de signaux de test et petite boîte à rythme (SignalGenerator.h).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "neptune/core/SignalGenerator.h"

using namespace neptune::core;
using Catch::Approx;
namespace g = neptune::core::generator;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Rig
{
    g::GeneratorEngine e;
    double sr;
    explicit Rig (double rate = 48000.0) : sr (rate)
    {
        REQUIRE (e.ok());
        e.prepare (rate, 512);
    }
    void set (ParamId id, double v) { e.params().set (id, v); }
    // Rend `n` échantillons par blocs de `block`, avec la position de transport donnée (ppq avancé de bloc en bloc si `t.valid`).
    std::pair<std::vector<float>, std::vector<float>> run (std::size_t n, g::Transport t = {}, int block = 512)
    {
        std::vector<float> l (n), r (n);
        for (std::size_t off = 0; off < n; off += static_cast<std::size_t> (block))
        {
            const int m = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), n - off));
            g::Transport tb = t;
            if (t.valid) tb.ppq = t.ppq + static_cast<double> (off) * t.bpm / 60.0 / sr;
            e.process (l.data() + off, r.data() + off, m, tb);
        }
        return { l, r };
    }
    std::size_t samples (double seconds) const { return static_cast<std::size_t> (std::lround (seconds * sr)); }
};

float peak (const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t> (-1))
{
    float p = 0.0f;
    for (std::size_t i = from; i < v.size() && i < to; ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}
double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to && i < v.size(); ++i) s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (to - from));
}
bool finite (const std::vector<float>& v)
{
    for (const float x : v) if (! std::isfinite (x)) return false;
    return true;
}
// Fréquence estimée par les passages à zéro montants dans [from, to).
double zeroCrossingFreq (const std::vector<float>& v, std::size_t from, std::size_t to, double sr)
{
    int count = 0;
    for (std::size_t i = from + 1; i < to && i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f) ++count;
    return count * sr / static_cast<double> (to - from);
}
// Puissance à la fréquence f (Goertzel) sur [from, to).
double power (const std::vector<float>& v, std::size_t from, std::size_t to, double f, double sr)
{
    const double w = 2.0 * kPi * f / sr, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < to; ++i) { const double s = v[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / static_cast<double> (to - from);
}
double bandPowerDb (const std::vector<float>& v, double f0, int bins, double sr)
{
    double acc = 0.0;
    for (int i = 0; i < bins; ++i) acc += power (v, 0, v.size(), f0 + 2.0 * i, sr);
    return 10.0 * std::log10 (acc / bins);
}
// Premier échantillon dont la valeur absolue dépasse `threshold` à partir de `from` ; -1 si aucun.
long long onset (const std::vector<float>& v, std::size_t from, float threshold)
{
    for (std::size_t i = from; i < v.size(); ++i) if (std::fabs (v[i]) > threshold) return static_cast<long long> (i);
    return -1;
}
void setStep (Rig& r, int drum, int step, int state) { r.set (g::stepParam (drum, step), state); }
} // namespace

TEST_CASE ("generator: les paramètres sont valides et l'état de départ est silencieux", "[generator]")
{
    Rig r;
    CHECK (r.e.params().size() > 190);
    std::set<ParamId> ids;
    for (std::size_t i = 0; i < r.e.params().size(); ++i) { CHECK (ids.insert (r.e.params().spec (i).id).second); CHECK (r.e.params().spec (i).validate().empty()); }
    const auto [l, rr] = r.run (4800);
    CHECK (peak (l) == 0.0f);
    CHECK (peak (rr) == 0.0f);
}

TEST_CASE ("generator: sinus continu, fréquence et niveau", "[generator]")
{
    for (const double rate : { 44100.0, 48000.0 })
    {
        Rig r (rate);
        r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 1000.0);
        const auto [l, rr] = r.run (r.samples (1.0));
        CHECK (finite (l));
        CHECK (peak (l, 2000) == Approx (1.0).margin (0.002));
        CHECK (zeroCrossingFreq (l, 4000, l.size() - 4000, rate) == Approx (1000.0).margin (2.0));
        CHECK (peak (rr, 2000) == Approx (1.0).margin (0.002));   // le centre envoie le même niveau des deux côtés
    }
    Rig r;
    r.set (g::on, 1); r.set (g::level, -12.0);
    const auto [l, rr] = r.run (24000);
    CHECK (peak (l, 4000) == Approx (std::pow (10.0, -12.0 / 20.0)).margin (0.002));
}

TEST_CASE ("generator: démarrage et arrêt sans saut", "[generator]")
{
    Rig r;
    r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 100.0);
    auto [l, rr] = r.run (2400);
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < l.size(); ++i) maxStep = std::max (maxStep, std::fabs (l[i] - l[i - 1]));
    CHECK (maxStep < 0.05f);   // un sinus de 100 Hz avance d'au plus 0,013 par échantillon
    r.set (g::on, 0);
    auto [l2, r2] = r.run (2400);
    maxStep = 0.0f;
    for (std::size_t i = 1; i < l2.size(); ++i) maxStep = std::max (maxStep, std::fabs (l2[i] - l2[i - 1]));
    CHECK (maxStep < 0.05f);
    CHECK (peak (l2, 600) == 0.0f);   // silence complet après le fondu de 4 ms
}

TEST_CASE ("generator: panoramique et polarité", "[generator]")
{
    Rig r;
    r.set (g::on, 1); r.set (g::level, 0.0);
    r.set (g::pan, -100);
    auto a = r.run (9600);
    CHECK (peak (a.first, 3000) == Approx (1.0).margin (0.003));
    CHECK (peak (a.second, 3000) < 1.0e-4f);
    r.set (g::pan, 100);
    auto b = r.run (9600);
    CHECK (peak (b.first, 3000) < 1.0e-4f);
    CHECK (peak (b.second, 3000) == Approx (1.0).margin (0.003));
    r.set (g::pan, 0);
    r.set (g::polarity, g::kPolRight);
    auto c = r.run (9600);
    for (std::size_t i = 3000; i < c.first.size(); i += 97) CHECK (c.second[i] == Approx (-c.first[i]).margin (1.0e-5));
}

TEST_CASE ("generator: carré à rapport cyclique réglable, dents de scie, triangle, continu", "[generator]")
{
    Rig r;
    r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 500.0);
    r.set (g::signal, g::kSquare); r.set (g::width, 25.0);
    auto sq = r.run (48000);
    std::size_t pos = 0, n = 0;
    for (std::size_t i = 4800; i < sq.first.size(); ++i) { ++n; if (sq.first[i] > 0.0f) ++pos; }
    CHECK (static_cast<double> (pos) / static_cast<double> (n) == Approx (0.25).margin (0.01));
    CHECK (peak (sq.first, 4800) < 1.2f);

    r.set (g::signal, g::kSawUp);
    auto up = r.run (9600);
    r.set (g::signal, g::kSawDown);
    auto down = r.run (9600);
    CHECK (finite (up.first));
    // la montante monte presque partout, la descendante descend presque partout
    int rising = 0, falling = 0;
    for (std::size_t i = 4800; i + 1 < up.first.size(); ++i) { if (up.first[i + 1] > up.first[i]) ++rising; if (down.first[i + 1] < down.first[i]) ++falling; }
    CHECK (rising > 9000 * 0.9 * 0.5);   // plus de 45 % des échantillons
    CHECK (falling > 9000 * 0.9 * 0.5);

    r.set (g::signal, g::kTriangle);
    auto tri = r.run (9600);
    CHECK (peak (tri.first, 4800) == Approx (1.0).margin (0.02));
    r.set (g::signal, g::kDc);
    r.set (g::level, -6.0);
    auto dc = r.run (9600);
    CHECK (dc.first.back() == Approx (std::pow (10.0, -6.0 / 20.0)).margin (0.001));
}

TEST_CASE ("generator: impulsions", "[generator]")
{
    Rig r;
    r.set (g::signal, g::kImpulse); r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 1000.0);
    auto [l, rr] = r.run (48000);
    int impulses = 0;
    for (std::size_t i = 4800; i < l.size(); ++i)
    {
        if (l[i] > 0.5f) ++impulses;
        else CHECK (std::fabs (l[i]) < 1.0e-4f);   // le reste est silencieux : une impulsion = un échantillon
    }
    CHECK (impulses == Approx (900).margin (2));   // 43 200 échantillons de mesure à 1 kHz = 900 impulsions
    CHECK (l[0] > 0.5f);                            // la première impulsion tombe au tout début, sans fondu
}

TEST_CASE ("generator: deux sinus", "[generator]")
{
    Rig r;
    r.set (g::signal, g::kTwoTone); r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 1000.0); r.set (g::freq2, 3000.0); r.set (g::level2, -12.0);
    auto [l, rr] = r.run (48000);
    const double p1 = power (l, 4800, l.size(), 1000.0, 48000.0), p2 = power (l, 4800, l.size(), 3000.0, 48000.0), p3 = power (l, 4800, l.size(), 2000.0, 48000.0);
    CHECK (10.0 * std::log10 (p1 / p2) == Approx (12.0).margin (0.3));
    CHECK (p3 < p1 * 1.0e-4);
    CHECK (peak (l, 4800) <= 1.001f);
}

TEST_CASE ("generator: bruits, niveau efficace et pente du spectre", "[generator]")
{
    struct C { int sig; double slopeDb; };   // pente attendue entre 500 Hz et 4 kHz (trois octaves)
    for (const C c : { C { g::kWhite, 0.0 }, C { g::kPink, -9.0 }, C { g::kBrown, -18.0 }, C { g::kBlue, 9.0 }, C { g::kViolet, 18.0 } })
    {
        Rig r;
        r.set (g::signal, c.sig); r.set (g::on, 1); r.set (g::level, -20.0);
        auto [l, rr] = r.run (96000);
        CHECK (finite (l));
        const double level = 20.0 * std::log10 (rms (l, 9600, l.size()));
        INFO ("signal " << g::signalName (c.sig));
        CHECK (level == Approx (-20.0).margin (0.6));
        const std::vector<float> body (l.begin() + 9600, l.end());
        const double lo = bandPowerDb (body, 500.0, 40, 48000.0), hi = bandPowerDb (body, 4000.0, 40, 48000.0);
        CHECK (hi - lo == Approx (c.slopeDb).margin(2.0));
    }
}

TEST_CASE ("generator: balayages logarithmique et linéaire", "[generator]")
{
    {
        Rig r;
        r.set (g::signal, g::kSweepLog); r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::sweepStart, 100.0); r.set (g::sweepEnd, 10000.0); r.set (g::sweepTime, 2.0);
        auto [l, rr] = r.run (96000);
        CHECK (zeroCrossingFreq (l, 45600, 50400, 48000.0) == Approx (1000.0).epsilon (0.05));   // au milieu : moyenne géométrique
        CHECK (zeroCrossingFreq (l, 4800, 9600, 48000.0) < 200.0);
        CHECK (peak (l, 4800) == Approx (1.0).margin (0.002));
    }
    {
        Rig r;
        r.set (g::signal, g::kSweepLin); r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::sweepStart, 1000.0); r.set (g::sweepEnd, 5000.0); r.set (g::sweepTime, 2.0);
        auto [l, rr] = r.run (96000);
        CHECK (zeroCrossingFreq (l, 45600, 50400, 48000.0) == Approx (3000.0).epsilon (0.03));
    }
}

TEST_CASE ("generator: mode déclenché", "[generator]")
{
    Rig r;
    r.set (g::mode, g::kTriggered); r.set (g::level, 0.0); r.set (g::length, 100.0);
    CHECK (peak (r.run (4800).first) == 0.0f);   // rien sans déclenchement
    r.e.postTrigger();
    auto [l, rr] = r.run (24000);
    const long long first = onset (l, 0, 0.001f);
    CHECK (first >= 0);
    long long last = -1;
    for (std::size_t i = 0; i < l.size(); ++i) if (std::fabs (l[i]) > 0.001f) last = static_cast<long long> (i);
    CHECK (last - first == Approx (4800).margin (200));   // 100 ms
    // le passage du paramètre « Trigger » à 1 déclenche aussi
    r.set (g::trigger, 1);
    auto [l2, r2] = r.run (4800);
    CHECK (peak (l2) > 0.5f);
    r.set (g::trigger, 0);
    // une note MIDI déclenche
    r.run (24000);
    r.e.noteOn (60, 1.0f);
    auto [l3, r3] = r.run (4800);
    CHECK (peak (l3) > 0.5f);
}

TEST_CASE ("generator: mode note MIDI, fréquence de la note", "[generator]")
{
    Rig r;
    r.set (g::mode, g::kMidi); r.set (g::level, 0.0);
    r.e.noteOn (69, 1.0f);
    auto [l, rr] = r.run (48000);
    CHECK (zeroCrossingFreq (l, 4800, 43200, 48000.0) == Approx (440.0).margin (1.5));
    r.e.noteOn (81, 1.0f);   // une octave plus haut, legato
    auto [l2, r2] = r.run (48000);
    CHECK (zeroCrossingFreq (l2, 4800, 43200, 48000.0) == Approx (880.0).margin (2.5));
    r.e.noteOff (81);
    auto [l3, r3] = r.run (48000);
    CHECK (zeroCrossingFreq (l3, 4800, 43200, 48000.0) == Approx (440.0).margin (1.5));   // la note tenue précédente reprend
    r.e.noteOff (69);
    auto [l4, r4] = r.run (4800);
    CHECK (peak (l4, 600) == 0.0f);
}

TEST_CASE ("generator: salves", "[generator]")
{
    Rig r;
    r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::freq, 1000.0); r.set (g::burstOn, 10.0); r.set (g::burstOff, 30.0);
    auto [l, rr] = r.run (48000);
    // période de 40 ms : 480 + 1440 échantillons ; sonne pendant 480 échantillons sur 1920
    int sounding = 0;
    for (std::size_t i = 9600; i < l.size(); ++i) if (std::fabs (l[i]) > 0.01f) ++sounding;
    CHECK (static_cast<double> (sounding) / static_cast<double> (l.size() - 9600) == Approx (0.25).margin (0.03));
}

TEST_CASE ("generator: tous les signaux restent finis et bornés", "[generator]")
{
    for (int mode = 0; mode < g::kNumModes; ++mode)
        for (int s = 0; s < g::kNumSignals; ++s)
        {
            Rig r;
            r.set (g::signal, s); r.set (g::mode, mode); r.set (g::on, 1); r.set (g::level, 0.0); r.set (g::sweepTime, 0.2);
            if (mode == g::kTriggered) r.e.postTrigger();
            if (mode == g::kMidi) r.e.noteOn (60, 1.0f);
            auto [l, rr] = r.run (24000, {}, 300);
            INFO ("signal " << s << " mode " << mode);
            CHECK (finite (l));
            CHECK (finite (rr));
            CHECK (peak (l) <= 1.3f);
        }
}

TEST_CASE ("generator: les huit sons de batterie répondent et s'éteignent", "[generator][drums]")
{
    for (int d = 0; d < g::kNumDrums; ++d)
    {
        Rig r;
        r.e.postDrum (d);
        auto [l, rr] = r.run (r.samples (4.0));
        INFO ("son " << g::drumName (d));
        CHECK (finite (l));
        CHECK (peak (l) > 0.1f);
        CHECK (peak (l) < 1.6f);
        CHECK (peak (l, r.samples (3.0)) == 0.0f);   // la voix est finie
        CHECK (l[0] == Approx (rr[0]).margin (1.0e-6));
    }
}

TEST_CASE ("generator: batterie, panoramique, sourdine, durée, charleston", "[generator][drums]")
{
    {
        Rig r;
        r.set (g::drumParam (g::kKick, g::kDrumPan), -100);
        r.e.postDrum (g::kKick);
        auto [l, rr] = r.run (24000);
        CHECK (peak (l) > 0.1f);
        CHECK (peak (rr) == 0.0f);
    }
    {
        Rig r;
        r.set (g::drumParam (g::kSnare, g::kDrumMute), 1);
        r.e.postDrum (g::kSnare);
        CHECK (peak (r.run (24000).first) == 0.0f);
    }
    {
        Rig a, b;
        b.set (g::drumParam (g::kTom, g::kDrumDecay), 400);
        a.e.postDrum (g::kTom);
        b.e.postDrum (g::kTom);
        const auto la = a.run (a.samples (1.0)).first, lb = b.run (b.samples (1.0)).first;
        CHECK (rms (lb, 24000, 36000) > 3.0 * rms (la, 24000, 36000));
    }
    {
        Rig open, choked;
        open.e.postDrum (g::kHatOpen);
        const auto lo = open.run (open.samples (0.5)).first;
        choked.e.postDrum (g::kHatOpen);
        choked.run (480);
        choked.e.postDrum (g::kHatClosed);
        const auto lc = choked.run (choked.samples (0.5)).first;
        CHECK (rms (lo, 14400, 24000) > 0.01);
        CHECK (rms (lc, 14400, 24000) < 0.1 * rms (lo, 14400, 24000));
    }
}

TEST_CASE ("generator: notes MIDI de la batterie", "[generator][drums]")
{
    Rig r;
    r.set (g::mode, g::kMidi);
    r.e.noteOn (36, 1.0f);   // grosse caisse
    auto [l, rr] = r.run (24000);
    CHECK (peak (l) > 0.1f);
    CHECK (zeroCrossingFreq (l, 9600, 24000, 48000.0) < 120.0);   // pas de signal de test aigu : seulement la grosse caisse
    r.set (g::drumNote, 48);
    r.e.noteOn (36, 1.0f);   // ce n'est plus une note de batterie : signal de test en mode MIDI
    auto [l2, r2] = r.run (9600);
    CHECK (zeroCrossingFreq (l2, 4800, 9600, 48000.0) == Approx (65.4).margin (12.0));
    r.e.noteOff (36);
    r.run (4800);
    r.e.noteOn (49, 0.5f);   // deuxième son
    CHECK (peak (r.run (9600).first) > 0.05f);
}

TEST_CASE ("generator: séquenceur interne, position des pas", "[generator][sequencer]")
{
    Rig r;
    r.set (g::seqSync, g::kSyncInternal); r.set (g::tempo, 120.0); r.set (g::steps, 16);
    setStep (r, g::kRim, 0, g::kStepAccent);
    setStep (r, g::kRim, 8, g::kStepNormal);
    r.set (g::seqRun, 1);
    auto [l, rr] = r.run (96000 + 6000, {}, 300);   // une mesure à 120 BPM = 2 s ; un pas = 6000 échantillons
    const long long a = onset (l, 0, 0.02f);
    const long long b = onset (l, 6000, 0.02f);
    CHECK (a >= 0);
    CHECK (a < 40);
    CHECK (b - 48000 >= -2);
    CHECK (b - 48000 < 40);
    const long long c = onset (l, 60000, 0.02f);
    CHECK (c - 96000 >= -2);   // la mesure suivante recommence
    CHECK (c - 96000 < 40);
    // l'accent est plus fort que le pas normal
    CHECK (peak (l, 0, 3000) > peak (l, 48000, 51000));
    r.set (g::seqRun, 0);
    r.run (48000);
    CHECK (r.e.currentStep() == -1);
    CHECK (peak (r.run (48000).first) == 0.0f);
}

TEST_CASE ("generator: séquenceur, nombre de pas et swing", "[generator][sequencer]")
{
    {
        Rig r;
        r.set (g::seqSync, g::kSyncInternal); r.set (g::tempo, 120.0); r.set (g::steps, 8);   // un pas = 12 000 échantillons
        setStep (r, g::kRim, 0, g::kStepNormal);
        setStep (r, g::kRim, 3, g::kStepNormal);
        r.set (g::seqRun, 1);
        auto [l, rr] = r.run (60000);
        const long long second = onset (l, 6000, 0.02f);
        CHECK (second - 36000 >= -2);
        CHECK (second - 36000 < 40);
    }
    {
        Rig r;
        r.set (g::seqSync, g::kSyncInternal); r.set (g::tempo, 120.0); r.set (g::steps, 16); r.set (g::swing, 50.0);
        setStep (r, g::kRim, 1, g::kStepNormal);
        r.set (g::seqRun, 1);
        auto [l, rr] = r.run (24000);
        const long long at = onset (l, 0, 0.02f);
        CHECK (at - (6000 + 1500) >= -2);   // retard de la moitié du demi-pas : 0,25 pas
        CHECK (at - (6000 + 1500) < 40);
    }
}

TEST_CASE ("generator: séquenceur calé sur l'hôte", "[generator][sequencer]")
{
    Rig r;
    r.set (g::seqSync, g::kSyncHost); r.set (g::steps, 16);
    setStep (r, g::kSnare, 4, g::kStepNormal);
    g::Transport stopped;
    stopped.valid = true; stopped.playing = false; stopped.bpm = 90.0;
    CHECK (peak (r.run (48000, stopped).first) == 0.0f);   // l'hôte est arrêté : rien
    CHECK (r.e.currentStep() == -1);
    g::Transport t = stopped;
    t.playing = true;
    t.ppq = 0.0;
    auto [l, rr] = r.run (200000, t, 480);
    // 90 BPM : une noire = 32 000 échantillons ; le pas 4 est le temps 2 de la mesure
    const double beat = 48000.0 * 60.0 / 90.0;
    const long long a = onset (l, 0, 0.02f);
    CHECK (a - static_cast<long long> (beat) >= -2);
    CHECK (a - static_cast<long long> (beat) < 60);
    // un saut de position (boucle) rejoue le pas
    g::Transport back = t;
    back.ppq = 0.0;
    r.run (4800, t, 480);
    CHECK (r.e.currentStep() >= 0);
}

TEST_CASE ("generator: arrêt d'urgence", "[generator]")
{
    Rig r;
    r.set (g::mode, g::kMidi); r.set (g::level, 0.0);
    r.e.noteOn (60, 1.0f);
    r.e.postDrum (g::kKick);
    r.run (4800);
    r.e.postPanic();
    r.run (480);
    CHECK (peak (r.run (4800).first) == 0.0f);
}
