#include "FmView.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// ---------------------------------------------------------------------------------------------------- potard entier d'un patch
class Dial : public juce::Component
{
public:
    Dial()
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.onValueChange = [this] { if (! quiet && onValue) onValue (static_cast<int> (std::lround (slider.getValue()))); };
        slider.onDragStart = [this] { if (onBegin) onBegin(); };
        slider.onDragEnd = [this] { if (onEnd) onEnd(); };
        addAndMakeVisible (slider);
    }
    void setup (juce::LookAndFeel* look, juce::Colour textColour, const juce::String& cap, int lo, int hi, int def)
    {
        caption = cap;
        capColour = textColour;
        slider.setLookAndFeel (look);
        slider.setRange (lo, hi, 1.0);
        slider.setDoubleClickReturnValue (true, def);
        slider.setColour (juce::Slider::textBoxTextColourId, textColour.withAlpha (0.95f));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    ~Dial() override { slider.setLookAndFeel (nullptr); }
    void setValueQuiet (int v)
    {
        quiet = true;
        slider.setValue (v, juce::dontSendNotification);
        quiet = false;
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (capColour.withAlpha (isEnabled() ? 0.75f : 0.35f));
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (caption, 0, 0, getWidth(), 14, juce::Justification::centred, true);
    }
    void resized() override { slider.setBounds (0, 14, getWidth(), getHeight() - 14); }

    juce::Slider slider;
    juce::String caption;
    juce::Colour capColour;
    std::function<void (int)> onValue;
    std::function<void()> onBegin, onEnd;
    bool quiet = false;
};

// ---------------------------------------------------------------------------------------------------- diagramme d'algorithme
class AlgorithmDiagram : public juce::Component
{
public:
    explicit AlgorithmDiagram (const theme::Palette& p) : palette_ (p) {}
    std::function<void (int op)> onSelect;
    std::function<void (int op)> onToggle;

    void setState (int algorithm, int selected, int mask)
    {
        if (algorithm == algorithm_ && selected == selected_ && mask == mask_) return;
        algorithm_ = algorithm;
        selected_ = selected;
        mask_ = mask;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& graph = fm::algorithmGraph (algorithm_);
        const auto lay = layout();
        auto col = [this] (Token t)
        {
            const auto& c = palette_.color (t);
            return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
        };
        g.setColour (col (Token::PanelBg));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        // modulations
        g.setColour (col (Token::TextDim));
        for (const auto& e : graph.edges)
        {
            const auto a = lay[static_cast<std::size_t> (e.from)].getCentre(), b = lay[static_cast<std::size_t> (e.to)].getCentre();
            g.drawLine (a.x, a.y + boxH / 2.0f, b.x, b.y - boxH / 2.0f, 1.5f);
        }
        // feedback : boucle à droite de l'opérateur d'arrivée vers celui de départ
        if (graph.feedbackFrom >= 0)
        {
            const auto from = lay[static_cast<std::size_t> (graph.feedbackFrom)], to = lay[static_cast<std::size_t> (graph.feedbackTo)];
            const float x = std::max (from.getRight(), to.getRight()) + 8.0f;
            juce::Path p;
            p.startNewSubPath (from.getRight(), from.getCentreY());
            p.lineTo (x, from.getCentreY());
            p.lineTo (x, to.getCentreY());
            p.lineTo (to.getRight(), to.getCentreY());
            g.setColour (col (Token::Accent2));
            g.strokePath (p, juce::PathStrokeType (1.5f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("FB", static_cast<int> (x) + 2, static_cast<int> ((from.getCentreY() + to.getCentreY()) * 0.5f) - 5, 16, 10, juce::Justification::left);
        }
        // sortie audio sous les porteuses
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)];
            const bool carrier = ((graph.carrierMask >> op) & 1) != 0;
            if (carrier)
            {
                g.setColour (col (Token::Accent));
                g.drawLine (r.getCentreX(), r.getBottom(), r.getCentreX(), static_cast<float> (getHeight()) - 6.0f, 2.0f);
            }
        }
        g.setColour (col (Token::Accent));
        g.drawHorizontalLine (getHeight() - 6, 4.0f, static_cast<float> (getWidth()) - 4.0f);
        // opérateurs
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)];
            const bool on = ((mask_ >> op) & 1) != 0, sel = op == selected_;
            const bool carrier = ((graph.carrierMask >> op) & 1) != 0;
            g.setColour (sel ? col (Token::Accent).withAlpha (0.55f) : col (Token::BgCard));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (carrier ? col (Token::Accent) : col (Token::CtlBd));
            g.drawRoundedRectangle (r, 5.0f, sel ? 2.5f : 1.2f);
            g.setColour (col (Token::Text).withAlpha (on ? 1.0f : 0.35f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("OP" + juce::String (fm::kOperators - op), r, juce::Justification::centred);
            // pastille d'activation
            g.setColour (on ? col (Token::Accent) : col (Token::TextDim).withAlpha (0.4f));
            g.fillEllipse (r.getRight() - 12.0f, r.getY() + 3.0f, 8.0f, 8.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto lay = layout();
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)];
            if (! r.contains (e.position)) continue;
            const juce::Rectangle<float> dot (r.getRight() - 14.0f, r.getY(), 14.0f, 14.0f);
            if (dot.contains (e.position) || e.mods.isRightButtonDown()) { if (onToggle) onToggle (op); }
            else if (onSelect) onSelect (op);
            return;
        }
    }

private:
    static constexpr float boxW = 54.0f, boxH = 28.0f;

    std::array<juce::Rectangle<float>, fm::kOperators> layout() const
    {
        const auto& graph = fm::algorithmGraph (algorithm_);
        std::array<int, fm::kOperators> level {};
        for (int pass = 0; pass < fm::kOperators; ++pass)
            for (const auto& e : graph.edges) level[static_cast<std::size_t> (e.from)] = std::max (level[static_cast<std::size_t> (e.from)],
                                                                                                   level[static_cast<std::size_t> (e.to)] + 1);
        int rows = 1;
        for (int l : level) rows = std::max (rows, l + 1);
        std::array<juce::Rectangle<float>, fm::kOperators> out;
        const float top = 6.0f, bottom = static_cast<float> (getHeight()) - 16.0f;
        const float rowStep = rows > 1 ? (bottom - boxH - top) / static_cast<float> (rows - 1) : 0.0f;
        for (int row = 0; row < rows; ++row)
        {
            std::vector<int> ops;
            for (int op = fm::kOperators - 1; op >= 0; --op) // OP1 à gauche
                if (level[static_cast<std::size_t> (op)] == row) ops.push_back (op);
            const float y = bottom - boxH - rowStep * static_cast<float> (row);
            const float cell = (static_cast<float> (getWidth()) - 24.0f) / static_cast<float> (std::max<std::size_t> (ops.size(), 1));
            for (std::size_t i = 0; i < ops.size(); ++i)
                out[static_cast<std::size_t> (ops[i])] = juce::Rectangle<float> (4.0f + cell * static_cast<float> (i) + (cell - boxW) * 0.5f, y, boxW, boxH);
        }
        return out;
    }

    theme::Palette palette_;
    int algorithm_ = 0, selected_ = 5, mask_ = 0x3f;
};

// ---------------------------------------------------------------------------------------------------- enveloppe 4 vitesses / 4 niveaux
class DxEnvelopeEditor : public juce::Component
{
public:
    explicit DxEnvelopeEditor (const theme::Palette& p) : palette_ (p) {}
    std::function<void (const std::array<int, 4>& rates, const std::array<int, 4>& levels)> onChange;
    std::function<void()> onBegin, onEnd;

    void setValues (const std::array<int, 4>& rates, const std::array<int, 4>& levels)
    {
        if (dragging_ >= 0) return;
        if (rates == rates_ && levels == levels_) return;
        rates_ = rates;
        levels_ = levels;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto col = [this] (Token t)
        {
            const auto& c = palette_.color (t);
            return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
        };
        g.setColour (col (Token::PanelBg));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        const auto pts = points();
        juce::Path path;
        path.startNewSubPath (pts[0]);
        for (std::size_t i = 1; i < pts.size(); ++i) path.lineTo (pts[i]);
        g.setColour (col (Token::Accent));
        g.strokePath (path, juce::PathStrokeType (2.0f));
        // niveau de repos (L4) en pointillés jusqu'au bord
        const juce::String labels[5] = { "", "R1/L1", "R2/L2", "R3/L3", "R4/L4" };
        g.setFont (juce::FontOptions (9.5f));
        for (std::size_t i = 1; i < pts.size(); ++i)
        {
            const bool handle = i != 4; // le point 4 est la fin du palier de maintien
            if (! handle) continue;
            const std::size_t idx = i < 4 ? i : 4;
            juce::ignoreUnused (idx);
            g.setColour (col (Token::Handle));
            g.fillEllipse (pts[i].x - 5.0f, pts[i].y - 5.0f, 10.0f, 10.0f);
        }
        g.setColour (col (Token::TextDim));
        for (int h = 0; h < 4; ++h)
        {
            const auto p = handlePoint (h);
            g.drawText (labels[h + 1], static_cast<int> (p.x) - 20, static_cast<int> (p.y) - 18, 40, 12, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging_ = -1;
        float best = 14.0f;
        for (int h = 0; h < 4; ++h)
        {
            const float d = handlePoint (h).getDistanceFrom (e.position);
            if (d < best) { best = d; dragging_ = h; }
        }
        if (dragging_ >= 0 && onBegin) onBegin();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging_ < 0) return;
        const int h = dragging_;
        const float H = static_cast<float> (getHeight()) - 16.0f;
        levels_[static_cast<std::size_t> (h)] = std::clamp (static_cast<int> (std::lround ((1.0f - (e.position.y - 8.0f) / H) * 99.0f)), 0, 99);
        // la position horizontale règle la vitesse du segment qui mène à ce point
        const auto prevX = h == 0 ? 8.0f : handlePoint (h - 1).x;
        const float width = e.position.x - prevX;
        rates_[static_cast<std::size_t> (h)] = std::clamp (99 - static_cast<int> (std::lround ((width - kMinSeg) / kSegScale)), 0, 99);
        if (onChange) onChange (rates_, levels_);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging_ >= 0 && onEnd) onEnd();
        dragging_ = -1;
    }

private:
    static constexpr float kMinSeg = 14.0f, kSegScale = 2.4f, kSustain = 26.0f;

    float levelY (int level) const
    {
        const float H = static_cast<float> (getHeight()) - 16.0f;
        return 8.0f + H * (1.0f - static_cast<float> (level) / 99.0f);
    }
    float segWidth (int h) const { return kMinSeg + static_cast<float> (99 - rates_[static_cast<std::size_t> (h)]) * kSegScale; }

    // départ (niveau L4), L1, L2, L3, fin du maintien, L4
    std::array<juce::Point<float>, 6> points() const
    {
        std::array<juce::Point<float>, 6> p;
        float x = 8.0f;
        p[0] = { x, levelY (levels_[3]) };
        for (int h = 0; h < 3; ++h)
        {
            x += segWidth (h);
            p[static_cast<std::size_t> (h + 1)] = { x, levelY (levels_[static_cast<std::size_t> (h)]) };
        }
        x += kSustain;
        p[4] = { x, levelY (levels_[2]) };
        x += segWidth (3);
        p[5] = { x, levelY (levels_[3]) };
        return p;
    }

    juce::Point<float> handlePoint (int h) const
    {
        const auto p = points();
        return h < 3 ? p[static_cast<std::size_t> (h + 1)] : p[5];
    }

    theme::Palette palette_;
    std::array<int, 4> rates_ { 99, 99, 99, 99 }, levels_ { 99, 99, 99, 0 };
    int dragging_ = -1;
};

// ---------------------------------------------------------------------------------------------------- mise à l'échelle du clavier
class KeyScaleCurve : public juce::Component
{
public:
    explicit KeyScaleCurve (const theme::Palette& p) : palette_ (p) {}
    void setPatch (const fm::Patch& patch, int op)
    {
        if (patch_ == patch && op_ == op) return;
        patch_ = patch;
        op_ = op;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto col = [this] (Token t)
        {
            const auto& c = palette_.color (t);
            return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
        };
        g.setColour (col (Token::PanelBg));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        const auto b = getLocalBounds().toFloat().reduced (6.0f);
        g.setColour (col (Token::Grid));
        g.drawHorizontalLine (static_cast<int> (b.getCentreY()), b.getX(), b.getRight());
        // point de rupture : note du patch + 21 (A-1 = note 9... l'échelle de msfa part de rupture + 17)
        const int breakNote = std::clamp (patch_.op (op_, fm::kBreakPoint) + 17, 0, 127);
        const float bx = b.getX() + b.getWidth() * static_cast<float> (breakNote) / 127.0f;
        g.setColour (col (Token::TextDim));
        g.drawVerticalLine (static_cast<int> (bx), b.getY(), b.getBottom());
        juce::Path p;
        for (int n = 0; n <= 127; ++n)
        {
            const float v = static_cast<float> (fm::keyboardLevelScaling (patch_, op_, n));
            const float y = b.getCentreY() - juce::jlimit (-1.0f, 1.0f, v / 60.0f) * (b.getHeight() * 0.5f);
            const float x = b.getX() + b.getWidth() * static_cast<float> (n) / 127.0f;
            if (n == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

private:
    theme::Palette palette_;
    fm::Patch patch_ = fm::Patch::initVoice();
    int op_ = 5;
};

const char* const kCurveNames[4] = { "-LIN", "-EXP", "+EXP", "+LIN" };
const char* const kWaveKeys[6] = { "fm.lfo_triangle", "fm.lfo_saw_down", "fm.lfo_saw_up", "fm.lfo_square", "fm.lfo_sine", "fm.lfo_sh" };
} // namespace

// ---------------------------------------------------------------------------------------------------- vue
struct FmView::Impl
{
    explicit Impl (FmView& o)
        : owner (o), diagram (o.palette_), opEg (o.palette_), pitchEg (o.palette_), curve (o.palette_)
    {
        auto* look = owner.sliderLook_.get();
        const auto textCol = owner.colour (Token::Text);
        auto add = [this] (juce::Component& c, int page)
        {
            owner.addAndMakeVisible (c);
            pageComponents.push_back ({ page, &c });
        };

        // ---- en-tête
        for (int i = 0; i < 2; ++i)
        {
            tabs[i].setButtonText (owner.text (i == 0 ? "fm.tab_operators" : "fm.tab_global"));
            tabs[i].setClickingTogglesState (true);
            tabs[i].setRadioGroupId (7200);
            tabs[i].setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::Accent));
            tabs[i].setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            tabs[i].setToggleState (i == 0, juce::dontSendNotification);
            tabs[i].onClick = [this, i] { setPage (i); };
            owner.addAndMakeVisible (tabs[i]);
        }
        nameEditor.setMultiLine (false);
        nameEditor.setInputRestrictions (fm::kNameLength);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, owner.colour (Token::BgCard));
        nameEditor.setColour (juce::TextEditor::textColourId, owner.colour (Token::Text));
        nameEditor.setColour (juce::TextEditor::outlineColourId, owner.colour (Token::CtlBd));
        nameEditor.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
        owner.addAndMakeVisible (nameEditor);
        importBtn.setButtonText (owner.text ("fm.import"));
        importBtn.onClick = [this] { if (owner.onImportSysex) owner.onImportSysex(); };
        exportVoiceBtn.setButtonText (owner.text ("fm.export_voice"));
        exportVoiceBtn.onClick = [this] { if (owner.onExportVoice) owner.onExportVoice(); };
        exportBankBtn.setButtonText (owner.text ("fm.export_bank"));
        exportBankBtn.onClick = [this] { if (owner.onExportBank) owner.onExportBank(); };
        for (auto* b : { &importBtn, &exportVoiceBtn, &exportBankBtn }) owner.addAndMakeVisible (b);

        auto dial = [&] (Dial& d, int page, const char* key, int lo, int hi, int def, std::function<void (fm::FmDescription&, int)> apply)
        {
            d.setup (look, textCol, owner.text (key), lo, hi, def);
            d.onBegin = [this] { beginGesture(); };
            d.onEnd = [this] { endGesture(); };
            d.onValue = [this, apply] (int v) { edit ([&] (fm::FmDescription& x) { apply (x, v); }); };
            add (d, page);
        };
        auto combo = [&] (juce::ComboBox& c, int page)
        {
            c.setColour (juce::ComboBox::backgroundColourId, owner.colour (Token::BgCard));
            c.setColour (juce::ComboBox::textColourId, owner.colour (Token::Text));
            c.setColour (juce::ComboBox::outlineColourId, owner.colour (Token::CtlBd));
            add (c, page);
        };
        auto label = [&] (juce::Label& l, int page, const char* key)
        {
            l.setText (owner.text (key), juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
            l.setFont (juce::FontOptions (11.0f));
            add (l, page);
        };
        auto toggle = [&] (juce::ToggleButton& t, int page, const char* key, std::function<void (fm::FmDescription&, bool)> apply)
        {
            t.setButtonText (owner.text (key));
            t.setColour (juce::ToggleButton::textColourId, owner.colour (Token::Text));
            t.onClick = [this, &t, apply]
            {
                if (updating) return;
                beginGesture();
                edit ([&] (fm::FmDescription& x) { apply (x, t.getToggleState()); });
                endGesture();
            };
            add (t, page);
        };
        auto opField = [this] (int field) { return [this, field] (fm::FmDescription& d, int v) { d.patch.setOp (selectedOp, field, v); }; };
        auto globalByte = [] (int index) { return [index] (fm::FmDescription& d, int v) { d.patch.setGlobal (index, v); }; };

        // ---- page 0 : algorithme et opérateurs
        diagram.onSelect = [this] (int op) { selectedOp = op; refresh(); };
        diagram.onToggle = [this] (int op)
        {
            beginGesture();
            edit ([op] (fm::FmDescription& d) { d.patch.setOperatorOn (op, ! d.patch.operatorOn (op)); });
            endGesture();
        };
        add (diagram, 0);
        for (int a = 0; a < fm::kNumAlgorithms; ++a) algoBox.addItem (juce::String (a + 1), a + 1);
        combo (algoBox, 0);
        algoBox.onChange = [this]
        {
            if (updating || algoBox.getSelectedId() < 1) return;
            beginGesture();
            edit ([this] (fm::FmDescription& d) { d.patch.setGlobal (fm::kAlgorithmOffset, algoBox.getSelectedId() - 1); });
            endGesture();
        };
        algoPrev.setButtonText ("<");
        algoNext.setButtonText (">");
        algoPrev.onClick = [this] { stepAlgorithm (-1); };
        algoNext.onClick = [this] { stepAlgorithm (+1); };
        add (algoPrev, 0);
        add (algoNext, 0);
        label (algoLabel, 0, "fm.algorithm");
        dial (feedback, 0, "fm.feedback", 0, 7, 0, globalByte (fm::kFeedbackOffset));
        toggle (keySync, 0, "fm.key_sync", [] (fm::FmDescription& d, bool b) { d.patch.setGlobal (136, b ? 1 : 0); });

        label (opTitle, 0, "fm.operator");
        opTitle.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        opTitle.setColour (juce::Label::textColourId, owner.colour (Token::Accent));
        modeBox.addItem (owner.text ("fm.mode_ratio"), 1);
        modeBox.addItem (owner.text ("fm.mode_fixed"), 2);
        combo (modeBox, 0);
        modeBox.onChange = [this]
        {
            if (updating) return;
            beginGesture();
            edit ([this] (fm::FmDescription& d) { d.patch.setOp (selectedOp, fm::kMode, modeBox.getSelectedId() - 1); });
            endGesture();
        };
        dial (coarse, 0, "fm.coarse", 0, 31, 1, opField (fm::kCoarse));
        dial (fine, 0, "fm.fine", 0, 99, 0, opField (fm::kFine));
        dial (detune, 0, "fm.detune", 0, 14, 7, opField (fm::kDetune));
        dial (outLevel, 0, "fm.level", 0, 99, 0, opField (fm::kOutputLevel));
        dial (velSens, 0, "fm.vel_sens", 0, 7, 0, opField (fm::kVelocitySens));
        dial (rateScale, 0, "fm.rate_scaling", 0, 7, 0, opField (fm::kRateScaling));
        dial (ams, 0, "fm.ams", 0, 3, 0, opField (fm::kAmpModSens));
        label (freqLabel, 0, "fm.operator");
        freqLabel.setColour (juce::Label::textColourId, owner.colour (Token::Accent2));
        opEg.onBegin = [this] { beginGesture(); };
        opEg.onEnd = [this] { endGesture(); };
        opEg.onChange = [this] (const std::array<int, 4>& r, const std::array<int, 4>& l)
        {
            edit ([&] (fm::FmDescription& d)
                  {
                      for (int i = 0; i < 4; ++i)
                      {
                          d.patch.setOp (selectedOp, fm::kRate1 + i, r[static_cast<std::size_t> (i)]);
                          d.patch.setOp (selectedOp, fm::kLevel1 + i, l[static_cast<std::size_t> (i)]);
                      }
                  });
        };
        add (opEg, 0);
        label (scalingLabel, 0, "fm.key_scaling");
        dial (breakPoint, 0, "fm.break_point", 0, 99, 39, opField (fm::kBreakPoint));
        dial (leftDepth, 0, "fm.left_depth", 0, 99, 0, opField (fm::kLeftDepth));
        dial (rightDepth, 0, "fm.right_depth", 0, 99, 0, opField (fm::kRightDepth));
        for (int c = 0; c < 4; ++c) { leftCurve.addItem (kCurveNames[c], c + 1); rightCurve.addItem (kCurveNames[c], c + 1); }
        combo (leftCurve, 0);
        combo (rightCurve, 0);
        leftCurve.onChange = [this] { if (! updating) { beginGesture(); edit ([this] (fm::FmDescription& d) { d.patch.setOp (selectedOp, fm::kLeftCurve, leftCurve.getSelectedId() - 1); }); endGesture(); } };
        rightCurve.onChange = [this] { if (! updating) { beginGesture(); edit ([this] (fm::FmDescription& d) { d.patch.setOp (selectedOp, fm::kRightCurve, rightCurve.getSelectedId() - 1); }); endGesture(); } };
        add (curve, 0);

        // ---- page 1 : global
        label (lfoLabel, 1, "fm.lfo");
        dial (lfoSpeed, 1, "fm.lfo_speed", 0, 99, 35, globalByte (137));
        dial (lfoDelay, 1, "fm.lfo_delay", 0, 99, 0, globalByte (138));
        dial (lfoPmd, 1, "fm.lfo_pmd", 0, 99, 0, globalByte (139));
        dial (lfoAmd, 1, "fm.lfo_amd", 0, 99, 0, globalByte (140));
        dial (lfoPms, 1, "fm.lfo_pms", 0, 7, 3, globalByte (143));
        for (int w = 0; w < 6; ++w) lfoWave.addItem (owner.text (kWaveKeys[w]), w + 1);
        combo (lfoWave, 1);
        lfoWave.onChange = [this] { if (! updating) { beginGesture(); edit ([this] (fm::FmDescription& d) { d.patch.setGlobal (142, lfoWave.getSelectedId() - 1); }); endGesture(); } };
        toggle (lfoSync, 1, "fm.lfo_sync", [] (fm::FmDescription& d, bool b) { d.patch.setGlobal (141, b ? 1 : 0); });
        label (pitchEgLabel, 1, "fm.pitch_eg");
        pitchEg.onBegin = [this] { beginGesture(); };
        pitchEg.onEnd = [this] { endGesture(); };
        pitchEg.onChange = [this] (const std::array<int, 4>& r, const std::array<int, 4>& l)
        {
            edit ([&] (fm::FmDescription& d)
                  {
                      for (int i = 0; i < 4; ++i)
                      {
                          d.patch.setGlobal (fm::kPitchEgOffset + i, r[static_cast<std::size_t> (i)]);
                          d.patch.setGlobal (fm::kPitchEgOffset + 4 + i, l[static_cast<std::size_t> (i)]);
                      }
                  });
        };
        add (pitchEg, 1);
        dial (transpose, 1, "fm.transpose", 0, 48, 24, globalByte (fm::kTransposeOffset));
        dial (masterTune, 1, "fm.master_tune", -100, 100, 0, [] (fm::FmDescription& d, int v) { d.masterTuneCents = v; });
        label (voicesLabel, 1, "fm.voices");
        polyBox.addItem (owner.text ("fm.poly"), 1);
        polyBox.addItem (owner.text ("fm.mono"), 2);
        polyBox.addItem (owner.text ("fm.legato"), 3);
        combo (polyBox, 1);
        polyBox.onChange = [this]
        {
            if (updating || polyBox.getSelectedId() < 1) return;
            beginGesture();
            edit ([this] (fm::FmDescription& d)
                  {
                      const int i = polyBox.getSelectedId();
                      d.polyphony = i == 2 ? PolyphonyMode::Mono : i == 3 ? PolyphonyMode::Legato : PolyphonyMode::Poly;
                  });
            endGesture();
        };
        stealBox.addItem (owner.text ("fm.steal_oldest"), 1);
        stealBox.addItem (owner.text ("fm.steal_quietest"), 2);
        stealBox.addItem (owner.text ("fm.steal_keep_low"), 3);
        stealBox.addItem (owner.text ("fm.steal_keep_high"), 4);
        combo (stealBox, 1);
        stealBox.onChange = [this]
        {
            if (updating || stealBox.getSelectedId() < 1) return;
            beginGesture();
            edit ([this] (fm::FmDescription& d)
                  {
                      const int i = stealBox.getSelectedId();
                      d.stealMode = i == 2 ? VoiceStealMode::Quietest : i == 3 ? VoiceStealMode::KeepLow : i == 4 ? VoiceStealMode::KeepHigh : VoiceStealMode::Oldest;
                  });
            endGesture();
        };
        dial (maxVoices, 1, "fm.max_voices", 1, fm::kMaxVoices, 16, [] (fm::FmDescription& d, int v) { d.maxVoices = v; });
        dial (glide, 1, "fm.glide", 0, fm::kMaxGlideMs, 0, [] (fm::FmDescription& d, int v) { d.glideMs = v; });
        dial (bendRange, 1, "fm.bend_range", 0, 12, 2, [] (fm::FmDescription& d, int v) { d.pitchBendRange = v; });
        dial (wheelPitch, 1, "fm.wheel_pitch", 0, 15, 0, [] (fm::FmDescription& d, int v) { d.wheelPitchSens = v; });
        dial (wheelAmp, 1, "fm.wheel_amp", 0, 15, 0, [] (fm::FmDescription& d, int v) { d.wheelAmpSens = v; });
        dial (unison, 1, "fm.unison", 1, fm::kMaxUnison, 1, [] (fm::FmDescription& d, int v) { d.unisonVoices = v; });
        label (macrosLabel, 1, "fm.macros");

        // macros automatisables : potards liés aux paramètres de la piste
        struct MacroDef { mixer::FmKind kind; const char* key; };
        const MacroDef defs[] = { { mixer::kFmBrightness, "fm.brightness" }, { mixer::kFmEnvSpeed, "fm.env_speed" },
                                  { mixer::kFmFeedbackDrive, "fm.feedback_drive" }, { mixer::kFmLevelOp1, "fm.op1" }, { mixer::kFmLevelOp2, "fm.op2" },
                                  { mixer::kFmLevelOp3, "fm.op3" }, { mixer::kFmLevelOp4, "fm.op4" }, { mixer::kFmLevelOp5, "fm.op5" },
                                  { mixer::kFmLevelOp6, "fm.op6" }, { mixer::kFmUnisonDetune, "fm.unison_detune" },
                                  { mixer::kFmUnisonSpread, "fm.unison_spread" } };
        for (const auto& m : defs)
        {
            Macro mk;
            mk.kind = m.kind;
            mk.captionKey = m.key;
            mk.knob = std::make_unique<NeptuneKnob> (owner.host_, mixer::fmParam (0, m.kind), owner.palette_, owner.knobStyle_,
                                                     [this] (ParamId id, uistate::Terminal t) { openModal (id, t); });
            add (*mk.knob, 1);
            macros.push_back (std::move (mk));
        }
        setPage (0);
    }

    // ---- état
    void setPage (int p)
    {
        page = p;
        for (int i = 0; i < 2; ++i) tabs[i].setToggleState (i == p, juce::dontSendNotification);
        for (auto& [pg, c] : pageComponents) c->setVisible (pg == p);
        refresh();
        owner.resized();
        owner.repaint();
    }

    void beginGesture()
    {
        if (updating) return;
        dragging = true;
        if (owner.onGestureBegin) owner.onGestureBegin();
    }
    void endGesture()
    {
        if (! dragging) return;
        dragging = false;
        if (owner.onGestureEnd) owner.onGestureEnd();
    }
    void edit (const std::function<void (fm::FmDescription&)>& change)
    {
        if (updating) return;
        auto next = d;
        change (next);
        next = fm::sanitised (std::move (next));
        if (next == d) return;
        d = next;
        if (owner.onChange) owner.onChange (d);
        refresh();
    }
    void commitName()
    {
        if (updating) return;
        const auto s = nameEditor.getText().toStdString();
        if (s == d.patch.name()) return;
        beginGesture();
        edit ([&s] (fm::FmDescription& x) { x.patch.setName (s); });
        endGesture();
    }
    void stepAlgorithm (int delta)
    {
        const int a = std::clamp (d.patch.algorithm() + delta, 0, fm::kNumAlgorithms - 1);
        beginGesture();
        edit ([a] (fm::FmDescription& x) { x.patch.setGlobal (fm::kAlgorithmOffset, a); });
        endGesture();
    }

    // Recopie la description dans les contrôles (sans notification)
    void refresh()
    {
        updating = true;
        const auto& p = d.patch;
        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (U (p.name()), false);
        int mask = 0;
        for (int op = 0; op < fm::kOperators; ++op) mask |= p.operatorOn (op) ? (1 << op) : 0;
        diagram.setState (p.algorithm(), selectedOp, mask);
        algoBox.setSelectedId (p.algorithm() + 1, juce::dontSendNotification);
        feedback.setValueQuiet (p.feedback());
        keySync.setToggleState (p.global (136) != 0, juce::dontSendNotification);
        opTitle.setText (owner.text ("fm.operator") + " OP" + juce::String (fm::kOperators - selectedOp), juce::dontSendNotification);
        modeBox.setSelectedId (p.op (selectedOp, fm::kMode) + 1, juce::dontSendNotification);
        coarse.setValueQuiet (p.op (selectedOp, fm::kCoarse));
        fine.setValueQuiet (p.op (selectedOp, fm::kFine));
        detune.setValueQuiet (p.op (selectedOp, fm::kDetune));
        outLevel.setValueQuiet (p.op (selectedOp, fm::kOutputLevel));
        velSens.setValueQuiet (p.op (selectedOp, fm::kVelocitySens));
        rateScale.setValueQuiet (p.op (selectedOp, fm::kRateScaling));
        ams.setValueQuiet (p.op (selectedOp, fm::kAmpModSens));
        breakPoint.setValueQuiet (p.op (selectedOp, fm::kBreakPoint));
        leftDepth.setValueQuiet (p.op (selectedOp, fm::kLeftDepth));
        rightDepth.setValueQuiet (p.op (selectedOp, fm::kRightDepth));
        leftCurve.setSelectedId (p.op (selectedOp, fm::kLeftCurve) + 1, juce::dontSendNotification);
        rightCurve.setSelectedId (p.op (selectedOp, fm::kRightCurve) + 1, juce::dontSendNotification);
        std::array<int, 4> r {}, l {};
        for (int i = 0; i < 4; ++i)
        {
            r[static_cast<std::size_t> (i)] = p.op (selectedOp, fm::kRate1 + i);
            l[static_cast<std::size_t> (i)] = p.op (selectedOp, fm::kLevel1 + i);
        }
        opEg.setValues (r, l);
        curve.setPatch (p, selectedOp);
        // fréquence résultante : rapport (coarse 0 = 0,5) ou fréquence fixe en Hz
        const int coarseV = p.op (selectedOp, fm::kCoarse), fineV = p.op (selectedOp, fm::kFine);
        if (p.op (selectedOp, fm::kMode) == 0)
            freqLabel.setText (owner.text ("fm.ratio") + " " + juce::String ((coarseV == 0 ? 0.5 : coarseV) * (1.0 + fineV / 100.0), 3), juce::dontSendNotification);
        else
            freqLabel.setText (owner.text ("fm.fixed") + " " + juce::String (std::pow (10.0, (coarseV & 3) + fineV * 0.01), 1) + " Hz", juce::dontSendNotification);

        lfoSpeed.setValueQuiet (p.global (137));
        lfoDelay.setValueQuiet (p.global (138));
        lfoPmd.setValueQuiet (p.global (139));
        lfoAmd.setValueQuiet (p.global (140));
        lfoPms.setValueQuiet (p.global (143));
        lfoWave.setSelectedId (p.global (142) + 1, juce::dontSendNotification);
        lfoSync.setToggleState (p.global (141) != 0, juce::dontSendNotification);
        std::array<int, 4> pr {}, pl {};
        for (int i = 0; i < 4; ++i)
        {
            pr[static_cast<std::size_t> (i)] = p.global (fm::kPitchEgOffset + i);
            pl[static_cast<std::size_t> (i)] = p.global (fm::kPitchEgOffset + 4 + i);
        }
        pitchEg.setValues (pr, pl);
        transpose.setValueQuiet (p.global (fm::kTransposeOffset));
        masterTune.setValueQuiet (d.masterTuneCents);
        polyBox.setSelectedId (d.polyphony == PolyphonyMode::Mono ? 2 : d.polyphony == PolyphonyMode::Legato ? 3 : 1, juce::dontSendNotification);
        stealBox.setSelectedId (d.stealMode == VoiceStealMode::Quietest ? 2 : d.stealMode == VoiceStealMode::KeepLow ? 3
                                : d.stealMode == VoiceStealMode::KeepHigh ? 4 : 1, juce::dontSendNotification);
        maxVoices.setValueQuiet (d.maxVoices);
        glide.setValueQuiet (d.glideMs);
        bendRange.setValueQuiet (d.pitchBendRange);
        wheelPitch.setValueQuiet (d.wheelPitchSens);
        wheelAmp.setValueQuiet (d.wheelAmpSens);
        unison.setValueQuiet (d.unisonVoices);
        updating = false;
    }

    void openModal (ParamId id, uistate::Terminal t)
    {
        if (modal) return;
        modal = std::make_unique<KnobModal> (owner.host_, id, t, owner.palette_, owner.tr_,
                                             [safe = juce::Component::SafePointer<FmView> (&owner)]
                                             {
                                                 juce::MessageManager::callAsync ([safe]
                                                 {
                                                     if (safe == nullptr) return;
                                                     safe->impl_->closeModal();
                                                 });
                                             });
        owner.addAndMakeVisible (*modal);
        modal->setBounds (owner.getLocalBounds());
        modal->toFront (true);
    }
    void closeModal()
    {
        if (! modal) return;
        owner.host_.setModalSize (modal->terminal(), modal->panelSize());
        modal.reset();
    }

    struct Macro
    {
        mixer::FmKind kind = mixer::kFmBrightness;
        const char* captionKey = "";
        std::unique_ptr<NeptuneKnob> knob;
    };

    FmView& owner;
    fm::FmDescription d;
    int selectedOp = 5; // OP1
    int page = 0;
    int track = 0;
    bool updating = false, dragging = false;

    juce::TextButton tabs[2], importBtn, exportVoiceBtn, exportBankBtn, algoPrev, algoNext;
    juce::TextEditor nameEditor;
    AlgorithmDiagram diagram;
    juce::ComboBox algoBox, modeBox, leftCurve, rightCurve, lfoWave, polyBox, stealBox;
    Dial feedback, coarse, fine, detune, outLevel, velSens, rateScale, ams, breakPoint, leftDepth, rightDepth;
    Dial lfoSpeed, lfoDelay, lfoPmd, lfoAmd, lfoPms, transpose, masterTune, maxVoices, glide, bendRange, wheelPitch, wheelAmp, unison;
    juce::ToggleButton keySync, lfoSync;
    juce::Label algoLabel, opTitle, freqLabel, scalingLabel, lfoLabel, pitchEgLabel, voicesLabel, macrosLabel;
    DxEnvelopeEditor opEg, pitchEg;
    KeyScaleCurve curve;
    std::vector<Macro> macros;
    std::vector<std::pair<int, juce::Component*>> pageComponents;
    std::unique_ptr<KnobModal> modal;
};

FmView::FmView (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, theme::KnobStyle knobStyle)
    : host_ (host), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), sliderLook_ (std::make_unique<NeptuneSliderLook> (palette, knobStyle)),
      keyboard_ (palette)
{
    impl_ = std::make_unique<Impl> (*this);
    keyboard_.onNoteOn = [this] (int key, float v) { if (onNoteOn) onNoteOn (key, v); };
    keyboard_.onNoteOff = [this] (int key) { if (onNoteOff) onNoteOff (key); };
    octDown_.setButtonText (text ("synth.oct_down"));
    octUp_.setButtonText (text ("synth.oct_up"));
    octDown_.onClick = [this] { keyboard_.shiftOctave (-1); };
    octUp_.onClick = [this] { keyboard_.shiftOctave (+1); };
    hold_.setButtonText (text ("synth.hold"));
    hold_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    hold_.onClick = [this] { keyboard_.setHold (hold_.getToggleState()); };
    keyboardButton_.onClick = [this]
    {
        keyboardMode_ = keyboardMode_ < 0 ? 0 : keyboardMode_ == 0 ? 1 : -1;
        const char* key = keyboardMode_ < 0 ? "synth.kb_auto" : keyboardMode_ == 0 ? "synth.kb_hidden" : "synth.kb_shown";
        keyboardButton_.setButtonText (text (key));
        if (! lastArea_.isEmpty()) placeScaled (lastArea_);
    };
    keyboardButton_.setButtonText (text ("synth.kb_auto"));
    for (auto* c : std::initializer_list<juce::Component*> { &keyboard_, &octDown_, &octUp_, &hold_, &keyboardButton_ }) addAndMakeVisible (c);
    startTimerHz (20);
}

FmView::~FmView()
{
    stopTimer();
    impl_.reset(); // les potards (et leurs look) avant sliderLook_
}

juce::Colour FmView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String FmView::text (const char* key) const { return U (std::string (tr_.tr (key))); }

void FmView::placeScaled (juce::Rectangle<int> area)
{
    lastArea_ = area;
    const bool shown = keyboardMode_ == 1 || (keyboardMode_ < 0 && area.getHeight() >= designHeight);
    if (keyboardShown_ && ! shown) keyboard_.releaseAll();
    keyboardShown_ = shown;
    const int dh = shown ? designHeight : designHeightNoKeyboard;
    const float s = juce::jmin (1.0f, static_cast<float> (area.getWidth()) / designWidth, static_cast<float> (area.getHeight()) / dh);
    const float scale = juce::jmax (0.4f, s);
    setTransform (juce::AffineTransform::scale (scale));
    // la transformation s'applique aux limites dans le repère du parent (autour de son origine) : position = zone / échelle
    const auto x = juce::roundToInt (static_cast<float> (area.getX()) / scale);
    const auto y = juce::roundToInt (static_cast<float> (area.getY()) / scale);
    const auto w = juce::roundToInt (static_cast<float> (area.getWidth()) / scale);
    const auto h = juce::roundToInt (static_cast<float> (area.getHeight()) / scale);
    if (getWidth() == w && getHeight() == h) resized();
    setBounds (x, y, w, h);
}

void FmView::setDescription (const fm::FmDescription& d)
{
    if (impl_->dragging) return;
    const auto clean = fm::sanitised (d);
    if (clean == impl_->d) return;
    impl_->d = clean;
    impl_->refresh();
}

void FmView::setTrack (int track)
{
    impl_->track = track;
    for (auto& m : impl_->macros) m.knob->setParamId (mixer::fmParam (track, m.kind));
}

void FmView::timerCallback()
{
    if (! isVisible()) return;
    repaint();
}

void FmView::paint (juce::Graphics& g)
{
    // légendes et valeurs des macros (page globale)
    if (impl_->page != 1) return;
    for (const auto& m : impl_->macros)
    {
        const auto b = m.knob->getBounds();
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (colour (Token::TextDim));
        g.drawText (text (m.captionKey), b.getX() - 14, b.getY() - 15, b.getWidth() + 28, 14, juce::Justification::centred);
        g.setColour (colour (Token::Accent));
        g.drawText (U (host_.parameters().toText (mixer::fmParam (impl_->track, m.kind))), b.getX() - 14, b.getBottom() + 1, b.getWidth() + 28, 14,
                    juce::Justification::centred);
    }
}

void FmView::resized()
{
    auto& m = *impl_;
    auto r = getLocalBounds();
    {
        auto top = r.removeFromTop (34);
        keyboardButton_.setBounds (top.removeFromRight (190).reduced (2));
        m.exportBankBtn.setBounds (top.removeFromRight (120).reduced (2));
        m.exportVoiceBtn.setBounds (top.removeFromRight (120).reduced (2));
        m.importBtn.setBounds (top.removeFromRight (110).reduced (2));
        m.nameEditor.setBounds (top.removeFromRight (150).reduced (2));
        for (auto& t : m.tabs) t.setBounds (top.removeFromLeft (170).reduced (2));
    }
    r.removeFromTop (6);
    for (auto* c : std::initializer_list<juce::Component*> { &keyboard_, &octDown_, &octUp_, &hold_ }) c->setVisible (keyboardShown_);
    auto bottom = r.removeFromBottom (keyboardShown_ ? 136 : 0);
    if (keyboardShown_)
    {
        auto line = bottom.removeFromTop (32);
        octDown_.setBounds (line.removeFromLeft (80).reduced (2));
        octUp_.setBounds (line.removeFromLeft (80).reduced (2));
        hold_.setBounds (line.removeFromLeft (120).reduced (2));
        keyboard_.setBounds (bottom.reduced (0, 2));
    }

    constexpr int dialW = 72, dialH = 86;
    if (m.page == 0)
    {
        auto left = r.removeFromLeft (340);
        r.removeFromLeft (14);
        m.diagram.setBounds (left.removeFromTop (230));
        left.removeFromTop (8);
        auto row = left.removeFromTop (28);
        m.algoLabel.setBounds (row.removeFromLeft (80));
        m.algoPrev.setBounds (row.removeFromLeft (34).reduced (1));
        m.algoBox.setBounds (row.removeFromLeft (80).reduced (1));
        m.algoNext.setBounds (row.removeFromLeft (34).reduced (1));
        left.removeFromTop (6);
        auto fbRow = left.removeFromTop (dialH);
        m.feedback.setBounds (fbRow.removeFromLeft (dialW));
        m.keySync.setBounds (fbRow.removeFromLeft (150).withTrimmedTop (26));

        // opérateur choisi
        auto head = r.removeFromTop (30);
        m.opTitle.setBounds (head.removeFromLeft (220));
        m.modeBox.setBounds (head.removeFromLeft (110).reduced (2));
        m.freqLabel.setBounds (head.removeFromLeft (220));
        auto dials = r.removeFromTop (dialH);
        for (auto* d : { &m.coarse, &m.fine, &m.detune, &m.outLevel, &m.velSens, &m.rateScale, &m.ams })
            d->setBounds (dials.removeFromLeft (dialW));
        r.removeFromTop (8);
        auto lower = r;
        m.opEg.setBounds (lower.removeFromLeft (360).withHeight (150));
        lower.removeFromLeft (12);
        m.scalingLabel.setBounds (lower.removeFromTop (18));
        auto scaleRow = lower.removeFromTop (dialH);
        m.breakPoint.setBounds (scaleRow.removeFromLeft (dialW));
        m.leftDepth.setBounds (scaleRow.removeFromLeft (dialW));
        m.rightDepth.setBounds (scaleRow.removeFromLeft (dialW));
        auto curves = scaleRow.removeFromLeft (120);
        m.leftCurve.setBounds (curves.removeFromTop (28).reduced (2));
        m.rightCurve.setBounds (curves.removeFromTop (28).reduced (2));
        lower.removeFromTop (6);
        m.curve.setBounds (lower.removeFromLeft (300).withHeight (90));
    }
    else
    {
        auto row = r.removeFromTop (22);
        m.lfoLabel.setBounds (row.removeFromLeft (200));
        m.pitchEgLabel.setBounds (row.removeFromRight (400));
        auto top = r.removeFromTop (dialH);
        auto lfoArea = top.removeFromLeft (dialW * 5 + 20);
        for (auto* d : { &m.lfoSpeed, &m.lfoDelay, &m.lfoPmd, &m.lfoAmd, &m.lfoPms }) d->setBounds (lfoArea.removeFromLeft (dialW));
        auto lfoOpts = top.removeFromLeft (160);
        m.lfoWave.setBounds (lfoOpts.removeFromTop (28).reduced (2));
        m.lfoSync.setBounds (lfoOpts.removeFromTop (28));
        top.removeFromLeft (20);
        m.pitchEg.setBounds (top.removeFromLeft (360).withHeight (dialH + 30));
        r.removeFromTop (34);
        auto row2 = r.removeFromTop (22);
        m.voicesLabel.setBounds (row2.removeFromLeft (200));
        auto voices = r.removeFromTop (dialH);
        m.polyBox.setBounds (voices.removeFromLeft (110).withTrimmedTop (28).withHeight (28).reduced (2));
        m.stealBox.setBounds (voices.removeFromLeft (130).withTrimmedTop (28).withHeight (28).reduced (2));
        for (auto* d : { &m.maxVoices, &m.glide, &m.bendRange, &m.wheelPitch, &m.wheelAmp, &m.unison, &m.transpose, &m.masterTune })
            d->setBounds (voices.removeFromLeft (dialW));
        r.removeFromTop (4);
        m.macrosLabel.setBounds (r.removeFromTop (22).removeFromLeft (300));
        r.removeFromTop (18);
        auto macros = r.removeFromTop (92);
        const int knob = 50;
        for (auto& mk : m.macros)
        {
            auto cell = macros.removeFromLeft (dialW);
            mk.knob->setBounds (cell.getCentreX() - knob / 2, cell.getY() + 6, knob, knob);
        }
    }
    if (m.modal) m.modal->setBounds (getLocalBounds());
}
