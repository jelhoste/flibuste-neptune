#include <catch2/catch_test_macros.hpp>

#include "neptune/core/ProbePool.h"

using namespace neptune::core;

TEST_CASE ("probe pool builds with the expected size")
{
    for (int tracks : { 0, 1, 32, 64 })
    {
        ParameterSet ps;
        std::string err;
        REQUIRE (probe::buildPool (ps, tracks, &err));
        CHECK (err.empty());
        CHECK (static_cast<int> (ps.size()) == probe::expectedCount (tracks));
    }
}

TEST_CASE ("probe pool ids follow the documented scheme")
{
    ParameterSet ps;
    REQUIRE (probe::buildPool (ps, 4));

    CHECK (ps.find (probe::masterVolume) != nullptr);
    CHECK (ps.find (probe::trackParam (3, probe::kSolo)) != nullptr);
    CHECK (ps.find (probe::trackParam (3, probe::kInsertBypass + 7)) != nullptr);
    CHECK (ps.find (probe::trackParam (3, probe::kSendLevel + 7)) != nullptr);
    CHECK (ps.find (probe::trackParam (4, probe::kVolume)) == nullptr); // piste 5 : hors pool

    CHECK (ps.find (probe::trackParam (0, probe::kType))->kind == Kind::Choice);
    CHECK (ps.find (probe::algorithm)->kind == Kind::Stepped);
    CHECK (ps.find (probe::trackParam (1, probe::kMute))->kind == Kind::Toggle);
}

TEST_CASE ("probe pool defaults normalize inside 0..1")
{
    ParameterSet ps;
    REQUIRE (probe::buildPool (ps, 8));
    for (std::size_t i = 0; i < ps.size(); ++i)
    {
        const double n = ps.getNormalizedAt (i);
        CHECK (n >= 0.0);
        CHECK (n <= 1.0);
    }
}

TEST_CASE ("probe pool text for a muted send shows minus infinity")
{
    ParameterSet ps;
    REQUIRE (probe::buildPool (ps, 1));
    CHECK (ps.toText (probe::trackParam (0, probe::kSendLevel)) == "-inf");
    CHECK (ps.toText (probe::trackParam (0, probe::kVolume)) == "0.0 dB");
}
