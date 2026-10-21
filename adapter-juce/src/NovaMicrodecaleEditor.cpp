#include "NovaMicrodecaleEditor.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/MicrodecalePresets.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace md = neptune::core::microdecale;

namespace
{
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // deux barres décalées
    {
        p.startNewSubPath (7.0f, 20.0f); p.lineTo (7.0f, 6.0f);
        p.startNewSubPath (16.0f, 4.0f); p.lineTo (16.0f, 18.0f);
    }
    else if (kind == 1)   // curseurs
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (14.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (6.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (12.0f, 16.0f, 4.0f, 4.0f);
    }
    else
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    return p;
}

void setPlain (ParamHost& host, ParamId id, double v)
{
    if (const auto* spec = host.parameters().find (id)) host.uiSetNormalised (id, spec->toNormalized (v));
}
} // namespace

// ------------------------------------------------------------------------------------------------ vue des deux voies sur l'axe du temps
class NovaMicrodecaleEditor::MicroView : public juce::Component
{
public:
    MicroView (const nova::Skin& s, ParamHost& h) : skin_ (s), host_ (h) {}

    void update()
    {
        std::uint32_t key = 0;
        for (ParamId id = md::mode; id <= md::panDryR; ++id) key = key * 31u + host_.parameters().version (id);
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        const auto plot = plotArea();
        const float centre = plot.getCentreY(), half = plot.getHeight() * 0.5f * 0.9f;
        const double tm = tMax();
        double step = 10.0;
        for (const double s : { 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 500.0 }) { step = s; if (tm / s <= 9.0) break; }
        g.setFont (skin_.font (9.0f));
        for (double t = 0.0; t <= tm + 1.0e-6; t += step)
        {
            const float x = tToX (t);
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (x - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (static_cast<int> (t)) + " ms", juce::Rectangle<float> (x - skin_.px (24.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        g.setColour (skin_.colour (Token::Grid));
        g.fillRect (plot.getX(), centre - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        g.drawText ("L", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY(), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getBottom() - skin_.px (16.0f), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        // l'impulsion d'entrée
        g.setColour (skin_.colour (Token::Handle, 0.9f));
        g.fillRect (tToX (0.0), plot.getY(), std::max (2.0f, skin_.px (2.0f)), plot.getHeight());
        const double base = MicrodecaleModule::shifterDelayMs (get (md::splice));
        for (int v = 0; v < 2; ++v)
        {
            const bool up = v == 0;   // la voie gauche au-dessus de la ligne, la droite en dessous
            const double delay = get (v == 0 ? md::delayL : md::delayR), fb = std::fabs (get (v == 0 ? md::feedbackL : md::feedbackR)) * 0.01;
            const double level = std::fabs (get (v == 0 ? md::levelShiftL : md::levelShiftR)) * 0.01;
            const double dryLevel = std::fabs (get (v == 0 ? md::levelDryL : md::levelDryR)) * 0.01;
            const Token tok = v == 0 ? Token::Accent : Token::Pink;
            // répétitions du feedback (chaque tour repasse par le décaleur : retard du décaleur + retard)
            for (int r = 0; r < 24; ++r)
            {
                const double amp = level * std::pow (fb, r);
                const double t = (base + delay) * (r + 1);
                if (t > tm || amp < 0.01) break;
                const float h = static_cast<float> (std::min (1.0, amp)) * half, w = skin_.px (r == 0 ? 5.0f : 3.0f);
                g.setColour (skin_.colour (tok, r == 0 ? 1.0f : 0.38f));
                g.fillRoundedRectangle (tToX (t) - w * 0.5f, up ? centre - h : centre, w, h, w * 0.4f);
            }
            if (dryLevel > 0.01)   // prise de retard du signal sec : au retard réglé, sans le retard du décaleur
            {
                const float h = static_cast<float> (std::min (1.0, dryLevel)) * half, w = skin_.px (3.0f);
                g.setColour (skin_.colour (Token::TextDim, 0.8f));
                g.fillRoundedRectangle (tToX (delay + 0.5) - w * 0.5f, up ? centre - h : centre, w, h, w * 0.4f);
            }
            // hauteur de la voie, au-dessus de sa barre
            const double cents = get (v == 0 ? md::shiftL : md::shiftR);
            const float x = tToX (base + delay);
            g.setColour (skin_.colour (tok));
            g.setFont (skin_.font (11.0f, true));
            const juce::String text = (cents >= 0.0 ? "+" : "") + juce::String (cents, 1) + " c";
            const float h = static_cast<float> (std::min (1.0, level)) * half;
            g.drawText (text, juce::Rectangle<float> (x - skin_.px (40.0f), up ? centre - h - skin_.px (16.0f) : centre + h + skin_.px (2.0f), skin_.px (80.0f), skin_.px (14.0f)), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (hit (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag_ = e.mods.isPopupMenu() ? -1 : hit (e.position);
        if (drag_ < 0) return;
        frozenMax_ = tMax();
        host_.uiBeginEdit (drag_ == 0 ? md::delayL : md::delayR);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag_ < 0) return;
        const double base = MicrodecaleModule::shifterDelayMs (get (md::splice));
        setPlain (host_, drag_ == 0 ? md::delayL : md::delayR, std::clamp (xToT (e.position.x) - base, 0.0, md::maxDelayMs));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag_ >= 0) host_.uiEndEdit (drag_ == 0 ? md::delayL : md::delayR);
        drag_ = -1;
        frozenMax_ = 0.0;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int v = hit (e.position);
        if (v >= 0) host_.uiResetToDefault (v == 0 ? md::delayL : md::delayR);
    }

private:
    double get (ParamId id) const { return host_.parameters().get (id); }
    double tMax() const
    {
        if (drag_ >= 0 && frozenMax_ > 0.0) return frozenMax_;
        const double base = MicrodecaleModule::shifterDelayMs (get (md::splice));
        return std::max (60.0, (base + std::max (get (md::delayL), get (md::delayR))) * 1.35);
    }
    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (36.0f), b.getY() + skin_.px (12.0f), b.getWidth() - skin_.px (36.0f + 14.0f), b.getHeight() - skin_.px (12.0f + 24.0f) };
    }
    float tToX (double ms) const { const auto p = plotArea(); return p.getX() + static_cast<float> (ms / tMax()) * p.getWidth(); }
    double xToT (float x) const { const auto p = plotArea(); return std::clamp (static_cast<double> ((x - p.getX()) / p.getWidth()), 0.0, 1.0) * tMax(); }
    int hit (juce::Point<float> p) const
    {
        const double base = MicrodecaleModule::shifterDelayMs (get (md::splice));
        const auto plot = plotArea();
        int best = -1;
        float bestD = skin_.px (12.0f);
        for (int v = 0; v < 2; ++v)
        {
            const bool inHalf = v == 0 ? p.y <= plot.getCentreY() + skin_.px (4.0f) : p.y >= plot.getCentreY() - skin_.px (4.0f);
            const float d = std::fabs (p.x - tToX (base + get (v == 0 ? md::delayL : md::delayR)));
            if (inHalf && d < bestD) { bestD = d; best = v; }
        }
        return best;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    std::uint32_t key_ = 0xFFFFFFFFu;
    int drag_ = -1;
    double frozenMax_ = 0.0;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaMicrodecaleEditor::NovaMicrodecaleEditor (ParamHost& host, const MicrodecaleModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                              const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.microdecale")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("md.page_micro"), iconPath (0));
    addPage (tx ("md.page_voices"), iconPath (1));
    addPage (tx ("nv.page_settings"), iconPath (2));
    buildMicroPage();
    buildVoicesPage();
    buildSettingsPage (2);
    if (const auto names = presetNames(); ! names.empty()) presetPill()->setText (names[0]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaMicrodecaleEditor::~NovaMicrodecaleEditor() = default;

std::vector<juce::String> NovaMicrodecaleEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : md::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}

std::vector<std::pair<ParamId, double>> NovaMicrodecaleEditor::presetValues (int i) const { return md::factoryPresets()[static_cast<std::size_t> (i)].values; }

std::vector<ParamId> NovaMicrodecaleEditor::presetParamIds() const { return md::presetParams(); }

void NovaMicrodecaleEditor::buildMicroPage()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    addInfo (-1, metersPanel_, "hlp.md_meters", true);
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    viewPanel_ = add<nova::Panel> (0, skin());
    viewPanel_->setTitle (tx ("md.view_title"));
    view_ = add<MicroView> (0, skin(), host());
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, outMeter_ }) m->setModeVisible (false);
    inTrimPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    inTrimPill_->bindValue (host(), stock::inputTrim, {});
    inTrimPill_->onClick = [this] { openModal (stock::inputTrim, uistate::Terminal::Pointer); };
    modePill_ = add<nova::Pill> (0, skin());
    modePill_->bindChoice (host(), md::mode, tx ("md.mode"), { tx ("md.stereo"), tx ("md.mono") });
    addInfo (0, modePill_, "hlp.md_mode");
    note_ = add<nova::Text> (0, skin(), Token::TextDim2, 11.0f);
    note_->setText (tx ("md.note"));

    struct Spec { ParamId id; const char* title; bool pink; const char* help; };
    const Spec specs[8] = { { md::shiftL, "md.pitch_l", false, "hlp.md_pitch" }, { md::shiftR, "md.pitch_r", false, "hlp.md_pitch" },
                            { md::delayL, "md.delay_l", false, "hlp.md_delay" }, { md::delayR, "md.delay_r", false, "hlp.md_delay" },
                            { md::feedbackL, "md.fb_l", false, "hlp.md_feedback" }, { md::feedbackR, "md.fb_r", false, "hlp.md_feedback" },
                            { stock::mix, "md.mix", true, "hlp.md_mix" }, { stock::outputTrim, "md.output", true, "hlp.md_output" } };
    for (const auto& s : specs)
    {
        Column c {};
        c.card = add<nova::Panel> (0, skin());
        c.knob = add<nova::KnobCard> (0, skin(), host(), s.id, tx (s.title), modalOpener(), s.pink);
        c.knob->setPlain (true);
        if (s.id == md::shiftL || s.id == md::shiftR) c.knob->setDisplayRange (-50.0, 50.0, 1.0);   // la course du potard couvre +/-50 cents ; la modale garde +/-1200
        addInfo (0, c.card, s.help, 1);
        columns_.push_back (c);
    }
}

void NovaMicrodecaleEditor::buildVoicesPage()
{
    voicesPanel_ = add<nova::Panel> (1, skin());
    voicesPanel_->setTitle (tx ("md.voices_title"));
    struct P { ParamId id; const char* title; const char* help; };
    for (const P p : { P { md::levelShiftL, "md.lvl_shift_l", "hlp.md_level" }, P { md::levelShiftR, "md.lvl_shift_r", "hlp.md_level" }, P { md::levelDryL, "md.lvl_dry_l", "hlp.md_level" },
                       P { md::levelDryR, "md.lvl_dry_r", "hlp.md_level" }, P { md::splice, "md.splice", "hlp.md_splice" },
                       P { md::panShiftL, "md.pan_shift_l", "hlp.md_pan" }, P { md::panShiftR, "md.pan_shift_r", "hlp.md_pan" }, P { md::panDryL, "md.pan_dry_l", "hlp.md_pan" },
                       P { md::panDryR, "md.pan_dry_r", "hlp.md_pan" }, P { md::mod, "md.mod", "hlp.md_mod" }, P { md::modRate, "md.mod_rate", "hlp.md_modrate" } })
    {
        auto* kc = add<nova::KnobCard> (1, skin(), host(), p.id, tx (p.title), modalOpener(), false);
        voiceCards_.push_back (kc);
        addInfo (1, kc, p.help, true);
    }
    wavePill_ = add<nova::Pill> (1, skin());
    wavePill_->bindChoice (host(), md::modWave, tx ("md.mod_wave"), { tx ("md.sine"), tx ("md.random") });
    addInfo (1, wavePill_, "hlp.md_modwave");
}

void NovaMicrodecaleEditor::layout()
{
    // ---- page MICRO
    // deuxième ligne (menu et potards) : 168 px, le menu a 14 px de marge en haut et en bas comme à gauche et à droite ; première ligne agrandie d'autant (14 px entre les deux)
    metersPanel_->setBounds (R (14, 72, 176, 456));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 2, trimH = skin().ipx (40.0f);
        const auto meters = in.withTrimmedBottom (trimH);
        inMeter_->setBounds (meters.withWidth (w));
        outMeter_->setBounds (meters.withX (meters.getX() + w).withWidth (w));
        inTrimPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), w - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 542, 176, 168));
    layoutNav (542, 168);
    viewPanel_->setBounds (R (204, 72, 942, 456));
    view_->setBounds (R (220, 104, 910, 362));
    modePill_->setBounds (R (220, 480, 170, 32));
    note_->setBounds (R (430, 476, 700, 40));
    const float colW = (942.0f - 12.0f * 7.0f) / 8.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const float x = 204.0f + static_cast<float> (i) * (colW + 12.0f);
        columns_[i].card->setBounds (R (x, 542, colW, 168));
        columns_[i].knob->setBounds (R (x, 542 + 18, colW, 132));
    }

    // ---- page VOICES
    voicesPanel_->setBounds (R (204, 72, 942, 638));
    const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
    for (std::size_t i = 0; i < voiceCards_.size(); ++i)
    {
        const float x = 220.0f + static_cast<float> (i % 5) * (cw + 12.0f), y = 108.0f + static_cast<float> (i / 5) * 172.0f;
        voiceCards_[i]->setBounds (R (x, y, cw, 160));
    }
    wavePill_->setBounds (R (220.0f + (cw + 12.0f), 108.0f + 2.0f * 172.0f + 60.0f, cw, 32));

    // ---- barre du bas
    abPill()->setBounds (R (14, 724 + 12, 40, 32));

    // ---- en-tête
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaMicrodecaleEditor::tick()
{
    if (module_ != nullptr)
    {
        inMeter_->update (dbOf (module_->meter (md::kInPeakL)), dbOf (module_->meter (md::kInPeakR)));
        outMeter_->update (dbOf (module_->meter (md::kOutPeakL)), dbOf (module_->meter (md::kOutPeakR)));
    }
    if (currentPage() == 0) view_->update();
}
