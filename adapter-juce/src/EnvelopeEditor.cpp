#include "EnvelopeEditor.h"
#include <algorithm>
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr float kHitRadius = 22.0f;        // zone de saisie >= 44 px de diamètre (tactile)
constexpr int kToggleRowHeight = 40;
constexpr int kValueRowHeight = 32;
constexpr juce::uint32 kLongPressMs = 500;
constexpr float kMoveSlop = 6.0f;
const char* const kToggleKeys[5] = { "envelope.delay", "envelope.attack", "envelope.hold", "envelope.decay", "envelope.release" };
}

EnvelopeEditor::EnvelopeEditor (const theme::Palette& palette, const i18n::Translator& tr) : palette_ (palette), tr_ (tr)
{
    for (int i = 0; i < 5; ++i)
    {
        toggles_[i].setButtonText (juce::String::fromUTF8 (std::string (tr_.tr (kToggleKeys[i])).c_str()));
        toggles_[i].setClickingTogglesState (true);
        toggles_[i].onClick = [this, i]
        {
            if (updating_) return;
            segment (env_, static_cast<Seg> (i)).on = toggles_[i].getToggleState();
            changed();
        };
        addAndMakeVisible (toggles_[i]);
    }
    presets_.setTextWhenNothingSelected (juce::String::fromUTF8 (std::string (tr_.tr ("envelope.preset")).c_str()));
    for (int i = 0; i < numEnvelopePresets; ++i)
        presets_.addItem (juce::String::fromUTF8 (std::string (tr_.tr (presetI18nKey (static_cast<EnvelopePreset> (i)))).c_str()), i + 1);
    presets_.onChange = [this]
    {
        if (updating_) return;
        const int id = presets_.getSelectedId();
        if (id < 1) return;
        env_ = presetEnvelope (static_cast<EnvelopePreset> (id - 1));
        syncControls();
        changed();
    };
    addAndMakeVisible (presets_);
}

juce::Colour EnvelopeEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

EnvelopeSegment& EnvelopeEditor::segment (EnvelopeDescription& e, Seg s) const
{
    switch (s)
    {
        case Seg::Delay: return e.delay;
        case Seg::Attack: return e.attack;
        case Seg::Hold: return e.hold;
        case Seg::Decay: return e.decay;
        default: return e.release;
    }
}

void EnvelopeEditor::setEnvelope (const EnvelopeDescription& e)
{
    if (drag_.part != Part::None) return; // pas pendant un glissé de l'utilisateur
    if (e == env_) return;
    env_ = e;
    syncControls();
    repaint();
}

void EnvelopeEditor::syncControls()
{
    updating_ = true;
    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    for (int i = 0; i < 5; ++i) toggles_[i].setToggleState (segs[i]->on, juce::dontSendNotification);
    int match = 0;
    for (int i = 0; i < numEnvelopePresets; ++i)
        if (presetEnvelope (static_cast<EnvelopePreset> (i)) == env_) match = i + 1;
    presets_.setSelectedId (match, juce::dontSendNotification);
    updating_ = false;
}

void EnvelopeEditor::changed()
{
    env_ = sanitised (env_);
    syncControls();
    repaint();
    if (onChange) onChange (env_);
}

void EnvelopeEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kToggleRowHeight);
    presets_.setBounds (top.removeFromRight (300).reduced (2));
    const int w = juce::jmin (84, top.getWidth() / 5);
    for (int i = 0; i < 5; ++i) toggles_[i].setBounds (top.removeFromLeft (w).reduced (2));
}

EnvelopeEditor::Frame EnvelopeEditor::frame() const
{
    Frame f;
    const auto area = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (kToggleRowHeight + kValueRowHeight + 8)).reduced (12.0f, 8.0f);
    f.graph = area;
    f.yTop = f.graph.getY() + 8.0f;
    f.yBottom = f.graph.getBottom() - 6.0f;
    f.layout = computeEnvelopeLayout (env_, static_cast<double> (f.graph.getWidth()) - 4.0, frozenScale_);

    // Les valeurs de durée sont toujours sous les boutons d'activation : alignées, jamais superposées.
    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    for (int i = 0; i < 5; ++i)
    {
        juce::String value;
        if (! segs[i]->on) value = "off";
        else if (segs[i]->ms < 10.0) value = juce::String (segs[i]->ms, 1) + " ms";
        else if (segs[i]->ms < 1000.0) value = juce::String (static_cast<int> (std::lround (segs[i]->ms))) + " ms";
        else value = juce::String (segs[i]->ms / 1000.0, 2) + " s";
        const auto b = toggles_[i].getBounds().toFloat();
        f.chips.push_back ({ static_cast<Seg> (i), juce::Rectangle<float> (b.getX(), static_cast<float> (kToggleRowHeight), b.getWidth(),
                                                                          static_cast<float> (kValueRowHeight - 2)), value });
    }
    return f;
}

EnvelopeEditor::Hit EnvelopeEditor::hitTest (juce::Point<float> p) const
{
    const auto f = frame();
    for (const auto& c : f.chips)
        if (c.rect.expanded (0.0f, 4.0f).contains (p)) return { c.seg, Part::Chip };

    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    const float sustainLevel = env_.decay.on ? static_cast<float> (env_.sustain) : 1.0f;
    auto Y = [&] (float level) { return f.yBottom - level * (f.yBottom - f.yTop); };
    Hit best;
    float bestD = kHitRadius;

    // poignée du plateau de sustain
    {
        const juce::Point<float> h (f.graph.getX() + static_cast<float> (0.5 * (f.layout.plateauX0 + f.layout.plateauX1)), Y (sustainLevel));
        if (h.getDistanceFrom (p) < bestD) { bestD = h.getDistanceFrom (p); best = { Seg::Decay, Part::Sustain }; }
    }
    // délai à 0 ms : son nœud est collé à la marge gauche
    if (env_.delay.on && ! f.layout.active[0])
    {
        const juce::Point<float> h (f.graph.getX() - 2.0f, f.yBottom);
        if (h.getDistanceFrom (p) < bestD) { bestD = h.getDistanceFrom (p); best = { Seg::Delay, Part::Node }; }
    }
    for (int i = 0; i < 5; ++i)
    {
        if (! f.layout.active[i]) continue; // un segment sans largeur se règle par sa valeur, sous son bouton
        const auto s = static_cast<Seg> (i);
        const float y0 = i == 1 ? 0.0f : (i == 4 ? sustainLevel : (i == 3 ? 1.0f : (i == 2 ? 1.0f : 0.0f)));
        const float y1 = i == 1 ? 1.0f : (i == 2 ? 1.0f : (i == 3 ? sustainLevel : 0.0f));
        const juce::Point<float> node (f.graph.getX() + static_cast<float> (f.layout.x1[i]), Y (y1));
        if (node.getDistanceFrom (p) < bestD) { bestD = node.getDistanceFrom (p); best = { s, Part::Node }; }
        if ((i == 1 || i == 3 || i == 4) && f.layout.x1[i] - f.layout.x0[i] >= 24.0)
        {
            const double g = envelopeCurveFraction (segs[i]->curve, i == 1 ? 0 : (i == 3 ? 1 : 2), env_.sustain, 0.5);
            const juce::Point<float> mid (f.graph.getX() + static_cast<float> (0.5 * (f.layout.x0[i] + f.layout.x1[i])),
                                          Y (y0 + (y1 - y0) * static_cast<float> (g)));
            if (mid.getDistanceFrom (p) < bestD) { bestD = mid.getDistanceFrom (p); best = { s, Part::Shape }; }
        }
    }
    return best;
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    const auto f = frame();
    const auto& L = f.layout;
    g.setColour (colour (Token::BgCard));
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (kToggleRowHeight + kValueRowHeight + 4)), 8.0f);
    g.setColour (colour (Token::Grid));
    g.drawHorizontalLine (static_cast<int> (f.yTop), f.graph.getX(), f.graph.getRight());
    g.drawHorizontalLine (static_cast<int> (f.yBottom), f.graph.getX(), f.graph.getRight());

    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    const float sustainLevel = env_.decay.on ? static_cast<float> (env_.sustain) : 1.0f;
    const float x0 = f.graph.getX();
    auto X = [&] (double x) { return x0 + static_cast<float> (x); };
    auto Y = [&] (float level) { return f.yBottom - level * (f.yBottom - f.yTop); };

    juce::Path path;
    const float startLevel = (L.active[0] || L.active[1]) ? 0.0f : 1.0f;
    path.startNewSubPath (X (0.0), Y (startLevel));
    float level = startLevel;
    auto vertical = [&] (double x, float to) { if (std::abs (to - level) > 1e-6f) { path.lineTo (X (x), Y (to)); level = to; } };
    auto curve = [&] (int i, int kind, float from, float to)
    {
        constexpr int steps = 48;
        for (int s = 1; s <= steps; ++s)
        {
            const double t = static_cast<double> (s) / steps;
            const double fr = envelopeCurveFraction (segs[i]->curve, kind, env_.sustain, t);
            path.lineTo (X (L.x0[i] + (L.x1[i] - L.x0[i]) * t), Y (from + (to - from) * static_cast<float> (fr)));
        }
        level = to;
    };

    if (L.active[0]) { path.lineTo (X (L.x1[0]), Y (0.0f)); level = 0.0f; }
    // attaque
    if (L.active[1]) { vertical (L.x0[1], 0.0f); curve (1, 0, 0.0f, 1.0f); }
    else vertical (L.x0[1], 1.0f);
    // hold
    if (L.active[2]) path.lineTo (X (L.x1[2]), Y (1.0f));
    // decay
    if (L.active[3]) curve (3, 1, 1.0f, sustainLevel);
    else vertical (L.x0[3], sustainLevel);
    // sustain
    path.lineTo (X (L.plateauX1), Y (level));
    // relâchement
    if (L.active[4]) curve (4, 2, level, 0.0f);
    else vertical (L.x0[4], 0.0f);
    path.lineTo (f.graph.getRight(), Y (0.0f));

    juce::Path fill (path);
    fill.lineTo (f.graph.getRight(), f.yBottom);
    fill.lineTo (x0, f.yBottom);
    fill.closeSubPath();
    g.saveState();
    g.reduceClipRegion (f.graph.toNearestInt());
    g.setColour (colour (Token::Accent).withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (colour (Token::Accent));
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // nœuds et poignées de forme
    const float y0s[5] = { 0.0f, 0.0f, 1.0f, 1.0f, sustainLevel };
    const float y1s[5] = { 0.0f, 1.0f, 1.0f, sustainLevel, 0.0f };
    for (int i = 0; i < 5; ++i)
    {
        if (! L.active[i]) continue;
        g.setColour (colour (Token::Handle));
        g.fillEllipse (X (L.x1[i]) - 5.0f, Y (y1s[i]) - 5.0f, 10.0f, 10.0f);
        if ((i == 1 || i == 3 || i == 4) && L.x1[i] - L.x0[i] >= 24.0)
        {
            const double fr = envelopeCurveFraction (segs[i]->curve, i == 1 ? 0 : (i == 3 ? 1 : 2), env_.sustain, 0.5);
            const float mx = X (0.5 * (L.x0[i] + L.x1[i]));
            const float my = Y (y0s[i] + (y1s[i] - y0s[i]) * static_cast<float> (fr));
            g.setColour (colour (Token::Pink));
            if (segs[i]->curve.db) g.drawEllipse (mx - 5.0f, my - 5.0f, 10.0f, 10.0f, 2.0f); // creux : courbe en dB
            else g.fillEllipse (mx - 5.0f, my - 5.0f, 10.0f, 10.0f);
        }
    }
    // nœud du délai à 0 ms, collé à la marge gauche
    if (env_.delay.on && ! L.active[0])
    {
        g.setColour (colour (Token::Handle));
        g.fillEllipse (x0 - 2.0f - 5.0f, f.yBottom - 5.0f, 10.0f, 10.0f);
    }
    // poignée du plateau (niveau de sustain)
    {
        const float hx = X (0.5 * (L.plateauX0 + L.plateauX1)), hy = Y (sustainLevel);
        g.setColour (colour (Token::Pink));
        g.fillRoundedRectangle (hx - 7.0f, hy - 4.0f, 14.0f, 8.0f, 2.0f);
    }
    // segment illuminé : bande sur sa largeur, ou repère vertical à la jonction s'il n'a pas de largeur
    if (highlight_ != Seg::None)
    {
        const int i = static_cast<int> (highlight_);
        g.setColour (colour (Token::Accent).withAlpha (0.18f));
        if (L.active[i]) g.fillRect (juce::Rectangle<float> (X (L.x0[i]), f.yTop, static_cast<float> (L.x1[i] - L.x0[i]), f.yBottom - f.yTop));
        else
        {
            g.setColour (colour (Token::Accent).withAlpha (0.7f));
            g.drawVerticalLine (static_cast<int> (X (L.x1[i])), f.yTop, f.yBottom);
        }
    }
    // niveau de sustain, au-dessus du plateau
    g.setColour (colour (Token::Accent));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (env_.sustain * 100.0, 0) + " %",
                juce::Rectangle<float> (X (L.plateauX0), Y (sustainLevel) - 20.0f, static_cast<float> (L.plateauX1 - L.plateauX0), 16.0f),
                juce::Justification::centred);
    g.restoreState();

    // valeurs de durée, sous les boutons d'activation
    for (const auto& c : f.chips)
    {
        const bool on = segs[static_cast<int> (c.seg)]->on;
        const bool lit = c.seg == highlight_;
        g.setColour (lit ? colour (Token::Accent).withAlpha (0.25f) : colour (Token::BgApp).withAlpha (0.6f));
        g.fillRoundedRectangle (c.rect.reduced (2.0f, 0.0f), 5.0f);
        g.setColour (lit ? colour (Token::Accent) : (on ? colour (Token::Border) : colour (Token::Grid)));
        g.drawRoundedRectangle (c.rect.reduced (2.5f, 0.5f), 5.0f, 1.0f);
        g.setColour (on ? colour (Token::Text) : colour (Token::TextDim2));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (c.text, c.rect.toNearestInt(), juce::Justification::centred);
    }
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    const Seg h = hit.part == Part::Chip ? hit.seg : Seg::None;
    if (h != highlight_) { highlight_ = h; repaint(); }
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (drag_.part == Part::None && highlight_ != Seg::None) { highlight_ = Seg::None; repaint(); }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    drag_ = hitTest (e.position);
    downPos_ = e.position;
    downTime_ = juce::Time::getMillisecondCounter();
    longPressFired_ = false;
    moved_ = false;
    frozenScale_ = 0.0;
    if (drag_.part == Part::None) return;
    downMs_ = segment (env_, drag_.seg).ms;
    highlight_ = drag_.part == Part::Chip ? drag_.seg : Seg::None;
    if (drag_.part != Part::Chip) frozenScale_ = frame().layout.scale; // échelle figée pendant le glissé
    if (e.mods.isPopupMenu() && drag_.part == Part::Shape) // clic droit : bascule dB
    {
        auto& c = segment (env_, drag_.seg).curve;
        c.db = ! c.db;
        longPressFired_ = true;
        changed();
        return;
    }
    startTimerHz (20); // appui long
}

void EnvelopeEditor::timerCallback()
{
    if (drag_.part == Part::Shape && ! moved_ && ! longPressFired_ && juce::Time::getMillisecondCounter() - downTime_ >= kLongPressMs)
    {
        longPressFired_ = true;
        auto& c = segment (env_, drag_.seg).curve;
        c.db = ! c.db;
        changed();
    }
    if (drag_.part == Part::None) stopTimer();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag_.part == Part::None || longPressFired_) return;
    if (e.position.getDistanceFrom (downPos_) < kMoveSlop && ! moved_) return;
    moved_ = true;
    auto& seg = segment (env_, drag_.seg);
    const int i = static_cast<int> (drag_.seg);

    if (drag_.part == Part::Chip)
    {
        seg.ms = envelopeScrubTime (downMs_, static_cast<double> (e.position.x - downPos_.x));
        if (! seg.on && seg.ms > 0.0) seg.on = true; // saisir un segment le réactive
        changed();
        return;
    }

    const auto f = frame();
    if (drag_.part == Part::Sustain)
    {
        env_.sustain = juce::jlimit (0.0f, 1.0f, (f.yBottom - e.position.y) / (f.yBottom - f.yTop));
        if (env_.sustain < 0.999) env_.decay.on = true; // descendre le plateau demande un decay (0 ms = saut direct)
        changed();
        return;
    }
    if (drag_.part == Part::Node)
    {
        // le nœud d'un délai à 0 ms est collé à la marge : sa largeur se mesure depuis le bord gauche de la courbe
        const double width = static_cast<double> (e.position.x - f.graph.getX()) - (f.layout.active[i] ? f.layout.x0[i] : 0.0);
        seg.ms = envelopeTimeFromWidth (width, frozenScale_);
        if (drag_.seg == Seg::Decay)
            env_.sustain = juce::jlimit (0.0f, 1.0f, (f.yBottom - e.position.y) / (f.yBottom - f.yTop));
    }
    else // forme
    {
        const double sustainLevel = env_.decay.on ? env_.sustain : 1.0;
        const double a = i == 1 ? 0.0 : (i == 3 ? 1.0 : sustainLevel);
        const double b = i == 1 ? 1.0 : (i == 3 ? sustainLevel : 0.0);
        const double lvl = (f.yBottom - e.position.y) / (f.yBottom - f.yTop);
        const double frac = std::abs (b - a) < 1e-6 ? 0.5 : (lvl - a) / (b - a);
        const double gc = juce::jlimit (0.03, 0.97, frac);
        // g(0.5) = 0.5^p  ->  p = ln g / ln 0.5 ;  p = 2^(-3 shape)  ->  shape = -log2(p) / 3
        const double p = std::log (gc) / std::log (0.5);
        seg.curve.shape = juce::jlimit (-1.0, 1.0, -std::log2 (p) / 3.0);
        seg.curve.db = false;
    }
    changed();
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    const Hit hit = drag_;
    const bool tapped = ! moved_ && ! longPressFired_;
    drag_ = {};
    frozenScale_ = 0.0; // au relâchement : tout est recalculé
    stopTimer();
    highlight_ = Seg::None;
    if (hit.part == Part::Chip && tapped) askValue (hit.seg); // toucher une valeur : saisie en chiffres
    repaint();
}

void EnvelopeEditor::askValue (Seg s)
{
    auto* editor = new juce::TextEditor();
    editor->setSize (110, 32);
    editor->setInputRestrictions (8, "0123456789.");
    editor->setText (juce::String (segment (env_, s).ms, 1), false);
    editor->selectAll();
    juce::Component::SafePointer<EnvelopeEditor> safe (this);
    editor->onReturnKey = [safe, s, editor]
    {
        if (safe == nullptr) return;
        auto& seg = safe->segment (safe->env_, s);
        seg.ms = editor->getText().getDoubleValue();
        seg.on = seg.ms > 0.0 || seg.on;
        safe->changed();
        if (auto* box = editor->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    };
    const auto f = frame();
    for (const auto& c : f.chips)
        if (c.seg == s)
        {
            juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (editor),
                                                    localAreaToGlobal (c.rect.toNearestInt()), nullptr);
            editor->grabKeyboardFocus();
            return;
        }
    delete editor;
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    if (hit.part == Part::Shape)
    {
        auto& c = segment (env_, hit.seg).curve;
        c.shape = 0.0;
        c.db = false;
        changed();
    }
}
