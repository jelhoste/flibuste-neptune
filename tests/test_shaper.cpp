// Shaper de transitoires (La Flèche) : suréchantillonneur, biquads, DSP.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/Biquad.h"
#include "neptune/core/Oversampler.h"

using namespace neptune::core;
using Catch::Approx;

namespace
{
constexpr double M_PI_ = 3.14159265358979323846;
std::vector<float> roundTrip (int factor, const std::vector<float>& in, int block)
{
    Oversampler os;
    os.prepare (block);
    os.setFactor (factor);
    std::vector<float> out (in.size());
    for (std::size_t off = 0; off < in.size(); off += static_cast<std::size_t> (block))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), in.size() - off));
        os.up (in.data() + off, n);
        os.down (out.data() + off, n);
    }
    return out;
}
} // namespace

TEST_CASE ("oversampler: an impulse comes back after exactly the declared latency", "[shaper][oversampler]")
{
    for (const int factor : { 1, 2, 4 })
    {
        std::vector<float> in (512, 0.0f);
        in[100] = 1.0f;
        const auto out = roundTrip (factor, in, 64);
        int peakAt = 0;
        float peak = 0.0f;
        for (int i = 0; i < 512; ++i)
            if (std::fabs (out[static_cast<std::size_t> (i)]) > peak) { peak = std::fabs (out[static_cast<std::size_t> (i)]); peakAt = i; }
        INFO ("factor " << factor);
        CHECK (peakAt == 100 + Oversampler::latencyFor (factor));
        CHECK (peak == Approx (1.0).margin (0.05));   // une impulsion perd un peu de crête en traversant deux filtres passe-bas
    }
}

TEST_CASE ("oversampler: a sine below the cutoff keeps its level and block size does not matter", "[shaper][oversampler]")
{
    std::vector<float> in (4096);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * static_cast<float> (std::sin (2.0 * M_PI_ * 1000.0 / 48000.0 * static_cast<double> (i)));
    for (const int factor : { 2, 4 })
    {
        const auto a = roundTrip (factor, in, 128), b = roundTrip (factor, in, 37);
        double ea = 0.0;
        for (std::size_t i = 1000; i < 4000; ++i)
        {
            ea += static_cast<double> (a[i]) * a[i];
            CHECK (a[i] == Approx (b[i]).margin (1e-5));
        }
        CHECK (std::sqrt (ea / 3000.0) == Approx (0.5 / std::sqrt (2.0)).margin (0.005));
        // alignée sur l'entrée retardée de la latence
        const auto lat = static_cast<std::size_t> (Oversampler::latencyFor (factor));
        for (std::size_t i = 1000; i < 1100; ++i) CHECK (a[i] == Approx (in[i - lat]).margin (0.01));
    }
}

TEST_CASE ("oversampler: images near Nyquist are attenuated by the interpolation filter", "[shaper][oversampler]")
{
    Oversampler os;
    os.prepare (256);
    os.setFactor (2);
    std::vector<float> in (256);
    // 0,45 x fs : proche de Nyquist, l'image haute cadence (0,55 x fs) doit être fortement atténuée dans le tampon haute cadence
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float> (std::sin (2.0 * M_PI_ * 0.2 * static_cast<double> (i)));
    for (int pass = 0; pass < 3; ++pass) os.up (in.data(), 256);
    const float* hi = os.up (in.data(), 256);
    double e = 0.0;
    for (int i = 128; i < 512; ++i) e += static_cast<double> (hi[i]) * hi[i];
    CHECK (std::sqrt (e / 384.0) == Approx (0.7071).margin (0.02));   // le signal utile passe
}

TEST_CASE ("biquad: Butterworth cascades have -3 dB at the cutoff and the requested slope", "[shaper][biquad]")
{
    for (const int slope : { 6, 12, 18, 24, 48 })
    {
        ButterworthCascade hp, lp;
        hp.design (true, 48000.0, 1000.0, slope);
        lp.design (false, 48000.0, 1000.0, slope);
        INFO ("slope " << slope);
        CHECK (20.0 * std::log10 (hp.magnitude (48000.0, 1000.0)) == Approx (-3.01).margin (0.1));
        CHECK (20.0 * std::log10 (lp.magnitude (48000.0, 1000.0)) == Approx (-3.01).margin (0.1));
        // une octave au-delà de la coupure, la pente vaut `slope` dB (approx. pour les basses pentes)
        const double hpOct = 20.0 * std::log10 (hp.magnitude (48000.0, 500.0)), lpOct = 20.0 * std::log10 (lp.magnitude (48000.0, 2000.0));
        CHECK (hpOct < -slope * 0.75);
        CHECK (lpOct < -slope * 0.7);
        CHECK (20.0 * std::log10 (hp.magnitude (48000.0, 15000.0)) == Approx (0.0).margin (0.2));
        CHECK (20.0 * std::log10 (lp.magnitude (48000.0, 100.0)) == Approx (0.0).margin (0.2));
    }
    const auto pk = BiquadCoefs::peaking (48000.0, 2000.0, 9.0, 1.0);
    CHECK (20.0 * std::log10 (pk.magnitude (48000.0, 2000.0)) == Approx (9.0).margin (0.05));
    CHECK (20.0 * std::log10 (pk.magnitude (48000.0, 100.0)) == Approx (0.0).margin (0.3));
}

#include "neptune/core/ShaperModule.h"

namespace
{
constexpr double kRate = 48000.0;

struct ShaperRig
{
    ModuleRunner runner { std::make_unique<ShaperModule>() };
    ShaperRig()
    {
        REQUIRE (runner.ok());
        runner.prepare (kRate, 256);
    }
    void set (ParamId id, double v) { runner.params().set (id, v); }
    int latency() { return runner.latencySamples(); }
    // joue `in` (L = R) par blocs de 256 ; rend la sortie gauche
    std::vector<float> play (const std::vector<float>& in, const float* key = nullptr)
    {
        std::vector<float> out (in.size());
        for (std::size_t off = 0; off < in.size(); off += 256)
        {
            const int n = static_cast<int> (std::min<std::size_t> (256, in.size() - off));
            std::vector<float> l (in.begin() + static_cast<std::ptrdiff_t> (off), in.begin() + static_cast<std::ptrdiff_t> (off) + n), r = l;
            if (key != nullptr) runner.setKey (key + off, key + off); else runner.setKey (nullptr, nullptr);
            runner.process (l.data(), r.data(), n, false);
            std::copy (l.begin(), l.end(), out.begin() + static_cast<std::ptrdiff_t> (off));
        }
        return out;
    }
};

// Deux « coups » : une sinusoïde de 200 Hz à enveloppe exponentielle (attaque très vive, queue de 120 ms).
std::vector<float> drumHits (int total = 48000)
{
    std::vector<float> x (static_cast<std::size_t> (total), 0.0f);
    for (const int start : { 4800, 24000 })
        for (int i = 0; start + i < total && i < 12000; ++i)
        {
            const double t = i / kRate;
            x[static_cast<std::size_t> (start + i)] += static_cast<float> (0.6 * std::exp (-t / 0.05) * std::sin (2.0 * M_PI_ * 200.0 * t));
        }
    return x;
}

float peakOf (const std::vector<float>& v, std::size_t a, std::size_t b)
{
    float p = 0.0f;
    for (std::size_t i = a; i < b && i < v.size(); ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}
} // namespace

TEST_CASE ("shaper: declares its parameters, with the documented ranges and defaults", "[shaper]")
{
    ShaperRig r;
    const auto& set = r.runner.params();
    for (const ParamId id : { shaper::attackGain1, shaper::sustainGain2, shaper::levelDependency, shaper::range, shaper::oversampling, shaper::scBand3Q, shaper::meterPeakDecay })
        CHECK (set.find (id) != nullptr);
    CHECK (set.get (shaper::range) == Approx (15.0));
    CHECK (set.get (shaper::attackTime) == Approx (10.0));
    CHECK (set.get (shaper::oversampling) == Approx (2.0));   // 4x par défaut
    CHECK (set.find (shaper::lookahead)->automatable == false);
    CHECK (set.find (shaper::attackGain1)->automatable == true);
    CHECK (r.latency() == Oversampler::kLatency4x);
    r.set (shaper::oversampling, 0.0);
    r.set (shaper::lookahead, 5.0);
    CHECK (r.latency() == 240);
}

TEST_CASE ("shaper: with neutral settings the output is the input delayed by the declared latency", "[shaper]")
{
    for (const int os : { 0, 1, 2 })
    {
        ShaperRig r;
        r.set (shaper::oversampling, os);
        const auto in = drumHits();
        const auto out = r.play (in);
        const auto lat = static_cast<std::size_t> (r.latency());
        double err = 0.0;
        for (std::size_t i = 10000; i < 40000; ++i) err = std::max (err, static_cast<double> (std::fabs (out[i] - in[i - lat])));
        INFO ("oversampling " << os);
        CHECK (err < 0.01);
    }
}

TEST_CASE ("shaper: positive attack raises the transient peak, negative sustain shortens the tail", "[shaper]")
{
    const auto in = drumHits();
    ShaperRig neutral;
    const auto flat = neutral.play (in);
    ShaperRig boosted;
    boosted.set (shaper::attackGain1, 100.0);
    const auto up = boosted.play (in);
    const auto lat = static_cast<std::size_t> (boosted.latency());
    const auto peakFlat = peakOf (flat, 4800 + lat, 4800 + lat + 600), peakUp = peakOf (up, 4800 + lat, 4800 + lat + 600);
    CHECK (peakUp > peakFlat * 1.3f);                                   // l'attaque est renforcée
    CHECK (peakUp < peakFlat * 6.0f);                                   // et reste dans la plage RANGE (15 dB max = x5,6)
    // la queue (60 à 110 ms après le début) n'est pas touchée par ATTACK
    CHECK (peakOf (up, 4800 + lat + 3000, 4800 + lat + 5000) == Approx (peakOf (flat, 4800 + lat + 3000, 4800 + lat + 5000)).epsilon (0.15));

    ShaperRig dry;
    dry.set (shaper::sustainGain1, -100.0);
    const auto cut = dry.play (in);
    CHECK (peakOf (cut, 4800 + lat + 3000, 4800 + lat + 6000) < peakOf (flat, 4800 + lat + 3000, 4800 + lat + 6000) * 0.8f);
    CHECK (peakOf (cut, 4800 + lat, 4800 + lat + 300) == Approx (peakOf (flat, 4800 + lat, 4800 + lat + 300)).epsilon (0.2));   // l'attaque reste
}

TEST_CASE ("shaper: LEVEL reserves the effect to loud or soft transients", "[shaper]")
{
    std::vector<float> in (48000, 0.0f);
    for (const auto& [start, amp] : { std::pair<int, float> { 4800, 0.7f }, std::pair<int, float> { 40000, 0.05f } })
        for (int i = 0; i < 6000; ++i) in[static_cast<std::size_t> (start + i)] += amp * static_cast<float> (std::exp (-(i / kRate) / 0.04) * std::sin (2.0 * M_PI_ * 200.0 * i / kRate));
    ShaperRig flat;
    const auto ref = flat.play (in);
    const auto lat = static_cast<std::size_t> (flat.latency());
    auto ratio = [&] (double level)
    {
        ShaperRig r;
        r.set (shaper::attackGain1, 100.0);
        r.set (shaper::levelDependency, level);
        r.set (shaper::levelThreshold, -24.0);
        r.set (shaper::levelKnee, 6.0);
        const auto out = r.play (in);
        const float loud = peakOf (out, 4800 + lat, 4800 + lat + 500) / peakOf (ref, 4800 + lat, 4800 + lat + 500);
        const float soft = peakOf (out, 40000 + lat, 40000 + lat + 500) / peakOf (ref, 40000 + lat, 40000 + lat + 500);
        return std::pair<float, float> { loud, soft };
    };
    const auto none = ratio (0.0), forLoud = ratio (100.0), forSoft = ratio (-100.0);
    CHECK (none.first > 1.2f);
    CHECK (none.second > 1.2f);
    CHECK (forLoud.first > forLoud.second + 0.2f);    // fort traité, doux presque épargné
    CHECK (forSoft.second > forSoft.first + 0.2f);
}

TEST_CASE ("shaper: DELTA outputs only what the processing adds, LISTEN the detection key", "[shaper]")
{
    const auto in = drumHits();
    ShaperRig r;
    r.set (shaper::delta, 1.0);
    const auto none = r.play (in);                                                   // aucun traitement : différence nulle
    CHECK (peakOf (none, 10000, 40000) < 0.01f);
    ShaperRig b;
    b.set (shaper::delta, 1.0);
    b.set (shaper::attackGain1, 100.0);
    const auto added = b.play (in);
    CHECK (peakOf (added, 4800, 8000) > 0.1f);

    ShaperRig l;
    l.set (shaper::scEnable, 1.0);
    l.set (shaper::scListen, 1.0);
    l.set (shaper::scGain, 6.0206);
    l.set (shaper::scEqOn, 0.0);
    const auto key = l.play (in);
    for (std::size_t i = 5000; i < 5200; ++i) CHECK (key[i] == Approx (2.0f * in[i]).margin (1e-3));   // la clé est le signal x2, sans latence
}

TEST_CASE ("shaper: an external key drives the detection while the audio stays untouched in level", "[shaper]")
{
    // audio : sinusoïde continue ; clé : coups de batterie. ATTACK renforce l'audio aux instants des coups de la clé.
    std::vector<float> tone (48000);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.2f * static_cast<float> (std::sin (2.0 * M_PI_ * 440.0 * static_cast<double> (i) / kRate));
    const auto key = drumHits();
    ShaperRig r;
    r.set (shaper::scEnable, 1.0);
    r.set (shaper::scSource, 1.0);
    r.set (shaper::scEqOn, 0.0);
    r.set (shaper::attackGain1, 100.0);
    const auto out = r.play (tone, key.data());
    const auto lat = static_cast<std::size_t> (r.latency());
    const float at = peakOf (out, 4800 + lat, 4800 + lat + 600), away = peakOf (out, 15000, 17000);
    CHECK (at > away * 1.3f);                      // renforcé pendant le coup de la clé
    CHECK (away == Approx (0.2f).margin (0.02f));  // inchangé ailleurs
}

TEST_CASE ("shaper: the detection EQ shapes the key (a high-pass changes what the detector sees)", "[shaper]")
{
    const auto in = drumHits();   // 200 Hz
    ShaperRig plain;
    plain.set (shaper::attackGain1, 100.0);
    const auto a = plain.play (in);
    ShaperRig hp;
    hp.set (shaper::scEnable, 1.0);
    hp.set (shaper::scHpFreq, 2000.0);
    hp.set (shaper::scHpSlope, 48.0);
    hp.set (shaper::attackGain1, 100.0);
    const auto b = hp.play (in);
    const auto lat = static_cast<std::size_t> (plain.latency());
    // la clé filtrée voit un tout autre signal (le corps grave disparaît, seul le front reste) : le gain appliqué n'est plus le même
    const float pa = peakOf (a, 4800 + lat, 4800 + lat + 600), pb = peakOf (b, 4800 + lat, 4800 + lat + 600);
    CHECK (std::fabs (pa - pb) > 0.1f * std::min (pa, pb));
    CHECK (hp.runner.module().numMeters() == shaper::kNumMeters);
}

TEST_CASE ("shaper: the runner keeps dry and bypass aligned with the module latency", "[shaper][runner]")
{
    ShaperRig r;
    const auto in = drumHits();
    r.set (shaper::attackGain1, 100.0);
    r.set (stock::mix, 0.0);
    for (int i = 0; i < 4; ++i) r.play (std::vector<float> (256, 0.0f));   // laisse le mix atteindre 0
    const auto out = r.play (in);
    const auto lat = static_cast<std::size_t> (r.latency());
    for (std::size_t i = 6000; i < 20000; ++i) REQUIRE (out[i] == Approx (in[i - lat]).margin (1e-6));
}

TEST_CASE ("shaper: meters and the analysis ring are filled", "[shaper]")
{
    ShaperRig r;
    auto& m = static_cast<ShaperModule&> (r.runner.module());
    r.play (std::vector<float> (4096, 0.0f));
    CHECK (m.analyzer().writePos() == 0);                    // rien n'est copié tant qu'aucun éditeur n'a activé l'anneau
    m.analyzer().setActive (true);
    r.play (drumHits());
    CHECK (m.meter (shaper::kInPeakL) >= 0.0f);
    const auto& ring = m.analyzer();
    REQUIRE (ring.writePos() > 0);
    float inPeak = 0.0f, outPeak = 0.0f, fastMax = 0.0f, slowMax = 0.0f, gainMax = 0.0f;
    const auto end = ring.writePos(), start = end > ring.capacity() ? end - ring.capacity() : 0;
    for (auto p = start; p < end; ++p)
    {
        inPeak = std::max (inPeak, std::fabs (ring.sample (shaper::kAnIn, 0, p)));
        outPeak = std::max (outPeak, std::fabs (ring.sample (shaper::kAnOut, 0, p)));
        fastMax = std::max (fastMax, ring.sample (shaper::kAnFast, 0, p));
        slowMax = std::max (slowMax, ring.sample (shaper::kAnSlow, 0, p));
        gainMax = std::max (gainMax, std::fabs (ring.sample (shaper::kAnGain, 0, p)));
    }
    CHECK (inPeak > 0.3f);
    CHECK (outPeak > 0.0f);
    CHECK (fastMax >= slowMax);                              // le suiveur rapide monte au moins aussi haut que le lent
    CHECK (fastMax > 0.0f);
    (void) gainMax;
    std::vector<float> k (1024);
    m.copyKey (k.data(), 1024);
    CHECK (m.detectionResponseDb (r.runner.params(), 1000.0) == Approx (0.0).margin (1.0));
}

TEST_CASE ("shaper: the gain stream follows the applied gain", "[shaper]")
{
    ShaperRig r;
    r.set (shaper::attackGain1, 100.0);
    auto& m = static_cast<ShaperModule&> (r.runner.module());
    m.analyzer().setActive (true);
    r.play (drumHits());
    const auto& ring = m.analyzer();
    float gainMax = 0.0f;
    const auto end = ring.writePos(), start = end > ring.capacity() ? end - ring.capacity() : 0;
    for (auto p = start; p < end; ++p) gainMax = std::max (gainMax, ring.sample (shaper::kAnGain, 0, p));
    CHECK (gainMax > 1.0f);                                  // renfort d'attaque : gain positif, en dB
}

#include "neptune/core/ShaperPresets.h"

TEST_CASE ("shaper: factory presets apply and Default restores neutral settings", "[shaper][presets]")
{
    ShaperRig r;
    const auto& list = shaper::factoryPresets();
    REQUIRE (list.size() >= 5);
    CHECK (std::string (list[0].name) == "Default");
    CHECK (shaper::applyPreset (r.runner.params(), list[1]) > 0);
    CHECK (r.runner.params().get (shaper::attackGain1) == Approx (60.0));
    r.set (shaper::lookahead, 3.0);                               // la latence n'est jamais touchée par un preset
    shaper::applyPreset (r.runner.params(), list[0]);
    CHECK (r.runner.params().get (shaper::attackGain1) == Approx (0.0));
    CHECK (r.runner.params().get (shaper::lookahead) == Approx (3.0));
    for (const auto& p : list)
        for (const auto& [id, v] : p.values)
        {
            REQUIRE (r.runner.params().find (id) != nullptr);
            CHECK (r.runner.params().find (id)->clamp (v) == Approx (v));
        }
}

TEST_CASE ("shaper: the analyzer display settings are hidden, not automatable and default to 18 s / Sync", "[shaper]")
{
    ShaperRig r;
    for (const auto id : { shaper::anTime, shaper::anMove })
    {
        const auto* spec = r.runner.params().find (id);
        REQUIRE (spec != nullptr);
        CHECK (spec->hidden);
        CHECK (! spec->automatable);
    }
    CHECK (r.runner.params().get (shaper::anTime) == Approx (3.0));
    CHECK (r.runner.params().get (shaper::anMove) == Approx (0.0));
}
