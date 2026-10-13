// Matrice notes × vélocité : places, déplacement, étirement, duplication, annulation par différences.
#include <catch2/catch_test_macros.hpp>

#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/MappingEdit.h"

using namespace neptune::core;
using namespace neptune::core::mapping;

namespace
{
ZoneDescription zone (const std::string& path, int kl, int kh, int vl, int vh, int root, int rr = 0, const std::string& group = {},
                      bool release = false)
{
    ZoneDescription z;
    z.samplePath = path;
    z.keyLow = kl; z.keyHigh = kh; z.velLow = vl; z.velHigh = vh;
    z.rootNote = root;
    z.roundRobin = rr;
    z.group = group;
    z.onRelease = release;
    return z;
}

InstrumentDescription sample()
{
    InstrumentDescription d;
    d.zones = {
        zone ("a1.wav", 60, 62, 1, 64, 61, 1),
        zone ("a2.wav", 60, 62, 1, 64, 61, 2),            // alternative round-robin de la même place
        zone ("b.wav", 60, 62, 65, 127, 61),
        zone ("c.wav", 63, 65, 1, 127, 64),
        zone ("p.wav", 40, 41, 1, 127, 40, 0, "pedal"),
        zone ("r.wav", 60, 62, 1, 127, 61, 0, {}, true),
    };
    return d;
}
} // namespace

TEST_CASE ("places group round-robin alternates and respect the view")
{
    const auto d = sample();
    const auto main = places (d, View {});
    REQUIRE (main.size() == 3);
    CHECK (main[0].keyLow == 60);
    CHECK (main[0].zones == std::vector<int> { 0, 1 }); // a1 + a2 : une seule place
    CHECK_FALSE (main[0].overlaps);

    CHECK (places (d, View { View::Category::Release, {} }).size() == 1);
    CHECK (places (d, View { View::Category::Attack, "pedal" }).size() == 1);
    CHECK (places (d, View { View::Category::Attack, "absent" }).empty());
    const auto vs = views (d);
    REQUIRE (vs.size() == 3); // attaque, relâchement, Groupe « pedal »
    CHECK (vs[0] == View {});
    CHECK (vs[1].category == View::Category::Release);
    CHECK (vs[2].group == "pedal");
}

TEST_CASE ("overlaps are flagged, not prevented")
{
    auto d = sample();
    d.zones[3].keyLow = 62; // c recouvre a/b sur la note 62
    const auto p = places (d, View {});
    int flagged = 0;
    for (const auto& x : p) flagged += x.overlaps ? 1 : 0;
    CHECK (flagged == 3); // a (1-64), b (65-127) et c (1-127) se coupent deux à deux avec c
    d.zones[3].keyLow = 63;
    for (const auto& x : places (d, View {})) CHECK_FALSE (x.overlaps);
}

TEST_CASE ("move: whole selection shifts as a block, root stays unless asked")
{
    const auto d = sample();
    int ak = 0, av = 0;
    auto m = moveZones (d, { 0, 1, 2 }, 5, 0, false, &ak, &av);
    CHECK (ak == 5);
    CHECK (m.zones[0].keyLow == 65);
    CHECK (m.zones[1].keyHigh == 67);
    CHECK (m.zones[0].rootNote == 61); // la racine reste
    CHECK (m.zones[3].keyLow == 63);   // zone non sélectionnée inchangée

    m = moveZones (d, { 0 }, 5, 0, true);
    CHECK (m.zones[0].rootNote == 66); // Maj : la racine suit

    // borné : la zone la plus à droite (127) empêche tout déplacement vers la droite
    auto edge = d;
    edge.zones[3].keyHigh = 127;
    m = moveZones (edge, { 0, 3 }, 10, 0, false, &ak, &av);
    CHECK (ak == 0);
    CHECK (m.zones[0].keyLow == 60);
    // vélocité : 127 en haut, 1 en bas
    m = moveZones (d, { 0 }, 0, 100, false, &ak, &av);
    CHECK (av == 63);
    CHECK (m.zones[0].velHigh == 127);
    m = moveZones (d, { 0 }, 0, -100, false, &ak, &av);
    CHECK (m.zones[0].velLow == 1);
    CHECK (moveZones (d, {}, 3, 3, false) == d);
    CHECK (moveZones (d, { 99, -1 }, 3, 3, false) == d); // indices invalides ignorés
}

TEST_CASE ("resize: one edge, same delta for all, clamped so nothing inverts")
{
    const auto d = sample();
    int applied = 0;
    auto r = resizeEdge (d, { 0, 1 }, Edge::KeyHigh, 4, &applied);
    CHECK (applied == 4);
    CHECK (r.zones[0].keyHigh == 66);
    CHECK (r.zones[1].keyHigh == 66);
    CHECK (r.zones[0].keyLow == 60);
    CHECK (r.zones[0].rootNote == 61);

    // la zone étroite (63-65, largeur 3) borne le delta de toute la sélection
    r = resizeEdge (d, { 0, 3 }, Edge::KeyLow, 10, &applied);
    CHECK (applied == 2); // min (62-60, 65-63)
    CHECK (r.zones[0].keyLow == 62);
    CHECK (r.zones[3].keyLow == 65);

    r = resizeEdge (d, { 0 }, Edge::VelHigh, -100, &applied);
    CHECK (r.zones[0].velHigh == r.zones[0].velLow);
    r = resizeEdge (d, { 2 }, Edge::VelHigh, 50, &applied);
    CHECK (r.zones[2].velHigh == 127);
    r = resizeEdge (d, { 0 }, Edge::KeyLow, -100, &applied);
    CHECK (r.zones[0].keyLow == 0);
}

TEST_CASE ("duplicate adds copies at the end with the same sample and root")
{
    const auto d = sample();
    std::vector<int> fresh;
    const auto c = duplicateZones (d, { 2, 3 }, 12, 0, &fresh);
    REQUIRE (c.zones.size() == d.zones.size() + 2);
    CHECK (fresh == std::vector<int> { 6, 7 });
    CHECK (c.zones[6].samplePath == "b.wav");
    CHECK (c.zones[6].keyLow == 72);
    CHECK (c.zones[6].rootNote == 61);
    CHECK (c.zones[2].keyLow == 60); // l'original ne bouge pas
    CHECK (c.zones[7].keyHigh == 77);
}

TEST_CASE ("instrument edit command stores only the changed zones and merges a drag into one entry")
{
    auto d = sample();
    InstrumentDescription current = d;
    auto get = [&] { return current; };
    auto set = [&] (const InstrumentDescription& v) { current = v; };
    UndoManager um;

    um.beginGesture();
    for (int step = 1; step <= 5; ++step)
    {
        const auto next = moveZones (d, { 0, 1 }, step, 0, false); // toujours depuis le début du geste
        um.perform (std::make_unique<InstrumentEditCommand> (current, next, get, set, "Move", "matrix"));
    }
    um.endGesture();
    CHECK (um.undoSize() == 1);
    CHECK (current.zones[0].keyLow == 65);

    um.undo();
    CHECK (current == d);
    um.redo();
    CHECK (current.zones[0].keyLow == 65);
    CHECK (current.zones[3] == d.zones[3]);

    // le nombre de zones change : copie complète ; un déplacement dans le même geste s'y fond (copie puis glissé)
    um.clear();
    current = d;
    um.beginGesture();
    std::vector<int> fresh;
    const auto dup = duplicateZones (d, { 2 }, 0, 0, &fresh);
    um.perform (std::make_unique<InstrumentEditCommand> (current, dup, get, set, "Duplicate", "matrix"));
    const auto moved = moveZones (dup, fresh, 7, 0, false);
    um.perform (std::make_unique<InstrumentEditCommand> (current, moved, get, set, "Move", "matrix"));
    um.endGesture();
    CHECK (um.undoSize() == 1);
    CHECK (current.zones.size() == d.zones.size() + 1);
    CHECK (current.zones.back().keyLow == 67);
    um.undo();
    CHECK (current == d);
    um.redo();
    CHECK (current == moved);
}

TEST_CASE ("an edit that changes only load settings round-trips through undo")
{
    InstrumentDescription current = sample();
    const auto before = current;
    auto get = [&] { return current; };
    auto set = [&] (const InstrumentDescription& v) { current = v; };
    UndoManager um;
    auto next = current;
    next.streamFromDisk = true;
    next.preloadKB = 512;
    next.unloadedGroups = { "pedal" };
    um.perform (std::make_unique<InstrumentEditCommand> (current, next, get, set, "Settings"));
    CHECK (current.streamFromDisk);
    um.undo();
    CHECK (current == before);
    um.redo();
    CHECK (current == next);
}
