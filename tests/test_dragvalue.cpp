#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "neptune/core/DragValue.h"

using namespace neptune::core::dragvalue;
using Catch::Approx;

namespace
{
Range rate() { Range r; r.min = 0.01; r.max = 50.0; r.log = true; r.decimals = 2; r.def = 1.0; return r; }
Range percent() { Range r; r.min = 0.0; r.max = 100.0; r.step = 1.0; r.decimals = 0; return r; }
Range bipolar() { Range r; r.min = -100.0; r.max = 100.0; r.step = 1.0; r.decimals = 0; return r; }
}

TEST_CASE ("dragvalue: clamp and step rounding")
{
    CHECK (clamp (percent(), 150.0) == 100.0);
    CHECK (clamp (percent(), -3.0) == 0.0);
    CHECK (clamp (percent(), 12.4) == 12.0);
    CHECK (clamp (percent(), 12.6) == 13.0);
    CHECK (clamp (bipolar(), -99.6) == -100.0);
    CHECK (clamp (rate(), 7.123) == Approx (7.123));        // continu : pas d'arrondi
    CHECK (clamp (rate(), 0.0) == Approx (0.01));
    CHECK (clamp (rate(), 1000.0) == Approx (50.0));
    CHECK (clamp (rate(), std::nan ("")) == Approx (0.01)); // valeur invalide : le minimum
}

TEST_CASE ("dragvalue: normalised conversion, linear and logarithmic, round trip")
{
    CHECK (toNormalised (percent(), 0.0) == Approx (0.0));
    CHECK (toNormalised (percent(), 25.0) == Approx (0.25));
    CHECK (toNormalised (bipolar(), 0.0) == Approx (0.5));
    CHECK (fromNormalised (percent(), 0.5) == 50.0);
    CHECK (fromNormalised (percent(), 2.0) == 100.0);       // borné
    CHECK (fromNormalised (percent(), -1.0) == 0.0);
    // logarithmique : 0,01 -> 0, 50 -> 1, et le milieu est la moyenne géométrique
    CHECK (toNormalised (rate(), 0.01) == Approx (0.0));
    CHECK (toNormalised (rate(), 50.0) == Approx (1.0));
    CHECK (fromNormalised (rate(), 0.5) == Approx (std::sqrt (0.01 * 50.0)).epsilon (1e-9));
    for (const double v : { 0.01, 0.1, 1.0, 7.5, 50.0 }) CHECK (fromNormalised (rate(), toNormalised (rate(), v)) == Approx (v).epsilon (1e-9));
    for (const double v : { -100.0, -37.0, 0.0, 1.0, 100.0 }) CHECK (fromNormalised (bipolar(), toNormalised (bipolar(), v)) == v);
    Range degenerate;
    degenerate.min = degenerate.max = 5.0;
    CHECK (toNormalised (degenerate, 5.0) == 0.0);
}

TEST_CASE ("dragvalue: display text")
{
    CHECK (format (rate(), 1.0) == "1.00");
    CHECK (format (rate(), 12.3456) == "12.35");
    CHECK (format (percent(), 49.6) == "50");
    CHECK (format (bipolar(), -0.2) == "0");                // jamais « -0 »
    CHECK (format (bipolar(), -37.0) == "-37");
    Range r = rate();
    r.decimals = 99;                                        // borné à 6
    CHECK (format (r, 1.0) == "1.000000");
}

TEST_CASE ("dragvalue: parsing what the user types")
{
    double v = 0.0;
    CHECK (parse (rate(), "1,5", v));            CHECK (v == Approx (1.5));
    CHECK (parse (rate(), "1.5 Hz", v));         CHECK (v == Approx (1.5));
    CHECK (parse (rate(), "  3 hz ", v));        CHECK (v == Approx (3.0));
    CHECK (parse (rate(), "999", v));            CHECK (v == Approx (50.0));    // borné
    CHECK (parse (rate(), "0", v));              CHECK (v == Approx (0.01));
    CHECK (parse (bipolar(), "-20 %", v));       CHECK (v == -20.0);
    CHECK (parse (bipolar(), "-20.4", v));       CHECK (v == -20.0);            // arrondi au pas
    CHECK (parse (percent(), "+7", v));          CHECK (v == 7.0);
    CHECK_FALSE (parse (percent(), "", v));
    CHECK_FALSE (parse (percent(), "   ", v));
    CHECK_FALSE (parse (percent(), "abc", v));
    CHECK_FALSE (parse (percent(), "-", v));
    CHECK_FALSE (parse (percent(), "1 2", v));   // deux nombres : refusé
    CHECK_FALSE (parse (percent(), "5-3", v));
    CHECK_FALSE (parse (percent(), "1e400", v)); // infini : refusé
    v = 42.0;
    CHECK_FALSE (parse (percent(), "xyz", v));
    CHECK (v == 42.0);                           // `out` est laissé tel quel en cas d'échec
}

TEST_CASE ("dragvalue: dragging over the pill covers the whole range, Shift is fine, steps are respected")
{
    const auto p = percent();
    CHECK (dragged (p, 0.0, 300.0, 300.0, false) == 100.0);                 // toute la largeur = toute l'étendue
    CHECK (dragged (p, 0.0, 150.0, 300.0, false) == 50.0);
    CHECK (dragged (p, 0.5, -150.0, 300.0, false) == 0.0);
    CHECK (dragged (p, 0.5, 9999.0, 300.0, false) == 100.0);                // borné
    CHECK (dragged (p, 0.0, 150.0, 300.0, true) == 5.0);                    // réglage fin : un dixième
    CHECK (dragged (p, 0.5, 0.0, 300.0, false) == 50.0);                    // pas de déplacement : pas de changement
    CHECK (dragged (p, 0.0, 10.0, 5.0, false) == 50.0);                     // largeur minimale de 20 px (10 / 20)
    const auto r = rate();
    CHECK (dragged (r, toNormalised (r, 1.0), 0.0, 300.0, false) == Approx (1.0).epsilon (1e-9));
    CHECK (dragged (r, 0.0, 300.0, 300.0, false) == Approx (50.0));
    const double a = dragged (r, 0.0, 100.0, 300.0, false), b = dragged (r, 0.0, 200.0, 300.0, false);
    CHECK (b / a == Approx (a / 0.01).epsilon (1e-6));                      // logarithmique : rapports constants par distance
}

TEST_CASE ("dragvalue: wheel notches")
{
    CHECK (wheel (percent(), 50.0, 1, false) == 51.0);
    CHECK (wheel (percent(), 50.0, -3, false) == 47.0);
    CHECK (wheel (percent(), 100.0, 5, false) == 100.0);
    CHECK (wheel (percent(), 50.0, 0, false) == 50.0);
    const auto r = rate();
    CHECK (wheel (r, 1.0, 1, false) > 1.0);
    CHECK (wheel (r, 1.0, -1, false) < 1.0);
    CHECK (wheel (r, 1.0, 1, true) - 1.0 < wheel (r, 1.0, 1, false) - 1.0);  // fin : plus petit
    CHECK (wheel (r, 50.0, 1, false) == Approx (50.0));
}
