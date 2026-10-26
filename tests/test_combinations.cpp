#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "neptune/core/Combinations.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/NoteRouter.h"

using namespace neptune::core;
using namespace neptune::core::combi;

namespace
{
// Trois claviers (pistes 0 à 2) : leurs modulateurs, plus les tirants et les transpositions du routeur.
ParameterSet makeSet()
{
    ParameterSet set;
    REQUIRE (router::addSwitchParams (set));
    for (int t = 0; t < 3; ++t) REQUIRE (mixer::addModulatorParams (set, t, "Track " + std::to_string (t + 1) + " "));
    return set;
}

ParamId stop (int track, int i) { return mixer::modulatorParam (track, i); }

bool contains (const std::vector<ParamId>& ids, ParamId id) { return std::find (ids.begin(), ids.end(), id) != ids.end(); }
} // namespace

TEST_CASE ("combinations: console parameters are the stops, the router switches and the rule transpositions")
{
    auto set = makeSet();
    const auto stops = stopParams (set, 1);
    REQUIRE (stops.size() == 16);
    CHECK (stops.front() == stop (1, 0));
    CHECK (stops.back() == stop (1, 15));
    CHECK (stopParams (set, 5).empty());        // piste que le jeu ne connaît pas
    CHECK (stopParams (set, -1).empty());

    const auto all = consoleParams (set);
    CHECK (all.size() == 3 * 16 + router::kSwitches + router::kMaxRules);
    CHECK (contains (all, stop (0, 3)));
    CHECK (contains (all, stop (2, 15)));
    CHECK (contains (all, router::switchParam (31)));
    CHECK (contains (all, router::transposeParam (0)));
    CHECK_FALSE (contains (all, stop (3, 0)));
}

TEST_CASE ("combinations: capture and recall restore real values and report only what changed")
{
    auto set = makeSet();
    set.set (stop (0, 0), 0.0);
    set.set (stop (0, 1), 100.0);
    set.set (router::switchParam (2), 1.0);
    set.set (router::transposeParam (1), 12.0);
    const auto mem = capture (set, consoleParams (set));
    CHECK (mem.used);
    CHECK (mem.values.size() == consoleParams (set).size());

    set.set (stop (0, 0), 100.0);
    set.set (router::switchParam (2), 0.0);
    const auto changes = recall (set, mem);
    CHECK (set.get (stop (0, 0)) == 0.0);
    CHECK (set.get (router::switchParam (2)) == 1.0);
    CHECK (set.get (router::transposeParam (1)) == 12.0);
    REQUIRE (changes.size() == 2);                       // les deux seuls paramètres qui avaient bougé
    CHECK (recall (set, mem).empty());                   // rappel sans effet : rien ne change

    Memory odd;
    odd.used = true;
    odd.values = { { 987654, 1.0 }, { stop (0, 4), 500.0 }, { router::transposeParam (0), 99.0 } };
    recall (set, odd);                                   // id inconnu ignoré, valeurs bornées au paramètre
    CHECK (set.get (stop (0, 4)) == 100.0);
    CHECK (set.get (router::transposeParam (0)) == 48.0);
}

TEST_CASE ("combinations: Set stores a general, the next press recalls it, a stored name is kept")
{
    auto set = makeSet();
    Library lib;
    Controller c;

    CHECK (c.general (lib, set, 0).result == Result::Empty);
    CHECK (c.general (lib, set, 10).result == Result::Ignored);
    CHECK (c.general (lib, set, -1).result == Result::Ignored);

    set.set (stop (0, 0), 0.0);
    set.set (router::switchParam (0), 1.0);
    c.arm (true);
    CHECK (c.setActive());
    CHECK (c.general (lib, set, 3).result == Result::Stored);
    CHECK_FALSE (c.armed());                             // l'armement se défait après un enregistrement
    REQUIRE (lib.general (0, 3) != nullptr);
    CHECK (lib.general (0, 3)->name.empty());

    lib.generals[generalKey (0, 3)].name = "Plein-jeu";
    set.set (stop (0, 0), 100.0);
    set.set (router::switchParam (0), 0.0);
    const auto out = c.general (lib, set, 3);
    CHECK (out.result == Result::Recalled);
    CHECK (out.changes.size() == 2);
    CHECK (set.get (stop (0, 0)) == 0.0);
    CHECK (set.get (router::switchParam (0)) == 1.0);

    c.setHeld (true);                                    // touche tenue : plusieurs enregistrements de suite
    set.set (router::switchParam (0), 0.0);
    CHECK (c.general (lib, set, 3).result == Result::Stored);
    CHECK (c.general (lib, set, 4).result == Result::Stored);
    CHECK (c.held());
    CHECK (lib.general (0, 3)->name == "Plein-jeu");     // le nom existant est conservé en écrasant
    c.setHeld (false);
    CHECK (c.general (lib, set, 4).result == Result::Recalled);
}

TEST_CASE ("combinations: a divisional only sets the stops of its own keyboard")
{
    auto set = makeSet();
    Library lib;
    Controller c;

    set.set (stop (1, 0), 0.0);
    set.set (stop (1, 5), 100.0);
    set.set (stop (0, 0), 0.0);
    set.set (router::switchParam (0), 1.0);
    c.arm (true);
    CHECK (c.divisional (lib, set, 1, 2).result == Result::Stored);
    REQUIRE (lib.divisional (1, 0, 2) != nullptr);
    CHECK (lib.divisional (1, 0, 2)->values.size() == 16);
    CHECK (lib.divisional (0, 0, 2) == nullptr);

    set.set (stop (1, 0), 100.0);
    set.set (stop (1, 5), 0.0);
    set.set (stop (0, 0), 100.0);                        // un autre clavier : ne doit pas bouger
    set.set (router::switchParam (0), 0.0);              // un accouplement : ne doit pas bouger
    const auto out = c.divisional (lib, set, 1, 2);
    CHECK (out.result == Result::Recalled);
    CHECK (set.get (stop (1, 0)) == 0.0);
    CHECK (set.get (stop (1, 5)) == 100.0);
    CHECK (set.get (stop (0, 0)) == 100.0);
    CHECK (set.get (router::switchParam (0)) == 0.0);

    CHECK (c.divisional (lib, set, 2, 2).result == Result::Empty);
    CHECK (c.divisional (lib, set, 16, 0).result == Result::Ignored);
    CHECK (c.divisional (lib, set, 0, 10).result == Result::Ignored);
}

TEST_CASE ("combinations: the general cancel pushes everything back and leaves the memories alone")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    set.set (stop (0, 0), 100.0);
    set.set (stop (2, 7), 100.0);
    set.set (router::switchParam (4), 1.0);
    set.set (router::transposeParam (2), -12.0);
    c.arm (true);
    c.general (lib, set, 0);
    const Library before = lib;

    const auto out = c.cancel (set);
    CHECK (out.result == Result::Recalled);
    CHECK (set.get (stop (0, 0)) == 0.0);
    CHECK (set.get (stop (2, 7)) == 0.0);
    CHECK (set.get (router::switchParam (4)) == 0.0);
    CHECK (set.get (router::transposeParam (2)) == 0.0);
    CHECK (lib == before);
    CHECK (c.cancel (set).changes.empty());              // déjà tout repoussé

    CHECK (c.general (lib, set, 0).result == Result::Recalled);   // le général revient
    CHECK (set.get (stop (0, 0)) == 100.0);
}

TEST_CASE ("combinations: banks are separate and limited")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    CHECK (c.bank() == 0);
    CHECK_FALSE (c.previousBank());
    set.set (router::switchParam (0), 1.0);
    c.arm (true);
    c.general (lib, set, 0);

    CHECK (c.nextBank());
    CHECK (c.bank() == 1);
    CHECK (c.general (lib, set, 0).result == Result::Empty);       // le piston 1 de la banque 2 est vide
    set.set (router::switchParam (0), 0.0);
    c.arm (true);
    c.general (lib, set, 0);
    CHECK (lib.general (0, 0) != nullptr);
    CHECK (lib.general (1, 0) != nullptr);
    CHECK (lib.general (0, 0)->values != lib.general (1, 0)->values);

    c.setBank (99);
    CHECK (c.bank() == kBanks - 1);
    CHECK_FALSE (c.nextBank());
    c.setBank (-4);
    CHECK (c.bank() == 0);
}

TEST_CASE ("combinations: the sequencer steps through the generals without wrapping")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    for (int p = 0; p < 4; ++p)
    {
        set.set (router::switchParam (0), p == 1 || p == 3 ? 1.0 : 0.0);
        set.set (router::switchParam (1), p >= 2 ? 1.0 : 0.0);
        c.arm (true);
        c.general (lib, set, p);
    }
    lib.sequence = { generalKey (0, 0), generalKey (0, 1), generalKey (0, 9), generalKey (0, 3) };   // le pas 3 est vide

    CHECK (c.sequencePosition() == -1);
    CHECK (c.sequencePrevious (lib, set).result == Result::Ignored);
    CHECK (c.sequenceNext (lib, set).result == Result::Recalled);
    CHECK (c.sequencePosition() == 0);
    CHECK (c.sequenceNext (lib, set).result == Result::Recalled);
    CHECK (set.get (router::switchParam (0)) == 1.0);
    CHECK (c.sequenceNext (lib, set).result == Result::Empty);     // pas vide : la position avance quand même
    CHECK (c.sequencePosition() == 2);
    CHECK (c.sequenceNext (lib, set).result == Result::Recalled);
    CHECK (c.sequenceNext (lib, set).result == Result::Ignored);   // fin de liste : reste en place
    CHECK (c.sequencePosition() == 3);
    CHECK (c.sequencePrevious (lib, set).result == Result::Empty);
    CHECK (c.sequencePrevious (lib, set).result == Result::Recalled);
    CHECK (c.sequencePosition() == 1);

    c.general (lib, set, 3);                                       // rappel direct d'un général de la liste : la position s'y cale
    CHECK (c.sequencePosition() == 3);
    c.resetSequence();
    CHECK (c.sequencePosition() == -1);
    CHECK (c.sequenceNext (Library(), set).result == Result::Ignored);   // liste vide
}

TEST_CASE ("combinations: a program change is a piston of the current bank")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    set.set (router::switchParam (7), 1.0);
    c.arm (true);
    c.general (lib, set, 2);
    set.set (router::switchParam (7), 0.0);
    CHECK (c.programChange (lib, set, 2).result == Result::Recalled);
    CHECK (set.get (router::switchParam (7)) == 1.0);
    CHECK (c.programChange (lib, set, 5).result == Result::Empty);
    CHECK (c.programChange (lib, set, 10).result == Result::Ignored);
    CHECK (c.programChange (lib, set, -1).result == Result::Ignored);
}

TEST_CASE ("combinations: JSON round trip, bounds and tolerant reading")
{
    Library lib;
    Memory a;
    a.used = true;
    a.name = "Récit à Grand-orgue";
    a.values = { { stop (0, 0), 100.0 }, { router::switchParam (3), 1.0 }, { router::transposeParam (0), -12.0 } };
    lib.generals[generalKey (2, 5)] = a;
    Memory b;
    b.used = true;                                       // sans valeur : valide
    lib.divisionals[divisionalKey (3, 1, 9)] = b;
    lib.sequence = { generalKey (2, 5), generalKey (0, 0) };
    CHECK (fromJson (toJson (lib)) == lib);
    CHECK (Library().empty());
    CHECK_FALSE (lib.empty());

    // bornes : clef hors plage, mémoire non enregistrée, nom trop long, liste trop longue
    Library bad;
    bad.generals[numGeneralKeys] = a;
    bad.generals[-1] = a;
    bad.divisionals[numDivisionalKeys + 3] = a;
    bad.generals[generalKey (0, 1)].name = "sans enregistrement";   // used == false
    Memory longName = a;
    longName.name = std::string (500, 'x');
    longName.values.emplace_back (0, 1.0);                         // identifiant nul retiré
    bad.generals[generalKey (0, 2)] = longName;
    bad.sequence.assign (200, generalKey (1, 1));
    bad.sequence.push_back (-5);
    const auto clean = sanitised (bad);
    CHECK (clean.generals.size() == 1);
    CHECK (clean.divisionals.empty());
    CHECK (clean.generals.at (generalKey (0, 2)).name.size() == kMaxName);
    CHECK (clean.generals.at (generalKey (0, 2)).values.size() == 3);
    CHECK (clean.sequence.size() == static_cast<std::size_t> (kMaxSequence));

    CHECK (fromJson ("").empty());
    CHECK (fromJson ("[1,2]").empty());
    CHECK (fromJson ("{\"generals\":[{\"bank\":99,\"piston\":0,\"values\":[]},{\"bank\":1,\"piston\":2,\"name\":7,\"values\":[[5,1],[\"x\",2],[6]]}]}")
               .generals.size() == 1);
    const auto tolerant = fromJson ("{\"generals\":[{\"bank\":1,\"piston\":2,\"values\":[[5,1],[\"x\",2],[6]]}],\"sequence\":[{\"bank\":0,\"piston\":1},{\"bank\":-1}]}");
    REQUIRE (tolerant.generals.size() == 1);
    CHECK (tolerant.generals.at (generalKey (1, 2)).values == std::vector<std::pair<ParamId, double>> ({ { 5, 1.0 } }));
    CHECK (tolerant.sequence == std::vector<int> ({ generalKey (0, 1) }));
}

TEST_CASE ("combinations: the library lives in the mixer state")
{
    MixerState m;
    CHECK (toJson (m).find ("combinations") == std::string::npos);   // absent tant qu'il n'y en a pas
    Memory a;
    a.used = true;
    a.name = "Tutti";
    a.values = { { stop (0, 1), 100.0 } };
    m.combinations.generals[generalKey (0, 0)] = a;
    m.combinations.sequence = { generalKey (0, 0) };
    const auto back = mixerFromJson (toJson (m));
    CHECK (back.combinations == m.combinations);
    CHECK (back == m);

    MixerState other = m;
    other.combinations.generals[generalKey (0, 0)].name = "Autre";
    CHECK_FALSE (other == m);
}
