#include "NovaEqualizerEditor.h"

#include <algorithm>
#include <cmath>

#include "NovaSpectrum.h"
#include "neptune/core/EqualizerPresets.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace eq = neptune::core::equalizer;

namespace
{
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }
constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kRangeDb = 24.0;

bool hasGain (int t) { return t == eq::kPeak || t == eq::kLowShelf || t == eq::kHighShelf || t == eq::kTiltShelf || t == eq::kFlatGain; }
bool hasSlope (int t) { return t == eq::kLowPass || t == eq::kHighPass; }
bool hasQ (int t) { return t == eq::kPeak || t == eq::kLowShelf || t == eq::kHighShelf || t == eq::kNotch || t == eq::kBandPass || t == eq::kAllPass; }
bool hasFreq (int t) { return t != eq::kFlatGain; }

Token bandToken (int band)
{
    static const Token t[8] = { Token::Accent, Token::Pink, Token::Band1, Token::Band2, Token::Band3, Token::KnobHot, Token::Accent2, Token::Pink2 };
    return t[band % 8];
}

juce::String freqLabel (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }

juce::String freqText (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + "k" : juce::String (juce::roundToInt (f));
}

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // curseurs
    {
        for (const float x : { 4.0f, 12.0f, 20.0f }) { p.startNewSubPath (x, 21.0f); p.lineTo (x, x == 4.0f ? 14.0f : x == 12.0f ? 12.0f : 16.0f); p.startNewSubPath (x, x == 4.0f ? 10.0f : 8.0f); p.lineTo (x, 3.0f); }
    }
    else if (kind == 2)   // réglages : curseurs
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (6.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (14.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (8.0f, 16.0f, 4.0f, 4.0f);
    }
    else   // impulsion
    {
        p.startNewSubPath (2.0f, 12.0f); p.lineTo (7.0f, 12.0f); p.lineTo (10.0f, 4.0f); p.lineTo (14.0f, 20.0f); p.lineTo (17.0f, 12.0f); p.lineTo (22.0f, 12.0f);
    }
    return p;
}
} // namespace

// ------------------------------------------------------------------------------------------------ courbe de réponse interactive
class NovaEqualizerEditor::CurveView : public juce::Component
{
public:
    struct Hooks
    {
        std::function<int()> selected;
        std::function<void (int)> select;
        std::function<int (double, double)> add;
        std::function<void (int)> remove;
        std::function<void (int)> menu;
    };

    CurveView (const nova::Skin& s, ParamHost& h, const EqualizerModule* m, Hooks hooks) : skin_ (s), host_ (h), module_ (m), hooks_ (std::move (hooks)), engine_ (h, m) {}

    void stepSpectrum() { engine_.stepOnce(); }

    // 30 Hz : reconstruit les courbes seulement si un réglage ou le gain d'une bande dynamique a changé
    void update()
    {
        // spectre : l'analyse tourne tant qu'il est affiché ; une nouvelle image ou un changement d'échelle reconstruit les tracés
        {
            const auto& ps = host_.parameters();
            const bool on = ps.get (eq::spOn) >= 0.5;
            engine_.setRunning (on);
            bool changed = on && engine_.pullFrame (frame_);
            std::uint32_t sk = 0;
            for (const auto id : { eq::spOn, eq::spTop, eq::spMin, eq::spShowIn, eq::spShowOut, eq::spQuality }) sk = sk * 31u + ps.version (id);
            if (sk != spKey_) { spKey_ = sk; changed = true; }
            if (changed) { spValid_ = false; repaint(); }
        }
        std::uint32_t key = static_cast<std::uint32_t> (hooks_.selected() + 2);
        for (int b = 0; b < eq::kBands; ++b)
        {
            for (int k = 0; k <= eq::kSideLink; ++k) key = key * 31u + host_.parameters().version (eq::bandParam (b, k));
            if (module_ != nullptr && host_.parameters().get (eq::bandParam (b, eq::kDynOn)) >= 0.5)
                key = key * 31u + static_cast<std::uint32_t> (std::lround (module_->meter (eq::kBandGain0 + b) * 8.0f) + 4096);
        }
        key = key * 31u + host_.parameters().version (eq::gainScale);
        if (key == key_) return;
        key_ = key;
        valid_ = false;
        repaint();
    }

    void resized() override { valid_ = false; spValid_ = false; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
        const auto plot = plotArea();
        g.setFont (skin_.font (9.0f));
        for (const double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (skin_.colour (Token::Grid, 0.5f));
            g.drawVerticalLine (juce::roundToInt (xOf (f)), plot.getY(), plot.getBottom());
            if (f == 100.0 || f == 1000.0 || f == 10000.0 || f == 20.0 || f == 20000.0)
            {
                g.setColour (skin_.colour (Token::TextDim2));
                g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0),
                            juce::Rectangle<float> (xOf (f) - skin_.px (18.0f), plot.getBottom() + skin_.px (3.0f), skin_.px (36.0f), skin_.px (12.0f)), juce::Justification::centred);
            }
        }
        for (int d = -24; d <= 24; d += 12)
        {
            g.setColour (skin_.colour (Token::Grid, d == 0 ? 0.95f : 0.5f));
            g.drawHorizontalLine (juce::roundToInt (yOf (d)), plot.getX(), plot.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (d > 0 ? "+" + juce::String (d) : juce::String (d), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), plot.getX() - b.getX() - skin_.px (6.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        if (module_ == nullptr) return;
        if (host_.parameters().get (eq::spOn) >= 0.5)
        {
            if (! spValid_) rebuildSpectrum();
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (plot.toNearestInt());
            if (host_.parameters().get (eq::spShowIn) >= 0.5) { g.setColour (skin_.colour (Token::TextDim2, 0.22f)); g.fillPath (spPath_[0]); }
            if (host_.parameters().get (eq::spShowOut) >= 0.5)
            {
                g.setColour (skin_.colour (Token::Accent2, 0.20f));
                g.fillPath (spPath_[1]);
                g.setColour (skin_.colour (Token::Accent2, 0.85f));
                g.strokePath (spPath_[1], juce::PathStrokeType (skin_.px (1.4f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        if (! valid_) rebuild();

        const int sel = hooks_.selected();
        for (const auto& c : curves_)
        {
            const bool isSel = c.band == sel;
            if (isSel)
            {
                juce::Path filled (c.path);
                filled.lineTo (plot.getRight(), yOf (0.0));
                filled.lineTo (plot.getX(), yOf (0.0));
                filled.closeSubPath();
                g.setColour (skin_.colour (bandToken (c.band), 0.14f));
                g.fillPath (filled);
            }
            g.setColour (skin_.colour (bandToken (c.band), isSel ? 0.9f : 0.4f));
            g.strokePath (c.path, juce::PathStrokeType (skin_.px (isSel ? 1.8f : 1.1f)));
        }
        if (! curves_.empty())
        {
            g.setColour (skin_.colour (Token::Accent));   // courbe de résultat orangée
            g.strokePath (total_, juce::PathStrokeType (skin_.px (2.2f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // points
        for (const auto& c : curves_)
        {
            const auto p = pointOf (c.band);
            const float r = skin_.px (c.band == sel ? 8.0f : 7.0f);
            const bool dyn = host_.parameters().get (eq::bandParam (c.band, eq::kDynOn)) >= 0.5 && hasGain (c.type);
            if (c.band == sel)
            {
                g.setColour (skin_.colour (bandToken (c.band), 0.3f));
                g.fillEllipse (p.x - r * 1.7f, p.y - r * 1.7f, r * 3.4f, r * 3.4f);
            }
            g.setColour (skin_.colour (bandToken (c.band)));
            g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
            g.setColour (skin_.colour (Token::StageBg));
            if (dyn) g.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, skin_.px (1.6f));
            g.setFont (skin_.font (9.0f, true));
            g.drawText (juce::String (c.band + 1), juce::Rectangle<float> (p.x - r, p.y - r, r * 2, r * 2), juce::Justification::centred);
        }
    }

    // ---- souris
    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (hit (e.position, 16.0f) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int b = hit (e.position, 16.0f);
        if (b < 0) return;
        hooks_.select (b);
        if (e.mods.isPopupMenu()) { hooks_.menu (b); return; }
        drag_ = b;
        const int t = typeOf (b);
        host_.uiBeginEdit (eq::bandParam (b, eq::kFreq));
        if (hasGain (t)) host_.uiBeginEdit (eq::bandParam (b, eq::kGain));
        startPos_ = e.position;
        startFreq_ = host_.parameters().get (eq::bandParam (b, eq::kFreq));
        startGain_ = host_.parameters().get (eq::bandParam (b, eq::kGain));
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag_ < 0) return;
        const int b = drag_, t = typeOf (b);
        const float k = e.mods.isShiftDown() ? 0.1f : 1.0f;   // Maj : déplacement fin
        const auto plot = plotArea();
        if (hasFreq (t))
        {
            const double octaves = std::log2 (kMaxHz / kMinHz) * static_cast<double> ((e.position.x - startPos_.x) * k / plot.getWidth());
            const auto* spec = host_.parameters().find (eq::bandParam (b, eq::kFreq));
            const double f = std::clamp (startFreq_ * std::pow (2.0, octaves), kMinHz, kMaxHz);
            if (spec != nullptr) host_.uiSetNormalised (eq::bandParam (b, eq::kFreq), spec->toNormalized (f));
        }
        if (hasGain (t))
        {
            const double scale = host_.parameters().get (eq::gainScale) / 100.0;
            if (std::fabs (scale) > 0.05)
            {
                const double dDb = -static_cast<double> ((e.position.y - startPos_.y) * k / (plot.getHeight() * 0.5f)) * kRangeDb;
                const auto* spec = host_.parameters().find (eq::bandParam (b, eq::kGain));
                const double g = std::clamp (startGain_ + dDb / scale, -30.0, 30.0);
                if (spec != nullptr) host_.uiSetNormalised (eq::bandParam (b, eq::kGain), spec->toNormalized (g));
            }
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag_ < 0) return;
        const int b = drag_;
        if (hasGain (typeOf (b))) host_.uiEndEdit (eq::bandParam (b, eq::kGain));
        host_.uiEndEdit (eq::bandParam (b, eq::kFreq));
        drag_ = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = hit (e.position, 16.0f);
        if (b >= 0) { hooks_.remove (b); return; }
        const auto plot = plotArea();
        if (! plot.contains (e.position)) return;
        const double f = fOfX (e.position.x);
        const double g = std::clamp (-(static_cast<double> (e.position.y - plot.getCentreY()) / (plot.getHeight() * 0.5)) * kRangeDb, -18.0, 18.0);
        hooks_.add (f, g);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = hit (e.position, 24.0f);
        if (b < 0 || w.deltaY == 0.0f) return;
        const int t = typeOf (b);
        const int dir = w.deltaY > 0.0f ? 1 : -1;
        if (hasSlope (t))
        {
            const auto id = eq::bandParam (b, eq::kOrder);
            nova::setParam (host_, id, std::clamp (static_cast<int> (std::lround (host_.parameters().get (id))) + dir, 0, 6));
        }
        else if (hasQ (t))
        {
            const auto id = eq::bandParam (b, eq::kQ);
            const double q = std::clamp (host_.parameters().get (id) * std::pow (1.12, dir), 0.025, 25.0);
            nova::setParam (host_, id, q);
        }
        hooks_.select (b);
    }

private:
    struct Curve { int band; int type; juce::Path path; };

    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (34.0f), b.getY() + skin_.px (10.0f), b.getWidth() - skin_.px (34.0f + 14.0f), b.getHeight() - skin_.px (10.0f + 22.0f) };
    }
    float xOf (double f) const { const auto p = plotArea(); return p.getX() + static_cast<float> (std::log (f / kMinHz) / std::log (kMaxHz / kMinHz)) * p.getWidth(); }
    double fOfX (float x) const { const auto p = plotArea(); return kMinHz * std::pow (kMaxHz / kMinHz, static_cast<double> ((x - p.getX()) / p.getWidth())); }
    float yOf (double db) const { const auto p = plotArea(); return p.getCentreY() - static_cast<float> (std::clamp (db, -kRangeDb, kRangeDb) / kRangeDb) * p.getHeight() * 0.5f; }
    int typeOf (int b) const { return static_cast<int> (std::lround (host_.parameters().get (eq::bandParam (b, eq::kType)))); }

    // position d'un point : fréquence de la bande, gain réglé (avec l'échelle) ou gain courant d'une bande dynamique ; zéro pour les types sans gain
    juce::Point<float> pointOf (int b) const
    {
        const auto& ps = host_.parameters();
        const int t = typeOf (b);
        double db = 0.0;
        if (hasGain (t))
            db = ps.get (eq::bandParam (b, eq::kDynOn)) >= 0.5 && module_ != nullptr ? static_cast<double> (module_->meter (eq::kBandGain0 + b))
                                                                                   : ps.get (eq::bandParam (b, eq::kGain)) * ps.get (eq::gainScale) / 100.0;
        const double f = hasFreq (t) ? ps.get (eq::bandParam (b, eq::kFreq)) : 1000.0;
        return { xOf (std::clamp (f, kMinHz, kMaxHz)), yOf (db) };
    }
    int hit (juce::Point<float> p, float radius) const
    {
        int best = -1;
        float bestD = skin_.px (radius);
        for (const auto& c : curves_)
        {
            const float d = pointOf (c.band).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = c.band; }
        }
        return best;
    }
    void rebuildSpectrum()
    {
        const auto plot = plotArea();
        static constexpr float tops[7] = { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f };
        static constexpr float ranges[3] = { -60.0f, -72.0f, -96.0f };
        const float top = tops[juce::jlimit (0, 6, static_cast<int> (std::lround (host_.parameters().get (eq::spTop))))];
        const float range = ranges[juce::jlimit (0, 2, static_cast<int> (std::lround (host_.parameters().get (eq::spMin))))];
        auto xOfHz = [this] (float hz) { return xOf (static_cast<double> (hz)); };
        auto yOfDb = [plot, top, range] (float db) { return plot.getY() + (top - db) / (-range) * plot.getHeight(); };
        for (int src = 0; src < 2; ++src) spPath_[src] = nova::makeSpectrumPath (frame_, src, xOfHz, yOfDb, plot.getBottom() + plot.getHeight() * 0.5f, plot.getRight() + 4.0f);
        spValid_ = true;
    }

    void rebuild()
    {
        curves_.clear();
        total_.clear();
        const auto plot = plotArea();
        const int n = juce::jmax (8, static_cast<int> (plot.getWidth() / 3.0f));
        std::vector<double> sum (static_cast<std::size_t> (n) + 1, 0.0);
        std::vector<double> freqs (static_cast<std::size_t> (n) + 1);
        for (int i = 0; i <= n; ++i) freqs[static_cast<std::size_t> (i)] = kMinHz * std::pow (kMaxHz / kMinHz, static_cast<double> (i) / n);
        for (int b = 0; b < eq::kBands; ++b)
        {
            if (static_cast<int> (std::lround (host_.parameters().get (eq::bandParam (b, eq::kStatus)))) != eq::kOn) continue;
            Curve c { b, typeOf (b), {} };
            for (int i = 0; i <= n; ++i)
            {
                const double db = module_->bandResponseDb (host_.parameters(), b, freqs[static_cast<std::size_t> (i)], true);
                sum[static_cast<std::size_t> (i)] += db;
                const float x = xOf (freqs[static_cast<std::size_t> (i)]), y = yOf (db);
                if (i == 0) c.path.startNewSubPath (x, y); else c.path.lineTo (x, y);
            }
            curves_.push_back (std::move (c));
        }
        for (int i = 0; i <= n; ++i)
        {
            const float x = xOf (freqs[static_cast<std::size_t> (i)]), y = yOf (sum[static_cast<std::size_t> (i)]);
            if (i == 0) total_.startNewSubPath (x, y); else total_.lineTo (x, y);
        }
        // bandes contournées : points visibles mais sans courbe
        valid_ = true;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    const EqualizerModule* module_;
    Hooks hooks_;
    std::vector<Curve> curves_;
    juce::Path total_;
    nova::SpectrumEngine engine_;
    nova::SpectrumEngine::Frame frame_;
    juce::Path spPath_[2];
    bool spValid_ = false;
    std::uint32_t spKey_ = 0xFFFFFFFFu;
    bool valid_ = false;
    std::uint32_t key_ = 0xFFFFFFFFu;
    int drag_ = -1;
    juce::Point<float> startPos_;
    double startFreq_ = 1000.0, startGain_ = 0.0;
};

// ------------------------------------------------------------------------------------------------ bandeau de bandes
class NovaEqualizerEditor::BandStrip : public juce::Component
{
public:
    struct Hooks
    {
        std::function<std::vector<int>()> bands;
        std::function<int()> selected;
        std::function<void (int)> select;
        std::function<void (int)> remove;
        std::function<void()> add;
        std::function<juce::String (int)> label;   // « 1.2k  Bell »
        std::function<bool (int)> bypassed;
        std::function<bool (int)> dynamic;
    };
    BandStrip (const nova::Skin& s, Hooks h, juce::String addText) : skin_ (s), hooks_ (std::move (h)), addText_ (std::move (addText)) {}

    void tick()
    {
        const auto b = hooks_.bands();
        std::vector<juce::String> labels;
        for (const int i : b) labels.push_back (hooks_.label (i) + (hooks_.bypassed (i) ? "!" : "") + (hooks_.dynamic (i) ? "~" : ""));
        const int sel = hooks_.selected();
        if (b == bands_ && labels == labels_ && sel == sel_) return;
        bands_ = b; labels_ = std::move (labels); sel_ = sel;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (std::size_t k = 0; k < bands_.size(); ++k)
        {
            const int band = bands_[k];
            const auto r = chipRect (static_cast<int> (k)).toFloat().reduced (0.5f);
            skin_.fill (g, Token::BgCard, r, skin_.px (static_cast<float> (skin_.metrics().pillRadius)));
            g.setColour (band == sel_ ? skin_.colour (bandToken (band)) : skin_.colour (Token::CtlBd));
            g.drawRoundedRectangle (r, skin_.px (static_cast<float> (skin_.metrics().pillRadius)), band == sel_ ? skin_.borderWidth() * 1.5f : skin_.borderWidth());
            const float d = skin_.px (8.0f);
            g.setColour (skin_.colour (bandToken (band)));
            g.fillEllipse (r.getX() + skin_.px (9.0f), r.getCentreY() - d / 2, d, d);
            auto txt = labels_[k];
            const bool byp = txt.endsWith ("!") || txt.endsWith ("!~");
            txt = txt.removeCharacters ("!~");
            g.setColour (byp ? skin_.colour (Token::TextDim2) : skin_.colour (Token::TextDim));
            g.setFont (skin_.font (11.0f, true));
            g.drawText (txt, r.withTrimmedLeft (skin_.px (22.0f)).withTrimmedRight (skin_.px (16.0f)), juce::Justification::centredLeft, true);
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), r.withLeft (r.getRight() - skin_.px (18.0f)), juce::Justification::centred);
        }
        if (static_cast<int> (bands_.size()) < eq::kBands)
        {
            const auto r = addRect().toFloat().reduced (0.5f);
            juce::Path outline;
            outline.addRoundedRectangle (r, skin_.px (static_cast<float> (skin_.metrics().pillRadius)));
            juce::Path dashed;
            const float dash[] = { skin_.px (4.0f), skin_.px (3.0f) };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
            g.setColour (skin_.colour (Token::CtlBd));
            g.fillPath (dashed);
            g.setColour (skin_.colour (Token::TextDim2));
            g.setFont (skin_.font (11.0f, true));
            g.drawText (addText_, r, juce::Justification::centred);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        for (std::size_t k = 0; k < bands_.size(); ++k)
        {
            const auto r = chipRect (static_cast<int> (k));
            if (! r.contains (e.getPosition())) continue;
            if (e.x > r.getRight() - skin_.ipx (18.0f)) hooks_.remove (bands_[k]);
            else hooks_.select (bands_[k]);
            return;
        }
        if (static_cast<int> (bands_.size()) < eq::kBands && addRect().contains (e.getPosition())) hooks_.add();
    }

private:
    int chipWidth() const
    {
        const int n = static_cast<int> (bands_.size());
        const int addW = skin_.ipx (96.0f), gap = skin_.ipx (6.0f);
        const int avail = getWidth() - addW - gap * (n + 1);
        return juce::jlimit (skin_.ipx (58.0f), skin_.ipx (112.0f), n > 0 ? avail / n : avail);
    }
    juce::Rectangle<int> chipRect (int k) const { return { k * (chipWidth() + skin_.ipx (6.0f)), 0, chipWidth(), getHeight() }; }
    juce::Rectangle<int> addRect() const { return { static_cast<int> (bands_.size()) * (chipWidth() + skin_.ipx (6.0f)), 0, skin_.ipx (96.0f), getHeight() }; }

    const nova::Skin& skin_;
    Hooks hooks_;
    juce::String addText_;
    std::vector<int> bands_;
    std::vector<juce::String> labels_;
    int sel_ = -2;
};

// ------------------------------------------------------------------------------------------------ pastille de la bande sélectionnée
class NovaEqualizerEditor::BandLabel : public juce::Component
{
public:
    explicit BandLabel (const nova::Skin& s) : skin_ (s) {}
    void set (int band, const juce::String& text)
    {
        if (band == band_ && text == text_) return;
        band_ = band; text_ = text;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float d = skin_.px (10.0f);
        if (band_ >= 0)
        {
            g.setColour (skin_.colour (bandToken (band_)));
            g.fillEllipse (b.getX(), b.getCentreY() - d / 2, d, d);
        }
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (text_, b.withTrimmedLeft (band_ >= 0 ? d + skin_.px (8.0f) : 0.0f), juce::Justification::centredLeft);
    }
private:
    const nova::Skin& skin_;
    int band_ = -2;
    juce::String text_;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaEqualizerEditor::NovaEqualizerEditor (ParamHost& host, const EqualizerModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                          const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.equalizer")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("nv.page_equalizer"), iconPath (0));
    addPage (tx ("nv.page_dynamics"), iconPath (1));
    addPage (tx ("nv.page_settings"), iconPath (2));
    buildMain();
    buildDynamics();
    buildBottomBar();
    buildSettingsPage (2);
    selected_ = firstActive();
    bindSelected();
    if (const auto names = presetNames(); names.size() > 1) presetPill()->setText (names[0]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaEqualizerEditor::~NovaEqualizerEditor() = default;

std::vector<juce::String> NovaEqualizerEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : eq::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}
std::vector<std::pair<ParamId, double>> NovaEqualizerEditor::presetValues (int i) const { return eq::factoryPresets()[static_cast<std::size_t> (i)].values; }
std::vector<ParamId> NovaEqualizerEditor::presetParamIds() const { return eq::presetParams(); }

juce::String NovaEqualizerEditor::typeLabel (int type) const
{
    static const char* const keys[eq::kNumTypes] = { "nv.t_peak", "nv.t_lowshelf", "nv.t_lowpass", "nv.t_highshelf", "nv.t_highpass", "nv.t_notch", "nv.t_bandpass", "nv.t_tilt", "nv.t_allpass", "nv.t_flat" };
    return tx (keys[juce::jlimit (0, eq::kNumTypes - 1, type)]);
}

bool NovaEqualizerEditor::active (int band) const
{
    return band >= 0 && band < eq::kBands && choiceValue (eq::bandParam (band, eq::kStatus)) != eq::kOff;
}

int NovaEqualizerEditor::firstActive() const
{
    for (int b = 0; b < eq::kBands; ++b) if (active (b)) return b;
    return -1;
}

int NovaEqualizerEditor::addBand (double freq, double gain)
{
    int slot = -1;
    for (int b = 0; b < eq::kBands && slot < 0; ++b) if (! active (b)) slot = b;
    if (slot < 0) return -1;
    // l'emplacement repart de ses valeurs par défaut (une ancienne bande supprimée y a laissé ses réglages)
    for (int k = 0; k <= eq::kSideLink; ++k)
    {
        const auto id = eq::bandParam (slot, k);
        if (const auto* spec = host().parameters().find (id))
            if (k != eq::kStatus && value (id) != spec->defaultValue) nova::setParam (host(), id, spec->defaultValue);
    }
    nova::setParam (host(), eq::bandParam (slot, eq::kFreq), juce::jlimit (kMinHz, kMaxHz, freq));
    nova::setParam (host(), eq::bandParam (slot, eq::kGain), juce::jlimit (-18.0, 18.0, gain));
    nova::setParam (host(), eq::bandParam (slot, eq::kStatus), eq::kOn);
    select (slot);
    return slot;
}

void NovaEqualizerEditor::removeBand (int band)
{
    if (! active (band)) return;
    nova::setParam (host(), eq::bandParam (band, eq::kStatus), eq::kOff);
    if (selected_ == band)
    {
        int next = -1;
        for (int b = band + 1; b < eq::kBands && next < 0; ++b) if (active (b)) next = b;
        for (int b = band - 1; b >= 0 && next < 0; --b) if (active (b)) next = b;
        select (next);
    }
}

void NovaEqualizerEditor::select (int band)
{
    if (band == selected_) return;
    selected_ = band;
    bindSelected();
}

void NovaEqualizerEditor::showChoiceMenu (juce::Component* target, ParamId id, std::vector<juce::String> labels)
{
    juce::PopupMenu menu;
    const int cur = choiceValue (id);
    for (std::size_t i = 0; i < labels.size(); ++i) menu.addItem (static_cast<int> (i) + 1, labels[i], true, static_cast<int> (i) == cur);
    menu.setLookAndFeel (&target->getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, id, safe = juce::Component::SafePointer<juce::Component> (this)] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        nova::setParam (host(), id, choice - 1);
    });
}

void NovaEqualizerEditor::showBandMenu (int band)
{
    if (! active (band)) return;
    juce::PopupMenu types, stereo;
    const int curType = choiceValue (eq::bandParam (band, eq::kType)), curStereo = choiceValue (eq::bandParam (band, eq::kStereo));
    for (int t = 0; t < eq::kNumTypes; ++t) types.addItem (100 + t, typeLabel (t), true, t == curType);
    const char* const sk[5] = { "nv.s_stereo", "nv.s_left", "nv.s_right", "nv.s_mid", "nv.s_side" };
    for (int s = 0; s < 5; ++s) stereo.addItem (200 + s, tx (sk[s]), true, s == curStereo);
    juce::PopupMenu menu;
    menu.addSubMenu ("Type", types);
    menu.addSubMenu (tx ("nv.stereo_mode"), stereo);
    menu.addItem (1, tx ("nv.bypass_band"), true, choiceValue (eq::bandParam (band, eq::kStatus)) == eq::kBypassed);
    menu.addItem (2, tx ("nv.dynamic"), hasGain (curType), value (eq::bandParam (band, eq::kDynOn)) >= 0.5);
    menu.addSeparator();
    menu.addItem (3, tx ("nv.delete_band"));
    menu.setLookAndFeel (&getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, band, safe = juce::Component::SafePointer<juce::Component> (this)] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r >= 200) nova::setParam (host(), eq::bandParam (band, eq::kStereo), r - 200);
        else if (r >= 100) nova::setParam (host(), eq::bandParam (band, eq::kType), r - 100);
        else if (r == 1) nova::setParam (host(), eq::bandParam (band, eq::kStatus), choiceValue (eq::bandParam (band, eq::kStatus)) == eq::kBypassed ? eq::kOn : eq::kBypassed);
        else if (r == 2) nova::setParam (host(), eq::bandParam (band, eq::kDynOn), value (eq::bandParam (band, eq::kDynOn)) >= 0.5 ? 0.0 : 1.0);
        else if (r == 3) removeBand (band);
    });
}

void NovaEqualizerEditor::buildMain()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    addInfo (-1, metersPanel_, "hlp.eq_meters", true);
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, outMeter_ }) m->setModeVisible (false);

    curvePanel_ = add<nova::Panel> (0, skin());
    curvePanel_->setTitle (tx ("nv.eq_title"));
    CurveView::Hooks ch;
    ch.selected = [this] { return selected_; };
    ch.select = [this] (int b) { select (b); };
    ch.add = [this] (double f, double g) { return addBand (f, g); };
    ch.remove = [this] (int b) { removeBand (b); };
    ch.menu = [this] (int b) { showBandMenu (b); };
    curve_ = add<CurveView> (0, skin(), host(), module_, std::move (ch));
    spectrumPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    spectrumPill_->bindToggle (host(), eq::spOn, tx ("nv.spectrum"));
    spectrumGear_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Icon);
    {
        juce::Path gear;   // curseurs de réglage
        for (const float y : { 6.0f, 12.0f, 18.0f }) { gear.startNewSubPath (3.0f, y); gear.lineTo (21.0f, y); }
        gear.addEllipse (6.0f, 4.0f, 4.0f, 4.0f);
        gear.addEllipse (14.0f, 10.0f, 4.0f, 4.0f);
        gear.addEllipse (8.0f, 16.0f, 4.0f, 4.0f);
        spectrumGear_->setIcon (gear);
    }
    spectrumGear_->onClick = [this]
    {
        auto t = [this] (const char* k) { return tx (k); };
        std::vector<nova::SettingItem> items = {
            { eq::spQuality, t ("nv.sp_quality"), { "Normal", "High" } },
            { eq::spSpeed, t ("nv.sp_speed"), { "Very Fast", "Fast", "Medium", "Slow", "Very Slow" } },
            { eq::spTilt, t ("nv.sp_tilt"), { "0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" } },
            { eq::spSmoothType, t ("nv.sp_smooth_type"), { "OCT", "ERB" } },
            { eq::spSmoothOct, t ("nv.sp_smooth_oct"), { "1/24", "1/12", "1/6", "1/3", "1" } },
            { eq::spSmoothErb, t ("nv.sp_smooth_erb"), { "1/4", "1/2", "1", "2", "4" } },
            { eq::spTop, t ("nv.an_max"), { "6 dB", "0 dB", "-6 dB", "-12 dB", "-18 dB", "-24 dB", "-36 dB" } },
            { eq::spMin, t ("nv.sp_range"), { "-60 dB", "-72 dB", "-96 dB" } },
            { eq::spChannels, t ("nv.an_ch"), { "Stereo", "Left", "Right", "Mid", "Side" } },
            { eq::spShowIn, t ("nv.an_in"), {} }, { eq::spShowOut, t ("nv.an_out"), {} } };
        nova::showSettingsPopup (spectrumGear_, skin(), host(), t ("nv.spectrum"), std::move (items));
    };
    curvePanel_->setMetaRightInset (150.0f);

    BandStrip::Hooks sh;
    sh.bands = [this] { std::vector<int> v; for (int b = 0; b < eq::kBands; ++b) if (active (b)) v.push_back (b); return v; };
    sh.selected = [this] { return selected_; };
    sh.select = [this] (int b) { select (b); };
    sh.remove = [this] (int b) { removeBand (b); };
    sh.add = [this] { addBand (1000.0, 0.0); };
    sh.label = [this] (int b)
    {
        const int t = choiceValue (eq::bandParam (b, eq::kType));
        static const char* const abbr[eq::kNumTypes] = { "Bell", "L.Shelf", "LP", "H.Shelf", "HP", "Notch", "BP", "Tilt", "AP", "Flat" };
        return (hasFreq (t) ? freqText (value (eq::bandParam (b, eq::kFreq))) + "  " : juce::String()) + abbr[juce::jlimit (0, eq::kNumTypes - 1, t)];
    };
    sh.bypassed = [this] (int b) { return choiceValue (eq::bandParam (b, eq::kStatus)) == eq::kBypassed; };
    sh.dynamic = [this] (int b) { return value (eq::bandParam (b, eq::kDynOn)) >= 0.5 && hasGain (choiceValue (eq::bandParam (b, eq::kType))); };
    strip_ = add<BandStrip> (-2, skin(), std::move (sh), tx ("nv.add_band"));

    toolbarPanel_ = add<nova::Panel> (-2, skin());
    label_ = add<BandLabel> (-2, skin());
    typePill_ = add<nova::Pill> (-2, skin());
    typePill_->onClick = [this]
    {
        if (!active (selected_)) return;
        std::vector<juce::String> l;
        for (int t = 0; t < eq::kNumTypes; ++t) l.push_back (typeLabel (t));
        showChoiceMenu (typePill_, eq::bandParam (selected_, eq::kType), std::move (l));
    };
    onPill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    onPill_->setText (tx ("nv.on"));
    onPill_->onClick = [this]
    {
        if (active (selected_)) nova::setParam (host(), eq::bandParam (selected_, eq::kStatus), choiceValue (eq::bandParam (selected_, eq::kStatus)) == eq::kOn ? eq::kBypassed : eq::kOn);
    };
    dynPill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    stereoPill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    stereoPill_->onClick = [this]
    {
        if (!active (selected_)) return;
        showChoiceMenu (stereoPill_, eq::bandParam (selected_, eq::kStereo), { tx ("nv.s_stereo"), tx ("nv.s_left"), tx ("nv.s_right"), tx ("nv.s_mid"), tx ("nv.s_side") });
    };
    deletePill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    deletePill_->setText (tx ("nv.delete_band"));
    deletePill_->setTintToken (Token::KnobHot);
    deletePill_->onClick = [this] { removeBand (selected_); };

    addInfo (-2, typePill_, "hlp.bandtype");
    addInfo (-2, dynPill_, "hlp.banddynamic");
    addInfo (-2, stereoPill_, "hlp.bandstereo");

    for (int i = 0; i < 7; ++i)
    {
        auto* k = add<nova::KnobCard> (-2, skin(), host(), eq::bandParam (0, eq::kFreq), juce::String(), modalOpener(), false);
        k->setAutoRange (true);
        knobs_.push_back (k);
        addInfo (-2, k, "hlp.eq_freq", true);   // le sujet suit le paramètre affiché (assignKnobs)
    }
}

void NovaEqualizerEditor::buildDynamics()
{
    detPanel_ = add<nova::Panel> (1, skin());
    detPanel_->setTitle (tx ("nv.det_title"));
    dynBypassPill_ = add<nova::Pill> (1, skin());
    detTypePill_ = add<nova::Pill> (1, skin());
    detTypePill_->onClick = [this]
    {
        if (active (selected_)) showChoiceMenu (detTypePill_, eq::bandParam (selected_, eq::kSideType), { tx ("nv.d_bp"), tx ("nv.d_lp"), tx ("nv.d_hp"), tx ("nv.d_all") });
    };
    detSlopePill_ = add<nova::Pill> (1, skin());
    detSlopePill_->onClick = [this]
    {
        if (active (selected_))
            showChoiceMenu (detSlopePill_, eq::bandParam (selected_, eq::kSideOrder),
                            { tx ("nv.slope_6"), tx ("nv.slope_12"), tx ("nv.slope_24"), tx ("nv.slope_36"), tx ("nv.slope_48"), tx ("nv.slope_72"), tx ("nv.slope_96") });
    };
    detLinkPill_ = add<nova::Pill> (1, skin());
    addInfo (1, dynBypassPill_, "hlp.eq_dynbypass");
    addInfo (1, detTypePill_, "hlp.detect");
    addInfo (1, detLinkPill_, "hlp.detlink");
    const char* const keys[10] = { "nv.thresh", "nv.range", "nv.attack", "nv.release", "nv.knee", "nv.smooth", "nv.rms_len", "nv.rms_mix", "nv.det_freq", "nv.det_q" };
    const char* const helps[10] = { "hlp.dtthresh", "hlp.dtrange", "hlp.dtattack", "hlp.dtrelease", "hlp.dtknee", "hlp.dtsmooth", "hlp.dtrms", "hlp.dtrmsmix", "hlp.detect", "hlp.dtq" };
    for (int i = 0; i < 10; ++i)
    {
        auto* k = add<nova::KnobCard> (1, skin(), host(), eq::bandParam (0, eq::kFreq), tx (keys[i]), modalOpener(), false);
        k->setAutoRange (true);
        detKnobs_.push_back (k);
        if (helps[i] != nullptr) addInfo (1, k, helps[i], true);
    }
}

void NovaEqualizerEditor::buildBottomBar()
{
    phasePill_ = add<nova::Pill> (-1, skin());
    phasePill_->bindToggle (host(), eq::phaseFlip, tx ("nv.phase"));
    extPill_ = add<nova::Pill> (-1, skin());
    extPill_->bindToggle (host(), eq::externalSide, tx ("nv.ext_key"));
    // gain d'entrée (réglage commun des modules) et gain de sortie de l'égaliseur : pastilles à glisser sous les vu-mètres ; double-clic : saisie
    inTrimPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    inTrimPill_->bindValue (host(), stock::inputTrim, {});
    inTrimPill_->onClick = [this] { openModal (stock::inputTrim, uistate::Terminal::Pointer); };
    outPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    outPill_->bindValue (host(), eq::outputGain, {});
    outPill_->onClick = [this] { openModal (eq::outputGain, uistate::Terminal::Pointer); };
    scalePill_ = add<nova::Pill> (-1, skin());
    scalePill_->bindValue (host(), eq::gainScale, tx ("nv.gain_scale"));
    scalePill_->onClick = [this] { openModal (eq::gainScale, uistate::Terminal::Pointer); };
    addInfo (-1, phasePill_, "hlp.eqphase");
    addInfo (-1, extPill_, "hlp.eqext");
    addInfo (-1, outPill_, "hlp.eqout", 2);
    addInfo (-1, scalePill_, "hlp.eqscale");
    minPhasePill_ = add<nova::Pill> (-1, skin());
    minPhasePill_->setText (tx ("nv.min_phase"));
    minPhasePill_->setStatic (true);
    minPhasePill_->setDim (true);
}

// Rebranche la barre d'outils et les potards sur la bande sélectionnée
void NovaEqualizerEditor::bindSelected()
{
    const bool ok = active (selected_);
    if (ok)
    {
        dynPill_->bindToggle (host(), eq::bandParam (selected_, eq::kDynOn), tx ("nv.dynamic"));
        detLinkPill_->bindToggle (host(), eq::bandParam (selected_, eq::kSideLink), tx ("nv.side_link"));
        dynBypassPill_->bindToggle (host(), eq::bandParam (selected_, eq::kDynBypass), tx ("nv.dyn_bypass"));
    }
    assignKnobs();
    if (curve_ != nullptr) curve_->update();
}

void NovaEqualizerEditor::assignKnobs()
{
    const bool ok = active (selected_);
    const int t = ok ? choiceValue (eq::bandParam (selected_, eq::kType)) : eq::kPeak;
    const bool dyn = ok && hasGain (t) && value (eq::bandParam (selected_, eq::kDynOn)) >= 0.5;
    struct D { int k; const char* key; };
    std::vector<D> defs;
    if (hasFreq (t)) defs.push_back ({ eq::kFreq, "nv.freq" });
    if (hasGain (t)) defs.push_back ({ eq::kGain, "nv.gain" });
    if (hasSlope (t)) defs.push_back ({ eq::kOrder, "nv.slope" });
    if (hasQ (t)) defs.push_back ({ eq::kQ, "nv.q" });
    if (dyn)
    {
        defs.push_back ({ eq::kThreshold, "nv.thresh" });
        defs.push_back ({ eq::kTarget, "nv.range" });
        defs.push_back ({ eq::kAttack, "nv.attack" });
        defs.push_back ({ eq::kRelease, "nv.release" });
    }
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        const bool used = ok && i < defs.size() && currentPage() != 2;
        knobs_[i]->setVisible (used);
        if (used)
        {
            knobs_[i]->setParam (eq::bandParam (selected_, defs[i].k), tx (defs[i].key));
            const int k = defs[i].k;
            setInfoText (knobs_[i], k == eq::kFreq ? "hlp.eq_freq" : k == eq::kGain ? "hlp.eq_gain" : k == eq::kOrder ? "hlp.eq_slope" : k == eq::kQ ? "hlp.eq_q"
                                  : k == eq::kThreshold ? "hlp.dtthresh" : k == eq::kTarget ? "hlp.dtrange" : k == eq::kAttack ? "hlp.dtattack" : "hlp.dtrelease");
        }
    }
    if (ok)
        for (std::size_t i = 0; i < detKnobs_.size(); ++i)
        {
            const int ks[10] = { eq::kThreshold, eq::kTarget, eq::kAttack, eq::kRelease, eq::kKnee, eq::kSmooth, eq::kRmsLength, eq::kRmsMix, eq::kSideFreq, eq::kSideQ };
            const char* const keys[10] = { "nv.thresh", "nv.range", "nv.attack", "nv.release", "nv.knee", "nv.smooth", "nv.rms_len", "nv.rms_mix", "nv.det_freq", "nv.det_q" };
            detKnobs_[i]->setParam (eq::bandParam (selected_, ks[i]), tx (keys[i]));
        }
    for (auto* k : detKnobs_) k->setVisible (ok && currentPage() == 1);
}

void NovaEqualizerEditor::layout()
{
    metersPanel_->setBounds (R (14, 72, 176, 474));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (14.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 2, trimH = skin().ipx (60.0f);
        const auto meters = in.withTrimmedBottom (trimH);
        inMeter_->setBounds (meters.withWidth (w));
        outMeter_->setBounds (meters.withX (meters.getX() + w).withWidth (w));
        const int py = in.getBottom() - skin().ipx (50.0f), ph = skin().ipx (24.0f);
        inTrimPill_->setBounds (in.getX() + skin().ipx (6.0f), py, w - skin().ipx (12.0f), ph);
        outPill_->setBounds (in.getX() + w + skin().ipx (6.0f), py, w - skin().ipx (12.0f), ph);
    }
    // marges uniformes de 14 px entre tous les conteneurs. Colonne de gauche : le vu-mètre descend jusqu'au bas de la barre d'outils de la bande (546),
    // la zone du menu s'aligne sur les potards (560 à 710) ; les entrées y sont centrées.
    navPanel_->setBounds (R (14, 560, 176, 150));
    layoutNav (560, 150);

    curvePanel_->setBounds (R (204, 72, 942, 370));
    curve_->setBounds (R (220, 100, 910, 334));
    spectrumGear_->setBounds (R (1146 - 14 - 28, 76, 28, 24));
    spectrumPill_->setBounds (R (1146 - 14 - 28 - 8 - 104, 76, 104, 24));
    detPanel_->setBounds (R (204, 72, 942, 370));
    dynBypassPill_->setBounds (R (220, 108, 150, 30));
    detTypePill_->setBounds (R (392, 108, 150, 30));
    detSlopePill_->setBounds (R (574, 108, 120, 30));
    detLinkPill_->setBounds (R (702, 108, 90, 30));
    {
        const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
        for (std::size_t i = 0; i < detKnobs_.size(); ++i)
            detKnobs_[i]->setBounds (R (220.0f + static_cast<float> (i % 5) * (cw + 12.0f), i < 5 ? 146.0f : 280.0f, cw, 130));
    }
    strip_->setBounds (R (204, 456, 942, 32));
    toolbarPanel_->setBounds (R (204, 502, 942, 44));
    label_->setBounds (R (220, 512, 130, 24));
    typePill_->setBounds (R (352, 508, 130, 32));
    onPill_->setBounds (R (510, 508, 56, 32));
    dynPill_->setBounds (R (574, 508, 100, 32));
    stereoPill_->setBounds (R (702, 508, 96, 32));
    deletePill_->setBounds (R (1146 - 14 - 130, 508, 130, 32));
    const float kw = (942.0f - 12.0f * 6.0f) / 7.0f;
    for (std::size_t i = 0; i < knobs_.size(); ++i) knobs_[i]->setBounds (R (204.0f + static_cast<float> (i) * (kw + 12.0f), 560, kw, 150));

    // barre du bas
    const float by = 724.0f + 12.0f;
    abPill()->setBounds (R (14, by, 40, 32));
    float x = 58.0f;
    for (auto* p : { phasePill_, extPill_, scalePill_ })
    {
        const float w = p == phasePill_ ? 92.0f : p == extPill_ ? 100.0f : 150.0f;
        p->setBounds (R (x, by, w, 32));
        x += w + 26.0f;   // place du bouton « ? »
    }
    minPhasePill_->setBounds (R (1146 - 14 - 170, by, 170, 32));
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaEqualizerEditor::stepSpectrumForTest() { curve_->stepSpectrum(); }

void NovaEqualizerEditor::tick()
{
    // sélection : la bande sélectionnée a disparu (annulation, preset, hôte) ou aucune n'était choisie
    if (! active (selected_) && firstActive() != selected_)
    {
        selected_ = firstActive();
        bindSelected();
    }
    // les potards suivent le type (gain, pente, Q) et l'état dynamique de la bande
    {
        const int t = active (selected_) ? choiceValue (eq::bandParam (selected_, eq::kType)) : -1;
        const int d = active (selected_) ? choiceValue (eq::bandParam (selected_, eq::kDynOn)) : -1;
        if (t != lastType_ || d != lastDyn_ || selected_ != lastSel_) { lastType_ = t; lastDyn_ = d; lastSel_ = selected_; assignKnobs(); }
    }
    if (module_ != nullptr)
    {
        inMeter_->update (dbOf (module_->meter (eq::kInPeakL)), dbOf (module_->meter (eq::kInPeakR)));
        outMeter_->update (dbOf (module_->meter (eq::kOutPeakL)), dbOf (module_->meter (eq::kOutPeakR)));
    }
    curve_->update();
    strip_->tick();

    const bool ok = active (selected_);
    const int t = ok ? choiceValue (eq::bandParam (selected_, eq::kType)) : eq::kPeak;
    label_->set (ok ? selected_ : -1, ok ? tx ("nv.band") + " " + juce::String (selected_ + 1) : tx ("nv.no_band"));
    typePill_->setText (ok ? typeLabel (t) : juce::String ("--"));
    onPill_->setOn (ok && choiceValue (eq::bandParam (selected_, eq::kStatus)) == eq::kOn);
    const char* const sk[5] = { "nv.s_stereo", "nv.s_left", "nv.s_right", "nv.s_mid", "nv.s_side" };
    stereoPill_->setText (ok ? tx (sk[juce::jlimit (0, 4, choiceValue (eq::bandParam (selected_, eq::kStereo)))]) : juce::String());
    for (auto* p : { typePill_, onPill_, stereoPill_, deletePill_ }) p->setEnabled (ok);
    dynPill_->setVisible (ok && hasGain (t) && currentPage() != 2);
    dynPill_->setEnabled (ok);
    if (ok)
    {
        const char* const dk[4] = { "nv.d_bp", "nv.d_lp", "nv.d_hp", "nv.d_all" };
        detTypePill_->setText (tx ("nv.det_type") + "  " + tx (dk[juce::jlimit (0, 3, choiceValue (eq::bandParam (selected_, eq::kSideType)))]));
        static const char* const sl[7] = { "nv.slope_6", "nv.slope_12", "nv.slope_24", "nv.slope_36", "nv.slope_48", "nv.slope_72", "nv.slope_96" };
        detSlopePill_->setText (tx ("nv.slope") + "  " + tx (sl[juce::jlimit (0, 6, choiceValue (eq::bandParam (selected_, eq::kSideOrder)))]));
    }
    const float dim = ok && hasGain (t) && value (eq::bandParam (selected_, eq::kDynOn)) >= 0.5 ? 1.0f : 0.4f;
    for (auto* c : std::initializer_list<juce::Component*> { detTypePill_, detSlopePill_, detLinkPill_ }) c->setAlpha (dim);
    for (auto* k : detKnobs_) k->setAlpha (dim);
    dynBypassPill_->setAlpha (dim);
    {
        const auto title = tx ("nv.det_title") + (ok ? "   -   " + tx ("nv.band") + " " + juce::String (selected_ + 1) : juce::String());
        if (title != detTitle_) { detTitle_ = title; detPanel_->setTitle (title); }
    }
    detLinkPill_->refresh();

    if (ok)
    {
        const double f = value (eq::bandParam (selected_, eq::kFreq)), gdb = value (eq::bandParam (selected_, eq::kGain));
        const auto meta = juce::String::fromUTF8 (translator().format ("nv.band_readout", { std::to_string (selected_ + 1), freqLabel (f).toStdString(),
                                                                                                hasGain (t) ? juce::String (gdb, 1).toStdString() + " dB" : std::string ("--") }).c_str());
        if (meta != metaText_) { metaText_ = meta; curvePanel_->setMeta (meta); }
    }
    else if (metaText_ != tx ("nv.no_band")) { metaText_ = tx ("nv.no_band"); curvePanel_->setMeta (metaText_); }

}
