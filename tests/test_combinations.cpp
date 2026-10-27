#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "neptune/core/Combinations.h"
#include "neptune/core/Control.h"
#include "neptune/core/Facade.h"
#include "neptune/core/FacadeTransfer.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/MappingEdit.h"
#include "neptune/core/Version.h"
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
    REQUIRE (stops.size() == static_cast<std::size_t> (kModulators));
    CHECK (stops.front() == stop (1, 0));
    CHECK (stops.back() == stop (1, kModulators - 1));
    CHECK (stopParams (set, 5).empty());        // piste que le jeu ne connaît pas
    CHECK (stopParams (set, -1).empty());

    const auto all = consoleParams (set);
    CHECK (all.size() == static_cast<std::size_t> (3 * kModulators + router::kSwitches + router::kMaxRules));
    CHECK (contains (all, stop (0, 3)));
    CHECK (contains (all, stop (2, kModulators - 1)));
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
    CHECK (lib.divisional (1, 0, 2)->values.size() == static_cast<std::size_t> (kModulators));
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

TEST_CASE ("combinations: a program change is a general piston, a divisional piston or a bank")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    set.set (router::switchParam (7), 1.0);
    c.arm (true);
    c.general (lib, set, 2);
    set.set (stop (1, 4), 0.0);
    c.arm (true);
    c.divisional (lib, set, 1, 3);
    set.set (router::switchParam (7), 0.0);
    set.set (stop (1, 4), 100.0);

    CHECK (c.programChange (lib, set, 2).result == Result::Recalled);            // 0..9 : général de la banque courante
    CHECK (set.get (router::switchParam (7)) == 1.0);
    CHECK (c.programChange (lib, set, 5).result == Result::Empty);
    CHECK (c.programChange (lib, set, 13, 1).result == Result::Recalled);        // 10..19 : divisionnel 4 du clavier (piste) 1
    CHECK (set.get (stop (1, 4)) == 0.0);
    CHECK (c.programChange (lib, set, 13, 2).result == Result::Empty);           // un autre clavier n'a rien à cet endroit
    CHECK (c.programChange (lib, set, 13).result == Result::Ignored);            // aucune piste sur ce canal : ignoré
    CHECK (c.programChange (lib, set, 13, 99).result == Result::Ignored);
    CHECK (c.bank() == 0);
    CHECK (c.programChange (lib, set, 23).result == Result::Done);               // 20..29 : banque 4
    CHECK (c.bank() == 3);
    CHECK (c.programChange (lib, set, 23).result == Result::Ignored);            // déjà dans cette banque : rien ne change
    CHECK (c.programChange (lib, set, 20).result == Result::Done);
    CHECK (c.bank() == 0);
    CHECK (c.programChange (lib, set, 30).result == Result::Ignored);            // le reste est ignoré
    CHECK (c.programChange (lib, set, 127).result == Result::Ignored);
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

TEST_CASE ("combinations: every command has a key, a label and a control action that round-trips")
{
    std::vector<std::string> keys;
    for (int c = 1; c <= kNumCommands; ++c)
    {
        const auto key = commandKey (c);
        CHECK_FALSE (key.empty());
        CHECK_FALSE (commandLabel (c).empty());
        CHECK (std::find (keys.begin(), keys.end(), key) == keys.end());
        keys.push_back (key);
        control::Action a = control::Action::None;
        REQUIRE (control::actionFromString (key, a));
        CHECK (control::combinationCommand (a) == c);
        CHECK (control::combinationAction (c) == a);
        CHECK (std::string (control::toString (a)) == key);
    }
    CHECK (commandKey (0).empty());
    CHECK (commandKey (kNumCommands + 1).empty());
    CHECK (control::combinationCommand (control::Action::Panic) == 0);
    CHECK (std::string (control::toString (control::Action::Panic)) == "panic");
    control::Action none;
    CHECK_FALSE (control::actionFromString ("combi_general_11", none));
    CHECK (control::kNumActions == control::kNumBaseActions + kNumCommands);
}

TEST_CASE ("combinations: run executes every kind of command")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    set.set (router::switchParam (0), 1.0);

    CHECK (c.run (kCommandSet, lib, set, 0).result == Result::Done);
    CHECK (c.armed());
    CHECK (c.run (kCommandGeneral + 2, lib, set, 0).result == Result::Stored);
    CHECK_FALSE (c.armed());
    CHECK (c.run (kCommandSet, lib, set, 0).result == Result::Done);
    CHECK (c.run (kCommandSet, lib, set, 0).result == Result::Done);   // un second appui désarme
    CHECK_FALSE (c.armed());

    set.set (router::switchParam (0), 0.0);
    CHECK (c.run (kCommandGeneral + 2, lib, set, 0).result == Result::Recalled);
    CHECK (set.get (router::switchParam (0)) == 1.0);
    CHECK (c.run (kCommandCancel, lib, set, 0).result == Result::Recalled);
    CHECK (set.get (router::switchParam (0)) == 0.0);

    c.arm (true);
    set.set (stop (2, 3), 0.0);
    CHECK (c.run (kCommandDivisional + 4, lib, set, 2).result == Result::Stored);
    CHECK (lib.divisional (2, 0, 4) != nullptr);
    CHECK (c.run (kCommandDivisional + 4, lib, set, 2).result == Result::Recalled);

    CHECK (c.run (kCommandBankNext, lib, set, 0).result == Result::Done);
    CHECK (c.bank() == 1);
    CHECK (c.run (kCommandBankPrevious, lib, set, 0).result == Result::Done);
    CHECK (c.run (kCommandBankPrevious, lib, set, 0).result == Result::Ignored);   // déjà à la première banque
    lib.sequence = { generalKey (0, 2) };
    CHECK (c.run (kCommandNext, lib, set, 0).result == Result::Recalled);
    CHECK (c.run (kCommandPrevious, lib, set, 0).result == Result::Ignored);
    CHECK (c.run (0, lib, set, 0).result == Result::Ignored);
    CHECK (c.run (kNumCommands + 1, lib, set, 0).result == Result::Ignored);
}

TEST_CASE ("combinations: a writer receives every value that changes, clamped, and nothing else")
{
    auto set = makeSet();
    Library lib;
    Controller c;
    std::vector<std::pair<ParamId, double>> written;
    c.setWriter ([&] (ParamId id, double v) { written.emplace_back (id, v); set.set (id, v); });

    set.set (stop (0, 0), 0.0);
    set.set (router::switchParam (1), 1.0);
    c.arm (true);
    c.general (lib, set, 0);
    CHECK (written.empty());                             // enregistrer n'écrit aucun paramètre
    set.set (stop (0, 0), 100.0);
    set.set (router::switchParam (1), 0.0);
    const auto out = c.general (lib, set, 0);
    CHECK (out.result == Result::Recalled);
    REQUIRE (written.size() == 2);
    CHECK (out.changes.size() == 2);
    CHECK (c.cancel (set).changes.size() == static_cast<std::size_t> (3 * kModulators));   // tous les modulateurs (100 % par défaut) passent à 0 ; le tirant 1 déjà à 0
    CHECK (written.size() == static_cast<std::size_t> (2 + 3 * kModulators));

    Memory big;
    big.used = true;
    big.values = { { stop (0, 4), 1000.0 } };
    written.clear();
    recall (set, big, [&] (ParamId id, double v) { written.emplace_back (id, v); set.set (id, v); });
    REQUIRE (written.size() == 1);
    CHECK (written[0].second == 100.0);                  // bornée avant l'écriture
}

TEST_CASE ("combinations: a memory matches the console only when every known value is the current one")
{
    auto set = makeSet();
    Memory m = capture (set, consoleParams (set));
    CHECK (matches (set, m));
    set.set (stop (1, 2), 0.0);
    CHECK_FALSE (matches (set, m));
    set.set (stop (1, 2), 100.0);
    CHECK (matches (set, m));
    Memory none;
    none.used = true;
    CHECK_FALSE (matches (set, none));                   // sans valeur : jamais allumée
    none.values = { { 424242, 1.0 } };
    CHECK_FALSE (matches (set, none));                   // seulement des paramètres inconnus
}

TEST_CASE ("combinations: a Facade button can carry a combination command")
{
    facade::Facade f;
    auto w = facade::defaultWidget (facade::WidgetType::Action, f.freeId(), 10, 10);
    w.actionMode = 1 + kCommandCancel;
    f.widgets.push_back (w);
    const auto back = facade::fromJson (facade::toJson (f));
    REQUIRE (back.widgets.size() == 1);
    CHECK (back.widgets[0].actionMode == 1 + kCommandCancel);
    f.widgets[0].actionMode = 1 + kNumCommands;
    CHECK (facade::fromJson (facade::toJson (f)).widgets[0].actionMode == 1 + kNumCommands);
    f.widgets[0].actionMode = 999;
    CHECK (facade::fromJson (facade::toJson (f)).widgets[0].actionMode == 1 + kNumCommands);   // borné
}

TEST_CASE ("version: the development version string is filled in by the build")
{
    const std::string v = neptune::core::version::kString;
    CHECK (v.rfind ("dev ", 0) == 0);
    CHECK (v.find ("@") == std::string::npos);   // aucune variable CMake laissée telle quelle
    CHECK (v.size() > 20);
}

TEST_CASE ("modulators: 32 per track, the first 16 keep their identifiers, the others have their own range")
{
    static_assert (kModulators == 32);
    CHECK (mixer::modulatorParam (0, 0) == 14000);
    CHECK (mixer::modulatorParam (2, 3) == 14000 + 2 * 16 + 3);            // inchangé : les projets et automations existants
    CHECK (mixer::modulatorParam (15, 15) == 14000 + 15 * 16 + 15);
    CHECK (mixer::modulatorParam (0, 16) == 16000);
    CHECK (mixer::modulatorParam (1, 31) == 16000 + 16 + 15);

    ParameterSet set;                                                      // aucun doublon d'identifiant sur les 16 pistes (l'ajout le refuserait)
    for (int t = 0; t < mixer::numTracks; ++t) REQUIRE (mixer::addModulatorParams (set, t, "Track " + std::to_string (t + 1) + " "));
    CHECK (set.size() == static_cast<std::size_t> (mixer::numTracks * kModulators));
    CHECK (set.find (mixer::modulatorParam (3, 20))->name == "Track 4 Modulator 21");

    // une Façade faite sur une piste se rebranche, modulateurs 17 à 32 compris
    CHECK (facade::remapParamTrack (mixer::modulatorParam (2, 20), 2, 5) == mixer::modulatorParam (5, 20));
    CHECK (facade::remapParamTrack (mixer::modulatorParam (2, 3), 2, 5) == mixer::modulatorParam (5, 3));
    ParameterSet small;
    REQUIRE (mixer::addModulatorParams (small, 1, "Track 2 "));
    CHECK (facade::instrumentParamsOfTrack (small, 1).size() == static_cast<std::size_t> (kModulators));
}

TEST_CASE ("modulators: a 32-bit mask round-trips and an old 16-bit file still reads")
{
    InstrumentDescription d;
    d.trackModulators = 0x80000001u;
    d.groups["Plein-jeu"].modulators = 0x00010000u | (1u << 31);
    ZoneDescription z;
    z.group = "Plein-jeu";
    z.modulators = 1u << 20;
    d.zones.push_back (z);
    d.modulators[25].name = "Cornet";
    d.modulators[25].keyswitchNote = 60;
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.trackModulators == 0x80000001u);
    CHECK (back.groups.at ("Plein-jeu").modulators == (0x00010000u | (1u << 31)));
    REQUIRE (back.zones.size() == 1);
    CHECK (back.zones[0].modulators == (1u << 20));
    CHECK (back.modulators[25].name == "Cornet");
    CHECK (back.modulators[25].keyswitchNote == 60);

    const auto old = instrumentFromJson ("{\"track_modulators\": 65535, \"modulators\": [{\"index\": 3, \"name\": \"Ancien\"}]}");
    CHECK (old.trackModulators == 65535u);
    CHECK (old.modulators[3].name == "Ancien");
    CHECK (instrumentFromJson ("{\"track_modulators\": 99999999999}").trackModulators == 0xFFFFFFFFu);   // borné
    CHECK (instrumentFromJson ("{\"track_modulators\": -4}").trackModulators == 0u);
}

TEST_CASE ("modulators: a group switch can be any of the 32")
{
    InstrumentDescription d;
    for (int g = 0; g < 30; ++g) d.declaredGroups.push_back ("Jeu " + std::to_string (g + 1));
    for (int g = 0; g < 30; ++g) CHECK (mapping::groupSwitch (d, "Jeu " + std::to_string (g + 1)) == g);
    CHECK (d.modulators[29].name == "Jeu 30");
    CHECK (d.groups.at ("Jeu 30").modulators == (1u << 29));
    d.declaredGroups.push_back ("Jeu 31");
    d.declaredGroups.push_back ("Jeu 32");
    d.declaredGroups.push_back ("Jeu 33");
    CHECK (mapping::groupSwitch (d, "Jeu 31") == 30);
    CHECK (mapping::groupSwitch (d, "Jeu 32") == 31);
    CHECK (mapping::groupSwitch (d, "Jeu 33") == -1);                      // les 32 sont pris
}

TEST_CASE ("combinations: the sequence and the generals can be edited from the screen")
{
    Library lib;
    CHECK_FALSE (sequenceAdd (lib, -1));
    CHECK_FALSE (sequenceAdd (lib, numGeneralKeys));
    CHECK (sequenceAdd (lib, generalKey (0, 1)));
    CHECK (sequenceAdd (lib, generalKey (2, 5)));
    CHECK (sequenceAdd (lib, generalKey (0, 1)));                      // le même général peut revenir plusieurs fois
    CHECK (lib.sequence == std::vector<int> ({ generalKey (0, 1), generalKey (2, 5), generalKey (0, 1) }));

    CHECK (sequenceMove (lib, 0, 1));
    CHECK (lib.sequence == std::vector<int> ({ generalKey (2, 5), generalKey (0, 1), generalKey (0, 1) }));
    CHECK_FALSE (sequenceMove (lib, 0, -1));                           // déjà en tête
    CHECK_FALSE (sequenceMove (lib, 2, 1));                            // déjà à la fin
    CHECK_FALSE (sequenceMove (lib, 5, -1));
    CHECK_FALSE (sequenceMove (lib, 1, 0));
    CHECK (sequenceRemove (lib, 1));
    CHECK_FALSE (sequenceRemove (lib, 2));
    CHECK_FALSE (sequenceRemove (lib, -1));
    CHECK (lib.sequence.size() == 2);

    while (sequenceAdd (lib, generalKey (1, 1))) {}
    CHECK (lib.sequence.size() == static_cast<std::size_t> (kMaxSequence));   // la liste est bornée

    Memory m;
    m.used = true;
    lib.generals[generalKey (0, 1)] = m;
    CHECK_FALSE (renameGeneral (lib, generalKey (0, 2), "Absent"));    // pas de mémoire à renommer
    CHECK (renameGeneral (lib, generalKey (0, 1), "   Plein-jeu  "));
    CHECK (lib.generals.at (generalKey (0, 1)).name == "Plein-jeu");
    CHECK (renameGeneral (lib, generalKey (0, 1), std::string (300, 'x')));
    CHECK (lib.generals.at (generalKey (0, 1)).name.size() == kMaxName);
    CHECK (renameGeneral (lib, generalKey (0, 1), "   "));
    CHECK (lib.generals.at (generalKey (0, 1)).name.empty());

    CHECK (eraseGeneral (lib, generalKey (0, 1)));
    CHECK_FALSE (eraseGeneral (lib, generalKey (0, 1)));
    CHECK (lib.general (0, 1) == nullptr);
    CHECK (lib.sequence.size() == static_cast<std::size_t> (kMaxSequence));   // les pas qui la visaient restent

    Controller c;
    auto set = makeSet();
    lib.sequence = { generalKey (0, 0), generalKey (0, 1), generalKey (0, 2) };
    c.sequenceNext (lib, set);
    c.sequenceNext (lib, set);
    c.sequenceNext (lib, set);
    CHECK (c.sequencePosition() == 2);
    c.clampSequence (2);
    CHECK (c.sequencePosition() == 1);                                 // la liste a raccourci : la position suit
    c.clampSequence (0);
    CHECK (c.sequencePosition() == -1);
    c.clampSequence (5);
    CHECK (c.sequencePosition() == -1);
}

TEST_CASE ("combinations: divisionals can be renamed and erased like generals")
{
    Library lib;
    Memory m;
    m.used = true;
    lib.divisionals[divisionalKey (2, 1, 4)] = m;
    CHECK_FALSE (renameDivisional (lib, divisionalKey (2, 1, 5), "Absent"));
    CHECK (renameDivisional (lib, divisionalKey (2, 1, 4), "  Fonds 8  "));
    CHECK (lib.divisional (2, 1, 4)->name == "Fonds 8");
    CHECK_FALSE (eraseDivisional (lib, divisionalKey (2, 1, 5)));
    CHECK (eraseDivisional (lib, divisionalKey (2, 1, 4)));
    CHECK (lib.divisional (2, 1, 4) == nullptr);
    CHECK (lib.divisionals.empty());
}
