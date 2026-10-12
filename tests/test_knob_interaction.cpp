#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "neptune/core/KnobInteraction.h"
#include <string>
#include <vector>

using namespace neptune::core::ui;
using Catch::Matchers::WithinAbs;

namespace
{
struct Rig
{
    KnobInteraction k;
    std::vector<std::string> log;
    std::vector<double> values;
    int begins = 0, ends = 0, resets = 0, modals = 0;

    explicit Rig (KnobConfig c = {}) : k (c)
    {
        KnobCallbacks cb;
        cb.beginGesture = [this] { ++begins; log.push_back ("begin"); };
        cb.endGesture = [this] { ++ends; log.push_back ("end"); };
        cb.setNormalised = [this] (double v) { values.push_back (v); log.push_back ("set"); };
        cb.resetToDefault = [this] { ++resets; log.push_back ("reset"); };
        cb.openModal = [this] { ++modals; log.push_back ("modal"); };
        k.setCallbacks (cb);
        k.setValue (0.5);
    }
};
} // namespace

TEST_CASE ("drag up raises the value with the mockup sensitivity")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 10, 100, 0);
    r.k.pointerMove (false, 10, 98, 10);   // dans la tolérance : rien
    CHECK (r.begins == 0);
    r.k.pointerMove (false, 10, 90, 20);   // franchit la tolérance : ancre ici, début de geste
    CHECK (r.begins == 1);
    r.k.pointerMove (false, 10, 40, 30);   // 50 px vers le haut depuis l'ancre
    CHECK_THAT (r.k.value(), WithinAbs (0.5 + 50 * 0.006, 1e-9));
    r.k.pointerUp (40);
    CHECK (r.ends == 1);
    CHECK (r.modals == 0);
    CHECK (r.resets == 0);
}

TEST_CASE ("value is clamped to 0..1")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 500, 0);
    r.k.pointerMove (false, 0, 400, 1);
    r.k.pointerMove (false, 0, -5000, 2);
    CHECK (r.k.value() == 1.0);
    r.k.pointerMove (false, 0, 9000, 3);
    CHECK (r.k.value() == 0.0);
    r.k.pointerUp (4);
}

TEST_CASE ("shift gives fine adjustment without a jump when toggled mid-drag")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 200, 0);
    r.k.pointerMove (false, 0, 190, 1);
    r.k.pointerMove (false, 0, 140, 2);    // 50 px normaux
    const double v1 = r.k.value();
    CHECK_THAT (v1, WithinAbs (0.5 + 50 * 0.006, 1e-9));
    r.k.pointerMove (true, 0, 140, 3);     // Shift pressé sans bouger : aucun saut
    CHECK (r.k.value() == v1);
    r.k.pointerMove (true, 0, 90, 4);      // 50 px en fin = 10 fois moins
    CHECK_THAT (r.k.value(), WithinAbs (v1 + 50 * 0.006 * 0.1, 1e-9));
    r.k.pointerUp (5);
}

TEST_CASE ("right button dragged is fine, right button released in place opens the modal")
{
    Rig r;
    r.k.pointerDown (MouseButton::Right, PointerKind::Mouse, false, 0, 100, 0);
    r.k.pointerMove (false, 0, 90, 1);
    r.k.pointerMove (false, 0, 40, 2);
    CHECK_THAT (r.k.value(), WithinAbs (0.5 + 50 * 0.006 * 0.1, 1e-9));
    r.k.pointerUp (3);
    CHECK (r.modals == 0);

    Rig q;
    q.k.pointerDown (MouseButton::Right, PointerKind::Mouse, false, 0, 100, 0);
    q.k.pointerMove (false, 1, 101, 1); // tremblement dans la tolérance
    q.k.pointerUp (2);
    CHECK (q.modals == 1);
    CHECK (q.begins == 0);
}

TEST_CASE ("long press opens the modal once, release does nothing more")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Touch, false, 0, 0, 0);
    r.k.tick (300);
    CHECK (r.modals == 0);
    r.k.tick (500);
    CHECK (r.modals == 1);
    r.k.tick (900);
    CHECK (r.modals == 1);
    r.k.pointerUp (1000);
    CHECK (r.modals == 1);
    CHECK (r.resets == 0);
    CHECK (r.begins == 0);
}

TEST_CASE ("moving cancels the long press")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 100, 0);
    r.k.pointerMove (false, 0, 80, 100);
    r.k.tick (700);
    CHECK (r.modals == 0);
    r.k.pointerUp (800);
    CHECK (r.begins == 1);
    CHECK (r.ends == 1);
}

TEST_CASE ("double click or double tap resets to default")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 10, 10, 0);
    r.k.pointerUp (50);
    CHECK (r.resets == 0);
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 11, 10, 200);
    r.k.pointerUp (240);
    CHECK (r.resets == 1);
    CHECK (r.modals == 0);

    // un troisième appui ne compte pas comme double-clic avec le précédent
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 11, 10, 300);
    r.k.pointerUp (320);
    CHECK (r.resets == 1);
}

TEST_CASE ("slow, distant or dragged taps are not a double tap")
{
    Rig slow;
    slow.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 0);
    slow.k.pointerUp (10);
    slow.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 900);
    slow.k.pointerUp (910);
    CHECK (slow.resets == 0);

    Rig far;
    far.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 0);
    far.k.pointerUp (10);
    far.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 50, 0, 100);
    far.k.pointerUp (110);
    CHECK (far.resets == 0);

    Rig drag;
    drag.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 0);
    drag.k.pointerUp (10);
    drag.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 100);
    drag.k.pointerMove (false, 0, -30, 110);
    drag.k.pointerUp (120);
    CHECK (drag.resets == 0);
    CHECK (drag.begins == 1);
}

TEST_CASE ("touch tolerates a larger finger movement than the mouse")
{
    Rig touch;
    touch.k.pointerDown (MouseButton::Left, PointerKind::Touch, false, 0, 0, 0);
    touch.k.pointerMove (false, 0, -6, 10);
    CHECK (touch.begins == 0);
    touch.k.pointerMove (false, 0, -9, 20);
    CHECK (touch.begins == 1);
    touch.k.pointerUp (30);

    Rig mouse;
    mouse.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 0, 0);
    mouse.k.pointerMove (false, 0, -6, 10);
    CHECK (mouse.begins == 1);
}

TEST_CASE ("wheel notches form a single gesture closed after an idle delay")
{
    Rig r;
    r.k.wheel (1, false, 0);
    r.k.wheel (1, false, 50);
    r.k.wheel (-1, false, 100);
    CHECK (r.begins == 1);
    CHECK (r.ends == 0);
    CHECK_THAT (r.k.value(), WithinAbs (0.52, 1e-9));
    r.k.tick (300);
    CHECK (r.ends == 0);
    r.k.tick (600);
    CHECK (r.ends == 1);
    r.k.wheel (1, true, 700);
    CHECK_THAT (r.k.value(), WithinAbs (0.522, 1e-9)); // pas fin
    CHECK (r.begins == 2);
}

TEST_CASE ("keys are self-contained gestures and honour discrete steps")
{
    Rig r;
    CHECK (r.k.key (Key::Up, false));
    CHECK_THAT (r.k.value(), WithinAbs (0.51, 1e-9));
    CHECK (r.k.key (Key::Down, true));
    CHECK_THAT (r.k.value(), WithinAbs (0.509, 1e-9));
    CHECK (r.k.key (Key::PageUp, false));
    CHECK_THAT (r.k.value(), WithinAbs (0.609, 1e-9));
    r.k.key (Key::End, false);
    CHECK (r.k.value() == 1.0);
    r.k.key (Key::Home, false);
    CHECK (r.k.value() == 0.0);
    CHECK (r.begins == r.ends);

    Rig d;
    d.k.setSteps (31); // ex. algorithme 1..32
    d.k.setValue (0.0);
    d.k.key (Key::Right, false);
    CHECK_THAT (d.k.value(), WithinAbs (1.0 / 31, 1e-9));
    d.k.key (Key::PageUp, false); // 3 pas (31/10)
    CHECK_THAT (d.k.value(), WithinAbs (4.0 / 31, 1e-9));
}

TEST_CASE ("enter opens the modal and delete resets")
{
    Rig r;
    r.k.key (Key::Enter, false);
    r.k.key (Key::Delete, false);
    CHECK (r.modals == 1);
    CHECK (r.resets == 1);
}

TEST_CASE ("cancelling a drag closes the gesture exactly once")
{
    Rig r;
    r.k.pointerDown (MouseButton::Left, PointerKind::Mouse, false, 0, 100, 0);
    r.k.pointerMove (false, 0, 50, 1);
    CHECK (r.k.gestureOpen());
    r.k.pointerCancel();
    r.k.pointerUp (2); // le relâchement tardif ne referme rien
    CHECK (r.begins == 1);
    CHECK (r.ends == 1);
    CHECK_FALSE (r.k.gestureOpen());
}

TEST_CASE ("hit size never goes below the minimum touch target")
{
    KnobConfig c;
    CHECK (effectiveHitSize (30, c) == 44);
    CHECK (effectiveHitSize (60, c) == 60);
    CHECK (c.minTouchTarget == 44);
}
