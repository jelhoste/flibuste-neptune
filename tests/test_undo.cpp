#include <catch2/catch_test_macros.hpp>

#include "neptune/core/Undo.h"

using namespace neptune::core;

namespace
{
struct Fixture
{
    ParameterSet ps;
    UndoManager um;
    Fixture()
    {
        ps.add (ParamSpec::continuous (1, "Volume", Unit::Decibel, -60, 6, 0));
        ps.add (ParamSpec::continuous (2, "Pan", Unit::Percent, -100, 100, 0));
        ps.add (ParamSpec::toggle (3, "Mute"));
    }
};
} // namespace

TEST_CASE ("undo and redo a single parameter change")
{
    Fixture f;
    CHECK_FALSE (f.um.canUndo());

    CHECK (f.um.setParameter (f.ps, 1, -12.0));
    CHECK (f.ps.get (1) == -12.0);
    CHECK (f.um.canUndo());
    CHECK (f.um.undoName() == "Set Volume");

    CHECK (f.um.undo());
    CHECK (f.ps.get (1) == 0.0);
    CHECK (f.um.canRedo());
    CHECK (f.um.redoName() == "Set Volume");

    CHECK (f.um.redo());
    CHECK (f.ps.get (1) == -12.0);
    CHECK_FALSE (f.um.canRedo());
}

TEST_CASE ("no history entry when the value does not change")
{
    Fixture f;
    CHECK_FALSE (f.um.setParameter (f.ps, 1, 0.0));  // déjà à 0
    CHECK_FALSE (f.um.setParameter (f.ps, 99, 1.0)); // id inconnu
    CHECK (f.um.undoSize() == 0);

    f.um.setParameter (f.ps, 1, 100.0); // borné à 6
    CHECK (f.um.undoSize() == 1);
    CHECK_FALSE (f.um.setParameter (f.ps, 1, 500.0)); // toujours 6 : inchangé
    CHECK (f.um.undoSize() == 1);
}

TEST_CASE ("a new action clears the redo stack")
{
    Fixture f;
    f.um.setParameter (f.ps, 1, -6.0);
    f.um.setParameter (f.ps, 1, -12.0);
    f.um.undo();
    CHECK (f.um.redoSize() == 1);

    f.um.setParameter (f.ps, 2, 50.0);
    CHECK (f.um.redoSize() == 0);
    CHECK_FALSE (f.um.redo());
}

TEST_CASE ("a gesture merges consecutive changes of the same parameter")
{
    Fixture f;
    f.um.beginGesture();
    f.um.setParameter (f.ps, 1, -1.0);
    f.um.setParameter (f.ps, 1, -2.0);
    f.um.setParameter (f.ps, 1, -3.0);
    f.um.endGesture();

    CHECK (f.um.undoSize() == 1);
    CHECK (f.ps.get (1) == -3.0);
    f.um.undo();
    CHECK (f.ps.get (1) == 0.0); // retour à la valeur d'avant le geste
    f.um.redo();
    CHECK (f.ps.get (1) == -3.0);
}

TEST_CASE ("a gesture does not merge different parameters")
{
    Fixture f;
    f.um.beginGesture();
    f.um.setParameter (f.ps, 1, -5.0);
    f.um.setParameter (f.ps, 2, 40.0);
    f.um.endGesture();
    CHECK (f.um.undoSize() == 2);
}

TEST_CASE ("changes outside a gesture are never merged")
{
    Fixture f;
    f.um.setParameter (f.ps, 1, -1.0);
    f.um.setParameter (f.ps, 1, -2.0);
    CHECK (f.um.undoSize() == 2);
}

TEST_CASE ("two gestures on the same parameter stay separate")
{
    Fixture f;
    f.um.beginGesture();
    f.um.setParameter (f.ps, 1, -1.0);
    f.um.endGesture();
    f.um.beginGesture();
    f.um.setParameter (f.ps, 1, -2.0);
    f.um.endGesture();
    CHECK (f.um.undoSize() == 2);
}

TEST_CASE ("a transaction is one undo step and undoes in reverse order")
{
    Fixture f;
    f.um.beginTransaction ("Reset track");
    f.um.setParameter (f.ps, 1, -10.0);
    f.um.setParameter (f.ps, 2, 30.0);
    f.um.setParameter (f.ps, 3, 1.0);
    CHECK_FALSE (f.um.canUndo()); // pas d'undo au milieu d'une transaction
    f.um.endTransaction();

    CHECK (f.um.undoSize() == 1);
    CHECK (f.um.undoName() == "Reset track");

    f.um.undo();
    CHECK (f.ps.get (1) == 0.0);
    CHECK (f.ps.get (2) == 0.0);
    CHECK (f.ps.get (3) == 0.0);

    f.um.redo();
    CHECK (f.ps.get (1) == -10.0);
    CHECK (f.ps.get (2) == 30.0);
    CHECK (f.ps.get (3) == 1.0);
}

TEST_CASE ("nested transactions produce one entry and empty ones none")
{
    Fixture f;
    f.um.beginTransaction ("outer");
    f.um.beginTransaction ("inner");
    f.um.setParameter (f.ps, 1, -3.0);
    f.um.endTransaction();
    f.um.setParameter (f.ps, 2, 10.0);
    f.um.endTransaction();
    CHECK (f.um.undoSize() == 1);
    CHECK (f.um.undoName() == "outer");

    f.um.beginTransaction ("empty");
    f.um.endTransaction();
    CHECK (f.um.undoSize() == 1);
}

TEST_CASE ("history depth is bounded")
{
    ParameterSet ps;
    ps.add (ParamSpec::continuous (1, "x", Unit::None, 0, 1000, 0));
    UndoManager um (5);
    for (int i = 1; i <= 20; ++i) um.setParameter (ps, 1, i);
    CHECK (um.undoSize() == 5);

    int steps = 0;
    while (um.undo()) ++steps;
    CHECK (steps == 5);
    CHECK (ps.get (1) == 15.0); // les 15 premières entrées ont été oubliées
}

TEST_CASE ("revision changes whenever the history changes")
{
    Fixture f;
    const auto r0 = f.um.revision();
    f.um.setParameter (f.ps, 1, -1.0);
    const auto r1 = f.um.revision();
    CHECK (r1 != r0);
    f.um.undo();
    CHECK (f.um.revision() != r1);
    f.um.clear();
    CHECK (f.um.undoSize() == 0);
    CHECK (f.um.redoSize() == 0);
}

TEST_CASE ("undo and redo on an empty history are harmless")
{
    Fixture f;
    CHECK_FALSE (f.um.undo());
    CHECK_FALSE (f.um.redo());
    CHECK (f.um.undoName().empty());
}

TEST_CASE ("snapshot command: undo/redo restores whole values, a gesture merges into one entry")
{
    UndoManager um;
    int value = 0;
    auto apply = [&value] (const int& v) { value = v; };
    auto edit = [&] (int to, const char* key)
    {
        um.perform (std::make_unique<SnapshotCommand<int>> (value, to, apply, "Edit", key));
    };

    um.beginGesture();
    edit (1, "k"); edit (2, "k"); edit (3, "k");
    um.endGesture();
    CHECK (value == 3);
    CHECK (um.undoSize() == 1);

    edit (4, "k"); // hors geste : entrée distincte
    edit (5, "k");
    CHECK (um.undoSize() == 3);

    um.beginGesture();
    edit (6, ""); edit (7, ""); // clé vide : jamais fusionnées
    um.endGesture();
    CHECK (um.undoSize() == 5);

    CHECK (um.undo()); CHECK (um.undo()); CHECK (value == 5);
    CHECK (um.undo()); CHECK (value == 4);
    CHECK (um.undo()); CHECK (value == 3);
    CHECK (um.undo()); CHECK (value == 0);
    CHECK (um.redo()); CHECK (value == 3);
}
