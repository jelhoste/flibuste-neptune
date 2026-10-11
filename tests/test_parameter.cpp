#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

#include "neptune/core/Parameter.h"

using namespace neptune::core;
using Catch::Approx;

TEST_CASE ("validation des définitions")
{
    CHECK (ParamSpec::continuous (1, "x", Unit::None, 0, 1, 0.5).validate().empty());
    CHECK_FALSE (ParamSpec::continuous (1, "", Unit::None, 0, 1, 0.5).validate().empty());               // nom vide
    CHECK_FALSE (ParamSpec::continuous (1, "x", Unit::None, 1, 1, 1).validate().empty());                // min == max
    CHECK_FALSE (ParamSpec::continuous (1, "x", Unit::None, 0, 1, 2).validate().empty());                // défaut hors plage
    CHECK_FALSE (ParamSpec::continuous (1, "f", Unit::Hertz, 0, 20000, 1000, Curve::Exponential).validate().empty()); // min = 0
    CHECK_FALSE (ParamSpec::continuous (1, "p", Unit::None, 0, 1, 0, Curve::Power, 0.0).validate().empty());
    CHECK_FALSE (ParamSpec::choice (1, "c", { "seul" }).validate().empty());
    CHECK_FALSE (ParamSpec::stepped (1, "s", Unit::None, 0, 10, 20).validate().empty());
}

TEST_CASE ("correspondance valeur <-> normalisé : aller-retour")
{
    const std::vector<ParamSpec> specs = {
        ParamSpec::continuous (1, "lin", Unit::Decibel, -60, 12, 0),
        ParamSpec::continuous (2, "exp", Unit::Hertz, 20, 20000, 1000, Curve::Exponential),
        ParamSpec::continuous (3, "pow", Unit::Milliseconds, 0, 2000, 100, Curve::Power, 2.5),
    };
    for (const auto& s : specs)
    {
        for (double n : { 0.0, 0.1, 0.25, 0.5, 0.75, 1.0 })
        {
            const double v = s.fromNormalized (n);
            CHECK (v >= s.min);
            CHECK (v <= s.max);
            CHECK (s.toNormalized (v) == Approx (n).margin (1e-9));
        }
        CHECK (s.fromNormalized (0.0) == Approx (s.min));
        CHECK (s.fromNormalized (1.0) == Approx (s.max));
    }
}

TEST_CASE ("courbes : valeurs connues")
{
    const auto freq = ParamSpec::continuous (1, "f", Unit::Hertz, 20, 20000, 1000, Curve::Exponential);
    CHECK (freq.fromNormalized (0.5) == Approx (632.456).epsilon (1e-4)); // moyenne géométrique de 20 et 20000

    const auto pre = ParamSpec::continuous (2, "pre", Unit::Milliseconds, 0, 2000, 0, Curve::Power, 2.5);
    CHECK (pre.fromNormalized (0.5) == Approx (2000.0 * std::pow (0.5, 2.5)));
}

TEST_CASE ("paramètres discrets : arrondi et bornes")
{
    const auto st = ParamSpec::stepped (1, "alg", Unit::None, 1, 32, 1);
    CHECK (st.clamp (4.4) == 4.0);
    CHECK (st.clamp (4.6) == 5.0);
    CHECK (st.clamp (-3) == 1.0);
    CHECK (st.clamp (99) == 32.0);
    CHECK (st.fromNormalized (1.0) == 32.0);
    CHECK (st.toNormalized (1.0) == 0.0);

    const auto tg = ParamSpec::toggle (2, "mute");
    CHECK (tg.fromNormalized (0.49) == 0.0);
    CHECK (tg.fromNormalized (0.51) == 1.0);
}

TEST_CASE ("valeurs non finies : retour au défaut, jamais de NaN en sortie")
{
    const auto s = ParamSpec::continuous (1, "x", Unit::None, 0, 10, 3);
    CHECK (s.clamp (std::nan ("")) == 3.0);
    CHECK (s.fromNormalized (std::nan ("")) == Approx (3.0));
    CHECK (s.clamp (std::numeric_limits<double>::infinity()) == 3.0);
}

TEST_CASE ("ParameterSet : ajout, doublons, recherche")
{
    ParameterSet ps;
    std::string err;
    CHECK (ps.add (ParamSpec::continuous (10, "vol", Unit::Decibel, -60, 6, 0), &err));
    CHECK (ps.add (ParamSpec::toggle (5, "mute")));
    CHECK_FALSE (ps.add (ParamSpec::toggle (10, "autre"), &err)); // id en double
    CHECK_FALSE (err.empty());
    CHECK_FALSE (ps.add (ParamSpec::continuous (11, "", Unit::None, 0, 1, 0), &err));

    CHECK (ps.size() == 2);
    CHECK (ps.find (10) != nullptr);
    CHECK (ps.find (10)->name == "vol");
    CHECK (ps.find (99) == nullptr);
    CHECK (ps.indexOf (99) == ParameterSet::npos);
}

TEST_CASE ("ParameterSet : valeurs, version, reset")
{
    ParameterSet ps;
    ps.add (ParamSpec::continuous (1, "vol", Unit::Decibel, -60, 6, 0));

    CHECK (ps.get (1) == 0.0);
    CHECK (ps.version (1) == 0);

    CHECK (ps.set (1, -12.0));
    CHECK (ps.get (1) == -12.0);
    CHECK (ps.version (1) == 1);

    CHECK_FALSE (ps.set (1, -12.0)); // inchangé : pas d'incrément
    CHECK (ps.version (1) == 1);

    CHECK (ps.set (1, 100.0)); // borné
    CHECK (ps.get (1) == 6.0);

    CHECK (ps.reset (1));
    CHECK (ps.get (1) == 0.0);

    CHECK_FALSE (ps.set (42, 1.0)); // id inconnu
    CHECK (ps.get (42) == 0.0);

    ps.setNormalized (1, 0.0);
    CHECK (ps.get (1) == -60.0);
    CHECK (ps.getNormalized (1) == Approx (0.0));
}

TEST_CASE ("texte : affichage")
{
    ParameterSet ps;
    auto dry = ParamSpec::continuous (1, "dry", Unit::Decibel, -100, 30, -100);
    dry.minText = "-inf";
    ps.add (dry);
    ps.add (ParamSpec::continuous (2, "freq", Unit::Hertz, 20, 20000, 1000, Curve::Exponential));
    ps.add (ParamSpec::toggle (3, "mute"));
    ps.add (ParamSpec::choice (4, "mode", { "STEREO", "SUM", "LEFT", "RIGHT" }, 1));
    ps.add (ParamSpec::stepped (5, "alg", Unit::None, 1, 32, 5));

    CHECK (ps.toText (1) == "-inf");
    ps.set (1, -6.0);
    CHECK (ps.toText (1) == "-6.0 dB");
    CHECK (ps.toText (2) == "1.00 kHz");
    ps.set (2, 440.0);
    CHECK (ps.toText (2) == "440 Hz");
    CHECK (ps.toText (3) == "OFF");
    CHECK (ps.toText (4) == "SUM");
    CHECK (ps.toText (5) == "5");
}

TEST_CASE ("texte : saisie")
{
    ParameterSet ps;
    ps.add (ParamSpec::continuous (1, "vol", Unit::Decibel, -100, 30, 0));
    ps.add (ParamSpec::continuous (2, "freq", Unit::Hertz, 20, 20000, 1000, Curve::Exponential));
    ps.add (ParamSpec::continuous (3, "time", Unit::Milliseconds, 0, 5000, 10));
    ps.add (ParamSpec::toggle (4, "mute"));
    ps.add (ParamSpec::choice (5, "mode", { "STEREO", "SUM", "LEFT", "RIGHT" }));

    double v = 0.0;
    CHECK (ps.fromText (1, " -6.5 dB ", v));
    CHECK (v == Approx (-6.5));
    CHECK (ps.fromText (1, "-inf", v));
    CHECK (v == -100.0);
    CHECK (ps.fromText (1, "500", v)); // hors plage : borné
    CHECK (v == 30.0);
    CHECK (ps.fromText (2, "2.5k", v));
    CHECK (v == Approx (2500.0));
    CHECK (ps.fromText (2, "1.2 kHz", v));
    CHECK (v == Approx (1200.0));
    CHECK (ps.fromText (3, "1.5 s", v));
    CHECK (v == Approx (1500.0));
    CHECK (ps.fromText (4, "on", v));
    CHECK (v == 1.0);
    CHECK (ps.fromText (5, "left", v));
    CHECK (v == 2.0);
    CHECK_FALSE (ps.fromText (1, "abc", v));
    CHECK_FALSE (ps.fromText (1, "", v));
    CHECK_FALSE (ps.fromText (99, "1", v));
}

TEST_CASE ("accès concurrent : écritures et lectures simultanées restent cohérentes")
{
    ParameterSet ps;
    ps.add (ParamSpec::continuous (1, "x", Unit::None, 0, 100, 0));

    std::atomic<bool> stop { false };
    std::atomic<int> bad { 0 };
    std::thread reader ([&] {
        while (! stop.load())
        {
            const double v = ps.get (1);
            if (v < 0.0 || v > 100.0) ++bad;
        }
    });
    for (int i = 0; i < 200000; ++i) ps.set (1, (i % 101) * 1.0);
    stop = true;
    reader.join();

    CHECK (bad.load() == 0);
    CHECK (ps.version (1) > 0);
}
