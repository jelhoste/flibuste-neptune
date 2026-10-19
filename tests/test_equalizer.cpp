// Égaliseur porté de ZL Equalizer : types de filtres et pentes, modes stéréo, bandes dynamiques, gain automatique, échelle de gain, réponse affichée.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "neptune/core/EqualizerModule.h"
#include "neptune/core/EqualizerPresets.h"

using namespace neptune::core;
using Catch::Approx;
namespace e = neptune::core::equalizer;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi_ = 3.14159265358979323846;

struct EqRig
{
    ModuleRunner runner { std::make_unique<EqualizerModule>() };
    EqRig()
    {
        REQUIRE (runner.ok());
        runner.prepare (kRate, 256);
    }
    EqualizerModule& eq() { return static_cast<EqualizerModule&> (runner.module()); }
    void set (ParamId id, double v) { runner.params().set (id, v); }
    void band (int b, e::Type type, double freq, double gain, double q = 0.707, int orderIdx = 1, int status = e::kOn, int stereo = 0)
    {
        set (e::bandParam (b, e::kStatus), status);
        set (e::bandParam (b, e::kType), type);
        set (e::bandParam (b, e::kFreq), freq);
        set (e::bandParam (b, e::kGain), gain);
        set (e::bandParam (b, e::kQ), q);
        set (e::bandParam (b, e::kOrder), orderIdx);
        set (e::bandParam (b, e::kStereo), stereo);
    }
    // sinus (gauche, droite en phase ou en opposition) ; rend le gain (dB) mesuré sur la fin
    std::pair<double, double> sine (double f, double amp = 0.1, double seconds = 0.6, double rightSign = 1.0, bool rightOn = true, const float* keyAmpSine = nullptr)
    {
        (void) keyAmpSine;
        const int blocks = static_cast<int> (seconds * kRate / 256.0);
        double sumInL = 0, sumOutL = 0, sumInR = 0, sumOutR = 0;
        long counter = 0;
        std::vector<float> l (256), r (256);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < 256; ++i)
            {
                const double x = amp * std::sin (2.0 * kPi_ * f * static_cast<double> (counter++) / kRate);
                l[static_cast<std::size_t> (i)] = static_cast<float> (x);
                r[static_cast<std::size_t> (i)] = rightOn ? static_cast<float> (x * rightSign) : 0.0f;
            }
            const auto inL = l, inR = r;
            runner.setKey (nullptr, nullptr);
            runner.process (l.data(), r.data(), 256, false);
            if (blk >= blocks * 3 / 4)
                for (int i = 0; i < 256; ++i)
                {
                    const auto k = static_cast<std::size_t> (i);
                    sumInL += inL[k] * inL[k]; sumOutL += l[k] * l[k];
                    sumInR += inR[k] * inR[k]; sumOutR += r[k] * r[k];
                }
        }
        auto db = [] (double o, double i) { return i < 1e-12 ? 0.0 : 10.0 * std::log10 (std::max (1e-18, o) / i); };
        return { db (sumOutL, sumInL), db (sumOutR, sumInR) };
    }
    double gain (double f, double amp = 0.1) { return sine (f, amp).first; }
};
} // namespace

TEST_CASE ("equalizer: with every band off the signal passes through unchanged", "[equalizer]")
{
    EqRig r;
    CHECK (r.gain (1000.0) == Approx (0.0).margin (1e-4));
    CHECK (r.eq().totalResponseDb (r.runner.params(), 1000.0, false) == Approx (0.0).margin (1e-9));
}

TEST_CASE ("equalizer: a peak band reaches its gain at the centre and spares far frequencies", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kPeak, 1000.0, 6.0, 1.0);
    CHECK (r.gain (1000.0) == Approx (6.0).margin (0.05));
    CHECK (r.gain (60.0) == Approx (0.0).margin (0.1));
    CHECK (r.gain (15000.0) == Approx (0.0).margin (0.3));
    CHECK (r.eq().bandResponseDb (r.runner.params(), 0, 1000.0, false) == Approx (6.0).margin (0.01));
    // la courbe affichée et la mesure concordent
    CHECK (r.eq().totalResponseDb (r.runner.params(), 1500.0, false) == Approx (r.gain (1500.0)).margin (0.05));
}

TEST_CASE ("equalizer: bandwidth follows Q for peak, notch and band pass", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kPeak, 1000.0, 12.0, 8.0);
    const double narrow = r.gain (1200.0);
    EqRig w;
    w.band (0, e::kPeak, 1000.0, 12.0, 0.5);
    CHECK (w.gain (1200.0) > narrow + 3.0);
    EqRig n;
    n.band (0, e::kNotch, 1000.0, 0.0, 4.0);
    CHECK (n.gain (1000.0) < -30.0);
    CHECK (n.gain (100.0) == Approx (0.0).margin (0.3));
    EqRig p;
    p.band (0, e::kBandPass, 1000.0, 0.0, 2.0);
    CHECK (p.gain (1000.0) == Approx (0.0).margin (0.3));
    CHECK (p.gain (100.0) < -12.0);
}

TEST_CASE ("equalizer: shelves and tilt", "[equalizer]")
{
    EqRig lo;
    lo.band (0, e::kLowShelf, 300.0, 6.0);
    CHECK (lo.gain (40.0) == Approx (6.0).margin (0.1));
    CHECK (lo.gain (8000.0) == Approx (0.0).margin (0.1));
    EqRig hi;
    hi.band (0, e::kHighShelf, 3000.0, -9.0);
    CHECK (hi.gain (12000.0) == Approx (-9.0).margin (0.2));
    CHECK (hi.gain (100.0) == Approx (0.0).margin (0.1));
    EqRig lo1;   // 6 dB/oct
    lo1.band (0, e::kLowShelf, 300.0, 6.0, 0.707, 0);
    CHECK (lo1.gain (30.0) == Approx (6.0).margin (0.3));
    CHECK (lo1.gain (300.0) == Approx (3.0).margin (0.3));
    EqRig hi1;
    hi1.band (0, e::kHighShelf, 3000.0, 6.0, 0.707, 0);
    CHECK (hi1.gain (20000.0) == Approx (6.0).margin (0.5));
    EqRig lo4;   // 24 dB/oct : pas de dépassement de plateau
    lo4.band (0, e::kLowShelf, 300.0, 6.0, 0.707, 2);
    CHECK (lo4.gain (30.0) == Approx (6.0).margin (0.2));
    EqRig tilt;
    tilt.band (0, e::kTiltShelf, 1000.0, 6.0);
    CHECK (tilt.gain (60.0) == Approx (-3.0).margin (0.3));
    CHECK (tilt.gain (16000.0) == Approx (3.0).margin (0.3));
}

TEST_CASE ("equalizer: pass filters have the requested slope", "[equalizer]")
{
    EqRig hp;
    hp.band (0, e::kHighPass, 1000.0, 0.0, 0.707, 1);   // 12 dB/oct
    CHECK (hp.gain (1000.0) == Approx (-3.0).margin (0.2));
    CHECK (hp.gain (250.0) == Approx (-24.1).margin (0.8));
    CHECK (hp.gain (10000.0) == Approx (0.0).margin (0.1));
    EqRig hp4;   // 48 dB/oct
    hp4.band (0, e::kHighPass, 1000.0, 0.0, 0.707, 4);
    CHECK (hp4.gain (500.0) < -40.0);
    CHECK (hp4.gain (1000.0) == Approx (-3.0).margin (0.5));
    EqRig lp1;   // 6 dB/oct
    lp1.band (0, e::kLowPass, 1000.0, 0.0, 0.707, 0);
    CHECK (lp1.gain (1000.0) == Approx (-3.0).margin (0.2));
    CHECK (lp1.gain (4000.0) == Approx (-12.3).margin (0.5));
    EqRig lp;   // résonance : Q 4 relève la crête
    lp.band (0, e::kLowPass, 1000.0, 0.0, 4.0, 1);
    CHECK (lp.gain (1000.0) > 8.0);
}

TEST_CASE ("equalizer: all pass keeps the magnitude and flat gain scales it", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kAllPass, 1000.0, 0.0, 0.707, 2);
    for (double f : { 80.0, 1000.0, 9000.0 }) CHECK (r.gain (f) == Approx (0.0).margin (0.05));
    EqRig g;
    g.band (0, e::kFlatGain, 1000.0, -7.0);
    CHECK (g.gain (500.0) == Approx (-7.0).margin (0.01));
}

TEST_CASE ("equalizer: stereo modes act on one side only", "[equalizer]")
{
    EqRig left;
    left.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kLeft);
    auto g = left.sine (1000.0);
    CHECK (g.first == Approx (6.0).margin (0.01));
    CHECK (g.second == Approx (0.0).margin (0.01));
    EqRig right;
    right.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kRight);
    g = right.sine (1000.0);
    CHECK (g.first == Approx (0.0).margin (0.01));
    CHECK (g.second == Approx (6.0).margin (0.01));
    // milieu : un signal en phase est relevé, un signal en opposition (côtés purs) ne l'est pas
    EqRig mid;
    mid.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kMid);
    CHECK (mid.sine (1000.0, 0.1, 0.6, 1.0).first == Approx (6.0).margin (0.01));
    EqRig mid2;
    mid2.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kMid);
    CHECK (mid2.sine (1000.0, 0.1, 0.6, -1.0).first == Approx (0.0).margin (0.01));
    EqRig side;
    side.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kSide);
    CHECK (side.sine (1000.0, 0.1, 0.6, -1.0).first == Approx (6.0).margin (0.01));
    EqRig side2;
    side2.band (0, e::kFlatGain, 1000.0, 6.0, 0.707, 1, e::kOn, e::kSide);
    CHECK (side2.sine (1000.0, 0.1, 0.6, 1.0).first == Approx (0.0).margin (0.01));
}

TEST_CASE ("equalizer: bypassed or off bands do nothing; several bands add up", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kPeak, 1000.0, 9.0, 1.0, 1, e::kBypassed);
    r.band (1, e::kPeak, 1000.0, 9.0, 1.0, 1, e::kOff);
    CHECK (r.gain (1000.0) == Approx (0.0).margin (1e-4));
    r.band (0, e::kPeak, 1000.0, 3.0, 1.0);
    r.band (1, e::kPeak, 1000.0, 4.0, 1.0);
    CHECK (r.gain (1000.0) == Approx (7.0).margin (0.05));
}

TEST_CASE ("equalizer: output gain, phase flip and gain scale", "[equalizer]")
{
    EqRig r;
    r.set (e::outputGain, -6.0);
    CHECK (r.gain (440.0) == Approx (-6.0).margin (0.05));
    EqRig s;
    s.band (0, e::kFlatGain, 1000.0, 8.0);
    s.set (e::gainScale, 50.0);
    CHECK (s.gain (1000.0) == Approx (4.0).margin (0.02));
    s.set (e::gainScale, -100.0);
    CHECK (s.gain (1000.0) == Approx (-8.0).margin (0.02));
    EqRig f;
    f.set (e::phaseFlip, 1.0);
    std::vector<float> a (256, 0.5f), b (256, 0.5f);
    for (int i = 0; i < 20; ++i) { std::fill (a.begin(), a.end(), 0.5f); std::fill (b.begin(), b.end(), 0.5f); f.runner.process (a.data(), b.data(), 256, false); }
    CHECK (a[255] == Approx (-0.5f).margin (1e-5));
}

TEST_CASE ("equalizer: auto gain compensates a boost", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kPeak, 1000.0, 12.0, 0.707);
    const double plain = r.gain (1000.0, 0.1);
    EqRig a;
    a.band (0, e::kPeak, 1000.0, 12.0, 0.707);
    a.set (e::autoGain, 1.0);
    const double compensated = a.sine (1000.0, 0.1, 2.0).first;
    CHECK (compensated < plain - 1.0);
    CHECK (a.eq().meter (e::kAutoGainDb) < -1.0f);
    // un gain plat est compensé exactement
    EqRig g;
    g.band (0, e::kFlatGain, 1000.0, 6.0);
    g.set (e::autoGain, 1.0);
    CHECK (g.sine (1000.0, 0.1, 2.0).first == Approx (0.0).margin (0.1));
}

TEST_CASE ("equalizer: a dynamic band slides to its target gain above the threshold", "[equalizer]")
{
    auto make = [] (double target)
    {
        auto r = std::make_unique<EqRig>();
        r->band (0, e::kPeak, 1000.0, 0.0, 1.0);
        r->set (e::bandParam (0, e::kDynOn), 1.0);
        r->set (e::bandParam (0, e::kTarget), target);
        r->set (e::bandParam (0, e::kThreshold), -30.0);
        r->set (e::bandParam (0, e::kKnee), 4.0);
        r->set (e::bandParam (0, e::kAttack), 5.0);
        r->set (e::bandParam (0, e::kRelease), 50.0);
        return r;
    };
    auto cut = make (-12.0);
    CHECK (cut->sine (1000.0, 0.5, 1.0).first < -9.0);                   // fort (-6 dBFS) : proche du gain cible, -12 dB
    CHECK (cut->eq().meter (e::kBandGain0) == Approx (-12.0f).margin (0.5f));
    auto quiet = make (-12.0);
    CHECK (quiet->sine (1000.0, 0.001, 1.0).first == Approx (0.0).margin (0.1));   // faible (-60 dBFS) : gain de base 0 dB
    auto boost = make (9.0);
    CHECK (boost->sine (1000.0, 0.5, 1.0).first > 6.0);
    // la détection est filtrée : un son loin de la bande ne déclenche pas
    auto off = make (-12.0);
    CHECK (off->sine (5000.0, 0.5, 1.0).first == Approx (0.0).margin (1.0));
    // dynamique contournée : le gain reste le gain de base
    auto held = make (-12.0);
    held->set (e::bandParam (0, e::kDynBypass), 1.0);
    CHECK (held->sine (1000.0, 0.5, 1.0).first == Approx (0.0).margin (0.05));
}

TEST_CASE ("equalizer: dynamic detection can use the external key", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kFlatGain, 1000.0, 0.0);
    r.set (e::bandParam (0, e::kDynOn), 1.0);
    r.set (e::bandParam (0, e::kTarget), -12.0);
    r.set (e::bandParam (0, e::kThreshold), -30.0);
    r.set (e::bandParam (0, e::kKnee), 4.0);
    r.set (e::bandParam (0, e::kAttack), 5.0);
    r.set (e::bandParam (0, e::kRelease), 50.0);
    r.set (e::bandParam (0, e::kSideType), e::kSideAll);
    r.set (e::externalSide, 1.0);
    std::vector<float> l (256), rr (256), kl (256), kr (256);
    double sumIn = 0, sumOut = 0;
    for (int blk = 0; blk < 200; ++blk)
    {
        for (int i = 0; i < 256; ++i)
        {
            const double x = 0.05 * std::sin (2.0 * kPi_ * 300.0 * (blk * 256 + i) / kRate);
            l[static_cast<std::size_t> (i)] = rr[static_cast<std::size_t> (i)] = static_cast<float> (x);
            kl[static_cast<std::size_t> (i)] = kr[static_cast<std::size_t> (i)] = blk >= 100 ? 0.5f : 0.0f;   // clé forte dans la 2e moitié
        }
        const auto in = l;
        r.runner.setKey (kl.data(), kr.data());
        r.runner.process (l.data(), rr.data(), 256, false);
        if (blk == 90 || blk == 199)
        {
            sumIn = sumOut = 0;
            for (int i = 0; i < 256; ++i) { sumIn += in[static_cast<std::size_t> (i)] * in[static_cast<std::size_t> (i)]; sumOut += l[static_cast<std::size_t> (i)] * l[static_cast<std::size_t> (i)]; }
            const double g = 10.0 * std::log10 (sumOut / sumIn);
            if (blk == 90) CHECK (g == Approx (0.0).margin (0.1)); else CHECK (g < -9.0);
        }
    }
}

TEST_CASE ("equalizer: filter design helpers and compensation", "[equalizer]")
{
    std::array<BiquadCoefs, e::kMaxSections> cf {};
    double scale = 1.0;
    CHECK (e::designFilter (e::kPeak, 2, kRate, 1000.0, 6.0, 1.0, cf, scale) == 1);
    CHECK (e::designFilter (e::kHighPass, 16, kRate, 1000.0, 0.0, 0.707, cf, scale) == 8);
    CHECK (e::designFilter (e::kPeak, 8, kRate, 1000.0, 6.0, 1.0, cf, scale) == 8);   // paire d'étagères d'ordre 4
    CHECK (e::designFilter (e::kPeak, 1, kRate, 1000.0, 6.0, 1.0, cf, scale) == 0);
    CHECK (e::designFilter (e::kFlatGain, 2, kRate, 1000.0, 6.0, 1.0, cf, scale) == 0);
    CHECK (scale == Approx (2.0).margin (0.01));
    CHECK (e::gainCompensationDb (e::kPeak, 1000.0, 6.0, 1.0) < 0.0);
    CHECK (e::gainCompensationDb (e::kPeak, 1000.0, -6.0, 1.0) > 0.0);
    CHECK (e::gainCompensationDb (e::kHighPass, 1000.0, 0.0, 1.0) == 0.0);
    CHECK (e::gainCompensationDb (e::kFlatGain, 1000.0, 5.0, 1.0) == Approx (-5.0));
}

TEST_CASE ("equalizer: high order peak still reaches its gain at the centre", "[equalizer]")
{
    EqRig r;
    r.band (0, e::kPeak, 1000.0, 9.0, 1.0, 4);   // 48 dB/oct
    CHECK (r.gain (1000.0) == Approx (9.0).margin (0.3));
    CHECK (r.gain (100.0) == Approx (0.0).margin (0.3));
    CHECK (r.gain (10000.0) == Approx (0.0).margin (0.3));
}

TEST_CASE ("equalizer: factory presets only name declared parameters, within range", "[equalizer]")
{
    ModuleRunner runner (std::make_unique<EqualizerModule>());
    REQUIRE (runner.ok());
    const auto& p = runner.params();
    CHECK (e::factoryPresets().size() >= 5);
    for (const auto id : e::presetParams()) CHECK (p.find (id) != nullptr);
    for (const auto& preset : e::factoryPresets())
        for (const auto& [id, v] : preset.values)
        {
            const auto* spec = p.find (id);
            REQUIRE (spec != nullptr);
            CHECK (spec->clamp (v) == Approx (v));
        }
}

TEST_CASE ("equalizer: analyzer ring carries input and output only when active, spectrum settings are hidden", "[equalizer][analyzer]")
{
    EqRig r;
    auto* m = static_cast<EqualizerModule*> (&r.runner.module());
    auto& ring = m->analyzer();
    const auto& p = r.runner.params();
    for (const auto id : { e::spOn, e::spQuality, e::spSpeed, e::spTilt, e::spSmoothType, e::spSmoothOct, e::spSmoothErb, e::spTop, e::spMin, e::spChannels, e::spShowIn, e::spShowOut })
    {
        const auto* s = p.find (id);
        REQUIRE (s != nullptr);
        CHECK (s->hidden);
        CHECK_FALSE (s->automatable);
    }
    CHECK (p.get (e::spOn) == 1.0);
    CHECK (p.get (e::spTilt) == 3.0);          // 4,5 dB/oct
    CHECK (p.get (e::spSmoothType) == 1.0);    // ERB
    std::vector<float> a (256, 0.5f), b (256, 0.25f);
    r.runner.process (a.data(), b.data(), 256, false);
    CHECK (ring.writePos() == 0);
    ring.setActive (true);
    std::vector<float> c (256, 0.5f), d (256, 0.25f);
    r.runner.process (c.data(), d.data(), 256, false);
    REQUIRE (ring.writePos() == 256);
    CHECK (ring.sample (e::kSpIn, 0, 10) == Approx (0.5f));
    CHECK (ring.sample (e::kSpIn, 1, 10) == Approx (0.25f));
    CHECK (ring.sample (e::kSpOut, 0, 10) == Approx (c[10]));     // sortie : le signal traité
}
