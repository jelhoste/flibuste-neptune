#include "UtilityEditor.h"
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
float dbOf (float lin) { return lin <= 1e-5f ? -100.0f : 20.0f * std::log10 (lin); }
float meterFraction (float lin) { return juce::jlimit (0.0f, 1.0f, (dbOf (lin) + 60.0f) / 66.0f); }
} // namespace

UtilityEditor::UtilityEditor (ParamHost& host, const StockModule* metersSource, const theme::Palette& palette, const i18n::Translator& tr,
                              theme::KnobStyle knobStyle, Options options)
    : host_ (host), meters_ (metersSource), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), options_ (std::move (options))
{
    setWantsKeyboardFocus (true);

    addChoice (utility::mode, { "util.ch_stereo", "util.ch_dual" }, "util.channels");
    addChoice (utility::io, { "util.io_ss", "util.io_sms", "util.io_mss" }, "util.io");
    if (options_.bypass) bypass_ = addToggle (stock::bypass, "util.bypass");

    addKnob (stock::inputTrim, "util.input");
    addKnob (utility::volumeL, "util.volume", "util.volume_l");
    addKnob (utility::volumeR, "util.volume_r");
    addKnob (stock::outputTrim, "util.output");
    addKnob (stock::mix, "util.mix");

    addChoice (utility::panMode, { "util.pan_single", "util.pan_dual" }, "util.pan_mode");
    addChoice (utility::panLaw, { "util.law_balance", "util.law_true" }, "util.pan_law");
    addKnob (utility::pan, "util.pan");
    addKnob (utility::panL, "util.pan_l");
    addKnob (utility::panR, "util.pan_r");
    addKnob (utility::width, "util.width");

    addKnob (utility::delayL, "util.delay", "util.delay_l");
    addKnob (utility::delayR, "util.delay_r");
    addToggle (utility::phaseL, "util.phase_l");
    addToggle (utility::phaseR, "util.phase_r");
    addToggle (utility::swap, "util.swap");
    addToggle (utility::mono, "util.mono");
    addToggle (utility::dc, "util.dc");

    delayInfo_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    delayInfo_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (delayInfo_);

    refreshStates();
    startTimerHz (30);
    setSize (preferredWidth, preferredHeight);
}

juce::Colour UtilityEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String UtilityEditor::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void UtilityEditor::addKnob (ParamId id, const char* label, const char* dualLabel)
{
    KnobSlot k;
    k.id = id;
    k.label = label;
    k.dualLabel = dualLabel;
    k.knob = std::make_unique<NeptuneKnob> (host_, id, palette_, knobStyle_,
                                            [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); });
    addAndMakeVisible (*k.knob);
    knobs_.push_back (std::move (k));
}

UtilityEditor::Toggle* UtilityEditor::addToggle (ParamId id, const char* label)
{
    auto t = std::make_unique<Toggle>();
    t->id = id;
    t->setButtonText (text (label));
    t->setClickingTogglesState (false);
    t->setColour (juce::TextButton::buttonOnColourId, colour (Token::Accent));
    t->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    auto* raw = t.get();
    t->onClick = [this, raw]
    {
        const bool on = host_.parameters().get (raw->id) < 0.5;
        const auto* spec = host_.parameters().find (raw->id);
        if (spec == nullptr) return;
        host_.uiBeginEdit (raw->id);
        host_.uiSetNormalised (raw->id, spec->toNormalized (on ? 1.0 : 0.0));
        host_.uiEndEdit (raw->id);
    };
    addAndMakeVisible (*t);
    toggles_.push_back (std::move (t));
    return raw;
}

UtilityEditor::Choice* UtilityEditor::addChoice (ParamId id, std::initializer_list<const char*> items, const char* label)
{
    auto c = std::make_unique<Choice>();
    c->id = id;
    int n = 1;
    for (const auto* key : items) c->addItem (text (key), n++);
    auto* raw = c.get();
    c->onChange = [this, raw]
    {
        const int index = raw->getSelectedId() - 1;
        const auto* spec = host_.parameters().find (raw->id);
        if (index < 0 || spec == nullptr || static_cast<int> (std::lround (host_.parameters().get (raw->id))) == index) return;
        host_.uiBeginEdit (raw->id);
        host_.uiSetNormalised (raw->id, spec->toNormalized (index));
        host_.uiEndEdit (raw->id);
    };
    addAndMakeVisible (*c);
    choiceLabels_.push_back ({ raw, text (label) });
    choices_.push_back (std::move (c));
    return raw;
}

void UtilityEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (host_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<UtilityEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    addAndMakeVisible (*modal_);
    modal_->setBounds (getLocalBounds());
    modal_->toFront (true);
}

void UtilityEditor::closeModal()
{
    if (! modal_) return;
    host_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

// Ce qui n'a pas de sens dans le mode choisi est grisé (spec : « tous les contrôles suivent ce mode »).
void UtilityEditor::refreshStates()
{
    const bool dual = value (utility::mode) >= 0.5;
    const bool pandual = value (utility::panMode) >= 0.5;
    auto enable = [this] (ParamId id, bool on)
    {
        for (auto& k : knobs_)
            if (k.id == id) { k.knob->setEnabled (on); k.knob->setAlpha (on ? 1.0f : 0.35f); }
        for (auto& t : toggles_)
            if (t->id == id) { t->setEnabled (on); t->setAlpha (on ? 1.0f : 0.35f); }
        for (auto& c : choices_)
            if (c->id == id) { c->setEnabled (on); c->setAlpha (on ? 1.0f : 0.35f); }
    };
    enable (utility::volumeR, dual);
    enable (utility::delayR, dual);
    enable (utility::io, ! dual);
    enable (utility::swap, ! dual);
    enable (utility::mono, ! dual);
    enable (utility::width, ! dual);
    enable (utility::panMode, ! dual);
    enable (utility::pan, ! dual && ! pandual);
    enable (utility::panLaw, ! dual && ! pandual);
    enable (utility::panL, ! dual && pandual);
    enable (utility::panR, ! dual && pandual);

    for (auto& t : toggles_)
        t->setToggleState (value (t->id) >= 0.5, juce::dontSendNotification);
    for (auto& c : choices_)
        c->setSelectedId (static_cast<int> (std::lround (value (c->id))) + 1, juce::dontSendNotification);

    const double rate = options_.sampleRate ? options_.sampleRate() : 48000.0;
    const double ms = value (utility::delayL);
    juce::String info = juce::String::fromUTF8 (tr_.format ("util.samples", { juce::String (std::lround (ms * 0.001 * rate)).toStdString() }).c_str());
    if (dual) info += "  /  " + juce::String::fromUTF8 (tr_.format ("util.samples", { juce::String (std::lround (value (utility::delayR) * 0.001 * rate)).toStdString() }).c_str());
    delayInfo_.setText (info + "   -   " + text ("util.delay_note"), juce::dontSendNotification);
}

void UtilityEditor::timerCallback()
{
    refreshStates();
    if (meters_ != nullptr)
    {
        auto fall = [] (float current, float incoming) { return incoming > current ? incoming : current * 0.9f; };
        peak_[0] = fall (peak_[0], meters_->meter (utility::kInPeakL));
        peak_[1] = fall (peak_[1], meters_->meter (utility::kInPeakR));
        peak_[2] = fall (peak_[2], meters_->meter (utility::kOutPeakL));
        peak_[3] = fall (peak_[3], meters_->meter (utility::kOutPeakR));
        correlation_ = 0.8f * correlation_ + 0.2f * meters_->meter (utility::kCorrelation);
    }
    repaint();
}

bool UtilityEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z') { if (m.isShiftDown()) host_.uiRedo(); else host_.uiUndo(); return true; }
    if (code == 'Y' || code == 'y') { host_.uiRedo(); return true; }
    return false;
}

void UtilityEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    groups_.clear();
    if (options_.brand) headerArea_ = r.removeFromTop (46); else headerArea_ = {};

    // sélecteurs du haut : canaux, E/S, bypass
    auto top = r.removeFromTop (54);
    {
        int x = top.getX();
        for (auto& [c, label] : choiceLabels_)
        {
            if (c->id != utility::mode && c->id != utility::io) continue;
            c->setBounds (x, top.getY() + 20, 170, 30);
            x += 182;
        }
        if (bypass_ != nullptr) bypass_->setBounds (top.getRight() - 110, top.getY() + 20, 110, 30);
    }

    metersArea_ = r.removeFromLeft (92);
    corrArea_ = r.removeFromBottom (44);
    delayInfo_.setBounds (r.removeFromBottom (20));

    const int rowH = r.getHeight() / 3;
    auto place = [] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> comps, int width = 0)
    {
        const int w = width > 0 ? width : row.getWidth() / static_cast<int> (comps.size());
        int x = row.getX();
        for (auto* c : comps) { c->setBounds (x, row.getY(), w, row.getHeight()); x += w; }
    };
    auto knobBox = [this] (ParamId id, juce::Rectangle<int> cell)
    {
        for (auto& k : knobs_)
            if (k.id == id)
            {
                const int side = juce::jmin (56, cell.getHeight() - 40, cell.getWidth() - 8);
                k.knob->setBounds (cell.getCentreX() - side / 2, cell.getY() + 20, side, side);
            }
    };
    auto choiceBox = [this] (ParamId id, juce::Rectangle<int> cell)
    {
        for (auto& c : choices_)
            if (c->id == id) c->setBounds (cell.getX() + 4, cell.getY() + 22, cell.getWidth() - 8, 28);
    };
    auto toggleBox = [this] (ParamId id, juce::Rectangle<int> cell)
    {
        for (auto& t : toggles_)
            if (t->id == id) t->setBounds (cell.getX() + 4, cell.getCentreY() - 16, cell.getWidth() - 8, 32);
    };
    (void) place;

    // rangée 1 : niveaux
    {
        auto row = r.removeFromTop (rowH);
        groups_.push_back ({ "LEVELS", row });
        const int w = row.getWidth() / 5;
        int x = row.getX();
        for (const auto id : { stock::inputTrim, utility::volumeL, utility::volumeR, stock::outputTrim, stock::mix })
        {
            knobBox (id, { x, row.getY() + 6, w, row.getHeight() - 6 });
            x += w;
        }
    }
    // rangée 2 : stéréo
    {
        auto row = r.removeFromTop (rowH);
        groups_.push_back ({ "STEREO", row });
        const int w = row.getWidth() / 6;
        int x = row.getX();
        choiceBox (utility::panMode, { x, row.getY() + 6, w, row.getHeight() - 6 }); x += w;
        choiceBox (utility::panLaw, { x, row.getY() + 6, w, row.getHeight() - 6 }); x += w;
        for (const auto id : { utility::pan, utility::panL, utility::panR, utility::width })
        {
            knobBox (id, { x, row.getY() + 6, w, row.getHeight() - 6 });
            x += w;
        }
    }
    // rangée 3 : temps, polarité, outils
    {
        auto row = r;
        groups_.push_back ({ "TIME / POLARITY", row });
        const int w = row.getWidth() / 7;
        int x = row.getX();
        knobBox (utility::delayL, { x, row.getY() + 6, w, row.getHeight() - 6 }); x += w;
        knobBox (utility::delayR, { x, row.getY() + 6, w, row.getHeight() - 6 }); x += w;
        for (const auto id : { utility::phaseL, utility::phaseR, utility::swap, utility::mono, utility::dc })
        {
            toggleBox (id, { x, row.getY() + 6, w, row.getHeight() - 6 });
            x += w;
        }
    }
    if (modal_) modal_->setBounds (getLocalBounds());
}

void UtilityEditor::drawMeters (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setFont (juce::FontOptions (11.0f));
    const int barW = 14, gap = 8;
    const int totalW = barW * 4 + gap * 3 + 12;
    int x = area.getX() + (area.getWidth() - totalW) / 2;
    const auto bars = area.withTrimmedTop (18).withTrimmedBottom (4);
    auto bar = [&] (float lin, int bx)
    {
        g.setColour (colour (Token::Border));
        g.fillRect (bx, bars.getY(), barW, bars.getHeight());
        const int h = static_cast<int> (meterFraction (lin) * static_cast<float> (bars.getHeight()));
        g.setColour (dbOf (lin) > 0.0f ? colour (Token::KnobHot) : colour (Token::Accent2));
        g.fillRect (bx, bars.getBottom() - h, barW, h);
    };
    g.setColour (colour (Token::TextDim));
    g.drawText (text ("util.meter_in"), x, area.getY(), barW * 2 + 2, 16, juce::Justification::centred);
    bar (peak_[0], x); bar (peak_[1], x + barW + 2);
    x += barW * 2 + gap + 8;
    g.setColour (colour (Token::TextDim));
    g.drawText (text ("util.meter_out"), x, area.getY(), barW * 2 + 2, 16, juce::Justification::centred);
    bar (peak_[2], x); bar (peak_[3], x + barW + 2);
}

void UtilityEditor::drawCorrelation (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (text ("util.correlation"), area.removeFromTop (16), juce::Justification::centredLeft);
    auto bar = area.reduced (0, 4);
    g.setColour (colour (Token::Border));
    g.fillRect (bar);
    const float cx = static_cast<float> (bar.getCentreX());
    const float half = static_cast<float> (bar.getWidth()) * 0.5f;
    const float w = correlation_ * half;
    g.setColour (correlation_ >= 0.0f ? colour (Token::Accent2) : colour (Token::KnobHot)); // négatif : annulation en somme mono
    g.fillRect (juce::Rectangle<float> (w >= 0.0f ? cx : cx + w, static_cast<float> (bar.getY()), std::abs (w), static_cast<float> (bar.getHeight())));
    g.setColour (colour (Token::Text));
    g.drawLine (cx, static_cast<float> (bar.getY()), cx, static_cast<float> (bar.getBottom()), 1.0f);
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (colour (Token::TextDim));
    g.drawText ("-1", bar.getX(), bar.getBottom() - 12, 20, 12, juce::Justification::centredLeft);
    g.drawText ("+1", bar.getRight() - 20, bar.getBottom() - 12, 20, 12, juce::Justification::centredRight);
    g.drawText (juce::String (correlation_, 2), bar.getCentreX() + 4, bar.getBottom() - 12, 50, 12, juce::Justification::centredLeft);
}

void UtilityEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgApp));
    if (options_.brand)
    {
        auto h = headerArea_;
        g.setColour (colour (Token::Text));
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("MOUSSAILLON", h.removeFromLeft (230), juce::Justification::centredLeft);
        g.setColour (colour (Token::Accent));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("UTILITY \xc2\xb7 FLIBUSTE SUITE", h, juce::Justification::centredLeft);
    }

    // titres des sélecteurs du haut
    g.setFont (juce::FontOptions (11.0f));
    for (const auto& [c, label] : choiceLabels_)
    {
        const auto b = c->getBounds();
        g.setColour (colour (Token::TextDim));
        g.drawText (label, b.getX(), b.getY() - 18, b.getWidth(), 16, juce::Justification::centredLeft);
    }

    // groupes
    for (const auto& grp : groups_)
    {
        g.setColour (colour (Token::BgCard));
        g.fillRoundedRectangle (grp.area.toFloat().reduced (2.0f), 6.0f);
        g.setColour (colour (Token::Border));
        g.drawRoundedRectangle (grp.area.toFloat().reduced (2.0f), 6.0f, 1.0f);
    }
    // sélecteurs de la rangée stéréo : leur titre
    for (const auto& [c, label] : choiceLabels_)
    {
        if (c->id != utility::panMode && c->id != utility::panLaw) continue;
        const auto b = c->getBounds();
        g.setColour (colour (Token::TextDim));
        g.drawText (label, b.getX(), b.getY() - 18, b.getWidth(), 16, juce::Justification::centredLeft);
    }

    // légendes des potards
    const bool dual = value (utility::mode) >= 0.5;
    for (const auto& k : knobs_)
    {
        const auto b = k.knob->getBounds();
        const char* key = dual && k.dualLabel != nullptr ? k.dualLabel : k.label;
        const float a = k.knob->isEnabled() ? 1.0f : 0.35f;
        g.setColour (colour (Token::TextDim).withMultipliedAlpha (a));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (text (key), b.getX() - 20, b.getY() - 18, b.getWidth() + 40, 16, juce::Justification::centred);
        g.setColour (colour (Token::Accent).withMultipliedAlpha (a));
        g.drawText (juce::String::fromUTF8 (host_.parameters().toText (k.id).c_str()), b.getX() - 20, b.getBottom() + 1, b.getWidth() + 40, 16,
                    juce::Justification::centred);
    }
    drawMeters (g, metersArea_);
    drawCorrelation (g, corrArea_);
}
