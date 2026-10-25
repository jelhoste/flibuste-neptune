// Système de presets : fichier, banques, grille, bundle, recherche à facettes, navigation, bibliothèque sur disque.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

#include "neptune/core/FileUtil.h"
#include "neptune/core/Preset.h"
#include "neptune/core/PresetLibrary.h"

using namespace neptune::core::preset;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    fs::path path;
    TempDir()
    {
        path = fs::temp_directory_path() / ("neptune_preset_" + newId());
        fs::create_directories (path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all (path, ec); }
};

Preset sample (const std::string& name)
{
    Preset p;
    p.id = newId();
    p.name = name;
    p.type = "Pad";
    p.characteristics = { "doux", "chaud" };
    p.rating = 2;
    p.customFields = { { "Morceau", "Ave Maria" }, { "Morceau", "Panis Angelicus" }, { "Concert", "Fin d'annee" } };
    p.parameters["env.attack"] = ParamValue::ofNumber (0.25);
    p.parameters["osc.waveform"] = ParamValue::ofText ("drawbar_88800000");
    p.captureOverrides["output.volume"] = Capture::Captured;
    p.module = "analogsynth";
    p.state = R"({"a":1})";
    p.facade = R"({"facade":{"widgets":[]},"track":2})";
    return p;
}
} // namespace

TEST_CASE ("preset ids are v4 uuids and unique", "[preset]")
{
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i)
    {
        const auto id = newId();
        REQUIRE (id.size() == 36);
        CHECK (id[8] == '-');
        CHECK (id[13] == '-');
        CHECK (id[14] == '4');
        CHECK (id[18] == '-');
        CHECK (std::string ("89ab").find (id[19]) != std::string::npos);
        seen.insert (id);
    }
    CHECK (seen.size() == 200);
}

TEST_CASE ("preset json round trip", "[preset]")
{
    Preset p = sample ("Ave Maria - Jeu doux");
    p.locked = true;
    p.derivedFrom = "factory-x";
    p.reverbEngine = "light";
    p.declaredLatencySamples = 64;
    p.pedalHeldOverride = PedalBehavior::Immediate;
    bool ok = false;
    const Preset q = presetFromJson (toJson (p), &ok);
    CHECK (ok);
    CHECK (q == p);
    CHECK_FALSE (q.factory);
}

TEST_CASE ("preset facade is kept as a nested object and optional", "[preset]")
{
    Preset p = sample ("Avec facade");
    const auto text = toJson (p);
    CHECK (text.find ("\"facade\"") != std::string::npos);
    CHECK (presetFromJson (text).facade == p.facade);
    p.facade.clear();
    CHECK (toJson (p).find ("\"facade\"") == std::string::npos);
    CHECK (presetFromJson (toJson (p)).facade.empty());
}

TEST_CASE ("preset reading is tolerant", "[preset]")
{
    bool ok = true;
    const Preset bad = presetFromJson ("not json", &ok);
    CHECK_FALSE (ok);
    CHECK (bad.name == "Init");
    CHECK_FALSE (bad.id.empty());

    // clé inconnue ignorée, version supérieure acceptée, champs invalides ramenés au défaut
    const Preset p = presetFromJson (R"({"schemaVersion":99,"name":"X","rating":12,"locked":"oui","futureKey":{"a":1},
        "parameters":{"a":1.5,"b":"txt","c":null,"d":true},"customFields":[{"key":"k"},{"key":"","value":"v"},5],
        "characteristics":["a","a",3,"b"]})", &ok);
    CHECK (ok);
    CHECK (p.name == "X");
    CHECK (p.rating == 3);
    CHECK_FALSE (p.locked);
    CHECK (p.parameters.size() == 3);
    CHECK (p.parameters.at ("d").number == 1.0);
    CHECK (p.customFields.size() == 1);
    CHECK (p.characteristics == std::vector<std::string> { "a", "b" });
}

TEST_CASE ("preset sanitising bounds everything", "[preset]")
{
    Preset p;
    p.name = std::string (500, 'x');
    p.rating = -4;
    for (int i = 0; i < 100; ++i) p.characteristics.push_back ("c" + std::to_string (i));
    p.reverbEngine = "bogus";
    p = sanitised (std::move (p));
    CHECK (p.name.size() == 128);
    CHECK (p.rating == 0);
    CHECK (p.characteristics.size() == 16);
    CHECK (p.reverbEngine.empty());
    CHECK_FALSE (p.id.empty());
}

TEST_CASE ("capture resolution: override, then global default, then captured", "[preset]")
{
    LibraryConfig cfg;
    cfg.parameterCaptureDefaults["output.volume"] = Capture::Free;
    Preset p;
    CHECK (captureOf ("env.attack", p, cfg) == Capture::Captured);
    CHECK (captureOf ("output.volume", p, cfg) == Capture::Free);
    p.captureOverrides["output.volume"] = Capture::Captured;
    CHECK (captureOf ("output.volume", p, cfg) == Capture::Captured);

    CHECK (pedalBehaviorOf (p, cfg) == PedalBehavior::Isolate);
    p.pedalHeldOverride = PedalBehavior::Immediate;
    CHECK (pedalBehaviorOf (p, cfg) == PedalBehavior::Immediate);
}

TEST_CASE ("library config round trip", "[preset]")
{
    LibraryConfig c;
    c.parameterCaptureDefaults["leslie.speed"] = Capture::Free;
    c.pedalHeldBehaviorDefault = PedalBehavior::Immediate;
    c.reverbPool.maxLightInstances = 5;
    c.trashRetentionDays = 30;
    c.trashMaxEntries = 10;
    CHECK (configFromJson (toJson (c)) == c);
    CHECK (configFromJson ("garbage") == LibraryConfig {});
}

TEST_CASE ("default bank names are letters", "[preset]")
{
    CHECK (defaultBankName (0) == "A");
    CHECK (defaultBankName (25) == "Z");
    CHECK (defaultBankName (26) == "A1");
    CHECK (defaultBankName (51) == "Z1");
    CHECK (defaultBankName (127) == "X4");
    Bank b;
    b.index = 2;
    CHECK (displayName (b) == "C");
    b.name = "Concert";
    CHECK (displayName (b) == "Concert");
}

TEST_CASE ("bank manifest round trip and forget", "[preset]")
{
    BankManifest m;
    m.ensure (5).name = "Noel";
    m.ensure (5).presets[0] = "id-a";
    m.ensure (5).presets[127] = "id-b";
    m.ensure (2).wrapToNextBank = true;
    m.ensure (2).customFields = { { "Concert", "2026" } };
    CHECK (m.banks[0].index == 2); // triées
    const auto back = banksFromJson (toJson (m));
    CHECK (back == m);
    CHECK (back.find (5)->presets[127] == "id-b");
    CHECK (m.forget ("id-a") == 1);
    CHECK (m.find (5)->presets[0].empty());
    // index invalide, doublon : ignorés
    const auto weird = banksFromJson (R"({"banks":[{"index":300},{"index":3,"presets":["a"]},{"index":3,"presets":["z"]}]})");
    CHECK (weird.banks.size() == 1);
    CHECK (weird.find (3)->presets[0] == "a");
}

TEST_CASE ("navigator steps over empty slots and wraps", "[preset]")
{
    BankManifest m;
    auto& a = m.ensure (0);
    a.presets[0] = "p0";
    a.presets[3] = "p3";
    auto& b = m.ensure (1);
    b.presets[1] = "p10";

    // reboucle dans la banque
    auto s = Navigator::step (m, { 0, 0 }, +1);
    REQUIRE (s);
    CHECK (*s == Slot { 0, 3 });
    s = Navigator::step (m, { 0, 3 }, +1);
    REQUIRE (s);
    CHECK (*s == Slot { 0, 0 });
    s = Navigator::step (m, { 0, 0 }, -1);
    REQUIRE (s);
    CHECK (*s == Slot { 0, 3 });

    // passe à la banque suivante
    m.find (0)->wrapToNextBank = true;
    s = Navigator::step (m, { 0, 3 }, +1);
    REQUIRE (s);
    CHECK (*s == Slot { 1, 1 });

    CHECK (Navigator::presetAt (m, { 1, 1 }) == "p10");
    CHECK (Navigator::presetAt (m, { 9, 1 }).empty());
    CHECK (Navigator::locate (m, "p3") == std::optional<Slot> (Slot { 0, 3 }));
    CHECK_FALSE (Navigator::locate (m, "nope"));

    BankManifest single;
    single.ensure (0).presets[7] = "only";
    CHECK_FALSE (Navigator::step (single, { 0, 7 }, +1));
    CHECK_FALSE (Navigator::step (BankManifest {}, { 0, 0 }, +1));
}

TEST_CASE ("navigator handles MIDI bank select and program change", "[preset]")
{
    BankManifest m;
    m.ensure (3).presets[12] = "x";
    Navigator n;
    CHECK (n.programChange (m, 12).empty()); // banque 0 : emplacement vide, le preset actuel reste
    n.bankSelect (3);
    CHECK (n.programChange (m, 12) == "x");
    CHECK (n.programChange (m, 13).empty());
    n.bankSelect (500);
    CHECK (n.bank() == 127);
}

TEST_CASE ("grid assign, swap, integrity and round trip", "[preset]")
{
    Grid g;
    g.gridId = newId();
    g.name = "Concert";
    CHECK (g.assign (2, "a").empty());
    CHECK (g.assign (0, "b").empty());
    CHECK (g.assign (2, "c") == "a"); // l'ancien est rendu
    CHECK (g.tiles[0].position == 0);
    CHECK (g.swapTiles (0, 2));
    CHECK (g.tiles[0].presetRef == "c");
    CHECK (g.tiles[1].presetRef == "b");
    CHECK_FALSE (g.swapTiles (-1, 3));
    CHECK (g.swapTiles (0, 5)); // vers une tuile vide
    CHECK (g.missing ({ "b" }) == std::vector<std::string> { "c" });
    g.locked = false;
    g.linkedConcertField = "Fin d'annee";
    CHECK (gridFromJson (toJson (g)) == g);
}

TEST_CASE ("bundle manifest round trip", "[preset]")
{
    BundleManifest b;
    b.bankName = "Concert Noel 2026";
    b.bankCustomFields = { { "Concert", "Fin d'annee 2026" } };
    b.presetOrder = { "a", "b", "c" };
    CHECK (bundleFromJson (toJson (b)) == b);
    CHECK (bundleFromJson ("[]").presetOrder.empty());
}

TEST_CASE ("faceted search: OR within a key, AND across keys", "[preset]")
{
    std::vector<Preset> all;
    {
        Preset p = sample ("Alpha");
        all.push_back (p);
    }
    {
        Preset p = sample ("beta");
        p.type = "Lead";
        p.rating = 0;
        p.characteristics = { "brillant" };
        p.customFields = { { "Morceau", "Gloria" }, { "Concert", "Fin d'annee" } };
        all.push_back (p);
    }
    {
        Preset p = sample ("Gamma");
        p.customFields = { { "Concert", "Printemps" } };
        p.locked = true;
        all.push_back (p);
    }
    Query q;
    CHECK (filter (all, q).size() == 3);
    CHECK (filter (all, q).front()->name == "Alpha"); // tri insensible à la casse : Alpha, beta, Gamma
    CHECK (filter (all, q)[1]->name == "beta");

    q.text = "AMM";
    CHECK (filter (all, q).size() == 1);

    q = Query {};
    q.type = "lead";
    CHECK (filter (all, q).size() == 1);

    q = Query {};
    q.minRating = 1;
    CHECK (filter (all, q).size() == 2);

    q = Query {};
    q.characteristics = { "brillant", "doux" }; // OR
    CHECK (filter (all, q).size() == 3);

    q = Query {};
    q.facets = { { "Morceau", "Ave Maria" }, { "Morceau", "Gloria" } }; // OR dans la clé
    CHECK (filter (all, q).size() == 2);
    q.facets.push_back ({ "Concert", "Fin d'annee" });                  // AND avec une autre clé
    CHECK (filter (all, q).size() == 2);
    q.facets.push_back ({ "Concert", "Printemps" });                    // OR dans la seconde clé
    CHECK (filter (all, q).size() == 2);
    q.facets = { { "Morceau", "Gloria" }, { "Concert", "Printemps" } }; // aucun preset n'a les deux
    CHECK (filter (all, q).empty());

    q = Query {};
    q.lockedOnly = true;
    CHECK (filter (all, q).size() == 1);
}

TEST_CASE ("library: save, locked and factory protection, save as", "[preset][library]")
{
    TempDir dir;
    Preset factoryPreset = sample ("Factory One");
    factoryPreset.id = "factory-1";
    Library lib (dir.path, { factoryPreset });
    REQUIRE (lib.open());
    REQUIRE (lib.factoryPresets().size() == 1);
    CHECK (lib.factoryPresets()[0].factory);

    std::string error;
    CHECK_FALSE (lib.save (lib.factoryPresets()[0], &error)); // factory : lecture seule

    Preset mine = sample ("Mine");
    CHECK (lib.save (mine));
    CHECK (lib.userPresets().size() == 1);
    CHECK (fs::exists (dir.path / "presets" / (mine.id + ".flipreset")));

    mine.name = "Mine v2";
    CHECK (lib.save (mine));
    CHECK (lib.find (mine.id)->name == "Mine v2");

    mine.locked = true;
    CHECK (lib.updateMetadata (mine)); // le verrou lui-même se règle sur un preset verrouillé
    CHECK (lib.find (mine.id)->locked);
    mine.name = "forbidden";
    CHECK_FALSE (lib.save (mine, &error)); // verrouillé : « Enregistrer » refusé
    CHECK (lib.find (mine.id)->name == "Mine v2");

    Preset copy;
    REQUIRE (lib.saveAs (lib.factoryPresets()[0], "Ma version", &copy));
    CHECK (copy.derivedFrom == "factory-1");
    CHECK_FALSE (copy.locked);
    CHECK (copy.id != "factory-1");
    CHECK (lib.userPresets().size() == 2);

    Preset copy2;
    REQUIRE (lib.saveAs (*lib.find (mine.id), "", &copy2));
    CHECK (copy2.name == "Mine v2");
    CHECK (copy2.derivedFrom.empty()); // dérive d'un preset utilisateur : rien d'informatif à noter
    CHECK_FALSE (copy2.locked);

    // rechargement : tout revient du disque
    Library again (dir.path, { factoryPreset });
    std::vector<std::string> warnings;
    REQUIRE (again.open (&warnings));
    CHECK (warnings.empty());
    CHECK (again.userPresets().size() == 3);
    CHECK (again.find (copy.id)->derivedFrom == "factory-1");
}

TEST_CASE ("library: corrupt and foreign files are skipped with a warning", "[preset][library]")
{
    TempDir dir;
    fs::create_directories (dir.path / "presets");
    { std::ofstream (dir.path / "presets" / "broken.flipreset") << "{{{"; }
    { std::ofstream (dir.path / "presets" / "evil.flipreset") << R"({"id":"../../evil","name":"x"})"; }
    { std::ofstream (dir.path / "presets" / "notes.txt") << "hello"; }
    Preset ok = sample ("OK");
    { std::ofstream (dir.path / "presets" / "ok.flipreset") << toJson (ok); }
    Library lib (dir.path);
    std::vector<std::string> warnings;
    REQUIRE (lib.open (&warnings));
    CHECK (lib.userPresets().size() == 1);
    CHECK (warnings.size() == 2);
}

TEST_CASE ("library: trash, restore, retention and bank cleanup", "[preset][library]")
{
    TempDir dir;
    Library lib (dir.path);
    REQUIRE (lib.open());
    Preset a = sample ("Alpha/\\..");
    Preset b = sample ("Beta");
    REQUIRE (lib.save (a));
    REQUIRE (lib.save (b));
    REQUIRE (lib.assign (0, 4, a.id));
    CHECK (lib.banks().find (0)->presets[4] == a.id);
    CHECK_FALSE (lib.assign (0, 4, "ghost"));
    CHECK_FALSE (lib.assign (0, 128, a.id));

    REQUIRE (lib.moveToTrash (a.id));
    CHECK (lib.find (a.id) == nullptr);
    CHECK (lib.banks().find (0)->presets[4].empty()); // emplacement vidé et enregistré
    CHECK_FALSE (fs::exists (dir.path / "presets" / (a.id + ".flipreset")));
    auto trash = lib.trash();
    REQUIRE (trash.size() == 1);
    CHECK (trash[0].presetId == a.id);
    CHECK (trash[0].folder.find ('/') == std::string::npos);

    REQUIRE (lib.restore (trash[0]));
    CHECK (lib.find (a.id) != nullptr);
    CHECK (lib.trash().empty());

    // rétention
    REQUIRE (lib.moveToTrash (b.id));
    CHECK (lib.purgeTrash() == 0);
    const auto future = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count()
                        + 15LL * 86400;
    CHECK (lib.purgeTrash (future) == 1);
    CHECK (lib.trash().empty());

    // maximum d'entrées : les plus anciennes partent
    LibraryConfig c = lib.config();
    c.trashMaxEntries = 2;
    REQUIRE (lib.setConfig (c));
    for (int i = 0; i < 4; ++i)
    {
        Preset p = sample ("n" + std::to_string (i));
        REQUIRE (lib.save (p));
        REQUIRE (lib.moveToTrash (p.id));
    }
    CHECK (lib.trash().size() == 2);
    CHECK (lib.emptyTrash());
    CHECK (lib.trash().empty());
}

TEST_CASE ("library: bundle export and import reassign positions", "[preset][library]")
{
    TempDir src, dst;
    Library from (src.path);
    REQUIRE (from.open());
    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i)
    {
        Preset p = sample ("P" + std::to_string (i));
        REQUIRE (from.save (p));
        ids.push_back (p.id);
    }
    REQUIRE (from.assign (7, 10, ids[2]));
    REQUIRE (from.assign (7, 3, ids[0]));
    REQUIRE (from.assign (7, 50, ids[1]));
    from.setBank ([&] { Bank b = *from.banks().find (7); b.name = "Concert"; return b; }());

    std::vector<Preset> presets;
    const auto manifest = from.exportManifest (7, &presets);
    CHECK (manifest.bankName == "Concert");
    CHECK (manifest.presetOrder == std::vector<std::string> { ids[0], ids[2], ids[1] }); // ordre des programmes
    REQUIRE (presets.size() == 3);

    Library to (dst.path);
    REQUIRE (to.open());
    Preset existing = sample ("Existing");
    REQUIRE (to.save (existing));
    REQUIRE (to.assign (2, 0, existing.id));
    presets[0].id = existing.id; // collision d'identifiant : l'import doit en donner un nouveau
    CHECK (to.importBundle (manifest, presets, 2) == 3);
    const auto& bank = *to.banks().find (2);
    CHECK (bank.presets[0] == existing.id);
    CHECK (! bank.presets[1].empty());
    CHECK (! bank.presets[2].empty());
    CHECK (! bank.presets[3].empty());
    CHECK (bank.presets[1] != existing.id);
    CHECK (to.userPresets().size() == 4);
}

TEST_CASE ("library: grids persist and report missing presets", "[preset][library]")
{
    TempDir dir;
    std::string gridId;
    {
        Library lib (dir.path);
        REQUIRE (lib.open());
        Preset p = sample ("P");
        REQUIRE (lib.save (p));
        Grid& g = lib.newGrid ("Concert");
        g.assign (0, p.id);
        g.assign (1, "vanished");
        gridId = g.gridId;
        REQUIRE (lib.saveGrid (g));
        CHECK (lib.gridProblems (g) == std::vector<std::string> { "vanished" });
    }
    Library again (dir.path);
    REQUIRE (again.open());
    REQUIRE (again.grids().size() == 1);
    CHECK (again.grids()[0].gridId == gridId);
    CHECK (again.grids()[0].tiles.size() == 2);
    CHECK (again.deleteGrid (gridId));
    CHECK (again.grids().empty());
}

TEST_CASE ("library: favourites are global, survive reopening and ignore bad ids", "[preset][library]")
{
    TempDir dir;
    Preset factoryPreset = sample ("Factory one");
    factoryPreset.id = "factory-one";
    {
        Library lib (dir.path, { factoryPreset });
        REQUIRE (lib.open());
        CHECK_FALSE (lib.isFavorite ("factory-one"));
        REQUIRE (lib.setFavorite ("factory-one", true));
        CHECK (lib.isFavorite ("factory-one"));
        CHECK_FALSE (lib.setFavorite ("../evil", true));
    }
    {
        Library lib (dir.path, { factoryPreset });
        REQUIRE (lib.open());
        CHECK (lib.isFavorite ("factory-one"));
        REQUIRE (lib.setFavorite ("factory-one", false));
        CHECK_FALSE (lib.isFavorite ("factory-one"));
    }
    // deux instances ouvertes : la seconde relit le disque avant d'écrire, la première marque n'est pas perdue
    Library a (dir.path), b (dir.path);
    REQUIRE (a.open());
    REQUIRE (b.open());
    REQUIRE (a.setFavorite ("aaa", true));
    REQUIRE (b.setFavorite ("bbb", true));
    CHECK (b.isFavorite ("aaa"));
    CHECK (b.isFavorite ("bbb"));
    { std::ofstream (dir.path / "favorites.json") << "{ not json"; }
    Library c (dir.path);
    REQUIRE (c.open());
    CHECK (c.favorites().empty());
}
