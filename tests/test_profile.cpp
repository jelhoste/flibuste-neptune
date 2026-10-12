#include <catch2/catch_test_macros.hpp>
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"
#include "neptune/core/State.h"

using namespace neptune::core;

TEST_CASE ("profile ids round trip")
{
    CHECK (profileFromString ("live") == PerformanceProfile::Live);
    CHECK (profileFromString ("studio") == PerformanceProfile::Studio);
    CHECK_FALSE (profileFromString ("turbo").has_value());
    CHECK (std::string (toString (PerformanceProfile::Studio)) == "studio");
}

TEST_CASE ("instances follow the shared profile unless locked")
{
    SharedProfile shared;
    InstanceProfile a (shared), b (shared);
    CHECK (a.effective() == PerformanceProfile::Live); // défaut

    shared.set (PerformanceProfile::Studio); // une bascule atteint toutes les instances du processus
    CHECK (a.effective() == PerformanceProfile::Studio);
    CHECK (b.effective() == PerformanceProfile::Studio);

    a.lockToCurrent(); // a garde Studio
    shared.set (PerformanceProfile::Live);
    CHECK (a.effective() == PerformanceProfile::Studio);
    CHECK (a.locked() == PerformanceProfile::Studio);
    CHECK (b.effective() == PerformanceProfile::Live);
    CHECK_FALSE (b.locked().has_value());

    a.unlock();
    CHECK (a.effective() == PerformanceProfile::Live);
    a.lockTo (PerformanceProfile::Live);
    shared.set (PerformanceProfile::Studio);
    CHECK (a.effective() == PerformanceProfile::Live);
}

TEST_CASE ("only the first instance initialises the shared profile")
{
    SharedProfile shared;
    CHECK (shared.initialiseOnce (PerformanceProfile::Studio));
    CHECK_FALSE (shared.initialiseOnce (PerformanceProfile::Live)); // une 2e instance ne l'écrase pas
    CHECK (shared.get() == PerformanceProfile::Studio);
}

TEST_CASE ("a manual switch before initialisation is not overwritten")
{
    SharedProfile shared;
    shared.set (PerformanceProfile::Studio);
    CHECK_FALSE (shared.initialiseOnce (PerformanceProfile::Live));
    CHECK (shared.get() == PerformanceProfile::Studio);
}

TEST_CASE ("profile lock is saved in the project state")
{
    ParameterSet set;
    auto doc = state::save (set, {}, PerformanceProfile::Studio);
    CHECK (state::load (set, doc).profileLock == PerformanceProfile::Studio);
    CHECK_FALSE (state::load (set, state::save (set)).profileLock.has_value());
    CHECK (state::save (set).find ("profile_lock") == std::string::npos);
}

TEST_CASE ("schema v1 projects still load, unreadable lock means unlocked")
{
    ParameterSet set;
    const auto v1 = state::load (set, R"({"format":"neptune-state","schema_version":1,"parameters":{}})");
    CHECK (v1.ok());
    CHECK (v1.fileVersion == 1);
    CHECK_FALSE (v1.profileLock.has_value());
    const auto bad = state::load (set, R"({"format":"neptune-state","schema_version":2,"profile_lock":"turbo","parameters":{}})");
    CHECK (bad.ok());
    CHECK_FALSE (bad.profileLock.has_value());
    CHECK (state::load (set, R"({"format":"neptune-state","schema_version":5,"parameters":{}})").status == state::Status::TooNew);
}
