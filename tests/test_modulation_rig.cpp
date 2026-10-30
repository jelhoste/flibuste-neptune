#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "neptune/core/ModulationRig.h"

using namespace neptune::core;
using namespace neptune::core::modulation;
using Catch::Approx;

namespace
{
constexpr ParamId kCutoff = 700, kLevel = 701;

ParameterSet makeSet()
{
    ParameterSet set;
    set.add (ParamSpec::continuous (kCutoff, "Cutoff", Unit::None, 0.0, 100.0, 50.0));
    set.add (ParamSpec::continuous (kLevel, "Level", Unit::None, 0.0, 100.0, 50.0));
    return set;
}

Matrix lfoOn (ParamId dest, double amount)
{
    Matrix m;
    m.lfos[0].shape = LfoShape::Square;
    m.lfos[0].rateHz = 2.0;
    Route r;
    r.source = { SourceKind::Lfo, 0, 0 };
    r.dest = dest;
    r.amount = amount;
    m.routes.push_back (r);
    return m;
}
}

TEST_CASE ("modulation rig: a LFO route moves the destination around its set value, and nothing else")
{
    auto set = makeSet();
    Rig rig;
    rig.prepare (48000.0);
    rig.setMatrix (lfoOn (kCutoff, 0.5), set);
    const auto cutoff = set.indexOf (kCutoff), level = set.indexOf (kLevel);
    float lo = 1.0e9f, hi = -1.0e9f;
    for (int block = 0; block < 400; ++block)   // 400 blocs de 64 : plus d'une période du LFO (2 Hz)
    {
        rig.process (64, set);
        const float off = set.modOffsetAt (cutoff);
        lo = std::min (lo, off);
        hi = std::max (hi, off);
        CHECK (set.modOffsetAt (level) == 0.0f);
        CHECK (set.getAt (cutoff) == Approx (50.0));   // la valeur réglée ne bouge jamais
    }
    CHECK (hi > 0.2f);
    CHECK (lo < -0.2f);
}

TEST_CASE ("modulation rig: without route, or after the route is removed, the offsets come back to zero")
{
    auto set = makeSet();
    Rig rig;
    rig.prepare (48000.0);
    const auto cutoff = set.indexOf (kCutoff);
    rig.process (64, set);
    CHECK (set.modOffsetAt (cutoff) == 0.0f);   // aucune matrice : rien ne bouge

    rig.setMatrix (lfoOn (kCutoff, 1.0), set);
    bool moved = false;
    for (int block = 0; block < 200 && ! moved; ++block)
    {
        rig.process (64, set);
        moved = set.modOffsetAt (cutoff) != 0.0f;
    }
    REQUIRE (moved);

    rig.setMatrix (Matrix {}, set);
    rig.process (64, set);
    rig.process (64, set);
    CHECK (set.modOffsetAt (cutoff) == 0.0f);

    rig.setMatrix (lfoOn (kCutoff, 1.0), set);
    for (int block = 0; block < 50; ++block) rig.process (64, set);
    rig.reset (set);
    CHECK (set.modOffsetAt (cutoff) == 0.0f);
}

TEST_CASE ("modulation rig: a controller feeds a route, and the held keys are the envelope gate")
{
    auto set = makeSet();
    Rig rig;
    rig.prepare (48000.0);
    Matrix m;
    Route cc;
    cc.source = { SourceKind::ControlChange, 74, 0 };
    cc.dest = kCutoff;
    cc.amount = 0.5;
    m.routes.push_back (cc);
    m.envelopes[0].env.attack.ms = 1.0;
    m.envelopes[0].env.sustain = 1.0;
    Route env;
    env.source = { SourceKind::Envelope, 0, 0 };
    env.dest = kLevel;
    env.amount = 0.4;
    m.routes.push_back (env);
    rig.setMatrix (m, set);
    const auto cutoff = set.indexOf (kCutoff), level = set.indexOf (kLevel);

    rig.process (64, set);
    CHECK (set.modOffsetAt (cutoff) == Approx (0.0f));
    rig.midiEvent ({ midi::MsgKind::ControlChange, 0, 74, 127 });
    rig.process (64, set);
    CHECK (set.modOffsetAt (cutoff) == Approx (0.5f).margin (0.02));   // contrôleur au maximum : la moitié de la course (amount 0,5)

    CHECK (set.modOffsetAt (level) == Approx (0.0f).margin (0.001));   // aucune touche tenue : l'enveloppe est au repos
    rig.noteOn (60);
    for (int block = 0; block < 20; ++block) rig.process (64, set);
    CHECK (set.modOffsetAt (level) == Approx (0.4f).margin (0.03));    // touche tenue, sustain à 1
    rig.noteOff (60);
    for (int block = 0; block < 300; ++block) rig.process (64, set);
    CHECK (set.modOffsetAt (level) == Approx (0.0f).margin (0.01));    // relâchée : l'enveloppe retombe
}
