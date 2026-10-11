#include <catch2/catch_test_macros.hpp>

#include "neptune/core/ProbePool.h"
#include "neptune/core/State.h"
#include "neptune/core/StateMigration.h"

using namespace neptune::core;

namespace
{
ParameterSet makePool (int tracks = 4)
{
    ParameterSet ps;
    REQUIRE (probe::buildPool (ps, tracks));
    return ps;
}
} // namespace

TEST_CASE ("state round trip restores every value exactly")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 4));
    a.set (probe::masterVolume, -7.123456789012345);
    a.set (probe::trackParam (2, probe::kMute), 1.0);
    a.set (probe::algorithm, 17.0);

    const std::string text = state::save (a);

    ParameterSet b;
    REQUIRE (probe::buildPool (b, 4));
    const auto r = state::load (b, text);
    REQUIRE (r.ok());
    CHECK (r.fileVersion == state::currentSchemaVersion);
    CHECK (r.applied == static_cast<int> (a.size()));
    CHECK (r.ignored == 0);
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK (b.getAt (i) == a.getAt (i));
}

TEST_CASE ("saved state declares format and schema version")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    const std::string text = state::save (a);
    CHECK (text.find ("\"format\": \"neptune-state\"") != std::string::npos);
    CHECK (text.find ("\"schema_version\": " + std::to_string (state::currentSchemaVersion)) != std::string::npos);
}

TEST_CASE ("state with a newer schema is refused and changes nothing")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    a.set (probe::masterVolume, -12.0);

    const std::string text = R"({"format":"neptune-state","schema_version":99,"parameters":{"1":0.0}})";
    const auto r = state::load (a, text);
    CHECK (r.status == state::Status::TooNew);
    CHECK (r.fileVersion == 99);
    CHECK_FALSE (r.message.empty());
    CHECK (a.get (probe::masterVolume) == -12.0);
}

TEST_CASE ("invalid or foreign documents are refused and change nothing")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    a.set (probe::masterVolume, -12.0);

    CHECK (state::load (a, "").status == state::Status::InvalidJson);
    CHECK (state::load (a, "not json {").status == state::Status::InvalidJson);
    CHECK (state::load (a, "[1,2,3]").status == state::Status::InvalidJson);
    CHECK (state::load (a, R"({"hello":"world"})").status == state::Status::NotNeptune);
    CHECK (state::load (a, R"({"format":"neptune-state","parameters":{}})").status == state::Status::InvalidJson);
    CHECK (state::load (a, R"({"format":"neptune-state","schema_version":0,"parameters":{}})").status
           == state::Status::InvalidJson);
    CHECK (state::load (a, R"({"format":"neptune-state","schema_version":1})").status == state::Status::InvalidJson);
    CHECK (state::load (a, "\x4e\x50\x54\x53\x01\x00\x00\x00").status == state::Status::InvalidJson); // ancien binaire 'NPTS'
    CHECK (a.get (probe::masterVolume) == -12.0);
}

TEST_CASE ("unknown ids and non numeric values are ignored, the rest is applied")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));

    const std::string text = R"({"format":"neptune-state","schema_version":1,"parameters":{
        "1":-6.0, "424242":1.0, "abc":2.0, "2":"left", "-3":1.0, "4":null}})";
    const auto r = state::load (a, text);
    REQUIRE (r.ok());
    CHECK (r.applied == 1);
    CHECK (r.ignored == 5);
    CHECK (a.get (probe::masterVolume) == -6.0);
}

TEST_CASE ("parameters absent from the state return to their default")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    a.set (probe::masterVolume, -20.0);
    a.set (probe::masterPan, 0.5);

    const std::string text = R"({"format":"neptune-state","schema_version":1,"parameters":{"2":0.25}})";
    REQUIRE (state::load (a, text).ok());
    CHECK (a.get (probe::masterPan) == 0.25);
    CHECK (a.get (probe::masterVolume) == a.find (probe::masterVolume)->defaultValue);
}

TEST_CASE ("out of range values are clamped by the parameter layer")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    const std::string text = R"({"format":"neptune-state","schema_version":1,"parameters":{"4":9999}})";
    REQUIRE (state::load (a, text).ok());
    CHECK (a.get (probe::algorithm) == a.find (probe::algorithm)->max);
}

TEST_CASE ("migration chain runs step by step up to the target version")
{
    using nlohmann::json;
    state::MigrationTable table;
    table[1] = [] (json& d) { d["parameters"]["renamed"] = d["parameters"]["old"]; d["parameters"].erase ("old"); return true; };
    table[2] = [] (json& d) { d["touched_by_2"] = true; return true; };

    json doc = { { "schema_version", 1 }, { "parameters", { { "old", 5 } } } };
    std::string err;
    REQUIRE (state::migrate (doc, 1, 3, table, &err));
    CHECK (doc["schema_version"] == 3);
    CHECK (doc["parameters"]["renamed"] == 5);
    CHECK (doc["touched_by_2"] == true);

    json missing = { { "schema_version", 1 } };
    CHECK_FALSE (state::migrate (missing, 1, 4, table, &err)); // pas de saut 3 -> 4
    CHECK_FALSE (err.empty());

    state::MigrationTable failing;
    failing[1] = [] (json&) { return false; };
    json d2 = { { "schema_version", 1 } };
    CHECK_FALSE (state::migrate (d2, 1, 2, failing, &err));
}

TEST_CASE ("registered migrations cover every older version")
{
    // Garde-fou : quand currentSchemaVersion monte, il faut enregistrer le saut correspondant.
    for (int v = 1; v < state::currentSchemaVersion; ++v)
        CHECK (state::registeredMigrations().count (v) == 1);
}
