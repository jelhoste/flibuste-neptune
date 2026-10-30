#include "NovaDiapasonEditor.h"

#include <algorithm>
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace gen = neptune::core::generator;

namespace
{
constexpr float kPi = 3.14159265358979f;
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // onde
    {
        p.startNewSubPath (2.0f, 12.0f);
        for (int i = 1; i <= 20; ++i) p.lineTo (2.0f + i, 12.0f - 7.0f * std::sin (static_cast<float> (i) * kPi / 5.0f));
    }
    else if (kind == 1)   // grille de pas
    {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) p.addRectangle (3.0f + static_cast<float> (c) * 5.0f, 5.0f + static_cast<float> (r) * 6.0f, 3.5f, 3.5f);
    }
    else
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    return p;
}
} // namespace

// ------------------------------------------------------------------------------------------------ rangée de pastilles à choix unique
class NovaDiapasonEditor::ChipGrid : public juce::Component, public nova::Refreshable
{
public:
    ChipGrid (const nova::Skin& s, ParamHost& h, ParamId id, std::vector<juce::String> labels, int cols)
        : skin_ (s), host_ (h), id_ (id), labels_ (std::move (labels)), cols_ (std::max (1, cols)) {}

    void refresh() override
    {
        const auto v = host_.parameters().version (id_);
        if (v == seen_) return;
        seen_ = v;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const int sel = static_cast<int> (std::lround (host_.parameters().get (id_)));
        for (int i = 0; i < static_cast<int> (labels_.size()); ++i)
        {
            const auto r = chip (i);
            const bool on = i == sel;
            if (on) skin_.fillActive (g, r, skin_.px (8.0f));
            else skin_.fill (g, Token::StageBg, r, skin_.px (8.0f));
            g.setColour (skin_.colour (on ? Token::Accent : Token::Border));
            g.drawRoundedRectangle (r.reduced (skin_.px (0.5f)), skin_.px (8.0f), std::max (1.0f, skin_.borderWidth()));
            g.setColour (skin_.colour (on ? Token::Text : Token::TextDim));
            g.setFont (skin_.font (12.0f, on));
            g.drawText (labels_[static_cast<std::size_t> (i)], r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < static_cast<int> (labels_.size()); ++i)
            if (chip (i).contains (e.position)) { nova::setParam (host_, id_, i); repaint(); return; }
    }

private:
    juce::Rectangle<float> chip (int i) const
    {
        const int rows = (static_cast<int> (labels_.size()) + cols_ - 1) / cols_;
        const float gap = skin_.px (6.0f), w = (static_cast<float> (getWidth()) - gap * static_cast<float> (cols_ - 1)) / static_cast<float> (cols_);
        const float h = (static_cast<float> (getHeight()) - gap * static_cast<float> (rows - 1)) / static_cast<float> (rows);
        return { static_cast<float> (i % cols_) * (w + gap), static_cast<float> (i / cols_) * (h + gap), w, h };
    }
    const nova::Skin& skin_;
    ParamHost& host_;
    ParamId id_;
    std::vector<juce::String> labels_;
    int cols_;
    std::uint32_t seen_ = 0xFFFFFFFFu;
};

// ------------------------------------------------------------------------------------------------ aperçu du signal choisi
class NovaDiapasonEditor::SignalView : public juce::Component
{
public:
    SignalView (const nova::Skin& s, ParamHost& h) : skin_ (s), host_ (h) {}

    void update()
    {
        std::uint32_t key = 0;
        for (const ParamId id : { gen::signal, gen::freq, gen::level, gen::width, gen::freq2, gen::level2, gen::sweepStart, gen::sweepEnd, gen::sweepTime })
            key = key * 31u + host_.parameters().version (id);
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        const auto plot = getLocalBounds().toFloat().withTrimmedLeft (skin_.px (40.0f)).withTrimmedRight (skin_.px (14.0f)).withTrimmedTop (skin_.px (28.0f)).withTrimmedBottom (skin_.px (22.0f));
        const int sig = static_cast<int> (std::lround (get (gen::signal)));
        const auto stroke = juce::PathStrokeType (std::max (1.5f, skin_.px (2.0f)), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setFont (skin_.font (11.0f, true));
        g.setColour (skin_.colour (Token::TextDim));

        auto line = [&] (float x0, float y0, float x1, float y1, Token t, float a = 1.0f)
        {
            g.setColour (skin_.colour (t, a));
            g.drawLine (x0, y0, x1, y1, std::max (1.0f, skin_.px (1.0f)));
        };
        auto label = [&] (const juce::String& s, juce::Rectangle<float> r, juce::Justification j, Token t = Token::TextDim2)
        {
            g.setColour (skin_.colour (t));
            g.setFont (skin_.font (10.0f));
            g.drawText (s, r, j);
        };

        const bool noise = sig >= gen::kWhite && sig <= gen::kViolet;
        const bool sweep = sig == gen::kSweepLog || sig == gen::kSweepLin;
        const auto& ps = host_.parameters();
        if (noise)
        {
            // spectre : pente en dB par octave, centrée sur 1 kHz ; échelle des fréquences logarithmique de 20 Hz à 20 kHz
            const double slope = sig == gen::kPink ? -3.0 : sig == gen::kBrown ? -6.0 : sig == gen::kBlue ? 3.0 : sig == gen::kViolet ? 6.0 : 0.0;
            auto xOf = [&] (double f) { return plot.getX() + static_cast<float> (std::log10 (f / 20.0) / 3.0) * plot.getWidth(); };
            auto yOf = [&] (double db) { return plot.getCentreY() - static_cast<float> (std::clamp (db, -40.0, 40.0) / 40.0) * plot.getHeight() * 0.5f; };
            for (const double f : { 100.0, 1000.0, 10000.0 })
            {
                line (xOf (f), plot.getY(), xOf (f), plot.getBottom(), Token::Grid, 0.6f);
                label (f >= 1000.0 ? juce::String (static_cast<int> (f / 1000.0)) + " kHz" : juce::String (static_cast<int> (f)) + " Hz",
                       { xOf (f) - skin_.px (30.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (60.0f), skin_.px (12.0f) }, juce::Justification::centred);
            }
            line (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), Token::Grid);
            juce::Path path;
            for (int i = 0; i <= 200; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 200.0);
                const float x = xOf (f), y = yOf (slope * std::log2 (f / 1000.0));
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (skin_.colour (Token::Accent));
            g.strokePath (path, stroke);
            label ((slope > 0.0 ? "+" : "") + juce::String (slope, 0) + " dB/oct", { plot.getX(), getLocalBounds().toFloat().getY() + skin_.px (8.0f), plot.getWidth(), skin_.px (14.0f) }, juce::Justification::centredLeft, Token::Text);
        }
        else if (sweep)
        {
            const double f0 = get (gen::sweepStart), f1 = get (gen::sweepEnd), t = get (gen::sweepTime);
            const double lo = std::log10 (10.0), hi = std::log10 (24000.0);
            auto yOf = [&] (double f) { return plot.getBottom() - static_cast<float> ((std::log10 (std::max (f, 10.0)) - lo) / (hi - lo)) * plot.getHeight(); };
            for (const double f : { 100.0, 1000.0, 10000.0 })
            {
                line (plot.getX(), yOf (f), plot.getRight(), yOf (f), Token::Grid, 0.6f);
                label (f >= 1000.0 ? juce::String (static_cast<int> (f / 1000.0)) + " kHz" : juce::String (static_cast<int> (f)) + " Hz",
                       { getLocalBounds().toFloat().getX() + skin_.px (2.0f), yOf (f) - skin_.px (6.0f), skin_.px (38.0f), skin_.px (12.0f) }, juce::Justification::centredRight);
            }
            juce::Path path;
            for (int i = 0; i <= 200; ++i)
            {
                const double u = i / 200.0;
                const double f = sig == gen::kSweepLog ? f0 * std::pow (f1 / f0, u) : f0 + (f1 - f0) * u;
                const float x = plot.getX() + static_cast<float> (u) * plot.getWidth(), y = yOf (f);
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (skin_.colour (Token::Accent));
            g.strokePath (path, stroke);
            label ("0", { plot.getX() - skin_.px (10.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (20.0f), skin_.px (12.0f) }, juce::Justification::centred);
            label (juce::String (t, 1) + " s", { plot.getRight() - skin_.px (30.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (60.0f), skin_.px (12.0f) }, juce::Justification::centred);
            label (ps.toText (gen::sweepStart) + "  >  " + ps.toText (gen::sweepEnd), { plot.getX(), getLocalBounds().toFloat().getY() + skin_.px (8.0f), plot.getWidth(), skin_.px (14.0f) }, juce::Justification::centredLeft, Token::Text);
        }
        else
        {
            const double pw = get (gen::width) * 0.01, ratio = std::clamp (get (gen::freq2) / std::max (1.0, get (gen::freq)), 0.1, 40.0);
            const double a2 = std::pow (10.0, get (gen::level2) / 20.0);
            constexpr int kCycles = 3;
            line (plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), Token::Grid);
            const float half = plot.getHeight() * 0.5f * 0.9f;
            juce::Path path;
            const int points = 600;
            for (int i = 0; i <= points; ++i)
            {
                const double u = static_cast<double> (i) / points * kCycles;
                const double ph = u - std::floor (u);
                double y = 0.0;
                switch (sig)
                {
                    case gen::kSine: y = std::sin (2.0 * kPi * ph); break;
                    case gen::kSquare: y = ph < pw ? 1.0 : -1.0; break;
                    case gen::kTriangle: y = ph < 0.25 ? 4.0 * ph : ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0; break;
                    case gen::kSawUp: y = 2.0 * ph - 1.0; break;
                    case gen::kSawDown: y = 1.0 - 2.0 * ph; break;
                    case gen::kTwoTone: y = (std::sin (2.0 * kPi * ph) + a2 * std::sin (2.0 * kPi * u * ratio)) / (1.0 + a2); break;
                    case gen::kDc: y = 1.0; break;
                    default: y = 0.0; break;
                }
                const float x = plot.getX() + static_cast<float> (i) / points * plot.getWidth(), yy = plot.getCentreY() - static_cast<float> (y) * half;
                if (sig == gen::kImpulse) continue;
                if (i == 0) path.startNewSubPath (x, yy); else path.lineTo (x, yy);
            }
            g.setColour (skin_.colour (Token::Accent));
            if (sig == gen::kImpulse)
                for (int c = 0; c < kCycles; ++c)
                {
                    const float x = plot.getX() + (static_cast<float> (c) + 0.02f) / kCycles * plot.getWidth();
                    g.fillRoundedRectangle (x - skin_.px (1.5f), plot.getCentreY() - half, skin_.px (3.0f), half, skin_.px (1.0f));
                }
            else g.strokePath (path, stroke);
            const juce::String name = juce::String::fromUTF8 (gen::signalName (sig));
            label (ps.toText (gen::freq) + "   " + ps.toText (gen::level), { plot.getX(), getLocalBounds().toFloat().getY() + skin_.px (8.0f), plot.getWidth(), skin_.px (14.0f) }, juce::Justification::centredLeft, Token::Text);
            (void) name;
        }
    }

private:
    double get (ParamId id) const { return host_.parameters().get (id); }
    const nova::Skin& skin_;
    ParamHost& host_;
    std::uint32_t key_ = 0xFFFFFFFFu;
};

// ------------------------------------------------------------------------------------------------ grille de pas
class NovaDiapasonEditor::StepGrid : public juce::Component, public nova::Refreshable
{
public:
    StepGrid (const nova::Skin& s, ParamHost& h, gen::GeneratorEngine& e, std::vector<juce::String> names) : skin_ (s), host_ (h), engine_ (e), names_ (std::move (names)) {}

    std::function<void (int)> onSelect;
    void setSelected (int d) { selected_ = d; repaint(); }

    void refresh() override
    {
        std::uint32_t key = static_cast<std::uint32_t> (engine_.currentStep() + 2) * 2654435761u;
        for (int d = 0; d < gen::kNumDrums; ++d)
        {
            key = key * 31u + host_.parameters().version (gen::drumParam (d, gen::kDrumMute));
            for (int st = 0; st < gen::kMaxSteps; ++st) key = key * 31u + host_.parameters().version (gen::stepParam (d, st));
        }
        key = key * 31u + host_.parameters().version (gen::steps);
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int nsteps = steps();
        const int cur = engine_.currentStep();
        for (int d = 0; d < gen::kNumDrums; ++d)
        {
            const auto lab = labelRect (d);
            const bool sel = d == selected_;
            const bool muted = host_.parameters().get (gen::drumParam (d, gen::kDrumMute)) >= 0.5;
            if (sel) skin_.fillActive (g, lab, skin_.px (8.0f));
            else skin_.fill (g, Token::StageBg, lab, skin_.px (8.0f));
            g.setColour (skin_.colour (muted ? Token::TextDim2 : sel ? Token::Text : Token::TextDim));
            g.setFont (skin_.font (12.0f, sel));
            g.drawText (names_[static_cast<std::size_t> (d)], lab.withTrimmedLeft (skin_.px (10.0f)).withTrimmedRight (skin_.px (34.0f)), juce::Justification::centredLeft);
            const auto m = muteRect (d);
            g.setColour (skin_.colour (muted ? Token::Pink : Token::Border));
            g.drawRoundedRectangle (m, skin_.px (5.0f), std::max (1.0f, skin_.borderWidth()));
            g.setFont (skin_.font (10.0f, true));
            g.drawText ("M", m, juce::Justification::centred);
            for (int st = 0; st < gen::kMaxSteps; ++st)
            {
                const auto c = cell (d, st);
                const bool inRange = st < nsteps;
                const int v = static_cast<int> (std::lround (host_.parameters().get (gen::stepParam (d, st))));
                if (v == 2) { skin_.fillActive (g, c, skin_.px (6.0f)); g.setColour (skin_.colour (Token::Pink)); g.fillRoundedRectangle (c.reduced (skin_.px (4.0f)), skin_.px (4.0f)); }
                else if (v == 1) { g.setColour (skin_.colour (Token::Accent, inRange ? 0.85f : 0.3f)); g.fillRoundedRectangle (c.reduced (skin_.px (4.0f), skin_.px (7.0f)), skin_.px (4.0f)); }
                g.setColour (skin_.colour (st == cur && inRange ? Token::Accent : Token::Border, inRange ? (st == cur ? 1.0f : 0.8f) : 0.3f));
                g.drawRoundedRectangle (c, skin_.px (6.0f), std::max (1.0f, skin_.borderWidth()));
                if (! inRange) { g.setColour (skin_.colour (Token::StageBg, 0.5f)); g.fillRoundedRectangle (c, skin_.px (6.0f)); }
            }
        }
        // repères tous les quatre pas
        for (int st = 4; st < nsteps; st += 4)
        {
            const float x = cell (0, st).getX() - gap() * 0.5f;
            g.setColour (skin_.colour (Token::Grid, 0.8f));
            g.fillRect (x - 0.5f, 0.0f, std::max (1.0f, skin_.px (1.0f)), static_cast<float> (getHeight()));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag_ = false;
        for (int d = 0; d < gen::kNumDrums; ++d)
        {
            if (muteRect (d).contains (e.position))
            {
                const ParamId id = gen::drumParam (d, gen::kDrumMute);
                nova::setParam (host_, id, host_.parameters().get (id) >= 0.5 ? 0.0 : 1.0);
                repaint();
                return;
            }
            if (labelRect (d).contains (e.position)) { selected_ = d; if (onSelect) onSelect (d); repaint(); return; }
        }
        const auto hit = cellAt (e.position);
        if (hit.first < 0) return;
        const ParamId id = gen::stepParam (hit.first, hit.second);
        const int now = static_cast<int> (std::lround (host_.parameters().get (id)));
        paintValue_ = e.mods.isPopupMenu() ? 0 : (now + 1) % 3;
        drag_ = true;
        last_ = hit;
        nova::setParam (host_, id, paintValue_);
        selected_ = hit.first;
        if (onSelect) onSelect (hit.first);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag_) return;
        const auto hit = cellAt (e.position);
        if (hit.first < 0 || hit == last_) return;
        last_ = hit;
        nova::setParam (host_, gen::stepParam (hit.first, hit.second), paintValue_);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag_ = false; }

private:
    int steps() const { return std::clamp (static_cast<int> (std::lround (host_.parameters().get (gen::steps))), 4, gen::kMaxSteps); }
    float gap() const { return skin_.px (4.0f); }
    float labelW() const { return skin_.px (150.0f); }
    float rowH() const { return static_cast<float> (getHeight()) / gen::kNumDrums; }
    juce::Rectangle<float> labelRect (int d) const { return { 0.0f, static_cast<float> (d) * rowH() + gap() * 0.5f, labelW() - gap(), rowH() - gap() }; }
    juce::Rectangle<float> muteRect (int d) const
    {
        const auto l = labelRect (d);
        return { l.getRight() - skin_.px (28.0f), l.getCentreY() - skin_.px (10.0f), skin_.px (22.0f), skin_.px (20.0f) };
    }
    juce::Rectangle<float> cell (int d, int st) const
    {
        const float w = (static_cast<float> (getWidth()) - labelW()) / gen::kMaxSteps;
        return { labelW() + static_cast<float> (st) * w + gap() * 0.5f, static_cast<float> (d) * rowH() + gap() * 0.5f, w - gap(), rowH() - gap() };
    }
    std::pair<int, int> cellAt (juce::Point<float> p) const
    {
        for (int d = 0; d < gen::kNumDrums; ++d)
            for (int st = 0; st < steps(); ++st)
                if (cell (d, st).expanded (gap() * 0.5f).contains (p)) return { d, st };
        return { -1, -1 };
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    gen::GeneratorEngine& engine_;
    std::vector<juce::String> names_;
    int selected_ = 0, paintValue_ = 0;
    bool drag_ = false;
    std::pair<int, int> last_ { -1, -1 };
    std::uint32_t key_ = 0xFFFFFFFFu;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaDiapasonEditor::NovaDiapasonEditor (ParamHost& host, gen::GeneratorEngine& engine, const theme::Palette& palette, theme::KnobStyle knob,
                                        const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.diapason")).c_str()).toUpperCase()),
      engine_ (engine)
{
    addPage (tx ("dp.page_signal"), iconPath (0));
    addPage (tx ("dp.page_drums"), iconPath (1));
    addPage (tx ("nv.page_settings"), iconPath (2));
    buildSignalPage();
    buildDrumsPage();
    buildSettingsPage (2);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaDiapasonEditor::~NovaDiapasonEditor() = default;

void NovaDiapasonEditor::buildSignalPage()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    outMeter_->setModeVisible (false);
    panicPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    panicPill_->setText (tx ("dp.panic"));
    panicPill_->onClick = [this] { engine_.postPanic(); };

    signalPanel_ = add<nova::Panel> (0, skin());
    signalPanel_->setTitle (tx ("dp.signal_title"));
    std::vector<juce::String> sigs, modes;
    for (int i = 0; i < gen::kNumSignals; ++i) sigs.push_back (tx (("dp.sig" + std::to_string (i)).c_str()));
    for (int i = 0; i < gen::kNumModes; ++i) modes.push_back (tx (("dp.mode" + std::to_string (i)).c_str()));
    signalChips_ = add<ChipGrid> (0, skin(), host(), gen::signal, sigs, 5);
    modeChips_ = add<ChipGrid> (0, skin(), host(), gen::mode, modes, 1);
    view_ = add<SignalView> (0, skin(), host());
    onPill_ = add<nova::Pill> (0, skin());
    onPill_->bindToggle (host(), gen::on, tx ("dp.on"));
    triggerPill_ = add<nova::Pill> (0, skin());
    triggerPill_->setText (tx ("dp.trigger"));
    triggerPill_->onClick = [this] { engine_.postTrigger(); };
    polarityPill_ = add<nova::Pill> (0, skin());
    polarityPill_->bindChoice (host(), gen::polarity, tx ("dp.polarity"), { tx ("dp.pol0"), tx ("dp.pol1"), tx ("dp.pol2"), tx ("dp.pol3") });
    signalHint_ = add<nova::Text> (0, skin(), Token::TextDim2, 11.0f);
    signalHint_->setText (tx ("dp.hint_signal"));

    for (int i = 0; i < 8; ++i)
    {
        Column c {};
        c.card = add<nova::Panel> (0, skin());
        c.knob = add<nova::KnobCard> (0, skin(), host(), gen::level, tx ("dp.level"), modalOpener(), i == 7);
        c.knob->setPlain (true);
        columns_.push_back (c);
    }
}

void NovaDiapasonEditor::assignKnobs()
{
    const int sig = choiceValue (gen::signal), md = choiceValue (gen::mode);
    const bool noise = sig >= gen::kWhite && sig <= gen::kViolet, sweep = sig == gen::kSweepLog || sig == gen::kSweepLin, dc = sig == gen::kDc;
    const bool two = sig == gen::kTwoTone, square = sig == gen::kSquare;
    struct Slot { ParamId id; const char* title; bool shown; };
    const bool periodic = ! noise && ! sweep && ! dc;
    const Slot slots[8] = {
        sweep ? Slot { gen::sweepStart, "dp.sw_start", true } : Slot { gen::freq, "dp.freq", periodic },
        { gen::level, "dp.level", true },
        { gen::pan, "dp.pan", true },
        square ? Slot { gen::width, "dp.width", true } : two ? Slot { gen::freq2, "dp.freq2", true } : Slot { gen::sweepEnd, "dp.sw_end", sweep },
        two ? Slot { gen::level2, "dp.level2", true } : Slot { gen::sweepTime, "dp.sw_time", sweep },
        md == gen::kContinuous ? Slot { gen::burstOn, "dp.burst_on", true } : Slot { gen::length, "dp.length", md == gen::kTriggered && ! sweep },
        { gen::burstOff, "dp.burst_off", md == gen::kContinuous },
        { gen::master, "dp.master", true } };
    columnPos_.assign (columns_.size(), -1);
    int rank = 0;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        columns_[i].knob->setParam (slots[i].id, tx (slots[i].title));
        const bool show = slots[i].shown && currentPage() == 0;
        columns_[i].card->setVisible (show);
        columns_[i].knob->setVisible (show);
        if (slots[i].shown) columnPos_[i] = rank++;
    }
    placeColumns();
    triggerPill_->setDim (md != gen::kTriggered);
    onPill_->setDim (md != gen::kContinuous);
}

void NovaDiapasonEditor::buildDrumsPage()
{
    drumsPanel_ = add<nova::Panel> (1, skin());
    drumsPanel_->setTitle (tx ("dp.drums_title"));
    runPill_ = add<nova::Pill> (1, skin());
    runPill_->bindToggle (host(), gen::seqRun, tx ("dp.run"));
    syncPill_ = add<nova::Pill> (1, skin());
    syncPill_->bindChoice (host(), gen::seqSync, tx ("dp.sync"), { tx ("dp.sync_int"), tx ("dp.sync_host") });
    tempoPill_ = add<nova::Pill> (1, skin());
    tempoPill_->bindValue (host(), gen::tempo, tx ("dp.tempo"));
    stepsPill_ = add<nova::Pill> (1, skin());
    stepsPill_->bindValue (host(), gen::steps, tx ("dp.steps"));
    swingPill_ = add<nova::Pill> (1, skin());
    swingPill_->bindValue (host(), gen::swing, tx ("dp.swing"));
    drumsLevelPill_ = add<nova::Pill> (1, skin());
    drumsLevelPill_->bindValue (host(), gen::drumLevel, tx ("dp.drums_level"));
    notePill_ = add<nova::Pill> (1, skin());
    notePill_->bindValue (host(), gen::drumNote, tx ("dp.drum_note"));
    masterPill_ = add<nova::Pill> (1, skin());   // le même réglage que le potard MASTER de la page SIGNAL
    masterPill_->bindValue (host(), gen::master, tx ("dp.master"));
    for (auto* p : { tempoPill_, stepsPill_, swingPill_, drumsLevelPill_, notePill_, masterPill_ })
    {
        const ParamId id = p == tempoPill_ ? gen::tempo : p == stepsPill_ ? gen::steps : p == swingPill_ ? gen::swing : p == drumsLevelPill_ ? gen::drumLevel : p == masterPill_ ? gen::master : gen::drumNote;
        p->onClick = [this, id] { openModal (id, uistate::Terminal::Pointer); };
    }
    demoPill_ = add<nova::Pill> (1, skin());
    demoPill_->setText (tx ("dp.demo"));
    demoPill_->onClick = [this] { demoBeat(); };
    clearPill_ = add<nova::Pill> (1, skin());
    clearPill_->setText (tx ("dp.clear"));
    clearPill_->onClick = [this] { clearPattern(); };
    drumsHint_ = add<nova::Text> (1, skin(), Token::TextDim2, 11.0f);
    drumsHint_->setText (tx ("dp.hint_drums"));

    std::vector<juce::String> names;
    for (int i = 0; i < gen::kNumDrums; ++i) names.push_back (tx (("dp.drum" + std::to_string (i)).c_str()));
    grid_ = add<StepGrid> (1, skin(), host(), engine_, names);
    grid_->onSelect = [this] (int d) { selectDrum (d); };

    drumCard_ = add<nova::Panel> (1, skin());
    struct K { int kind; const char* title; };
    for (const K k : { K { gen::kDrumLevel, "dp.drum_level" }, K { gen::kDrumTune, "dp.tune" }, K { gen::kDrumDecay, "dp.decay" }, K { gen::kDrumPan, "dp.drum_pan" } })
    {
        auto* kc = add<nova::KnobCard> (1, skin(), host(), gen::drumParam (0, k.kind), tx (k.title), modalOpener(), k.kind == gen::kDrumPan);
        kc->setPlain (true);
        drumKnobs_.push_back (kc);
    }
    mutePill_ = add<nova::Pill> (1, skin());
    mutePill_->bindToggle (host(), gen::drumParam (0, gen::kDrumMute), tx ("dp.mute"));
    playPill_ = add<nova::Pill> (1, skin());
    playPill_->setText (tx ("dp.play"));
    playPill_->onClick = [this] { engine_.postDrum (selectedDrum_); };
    selectDrum (0);
}

void NovaDiapasonEditor::selectDrum (int d)
{
    selectedDrum_ = std::clamp (d, 0, gen::kNumDrums - 1);
    if (grid_ != nullptr) grid_->setSelected (selectedDrum_);
    int k = 0;
    for (const int kind : { static_cast<int> (gen::kDrumLevel), static_cast<int> (gen::kDrumTune), static_cast<int> (gen::kDrumDecay), static_cast<int> (gen::kDrumPan) })
    {
        const char* titles[4] = { "dp.drum_level", "dp.tune", "dp.decay", "dp.drum_pan" };
        drumKnobs_[static_cast<std::size_t> (k)]->setParam (gen::drumParam (selectedDrum_, kind), tx (titles[k]));
        ++k;
    }
    mutePill_->bindToggle (host(), gen::drumParam (selectedDrum_, gen::kDrumMute), tx ("dp.mute"));
    drumCard_->setTitle (tx (("dp.drum" + std::to_string (selectedDrum_)).c_str()).toUpperCase());
    repaint();
}

void NovaDiapasonEditor::clearPattern()
{
    for (int d = 0; d < gen::kNumDrums; ++d)
        for (int s = 0; s < gen::kMaxSteps; ++s)
            if (value (gen::stepParam (d, s)) > 0.5) nova::setParam (host(), gen::stepParam (d, s), 0.0);
}

void NovaDiapasonEditor::demoBeat()
{
    clearPattern();
    nova::setParam (host(), gen::steps, 16.0);
    auto put = [this] (int d, int s, int v) { nova::setParam (host(), gen::stepParam (d, s), v); };
    for (const int s : { 0, 8, 10 }) put (gen::kKick, s, s == 0 ? 2 : 1);
    for (const int s : { 4, 12 }) put (gen::kSnare, s, 2);
    for (int s = 0; s < 16; s += 2) put (s == 14 ? gen::kHatOpen : gen::kHatClosed, s, s % 4 == 0 ? 2 : 1);
}

void NovaDiapasonEditor::layout()
{
    // ---- colonne de gauche (toutes les pages)
    metersPanel_->setBounds (R (14, 72, 176, 456));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        outMeter_->setBounds (in.withTrimmedBottom (skin().ipx (40.0f)));
        panicPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), in.getWidth() - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 542, 176, 168));
    layoutNav (542, 168);

    // ---- page SIGNAL
    signalPanel_->setBounds (R (204, 72, 942, 456));
    signalChips_->setBounds (R (220, 108, 910, 84));
    view_->setBounds (R (220, 204, 640, 246));
    modeChips_->setBounds (R (876, 204, 254, 92));
    onPill_->setBounds (R (876, 308, 122, 32));
    triggerPill_->setBounds (R (1008, 308, 122, 32));
    polarityPill_->setBounds (R (876, 352, 254, 32));
    signalHint_->setBounds (R (220, 462, 910, 52));
    placeColumns();

    // ---- page DRUMS
    drumsPanel_->setBounds (R (204, 72, 942, 638));
    runPill_->setBounds (R (220, 108, 90, 32));
    syncPill_->setBounds (R (318, 108, 150, 32));
    tempoPill_->setBounds (R (476, 108, 130, 32));
    stepsPill_->setBounds (R (614, 108, 110, 32));
    swingPill_->setBounds (R (732, 108, 120, 32));
    drumsLevelPill_->setBounds (R (860, 108, 120, 32));
    notePill_->setBounds (R (988, 108, 142, 32));
    demoPill_->setBounds (R (220, 150, 140, 30));
    clearPill_->setBounds (R (368, 150, 100, 30));
    masterPill_->setBounds (R (476, 150, 150, 30));
    drumsHint_->setBounds (R (640, 142, 490, 44));
    grid_->setBounds (R (220, 194, 910, 328));
    drumCard_->setBounds (R (220, 536, 910, 160));
    drumCard_->toBack();
    drumsPanel_->toBack();
    for (std::size_t i = 0; i < drumKnobs_.size(); ++i) drumKnobs_[i]->setBounds (R (232.0f + static_cast<float> (i) * 150.0f, 568, 140, 120));
    mutePill_->setBounds (R (860, 580, 130, 32));
    playPill_->setBounds (R (1000, 580, 118, 32));

    // ---- en-tête
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaDiapasonEditor::placeColumns()
{
    const float colW = (942.0f - 12.0f * 7.0f) / 8.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const int rank = i < columnPos_.size() && columnPos_[i] >= 0 ? columnPos_[i] : static_cast<int> (i);
        const float x = 204.0f + static_cast<float> (rank) * (colW + 12.0f);
        columns_[i].card->setBounds (R (x, 542, colW, 168));
        columns_[i].knob->setBounds (R (x, 542 + 18, colW, 132));
    }
}

void NovaDiapasonEditor::pageChanged() { assignKnobs(); }

void NovaDiapasonEditor::tick()
{
    outMeter_->update (dbOf (engine_.peakLeft()), dbOf (engine_.peakRight()));
    if (currentPage() == 0)
    {
        view_->update();
        const std::uint32_t key = static_cast<std::uint32_t> (choiceValue (gen::signal)) * 16u + static_cast<std::uint32_t> (choiceValue (gen::mode));
        if (key != knobKey_) { knobKey_ = key; assignKnobs(); }
    }
}
