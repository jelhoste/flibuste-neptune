#include "NovaMiradorEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

#include "neptune/core/SpectrumAnalyzer.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mir = neptune::core::mirador;

namespace
{
juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // courbe de sonie
    {
        p.startNewSubPath (2.0f, 16.0f);
        p.lineTo (7.0f, 10.0f); p.lineTo (11.0f, 14.0f); p.lineTo (16.0f, 6.0f); p.lineTo (22.0f, 9.0f);
    }
    else if (kind == 1)   // barres de niveau
    {
        p.addRectangle (4.0f, 8.0f, 4.0f, 12.0f);
        p.addRectangle (10.0f, 4.0f, 4.0f, 16.0f);
        p.addRectangle (16.0f, 11.0f, 4.0f, 9.0f);
    }
    else if (kind == 2)   // vectorscope
    {
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
        p.startNewSubPath (12.0f, 3.0f); p.lineTo (12.0f, 21.0f);
        p.startNewSubPath (3.0f, 12.0f); p.lineTo (21.0f, 12.0f);
    }
    else if (kind == 3)   // spectre
    {
        p.startNewSubPath (2.0f, 20.0f);
        for (int i = 0; i <= 20; ++i) p.lineTo (2.0f + static_cast<float> (i), 20.0f - 14.0f * std::exp (-0.5f * std::pow ((static_cast<float> (i) - 7.0f) / 4.0f, 2.0f)));
    }
    else
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    return p;
}

// « -inf » sous -150
juce::String number (float v, int decimals, bool plus = false)
{
    if (v <= -150.0f) return juce::String::fromUTF8 ("-\xE2\x88\x9E");
    juce::String s (v, decimals);
    return plus && v > 0.0f ? "+" + s : s;
}

// couleurs traditionnelles des mètres : vert, jaune, rouge (les mêmes dans tous les thèmes)
const juce::Colour kGreen (0xFF3DBE5E), kYellow (0xFFE8C13A), kRed (0xFFE5484D);

struct Zone { float lo, hi; juce::Colour colour; };

// Dégradé vert -> jaune -> rouge, diffus : le jaune s'installe autour de `yellowAt`, le rouge autour de `redAt` (même unité que lo..hi), `soft` = demi-largeur
// des transitions. Le dégradé couvre tout le mètre : seule la partie sous le niveau est dessinée.
juce::ColourGradient zoneGradient (juce::Point<float> from, juce::Point<float> to, float lo, float hi, float yellowAt, float redAt, float soft)
{
    juce::ColourGradient grad (kGreen, from, kRed, to, false);
    auto pos = [lo, hi] (float v) { return static_cast<double> (std::clamp ((v - lo) / (hi - lo), 0.0f, 1.0f)); };
    grad.addColour (pos (yellowAt - soft), kGreen);
    grad.addColour (pos (yellowAt + soft), kYellow);
    grad.addColour (pos (redAt - soft), kYellow);
    grad.addColour (pos (redAt + soft), kRed);
    return grad;
}
} // namespace

// ------------------------------------------------------------------------------------------------ page SONIE : valeurs, historique et mètre à la même hauteur
class NovaMiradorEditor::LoudView : public juce::Component
{
public:
    LoudView (const nova::Skin& s, ParamHost& h, mir::MiradorEngine& e, const i18n::Translator& tr) : skin_ (s), host_ (h), engine_ (e), tr_ (tr) {}

    void update()
    {
        const auto& ps = host_.parameters();
        const auto r = engine_.loudness();
        const auto lv = engine_.levels();
        const std::size_t cap = static_cast<std::size_t> (std::lround (ps.get (mir::historyMinutes))) * 600u;   // un point toutes les 100 ms d'audio
        bool changed = false;
        if (cap != history_.capacity()) { history_.setCapacity (cap); changed = true; }
        // le temps de l'historique est celui de l'audio (échantillons vus par le moteur), pas celui de l'écran
        const std::uint64_t total = engine_.processedSamples();
        const std::uint64_t step = std::max<std::uint64_t> (1, static_cast<std::uint64_t> (engine_.sampleRate() / 10.0));
        if (! started_ || total < lastTotal_) { lastTotal_ = total; started_ = true; }
        if (total - lastTotal_ >= step)
        {
            const std::uint64_t n = std::min<std::uint64_t> ((total - lastTotal_) / step, 50);
            for (std::uint64_t i = 0; i < n; ++i) history_.add (r.momentary, r.shortTerm);
            lastTotal_ = total - (total - lastTotal_) % step;
            changed = true;
        }
        peak_ = std::max (lv.peakL, lv.peakR);
        // barre blanche du mètre, comme celle du mètre d'entrée : le plus haut niveau atteint par la mesure choisie, jusqu'à la remise à zéro ou au changement de mesure
        const int mode = meterMode();
        if (mode != lastMode_) { lastMode_ = mode; holdLevel_ = mir::kNoValue; }
        const float level = meterValue();
        if (level > holdLevel_) holdLevel_ = level;
        const auto key = std::make_tuple (r.momentary, r.shortTerm, r.integrated, r.truePeakDb, r.range, peak_ + holdLevel_,
                                          ps.version (mir::target), ps.version (mir::customTarget), ps.version (mir::tolerance), ps.version (mir::meterMode));
        if (key != lastKey_) { lastKey_ = key; changed = true; }
        reading_ = r;
        if (changed) { buildPaths(); repaint(); }
    }
    void openLegend() { legendOpen_ = true; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (legendButton().contains (e.position)) { legendOpen_ = ! legendOpen_; repaint(); }
    }
    void clearHistory() { history_.clear(); holdLevel_ = mir::kNoValue; buildPaths(); repaint(); }
    void resized() override { buildPaths(); }

    void paint (juce::Graphics& g) override
    {
        const double tgt = target(), tol = tolerance();
        const auto state = mir::targetState (reading_.integrated, tgt, tol);
        const juce::Colour stateColour = state == mir::TargetState::OnTarget ? kGreen : state == mir::TargetState::Over ? kRed
                                       : state == mir::TargetState::Under ? kYellow : skin_.colour (Token::Text);

        // ---- rangée de valeurs
        const auto cells = cellRects();
        const juce::String labels[7] = { tx ("mx.momentary"), tx ("mx.short"), tx ("mx.integrated"), tx ("mx.lu"), tx ("mx.range"), tx ("mx.truepeak"), tx ("mx.plr") };
        const juce::String units[7] = { "LUFS", "LUFS", "LUFS", "LU", "LU", "dBTP", "dB" };
        const juce::String values[7] = { number (reading_.momentary, 1), number (reading_.shortTerm, 1), number (reading_.integrated, 1),
                                         state == mir::TargetState::Unknown ? juce::String::fromUTF8 ("\xE2\x80\x94") : number (static_cast<float> (reading_.integrated - tgt), 1, true),
                                         juce::String (reading_.range, 1), number (reading_.truePeakDb, 1),
                                         number (mir::plr (reading_), 1) + " / " + number (mir::psr (reading_), 1) };
        for (std::size_t i = 0; i < 7; ++i)
        {
            const auto r = cells[i];
            skin_.fill (g, Token::StageBg, r, skin_.px (8.0f));
            juce::Colour valueColour = skin_.colour (Token::Text);
            if (i == 2 || i == 3) valueColour = stateColour;
            if (i == 5 && reading_.truePeakDb > -150.0f) valueColour = reading_.truePeakDb > -1.0f ? kRed : reading_.truePeakDb > -3.0f ? kYellow : valueColour;
            g.setColour (skin_.colour (Token::TextDim2));
            g.setFont (skin_.font (9.5f, true));
            g.drawText (labels[i], r.withTrimmedTop (skin_.px (8.0f)).withHeight (skin_.px (14.0f)), juce::Justification::centred);
            g.setColour (valueColour);
            g.setFont (skin_.font (i == 6 ? 17.0f : 24.0f, true));
            g.drawText (values[i], r.withTrimmedTop (skin_.px (22.0f)).withHeight (skin_.px (36.0f)), juce::Justification::centred);
            g.setColour (skin_.colour (Token::TextDim));
            g.setFont (skin_.font (10.0f));
            g.drawText (units[i], r.withTrimmedTop (skin_.px (60.0f)).withHeight (skin_.px (16.0f)), juce::Justification::centred);
        }

        // ---- historique
        const auto plot = plotArea();
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat().withTop (plot.getY() - skin_.px (28.0f)), skin_.px (8.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (tx ("mx.history"), juce::Rectangle<float> (plot.getX(), plot.getY() - skin_.px (24.0f), plot.getWidth(), skin_.px (16.0f)), juce::Justification::centredLeft);
        // la tolérance : bande verte autour de la cible, jaune sur 3 LU de part et d'autre, rouge au-delà vers le haut. Aucun trait : seule la cible a sa ligne
        auto band = [&] (double lo, double hi, juce::Colour c, float alpha)
        {
            const float y0 = yOf (static_cast<float> (hi)), y1 = yOf (static_cast<float> (lo));
            if (y1 <= y0) return;
            g.setColour (c.withAlpha (alpha));
            g.fillRect (juce::Rectangle<float> (plot.getX(), y0, plot.getWidth(), y1 - y0));
        };
        band (tgt + tol + 3.0, 0.0, kRed, 0.10f);
        band (tgt + tol, tgt + tol + 3.0, kYellow, 0.14f);
        band (tgt - tol, tgt + tol, kGreen, 0.20f);
        band (tgt - tol - 3.0, tgt - tol, kYellow, 0.08f);
        for (int db = -60; db <= 0; db += 10)
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawLine (plot.getX(), yOf (static_cast<float> (db)), plot.getRight(), yOf (static_cast<float> (db)), std::max (1.0f, skin_.px (1.0f)));
            g.setColour (skin_.colour (Token::Text, 0.8f));
            g.setFont (skin_.font (11.0f, true));
            g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - skin_.px (38.0f), yOf (static_cast<float> (db)) - skin_.px (8.0f), skin_.px (34.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        }
        g.setColour (kGreen.withAlpha (0.9f));
        g.drawLine (plot.getX(), yOf (static_cast<float> (tgt)), plot.getRight(), yOf (static_cast<float> (tgt)), std::max (1.5f, skin_.px (1.5f)));
        // sous la courbe à court terme : un voile qui s'estompe vers le bas ; puis un halo large et le trait fin
        g.setGradientFill (juce::ColourGradient (skin_.colour (Token::Accent, 0.28f), 0.0f, plot.getY(), skin_.colour (Token::Accent, 0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (shortArea_);
        const auto stroke = juce::PathStrokeType (std::max (1.2f, skin_.px (1.5f)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (skin_.colour (Token::TextDim2, 0.7f));
        g.strokePath (momentaryPath_, stroke);
        g.setColour (skin_.colour (Token::Accent, 0.25f));
        g.strokePath (shortPath_, juce::PathStrokeType (std::max (4.0f, skin_.px (5.0f)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (shortPath_, stroke);
        if (reading_.integrated > -150.0f)   // sonie intégrée : trait fin sur toute la largeur
        {
            g.setColour (skin_.colour (Token::Text, 0.75f));
            g.drawLine (plot.getX(), yOf (reading_.integrated), plot.getRight(), yOf (reading_.integrated), std::max (1.0f, skin_.px (1.0f)));
        }

        // ---- légende : un petit bouton en bas à droite du graphe déplie vers la gauche une ligne qui donne la correspondance des couleurs
        {
            const auto btn = legendButton();
            skin_.fill (g, Token::BgCard, btn, skin_.px (5.0f));
            g.setColour (skin_.colour (Token::CtlBd));
            g.drawRoundedRectangle (btn.reduced (0.5f), skin_.px (5.0f), skin_.borderWidth());
            g.setColour (skin_.colour (legendOpen_ ? Token::Accent : Token::Text, 0.9f));
            g.setFont (skin_.font (9.5f, true));
            g.drawText (juce::String (legendOpen_ ? "< " : "") + tx ("mx.legend"), btn, juce::Justification::centred);
            if (legendOpen_)
            {
                struct Item { juce::String text; juce::Colour colour; bool band; };
                const Item items[5] = { { tx ("mx.momentary"), skin_.colour (Token::TextDim2, 0.8f), false }, { tx ("mx.short"), skin_.colour (Token::Accent), false },
                                        { tx ("mx.integrated"), skin_.colour (Token::Text, 0.85f), false }, { tx ("mx.target"), kGreen, false },
                                        { tx ("mx.tolerance"), kGreen, true } };
                const float swatch = skin_.px (20.0f), gapText = skin_.px (6.0f), gapItem = skin_.px (16.0f), charW = skin_.px (6.6f);
                float total = 0.0f;
                for (const auto& it : items) total += swatch + gapText + static_cast<float> (it.text.length()) * charW + gapItem;
                const float x0 = btn.getX() - skin_.px (8.0f) - total;
                skin_.fill (g, Token::StageBg, juce::Rectangle<float> (x0 - skin_.px (6.0f), btn.getY() - skin_.px (2.0f), total + skin_.px (4.0f), btn.getHeight() + skin_.px (4.0f)), skin_.px (5.0f));
                float x = x0;
                for (const auto& it : items)
                {
                    const float cy = btn.getCentreY();
                    if (it.band)   // bande de tolérance : vert au centre, jaune de part et d'autre
                    {
                        g.setColour (kYellow.withAlpha (0.55f));
                        g.fillRect (x, cy - skin_.px (6.0f), swatch, skin_.px (12.0f));
                        g.setColour (kGreen.withAlpha (0.85f));
                        g.fillRect (x, cy - skin_.px (3.0f), swatch, skin_.px (6.0f));
                    }
                    else
                    {
                        g.setColour (it.colour);
                        g.fillRoundedRectangle (x, cy - skin_.px (1.5f), swatch, skin_.px (3.0f), skin_.px (1.5f));
                    }
                    g.setColour (skin_.colour (Token::Text, 0.85f));
                    g.setFont (skin_.font (9.5f, true));
                    const float tw = static_cast<float> (it.text.length()) * charW;
                    g.drawText (it.text, juce::Rectangle<float> (x + swatch + gapText, btn.getY(), tw + skin_.px (4.0f), btn.getHeight()), juce::Justification::centredLeft);
                    x += swatch + gapText + tw + gapItem;
                }
            }
        }

        // ---- mètre, à la même hauteur que le graphe (même échelle verticale)
        const auto bar = meterBar();
        const float level = meterValue();
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (tx (("mx.mm" + std::to_string (meterMode())).c_str()).toUpperCase() + (meterMode() == mir::kMeterPeak ? "  (dBFS)" : "  (LUFS)"), juce::Rectangle<float> (bar.getX() - skin_.px (40.0f), plot.getY() - skin_.px (24.0f), bar.getWidth() + skin_.px (80.0f), skin_.px (16.0f)), juce::Justification::centred);
        g.setColour (skin_.colour (Token::Grid));
        g.fillRoundedRectangle (bar, skin_.px (3.0f));
        {
            // même style que le mètre d'entrée de la colonne de gauche : coins arrondis, barre de crête à l'intérieur
            juce::Graphics::ScopedSaveState clipState (g);
            juce::Path clip;
            clip.addRoundedRectangle (bar, skin_.px (3.0f));
            g.reduceClipRegion (clip);
            if (level > -150.0f)
            {
                const bool peakMode = meterMode() == mir::kMeterPeak;
                const float hi = static_cast<float> (target() + tolerance());
                g.setGradientFill (peakMode ? zoneGradient ({ 0.0f, bar.getBottom() }, { 0.0f, bar.getY() }, -60.0f, 0.0f, -18.0f, -6.0f, 4.0f)
                                            : zoneGradient ({ 0.0f, bar.getBottom() }, { 0.0f, bar.getY() }, -60.0f, 0.0f, hi, hi + 3.0f, 1.2f));
                const float top = yOf (level);
                g.fillRect (bar.getX(), top, bar.getWidth(), bar.getBottom() - top);
            }
            if (holdLevel_ > -150.0f)   // barre de crête : suit les pics, tient puis retombe lentement
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (bar.getX(), yOf (holdLevel_) - skin_.px (1.0f), bar.getWidth(), skin_.px (2.0f));
            }
        }
        // graduation : un repère tous les 5 dB, un chiffre tous les 10 dB, à gauche du mètre
        for (int db = -60; db <= 0; db += 5)
        {
            const float y = yOf (static_cast<float> (db));
            const bool major = db % 10 == 0;
            g.setColour (skin_.colour (Token::Text, major ? 0.85f : 0.5f));
            g.drawLine (bar.getX() - skin_.px (major ? 8.0f : 5.0f), y, bar.getX() - skin_.px (1.0f), y, std::max (1.2f, skin_.px (1.3f)));
            if (major)
            {
                g.setFont (skin_.font (11.5f, true));
                g.drawText (juce::String (db), juce::Rectangle<float> (bar.getX() - skin_.px (46.0f), y - skin_.px (8.0f), skin_.px (36.0f), skin_.px (16.0f)), juce::Justification::centredRight);
            }
        }
        if (meterMode() != mir::kMeterPeak)   // repère de la cible sur le mètre
        {
            g.setColour (skin_.colour (Token::Text));
            const float y = yOf (static_cast<float> (tgt));
            g.drawLine (bar.getX(), y, bar.getRight(), y, std::max (1.5f, skin_.px (1.5f)));
        }
    }

private:
    juce::String tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    double target() const { const auto& ps = host_.parameters(); return mir::targetLufs (static_cast<int> (std::lround (ps.get (mir::target))), ps.get (mir::customTarget)); }
    double tolerance() const { return host_.parameters().get (mir::tolerance); }
    int meterMode() const { return juce::jlimit (0, mir::kNumMeterModes - 1, static_cast<int> (std::lround (host_.parameters().get (mir::meterMode)))); }
    float meterValue() const
    {
        switch (meterMode())
        {
            case mir::kMeterPeak: return peak_;
            case mir::kMeterMomentary: return reading_.momentary;
            case mir::kMeterShortTerm: return reading_.shortTerm;
            default: return reading_.integrated;
        }
    }
    std::array<juce::Rectangle<float>, 7> cellRects() const
    {
        std::array<juce::Rectangle<float>, 7> out;
        const float gap = skin_.px (8.0f), w = (static_cast<float> (getWidth()) - gap * 6.0f) / 7.0f, h = skin_.px (80.0f);
        for (std::size_t i = 0; i < 7; ++i) out[i] = { static_cast<float> (i) * (w + gap), 0.0f, w, h };
        return out;
    }
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (skin_.px (122.0f)).withTrimmedLeft (skin_.px (44.0f)).withTrimmedRight (skin_.px (150.0f)).withTrimmedBottom (skin_.px (46.0f));
    }
    juce::Rectangle<float> legendButton() const
    {
        const auto plot = plotArea();
        return { plot.getRight() - skin_.px (74.0f), plot.getBottom() - skin_.px (26.0f), skin_.px (68.0f), skin_.px (20.0f) };
    }
    juce::Rectangle<float> meterBar() const
    {
        const auto plot = plotArea();
        return { plot.getRight() + skin_.px (54.0f), plot.getY(), skin_.px (40.0f), plot.getHeight() };
    }
    float yOf (float lufs) const
    {
        const auto plot = plotArea();   // la même échelle verticale sert au graphe et au mètre
        return plot.getBottom() - std::clamp ((lufs + 60.0f) / 60.0f, 0.0f, 1.0f) * plot.getHeight();
    }
    void buildPaths()
    {
        momentaryPath_.clear();
        shortPath_.clear();
        shortArea_.clear();
        const auto plot = plotArea();
        const std::size_t cap = std::max<std::size_t> (history_.capacity(), 2);
        const float pointStep = plot.getWidth() / static_cast<float> (cap - 1);
        const std::size_t n = history_.size();
        // plus de points que de pixels : moyenne par paquets pour que la courbe reste fine et fluide
        const std::size_t stride = std::max<std::size_t> (1, static_cast<std::size_t> (static_cast<float> (n) / std::max (1.0f, plot.getWidth() * 1.5f)));
        bool openM = false, openS = false;
        std::vector<juce::Point<float>> seg;
        auto closeSegment = [&]
        {
            if (seg.size() >= 2)
            {
                shortArea_.startNewSubPath (seg.front().x, plot.getBottom());
                for (const auto& pt : seg) shortArea_.lineTo (pt);
                shortArea_.lineTo (seg.back().x, plot.getBottom());
                shortArea_.closeSubPath();
            }
            seg.clear();
        };
        for (std::size_t i = 0; i < n; i += stride)
        {
            const std::size_t last = std::min (n, i + stride) - 1;
            float sumM = 0.0f, sumS = 0.0f;
            int cm = 0, cs = 0;
            for (std::size_t j = i; j <= last; ++j)
            {
                const auto pt = history_.at (j);
                if (pt.momentary > -150.0f) { sumM += pt.momentary; ++cm; }
                if (pt.shortTerm > -150.0f) { sumS += pt.shortTerm; ++cs; }
            }
            const float x = plot.getRight() - static_cast<float> (n - 1 - last) * pointStep;
            if (cm == 0) openM = false;
            else
            {
                const float y = yOf (sumM / static_cast<float> (cm));
                if (! openM) { momentaryPath_.startNewSubPath (x, y); openM = true; }
                else momentaryPath_.lineTo (x, y);
            }
            if (cs == 0) { if (openS) closeSegment(); openS = false; }
            else
            {
                const float y = yOf (sumS / static_cast<float> (cs));
                if (! openS) { shortPath_.startNewSubPath (x, y); openS = true; }
                else shortPath_.lineTo (x, y);
                seg.emplace_back (x, y);
            }
        }
        closeSegment();
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    mir::MiradorEngine& engine_;
    const i18n::Translator& tr_;
    LoudnessMeter::Reading reading_;
    float peak_ = mir::kNoValue, holdLevel_ = mir::kNoValue;
    int lastMode_ = -1;
    bool legendOpen_ = false;
    mir::LoudnessHistory history_ { 6000 };
    juce::Path momentaryPath_, shortPath_, shortArea_;
    std::uint64_t lastTotal_ = 0;
    bool started_ = false;
    std::tuple<float, float, float, float, float, float, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t> lastKey_ {};
};

// ------------------------------------------------------------------------------------------------ page NIVEAUX : barres, crête tenue, RMS, écrêtage
class NovaMiradorEditor::LevelsView : public juce::Component
{
public:
    LevelsView (const nova::Skin& s, mir::MiradorEngine& e, const i18n::Translator& tr) : skin_ (s), engine_ (e), tr_ (tr) {}

    void update()
    {
        shown_ = engine_.levels();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const juce::String names[2] = { tx ("mx.l"), tx ("mx.r") };
        const float peak[2] = { shown_.peakL, shown_.peakR }, hold[2] = { shown_.holdL, shown_.holdR }, rms[2] = { shown_.rmsL, shown_.rmsR };
        const bool clip[2] = { shown_.clipL, shown_.clipR };
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto row = rowRect (ch);
            skin_.fill (g, Token::StageBg, row, skin_.px (8.0f));
            g.setColour (skin_.colour (Token::Text));
            g.setFont (skin_.font (18.0f, true));
            g.drawText (names[ch], juce::Rectangle<float> (row.getX(), row.getY() + skin_.px (20.0f), skin_.px (50.0f), skin_.px (34.0f)), juce::Justification::centred);
            const auto bar = barRect (row);
            g.setColour (skin_.colour (Token::Grid, 0.4f));
            g.fillRoundedRectangle (bar, skin_.px (3.0f));
            for (int db = -60; db <= 0; db += 10)
            {
                const float x = xOf (bar, static_cast<float> (db));
                g.setColour (skin_.colour (Token::Grid, 0.7f));
                g.drawLine (x, bar.getY() - skin_.px (4.0f), x, bar.getBottom() + skin_.px (4.0f), std::max (1.0f, skin_.px (1.0f)));
                g.setColour (skin_.colour (Token::TextDim2));
                g.setFont (skin_.font (9.0f));
                g.drawText (juce::String (db), juce::Rectangle<float> (x - skin_.px (16.0f), bar.getBottom() + skin_.px (5.0f), skin_.px (32.0f), skin_.px (12.0f)), juce::Justification::centred);
            }
            // dégradé diffus vert -> jaune -> rouge ; RMS en haut (atténué), crête en bas
            const auto grad = zoneGradient ({ xOf (bar, -60.0f), 0.0f }, { xOf (bar, 0.0f), 0.0f }, -60.0f, 0.0f, -18.0f, -6.0f, 4.0f);
            auto zoned = [&] (float level, float y, float h, float alpha)
            {
                if (level <= -150.0f) return;
                auto gr = grad;
                gr.multiplyOpacity (alpha);
                g.setGradientFill (gr);
                g.fillRect (juce::Rectangle<float> (bar.getX(), y, xOf (bar, level) - bar.getX(), h));
            };
            zoned (rms[ch], bar.getY(), bar.getHeight() * 0.62f, 0.55f);
            zoned (peak[ch], bar.getY() + bar.getHeight() * 0.62f, bar.getHeight() * 0.38f, 1.0f);
            if (hold[ch] > -150.0f)
            {
                g.setColour (skin_.colour (Token::Text));
                g.fillRect (juce::Rectangle<float> (xOf (bar, hold[ch]) - skin_.px (1.5f), bar.getY() - skin_.px (3.0f), skin_.px (3.0f), bar.getHeight() + skin_.px (6.0f)));
            }
            // valeurs, sous l'échelle
            g.setColour (skin_.colour (Token::Text));
            g.setFont (skin_.font (13.0f, true));
            g.drawText (tx ("mx.peak") + "  " + number (hold[ch], 1) + " dBFS      " + tx ("mx.rms") + "  " + number (rms[ch], 1) + " dBFS",
                        juce::Rectangle<float> (bar.getX(), row.getY() + skin_.px (92.0f), bar.getWidth(), skin_.px (24.0f)), juce::Justification::centredLeft);
            // voyant d'écrêtage
            const auto led = juce::Rectangle<float> (row.getRight() - skin_.px (40.0f), bar.getCentreY() - skin_.px (9.0f), skin_.px (18.0f), skin_.px (18.0f));
            g.setColour (clip[ch] ? kRed : skin_.colour (Token::Grid));
            g.fillEllipse (led);
        }
        // facteur de crête
        const auto cr = juce::Rectangle<float> (0.0f, rowRect (1).getBottom() + skin_.px (20.0f), static_cast<float> (getWidth()), skin_.px (60.0f));
        skin_.fill (g, Token::StageBg, cr, skin_.px (8.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (tx ("mx.crest"), cr.withHeight (skin_.px (22.0f)).translated (0.0f, skin_.px (6.0f)), juce::Justification::centred);
        g.setColour (skin_.colour (Token::Text));
        g.setFont (skin_.font (24.0f, true));
        g.drawText (shown_.crest > 0.0f ? juce::String (shown_.crest, 1) + " dB" : juce::String::fromUTF8 ("\xE2\x80\x94"), cr.withTrimmedTop (skin_.px (24.0f)), juce::Justification::centred);
    }

private:
    juce::String tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Rectangle<float> rowRect (int ch) const
    {
        const float h = skin_.px (124.0f), gap = skin_.px (14.0f);
        return { 0.0f, static_cast<float> (ch) * (h + gap), static_cast<float> (getWidth()), h };
    }
    juce::Rectangle<float> barRect (juce::Rectangle<float> row) const
    {
        return { row.getX() + skin_.px (66.0f), row.getY() + skin_.px (22.0f), row.getWidth() - skin_.px (66.0f) - skin_.px (70.0f), skin_.px (34.0f) };
    }
    static float xOf (juce::Rectangle<float> bar, float db) { return bar.getX() + std::clamp ((db + 60.0f) / 60.0f, 0.0f, 1.0f) * bar.getWidth(); }

    const nova::Skin& skin_;
    mir::MiradorEngine& engine_;
    const i18n::Translator& tr_;
    mir::Levels shown_ {};
};

// ------------------------------------------------------------------------------------------------ page STÉRÉO : vectorscope, corrélation, équilibre, largeur
class NovaMiradorEditor::StereoView : public juce::Component
{
public:
    StereoView (const nova::Skin& s, mir::MiradorEngine& e, const i18n::Translator& tr) : skin_ (s), engine_ (e), tr_ (tr) {}

    void update()
    {
        shown_ = engine_.levels();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const float side = std::min (static_cast<float> (getHeight()), static_cast<float> (getWidth()) * 0.58f);
        const auto sq = juce::Rectangle<float> (0.0f, 0.0f, side, side);
        skin_.fill (g, Token::StageBg, sq, skin_.px (8.0f));
        const auto c = sq.getCentre();
        const float radius = side * 0.46f;
        g.setColour (skin_.colour (Token::Grid, 0.7f));
        g.drawEllipse (juce::Rectangle<float> (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f), std::max (1.0f, skin_.px (1.0f)));
        g.drawLine (c.x, c.y - radius, c.x, c.y + radius, std::max (1.0f, skin_.px (1.0f)));
        g.drawLine (c.x - radius, c.y, c.x + radius, c.y, std::max (1.0f, skin_.px (1.0f)));
        const float d = radius * 0.7071f;
        g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, std::max (1.0f, skin_.px (1.0f)));
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, std::max (1.0f, skin_.px (1.0f)));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        const float m = skin_.px (6.0f);
        g.drawText ("M", juce::Rectangle<float> (c.x - skin_.px (10.0f), sq.getY() + m, skin_.px (20.0f), skin_.px (14.0f)), juce::Justification::centred);
        g.drawText ("S", juce::Rectangle<float> (sq.getRight() - skin_.px (24.0f), c.y - skin_.px (7.0f), skin_.px (20.0f), skin_.px (14.0f)), juce::Justification::centred);
        g.drawText (tx ("mx.l"), juce::Rectangle<float> (c.x - d - skin_.px (22.0f), c.y - d - skin_.px (8.0f), skin_.px (20.0f), skin_.px (14.0f)), juce::Justification::centred);
        g.drawText (tx ("mx.r"), juce::Rectangle<float> (c.x + d + skin_.px (2.0f), c.y - d - skin_.px (8.0f), skin_.px (20.0f), skin_.px (14.0f)), juce::Justification::centred);

        // points : les 1024 derniers échantillons, gain automatique pour que le dessin occupe le cercle
        const auto& ring = engine_.ring();
        const std::uint64_t wp = ring.writePos();
        constexpr int kPoints = 1024;
        if (wp > static_cast<std::uint64_t> (kPoints))
        {
            float maxAbs = 0.0f;
            for (int i = 0; i < kPoints; ++i)
            {
                const std::uint64_t pos = wp - static_cast<std::uint64_t> (kPoints) + static_cast<std::uint64_t> (i);
                maxAbs = std::max ({ maxAbs, std::fabs (ring.sample (0, 0, pos)), std::fabs (ring.sample (0, 1, pos)) });
            }
            if (maxAbs > 1.0e-4f)
            {
                const float gain = std::min (8.0f, 1.0f / maxAbs);
                g.setColour (skin_.colour (Token::Accent, 0.55f));
                const float dot = std::max (1.5f, skin_.px (2.0f));
                for (int i = 0; i < kPoints; ++i)
                {
                    const std::uint64_t pos = wp - static_cast<std::uint64_t> (kPoints) + static_cast<std::uint64_t> (i);
                    const float l = ring.sample (0, 0, pos) * gain, r = ring.sample (0, 1, pos) * gain;
                    const float x = c.x + (r - l) * 0.7071f * radius, y = c.y - (l + r) * 0.7071f * radius;
                    g.fillRect (x - dot * 0.5f, y - dot * 0.5f, dot, dot);
                }
            }
        }

        // barres à droite : corrélation, équilibre, largeur
        const float x0 = side + skin_.px (30.0f), w = static_cast<float> (getWidth()) - x0;
        bar (g, juce::Rectangle<float> (x0, skin_.px (10.0f), w, skin_.px (92.0f)), tx ("mx.corr"), shown_.correlation, juce::String (shown_.correlation, 2), "-1", "0", "+1");
        bar (g, juce::Rectangle<float> (x0, skin_.px (122.0f), w, skin_.px (92.0f)), tx ("mx.balance"), shown_.balance, juce::String (shown_.balance > 0.005f ? "+" : "") + juce::String (juce::roundToInt (shown_.balance * 100.0f)) + " %", tx ("mx.l"), "", tx ("mx.r"));
        const auto wr = juce::Rectangle<float> (x0, skin_.px (234.0f), w, skin_.px (80.0f));
        skin_.fill (g, Token::StageBg, wr, skin_.px (8.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (tx ("mx.width"), wr.withHeight (skin_.px (22.0f)).translated (0.0f, skin_.px (6.0f)), juce::Justification::centred);
        g.setColour (skin_.colour (Token::Text));
        g.setFont (skin_.font (24.0f, true));
        g.drawText (juce::String (juce::roundToInt (shown_.width * 100.0f)) + " %", wr.withTrimmedTop (skin_.px (28.0f)), juce::Justification::centred);
    }

private:
    juce::String tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    // curseur sur une échelle de -1 à +1
    void bar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, float v, const juce::String& text, const juce::String& left, const juce::String& mid, const juce::String& right) const
    {
        skin_.fill (g, Token::StageBg, r, skin_.px (8.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (title, r.withHeight (skin_.px (20.0f)).translated (0.0f, skin_.px (6.0f)), juce::Justification::centred);
        const auto track = juce::Rectangle<float> (r.getX() + skin_.px (24.0f), r.getY() + skin_.px (44.0f), r.getWidth() - skin_.px (48.0f), skin_.px (6.0f));
        g.setColour (skin_.colour (Token::Grid));
        g.fillRoundedRectangle (track, skin_.px (3.0f));
        g.drawLine (track.getCentreX(), track.getY() - skin_.px (5.0f), track.getCentreX(), track.getBottom() + skin_.px (5.0f), std::max (1.0f, skin_.px (1.0f)));
        const float x = track.getCentreX() + std::clamp (v, -1.0f, 1.0f) * track.getWidth() * 0.5f;
        if (title == tx ("mx.corr")) g.setColour (v < 0.0f ? kRed : v < 0.2f ? kYellow : kGreen);   // opposition de phase : rouge ; faible : jaune ; sinon vert
        else g.setColour (skin_.colour (Token::Accent));
        g.fillEllipse (x - skin_.px (7.0f), track.getCentreY() - skin_.px (7.0f), skin_.px (14.0f), skin_.px (14.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f));
        g.drawText (left, juce::Rectangle<float> (r.getX(), track.getBottom() + skin_.px (6.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        g.drawText (mid, juce::Rectangle<float> (track.getCentreX() - skin_.px (24.0f), track.getBottom() + skin_.px (6.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        g.drawText (right, juce::Rectangle<float> (r.getRight() - skin_.px (48.0f), track.getBottom() + skin_.px (6.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        g.setColour (skin_.colour (Token::Text));
        g.setFont (skin_.font (13.0f, true));
        g.drawText (text, r.withTrimmedTop (r.getHeight() - skin_.px (20.0f)), juce::Justification::centred);
    }

    const nova::Skin& skin_;
    mir::MiradorEngine& engine_;
    const i18n::Translator& tr_;
    mir::Levels shown_ {};
};

// ------------------------------------------------------------------------------------------------ page SPECTRE
class NovaMiradorEditor::SpectrumView : public juce::Component
{
public:
    SpectrumView (const nova::Skin& s, mir::MiradorEngine& e) : skin_ (s), engine_ (e) {}

    void update()
    {
        auto& ring = engine_.ring();
        const double sr = ring.sampleRate();
        if (! prepared_ || std::fabs (sr - preparedRate_) > 0.1)
        {
            analyzer_.prepare (sr, false);
            SpectrumSettings st;
            st.highQuality = false;
            st.speedIdx = 2;
            st.tiltIdx = 3;
            st.smoothType = 1;
            st.smoothErb = 1;
            st.topDb = 0.0f;
            st.rangeDb = -90.0f;
            st.channels = SpectrumChannels::Stereo;
            analyzer_.setSettings (st);
            preparedRate_ = sr;
            prepared_ = true;
            readPos_ = ring.writePos();
        }
        analyzer_.pull (0, ring, 0, readPos_);
        analyzer_.analyze (0, 30.0);
        freqs_ = analyzer_.frequencies();
        db_ = analyzer_.spectrumDb (0);
        rebuild();
        repaint();
    }
    void resized() override { rebuild(); }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (8.0f));
        const auto plot = plotArea();
        g.setFont (skin_.font (10.0f));
        for (const double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xOfHz (static_cast<float> (f));
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), std::max (1.0f, skin_.px (1.0f)));
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (f >= 1000.0 ? juce::String (static_cast<int> (f / 1000.0)) + "k" : juce::String (static_cast<int> (f)),
                        juce::Rectangle<float> (x - skin_.px (20.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (40.0f), skin_.px (14.0f)), juce::Justification::centred);
        }
        for (int db = -90; db <= 0; db += 10)
        {
            const float y = yOfDb (static_cast<float> (db));
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawLine (plot.getX(), y, plot.getRight(), y, std::max (1.0f, skin_.px (1.0f)));
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - skin_.px (36.0f), y - skin_.px (7.0f), skin_.px (32.0f), skin_.px (14.0f)), juce::Justification::centredRight);
        }
        g.setColour (skin_.colour (Token::Accent, 0.28f));
        g.fillPath (path_);
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (path_, juce::PathStrokeType (std::max (1.0f, skin_.px (1.5f))));
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (skin_.px (44.0f)).withTrimmedRight (skin_.px (14.0f)).withTrimmedTop (skin_.px (12.0f)).withTrimmedBottom (skin_.px (24.0f));
    }
    float xOfHz (float hz) const
    {
        const auto plot = plotArea();
        return plot.getX() + static_cast<float> (std::log10 (std::max (hz, 20.0f) / 20.0f) / 3.0) * plot.getWidth();
    }
    float yOfDb (float db) const
    {
        const auto plot = plotArea();
        return plot.getBottom() - std::clamp ((db + 90.0f) / 90.0f, 0.0f, 1.0f) * plot.getHeight();
    }
    void rebuild()
    {
        path_.clear();
        if (db_.size() != freqs_.size() || freqs_.size() < 4) return;
        const auto plot = plotArea();
        bool started = false;
        float lastX = plot.getX();
        for (std::size_t i = 0; i < freqs_.size(); ++i)
        {
            const float x = xOfHz (i == 0 ? freqs_[1] * 0.5f : freqs_[i]);
            if (x > plot.getRight()) break;
            const float y = yOfDb (db_[i]);
            if (! started) { path_.startNewSubPath (x, plot.getBottom()); path_.lineTo (x, y); started = true; }
            else path_.lineTo (x, y);
            lastX = x;
        }
        if (started)
        {
            path_.lineTo (lastX, plot.getBottom());
            path_.closeSubPath();
        }
    }

    const nova::Skin& skin_;
    mir::MiradorEngine& engine_;
    SpectrumAnalyzer analyzer_;
    std::vector<float> freqs_, db_;
    juce::Path path_;
    std::uint64_t readPos_ = 0;
    double preparedRate_ = 0.0;
    bool prepared_ = false;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaMiradorEditor::NovaMiradorEditor (ParamHost& host, mir::MiradorEngine& engine, const theme::Palette& palette, theme::KnobStyle knob,
                                      const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.mirador")).c_str()).toUpperCase()),
      engine_ (engine)
{
    addPage (tx ("mx.page_loud"), iconPath (0));
    addPage (tx ("mx.page_levels"), iconPath (1));
    addPage (tx ("mx.page_stereo"), iconPath (2));
    addPage (tx ("mx.page_spectrum"), iconPath (3));
    addPage (tx ("nv.page_settings"), iconPath (4));
    buildCommon();
    buildLoudPage();
    buildLevelsPage();
    buildStereoPage();
    buildSpectrumPage();
    buildSettingsPage (4);
    engine_.ring().setActive (true);   // le signal n'est copié pour l'interface que tant que la fenêtre est ouverte
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaMiradorEditor::~NovaMiradorEditor() { engine_.ring().setActive (false); }

void NovaMiradorEditor::openLegendForTest() { loud_->openLegend(); }

void NovaMiradorEditor::buildCommon()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    peakMeter_ = add<nova::VMeter> (-1, skin(), tx ("mx.peak_unit"), nova::VMeter::Kind::Level, false);
    peakMeter_->setModeVisible (false);
    peakMeter_->setZones (true);
    resetPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    resetPill_->setText (tx ("mx.reset_all"));
    resetPill_->onClick = [this]
    {
        engine_.resetAll();
        peakMeter_->resetPeak();
        loud_->clearHistory();
    };
}

void NovaMiradorEditor::buildLoudPage()
{
    loudPanel_ = add<nova::Panel> (0, skin());
    loudPanel_->setTitle (tx ("mx.page_loud"));
    loud_ = add<LoudView> (0, skin(), host(), engine_, translator());
    targetPill_ = add<nova::Pill> (0, skin());
    targetPill_->bindChoice (host(), mir::target, tx ("mx.target"), { tx ("mx.tgt0"), tx ("mx.tgt1"), tx ("mx.tgt2"), tx ("mx.tgt3"), tx ("mx.tgt4") });
    customPill_ = add<nova::Pill> (0, skin());
    customPill_->bindValue (host(), mir::customTarget, tx ("mx.custom"));
    tolerancePill_ = add<nova::Pill> (0, skin());
    tolerancePill_->bindValue (host(), mir::tolerance, tx ("mx.tolerance"));
    historyPill_ = add<nova::Pill> (0, skin());
    historyPill_->bindValue (host(), mir::historyMinutes, tx ("mx.hist_len"));
    resetPlayPill_ = add<nova::Pill> (0, skin());
    resetPlayPill_->bindToggle (host(), mir::autoReset, tx ("mx.reset_play"));
    meterPill_ = add<nova::Pill> (0, skin());
    meterPill_->bindChoice (host(), mir::meterMode, tx ("mx.meter"), { tx ("mx.mm0"), tx ("mx.mm1"), tx ("mx.mm2"), tx ("mx.mm3") });
    resetLoudPill_ = add<nova::Pill> (0, skin());
    resetLoudPill_->setText (tx ("mx.reset_int"));
    resetLoudPill_->onClick = [this] { engine_.resetLoudness(); loud_->clearHistory(); };
    for (auto* p : { customPill_, tolerancePill_, historyPill_ })
    {
        const ParamId id = p == customPill_ ? mir::customTarget : p == tolerancePill_ ? mir::tolerance : mir::historyMinutes;
        p->onClick = [this, id] { openModal (id, uistate::Terminal::Pointer); };
    }
}

void NovaMiradorEditor::buildLevelsPage()
{
    levelsPanel_ = add<nova::Panel> (1, skin());
    levelsPanel_->setTitle (tx ("mx.page_levels") + " (dBFS)");
    levels_ = add<LevelsView> (1, skin(), engine_, translator());
    holdPill_ = add<nova::Pill> (1, skin());
    holdPill_->bindValue (host(), mir::peakHold, tx ("mx.hold"));
    fallPill_ = add<nova::Pill> (1, skin());
    fallPill_->bindValue (host(), mir::peakFall, tx ("mx.fall"));
    rmsPill_ = add<nova::Pill> (1, skin());
    rmsPill_->bindChoice (host(), mir::rmsWindow, tx ("mx.rms_win"), { "300 ms", "1 s", "3 s" });
    resetPeaksPill_ = add<nova::Pill> (1, skin());
    resetPeaksPill_->setText (tx ("mx.reset_peaks"));
    resetPeaksPill_->onClick = [this] { engine_.resetLevels(); peakMeter_->resetPeak(); };
    for (auto* p : { holdPill_, fallPill_ })
    {
        const ParamId id = p == holdPill_ ? mir::peakHold : mir::peakFall;
        p->onClick = [this, id] { openModal (id, uistate::Terminal::Pointer); };
    }
}

void NovaMiradorEditor::buildStereoPage()
{
    stereoPanel_ = add<nova::Panel> (2, skin());
    stereoPanel_->setTitle (tx ("mx.scope"));
    stereo_ = add<StereoView> (2, skin(), engine_, translator());
}

void NovaMiradorEditor::buildSpectrumPage()
{
    spectrumPanel_ = add<nova::Panel> (3, skin());
    spectrumPanel_->setTitle (tx ("mx.page_spectrum") + " (dBFS / Hz)");
    spectrum_ = add<SpectrumView> (3, skin(), engine_);
}

void NovaMiradorEditor::layout()
{
    // ---- colonne de gauche (toutes les pages)
    metersPanel_->setBounds (R (14, 72, 176, 456));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        peakMeter_->setBounds (in.withTrimmedBottom (skin().ipx (40.0f)));
        resetPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), in.getWidth() - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 542, 176, 168));
    layoutNav (542, 168);

    // ---- cadres de page
    for (auto* panel : { loudPanel_, levelsPanel_, stereoPanel_, spectrumPanel_ }) panel->setBounds (Rs (204, 72, 942, 638));

    // SONIE
    loud_->setBounds (Rs (220, 108, 910, 480));
    targetPill_->setBounds (R (220, 604, 210, 32));
    customPill_->setBounds (R (438, 604, 130, 32));
    tolerancePill_->setBounds (R (576, 604, 150, 32));
    historyPill_->setBounds (R (734, 604, 150, 32));
    resetPlayPill_->setBounds (Rr (892, 604, 238, 32));
    resetLoudPill_->setBounds (R (220, 650, 150, 32));
    meterPill_->setBounds (Rr (989, 548, 130, 28));   // sous le mètre de la page SONIE (centre du mètre : 220 + 760 + 54 + 20 = 1054)

    // NIVEAUX
    levels_->setBounds (Rs (220, 108, 910, 380));
    holdPill_->setBounds (R (220, 604, 170, 32));
    fallPill_->setBounds (R (398, 604, 170, 32));
    rmsPill_->setBounds (R (576, 604, 210, 32));
    resetPeaksPill_->setBounds (R (794, 604, 150, 32));

    // STÉRÉO et SPECTRE
    stereo_->setBounds (Rs (220, 108, 910, 500));
    spectrum_->setBounds (Rs (220, 108, 910, 580));

    // ---- en-tête
    if (bypassPill() != nullptr) bypassPill()->setBounds (Rr (1160 - 14 - 36, 11, 36, 36));
}

void NovaMiradorEditor::pageChanged() {}

void NovaMiradorEditor::tick()
{
    const auto lv = engine_.levels();
    peakMeter_->update (std::max (lv.peakL, -120.0f), std::max (lv.peakR, -120.0f));
    switch (currentPage())
    {
        case 0: loud_->update(); break;
        case 1: levels_->update(); break;
        case 2: stereo_->update(); break;
        case 3: spectrum_->update(); break;
        default: break;
    }
}
