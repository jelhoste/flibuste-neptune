// Réponses impulsionnelles d'usine de Convolution (IrGenerator.h) : durée, énergie, décroissance, décorrélation, déterminisme, ressort, chargement par le module.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "neptune/core/ConvolutionModule.h"
#include "neptune/core/IrGenerator.h"

using namespace neptune::core;
using Catch::Approx;

namespace
{
double energy (const std::vector<float>& v, std::size_t a, std::size_t b)
{
    double e = 0.0;
    for (std::size_t i = a; i < b && i < v.size(); ++i) e += static_cast<double> (v[i]) * v[i];
    return e;
}
// T20 extrapolé à 60 dB par intégration inverse de Schroeder (de -5 à -25 dB)
double rt60Of (const std::vector<float>& h, double sr)
{
    std::vector<double> edc (h.size());
    double acc = 0.0;
    for (std::size_t i = h.size(); i-- > 0;) { acc += static_cast<double> (h[i]) * h[i]; edc[i] = acc; }
    const double total = edc[0];
    double t5 = -1.0, t25 = -1.0;
    for (std::size_t i = 0; i < h.size(); ++i)
    {
        const double db = 10.0 * std::log10 (std::max (edc[i] / total, 1.0e-30));
        if (t5 < 0.0 && db <= -5.0) t5 = static_cast<double> (i) / sr;
        if (t25 < 0.0 && db <= -25.0) { t25 = static_cast<double> (i) / sr; break; }
    }
    return (t25 - t5) * 3.0;
}
double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { ab += static_cast<double> (a[i]) * b[i]; aa += static_cast<double> (a[i]) * a[i]; bb += static_cast<double> (b[i]) * b[i]; }
    return ab / std::sqrt (aa * bb);
}
} // namespace

TEST_CASE ("irgen: every factory IR is finite, stereo, unit energy, deterministic", "[irgen]")
{
    CHECK (irgen::entries().size() == 8);
    CHECK (irgen::isBuiltinPath ("builtin:hall"));
    CHECK_FALSE (irgen::isBuiltinPath ("/home/x/hall.wav"));
    CHECK (irgen::keyOfPath (irgen::pathFor ("plate")) == "plate");
    llc::IRBuffer none;
    CHECK_FALSE (irgen::generate ("nope", none));
    for (const auto& e : irgen::entries())
    {
        llc::IRBuffer a, b;
        REQUIRE (irgen::generate (e.key, a));
        REQUIRE (irgen::generate (e.key, b));
        INFO (e.key);
        REQUIRE (a.ch.size() == 2);
        CHECK (a.sampleRate == 48000.0);
        CHECK (a.ch[0].size() == a.ch[1].size());
        CHECK (a.ch[0].size() > 10000);
        CHECK (a.ch[0] == b.ch[0]);   // même clé, même IR
        for (const auto& c : a.ch)
        {
            for (const float v : c) REQUIRE (std::isfinite (v));
            CHECK (energy (c, 0, c.size()) == Approx (1.0).margin (0.15));
        }
        CHECK (std::fabs (correlation (a.ch[0], a.ch[1])) < 0.25);   // voies décorrélées : image large
    }
}

TEST_CASE ("irgen: the reverberation time follows the design", "[irgen]")
{
    struct C { const char* key; double rt60; };
    for (const C c : { C { "ambience", 0.35 }, C { "small_room", 0.6 }, C { "large_room", 1.1 }, C { "hall", 2.2 }, C { "large_hall", 3.4 }, C { "cathedral", 5.5 }, C { "plate", 1.8 } })
    {
        llc::IRBuffer ir;
        REQUIRE (irgen::generate (c.key, ir));
        INFO (c.key);
        const double rt = rt60Of (ir.ch[0], ir.sampleRate);
        CHECK (rt == Approx (c.rt60).epsilon (0.25));
        CHECK (static_cast<double> (ir.length()) / ir.sampleRate == Approx (c.rt60 * 1.05).margin (0.2));
    }
}

TEST_CASE ("irgen: the rooms start with a pre-delay of silence and the highs die first", "[irgen]")
{
    llc::IRBuffer hall;
    REQUIRE (irgen::generate ("hall", hall));
    const std::size_t pre = static_cast<std::size_t> (0.018 * 48000.0);
    CHECK (energy (hall.ch[0], 0, pre) == 0.0);   // pré-délai de 18 ms : rien avant
    CHECK (energy (hall.ch[0], pre, pre + 4800) > 0.0);
    // spectre : la queue est plus sombre que le début (rapport des variations d'une différence première, proxy des aigus)
    auto brightness = [] (const std::vector<float>& v, std::size_t a, std::size_t b)
    {
        double d = 0.0, e = 1.0e-20;
        for (std::size_t i = a + 1; i < b; ++i) { const double x = v[i] - v[i - 1]; d += x * x; e += static_cast<double> (v[i]) * v[i]; }
        return d / e;
    };
    CHECK (brightness (hall.ch[0], 4800, 9600) > 1.5 * brightness (hall.ch[0], 96000, 100800));
}

TEST_CASE ("irgen: the spring makes a train of dispersed echoes about 36 ms apart", "[irgen]")
{
    llc::IRBuffer sp;
    REQUIRE (irgen::generate ("spring", sp));
    const std::size_t T = static_cast<std::size_t> (0.036 * 48000.0), w = 96;
    // l'énergie autour d'un multiple de la période dépasse celle entre deux échos (premiers échos, avant que la dispersion ne les mêle)
    CHECK (energy (sp.ch[0], T - w, T + 4 * w) > 3.0 * energy (sp.ch[0], T / 2 - w, T / 2 + w));
    CHECK (energy (sp.ch[0], 0, 4 * w) > 0.0);
    CHECK (std::fabs (correlation (sp.ch[0], sp.ch[1])) < 0.35);
    CHECK (rt60Of (sp.ch[0], sp.sampleRate) == Approx (2.0).epsilon (0.4));
}

TEST_CASE ("irgen: Convolution loads a factory IR by its builtin path", "[irgen]")
{
    ConvolutionModule module;
    ModuleRunner runner (std::make_unique<ConvolutionModule>());
    auto& m = static_cast<ConvolutionModule&> (runner.module());
    runner.prepare (48000.0, 512);
    m.setIrPath (irgen::pathFor ("small_room"));
    bool loaded = false;
    for (int i = 0; i < 600 && ! loaded; ++i)
    {
        loaded = m.irStatus() == convolution::kLoaded;
        if (! loaded) std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    REQUIRE (loaded);
    CHECK (m.irLengthMs() > 300.0);
    m.setIrPath ("builtin:unknown");
    bool failed = false;
    for (int i = 0; i < 600 && ! failed; ++i)
    {
        failed = m.irStatus() == convolution::kError;
        if (! failed) std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    CHECK (failed);
}
