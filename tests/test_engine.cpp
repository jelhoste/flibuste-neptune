#include <catch2/catch_test_macros.hpp>
#include "neptune/core/Engine.h"
#include <vector>

using neptune::core::Engine;

TEST_CASE ("Engine outputs silence at palier 0")
{
    Engine e;
    e.prepare (48000.0, 256);
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    float* chans[] = { l.data(), r.data() };
    e.process (chans, 2, 256);
    for (float s : l) REQUIRE (s == 0.0f);
    for (float s : r) REQUIRE (s == 0.0f);
}

TEST_CASE ("noteOn and noteOff count active notes")
{
    Engine e;
    e.prepare (44100.0, 128);
    e.noteOn (0, 60, 0.8f);
    e.noteOn (0, 64, 0.8f);
    REQUIRE (e.activeNotes() == 2);
    e.noteOff (0, 60);
    e.noteOff (0, 64);
    e.noteOff (0, 64);
    REQUIRE (e.activeNotes() == 0);
}
