#include "NovaDelayEditor.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Biquad.h"
#include "neptune/core/DelayPresets.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace dl = neptune::core::delay;

namespace
{
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }
double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // barres d'écho
    {
        for (const float x : { 4.0f, 9.0f, 14.0f, 19.0f })
        {
            p.startNewSubPath (x, 20.0f);
            p.lineTo (x, x == 4.0f ? 5.0f : x == 9.0f ? 9.0f : x == 14.0f ? 13.0f : 17.0f);
        }
    }
    else if (kind == 1)   // lignes
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (6.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (14.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (8.0f, 16.0f, 4.0f, 4.0f);
    }
    else if (kind == 2)   // global
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    else   // réglages
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (14.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (6.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (12.0f, 16.0f, 4.0f, 4.0f);
    }
    return p;
}

Token lineToken (int n)
{
    static const Token t[dl::kLines] = { Token::Accent, Token::Pink, Token::Accent2, Token::Pink2, Token::Band1, Token::Band2, Token::Band3, Token::KnobHot };
    return t[n % dl::kLines];
}
} // namespace

// ------------------------------------------------------------------------------------------------ rangée des huit lignes
class NovaDelayEditor::LineRow : public juce::Component
{
public:
    LineRow (const nova::Skin& s, ParamHost& h, std::function<int()> selected, std::function<void (int)> select)
        : skin_ (s), host_ (h), selected_ (std::move (selected)), select_ (std::move (select)) {}

    void update()
    {
        std::uint32_t key = static_cast<std::uint32_t> (selected_());
        for (int n = 0; n < dl::kLines; ++n)
            for (const int k : { dl::kOn, dl::kSolo, dl::kLevel }) key = key * 31u + host_.parameters().version (dl::lineParam (n, k));
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int sel = selected_();
        for (int n = 0; n < dl::kLines; ++n)
        {
            const auto b = chip (n);
            const bool on = host_.parameters().get (dl::lineParam (n, dl::kOn)) >= 0.5, solo = host_.parameters().get (dl::lineParam (n, dl::kSolo)) >= 0.5;
            skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
            g.setColour (skin_.colour (n == sel ? lineToken (n) : Token::Grid, n == sel ? 1.0f : 0.8f));
            g.drawRoundedRectangle (b.reduced (0.5f), skin_.px (6.0f), skin_.px (n == sel ? 2.0f : 1.0f));
            // numéro
            const auto num = numberArea (b);
            g.setColour (skin_.colour (lineToken (n), on ? 1.0f : 0.35f));
            g.fillRoundedRectangle (num, skin_.px (5.0f));
            g.setColour (skin_.colour (Token::BgApp));
            g.setFont (skin_.font (15.0f, true));
            g.drawText (juce::String (n + 1), num, juce::Justification::centred);
            // ON et SOLO
            drawToggle (g, onArea (b), "ON", on, Token::Accent);
            drawToggle (g, soloArea (b), "S", solo, Token::Pink);
            // mini-niveau
            const auto bar = levelArea (b);
            g.setColour (skin_.colour (Token::Grid));
            g.fillRoundedRectangle (bar, bar.getHeight() / 2);
            const float frac = static_cast<float> (std::clamp ((host_.parameters().get (dl::lineParam (n, dl::kLevel)) + 60.0) / 66.0, 0.0, 1.0));
            g.setColour (skin_.colour (lineToken (n), on ? 1.0f : 0.35f));
            g.fillRoundedRectangle (bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * frac)), bar.getHeight() / 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragLine_ = -1;
        const int n = lineAt (e.position);
        if (n < 0) return;
        const auto b = chip (n);
        if (onArea (b).contains (e.position)) nova::setParam (host_, dl::lineParam (n, dl::kOn), host_.parameters().get (dl::lineParam (n, dl::kOn)) >= 0.5 ? 0.0 : 1.0);
        else if (soloArea (b).contains (e.position)) nova::setParam (host_, dl::lineParam (n, dl::kSolo), host_.parameters().get (dl::lineParam (n, dl::kSolo)) >= 0.5 ? 0.0 : 1.0);
        else if (levelArea (b).expanded (0.0f, skin_.px (5.0f)).contains (e.position))
        {
            dragLine_ = n;
            host_.uiBeginEdit (dl::lineParam (n, dl::kLevel));
            setLevel (n, e.position.x);
        }
        select_ (n);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragLine_ >= 0) setLevel (dragLine_, e.position.x); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragLine_ >= 0) host_.uiEndEdit (dl::lineParam (dragLine_, dl::kLevel));
        dragLine_ = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int n = lineAt (e.position);
        if (n >= 0 && levelArea (chip (n)).expanded (0.0f, skin_.px (5.0f)).contains (e.position)) host_.uiResetToDefault (dl::lineParam (n, dl::kLevel));
    }

private:
    juce::Rectangle<float> chip (int n) const
    {
        const float gap = skin_.px (8.0f), w = (static_cast<float> (getWidth()) - gap * 7.0f) / 8.0f;
        return { static_cast<float> (n) * (w + gap), 0.0f, w, static_cast<float> (getHeight()) };
    }
    int lineAt (juce::Point<float> p) const
    {
        for (int n = 0; n < dl::kLines; ++n) if (chip (n).contains (p)) return n;
        return -1;
    }
    juce::Rectangle<float> numberArea (juce::Rectangle<float> b) const { return { b.getX() + skin_.px (6.0f), b.getY() + skin_.px (6.0f), skin_.px (26.0f), skin_.px (24.0f) }; }
    juce::Rectangle<float> onArea (juce::Rectangle<float> b) const { return { b.getX() + skin_.px (36.0f), b.getY() + skin_.px (6.0f), skin_.px (30.0f), skin_.px (24.0f) }; }
    juce::Rectangle<float> soloArea (juce::Rectangle<float> b) const { return { b.getX() + skin_.px (70.0f), b.getY() + skin_.px (6.0f), skin_.px (26.0f), skin_.px (24.0f) }; }
    juce::Rectangle<float> levelArea (juce::Rectangle<float> b) const { return { b.getX() + skin_.px (8.0f), b.getBottom() - skin_.px (14.0f), b.getWidth() - skin_.px (16.0f), skin_.px (6.0f) }; }
    void drawToggle (juce::Graphics& g, juce::Rectangle<float> r, const char* text, bool lit, Token tint) const
    {
        if (lit) { g.setColour (skin_.colour (tint, 0.9f)); g.fillRoundedRectangle (r, skin_.px (5.0f)); }
        else { g.setColour (skin_.colour (Token::Grid)); g.drawRoundedRectangle (r.reduced (0.5f), skin_.px (5.0f), 1.0f); }
        g.setColour (skin_.colour (lit ? Token::BgApp : Token::TextDim));
        g.setFont (skin_.font (11.0f, true));
        g.drawText (text, r, juce::Justification::centred);
    }
    void setLevel (int n, float x)
    {
        const auto bar = levelArea (chip (n));
        const double frac = std::clamp (static_cast<double> ((x - bar.getX()) / bar.getWidth()), 0.0, 1.0);
        if (const auto* spec = host_.parameters().find (dl::lineParam (n, dl::kLevel)))
            host_.uiSetNormalised (dl::lineParam (n, dl::kLevel), spec->toNormalized (-60.0 + 66.0 * frac));
        repaint();
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    std::function<int()> selected_;
    std::function<void (int)> select_;
    std::uint32_t key_ = 0xFFFFFFFFu;
    int dragLine_ = -1;
};

// ------------------------------------------------------------------------------------------------ matrice des huit lignes (page TAPS)
class NovaDelayEditor::TapsMatrix : public juce::Component
{
public:
    TapsMatrix (const nova::Skin& s, ParamHost& h, std::function<int()> selected, std::function<void (int)> select)
        : skin_ (s), host_ (h), selected_ (std::move (selected)), select_ (std::move (select)) {}

    void update()
    {
        std::uint32_t key = static_cast<std::uint32_t> (selected_()) * 3u + host_.parameters().version (dl::sync);
        for (int n = 0; n < dl::kLines; ++n)
            for (int k = 0; k < dl::kLineParams; ++k) key = key * 31u + host_.parameters().version (dl::lineParam (n, k));
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool syncOn = host_.parameters().get (dl::sync) >= 0.5;
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        // en-tête
        g.setFont (skin_.font (11.0f, true));
        g.setColour (skin_.colour (Token::TextDim2));
        for (int c = 0; c < kCols; ++c)
        {
            const auto r = cell (-1, c);
            g.drawText (c == 3 && syncOn ? "DIV" : kCol[c].title, r, juce::Justification::centred);
        }
        const int sel = selected_();
        for (int n = 0; n < dl::kLines; ++n)
        {
            const bool on = host_.parameters().get (dl::lineParam (n, dl::kOn)) >= 0.5;
            const auto row = cell (n, 0).withLeft (cell (n, 0).getX() - skin_.px (4.0f)).withRight (cell (n, kCols - 1).getRight() + skin_.px (4.0f));
            if (n == sel) { g.setColour (skin_.colour (lineToken (n), 0.14f)); g.fillRoundedRectangle (row, skin_.px (5.0f)); g.setColour (skin_.colour (lineToken (n), 0.8f)); g.drawRoundedRectangle (row.reduced (0.5f), skin_.px (5.0f), skin_.px (1.2f)); }
            for (int c = 0; c < kCols; ++c)
            {
                const auto r = cell (n, c).reduced (skin_.px (2.0f), skin_.px (2.0f));
                if (c == 0)
                {
                    g.setColour (skin_.colour (lineToken (n), on ? 1.0f : 0.35f));
                    g.fillRoundedRectangle (r.withSizeKeepingCentre (skin_.px (24.0f), r.getHeight()), skin_.px (5.0f));
                    g.setColour (skin_.colour (Token::BgApp));
                    g.setFont (skin_.font (13.0f, true));
                    g.drawText (juce::String (n + 1), r, juce::Justification::centred);
                    continue;
                }
                const int k = kCol[c].k;
                const ParamId id = dl::lineParam (n, k == dl::kTime && syncOn ? dl::kDiv : k);
                const bool toggle = kCol[c].toggle;
                const bool value = host_.parameters().get (id) >= 0.5;
                if (toggle)
                {
                    g.setColour (value ? skin_.colour (k == dl::kSolo ? Token::Pink : Token::Accent, 0.9f) : skin_.colour (Token::Grid));
                    const auto box = r.withSizeKeepingCentre (skin_.px (18.0f), skin_.px (18.0f));
                    if (value) g.fillRoundedRectangle (box, skin_.px (4.0f)); else g.drawRoundedRectangle (box.reduced (0.5f), skin_.px (4.0f), 1.0f);
                    continue;
                }
                const bool neutral = isNeutral (id);
                g.setColour (skin_.colour (neutral ? Token::TextDim2 : Token::Text, on ? 1.0f : 0.45f));
                g.setFont (skin_.font (13.0f, ! neutral));
                g.drawText (juce::String::fromUTF8 (host_.parameters().toText (id).c_str()), r, juce::Justification::centred, true);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag_ = {};
        const auto at = cellAt (e.position);
        if (at.row < 0) return;
        select_ (at.row);
        if (at.col <= 0) return;
        const bool syncOn = host_.parameters().get (dl::sync) >= 0.5;
        const int k = kCol[at.col].k;
        const ParamId id = dl::lineParam (at.row, k == dl::kTime && syncOn ? dl::kDiv : k);
        if (kCol[at.col].toggle) { nova::setParam (host_, id, host_.parameters().get (id) >= 0.5 ? 0.0 : 1.0); repaint(); return; }
        if (e.mods.isPopupMenu()) return;
        drag_.id = id;
        drag_.start = host_.parameters().getNormalized (id);
        host_.uiBeginEdit (id);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag_.id == 0) return;
        const double scale = e.mods.isShiftDown() ? 1600.0 : 160.0;   // 160 px = toute la plage (Maj : dix fois plus fin)
        const double n = std::clamp (drag_.start - static_cast<double> (e.getDistanceFromDragStartY()) / (scale * static_cast<double> (skin_.px (1.0f))), 0.0, 1.0);
        host_.uiSetNormalised (drag_.id, n);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag_.id != 0) host_.uiEndEdit (drag_.id);
        drag_ = {};
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto at = cellAt (e.position);
        if (at.row < 0 || at.col <= 0 || kCol[at.col].toggle) return;
        const bool syncOn = host_.parameters().get (dl::sync) >= 0.5;
        const int k = kCol[at.col].k;
        host_.uiResetToDefault (dl::lineParam (at.row, k == dl::kTime && syncOn ? dl::kDiv : k));
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto at = cellAt (e.position);
        if (at.row < 0 || at.col <= 0 || kCol[at.col].toggle) return;
        const bool syncOn = host_.parameters().get (dl::sync) >= 0.5;
        const int k = kCol[at.col].k;
        const ParamId id = dl::lineParam (at.row, k == dl::kTime && syncOn ? dl::kDiv : k);
        const double step = (e.mods.isShiftDown() ? 0.002 : 0.02) * (w.deltaY > 0 ? 1.0 : -1.0);
        nova::setParamNormalised (host_, id, host_.parameters().getNormalized (id) + step);
        repaint();
    }

private:
    struct ColSpec { int k; const char* title; float w; bool toggle; };
    static constexpr int kCols = 14;
    static constexpr ColSpec kCol[kCols] = {
        { -1, "#", 34, false }, { dl::kOn, "ON", 40, true }, { dl::kSolo, "SOLO", 44, true }, { dl::kTime, "TIME", 82, false }, { dl::kLevel, "LEVEL", 66, false },
        { dl::kPan, "PAN", 66, false }, { dl::kOffset, "R OFFSET", 74, false }, { dl::kFeedback, "FEEDBACK", 74, false }, { dl::kPitch, "PITCH", 66, false },
        { dl::kLowCut, "LOW CUT", 72, false }, { dl::kHighCut, "HIGH CUT", 76, false }, { dl::kInvert, "INV", 40, true }, { dl::kModDepth, "MOD D", 62, false }, { dl::kModRate, "MOD R", 62, false } };
    struct Drag { ParamId id = 0; double start = 0.0; };
    struct At { int row = -1, col = -1; };

    float rowH() const { return skin_.px (40.0f); }
    float headH() const { return skin_.px (32.0f); }
    juce::Rectangle<float> cell (int row, int col) const
    {
        float total = 0.0f;
        for (const auto& c : kCol) total += c.w;
        const float scale = (static_cast<float> (getWidth()) - skin_.px (16.0f)) / total;
        float x = skin_.px (8.0f);
        for (int i = 0; i < col; ++i) x += kCol[i].w * scale;
        const float y = row < 0 ? 0.0f : headH() + static_cast<float> (row) * rowH();
        return { x, y, kCol[col].w * scale, row < 0 ? headH() : rowH() };
    }
    At cellAt (juce::Point<float> p) const
    {
        for (int r = 0; r < dl::kLines; ++r)
            for (int c = 0; c < kCols; ++c)
                if (cell (r, c).contains (p)) return { r, c };
        return {};
    }
    // valeur « neutre » (rien d'actif) affichée plus discrètement
    bool isNeutral (ParamId id) const
    {
        const auto* spec = host_.parameters().find (id);
        return spec != nullptr && std::fabs (host_.parameters().get (id) - spec->defaultValue) < 1.0e-9 && (id - dl::lineBase) % dl::lineStride != dl::kTime && (id - dl::lineBase) % dl::lineStride != dl::kDiv
               && (id - dl::lineBase) % dl::lineStride != dl::kLevel && (id - dl::lineBase) % dl::lineStride != dl::kFeedback;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    std::function<int()> selected_;
    std::function<void (int)> select_;
    std::uint32_t key_ = 0xFFFFFFFFu;
    Drag drag_;
};

// ------------------------------------------------------------------------------------------------ timeline des lignes / réponse des filtres
class NovaDelayEditor::DelayView : public juce::Component
{
public:
    DelayView (const nova::Skin& s, ParamHost& h, const DelayModule* m, std::function<int()> selected, std::function<void (int)> select, std::function<double()> rate)
        : skin_ (s), host_ (h), module_ (m), selected_ (std::move (selected)), select_ (std::move (select)), rate_ (std::move (rate)) {}

    bool filters = false;

    void update()
    {
        std::uint32_t key = static_cast<std::uint32_t> (selected_()) * 7u + (filters ? 1u : 0u) + static_cast<std::uint32_t> (std::lround (tempo() * 10.0));
        for (const auto id : { dl::sync, dl::routing }) key = key * 31u + host_.parameters().version (id);
        for (int n = 0; n < dl::kLines; ++n)
            for (int k = 0; k < dl::kLineParams; ++k) key = key * 31u + host_.parameters().version (dl::lineParam (n, k));
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        if (filters) paintFilters (g); else paintTimeline (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (! filters && hit (e.position).line >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag_ = {};
        if (filters || e.mods.isPopupMenu()) return;
        drag_ = hit (e.position);
        if (drag_.line < 0) return;
        select_ (drag_.line);
        frozenMax_ = tMax();
        const int n = drag_.line;
        const bool syncOn = host_.parameters().get (dl::sync) >= 0.5;
        host_.uiBeginEdit (syncOn ? dl::lineParam (n, dl::kDiv) : dl::lineParam (n, dl::kTime));
        host_.uiBeginEdit (dl::lineParam (n, dl::kLevel));
        drag_.sync = syncOn;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag_.line < 0) return;
        const int n = drag_.line;
        // temps : l'abscisse donne l'instant du premier écho (celui du canal droit moins son décalage) ; en cascade on retire les lignes précédentes
        const double offsetMs = drag_.right ? host_.parameters().get (dl::lineParam (n, dl::kOffset)) : 0.0;
        const double target = xToT (e.position.x) - offsetMs - prefixTime (n);
        if (drag_.sync)
        {
            const double bpm = tempo();
            int best = 0;
            double bestD = 1.0e9;
            for (int d = 0; d < dl::kDivisions; ++d)
                if (const double dd = std::fabs (dl::divisionBeats (d) * 60000.0 / bpm - target); dd < bestD) { bestD = dd; best = d; }
            setPlain (dl::lineParam (n, dl::kDiv), best);
        }
        else setPlain (dl::lineParam (n, dl::kTime), std::clamp (target, 1.0, dl::maxTimeMs));
        // niveau : la hauteur de la barre (moitié haute = gauche, moitié basse = droite)
        const auto plot = plotArea();
        const float centre = plot.getCentreY(), half = plot.getHeight() * 0.5f * 0.95f;
        const float frac = std::clamp ((drag_.right ? e.position.y - centre : centre - e.position.y) / half, 0.0f, 1.0f);
        setPlain (dl::lineParam (n, dl::kLevel), std::clamp (static_cast<double> (frac) * 54.0 - 48.0 - prefixLevelDb (n), -60.0, 6.0));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag_.line < 0) return;
        const int n = drag_.line;
        host_.uiEndEdit (drag_.sync ? dl::lineParam (n, dl::kDiv) : dl::lineParam (n, dl::kTime));
        host_.uiEndEdit (dl::lineParam (n, dl::kLevel));
        drag_ = {};
        repaint();
    }

private:
    struct Hit { int line = -1; bool right = false; bool sync = false; };

    double tempo() const { const double t = module_ != nullptr ? module_->tempo() : 0.0; return t > 1.0 ? t : 120.0; }
    bool on (int n) const { return host_.parameters().get (dl::lineParam (n, dl::kOn)) >= 0.5; }
    bool anySolo() const
    {
        for (int n = 0; n < dl::kLines; ++n) if (on (n) && host_.parameters().get (dl::lineParam (n, dl::kSolo)) >= 0.5) return true;
        return false;
    }
    bool audible (int n) const { return on (n) && (! anySolo() || host_.parameters().get (dl::lineParam (n, dl::kSolo)) >= 0.5); }
    double lineMs (int n) const
    {
        if (host_.parameters().get (dl::sync) >= 0.5)
            return dl::divisionBeats (static_cast<int> (std::lround (host_.parameters().get (dl::lineParam (n, dl::kDiv))))) * 60000.0 / tempo();
        return host_.parameters().get (dl::lineParam (n, dl::kTime));
    }
    bool cascade() const { return host_.parameters().get (dl::routing) >= 0.5; }
    // cascade : temps et niveaux des lignes actives précédentes s'additionnent
    double prefixTime (int n) const
    {
        double t = 0.0;
        if (cascade()) for (int k = 0; k < n; ++k) if (audible (k)) t += lineMs (k);
        return t;
    }
    double prefixLevelDb (int n) const
    {
        double l = 0.0;
        if (cascade()) for (int k = 0; k < n; ++k) if (audible (k)) l += host_.parameters().get (dl::lineParam (k, dl::kLevel));
        return l;
    }
    double firstEcho (int n) const { return prefixTime (n) + lineMs (n); }

    double tMax() const
    {
        if (drag_.line >= 0 && frozenMax_ > 0.0) return frozenMax_;
        double m = 400.0;
        for (int n = 0; n < dl::kLines; ++n)
            if (on (n)) m = std::max (m, (firstEcho (n) + std::max (0.0, host_.parameters().get (dl::lineParam (n, dl::kOffset)))) * 1.08);
        return m;
    }
    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (36.0f), b.getY() + skin_.px (12.0f), b.getWidth() - skin_.px (36.0f + 14.0f), b.getHeight() - skin_.px (12.0f + 24.0f) };
    }
    float tToX (double ms) const { const auto p = plotArea(); return p.getX() + static_cast<float> (ms / tMax()) * p.getWidth(); }
    double xToT (float x) const { const auto p = plotArea(); return std::clamp (static_cast<double> ((x - p.getX()) / p.getWidth()), 0.0, 1.0) * tMax(); }
    static float heightFrac (double lin) { return std::clamp (static_cast<float> ((20.0 * std::log10 (std::max (lin, 1.0e-6)) + 48.0) / 54.0), 0.0f, 1.0f); }
    void setPlain (ParamId id, double v)
    {
        if (const auto* spec = host_.parameters().find (id)) host_.uiSetNormalised (id, spec->toNormalized (v));
    }

    Hit hit (juce::Point<float> p) const
    {
        Hit best;
        float bestD = skin_.px (10.0f);
        const int sel = selected_();
        for (int n = 0; n < dl::kLines; ++n)
        {
            if (! on (n)) continue;
            for (const bool right : { false, true })
            {
                const double off = right ? host_.parameters().get (dl::lineParam (n, dl::kOffset)) : 0.0;
                const float x = tToX (firstEcho (n) + std::max (0.0, off));
                const bool inHalf = right ? p.y >= plotArea().getCentreY() - skin_.px (4.0f) : p.y <= plotArea().getCentreY() + skin_.px (4.0f);
                const float d = std::fabs (p.x - x) - (n == sel ? skin_.px (2.0f) : 0.0f);
                if (inHalf && d < bestD) { bestD = d; best.line = n; best.right = right; }
            }
        }
        return best;
    }

    void paintTimeline (juce::Graphics& g)
    {
        const auto plot = plotArea();
        const float centre = plot.getCentreY(), half = plot.getHeight() * 0.5f * 0.95f;
        const double tm = tMax();
        // grille : pas lisible (100 / 200 / 250 / 500 ms, 1 s, 2 s ...)
        double step = 100.0;
        for (const double s : { 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0 }) { step = s; if (tm / s <= 9.0) break; }
        g.setFont (skin_.font (9.0f));
        for (double t = 0.0; t <= tm + 1.0e-6; t += step)
        {
            const float x = tToX (t);
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (x - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (t >= 1000.0 ? juce::String (t / 1000.0, t / 1000.0 == std::floor (t / 1000.0) ? 0 : 2) + " s" : juce::String (static_cast<int> (t)) + " ms",
                        juce::Rectangle<float> (x - skin_.px (24.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        g.setColour (skin_.colour (Token::Grid));
        g.fillRect (plot.getX(), centre - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        g.drawText ("L", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY(), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getBottom() - skin_.px (16.0f), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);

        const int sel = selected_();
        // les lignes non sélectionnées d'abord, la sélectionnée par-dessus
        for (int pass = 0; pass < 2; ++pass)
            for (int n = 0; n < dl::kLines; ++n)
            {
                if (! on (n) || (pass == 0) == (n == sel)) continue;
                const bool heard = audible (n);
                const double lvDb = host_.parameters().get (dl::lineParam (n, dl::kLevel)) + prefixLevelDb (n);
                const double pan = host_.parameters().get (dl::lineParam (n, dl::kPan)) * 0.01;
                const double gl = pan > 0.0 ? 1.0 - pan : 1.0, gr = pan < 0.0 ? 1.0 + pan : 1.0;
                const double fb = std::fabs (host_.parameters().get (dl::lineParam (n, dl::kFeedback))) * 0.01;
                const double off = host_.parameters().get (dl::lineParam (n, dl::kOffset));
                const double t1 = firstEcho (n);
                for (int r = 0; r < 40; ++r)
                {
                    const double amp = dbToLin (lvDb) * std::pow (fb, r);
                    const double t = t1 * (r + 1);
                    if (t > tm || amp < 0.004) break;
                    const float alpha = (heard ? 1.0f : 0.3f) * (r == 0 ? 1.0f : 0.38f);
                    const float w = skin_.px (r == 0 ? (n == sel ? 6.0f : 4.5f) : 3.0f);
                    g.setColour (skin_.colour (lineToken (n), alpha));
                    const float hl = heightFrac (amp * gl) * half, hr = heightFrac (amp * gr) * half;
                    const float xl = tToX (t), xr = tToX (t + std::max (0.0, off) * (r + 1));
                    g.fillRoundedRectangle (xl - w * 0.5f, centre - hl, w, hl, w * 0.4f);
                    g.fillRoundedRectangle (xr - w * 0.5f, centre, w, hr, w * 0.4f);
                    if (r == 0)
                    {
                        g.setFont (skin_.font (10.0f, true));
                        g.drawText (juce::String (n + 1), juce::Rectangle<float> (xl - skin_.px (8.0f), centre - hl - skin_.px (14.0f), skin_.px (16.0f), skin_.px (12.0f)), juce::Justification::centred);
                    }
                }
            }
    }

    void paintFilters (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const auto plot = juce::Rectangle<float> (b.getX() + skin_.px (36.0f), b.getY() + skin_.px (12.0f), b.getWidth() - skin_.px (50.0f), b.getHeight() - skin_.px (36.0f));
        auto xOf = [&] (double f) { return plot.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        auto yOf = [&] (double db) { return plot.getY() + static_cast<float> ((6.0 - db) / 54.0) * plot.getHeight(); };
        g.setFont (skin_.font (9.0f));
        for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawVerticalLine (static_cast<int> (xOf (f)), plot.getY(), plot.getBottom());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0),
                        juce::Rectangle<float> (xOf (f) - skin_.px (16.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (32.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        for (const double d : { 0.0, -12.0, -24.0, -36.0, -48.0 })
        {
            g.setColour (skin_.colour (Token::Grid, d == 0.0 ? 1.0f : 0.6f));
            g.drawHorizontalLine (static_cast<int> (yOf (d)), plot.getX(), plot.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (static_cast<int> (d)), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), skin_.px (30.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        const int n = selected_();
        const double fs = rate_ ? rate_() : 48000.0;
        const double low = host_.parameters().get (dl::lineParam (n, dl::kLowCut)), high = host_.parameters().get (dl::lineParam (n, dl::kHighCut));
        const auto hp = BiquadCoefs::highpass (fs, low, 0.70710678);
        const auto lp = BiquadCoefs::lowpass (fs, std::min (high, 0.45 * fs), 0.70710678);
        // cumul des filtres sur les quatre premières répétitions (chaque répétition repasse par les filtres)
        for (int rep = 4; rep >= 1; --rep)
        {
            juce::Path p;
            for (int i = 0; i <= 240; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 240.0);
                double db = 0.0;
                if (low > 20.5) db += 20.0 * std::log10 (std::max (hp.magnitude (fs, f), 1.0e-6));
                if (high < 19990.0) db += 20.0 * std::log10 (std::max (lp.magnitude (fs, f), 1.0e-6));
                db = std::clamp (db * rep, -60.0, 6.0);
                const float x = xOf (f), y = yOf (db);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (skin_.colour (lineToken (n), rep == 1 ? 1.0f : 0.8f - 0.18f * static_cast<float> (rep)));
            g.strokePath (p, juce::PathStrokeType (skin_.px (rep == 1 ? 2.4f : 1.5f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (11.0f));
        g.drawText ("1x  2x  3x  4x", juce::Rectangle<float> (plot.getX() + skin_.px (6.0f), plot.getY() + skin_.px (4.0f), skin_.px (200.0f), skin_.px (16.0f)), juce::Justification::topLeft);
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    const DelayModule* module_;
    std::function<int()> selected_;
    std::function<void (int)> select_;
    std::function<double()> rate_;
    std::uint32_t key_ = 0xFFFFFFFFu;
    Hit drag_;
    double frozenMax_ = 0.0;
};

// ------------------------------------------------------------------------------------------------ simulation d'un coup d'entrée (page GLOBAL)
// Un second module de delay, à 16 kHz, reçoit les mêmes réglages et traite un court coup de sinus : l'enveloppe des échos (gauche en haut, droite en bas) et le
// gain de ducking sont tracés. Recalculé quand un réglage change (au plus dix fois par seconde), donc toujours fidèle au vrai module (feedback, routage,
// ping-pong, filtres, pitch, ducking).
class NovaDelayEditor::GlobalSim : public juce::Component
{
public:
    GlobalSim (const nova::Skin& s, ParamHost& h, const DelayModule* m) : skin_ (s), host_ (h), module_ (m), sim_ (std::make_unique<DelayModule>())
    {
        sim_.prepare (kFs, kBlock);
    }

    void update()
    {
        std::uint32_t key = static_cast<std::uint32_t> (std::lround ((module_ != nullptr ? module_->tempo() : 0.0) * 10.0));
        const auto& set = host_.parameters();
        for (std::size_t i = 0; i < set.size(); ++i) key = key * 31u + set.version (set.spec (i).id);
        if (key == key_ && ! dirty_) return;
        if (key != key_) { key_ = key; dirty_ = true; }
        const auto now = juce::Time::getMillisecondCounter();
        if (dirty_ && now - lastMs_ >= 100) { recompute(); lastMs_ = now; dirty_ = false; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
        const auto plot = juce::Rectangle<float> (b.getX() + skin_.px (36.0f), b.getY() + skin_.px (10.0f), b.getWidth() - skin_.px (50.0f), b.getHeight() - skin_.px (10.0f + 22.0f));
        const float duckH = skin_.px (34.0f), echoH = plot.getHeight() - duckH - skin_.px (8.0f);
        const float centre = plot.getY() + echoH * 0.5f, half = echoH * 0.5f * 0.95f;
        g.setFont (skin_.font (9.0f));
        for (int t = 0; t <= 8; ++t)
        {
            const float x = plot.getX() + plot.getWidth() * static_cast<float> (t) / 8.0f;
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (x - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (span_ * t / 8.0, span_ >= 4.0 ? 1 : 2) + " s", juce::Rectangle<float> (x - skin_.px (24.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        g.setColour (skin_.colour (Token::Grid));
        g.fillRect (plot.getX(), centre - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        g.drawText ("L", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY(), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY() + echoH - skin_.px (16.0f), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        g.drawText ("DUCK", juce::Rectangle<float> (b.getX() + skin_.px (2.0f), plot.getBottom() - duckH, skin_.px (34.0f), skin_.px (14.0f)), juce::Justification::centred);
        const std::size_t n = peaksL_.size();
        if (n == 0) return;
        const float colW = plot.getWidth() / static_cast<float> (n);
        auto frac = [] (float lin) { return std::clamp ((20.0f * std::log10 (std::max (lin, 1.0e-6f)) + 48.0f) / 48.0f, 0.0f, 1.0f); };
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = plot.getX() + static_cast<float> (i) * colW;
            g.setColour (skin_.colour (Token::Accent, 0.9f));
            const float hl = frac (peaksL_[i]) * half, hr = frac (peaksR_[i]) * half;
            if (hl > 0.5f) g.fillRect (x, centre - hl, std::max (1.0f, colW), hl);
            g.setColour (skin_.colour (Token::Pink2, 0.9f));
            if (hr > 0.5f) g.fillRect (x, centre, std::max (1.0f, colW), hr);
        }
        // coup d'entrée (repère) et courbe de ducking : 0 dB en haut de la bande, -24 dB en bas
        g.setColour (skin_.colour (Token::Handle, 0.9f));
        g.fillRect (plot.getX(), plot.getY(), std::max (2.0f, skin_.px (2.0f)), echoH);
        juce::Path duck;
        const float dTop = plot.getBottom() - duckH;
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = plot.getX() + (static_cast<float> (i) + 0.5f) * colW, y = dTop + (-duckDb_[i] / 24.0f) * duckH;
            if (i == 0) duck.startNewSubPath (x, y); else duck.lineTo (x, y);
        }
        g.setColour (skin_.colour (Token::KnobHot, 1.0f));
        g.strokePath (duck, juce::PathStrokeType (skin_.px (1.8f)));
    }

private:
    static constexpr double kFs = 16000.0;
    static constexpr int kBlock = 128;

    void recompute()
    {
        const auto& set = host_.parameters();
        auto& dst = sim_.params();
        for (std::size_t i = 0; i < set.size(); ++i) dst.set (set.spec (i).id, set.getAt (i));
        dst.set (stock::mix, 100.0);
        dst.set (stock::bypass, 0.0);
        auto& mod = static_cast<DelayModule&> (sim_.module());
        mod.setTempo (module_ != nullptr ? module_->tempo() : 0.0);
        // durée affichée : le plus long premier écho x 3, entre 1 et 6 s
        double longest = 0.3;
        const bool syncOn = set.get (dl::sync) >= 0.5;
        const double bpm = module_ != nullptr && module_->tempo() > 1.0 ? module_->tempo() : 120.0;
        for (int n = 0; n < dl::kLines; ++n)
            if (set.get (dl::lineParam (n, dl::kOn)) >= 0.5)
                longest = std::max (longest, (syncOn ? dl::divisionBeats (static_cast<int> (std::lround (set.get (dl::lineParam (n, dl::kDiv))))) * 60.0 / bpm : set.get (dl::lineParam (n, dl::kTime)) * 0.001));
        span_ = std::clamp (longest * 3.0, 1.0, 6.0);
        const int blocks = static_cast<int> (span_ * kFs / kBlock);
        sim_.reset();
        peaksL_.assign (static_cast<std::size_t> (blocks), 0.0f);
        peaksR_.assign (static_cast<std::size_t> (blocks), 0.0f);
        duckDb_.assign (static_cast<std::size_t> (blocks), 0.0f);
        std::vector<float> l (kBlock), r (kBlock);
        long sampleIndex = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i, ++sampleIndex)
            {
                const double t = static_cast<double> (sampleIndex) / kFs;
                const double v = t < 0.02 ? 0.7 * std::sin (3.14159265358979 * t / 0.02) * std::sin (2.0 * 3.14159265358979 * 1000.0 * t) : 0.0;
                l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = static_cast<float> (v);
            }
            sim_.process (l.data(), r.data(), kBlock, false);
            float pl = 0.0f, pr = 0.0f;
            for (int i = 0; i < kBlock; ++i) { pl = std::max (pl, std::fabs (l[static_cast<std::size_t> (i)])); pr = std::max (pr, std::fabs (r[static_cast<std::size_t> (i)])); }
            peaksL_[static_cast<std::size_t> (b)] = pl;
            peaksR_[static_cast<std::size_t> (b)] = pr;
            duckDb_[static_cast<std::size_t> (b)] = std::min (0.0f, mod.meter (dl::kDuckDb));
        }
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    const DelayModule* module_;
    ModuleRunner sim_;
    std::vector<float> peaksL_, peaksR_, duckDb_;
    double span_ = 2.0;
    std::uint32_t key_ = 0xFFFFFFFFu;
    std::uint32_t lastMs_ = 0;
    bool dirty_ = true;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaDelayEditor::NovaDelayEditor (ParamHost& host, const DelayModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                  const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.delay")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("dl.page_delay"), iconPath (0));
    addPage (tx ("dl.page_taps"), iconPath (1));
    addPage (tx ("dl.page_global"), iconPath (2));
    addPage (tx ("nv.page_settings"), iconPath (3));
    buildDelayPage();
    buildTapsPage();
    buildGlobalPage();
    buildSettingsPage (3);
    buildSettingsExtras();
    if (const auto names = presetNames(); ! names.empty()) presetPill()->setText (names[0]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaDelayEditor::~NovaDelayEditor() = default;

std::vector<juce::String> NovaDelayEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : dl::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}

std::vector<std::pair<ParamId, double>> NovaDelayEditor::presetValues (int i) const { return dl::factoryPresets()[static_cast<std::size_t> (i)].values; }

std::vector<ParamId> NovaDelayEditor::presetParamIds() const { return dl::presetParams(); }

void NovaDelayEditor::buildDelayPage()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    viewPanel_ = add<nova::Panel> (0, skin());
    viewPanel_->setTitle (tx ("dl.timeline"));
    viewPanel_->setMetaRightInset (130.0f);
    auto rate = [this] { const double r = options().sampleRate ? options().sampleRate() : 48000.0; return r > 0.0 ? r : 48000.0; };
    view_ = add<DelayView> (0, skin(), host(), module_, [this] { return selected_; }, [this] (int n) { selectLine (n); }, rate);
    rowDelay_ = add<LineRow> (0, skin(), host(), [this] { return selected_; }, [this] (int n) { selectLine (n); });
    viewPill_ = add<nova::Pill> (0, skin());
    viewPill_->onClick = [this] { showFilters_ = ! showFilters_; };
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, outMeter_ }) m->setModeVisible (false);
    inTrimPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    inTrimPill_->bindValue (host(), stock::inputTrim, {});
    inTrimPill_->onClick = [this] { openModal (stock::inputTrim, uistate::Terminal::Pointer); };
    addInfo (-1, inTrimPill_, "hlp.dl_input");

    // quatre potards qui suivent la ligne choisie, puis MIX et OUTPUT
    struct Spec { ParamId id; const char* title; bool pink; const char* help; };
    const Spec specs[7] = { { dl::lineParam (0, dl::kTime), "dl.time", false, "hlp.dl_time" }, { dl::lineParam (0, dl::kLevel), "dl.level", false, "hlp.dl_level" },
                            { dl::lineParam (0, dl::kPan), "dl.pan", false, "hlp.dl_pan" }, { dl::lineParam (0, dl::kFeedback), "dl.feedback", false, "hlp.dl_feedback" },
                            { dl::lineParam (0, dl::kPitch), "dl.pitch", false, "hlp.dl_pitch" },
                            { stock::mix, "dl.mix", true, "hlp.dl_mix" }, { stock::outputTrim, "dl.output", true, "hlp.dl_output" } };
    for (const auto& s : specs)
    {
        Column c {};
        c.card = add<nova::Panel> (0, skin());
        c.knob = add<nova::KnobCard> (0, skin(), host(), s.id, tx (s.title), modalOpener(), s.pink);
        c.knob->setPlain (true);
        addInfo (0, c.card, s.help, 1);
        columns_.push_back (c);
    }
}

void NovaDelayEditor::buildTapsPage()
{
    tapsPanel_ = add<nova::Panel> (1, skin());
    tapsPanel_->setTitle (tx ("dl.taps_title"));
    matrix_ = add<TapsMatrix> (1, skin(), host(), [this] { return selected_; }, [this] (int n) { selectLine (n); });
    struct P { int k; const char* title; const char* help; };
    for (const P p : { P { dl::kLowCut, "dl.lowcut", "hlp.dl_lowcut" }, P { dl::kHighCut, "dl.highcut", "hlp.dl_highcut" }, P { dl::kModDepth, "dl.moddepth", "hlp.dl_moddepth" },
                       P { dl::kModRate, "dl.modrate", "hlp.dl_modrate" }, P { dl::kOffset, "dl.offset", "hlp.dl_offset" } })
    {
        auto* kc = add<nova::KnobCard> (1, skin(), host(), dl::lineParam (0, p.k), tx (p.title), modalOpener(), false);
        tapsCards_.push_back (kc);
        addInfo (1, kc, p.help, true);
    }
    invertPill_ = add<nova::Pill> (1, skin());
    invertPill_->onClick = [this]
    {
        const ParamId id = dl::lineParam (selected_, dl::kInvert);
        nova::setParam (host(), id, value (id) >= 0.5 ? 0.0 : 1.0);
    };
    addInfo (1, invertPill_, "hlp.dl_invert");
    // copier / coller / remettre à zéro la ligne choisie
    copyPill_ = add<nova::Pill> (1, skin());
    copyPill_->setText (tx ("dl.copy"));
    copyPill_->onClick = [this]
    {
        clip_.clear();
        for (int k = 0; k < dl::kLineParams; ++k) clip_.push_back (value (dl::lineParam (selected_, k)));
    };
    pastePill_ = add<nova::Pill> (1, skin());
    pastePill_->setText (tx ("dl.paste"));
    pastePill_->onClick = [this]
    {
        if (clip_.size() != static_cast<std::size_t> (dl::kLineParams)) return;
        for (int k = 0; k < dl::kLineParams; ++k)
            if (k != dl::kSolo) nova::setParam (host(), dl::lineParam (selected_, k), clip_[static_cast<std::size_t> (k)]);
    };
    resetPill_ = add<nova::Pill> (1, skin());
    resetPill_->setText (tx ("dl.reset"));
    resetPill_->onClick = [this]
    {
        for (int k = 0; k < dl::kLineParams; ++k)
            if (const auto* spec = host().parameters().find (dl::lineParam (selected_, k))) nova::setParam (host(), spec->id, spec->defaultValue);
    };
    addInfo (1, copyPill_, "hlp.dl_copy");
}

void NovaDelayEditor::buildGlobalPage()
{
    globalPanel_ = add<nova::Panel> (2, skin());
    globalPanel_->setTitle (tx ("dl.global_title"));
    sim_ = add<GlobalSim> (2, skin(), host(), module_);
    syncPill_ = add<nova::Pill> (2, skin());
    syncPill_->bindToggle (host(), dl::sync, tx ("dl.sync"));
    routingPill_ = add<nova::Pill> (2, skin());
    routingPill_->bindChoice (host(), dl::routing, tx ("dl.routing"), { tx ("dl.parallel"), tx ("dl.cascade") });
    freezePill_ = add<nova::Pill> (2, skin());
    freezePill_->bindToggle (host(), dl::freeze, tx ("dl.freeze"));
    addInfo (2, syncPill_, "hlp.dl_sync");
    addInfo (2, routingPill_, "hlp.dl_routing");
    addInfo (2, freezePill_, "hlp.dl_freeze");
    syncNote_ = add<nova::Text> (2, skin(), Token::TextDim2, 11.0f);
    syncNote_->setText (tx ("dl.no_tempo"));
    struct P { ParamId id; const char* title; const char* help; };
    for (const P p : { P { dl::feedbackGlobal, "dl.fb_global", "hlp.dl_fbglobal" }, P { dl::pingPong, "dl.pingpong", "hlp.dl_pingpong" }, P { dl::drive, "dl.drive", "hlp.dl_drive" },
                       P { dl::width, "dl.width", "hlp.dl_width" }, P { stock::inputTrim, "dl.input", "hlp.dl_input" },
                       P { dl::duckAmount, "dl.duck_amount", "hlp.dl_duck_amount" }, P { dl::duckThreshold, "dl.duck_thresh", "hlp.dl_duck_thresh" },
                       P { dl::duckRelease, "dl.duck_release", "hlp.dl_duck_release" } })
    {
        auto* kc = add<nova::KnobCard> (2, skin(), host(), p.id, tx (p.title), modalOpener(), p.id >= dl::duckAmount && p.id <= dl::duckRelease);
        globalCards_.push_back (kc);
        addInfo (2, kc, p.help, true);
    }
}

void NovaDelayEditor::buildSettingsExtras()
{
    delaySection_ = add<nova::Text> (3, skin(), Token::TextDim2, 11.0f, true);
    delaySection_->setText (tx ("dl.set_section"));
    limiterPill_ = add<nova::Pill> (3, skin());
    limiterPill_->bindToggle (host(), dl::safetyLimiter, tx ("dl.limiter"));
    glideSlider_ = add<nova::SubSlider> (3, skin(), host(), dl::glideMs, tx ("dl.glide"));
    qualityPill_ = add<nova::Pill> (3, skin());
    qualityPill_->bindChoice (host(), dl::pitchQuality, tx ("dl.pitch_quality"), { tx ("dl.q_standard"), tx ("dl.q_high") });
    addInfo (3, qualityPill_, "hlp.dl_pitchquality");
    addInfo (3, limiterPill_, "hlp.dl_limiter");
    addInfo (3, glideSlider_, "hlp.dl_glide");
}

void NovaDelayEditor::rebindLineControls()
{
    const int n = selected_;
    const bool syncOn = value (dl::sync) >= 0.5;
    columns_[0].knob->setParam (dl::lineParam (n, syncOn ? dl::kDiv : dl::kTime), tx (syncOn ? "dl.div" : "dl.time"));
    columns_[1].knob->setParam (dl::lineParam (n, dl::kLevel), tx ("dl.level"));
    columns_[2].knob->setParam (dl::lineParam (n, dl::kPan), tx ("dl.pan"));
    columns_[3].knob->setParam (dl::lineParam (n, dl::kFeedback), tx ("dl.feedback"));
    columns_[4].knob->setParam (dl::lineParam (n, dl::kPitch), tx ("dl.pitch"));
    const int ks[5] = { dl::kLowCut, dl::kHighCut, dl::kModDepth, dl::kModRate, dl::kOffset };
    const char* titles[5] = { "dl.lowcut", "dl.highcut", "dl.moddepth", "dl.modrate", "dl.offset" };
    for (std::size_t i = 0; i < tapsCards_.size(); ++i) tapsCards_[i]->setParam (dl::lineParam (n, ks[i]), tx (titles[i]));
    boundLine_ = n;
    boundSync_ = syncOn ? 1 : 0;
}

void NovaDelayEditor::layout()
{
    // ---- page DELAY
    metersPanel_->setBounds (R (14, 72, 176, 410));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 2, trimH = skin().ipx (40.0f);
        const auto meters = in.withTrimmedBottom (trimH);
        inMeter_->setBounds (meters.withWidth (w));
        outMeter_->setBounds (meters.withX (meters.getX() + w).withWidth (w));
        inTrimPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), w - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 496, 176, 214));
    layoutNav (496, 214);
    viewPanel_->setBounds (R (204, 72, 942, 410));
    view_->setBounds (R (220, 104, 910, 300));
    rowDelay_->setBounds (R (220, 414, 910, 56));
    viewPill_->setBounds (R (1146 - 14 - 112, 76, 112, 24));
    const float colW = (942.0f - 12.0f * 6.0f) / 7.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const float x = 204.0f + static_cast<float> (i) * (colW + 12.0f);
        columns_[i].card->setBounds (R (x, 496, colW, 214));
        columns_[i].knob->setBounds (R (x, 496, colW, 132));
    }

    // ---- page TAPS
    tapsPanel_->setBounds (R (204, 72, 942, 638));
    matrix_->setBounds (R (220, 104, 910, 32.0f + 40.0f * 8.0f));
    const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
    for (std::size_t i = 0; i < tapsCards_.size(); ++i) tapsCards_[i]->setBounds (R (220.0f + static_cast<float> (i) * (cw + 12.0f), 474, cw, 150));
    invertPill_->setBounds (R (220, 650, 170, 32));
    copyPill_->setBounds (R (750, 650, 120, 32));
    pastePill_->setBounds (R (884, 650, 120, 32));
    resetPill_->setBounds (R (1018, 650, 112, 32));

    // ---- page GLOBAL
    globalPanel_->setBounds (R (204, 72, 942, 638));
    syncPill_->setBounds (R (220, 108, 110, 30));
    routingPill_->setBounds (R (372, 108, 210, 30));
    freezePill_->setBounds (R (624, 108, 120, 30));
    syncNote_->setBounds (R (780, 108, 350, 30));
    sim_->setBounds (R (220, 148, 910, 180));
    for (std::size_t i = 0; i < globalCards_.size(); ++i)
    {
        const float x = 220.0f + static_cast<float> (i % 5) * (cw + 12.0f), y = (i < 5 ? 340.0f : 522.0f);
        globalCards_[i]->setBounds (R (x, y, cw, 164));
    }

    // ---- page SETTINGS (en plus des sections communes)
    delaySection_->setBounds (R (220, 540, 400, 16));
    limiterPill_->setBounds (R (220, 568, 250, 34));
    glideSlider_->setBounds (R (510, 560, 300, 48));
    qualityPill_->setBounds (R (220, 618, 300, 34));

    // ---- barre du bas
    abPill()->setBounds (R (14, 724 + 12, 40, 32));

    // ---- en-tête
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaDelayEditor::tick()
{
    if (selected_ != boundLine_ || (value (dl::sync) >= 0.5 ? 1 : 0) != boundSync_) rebindLineControls();
    if (module_ != nullptr)
    {
        inMeter_->update (dbOf (module_->meter (dl::kInPeakL)), dbOf (module_->meter (dl::kInPeakR)));
        outMeter_->update (dbOf (module_->meter (dl::kOutPeakL)), dbOf (module_->meter (dl::kOutPeakR)));
    }
    const bool syncOn = value (dl::sync) >= 0.5;
    const double bpm = module_ != nullptr && module_->tempo() > 1.0 ? module_->tempo() : 120.0;
    juce::String meta = juce::String::fromUTF8 (translator().format ("dl.line_title", { std::to_string (selected_ + 1) }).c_str());
    meta += "  -  " + (syncOn ? juce::String (dl::divisionNames[std::clamp (choiceValue (dl::lineParam (selected_, dl::kDiv)), 0, dl::kDivisions - 1)])
                              : juce::String (value (dl::lineParam (selected_, dl::kTime)), 0) + " ms");
    if (meta != metaText_) { metaText_ = meta; viewPanel_->setMeta (meta); }

    if (currentPage() == 0)
    {
        view_->filters = showFilters_;
        viewPill_->setText (showFilters_ ? tx ("dl.filters") : tx ("dl.timeline"));
        view_->update();
        rowDelay_->update();
    }
    if (currentPage() == 1)
    {
        matrix_->update();
        invertPill_->setText (tx ("dl.invert") + "  " + (value (dl::lineParam (selected_, dl::kInvert)) >= 0.5 ? "ON" : "OFF"));
        invertPill_->setOn (value (dl::lineParam (selected_, dl::kInvert)) >= 0.5);
        const bool anyOn = value (dl::lineParam (selected_, dl::kOn)) >= 0.5;
        for (auto* k : tapsCards_) k->setAlpha (anyOn ? 1.0f : 0.4f);
    }
    if (currentPage() == 2)
    {
        sim_->update();
        syncPill_->setEnabled (options().hostTempo);
        syncPill_->setAlpha (options().hostTempo ? 1.0f : 0.4f);
        syncNote_->setVisible (! options().hostTempo);
    }
    const auto tempo = syncOn ? juce::String (bpm, 1) + " BPM" : juce::String();
    if (tempo != tempoText_) { tempoText_ = tempo; repaint (R (700, 724, 446, 56)); }
}

void NovaDelayEditor::paintBackground (juce::Graphics& g)
{
    g.setColour (skin().colour (Token::TextDim2));
    g.setFont (skin().font (11.0f, true));
    juce::String text = juce::String::fromUTF8 (translator().format ("nv.latency", { "0" }).c_str());
    if (tempoText_.isNotEmpty()) text = tx ("dl.tempo") + " " + tempoText_ + "     " + text;
    g.drawText (text, R (730, 724 + 12, 416, 32).toFloat(), juce::Justification::centredRight);
}
