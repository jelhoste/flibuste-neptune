#include "MappingMatrix.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include "neptune/core/ZoneImport.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kToolbarH = 40;
constexpr int kKeyboardH = 34;
constexpr int kLeftMargin = 36;
constexpr int kScrollH = 14;
constexpr float kEdgeSlop = 12.0f;   // zone de saisie d'un bord : >= 24 px de large au total
constexpr float kMoveSlop = 4.0f;

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
    zoomIn_.onClick = [this] { zoom (0.6, keyStart_ + keySpan_ / 2.0); };
    zoomOut_.onClick = [this] { zoom (1.0 / 0.6, keyStart_ + keySpan_ / 2.0); };
    zoomFit_.onClick = [this] { keyStart_ = 0.0; keySpan_ = 128.0; clampView(); updateScrollBar(); repaint(); };
    duplicate_.onClick = [this] { duplicateSelection(); };
    for (auto* b : { &zoomIn_, &zoomOut_, &zoomFit_, &duplicate_ }) addAndMakeVisible (*b);
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
    std::string signature = mapping::hasReleaseZones (description_) ? "r" : "-";
    const auto groups = mapping::groupNames (description_);
    for (const auto& g : groups) signature += "|" + g;
    if (signature == viewSignature_) return;
    viewSignature_ = signature;

    viewChoices_.clear();
    viewBox_.clear (juce::dontSendNotification);
    auto add = [this] (mapping::View v, const juce::String& label)
    {
        viewChoices_.push_back (std::move (v));
        viewBox_.addItem (label, static_cast<int> (viewChoices_.size()));
    };
    add ({ mapping::View::Kind::Main, {} }, text ("map.view_main"));
    if (mapping::hasReleaseZones (description_)) add ({ mapping::View::Kind::Release, {} }, text ("map.view_release"));
    for (const auto& g : groups) add ({ mapping::View::Kind::Group, g }, u8 (tr_.format ("map.view_group", { g })));

    int select = 1;
    for (std::size_t i = 0; i < viewChoices_.size(); ++i)
        if (viewChoices_[i] == view_) select = static_cast<int> (i) + 1;
    viewBox_.setSelectedId (select, juce::dontSendNotification);
    view_ = viewChoices_[static_cast<std::size_t> (select - 1)];
}

void MappingMatrix::rebuildPlaces() { places_ = mapping::places (description_, view_); }

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
    viewBox_.setBounds (bar.removeFromLeft (220));
    zoomOut_.setBounds (bar.removeFromLeft (44));
    zoomIn_.setBounds (bar.removeFromLeft (44));
    zoomFit_.setBounds (bar.removeFromLeft (90));
    duplicate_.setBounds (bar.removeFromLeft (130));
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
        g.setColour (isBlackKey (k) ? colour (Token::PanelBg) : colour (Token::Text).withAlpha (0.85f));
        g.fillRect (r.reduced (0.5f));
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

void MappingMatrix::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
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
