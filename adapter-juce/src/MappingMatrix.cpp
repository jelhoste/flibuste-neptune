#include "MappingMatrix.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include "neptune/core/ZoneImport.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kToolbarH = 40;
constexpr int kKeyboardH = 46;
constexpr int kLeftMargin = 36;
constexpr int kScrollH = 14;
constexpr float kEdgeSlop = 12.0f;   // zone de saisie d'un bord : >= 24 px de large au total
constexpr float kMoveSlop = 4.0f;
constexpr juce::uint32 kCursorMs = 900; // durée d'affichage du curseur d'une note jouée

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
bool isBlackKey (int k) { const int p = k % 12; return p == 1 || p == 3 || p == 6 || p == 8 || p == 10; }
}

MappingMatrix::MappingMatrix (const theme::Palette& palette, const i18n::Translator& tr) : palette_ (palette), tr_ (tr)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (viewBox_);
    viewBox_.onChange = [this]
    {
        const int i = viewBox_.getSelectedId() - 1;
        if (i >= 0 && i < static_cast<int> (viewChoices_.size())) view_ = viewChoices_[static_cast<std::size_t> (i)];
        rebuildPlaces();
        repaint();
    };
    zoomIn_.setButtonText ("+");
    zoomOut_.setButtonText ("-");
    zoomFit_.setButtonText (text ("map.fit"));
    duplicate_.setButtonText (text ("map.duplicate"));
    velocity_.setButtonText (text ("map.velocity"));
    velocity_.onClick = [this] { if (onOpenVelocity) onOpenVelocity (velocity_); };
    polyphony_.setButtonText (text ("map.polyphony"));
    polyphony_.onClick = [this] { if (onOpenPolyphony) onOpenPolyphony (polyphony_); };
    zoomIn_.onClick = [this] { zoom (0.6, keyStart_ + keySpan_ / 2.0); };
    zoomOut_.onClick = [this] { zoom (1.0 / 0.6, keyStart_ + keySpan_ / 2.0); };
    zoomFit_.onClick = [this] { keyStart_ = 0.0; keySpan_ = 128.0; clampView(); updateScrollBar(); repaint(); };
    duplicate_.onClick = [this] { duplicateSelection(); };
    for (auto* b : { &zoomIn_, &zoomOut_, &zoomFit_, &duplicate_, &velocity_, &polyphony_ }) addAndMakeVisible (*b);
    hint_.setText (text ("map.hint"), juce::dontSendNotification);
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (hint_);
    scroll_.addListener (this);
    scroll_.setAutoHide (false);
    addAndMakeVisible (scroll_);
    rebuildViewChoices();
}

juce::Colour MappingMatrix::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String MappingMatrix::text (const char* key) const { return u8 (std::string (tr_.tr (key))); }

// ---------------------------------------------------------------- données

void MappingMatrix::setDescription (const InstrumentDescription& d)
{
    if (op_ != Op::None) return; // pas pendant un glissé : la description locale est la référence
    if (d == description_) return;
    description_ = d;
    rebuildViewChoices();
    rebuildPlaces();
    repaint();
}

void MappingMatrix::setSelection (const std::vector<int>& zones)
{
    if (op_ != Op::None || zones == selected_) return;
    selected_ = zones;
    repaint();
}

void MappingMatrix::rebuildViewChoices()
{
    const auto all = mapping::views (description_);
    std::string signature;
    for (const auto& v : all) signature += std::to_string (static_cast<int> (v.category)) + ":" + v.group + "|";
    if (signature == viewSignature_) return;
    viewSignature_ = signature;

    viewChoices_.clear();
    viewBox_.clear (juce::dontSendNotification);
    auto categoryLabel = [this] (mapping::View::Category c, bool grouped)
    {
        switch (c)
        {
            case mapping::View::Category::First: return text (grouped ? "trig.first" : "map.view_first");
            case mapping::View::Category::Legato: return text (grouped ? "trig.legato" : "map.view_legato");
            case mapping::View::Category::Release: return text (grouped ? "trig.release" : "map.view_release");
            default: return text (grouped ? "trig.attack" : "map.view_main");
        }
    };
    for (const auto& v : all)
    {
        viewChoices_.push_back (v);
        const juce::String label = v.group.empty()
            ? categoryLabel (v.category, false)
            : u8 (tr_.format ("map.view_group", { v.group })) + "  -  " + categoryLabel (v.category, true);
        viewBox_.addItem (label, static_cast<int> (viewChoices_.size()));
    }
    int select = 1;
    for (std::size_t i = 0; i < viewChoices_.size(); ++i)
        if (viewChoices_[i] == view_) select = static_cast<int> (i) + 1;
    viewBox_.setSelectedId (select, juce::dontSendNotification);
    view_ = viewChoices_[static_cast<std::size_t> (select - 1)];
}

void MappingMatrix::rebuildPlaces()
{
    places_ = mapping::places (description_, view_);
    // fondus entre couches (mêmes règles que le moteur) pour les dessiner
    std::map<std::string, int> groupIds;
    std::vector<LayerKey> keys;
    keys.reserve (description_.zones.size());
    for (const auto& z : description_.zones)
    {
        int g = 0;
        if (! z.group.empty()) g = groupIds.emplace (z.group, static_cast<int> (groupIds.size()) + 1).first->second;
        keys.push_back ({ z.keyLow, z.keyHigh, z.velLow, z.velHigh, g, z.onRelease, static_cast<int> (z.trigger) });
    }
    fades_ = computeLayerFades (keys, description_.velocityFadeWidth);
}

bool MappingMatrix::placeSelected (const mapping::Place& p) const
{
    for (const int z : p.zones)
        if (std::binary_search (selected_.begin(), selected_.end(), z)) return true;
    return false;
}

std::vector<int> MappingMatrix::zonesOfSelectedPlaces() const
{
    std::vector<int> out;
    for (const auto& p : places_)
        if (placeSelected (p)) out.insert (out.end(), p.zones.begin(), p.zones.end());
    std::sort (out.begin(), out.end());
    return out;
}

void MappingMatrix::selectPlaces (const std::vector<int>& placeIndices, bool add)
{
    std::set<int> s;
    if (add) s.insert (selected_.begin(), selected_.end());
    for (const int i : placeIndices)
        if (i >= 0 && i < static_cast<int> (places_.size()))
            s.insert (places_[static_cast<std::size_t> (i)].zones.begin(), places_[static_cast<std::size_t> (i)].zones.end());
    selected_.assign (s.begin(), s.end());
    if (onSelect) onSelect (selected_);
    repaint();
}

// ---------------------------------------------------------------- géométrie

juce::Rectangle<float> MappingMatrix::plotArea() const
{
    return juce::Rectangle<float> (static_cast<float> (kLeftMargin), static_cast<float> (kToolbarH),
                                   static_cast<float> (getWidth() - kLeftMargin - 4),
                                   static_cast<float> (getHeight() - kToolbarH - kKeyboardH - kScrollH - 2));
}

float MappingMatrix::keyX (double key) const
{
    const auto a = plotArea();
    return a.getX() + static_cast<float> ((key - keyStart_) / keySpan_) * a.getWidth();
}

float MappingMatrix::velY (double level) const
{
    const auto a = plotArea();
    return a.getBottom() - static_cast<float> (level / 127.0) * a.getHeight();
}

juce::Rectangle<float> MappingMatrix::placeRect (const mapping::Place& p) const
{
    const float x0 = keyX (p.keyLow), x1 = keyX (p.keyHigh + 1);
    const float y0 = velY (p.velHigh), y1 = velY (p.velLow - 1);
    return { x0, y0, x1 - x0, y1 - y0 };
}

void MappingMatrix::clampView()
{
    keySpan_ = juce::jlimit (8.0, 128.0, keySpan_);
    keyStart_ = juce::jlimit (0.0, 128.0 - keySpan_, keyStart_);
}

void MappingMatrix::zoom (double factor, double aroundKey)
{
    const double rel = keySpan_ > 0.0 ? (aroundKey - keyStart_) / keySpan_ : 0.5;
    keySpan_ *= factor;
    keySpan_ = juce::jlimit (8.0, 128.0, keySpan_);
    keyStart_ = aroundKey - rel * keySpan_;
    clampView();
    updateScrollBar();
    repaint();
}

void MappingMatrix::updateScrollBar()
{
    scroll_.setRangeLimits (0.0, 128.0);
    scroll_.setCurrentRange (keyStart_, keySpan_, juce::dontSendNotification);
}

void MappingMatrix::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    keyStart_ = newStart;
    clampView();
    repaint();
}

void MappingMatrix::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (kToolbarH).reduced (2);
    viewBox_.setBounds (bar.removeFromLeft (170));
    zoomOut_.setBounds (bar.removeFromLeft (36));
    zoomIn_.setBounds (bar.removeFromLeft (36));
    zoomFit_.setBounds (bar.removeFromLeft (72));
    duplicate_.setBounds (bar.removeFromLeft (92));
    velocity_.setBounds (bar.removeFromLeft (96));
    polyphony_.setBounds (bar.removeFromLeft (104));
    hint_.setBounds (bar);
    scroll_.setBounds (kLeftMargin, getHeight() - kScrollH, getWidth() - kLeftMargin - 4, kScrollH);
    updateScrollBar();
}

// ---------------------------------------------------------------- dessin

void MappingMatrix::paint (juce::Graphics& g)
{
    const auto plot = plotArea();
    g.setColour (colour (Token::BgCard));
    g.fillRect (plot);

    // Grille : octaves (verticales) et paliers de vélocité (horizontales)
    g.setFont (juce::FontOptions (11.0f));
    for (int k = static_cast<int> (std::floor (keyStart_)); k <= static_cast<int> (std::ceil (keyStart_ + keySpan_)); ++k)
    {
        if (k < 0 || k > 127 || k % 12 != 0) continue;
        const float x = keyX (k);
        g.setColour (colour (Token::Grid));
        g.drawVerticalLine (static_cast<int> (x), plot.getY(), plot.getBottom());
    }
    for (const int v : { 1, 32, 64, 96, 127 })
    {
        const float y = velY (v - 0.5);
        g.setColour (colour (Token::Grid));
        g.drawHorizontalLine (static_cast<int> (y), plot.getX(), plot.getRight());
        g.setColour (colour (Token::TextDim));
        g.drawText (juce::String (v), 0, static_cast<int> (y) - 7, kLeftMargin - 4, 14, juce::Justification::centredRight);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // Places non sélectionnées d'abord, sélectionnées par-dessus
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < places_.size(); ++i)
        {
            const auto& p = places_[i];
            const bool sel = placeSelected (p);
            if ((pass == 1) != sel) continue;
            const auto r = placeRect (p);
            if (r.getRight() < plot.getX() || r.getX() > plot.getRight()) continue;
            const auto inner = r.reduced (0.5f);
            g.setColour (colour (sel ? Token::Accent : Token::Nav1).withAlpha (sel ? 0.55f : 0.45f));
            g.fillRect (inner);
            // fondu automatique aux frontières : bandes plus claires au-delà de la plage (là où la couche joue encore, en fondu)
            if (! p.zones.empty() && static_cast<std::size_t> (p.zones.front()) < fades_.size())
            {
                const auto& fd = fades_[static_cast<std::size_t> (p.zones.front())];
                g.setColour (colour (Token::Accent2).withAlpha (0.35f));
                const float uHi = fd.activeHi - 0.5f, uLo = fd.activeLo - 0.5f;
                if (uHi > static_cast<float> (p.velHigh) + 0.01f)
                    g.fillRect (juce::Rectangle<float> (inner.getX(), velY (uHi), inner.getWidth(), velY (p.velHigh) - velY (uHi)));
                if (uLo < static_cast<float> (p.velLow - 1) - 0.01f)
                    g.fillRect (juce::Rectangle<float> (inner.getX(), velY (p.velLow - 1), inner.getWidth(), velY (uLo) - velY (p.velLow - 1)));
            }
            if (p.overlaps) // chevauchement signalé : hachures
            {
                g.saveState();
                g.reduceClipRegion (inner.toNearestInt());
                g.setColour (colour (Token::KnobHot).withAlpha (0.7f));
                for (float x = inner.getX() - inner.getHeight(); x < inner.getRight(); x += 8.0f)
                    g.drawLine (x, inner.getBottom(), x + inner.getHeight(), inner.getY(), 1.0f);
                g.restoreState();
            }
            g.setColour (sel ? colour (Token::Accent) : colour (Token::Border));
            g.drawRect (inner, sel ? 2.0f : 1.0f);
            if (static_cast<int> (i) == hoverPlace_) { g.setColour (colour (Token::Text).withAlpha (0.8f)); g.drawRect (inner.reduced (1.5f), 1.0f); }

            // repères : choke (C n, ou C a>b si avancé) et condition de déclenchement (P : Premier, L : Legato)
            if (! p.zones.empty() && inner.getWidth() > 22.0f && inner.getHeight() > 12.0f)
            {
                const auto& z0 = description_.zones[static_cast<std::size_t> (p.zones.front())];
                juce::String tag;
                if (z0.trigger == TriggerCondition::First) tag += "P ";
                if (z0.trigger == TriggerCondition::Legato) tag += "L ";
                if (z0.chokeCuts != 0 || z0.chokeCutBy != 0)
                    tag += z0.chokeCuts == z0.chokeCutBy ? "C" + juce::String (z0.chokeCuts)
                                                          : "C" + juce::String (z0.chokeCuts) + ">" + juce::String (z0.chokeCutBy);
                if (tag.isNotEmpty())
                {
                    g.setColour (colour (Token::Accent));
                    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                    g.drawText (tag.trim(), inner.reduced (3.0f, 1.0f), juce::Justification::bottomRight, false);
                }
            }
            if (inner.getWidth() > 30.0f && inner.getHeight() > 16.0f)
            {
                g.setColour (colour (Token::Text));
                g.setFont (juce::FontOptions (11.0f));
                const auto& z = description_.zones[static_cast<std::size_t> (p.zones.front())];
                juce::String label;
                if (inner.getWidth() > 90.0f) label = u8 (std::filesystem::u8path (z.samplePath).stem().string());
                if (p.zones.size() > 1) label = (label.isEmpty() ? juce::String() : label + "  ") + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (static_cast<int> (p.zones.size()));
                g.drawText (label, inner.reduced (3.0f, 0.0f), juce::Justification::centred, true); // centré dans le rectangle, quelle que soit sa hauteur
            }
        }

    // Curseur des notes jouées : barre à (touche, vélocité) et contour des rectangles qui contiennent ce point
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (int k = 0; k < 128; ++k)
        {
            const auto& pl = played_[static_cast<std::size_t> (k)];
            if (pl.velocity <= 0) continue;
            const auto age = now - pl.time;
            if (age > kCursorMs) continue;
            const float a = 1.0f - static_cast<float> (age) / static_cast<float> (kCursorMs);
            const float y = velY (pl.velocity - 0.5);
            for (const auto& p : places_)
            {
                if (k < p.keyLow || k > p.keyHigh || pl.velocity < p.velLow || pl.velocity > p.velHigh) continue;
                g.setColour (colour (Token::KnobHot).withAlpha (0.35f + 0.65f * a));
                g.drawRect (placeRect (p).reduced (1.0f), 3.0f);
            }
            const float cx = (keyX (k) + keyX (k + 1)) * 0.5f;
            const float w = juce::jmax (18.0f, keyX (k + 1) - keyX (k));
            g.setColour (colour (Token::Text).withAlpha (0.25f + 0.75f * a));
            g.fillRoundedRectangle (cx - w * 0.5f, y - 2.0f, w, 4.0f, 2.0f);
            juce::Path tri;
            tri.addTriangle (cx - 5.0f, y - 11.0f, cx + 5.0f, y - 11.0f, cx, y - 3.0f);
            g.fillPath (tri);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (pl.velocity), static_cast<int> (cx) - 20, static_cast<int> (y) - 27, 40, 14, juce::Justification::centred);
        }
    }

    if (op_ == Op::Band)
    {
        const auto band = juce::Rectangle<float> (downPos_, bandEnd_);
        g.setColour (colour (Token::Accent).withAlpha (0.2f));
        g.fillRect (band);
        g.setColour (colour (Token::Accent));
        g.drawRect (band, 1.0f);
    }
    g.restoreState();

    if (places_.empty())
    {
        g.setColour (colour (Token::TextDim));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (text ("map.empty"), plot.toNearestInt(), juce::Justification::centred);
    }

    // Clavier
    const float ky = plot.getBottom() + 2.0f;
    const float kh = static_cast<float> (kKeyboardH);
    for (int k = juce::jmax (0, static_cast<int> (std::floor (keyStart_))); k <= juce::jmin (127, static_cast<int> (std::ceil (keyStart_ + keySpan_))); ++k)
    {
        const auto r = juce::Rectangle<float> (keyX (k), ky, keyX (k + 1) - keyX (k), kh);
        g.setColour (k == pressedKey_ ? colour (Token::Accent) : latched_[static_cast<std::size_t> (k)] ? colour (Token::KnobHot) : isBlackKey (k) ? colour (Token::PanelBg) : colour (Token::Text).withAlpha (0.85f));
        g.fillRect (r.reduced (0.5f));
        if (k == pressedKey_) // vélocité jouée : repère à la hauteur du doigt
        {
            g.setColour (colour (Token::BgApp));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (static_cast<int> (std::lround (pressedVelocity_ * 127.0f))), r.toNearestInt().withWidth (juce::jmax (28, r.toNearestInt().getWidth())),
                        juce::Justification::centredTop, false);
        }
        if (k % 12 == 0 && r.getWidth() > 6.0f)
        {
            g.setColour (isBlackKey (k) ? colour (Token::Text) : colour (Token::BgApp));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (u8 (import::midiToNoteName (k, import::NoteConvention {})), r.toNearestInt().withTrimmedLeft (-6).withWidth (juce::jmax (30, r.toNearestInt().getWidth())),
                        juce::Justification::bottomLeft, false);
        }
    }
}

// ---------------------------------------------------------------- souris

MappingMatrix::Hit MappingMatrix::hitTest (juce::Point<float> p) const
{
    Hit best;
    float bestScore = 1e9f;
    for (int i = static_cast<int> (places_.size()) - 1; i >= 0; --i)
    {
        const auto r = placeRect (places_[static_cast<std::size_t> (i)]);
        // un rectangle étroit garde son tiers central pour le déplacement (on zoome pour le saisir plus facilement)
        const float slopX = juce::jmin (kEdgeSlop, r.getWidth() / 3.0f), slopY = juce::jmin (kEdgeSlop, r.getHeight() / 3.0f);
        if (! r.expanded (kEdgeSlop).contains (p)) continue;

        Hit h;
        h.place = i;
        const bool nearL = std::abs (p.x - r.getX()) <= slopX && p.y >= r.getY() - slopY && p.y <= r.getBottom() + slopY;
        const bool nearR = std::abs (p.x - r.getRight()) <= slopX && p.y >= r.getY() - slopY && p.y <= r.getBottom() + slopY;
        const bool nearT = std::abs (p.y - r.getY()) <= slopY && p.x >= r.getX() - slopX && p.x <= r.getRight() + slopX;
        const bool nearB = std::abs (p.y - r.getBottom()) <= slopY && p.x >= r.getX() - slopX && p.x <= r.getRight() + slopX;
        if (nearL || nearR || nearT || nearB)
        {
            h.edge = true;
            // le bord le plus proche gagne ; un coin étire les deux axes
            const float dL = std::abs (p.x - r.getX()), dR = std::abs (p.x - r.getRight());
            const float dT = std::abs (p.y - r.getY()), dB = std::abs (p.y - r.getBottom());
            const bool horizontal = nearL || nearR;
            const bool vertical = nearT || nearB;
            if (horizontal) h.which = dL <= dR ? mapping::Edge::KeyLow : mapping::Edge::KeyHigh;
            if (vertical && ! horizontal) h.which = dB <= dT ? mapping::Edge::VelLow : mapping::Edge::VelHigh;
            if (horizontal && vertical)
            {
                h.both = true;
                h.second = dB <= dT ? mapping::Edge::VelLow : mapping::Edge::VelHigh;
            }
            const float score = juce::jmin (dL, dR, dT, dB);
            if (score < bestScore) { best = h; bestScore = score; }
            if (score < 1.0f) break;
        }
        else if (r.contains (p) && bestScore > 1000.0f)
        {
            best = h;
            bestScore = 1000.0f;
            break; // la place la plus haute dans l'ordre de dessin
        }
    }
    return best;
}

void MappingMatrix::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    const int hover = h.place;
    auto cursor = juce::MouseCursor::NormalCursor;
    if (h.place >= 0)
    {
        if (! h.edge) cursor = juce::MouseCursor::DraggingHandCursor;
        else if (h.both) cursor = juce::MouseCursor::UpDownLeftRightResizeCursor;
        else if (h.which == mapping::Edge::KeyLow || h.which == mapping::Edge::KeyHigh) cursor = juce::MouseCursor::LeftRightResizeCursor;
        else cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    setMouseCursor (cursor);
    if (hover != hoverPlace_) { hoverPlace_ = hover; repaint(); }
}

juce::Rectangle<float> MappingMatrix::keyboardArea() const
{
    const auto plot = plotArea();
    return { plot.getX(), plot.getBottom() + 2.0f, plot.getWidth(), static_cast<float> (kKeyboardH) };
}

// Joue la touche sous le doigt : haut de la touche = vélocité faible, bas = forte (comme dans beaucoup de sampleurs).
bool MappingMatrix::playKeyboard (juce::Point<float> p, bool starting)
{
    const auto kb = keyboardArea();
    if (starting && ! kb.contains (p)) return false;
    const int key = juce::jlimit (0, 127, static_cast<int> (std::floor (keyStart_ + (p.x - kb.getX()) / kb.getWidth() * keySpan_)));
    const float t = juce::jlimit (0.0f, 1.0f, (p.y - kb.getY()) / kb.getHeight());
    const float velocity = (1.0f + t * 126.0f) / 127.0f;
    if (key != pressedKey_)
    {
        if (pressedKey_ >= 0 && onNoteOff && ! latched_[static_cast<std::size_t> (pressedKey_)]) onNoteOff (pressedKey_);
        pressedKey_ = key;
        if (onNoteOn) onNoteOn (key, velocity);
    }
    pressedVelocity_ = velocity;
    repaint();
    return true;
}

void MappingMatrix::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isShiftDown() && keyboardArea().contains (e.position))
    {
        // Maj+clic : verrouille / libère une touche (elle reste enfoncée) : accords et legato jouables à la souris
        const auto kb = keyboardArea();
        const int key = juce::jlimit (0, 127, static_cast<int> (std::floor (keyStart_ + (e.position.x - kb.getX()) / kb.getWidth() * keySpan_)));
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.y - kb.getY()) / kb.getHeight());
        auto idx = static_cast<std::size_t> (key);
        latched_[idx] = ! latched_[idx];
        if (latched_[idx]) { if (onNoteOn) onNoteOn (key, (1.0f + t * 126.0f) / 127.0f); }
        else if (onNoteOff) onNoteOff (key);
        repaint();
        return;
    }
    if (playKeyboard (e.position, true)) { op_ = Op::None; keyboardDrag_ = true; return; }
    if (! plotArea().expanded (kEdgeSlop).contains (e.position)) return;
    down_ = hitTest (e.position);
    downPos_ = e.position;
    bandEnd_ = e.position;
    started_ = false;
    duplicating_ = false;
    extendOnUp_ = toggleOnUp_ = false;
    base_ = description_;

    if (down_.place < 0)
    {
        op_ = Op::Band;
        return;
    }
    const auto& place = places_[static_cast<std::size_t> (down_.place)];
    const bool ctrl = e.mods.isCommandDown() || e.mods.isCtrlDown();
    const bool already = placeSelected (place);
    if (! already && ! e.mods.isShiftDown() && ! (ctrl && ! down_.edge))
        selectPlaces ({ down_.place }, false); // un glissé sur une place non sélectionnée la sélectionne seule
    else if (! already)
        selectPlaces ({ down_.place }, true);  // Maj/Ctrl : ajoutée à la sélection
    else if (e.mods.isShiftDown()) extendOnUp_ = true;
    else if (ctrl && ! down_.edge) toggleOnUp_ = true; // Ctrl+clic : bascule ; Ctrl+glisser : duplique (décidé au mouvement)

    editZones_ = zonesOfSelectedPlaces();
    op_ = down_.edge ? Op::Resize : Op::Move;
    duplicating_ = ctrl && ! down_.edge;
}

void MappingMatrix::mouseDrag (const juce::MouseEvent& e)
{
    if (keyboardDrag_) { playKeyboard (e.position, false); return; } // glissando : la note suit le doigt
    if (op_ == Op::None) return;
    if (op_ == Op::Band)
    {
        bandEnd_ = e.position;
        repaint();
        return;
    }
    if (! started_ && e.position.getDistanceFrom (downPos_) < kMoveSlop) return;
    if (! started_)
    {
        started_ = true;
        toggleOnUp_ = extendOnUp_ = false;
        if (onGestureBegin) onGestureBegin();
        if (duplicating_)
        {
            std::vector<int> fresh;
            base_ = mapping::duplicateZones (base_, editZones_, 0, 0, &fresh);
            editZones_ = fresh;
            description_ = base_;
            if (onEdit) onEdit (base_, "Duplicate zones");
            selected_ = fresh;
            std::sort (selected_.begin(), selected_.end());
            if (onSelect) onSelect (selected_);
            rebuildPlaces();
        }
    }

    const double cellW = plotArea().getWidth() / keySpan_;
    const double cellH = plotArea().getHeight() / 127.0;
    const int dKey = static_cast<int> (std::lround ((e.position.x - downPos_.x) / cellW));
    const int dVel = static_cast<int> (std::lround (-(e.position.y - downPos_.y) / cellH));

    InstrumentDescription next;
    const char* name = "Move zones";
    if (op_ == Op::Move)
        next = mapping::moveZones (base_, editZones_, dKey, dVel, e.mods.isShiftDown());
    else
    {
        name = "Resize zones";
        auto step = [&] (const InstrumentDescription& from, mapping::Edge edge)
        {
            const bool keyAxis = edge == mapping::Edge::KeyLow || edge == mapping::Edge::KeyHigh;
            return mapping::resizeEdge (from, editZones_, edge, keyAxis ? dKey : dVel);
        };
        next = step (base_, down_.which);
        if (down_.both) next = step (next, down_.second);
    }
    description_ = next;
    rebuildPlaces();
    if (onEdit) onEdit (next, name);
    repaint();
}

void MappingMatrix::mouseUp (const juce::MouseEvent& e)
{
    if (keyboardDrag_)
    {
        keyboardDrag_ = false;
        if (pressedKey_ >= 0 && onNoteOff && ! latched_[static_cast<std::size_t> (pressedKey_)]) onNoteOff (pressedKey_);
        pressedKey_ = -1;
        repaint();
        return;
    }
    const Op op = op_;
    const bool started = started_;
    op_ = Op::None;
    started_ = false;
    if (op == Op::None) return;

    if (op == Op::Band)
    {
        const auto band = juce::Rectangle<float> (downPos_, e.position);
        if (band.getWidth() < kMoveSlop && band.getHeight() < kMoveSlop)
        {
            if (! (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown()))
            {
                selected_.clear();
                if (onSelect) onSelect (selected_);
            }
        }
        else
        {
            std::vector<int> hit;
            for (std::size_t i = 0; i < places_.size(); ++i)
                if (placeRect (places_[i]).intersects (band)) hit.push_back (static_cast<int> (i));
            selectPlaces (hit, e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown());
        }
        repaint();
        return;
    }
    if (started)
    {
        if (onGestureEnd) onGestureEnd();
        repaint();
        return;
    }
    // simple clic sur une place déjà sélectionnée
    if (down_.place >= 0 && down_.place < static_cast<int> (places_.size()))
    {
        const auto& place = places_[static_cast<std::size_t> (down_.place)];
        if (toggleOnUp_)
        {
            std::vector<int> keep;
            for (const int z : selected_)
                if (std::find (place.zones.begin(), place.zones.end(), z) == place.zones.end()) keep.push_back (z);
            selected_ = keep;
            if (onSelect) onSelect (selected_);
        }
        else if (! extendOnUp_ && ! e.mods.isShiftDown() && ! e.mods.isCommandDown() && ! e.mods.isCtrlDown())
            selectPlaces ({ down_.place }, false); // clic sans mouvement : seule cette place reste sélectionnée
    }
    repaint();
}

void MappingMatrix::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY);
        keyStart_ -= d * keySpan_ * 0.25;
        clampView();
        updateScrollBar();
        repaint();
        return;
    }
    const double key = keyStart_ + (e.position.x - plotArea().getX()) / plotArea().getWidth() * keySpan_;
    zoom (w.deltaY > 0 ? 0.8 : 1.25, key);
}

void MappingMatrix::duplicateSelection()
{
    const auto zones = zonesOfSelectedPlaces();
    if (zones.empty()) return;
    std::vector<int> fresh;
    auto next = mapping::duplicateZones (description_, zones, 0, 0, &fresh);
    description_ = next;
    selected_ = fresh;
    std::sort (selected_.begin(), selected_.end());
    if (onEdit) onEdit (next, "Duplicate zones");
    if (onSelect) onSelect (selected_);
    rebuildPlaces();
    repaint();
}

bool MappingMatrix::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (onDeleteRequested) onDeleteRequested();
        return true;
    }
    if ((k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) && (k.getKeyCode() == 'D' || k.getKeyCode() == 'd'))
    {
        duplicateSelection();
        return true;
    }
    return false;
}

void MappingMatrix::notePlayed (int key, int velocity)
{
    if (key < 0 || key > 127) return;
    played_[static_cast<std::size_t> (key)] = { juce::jlimit (1, 127, velocity), juce::Time::getMillisecondCounter() };
    if (! isTimerRunning()) startTimerHz (30);
    repaint();
}

void MappingMatrix::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    bool any = false;
    for (const auto& p : played_)
        if (p.velocity > 0 && now - p.time <= kCursorMs) { any = true; break; }
    repaint();
    if (! any) stopTimer();
}
