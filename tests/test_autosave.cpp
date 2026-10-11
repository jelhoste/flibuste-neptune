#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "neptune/core/Autosave.h"
#include "neptune/core/FileUtil.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/State.h"

using namespace neptune::core;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path path;
    TempDir()
    {
        static int counter = 0;
        path = fs::temp_directory_path() / ("neptune_test_autosave_" + std::to_string (++counter) + "_"
                                            + std::to_string (static_cast<long long> (reinterpret_cast<std::uintptr_t> (this))));
        fs::remove_all (path);
        fs::create_directories (path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all (path, ec); }
};

constexpr std::int64_t deadPid = 2147483000;

// Fabrique le fichier qu'aurait laissé une instance `id` d'un processus `pid` disparu.
void plantAutosave (const fs::path& dir, const std::string& id, std::int64_t pid, const std::string& state,
                    std::int64_t savedAt = 1700000000, const char* ext = ".neptuneautosave")
{
    nlohmann::json doc;
    doc["format"] = "neptune-autosave";
    doc["schema_version"] = 1;
    doc["instance_id"] = id;
    doc["pid"] = pid;
    doc["saved_at"] = savedAt;
    doc["state"] = state;
    REQUIRE (fileutil::writeAtomic (dir / (id + ext), doc.dump()));
}

const std::string someId = "0123456789abcdef0123456789abcdef";
} // namespace

TEST_CASE ("instance ids are 32 hex characters and unique")
{
    TempDir dir;
    AutosaveStore a (dir.path), b (dir.path);
    CHECK (AutosaveStore::isValidId (a.id()));
    CHECK (AutosaveStore::isValidId (b.id()));
    CHECK (a.id() != b.id());
}

TEST_CASE ("ids that could escape the folder are rejected")
{
    CHECK_FALSE (AutosaveStore::isValidId (""));
    CHECK_FALSE (AutosaveStore::isValidId ("../../etc/passwd"));
    CHECK_FALSE (AutosaveStore::isValidId ("0123456789ABCDEF0123456789ABCDEF")); // majuscules
    CHECK_FALSE (AutosaveStore::isValidId ("0123456789abcdef0123456789abcde"));  // 31
    CHECK_FALSE (AutosaveStore::isValidId ("0123456789abcdef0123456789abcdef0")); // 33
    CHECK_FALSE (AutosaveStore::isValidId ("0123456789abcdef0123456789abcd/."));
    CHECK (AutosaveStore::isValidId (someId));
}

TEST_CASE ("process helpers: self is alive, an absurd pid is not")
{
    CHECK (AutosaveStore::isProcessAlive (AutosaveStore::currentProcessId()));
    CHECK_FALSE (AutosaveStore::isProcessAlive (deadPid));
    CHECK_FALSE (AutosaveStore::isProcessAlive (0));
    CHECK_FALSE (AutosaveStore::isProcessAlive (-5));
}

TEST_CASE ("write creates the file once, skips identical state, discard removes it")
{
    TempDir dir;
    AutosaveStore store (dir.path / "autosave");
    const auto file = dir.path / "autosave" / (store.id() + ".neptuneautosave");

    REQUIRE (store.write ("state A"));
    CHECK (fs::exists (file));

    const auto t0 = fs::last_write_time (file);
    fs::last_write_time (file, t0 - std::chrono::hours (1)); // vieillit le fichier pour détecter une réécriture
    const auto aged = fs::last_write_time (file);
    REQUIRE (store.write ("state A"));
    CHECK ((fs::last_write_time (file) == aged)); // rien réécrit ; double parenthèses : Catch ne sait pas afficher le time_point de libc++

    REQUIRE (store.write ("state B"));
    CHECK ((fs::last_write_time (file) != aged));

    store.discard();
    CHECK_FALSE (fs::exists (file));
}

TEST_CASE ("baseline: an untouched state writes nothing")
{
    TempDir dir;
    AutosaveStore store (dir.path);
    store.setBaseline ("loaded state");
    REQUIRE (store.write ("loaded state"));
    CHECK (fs::is_empty (dir.path));
    REQUIRE (store.write ("edited state"));
    CHECK_FALSE (fs::is_empty (dir.path));
}

TEST_CASE ("adoptId accepts a valid free id and refuses invalid or duplicated ones")
{
    TempDir dir;
    AutosaveStore a (dir.path), b (dir.path);
    CHECK_FALSE (a.adoptId ("not-an-id"));
    CHECK_FALSE (a.adoptId ("../../../../tmp/x"));

    REQUIRE (a.adoptId (someId));
    CHECK (a.id() == someId);
    CHECK_FALSE (b.adoptId (someId)); // copie de piste : l'identifiant est déjà pris
    CHECK (b.id() != someId);
    CHECK (a.adoptId (someId)); // idempotent
}

TEST_CASE ("an id is released when its instance is destroyed")
{
    TempDir dir;
    {
        AutosaveStore a (dir.path);
        REQUIRE (a.adoptId (someId));
    }
    AutosaveStore b (dir.path);
    CHECK (b.adoptId (someId));
}

TEST_CASE ("no autosave left behind means nothing to recover")
{
    TempDir dir;
    AutosaveStore store (dir.path);
    REQUIRE (store.adoptId (someId));
    CHECK_FALSE (store.takeRecoverable().has_value());
}

TEST_CASE ("an orphan autosave is recovered, kept until resolved")
{
    TempDir dir;
    plantAutosave (dir.path, someId, deadPid, "crashed state", 1700000123);

    AutosaveStore store (dir.path);
    REQUIRE (store.adoptId (someId));

    const auto rec = store.takeRecoverable();
    REQUIRE (rec.has_value());
    CHECK (rec->state == "crashed state");
    CHECK (rec->savedAt == 1700000123);
    CHECK (fs::exists (dir.path / (someId + ".neptunerecovered")));
    CHECK_FALSE (fs::exists (dir.path / (someId + ".neptuneautosave"))); // libre pour les écritures de la nouvelle session
}

TEST_CASE ("an unresolved recovered state is offered again, then resolved")
{
    TempDir dir;
    plantAutosave (dir.path, someId, deadPid, "kept state", 1700000000, ".neptunerecovered");

    {
        AutosaveStore s (dir.path);
        REQUIRE (s.adoptId (someId));
        const auto rec = s.takeRecoverable();
        REQUIRE (rec.has_value());
        CHECK (rec->state == "kept state");
    }
    AutosaveStore s2 (dir.path);
    REQUIRE (s2.adoptId (someId));
    REQUIRE (s2.takeRecoverable().has_value());
    s2.resolveRecovered();
    CHECK_FALSE (s2.takeRecoverable().has_value());
    CHECK_FALSE (fs::exists (dir.path / (someId + ".neptunerecovered")));
}

TEST_CASE ("an autosave written by a live process is not a crash trace")
{
    TempDir dir;
    plantAutosave (dir.path, someId, AutosaveStore::currentProcessId(), "live state");
    AutosaveStore store (dir.path);
    REQUIRE (store.adoptId (someId));
    CHECK_FALSE (store.takeRecoverable().has_value());
    CHECK (fs::exists (dir.path / (someId + ".neptuneautosave"))); // intact
}

TEST_CASE ("corrupt autosave files are discarded without crashing")
{
    TempDir dir;
    {
        std::ofstream f (dir.path / (someId + ".neptuneautosave"));
        f << "{ not json";
    }
    AutosaveStore store (dir.path);
    REQUIRE (store.adoptId (someId));
    CHECK_FALSE (store.takeRecoverable().has_value());
    CHECK_FALSE (fs::exists (dir.path / (someId + ".neptuneautosave")));
}

TEST_CASE ("purgeStale removes old orphans only")
{
    TempDir dir;
    const std::string oldId = std::string (32, 'a');
    const std::string freshId = std::string (32, 'b');
    const std::string liveId = std::string (32, 'c');

    plantAutosave (dir.path, oldId, deadPid, "old");
    plantAutosave (dir.path, freshId, deadPid, "fresh");
    plantAutosave (dir.path, liveId, AutosaveStore::currentProcessId(), "live");
    {
        std::ofstream f (dir.path / "unrelated.txt");
        f << "keep me";
    }

    const auto old = fs::file_time_type::clock::now() - std::chrono::hours (24 * 40);
    fs::last_write_time (dir.path / (oldId + ".neptuneautosave"), old);
    fs::last_write_time (dir.path / (liveId + ".neptuneautosave"), old); // vieux mais écrit par un processus vivant
    fs::last_write_time (dir.path / "unrelated.txt", old);

    CHECK (AutosaveStore::purgeStale (dir.path, 30) == 1);
    CHECK_FALSE (fs::exists (dir.path / (oldId + ".neptuneautosave")));
    CHECK (fs::exists (dir.path / (freshId + ".neptuneautosave")));
    CHECK (fs::exists (dir.path / (liveId + ".neptuneautosave")));
    CHECK (fs::exists (dir.path / "unrelated.txt"));
    CHECK (AutosaveStore::purgeStale (dir.path / "does-not-exist", 30) == 0);
}

TEST_CASE ("state carries the instance id through save and load")
{
    ParameterSet a;
    REQUIRE (probe::buildPool (a, 1));
    const auto text = state::save (a, someId);
    CHECK (state::load (a, text).instanceId == someId);
    CHECK (state::load (a, state::save (a)).instanceId.empty());
}
