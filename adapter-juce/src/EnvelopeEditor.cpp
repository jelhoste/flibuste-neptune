#include "EnvelopeEditor.h"
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr float kMinFrac = 0.15f;      // un segment à 0 ms garde une largeur visible (et saisissable)
constexpr float kPlateau = 0.10f;      // largeur relative de la zone « sustain »
constexpr float kHitRadius = 22.0f;    // zone de saisie >= 44 px de diamètre (tactile)
constexpr juce::uint32 kLongPressMs = 500;
constexpr float kMoveSlop = 6.0f;
const char* const kToggleKeys[5] = { "envelope.delay", "envelope.attack", "envelope.hold", "envelope.decay", "envelope.release" };
}

struct EnvelopeEditor::Geometry
{
    juce::Rectangle<float> area;                // zone de la courbe
    float x[6] {};                              // début de chaque segment (Delay, Attack, Hold, Decay, [plateau], Release) puis fin
    float colLeft[5] {}, colWidth = 0.0f;       // colonne de chaque segment temporel
    float plateauLeft = 0.0f, plateauRight = 0.0f;
    float yBottom = 0.0f, yTop = 0.0f;
    float levelStart = 0.0f;                    // niveau au début de la courbe
    float sustainLevel = 1.0f;
    float segX0[5] {}, segX1[5] {};             // bornes affichées de chaque segment
    float segY0[5] {}, segY1[5] {};
    float yOf (float level) const { return yBottom - level * (yBottom - yTop); }
};

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
    if (match > 0) presets_.setSelectedId (match, juce::dontSendNotification);
    else presets_.setSelectedId (0, juce::dontSendNotification);
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
    auto top = r.removeFromTop (40);
    presets_.setBounds (top.removeFromRight (300).reduced (2));
    const int w = juce::jmin (84, top.getWidth() / 5);
    for (int i = 0; i < 5; ++i) toggles_[i].setBounds (top.removeFromLeft (w).reduced (2));
}

EnvelopeEditor::Geometry EnvelopeEditor::layout() const
{
    Geometry g;
    g.area = getLocalBounds().toFloat().withTrimmedTop (48.0f).reduced (12.0f, 8.0f);
    g.yTop = g.area.getY() + 6.0f;
    g.yBottom = g.area.getBottom() - 6.0f;
    // 5 colonnes temporelles + plateau entre decay et release
    const float total = g.area.getWidth();
    g.colWidth = total * (1.0f - kPlateau) / 5.0f;
    float cursor = g.area.getX();
    const int order[5] = { 0, 1, 2, 3, 4 }; // Delay, Attack, Hold, Decay, Release
    const double ms[5] = { env_.delay.ms, env_.attack.ms, env_.hold.ms, env_.decay.ms, env_.release.ms };
    g.levelStart = (env_.delay.on || env_.attack.on) ? 0.0f : 1.0f;
    g.sustainLevel = env_.decay.on ? static_cast<float> (env_.sustain) : 1.0f;
    float level = g.levelStart;
    for (int k = 0; k < 5; ++k)
    {
        const int i = order[k];
        if (i == 4)
        {
            g.plateauLeft = cursor;
            cursor += total * kPlateau;
            g.plateauRight = cursor;
        }
        g.colLeft[i] = cursor;
        const float u = static_cast<float> (envelopeUnitFromTime (ms[i]));
        const float f = kMinFrac + (1.0f - kMinFrac) * u;
        g.segX0[i] = cursor;
        g.segX1[i] = cursor + g.colWidth * f;
        // niveaux de départ et d'arrivée (un segment désactivé est sauté : le niveau ne change pas, sauf le saut au pic)
        float y0 = level, y1 = level;
        switch (i)
        {
            case 0: y0 = y1 = level; break;                          // délai : reste au niveau initial (0 s'il est actif)
            case 1: y0 = env_.attack.on ? 0.0f : 1.0f; y1 = 1.0f; break; // attaque désactivée : saut direct au pic
            case 2: y0 = y1 = 1.0f; break;
            case 3: y0 = 1.0f; y1 = g.sustainLevel; break;
            default: y0 = g.sustainLevel; y1 = 0.0f; break;
        }
        g.segY0[i] = y0;
        g.segY1[i] = y1;
        level = y1;
        cursor += g.colWidth;
    }
    return g;
}

EnvelopeEditor::Hit EnvelopeEditor::hitTest (juce::Point<float> p) const
{
    const auto g = layout();
    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    Hit best;
    float bestD = kHitRadius;
    for (int i = 0; i < 5; ++i)
    {
        const auto s = static_cast<Seg> (i);
        // nœud de fin de segment (ne dépend pas de la forme)
        {
            const juce::Point<float> n (g.segX1[i], g.yOf (g.segY1[i]));
            const float d = n.getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = { s, Part::Node }; }
        }
        // poignée de forme, seulement sur les segments qui ont une courbe
        if (i == 1 || i == 3 || i == 4)
        {
            const float midX = 0.5f * (g.segX0[i] + g.segX1[i]);
            const double ex = envelopeCurveFraction (segs[i]->curve, i == 1 ? 0 : (i == 3 ? 1 : 2), env_.sustain, 0.5);
            const float midY = g.yOf (g.segY0[i] + (g.segY1[i] - g.segY0[i]) * static_cast<float> (ex));
            const float d = juce::Point<float> (midX, midY).getDistanceFrom (p);
            if (d < bestD && segs[i]->on) { bestD = d; best = { s, Part::Shape }; }
        }
    }
    return best;
}

void EnvelopeEditor::paint (juce::Graphics& gfx)
{
    const auto g = layout();
    gfx.setColour (colour (Token::BgCard));
    gfx.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (44.0f), 8.0f);

    // repères de colonnes
    gfx.setColour (colour (Token::Grid));
    for (int i = 0; i < 5; ++i) gfx.drawVerticalLine (static_cast<int> (g.colLeft[i]), g.yTop, g.yBottom);
    gfx.drawHorizontalLine (static_cast<int> (g.yTop), g.area.getX(), g.area.getRight());
    gfx.drawHorizontalLine (static_cast<int> (g.yBottom), g.area.getX(), g.area.getRight());

    const EnvelopeSegment* segs[5] = { &env_.delay, &env_.attack, &env_.hold, &env_.decay, &env_.release };
    juce::Path path;
    float x = g.area.getX();
    float y = g.yOf (g.levelStart);
    path.startNewSubPath (x, y);
    auto lineTo = [&] (float nx, float ny) { path.lineTo (nx, ny); };

    for (int i = 0; i < 5; ++i)
    {
        if (i == 4) lineTo (g.plateauRight, g.yOf (g.sustainLevel)); // plateau de sustain
        // jonction avec le début du segment (les colonnes sont contiguës)
        lineTo (g.segX0[i], g.yOf (g.segY0[i]));
        const int kind = i == 1 ? 0 : (i == 3 ? 1 : 2);
        const bool curved = segs[i]->on && (i == 1 || i == 3 || i == 4);
        if (curved)
        {
            constexpr int steps = 48;
            for (int s = 1; s <= steps; ++s)
            {
                const double t = static_cast<double> (s) / steps;
                const double f = envelopeCurveFraction (segs[i]->curve, kind, env_.sustain, t);
                lineTo (g.segX0[i] + (g.segX1[i] - g.segX0[i]) * static_cast<float> (t),
                        g.yOf (g.segY0[i] + (g.segY1[i] - g.segY0[i]) * static_cast<float> (f)));
            }
        }
        else
            lineTo (g.segX1[i], g.yOf (segs[i]->on ? g.segY1[i] : g.segY0[i]));
    }
    lineTo (g.area.getRight(), g.yOf (0.0f));

    juce::Path fill (path);
    fill.lineTo (g.area.getRight(), g.yBottom);
    fill.lineTo (g.area.getX(), g.yBottom);
    fill.closeSubPath();
    gfx.setColour (colour (Token::Accent).withAlpha (0.18f));
    gfx.fillPath (fill);
    gfx.setColour (colour (Token::Accent));
    gfx.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // nœuds et poignées
    for (int i = 0; i < 5; ++i)
    {
        const bool on = segs[i]->on;
        const juce::Point<float> n (g.segX1[i], g.yOf (on ? g.segY1[i] : g.segY0[i]));
        gfx.setColour (on ? colour (Token::Handle) : colour (Token::TextDim2));
        gfx.fillEllipse (n.x - 5.0f, n.y - 5.0f, 10.0f, 10.0f);
        if (on && (i == 1 || i == 3 || i == 4))
        {
            const int kind = i == 1 ? 0 : (i == 3 ? 1 : 2);
            const double f = envelopeCurveFraction (segs[i]->curve, kind, env_.sustain, 0.5);
            const float mx = 0.5f * (g.segX0[i] + g.segX1[i]);
            const float my = g.yOf (g.segY0[i] + (g.segY1[i] - g.segY0[i]) * static_cast<float> (f));
            gfx.setColour (colour (Token::Pink));
            if (segs[i]->curve.db) gfx.drawEllipse (mx - 5.0f, my - 5.0f, 10.0f, 10.0f, 2.0f); // creux : courbe en dB
            else gfx.fillEllipse (mx - 5.0f, my - 5.0f, 10.0f, 10.0f);
        }
    }

    // temps, sous chaque colonne
    gfx.setColour (colour (Token::TextDim));
    gfx.setFont (juce::FontOptions (12.0f));
    for (int i = 0; i < 5; ++i)
    {
        const juce::String t = segs[i]->on ? juce::String (segs[i]->ms, segs[i]->ms < 100.0 ? 1 : 0) + " ms" : "-";
        gfx.drawText (t, juce::Rectangle<float> (g.colLeft[i], g.yBottom - 18.0f, g.colWidth, 16.0f), juce::Justification::centred);
    }
    gfx.setColour (colour (Token::Accent));
    gfx.drawText (juce::String (env_.sustain * 100.0, 0) + " %", juce::Rectangle<float> (g.plateauLeft - 6.0f, g.yTop + 2.0f, g.plateauRight - g.plateauLeft + 12.0f, 16.0f),
                  juce::Justification::centred);
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    drag_ = hitTest (e.position);
    downPos_ = e.position;
    downTime_ = juce::Time::getMillisecondCounter();
    longPressFired_ = false;
    moved_ = false;
    if (drag_.part == Part::None) return;
    downMs_ = segment (env_, drag_.seg).ms;
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
    const auto g = layout();
    const int i = static_cast<int> (drag_.seg);
    auto& seg = segment (env_, drag_.seg);

    if (drag_.part == Part::Node)
    {
        // temps : position du nœud dans sa colonne
        const float f = juce::jlimit (kMinFrac, 1.0f, (e.position.x - g.colLeft[i]) / g.colWidth);
        const double u = (f - kMinFrac) / (1.0 - kMinFrac);
        seg.ms = envelopeTimeFromUnit (u);
        if (! seg.on && seg.ms > 0.0) seg.on = true; // saisir un segment le réactive
        if (drag_.seg == Seg::Decay)
        {
            const float lvl = juce::jlimit (0.0f, 1.0f, (g.yBottom - e.position.y) / (g.yBottom - g.yTop));
            env_.sustain = lvl;
        }
    }
    else // forme
    {
        const double y0 = g.segY0[i], y1 = g.segY1[i];
        const double level = (g.yBottom - e.position.y) / (g.yBottom - g.yTop); // niveau sous la souris
        const double frac = std::abs (y1 - y0) < 1e-6 ? 0.5 : (level - y0) / (y1 - y0);
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
    drag_ = {};
    stopTimer();
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
