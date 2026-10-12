// Tests de la disposition de l'enveloppe à l'écran (palier 1).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "neptune/core/EnvelopeLayout.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
EnvelopeDescription withTimes (double a, double h, double d, double r)
{
    EnvelopeDescription e;
    e.attack = { a > 0.0, a, {} };
    e.hold = { h > 0.0, h, {} };
    e.decay = { d > 0.0, d, {} };
    e.release = { r > 0.0, r, {} };
    e.sustain = 0.5;
    return e;
}
}

TEST_CASE ("layout: a hold of 0 ms has no width and the attack starts at the origin")
{
    const auto l = computeEnvelopeLayout (withTimes (10.0, 0.0, 200.0, 50.0), 600.0);
    CHECK (l.x0[1] == 0.0);          // attaque tout de suite au début
    CHECK (l.x1[2] == l.x0[2]);      // hold sans largeur
    CHECK (l.x0[2] == l.x1[1]);
    CHECK (l.x0[3] == l.x1[2]);      // le decay suit directement
    CHECK_FALSE (l.active[2]);
    CHECK (l.active[1]);
}

TEST_CASE ("layout: a longer hold is visibly longer, in the right order, and widths grow with time")
{
    const auto a = computeEnvelopeLayout (withTimes (10.0, 20.0, 200.0, 50.0), 800.0);
    const auto b = computeEnvelopeLayout (withTimes (10.0, 400.0, 200.0, 50.0), 800.0);
    const double wa = a.x1[2] - a.x0[2], wb = b.x1[2] - b.x0[2];
    CHECK (wa > 0.0);
    CHECK (wb > wa + 10.0);
    // segments successifs sans trou ni chevauchement
    for (const auto& l : { a, b })
    {
        double prev = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            if (i == 4)
            {
                CHECK (l.plateauX0 == prev);
                prev = l.plateauX1;
            }
            CHECK (l.x0[i] == prev);
            CHECK (l.x1[i] >= l.x0[i]);
            prev = l.x1[i];
        }
        CHECK (l.endX == prev);
    }
}

TEST_CASE ("layout: logarithmic scale keeps 10 ms, 100 ms and 1 s distinguishable")
{
    auto width = [] (double ms)
    {
        const auto l = computeEnvelopeLayout (withTimes (0.0, 0.0, ms, 0.0), 2000.0);
        return l.x1[3] - l.x0[3];
    };
    const double w10 = width (10.0), w100 = width (100.0), w1000 = width (1000.0), w10000 = width (10000.0);
    CHECK (w10 < w100);
    CHECK (w100 < w1000);
    CHECK (w1000 < w10000);
    CHECK (w10000 < 12.0 * w10 + 1.0); // le plus long n'écrase pas le plus court (échelle compressée)
}

TEST_CASE ("layout: everything fits in the available width, minimum widths are respected")
{
    const auto e = withTimes (3000.0, 2000.0, 8000.0, 6000.0);
    const auto l = computeEnvelopeLayout (e, 400.0);
    CHECK (l.endX <= 400.0 + 1e-6);
    for (int i = 0; i < 5; ++i)
        if (l.active[i]) CHECK (l.x1[i] - l.x0[i] >= envelopeMinSegmentPx - 1e-9);

    // très étroit : on garde les largeurs minimales, la courbe peut dépasser plutôt que d'écraser un segment
    const auto narrow = computeEnvelopeLayout (e, 60.0);
    for (int i = 0; i < 5; ++i)
        if (narrow.active[i]) CHECK (narrow.x1[i] - narrow.x0[i] >= envelopeMinSegmentPx - 1e-9);
}

TEST_CASE ("layout: short envelopes are not stretched to fill the container")
{
    const auto l = computeEnvelopeLayout (withTimes (5.0, 0.0, 100.0, 50.0), 1000.0);
    CHECK (l.scale == envelopeMaxPxPerUnit);
    CHECK (l.endX < 500.0);
}

TEST_CASE ("layout: frozen scale keeps earlier boundaries while a segment is dragged")
{
    const auto before = computeEnvelopeLayout (withTimes (10.0, 20.0, 200.0, 50.0), 500.0);
    auto dragged = withTimes (10.0, 20.0, 2000.0, 50.0); // le decay devient beaucoup plus long
    const auto frozen = computeEnvelopeLayout (dragged, 500.0, before.scale);
    CHECK (frozen.scale == before.scale);
    CHECK (frozen.x0[1] == before.x0[1]);
    CHECK (frozen.x1[1] == before.x1[1]);
    CHECK (frozen.x1[2] == before.x1[2]);
    CHECK (frozen.x0[3] == before.x0[3]);
    CHECK (frozen.x1[3] > before.x1[3]);
    // au relâchement : recalcul, tout tient
    CHECK (computeEnvelopeLayout (dragged, 500.0).endX <= 500.0 + 1e-6);
}

TEST_CASE ("layout: pointer position to time is the inverse of the displayed width")
{
    const auto e = withTimes (0.0, 0.0, 250.0, 0.0);
    const auto l = computeEnvelopeLayout (e, 2000.0);
    const double w = l.x1[3] - l.x0[3];
    CHECK_THAT (envelopeTimeFromWidth (w, l.scale), WithinAbs (250.0, 0.5));
    CHECK (envelopeTimeFromWidth (0.0, l.scale) == 0.0);
    CHECK (envelopeTimeFromWidth (2.0, l.scale) == 0.0);               // en deçà de la demi-largeur minimale
    CHECK (envelopeTimeFromWidth (1e9, l.scale) == envelopeMaxMs);
    CHECK (envelopeTimeFromWidth (50.0, 0.0) == 0.0);                   // échelle nulle : pas de division
}

TEST_CASE ("layout: scrubbing a value from zero works and stays within bounds")
{
    CHECK (envelopeScrubTime (0.0, 0.0) == 0.0);
    CHECK (envelopeScrubTime (0.0, 40.0) > 0.0);
    CHECK (envelopeScrubTime (0.0, -100.0) == 0.0);
    CHECK (envelopeScrubTime (100.0, 20.0) > 100.0);
    CHECK (envelopeScrubTime (100.0, -20.0) < 100.0);
    CHECK (envelopeScrubTime (9000.0, 1000.0) == envelopeMaxMs);
    double prev = -1.0;
    for (int dx = -50; dx <= 200; dx += 10)
    {
        const double t = envelopeScrubTime (30.0, dx);
        CHECK (t >= prev);
        prev = t;
    }
}
