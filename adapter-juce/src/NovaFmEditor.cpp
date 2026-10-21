#include "NovaFmEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "neptune/core/FmEngine.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mx = neptune::core::mixer;

namespace
{
juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // opérateurs : deux boîtes reliées
    {
        p.addRectangle (3.0f, 3.0f, 8.0f, 7.0f);
        p.addRectangle (13.0f, 14.0f, 8.0f, 7.0f);
        p.startNewSubPath (7.0f, 10.0f); p.lineTo (7.0f, 17.0f); p.lineTo (13.0f, 17.0f);
    }
    else   // global : LFO
    {
        p.startNewSubPath (2.0f, 12.0f);
        p.cubicTo (6.0f, 2.0f, 9.0f, 2.0f, 12.0f, 12.0f);
        p.cubicTo (15.0f, 22.0f, 18.0f, 22.0f, 22.0f, 12.0f);
    }
    return p;
}

const char* const kWaveKeys[6] = { "fm.lfo_triangle", "fm.lfo_saw_down", "fm.lfo_saw_up", "fm.lfo_square", "fm.lfo_sine", "fm.lfo_sh" };
const char* const kCurveNames[4] = { "-LIN", "-EXP", "+EXP", "+LIN" };
} // namespace

// ------------------------------------------------------------------------------------------------ diagramme d'algorithme
class NovaFmEditor::AlgoView : public juce::Component
{
public:
    explicit AlgoView (const nova::Skin& s) : skin_ (s) {}
    std::function<void (int op)> onSelect, onToggle;

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
        const float s = skin_.scale();
        const auto& graph = fm::algorithmGraph (algorithm_);
        const auto lay = layout();
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (8.0f));
        g.setColour (skin_.colour (Token::TextDim));
        for (const auto& e : graph.edges)
        {
            const auto a = lay[static_cast<std::size_t> (e.from)].getCentre(), b = lay[static_cast<std::size_t> (e.to)].getCentre();
            g.drawLine (a.x * s, (a.y + boxH / 2.0f) * s, b.x * s, (b.y - boxH / 2.0f) * s, skin_.px (1.6f));
        }
        if (graph.feedbackFrom >= 0)
        {
            const auto from = lay[static_cast<std::size_t> (graph.feedbackFrom)], to = lay[static_cast<std::size_t> (graph.feedbackTo)];
            const float x = std::max (from.getRight(), to.getRight()) + 8.0f;
            juce::Path p;
            p.startNewSubPath (from.getRight() * s, from.getCentreY() * s);
            p.lineTo (x * s, from.getCentreY() * s);
            p.lineTo (x * s, to.getCentreY() * s);
            p.lineTo (to.getRight() * s, to.getCentreY() * s);
            g.setColour (skin_.colour (Token::Accent2));
            g.strokePath (p, juce::PathStrokeType (skin_.px (1.6f)));
            g.setFont (skin_.font (9.0f, true));
            g.drawText ("FB", juce::Rectangle<float> ((x + 2.0f) * s, ((from.getCentreY() + to.getCentreY()) * 0.5f - 5.0f) * s, 18.0f * s, 10.0f * s), juce::Justification::left);
        }
        const float H = static_cast<float> (getHeight()) / s;
        g.setColour (skin_.colour (Token::Accent));
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)];
            if (((graph.carrierMask >> op) & 1) != 0) g.drawLine (r.getCentreX() * s, r.getBottom() * s, r.getCentreX() * s, (H - 6.0f) * s, skin_.px (2.0f));
        }
        g.drawLine (4.0f * s, (H - 6.0f) * s, (static_cast<float> (getWidth()) / s - 4.0f) * s, (H - 6.0f) * s, skin_.px (2.0f));
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)] * s;
            const bool on = ((mask_ >> op) & 1) != 0, sel = op == selected_, carrier = ((graph.carrierMask >> op) & 1) != 0;
            skin_.fill (g, sel ? Token::Accent : Token::PanelBg, r, skin_.px (6.0f));
            if (sel) { g.setColour (skin_.colour (Token::BgApp).withAlpha (0.45f)); g.fillRoundedRectangle (r, skin_.px (6.0f)); }
            g.setColour (skin_.colour (carrier ? Token::Accent : Token::Grid));
            g.drawRoundedRectangle (r, skin_.px (6.0f), skin_.px (sel ? 2.4f : 1.3f));
            g.setColour (skin_.colour (Token::Text, on ? 1.0f : 0.35f));
            g.setFont (skin_.font (13.0f, true));
            g.drawText ("OP" + juce::String (fm::kOperators - op), r, juce::Justification::centred);
            g.setColour (on ? skin_.colour (Token::Accent) : skin_.colour (Token::TextDim, 0.4f));
            g.fillEllipse (r.getRight() - skin_.px (13.0f), r.getY() + skin_.px (3.0f), skin_.px (8.0f), skin_.px (8.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const float s = skin_.scale();
        const auto lay = layout();
        const auto pos = e.position / s;
        for (int op = 0; op < fm::kOperators; ++op)
        {
            const auto r = lay[static_cast<std::size_t> (op)];
            if (! r.contains (pos)) continue;
            const juce::Rectangle<float> dot (r.getRight() - 16.0f, r.getY(), 16.0f, 16.0f);
            if (dot.contains (pos) || e.mods.isRightButtonDown()) { if (onToggle) onToggle (op); }
            else if (onSelect) onSelect (op);
            return;
        }
    }

private:
    static constexpr float boxW = 58.0f, boxH = 30.0f;
    std::array<juce::Rectangle<float>, fm::kOperators> layout() const
    {
        const float s = skin_.scale(), W = static_cast<float> (getWidth()) / s, H = static_cast<float> (getHeight()) / s;
        const auto& graph = fm::algorithmGraph (algorithm_);
        std::array<int, fm::kOperators> level {};
        for (int pass = 0; pass < fm::kOperators; ++pass)
            for (const auto& e : graph.edges) level[static_cast<std::size_t> (e.from)] = std::max (level[static_cast<std::size_t> (e.from)], level[static_cast<std::size_t> (e.to)] + 1);
        int rows = 1;
        for (int l : level) rows = std::max (rows, l + 1);
        std::array<juce::Rectangle<float>, fm::kOperators> out;
        const float top = 8.0f, bottom = H - 18.0f;
        const float rowStep = rows > 1 ? (bottom - boxH - top) / static_cast<float> (rows - 1) : 0.0f;
        for (int row = 0; row < rows; ++row)
        {
            std::vector<int> ops;
            for (int op = fm::kOperators - 1; op >= 0; --op)   // OP1 à gauche
                if (level[static_cast<std::size_t> (op)] == row) ops.push_back (op);
            const float y = bottom - boxH - rowStep * static_cast<float> (row);
            const float cell = (W - 28.0f) / static_cast<float> (std::max<std::size_t> (ops.size(), 1));
            for (std::size_t i = 0; i < ops.size(); ++i)
                out[static_cast<std::size_t> (ops[i])] = juce::Rectangle<float> (6.0f + cell * static_cast<float> (i) + (cell - boxW) * 0.5f, y, boxW, boxH);
        }
        return out;
    }
    const nova::Skin& skin_;
    int algorithm_ = 0, selected_ = 5, mask_ = 0x3f;
};

// ------------------------------------------------------------------------------------------------ enveloppe 4 vitesses / 4 niveaux
class NovaFmEditor::EnvView : public juce::Component
{
public:
    explicit EnvView (const nova::Skin& s) : skin_ (s) {}
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
        const float s = skin_.scale();
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (8.0f));
        const auto pts = points();
        // niveau de repos et grille
        g.setColour (skin_.colour (Token::Grid));
        for (int q = 1; q < 4; ++q) g.drawHorizontalLine (juce::roundToInt (levelY (q * 25) * s), skin_.px (8.0f), static_cast<float> (getWidth()) - skin_.px (8.0f));
        juce::Path path;
        path.startNewSubPath (pts[0] * s);
        for (std::size_t i = 1; i < pts.size(); ++i) path.lineTo (pts[i] * s);
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (path, juce::PathStrokeType (skin_.px (2.2f)));
        static const char* const labels[4] = { "R1/L1", "R2/L2", "R3/L3", "R4/L4" };
        g.setFont (skin_.font (10.0f, true));
        for (int h = 0; h < 4; ++h)
        {
            const auto p = handlePoint (h) * s;
            g.setColour (skin_.colour (Token::Hring));
            g.fillEllipse (p.x - skin_.px (7.0f), p.y - skin_.px (7.0f), skin_.px (14.0f), skin_.px (14.0f));
            g.setColour (skin_.colour (Token::Handle));
            g.fillEllipse (p.x - skin_.px (5.0f), p.y - skin_.px (5.0f), skin_.px (10.0f), skin_.px (10.0f));
            g.setColour (skin_.colour (h == dragging_ ? Token::Text : Token::TextDim));
            g.drawText (labels[h], juce::Rectangle<float> (p.x - skin_.px (24.0f), p.y - skin_.px (24.0f), skin_.px (48.0f), skin_.px (14.0f)), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.position / skin_.scale();
        dragging_ = -1;
        float best = 16.0f;
        for (int h = 0; h < 4; ++h)
        {
            const float d = handlePoint (h).getDistanceFrom (pos);
            if (d < best) { best = d; dragging_ = h; }
        }
        if (dragging_ >= 0 && onBegin) onBegin();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging_ < 0) return;
        const auto pos = e.position / skin_.scale();
        const int h = dragging_;
        const float H = static_cast<float> (getHeight()) / skin_.scale() - 20.0f;
        levels_[static_cast<std::size_t> (h)] = std::clamp (static_cast<int> (std::lround ((1.0f - (pos.y - 10.0f) / H) * 99.0f)), 0, 99);
        const auto prevX = h == 0 ? 10.0f : handlePoint (h - 1).x;   // la position horizontale règle la vitesse du segment qui mène à ce point
        rates_[static_cast<std::size_t> (h)] = std::clamp (99 - static_cast<int> (std::lround ((pos.x - prevX - kMinSeg) / segScale())), 0, 99);
        if (onChange) onChange (rates_, levels_);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging_ >= 0 && onEnd) onEnd();
        dragging_ = -1;
        repaint();
    }

private:
    static constexpr float kMinSeg = 14.0f, kSustain = 26.0f;
    // largeur utile adaptée à la vue : 3 segments + maintien + segment de relâchement tiennent dans la largeur
    float segScale() const
    {
        const float W = static_cast<float> (getWidth()) / skin_.scale();
        return std::max (0.6f, (W - 20.0f - kSustain - 4.0f * kMinSeg) / (4.0f * 99.0f));
    }
    float levelY (int level) const
    {
        const float H = static_cast<float> (getHeight()) / skin_.scale() - 20.0f;
        return 10.0f + H * (1.0f - static_cast<float> (level) / 99.0f);
    }
    float segWidth (int h) const { return kMinSeg + static_cast<float> (99 - rates_[static_cast<std::size_t> (h)]) * segScale(); }
    std::array<juce::Point<float>, 6> points() const   // départ (L4), L1, L2, L3, fin du maintien, L4
    {
        std::array<juce::Point<float>, 6> p;
        float x = 10.0f;
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
    const nova::Skin& skin_;
    std::array<int, 4> rates_ { 99, 99, 99, 99 }, levels_ { 99, 99, 99, 0 };
    int dragging_ = -1;
};

// ------------------------------------------------------------------------------------------------ mise à l'échelle du clavier
class NovaFmEditor::ScaleView : public juce::Component
{
public:
    explicit ScaleView (const nova::Skin& s) : skin_ (s) {}
    void setPatch (const fm::Patch& patch, int op)
    {
        if (patch_ == patch && op_ == op) return;
        patch_ = patch;
        op_ = op;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (8.0f));
        const auto b = getLocalBounds().toFloat().reduced (skin_.px (8.0f));
        g.setColour (skin_.colour (Token::Grid));
        g.drawHorizontalLine (static_cast<int> (b.getCentreY()), b.getX(), b.getRight());
        const int breakNote = std::clamp (patch_.op (op_, fm::kBreakPoint) + 17, 0, 127);   // l'échelle de msfa part de rupture + 17
        const float bx = b.getX() + b.getWidth() * static_cast<float> (breakNote) / 127.0f;
        g.setColour (skin_.colour (Token::TextDim));
        g.drawVerticalLine (static_cast<int> (bx), b.getY(), b.getBottom());
        juce::Path p;
        for (int n = 0; n <= 127; ++n)
        {
            const float v = static_cast<float> (fm::keyboardLevelScaling (patch_, op_, n));
            const float y = b.getCentreY() - juce::jlimit (-1.0f, 1.0f, v / 60.0f) * (b.getHeight() * 0.5f);
            const float x = b.getX() + b.getWidth() * static_cast<float> (n) / 127.0f;
            if (n == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (skin_.px (2.2f)));
    }

private:
    const nova::Skin& skin_;
    fm::Patch patch_ = fm::Patch::initVoice();
    int op_ = 5;
};

// ------------------------------------------------------------------------------------------------ fabriques typées sur la description
void NovaFmEditor::edit (const std::function<void (Desc&)>& change, const char* name, const char* mergeKey)
{
    auto next = desc_;
    change (next);
    next = fm::sanitised (std::move (next));
    if (next == desc_) return;
    desc_ = next;
    apply_ (next, name, mergeKey);
}

nova::ValueSlider* NovaFmEditor::slider (int page, juce::String title, double lo, double hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set, int def,
                                         std::function<bool (const Desc&)> enabled, std::function<juce::String (double)> format)
{
    auto* s = sliderFn (page, std::move (title), lo, hi, 1.0, {}, [this, get] { return static_cast<double> (get (desc_)); },
                        [this, set] (double v) { const int i = static_cast<int> (std::lround (v)); edit ([&] (Desc& d) { set (d, i); }); }, def,
                        enabled ? std::function<bool()> ([this, enabled] { return enabled (desc_); }) : std::function<bool()> {});
    if (format) s->format = std::move (format);
    return s;
}

nova::NumberPill* NovaFmEditor::number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                                        std::function<juce::String (int)> text)
{
    return numberFn (page, std::move (label), lo, hi, [this, get] { return get (desc_); }, [this, set] (int v) { edit ([&] (Desc& d) { set (d, v); }); }, {}, std::move (text));
}

nova::ChoicePill* NovaFmEditor::choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set)
{
    return choiceFn (page, std::move (label), std::move (items), [this, get] { return get (desc_); }, [this, set] (int v) { edit ([&] (Desc& d) { set (d, v); }); });
}

nova::Pill* NovaFmEditor::toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set)
{
    return toggleFn (page, std::move (text), [this, get] { return get (desc_); }, [this, set] (bool b) { edit ([&] (Desc& d) { set (d, b); }); });
}

// réglage d'un champ de l'opérateur choisi
nova::ValueSlider* NovaFmEditor::opSlider (juce::String title, int field, int hi, int def, std::function<juce::String (double)> format)
{
    return slider (0, std::move (title), 0, hi, [this, field] (const Desc& d) { return d.patch.op (selectedOp_, field); },
                   [this, field] (Desc& d, int v) { d.patch.setOp (selectedOp_, field, v); }, def, {}, std::move (format));
}

// ------------------------------------------------------------------------------------------------ construction
NovaFmEditor::NovaFmEditor (ParamHost& host, PresetHost& presets, FmLink link, const theme::Palette& palette, theme::KnobStyle knobStyle, const i18n::Translator& tr, Options options)
    : NovaInstrumentBase (host, presets, link, palette, knobStyle, tr, std::move (options), juce::String::fromUTF8 ("FM6OP")), describe_ (link.description), apply_ (link.edit)
{
    desc_ = fm::sanitised (describe_());
    sysex_ = std::make_unique<FmSysexIO> (*this, presets, tr);
    addPage (tx ("fm.tab_operators").toUpperCase(), iconPath (0));
    addPage (tx ("fm.tab_global").toUpperCase(), iconPath (1));
    buildCommon ("hlp.fm_out");   // MATRICE, SETTINGS, colonne de gauche, clavier
    buildOperatorPage();
    buildGlobalPage();
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaFmEditor::~NovaFmEditor() = default;

void NovaFmEditor::buildOperatorPage()
{
    algoPanel_ = add<nova::Panel> (0, skin());
    algoPanel_->setTitle (tx ("fm.algorithm").toUpperCase());
    addInfo (0, algoPanel_, "hlp.fm_algo", 1);
    opPanel_ = add<nova::Panel> (0, skin());
    addInfo (0, opPanel_, "hlp.fm_op", 1);

    algo_ = add<AlgoView> (0, skin());
    algo_->onSelect = [this] (int op) { selectedOp_ = op; };
    algo_->onToggle = [this] (int op)
    {
        gestureBegin();
        edit ([op] (Desc& d) { d.patch.setOperatorOn (op, ! d.patch.operatorOn (op)); });
        gestureEnd();
    };
    algoPrev_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    algoPrev_->setText ("<");
    algoPrev_->onClick = [this] { gestureBegin(); edit ([] (Desc& d) { d.patch.setGlobal (fm::kAlgorithmOffset, std::max (0, d.patch.algorithm() - 1)); }); gestureEnd(); };
    algoNext_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    algoNext_->setText (">");
    algoNext_->onClick = [this] { gestureBegin(); edit ([] (Desc& d) { d.patch.setGlobal (fm::kAlgorithmOffset, std::min (fm::kNumAlgorithms - 1, d.patch.algorithm() + 1)); }); gestureEnd(); };
    algoNumber_ = number (0, tx ("fm.algorithm").toUpperCase(), 1, fm::kNumAlgorithms, [] (const Desc& d) { return d.patch.algorithm() + 1; },
                          [] (Desc& d, int v) { d.patch.setGlobal (fm::kAlgorithmOffset, v - 1); });
    feedback_ = slider (0, tx ("fm.feedback").toUpperCase(), 0, 7, [] (const Desc& d) { return d.patch.feedback(); }, [] (Desc& d, int v) { d.patch.setGlobal (fm::kFeedbackOffset, v); }, 0);
    addInfo (0, feedback_, "hlp.fm_feedback", 1);
    keySync_ = toggle (0, tx ("fm.key_sync"), [] (const Desc& d) { return d.patch.global (136) != 0; }, [] (Desc& d, bool b) { d.patch.setGlobal (136, b ? 1 : 0); });
    addInfo (0, keySync_, "hlp.fm_keysync", 1);

    // nom du patch, import / export sysex
    nameText_ = add<nova::Text> (0, skin(), Token::TextDim2, 11.0f, true);
    nameText_->setText (tx ("fm.patch_name").toUpperCase());
    name_ = add<juce::TextEditor> (0);
    name_->setMultiLine (false);
    name_->setInputRestrictions (fm::kNameLength);
    name_->onReturnKey = [this] { gestureBegin(); edit ([s = name_->getText().toStdString()] (Desc& d) { d.patch.setName (s); }); gestureEnd(); };
    name_->onFocusLost = [this] { if (name_->getText().toStdString() != desc_.patch.name()) name_->onReturnKey(); };
    onUpdate (0, [this]
    {
        if (! name_->hasKeyboardFocus (true)) { const auto n = juce::String::fromUTF8 (desc_.patch.name().c_str()); if (name_->getText() != n) name_->setText (n, false); }
    });
    importPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    importPill_->setText (tx ("fm.import"));
    importPill_->onClick = [this]
    {
        sysex_->importFile ([this] (const fm::Patch& patch)
        {
            gestureBegin();
            edit ([&patch] (Desc& d) { d.patch = patch; }, "Import DX7 voice", "");
            gestureEnd();
        });
    };
    exportVoicePill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    exportVoicePill_->setText (tx ("fm.export_voice"));
    exportVoicePill_->onClick = [this] { sysex_->exportVoice (desc_.patch); };
    exportBankPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    exportBankPill_->setText (tx ("fm.export_bank"));
    exportBankPill_->onClick = [this] { sysex_->exportBank(); };
    addInfo (0, nameText_, "hlp.fm_patch");

    // opérateur choisi
    opOn_ = toggle (0, tx ("filt.on"), [this] (const Desc& d) { return d.patch.operatorOn (selectedOp_); },
                    [this] (Desc& d, bool b) { d.patch.setOperatorOn (selectedOp_, b); });
    mode_ = choice (0, {}, { tx ("fm.mode_ratio"), tx ("fm.mode_fixed") }, [this] (const Desc& d) { return d.patch.op (selectedOp_, fm::kMode); },
                    [this] (Desc& d, int v) { d.patch.setOp (selectedOp_, fm::kMode, v); });
    addInfo (0, mode_->pill, "hlp.fm_mode", 1);
    freqText_ = add<nova::Text> (0, skin(), Token::Accent2, 13.0f, true);
    auto signedNote = [] (double v) { return juce::String (static_cast<int> (std::lround (v)) - 7); };
    const struct { const char* key; int field; int hi; int def; const char* help; bool detune; } ops[] = {
        { "fm.coarse", fm::kCoarse, 31, 1, "hlp.fm_coarse", false }, { "fm.fine", fm::kFine, 99, 0, "hlp.fm_fine", false }, { "fm.detune", fm::kDetune, 14, 7, "hlp.fm_detune", true },
        { "fm.level", fm::kOutputLevel, 99, 0, "hlp.fm_level", false }, { "fm.vel_sens", fm::kVelocitySens, 7, 0, "hlp.fm_velsens", false },
        { "fm.rate_scaling", fm::kRateScaling, 7, 0, "hlp.fm_ratescl", false }, { "fm.ams", fm::kAmpModSens, 3, 0, "hlp.fm_ams", false } };
    for (const auto& o : ops)
    {
        auto* s = opSlider (tx (o.key), o.field, o.hi, o.def, o.detune ? std::function<juce::String (double)> (signedNote) : std::function<juce::String (double)> {});
        addInfo (0, s, o.help, 1);
        opSliders_.push_back (s);
    }
    opEg_ = add<EnvView> (0, skin());
    opEg_->onBegin = [this] { gestureBegin(); };
    opEg_->onEnd = [this] { gestureEnd(); };
    opEg_->onChange = [this] (const std::array<int, 4>& r, const std::array<int, 4>& l)
    {
        edit ([&] (Desc& d)
        {
            for (int i = 0; i < 4; ++i)
            {
                d.patch.setOp (selectedOp_, fm::kRate1 + i, r[static_cast<std::size_t> (i)]);
                d.patch.setOp (selectedOp_, fm::kLevel1 + i, l[static_cast<std::size_t> (i)]);
            }
        });
    };
    addInfo (0, opEg_, "hlp.fm_eg", 1);
    scaleText_ = add<nova::Text> (0, skin(), Token::TextDim2, 11.0f, true);
    scaleText_->setText (tx ("fm.scaling_short"));
    addInfo (0, scaleText_, "hlp.fm_scaling");
    const struct { const char* key; int field; } sc[] = { { "fm.break_point", fm::kBreakPoint }, { "fm.left_depth", fm::kLeftDepth }, { "fm.right_depth", fm::kRightDepth } };
    for (const auto& c : sc) scaleSliders_.push_back (opSlider (tx (c.key), c.field, 99, c.field == fm::kBreakPoint ? 39 : 0));
    std::vector<juce::String> curves;
    for (const char* c : kCurveNames) curves.push_back (c);
    leftCurve_ = choice (0, "L", curves, [this] (const Desc& d) { return d.patch.op (selectedOp_, fm::kLeftCurve); }, [this] (Desc& d, int v) { d.patch.setOp (selectedOp_, fm::kLeftCurve, v); });
    rightCurve_ = choice (0, "R", curves, [this] (const Desc& d) { return d.patch.op (selectedOp_, fm::kRightCurve); }, [this] (Desc& d, int v) { d.patch.setOp (selectedOp_, fm::kRightCurve, v); });
    scale_ = add<ScaleView> (0, skin());

    onUpdate (0, [this]
    {
        const auto& p = desc_.patch;
        int mask = 0;
        for (int op = 0; op < fm::kOperators; ++op) mask |= p.operatorOn (op) ? (1 << op) : 0;
        algo_->setState (p.algorithm(), selectedOp_, mask);
        opPanel_->setTitle ((tx ("fm.operator") + " OP" + juce::String (fm::kOperators - selectedOp_)).toUpperCase());
        std::array<int, 4> r {}, l {};
        for (int i = 0; i < 4; ++i) { r[static_cast<std::size_t> (i)] = p.op (selectedOp_, fm::kRate1 + i); l[static_cast<std::size_t> (i)] = p.op (selectedOp_, fm::kLevel1 + i); }
        opEg_->setValues (r, l);
        scale_->setPatch (p, selectedOp_);
        const int coarse = p.op (selectedOp_, fm::kCoarse), fine = p.op (selectedOp_, fm::kFine);
        if (p.op (selectedOp_, fm::kMode) == 0)
            freqText_->setText (tx ("fm.ratio") + " " + juce::String ((coarse == 0 ? 0.5 : coarse) * (1.0 + fine / 100.0), 3));
        else
            freqText_->setText (tx ("fm.fixed") + " " + juce::String (std::pow (10.0, (coarse & 3) + fine * 0.01), 1) + " Hz");
    });
}

void NovaFmEditor::buildGlobalPage()
{
    lfoPanel_ = add<nova::Panel> (1, skin());
    lfoPanel_->setTitle (tx ("fm.lfo"));
    addInfo (1, lfoPanel_, "hlp.fm_lfo", 1);
    const struct { const char* key; int index; int hi; int def; } lfo[] = { { "fm.lfo_speed", 137, 99, 35 }, { "fm.lfo_delay", 138, 99, 0 }, { "fm.lfo_pmd", 139, 99, 0 },
                                                                            { "fm.lfo_amd", 140, 99, 0 }, { "fm.lfo_pms", 143, 7, 3 } };
    for (const auto& c : lfo)
        lfoSliders_.push_back (slider (1, tx (c.key), 0, c.hi, [i = c.index] (const Desc& d) { return d.patch.global (i); }, [i = c.index] (Desc& d, int v) { d.patch.setGlobal (i, v); }, c.def));
    std::vector<juce::String> waves;
    for (const char* k : kWaveKeys) waves.push_back (tx (k));
    lfoWave_ = choice (1, {}, waves, [] (const Desc& d) { return d.patch.global (142); }, [] (Desc& d, int v) { d.patch.setGlobal (142, v); });
    lfoSync_ = toggle (1, tx ("fm.lfo_sync"), [] (const Desc& d) { return d.patch.global (141) != 0; }, [] (Desc& d, bool b) { d.patch.setGlobal (141, b ? 1 : 0); });

    pitchPanel_ = add<nova::Panel> (1, skin());
    pitchPanel_->setTitle (tx ("fm.pitch_eg").toUpperCase());
    addInfo (1, pitchPanel_, "hlp.fm_pitcheg", 1);
    pitchEg_ = add<EnvView> (1, skin());
    pitchEg_->onBegin = [this] { gestureBegin(); };
    pitchEg_->onEnd = [this] { gestureEnd(); };
    pitchEg_->onChange = [this] (const std::array<int, 4>& r, const std::array<int, 4>& l)
    {
        edit ([&] (Desc& d)
        {
            for (int i = 0; i < 4; ++i)
            {
                d.patch.setGlobal (fm::kPitchEgOffset + i, r[static_cast<std::size_t> (i)]);
                d.patch.setGlobal (fm::kPitchEgOffset + 4 + i, l[static_cast<std::size_t> (i)]);
            }
        });
    };

    voicesPanel_ = add<nova::Panel> (1, skin());
    voicesPanel_->setTitle (tx ("fm.voices").toUpperCase());
    addInfo (1, voicesPanel_, "hlp.fm_voices", 1);
    polyMode_ = choice (1, {}, { tx ("fm.poly"), tx ("fm.mono"), tx ("fm.legato") }, [] (const Desc& d) { return d.polyphony == PolyphonyMode::Mono ? 1 : d.polyphony == PolyphonyMode::Legato ? 2 : 0; },
                        [] (Desc& d, int v) { d.polyphony = v == 1 ? PolyphonyMode::Mono : v == 2 ? PolyphonyMode::Legato : PolyphonyMode::Poly; });
    steal_ = choice (1, {}, { tx ("fm.steal_oldest"), tx ("fm.steal_quietest"), tx ("fm.steal_keep_low"), tx ("fm.steal_keep_high") },
                     [] (const Desc& d) { return d.stealMode == VoiceStealMode::Quietest ? 1 : d.stealMode == VoiceStealMode::KeepLow ? 2 : d.stealMode == VoiceStealMode::KeepHigh ? 3 : 0; },
                     [] (Desc& d, int v) { d.stealMode = v == 1 ? VoiceStealMode::Quietest : v == 2 ? VoiceStealMode::KeepLow : v == 3 ? VoiceStealMode::KeepHigh : VoiceStealMode::Oldest; });
    voiceSliders_.push_back (slider (1, tx ("fm.max_voices"), 1, fm::kMaxVoices, [] (const Desc& d) { return d.maxVoices; }, [] (Desc& d, int v) { d.maxVoices = v; }, 16));
    voiceSliders_.push_back (slider (1, tx ("fm.glide"), 0, fm::kMaxGlideMs, [] (const Desc& d) { return d.glideMs; }, [] (Desc& d, int v) { d.glideMs = v; }, 0));
    voiceSliders_.push_back (slider (1, tx ("fm.unison"), 1, fm::kMaxUnison, [] (const Desc& d) { return d.unisonVoices; }, [] (Desc& d, int v) { d.unisonVoices = v; }, 1));

    tunePanel_ = add<nova::Panel> (1, skin());
    tunePanel_->setTitle (juce::String (tx ("fm.transpose") + " / " + tx ("fm.bend_range")).toUpperCase());
    addInfo (1, tunePanel_, "hlp.fm_tuning", 1);
    tuneSliders_.push_back (slider (1, tx ("fm.transpose"), 0, 48, [] (const Desc& d) { return d.patch.global (fm::kTransposeOffset); },
                                    [] (Desc& d, int v) { d.patch.setGlobal (fm::kTransposeOffset, v); }, 24, {}, [] (double v) { return juce::String (static_cast<int> (std::lround (v)) - 24); }));
    tuneSliders_.push_back (slider (1, tx ("fm.master_tune"), -100, 100, [] (const Desc& d) { return d.masterTuneCents; }, [] (Desc& d, int v) { d.masterTuneCents = v; }, 0));
    tuneSliders_.push_back (slider (1, tx ("fm.bend_range"), 0, 12, [] (const Desc& d) { return d.pitchBendRange; }, [] (Desc& d, int v) { d.pitchBendRange = v; }, 2));
    tuneSliders_.push_back (slider (1, tx ("fm.wheel_pitch"), 0, 15, [] (const Desc& d) { return d.wheelPitchSens; }, [] (Desc& d, int v) { d.wheelPitchSens = v; }, 0));
    tuneSliders_.push_back (slider (1, tx ("fm.wheel_amp"), 0, 15, [] (const Desc& d) { return d.wheelAmpSens; }, [] (Desc& d, int v) { d.wheelAmpSens = v; }, 0));

    macrosPanel_ = add<nova::Panel> (1, skin());
    macrosPanel_->setTitle (tx ("fm.macros").toUpperCase());
    addInfo (1, macrosPanel_, "hlp.fm_macros", 1);
    const struct { mx::FmKind kind; const char* key; } defs[] = { { mx::kFmBrightness, "fm.brightness" }, { mx::kFmEnvSpeed, "fm.env_speed" }, { mx::kFmFeedbackDrive, "fm.feedback_drive" },
                                                                 { mx::kFmLevelOp1, "fm.op1" }, { mx::kFmLevelOp2, "fm.op2" }, { mx::kFmLevelOp3, "fm.op3" }, { mx::kFmLevelOp4, "fm.op4" },
                                                                 { mx::kFmLevelOp5, "fm.op5" }, { mx::kFmLevelOp6, "fm.op6" }, { mx::kFmUnisonDetune, "fm.unison_detune" },
                                                                 { mx::kFmUnisonSpread, "fm.unison_spread" } };
    for (const auto& m : defs) macros_.push_back (knob (1, mx::fmParam (link_.track, m.kind), tx (m.key), true));

    onUpdate (1, [this]
    {
        std::array<int, 4> r {}, l {};
        for (int i = 0; i < 4; ++i) { r[static_cast<std::size_t> (i)] = desc_.patch.global (fm::kPitchEgOffset + i); l[static_cast<std::size_t> (i)] = desc_.patch.global (fm::kPitchEgOffset + 4 + i); }
        pitchEg_->setValues (r, l);
    });
}

// ------------------------------------------------------------------------------------------------ mise en page
void NovaFmEditor::layout()
{
    const float H = contentH(), K = stretch();
    layoutCommon();

    // ---- page OPÉRATEURS
    algoPanel_->setBounds (R (X0, 72, 330, H));
    algo_->setBounds (R (X0 + 12, Y (36), 306, 240.0f * K));
    {
        const float ay = Y (36) + 240.0f * K + 10.0f, fy = ay + 40.0f;   // rangées sous le diagramme (ordonnées absolues de la maquette)
        algoPrev_->setBounds (R (X0 + 12, ay, 36, 28));
        algoNumber_->setBounds (R (X0 + 54, ay, 222, 28));
        algoNext_->setBounds (R (X0 + 282, ay, 36, 28));
        feedback_->setBounds (R (X0 + 12, fy, 140, 46));
        keySync_->setBounds (R (X0 + 164, fy + 14, 154, 28));
        nameText_->setBounds (R (X0 + 12, fy + 56, 200, 16));
        name_->setBounds (R (X0 + 12, fy + 76, 306, 30));
        importPill_->setBounds (R (X0 + 12, fy + 114, 148, 28));
        exportVoicePill_->setBounds (R (X0 + 170, fy + 114, 148, 28));
        exportBankPill_->setBounds (R (X0 + 12, fy + 148, 306, 28));
    }
    opPanel_->setBounds (R (X0 + 342, 72, 600, H));
    mode_->pill->setBounds (R (X0 + 354, Y (36), 170, 28));
    freqText_->setBounds (R (X0 + 536, Y (40), 250, 20));
    opOn_->setBounds (R (X0 + 342 + 600 - 12 - 70, Y (36), 70, 28));
    for (std::size_t i = 0; i < opSliders_.size(); ++i) opSliders_[i]->setBounds (R (X0 + 354.0f + static_cast<float> (i) * 83.0f, Y (78), 78, 46));
    opEg_->setBounds (R (X0 + 354, Y (140), 336, 360.0f * K - 8.0f));
    scaleText_->setBounds (R (X0 + 702, Y (140), 220, 16));
    for (std::size_t i = 0; i < scaleSliders_.size(); ++i) scaleSliders_[i]->setBounds (R (X0 + 702.0f + static_cast<float> (i) * 76.0f, Y (162), 72, 46));
    leftCurve_->pill->setBounds (R (X0 + 702, Y (222), 108, 28));
    rightCurve_->pill->setBounds (R (X0 + 816, Y (222), 108, 28));
    scale_->setBounds (R (X0 + 702, Y (262), 228, 140.0f * K));

    // ---- page GLOBAL
    lfoPanel_->setBounds (R (X0, Y (0), 465, 190.0f * K));
    for (std::size_t i = 0; i < lfoSliders_.size(); ++i) lfoSliders_[i]->setBounds (R (X0 + 12.0f + static_cast<float> (i) * 89.0f, Y (40), 84, 46));
    lfoWave_->pill->setBounds (R (X0 + 12, Y (116), 230, 28));
    lfoSync_->setBounds (R (X0 + 252, Y (116), 130, 28));
    pitchPanel_->setBounds (R (X0 + 477, Y (0), 465, 190.0f * K));
    pitchEg_->setBounds (R (X0 + 489, Y (36), 441, 140.0f * K));
    voicesPanel_->setBounds (R (X0, Y (200), 465, 152.0f * K));
    polyMode_->pill->setBounds (R (X0 + 12, Y (236), 190, 28));
    steal_->pill->setBounds (R (X0 + 212, Y (236), 241, 28));
    for (std::size_t i = 0; i < voiceSliders_.size(); ++i) voiceSliders_[i]->setBounds (R (X0 + 12.0f + static_cast<float> (i) * 150.0f, Y (284), 140, 46));
    tunePanel_->setBounds (R (X0 + 477, Y (200), 465, 152.0f * K));
    for (std::size_t i = 0; i < tuneSliders_.size(); ++i)
        tuneSliders_[i]->setBounds (R (X0 + 489.0f + static_cast<float> (i % 3) * 150.0f, Y (236.0f + static_cast<float> (i / 3) * 58.0f), 140, 46));
    macrosPanel_->setBounds (R (X0, Y (362), 942, 150.0f * K));
    for (std::size_t i = 0; i < macros_.size(); ++i) macros_[i]->setBounds (R (X0 + 12.0f + static_cast<float> (i) * 84.0f, Y (362) + 36.0f, 80, 90));
}

void NovaFmEditor::tick()
{
    if (! dragging())
    {
        const auto d = fm::sanitised (describe_());
        if (! (d == desc_)) desc_ = d;
    }
    tickCommon();
}
