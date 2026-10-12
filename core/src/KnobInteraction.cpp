#include "neptune/core/KnobInteraction.h"
#include <cmath>

namespace neptune::core::ui
{
void KnobInteraction::emitValue (double v)
{
    v = clamp01 (v);
    if (v == value_) return;
    value_ = v;
    if (cb_.setNormalised) cb_.setNormalised (v);
}

void KnobInteraction::closeWheelGesture()
{
    if (! wheelOpen_) return;
    wheelOpen_ = false;
    emitEnd();
}

void KnobInteraction::oneShot (double newValue)
{
    emitBegin();
    emitValue (newValue);
    emitEnd();
}

void KnobInteraction::pointerDown (MouseButton button, PointerKind kind, bool shift, double x, double y, double timeMs)
{
    closeWheelGesture();
    if (state_ == State::Dragging) pointerCancel(); // un second appui pendant un glissé : on referme le premier

    button_ = button;
    kind_ = kind;
    downX_ = x;
    downY_ = y;
    downTime_ = timeMs;
    fineActive_ = shift || button == MouseButton::Right;
    state_ = State::Pressed;

    secondPress_ = false;
    if (button == MouseButton::Left && haveLastTap_)
    {
        const double r = kind == PointerKind::Touch ? cfg_.doubleTapRadiusTouch : cfg_.doubleTapRadiusMouse;
        const double dx = x - lastTapX_, dy = y - lastTapY_;
        secondPress_ = (timeMs - lastTapTime_ <= cfg_.doubleTapMs) && (dx * dx + dy * dy <= r * r);
    }
}

void KnobInteraction::pointerMove (bool shift, double x, double y, double)
{
    if (state_ == State::Pressed)
    {
        const double dx = x - downX_, dy = y - downY_;
        if (dx * dx + dy * dy < slop() * slop()) return;
        // Au-delà de la tolérance : c'est un glissé. Ancré ici pour éviter un saut de valeur.
        state_ = State::Dragging;
        dragging_ = true;
        haveLastTap_ = false;
        secondPress_ = false;
        anchorY_ = y;
        anchorValue_ = value_;
        fineActive_ = (shift && kind_ == PointerKind::Mouse) || button_ == MouseButton::Right;
        emitBegin();
        return;
    }
    if (state_ != State::Dragging) return;

    const bool fine = (shift && kind_ == PointerKind::Mouse) || button_ == MouseButton::Right;
    if (fine != fineActive_) // changement de mode en cours de glissé : on ré-ancre, sans saut
    {
        fineActive_ = fine;
        anchorY_ = y;
        anchorValue_ = value_;
    }
    const double perPixel = 1.0 / cfg_.dragPixelsForFullRange * (fineActive_ ? cfg_.fineRatio : 1.0);
    emitValue (anchorValue_ + (anchorY_ - y) * perPixel);
}

void KnobInteraction::pointerUp (double timeMs)
{
    switch (state_)
    {
        case State::Dragging:
            dragging_ = false;
            emitEnd();
            break;
        case State::Pressed:
            if (button_ == MouseButton::Right)
            {
                if (cb_.openModal) cb_.openModal();
                haveLastTap_ = false;
            }
            else if (secondPress_)
            {
                haveLastTap_ = false;
                if (cb_.resetToDefault) cb_.resetToDefault();
            }
            else
            {
                haveLastTap_ = true; // premier tap : peut devenir un double-tap
                lastTapTime_ = timeMs;
                lastTapX_ = downX_;
                lastTapY_ = downY_;
            }
            break;
        case State::Consumed:
        case State::Idle:
            break;
    }
    state_ = State::Idle;
    secondPress_ = false;
}

void KnobInteraction::pointerCancel()
{
    if (dragging_)
    {
        dragging_ = false;
        emitEnd();
    }
    state_ = State::Idle;
    secondPress_ = false;
    haveLastTap_ = false;
}

void KnobInteraction::tick (double timeMs)
{
    if (state_ == State::Pressed && button_ == MouseButton::Left && timeMs - downTime_ >= cfg_.longPressMs)
    {
        state_ = State::Consumed; // le relâchement qui suit ne fait plus rien
        haveLastTap_ = false;
        secondPress_ = false;
        if (cb_.openModal) cb_.openModal();
    }
    if (wheelOpen_ && timeMs - lastWheelTime_ >= cfg_.wheelIdleMs) closeWheelGesture();
}

void KnobInteraction::wheel (double notches, bool shift, double timeMs)
{
    if (notches == 0.0 || state_ == State::Dragging) return;
    if (! wheelOpen_)
    {
        wheelOpen_ = true;
        emitBegin();
    }
    lastWheelTime_ = timeMs;
    const double step = steps_ > 0 ? 1.0 / steps_ : (shift ? cfg_.wheelFineStep : cfg_.wheelStep);
    emitValue (value_ + notches * step);
}

bool KnobInteraction::key (Key k, bool shift)
{
    if (state_ == State::Dragging) return true; // pas de clavier pendant un glissé
    closeWheelGesture();
    const double step = steps_ > 0 ? 1.0 / steps_ : (shift ? cfg_.keyFineStep : cfg_.keyStep);
    const double coarse = steps_ > 0 ? std::fmax (1.0, std::floor (steps_ / 10.0)) / steps_ : cfg_.keyCoarseStep;
    switch (k)
    {
        case Key::Up:
        case Key::Right: oneShot (value_ + step); return true;
        case Key::Down:
        case Key::Left: oneShot (value_ - step); return true;
        case Key::PageUp: oneShot (value_ + coarse); return true;
        case Key::PageDown: oneShot (value_ - coarse); return true;
        case Key::Home: oneShot (0.0); return true;
        case Key::End: oneShot (1.0); return true;
        case Key::Enter: if (cb_.openModal) cb_.openModal(); return true;
        case Key::Delete: if (cb_.resetToDefault) cb_.resetToDefault(); return true;
    }
    return false;
}
} // namespace neptune::core::ui
