#include "OrganView.h"
#include "neptune/core/Mixer.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kDrawbarW = 42, kDrawbarH = 190;
const char* const kFootage[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
// couleurs des tirettes : brun, brun, blanc, blanc, noir, blanc, noir, noir, blanc
const int kDrawbarKind[9] = { 0, 0, 1, 1, 2, 1, 2, 2, 1 };
} // namespace

// Tirette : le haut est « rentrée » (0), le bas « sortie » (8), comme sur la console.
class OrganView::Drawbar : public juce::Component
{
public:
    Drawbar (ParamHost& host, int kind, int offset, const juce::String& footage, const theme::Palette& pal)
        : host_ (host), kind_ (kind), offset_ (offset), footage_ (footage), palette_ (pal) {}

    void setTrack (int track) { id_ = mixer::organParam (track, offset_); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
        const juce::Colour bodyColour = kind_ == 0 ? juce::Colour (0xff6b3f1d) : kind_ == 1 ? juce::Colour (0xfff2eadb) : juce::Colour (0xff1c1a18);
        g.setColour (c (Token::TextDim));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (footage_, b.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred);
        auto valueArea = b.removeFromBottom (18.0f);
        auto slot = b.reduced (b.getWidth() * 0.3f, 2.0f);
        g.setColour (c (Token::Grid));
        g.fillRoundedRectangle (slot, 3.0f);
        const int v = value();
        const float y = slot.getY() + slot.getHeight() * (static_cast<float> (v) / 8.0f);
        // la bande de couleur va du côté des « pieds » (le repère en haut) jusqu'à la tirette orange ; en dessous, la partie vide
        g.setColour (bodyColour);
        g.fillRoundedRectangle (b.getX() + 3.0f, slot.getY(), b.getWidth() - 6.0f, juce::jmax (0.0f, y - slot.getY()), 3.0f);
        g.setColour (c (Token::Accent));
        g.fillRoundedRectangle (b.getX() + 1.0f, y - 6.0f, b.getWidth() - 2.0f, 12.0f, 3.0f);
        g.setColour (c (Token::Text));
        g.drawText (juce::String (v), valueArea.toNearestInt(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override { host_.uiBeginEdit (id_); set (e); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e); }
    void mouseUp (const juce::MouseEvent&) override { host_.uiEndEdit (id_); }
    void mouseDoubleClick (const juce::MouseEvent&) override { host_.uiResetToDefault (id_); repaint(); }

    int value() const { return juce::jlimit (0, 8, static_cast<int> (std::lround (host_.parameters().get (id_)))); }

private:
    void set (const juce::MouseEvent& e)
    {
        const auto b = getLocalBounds().toFloat();
        const float top = b.getY() + 18.0f, h = b.getHeight() - 18.0f - 20.0f;
        const float p = juce::jlimit (0.0f, 1.0f, (e.position.y - top) / juce::jmax (1.0f, h));
        host_.uiSetNormalised (id_, static_cast<double> (std::lround (p * 8.0f)) / 8.0);
        repaint();
    }

    ParamHost& host_;
    ParamId id_ = 0;
    int kind_, offset_;
    juce::String footage_;
    const theme::Palette& palette_;
};

class OrganView::Switch : public juce::Component
{
public:
    Switch (ParamHost& host, int offset, const juce::String& label, const theme::Palette& pal)
        : host_ (host), offset_ (offset), label_ (label), palette_ (pal) {}

    void setTrack (int track) { id_ = mixer::organParam (track, offset_); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
        const bool on = host_.parameters().get (id_) >= 0.5;
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (on ? c (Token::Accent) : c (Token::Grid));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (c (Token::CtlBd));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        g.setColour (on ? c (Token::BgApp) : c (Token::Text));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label_, getLocalBounds(), juce::Justification::centred, true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        const double next = host_.parameters().get (id_) >= 0.5 ? 0.0 : 1.0;
        host_.uiBeginEdit (id_);
        host_.uiSetNormalised (id_, next);
        host_.uiEndEdit (id_);
        repaint();
    }

private:
    ParamHost& host_;
    ParamId id_ = 0;
    int offset_;
    juce::String label_;
    const theme::Palette& palette_;
};

// Grand carré de la vitesse du Leslie (Chorale / Tremolo / Frein) : un clic choisit la vitesse.
class OrganView::SpeedSquare : public juce::Component
{
public:
    SpeedSquare (ParamHost& host, int value, const juce::String& label, const theme::Palette& pal)
        : host_ (host), value_ (value), label_ (label), palette_ (pal) {}

    void setTrack (int track) { id_ = mixer::organParam (track, mixer::kOrgLeslieSpeed); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
        const bool on = static_cast<int> (std::lround (host_.parameters().get (id_))) == value_;
        const auto b = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (on ? c (Token::Accent) : c (Token::Grid));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (c (Token::CtlBd));
        g.drawRoundedRectangle (b, 8.0f, 1.5f);
        g.setColour (on ? c (Token::BgApp) : c (Token::Text));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (label_, getLocalBounds(), juce::Justification::centred, true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        host_.uiBeginEdit (id_);
        host_.uiSetNormalised (id_, static_cast<double> (value_) / 2.0);
        host_.uiEndEdit (id_);
        repaint();
    }

private:
    ParamHost& host_;
    ParamId id_ = 0;
    int value_;
    juce::String label_;
    const theme::Palette& palette_;
};

// Réglage de la cabine Leslie (champ de la description) : curseur et intitulé.
struct OrganView::LeslieSlider
{
    juce::Slider slider;
    const char* labelKey = "";
    double organ::OrganDescription::* field = nullptr;
};

struct OrganView::ComboLink
{
    juce::ComboBox box;
    int offset = 0;
    ParamId id = 0;
};

juce::Colour OrganView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

OrganView::OrganView (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, theme::KnobStyle knobStyle,
                      KnobIds extraKnobs, OpenModal openModal)
    : host_ (host), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), extraIds_ (std::move (extraKnobs)), openModal_ (std::move (openModal)),
      keyboard_ (palette)
{
    namespace mx = neptune::core::mixer;
    for (int i = 0; i < 9; ++i) addAndMakeVisible (drawbars_.add (new Drawbar (host_, kDrawbarKind[i], mx::kOrgUpper0 + i, kFootage[i], palette_)));
    for (int i = 0; i < 9; ++i) addAndMakeVisible (drawbars_.add (new Drawbar (host_, kDrawbarKind[i], mx::kOrgLower0 + i, kFootage[i], palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (host_, 0, mx::kOrgPedal16, "16'", palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (host_, 1, mx::kOrgPedal8, "8'", palette_)));

    auto sw = [this] (int kind, const char* key)
    {
        addAndMakeVisible (switches_.add (new Switch (host_, kind, text (key), palette_)));
    };
    sw (mx::kOrgPercOn, "organ.perc_on");
    sw (mx::kOrgPercSoft, "organ.perc_soft");
    sw (mx::kOrgPercFast, "organ.perc_fast");
    sw (mx::kOrgPercThird, "organ.perc_third");
    sw (mx::kOrgVibUpper, "organ.vib_upper");
    sw (mx::kOrgVibLower, "organ.vib_lower");
    sw (mx::kOrgDriveOn, "organ.drive");

    for (const int kind : { static_cast<int> (mx::kOrgVibMode), static_cast<int> (mx::kOrgLeslieSpeed) })
    {
        auto* link = combos_.add (new ComboLink);
        link->offset = kind;
        link->id = mx::organParam (0, kind);
        const auto index = host_.parameters().indexOf (link->id);
        if (index != ParameterSet::npos)
        {
            int itemId = 1;
            for (const auto& choice : host_.parameters().spec (index).choices)
                link->box.addItem (juce::String::fromUTF8 (choice.c_str()), itemId++);
        }
        link->box.onChange = [this, link]
        {
            if (updating_) return;
            const int count = link->box.getNumItems();
            if (count < 2) return;
            host_.uiBeginEdit (link->id);
            host_.uiSetNormalised (link->id, static_cast<double> (link->box.getSelectedId() - 1) / static_cast<double> (count - 1));
            host_.uiEndEdit (link->id);
        };
        addAndMakeVisible (link->box);
    }

    // potards : Leslie dry/wet, overdrive (3), réverb ; puis ceux de l'instrument
    for (const int kind : { static_cast<int> (mx::kOrgLeslieMix), static_cast<int> (mx::kOrgDriveInput), static_cast<int> (mx::kOrgDriveCharacter),
                            static_cast<int> (mx::kOrgDriveOutput), static_cast<int> (mx::kOrgReverb) })
        addAndMakeVisible (knobs_.add (new NeptuneKnob (host_, mx::organParam (0, kind), palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { if (openModal_) openModal_ (pid, t); })));
    for (const auto& f : extraIds_)
        addAndMakeVisible (knobs_.add (new NeptuneKnob (host_, f (0), palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { if (openModal_) openModal_ (pid, t); })));

    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].addItem (text ("organ.off"), 1);
        for (int c = 1; c <= 16; ++c) channelBox_[m].addItem (juce::String (c), c + 1);
        for (int t = -24; t <= 24; ++t) transposeBox_[m].addItem ((t > 0 ? "+" : "") + juce::String (t), t + 25);
        channelBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = description_;
            d.channel[static_cast<std::size_t> (m)] = channelBox_[m].getSelectedId() - 1;
            description_ = d;
            if (onChange) onChange (d);
        };
        transposeBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = description_;
            d.transpose[static_cast<std::size_t> (m)] = transposeBox_[m].getSelectedId() - 25;
            description_ = d;
            if (onChange) onChange (d);
        };
        addAndMakeVisible (channelBox_[m]);
        addAndMakeVisible (transposeBox_[m]);
    }
    for (auto* s : { &splitLower_, &splitUpper_ })
    {
        s->setRange (0.0, 127.0, 1.0);
        s->setSliderStyle (juce::Slider::LinearBar);
        addAndMakeVisible (*s);
    }
    splitLower_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = description_;
        d.splitLower = static_cast<int> (splitLower_.getValue());
        description_ = d;
        if (onChange) onChange (d);
    };
    splitUpper_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = description_;
        d.splitUpper = static_cast<int> (splitUpper_.getValue());
        description_ = d;
        if (onChange) onChange (d);
    };

    // clavier à l'écran
    auto channelOfUpper = [this] { return juce::jmax (0, description_.channel[0] - 1); };
    keyboard_.onNoteOn = [this, channelOfUpper] (int key, float) { if (onNoteOn) onNoteOn (key, channelOfUpper()); };
    keyboard_.onNoteOff = [this, channelOfUpper] (int key) { if (onNoteOff) onNoteOff (key, channelOfUpper()); };
    addAndMakeVisible (keyboard_);
    octDown_.setButtonText (text ("synth.oct_down"));
    octUp_.setButtonText (text ("synth.oct_up"));
    octDown_.onClick = [this] { keyboard_.shiftOctave (-1); };
    octUp_.onClick = [this] { keyboard_.shiftOctave (1); };
    hold_.setButtonText (text ("synth.hold"));
    hold_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    hold_.onClick = [this] { keyboard_.setHold (hold_.getToggleState()); };
    addAndMakeVisible (octDown_);
    addAndMakeVisible (octUp_);
    addAndMakeVisible (hold_);

    // vibrato : profondeur (potard de l'orgue n° 6)
    addAndMakeVisible (knobs_.insert (kOwnKnobs - 1, new NeptuneKnob (host_, mx::organParam (0, mx::kOrgVibDepth), palette_, knobStyle_,
                                                                       [this] (ParamId pid, uistate::Terminal t) { if (openModal_) openModal_ (pid, t); })));

    // vitesse du Leslie : grands carrés (Chorale, Tremolo, Frein)
    for (int v = 0; v < 3; ++v)
        addChildComponent (squares_.add (new SpeedSquare (host_, v, text (v == 0 ? "organ.leslie_chorale" : v == 1 ? "organ.leslie_tremolo" : "organ.leslie_brake"), palette_)));

    // réglages de la cabine (essentiel) : vitesses, accélérations, niveau du cornet
    struct Def { const char* key; double organ::OrganDescription::* field; double lo, hi, step; const char* suffix; };
    static const Def defs[] = {
        { "organ.horn_slow", &organ::OrganDescription::hornSlowRpm, 5.0, 200.0, 0.5, " rpm" },
        { "organ.horn_fast", &organ::OrganDescription::hornFastRpm, 100.0, 900.0, 1.0, " rpm" },
        { "organ.drum_slow", &organ::OrganDescription::drumSlowRpm, 5.0, 100.0, 0.5, " rpm" },
        { "organ.drum_fast", &organ::OrganDescription::drumFastRpm, 60.0, 600.0, 1.0, " rpm" },
        { "organ.horn_acc", &organ::OrganDescription::hornAcc, 0.05, 2.0, 0.01, " s" },
        { "organ.horn_dec", &organ::OrganDescription::hornDec, 0.05, 2.0, 0.01, " s" },
        { "organ.drum_acc", &organ::OrganDescription::drumAcc, 0.5, 10.0, 0.05, " s" },
        { "organ.drum_dec", &organ::OrganDescription::drumDec, 0.5, 10.0, 0.05, " s" },
        { "organ.horn_level", &organ::OrganDescription::hornLevel, 0.0, 1.0, 0.01, "" },
    };
    for (const auto& def : defs)
    {
        auto* ls = leslieSliders_.add (new LeslieSlider);
        ls->labelKey = def.key;
        ls->field = def.field;
        ls->slider.setRange (def.lo, def.hi, def.step);
        ls->slider.setSliderStyle (juce::Slider::LinearBar);
        ls->slider.setTextValueSuffix (def.suffix);
        ls->slider.setColour (juce::Slider::trackColourId, colour (Token::Accent));
        ls->slider.setColour (juce::Slider::backgroundColourId, colour (Token::Grid));
        ls->slider.setColour (juce::Slider::textBoxTextColourId, colour (Token::Text));
        ls->slider.onValueChange = [this, ls]
        {
            if (updating_) return;
            auto d = description_;
            d.*(ls->field) = ls->slider.getValue();
            description_ = d;
            if (onChange) onChange (d);
        };
        addChildComponent (ls->slider);
    }

    // accord général
    tuneSlider_.setRange (-100.0, 100.0, 1.0);
    tuneSlider_.setSliderStyle (juce::Slider::LinearBar);
    tuneSlider_.setTextValueSuffix (" cents");
    tuneSlider_.setColour (juce::Slider::trackColourId, colour (Token::Accent));
    tuneSlider_.setColour (juce::Slider::backgroundColourId, colour (Token::Grid));
    tuneSlider_.setColour (juce::Slider::textBoxTextColourId, colour (Token::Text));
    tuneSlider_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = description_;
        d.tuningCents = static_cast<int> (tuneSlider_.getValue());
        description_ = d;
        if (onChange) onChange (d);
    };
    addChildComponent (tuneSlider_);
    tuneLabel_.setText (text ("organ.tune"), juce::dontSendNotification);
    tuneLabel_.setColour (juce::Label::textColourId, colour (Token::Text));
    addChildComponent (tuneLabel_);
    tuneHint_.setText (text ("organ.tune_hint"), juce::dontSendNotification);
    tuneHint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    tuneHint_.setFont (juce::FontOptions (11.0f));
    addChildComponent (tuneHint_);
    midiHint_.setText (text ("organ.midi_hint"), juce::dontSendNotification);
    midiHint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    midiHint_.setFont (juce::FontOptions (11.0f));
    midiHint_.setJustificationType (juce::Justification::topLeft);
    addChildComponent (midiHint_);

    // onglets
    static const char* const pageKeys[4] = { "organ.page_drawbars", "organ.page_leslie", "organ.page_live", "organ.page_settings" };
    for (int i = 0; i < 4; ++i)
    {
        pageButtons_[i].setButtonText (text (pageKeys[i]));
        pageButtons_[i].setClickingTogglesState (false);
        pageButtons_[i].onClick = [this, i] { setPage (static_cast<Page> (i)); };
        addAndMakeVisible (pageButtons_[i]);
    }

    bindTrack();
    setDescription (description_);
    updateVisibility();
}

OrganView::~OrganView() = default;

void OrganView::setPage (Page p)
{
    if (p == page_) return;
    page_ = p;
    updateVisibility();
    resized();
    repaint();
}

void OrganView::updateVisibility()
{
    const bool main = page_ == Page::Drawbars, live = page_ == Page::Live, les = page_ == Page::Leslie, set = page_ == Page::Settings;
    for (int i = 0; i < 4; ++i) pageButtons_[i].setToggleState (static_cast<int> (page_) == i, juce::dontSendNotification);
    for (auto* d : drawbars_) d->setVisible (main || live);
    for (int i = 0; i < switches_.size(); ++i) switches_[i]->setVisible (main || live);
    combos_[0]->box.setVisible (main || live);
    combos_[1]->box.setVisible (main);
    for (int i = 0; i < knobs_.size(); ++i)
        knobs_[i]->setVisible (i == 0 ? (main || les) : (main || live));
    for (int m = 0; m < 3; ++m) { channelBox_[m].setVisible (main); transposeBox_[m].setVisible (main); }
    splitLower_.setVisible (main);
    splitUpper_.setVisible (main);
    for (auto* s : squares_) s->setVisible (les || live);
    for (auto* l : leslieSliders_) l->slider.setVisible (les);
    tuneSlider_.setVisible (set);
    tuneLabel_.setVisible (set);
    tuneHint_.setVisible (set);
    midiHint_.setVisible (set);
}

void OrganView::bindTrack()
{
    for (auto* d : drawbars_) d->setTrack (track_);
    for (auto* s : switches_) s->setTrack (track_);
    for (auto* c : combos_) c->id = mixer::organParam (track_, c->offset);
    static const int kinds[kOwnKnobs] = { mixer::kOrgLeslieMix, mixer::kOrgDriveInput, mixer::kOrgDriveCharacter, mixer::kOrgDriveOutput,
                                          mixer::kOrgReverb, mixer::kOrgVibDepth };
    for (int i = 0; i < kOwnKnobs && i < knobs_.size(); ++i) knobs_[i]->setParamId (mixer::organParam (track_, kinds[i]));
    for (std::size_t i = 0; i < extraIds_.size() && kOwnKnobs + static_cast<int> (i) < knobs_.size(); ++i)
        knobs_[kOwnKnobs + static_cast<int> (i)]->setParamId (extraIds_[i] (track_));
    for (auto* s : squares_) s->setTrack (track_);
    refreshFromParameters();
    repaint();
}

void OrganView::setTrack (int track)
{
    if (track == track_) return;
    track_ = track;
    bindTrack();
}

void OrganView::setDescription (const organ::OrganDescription& d)
{
    description_ = d;
    updating_ = true;
    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].setSelectedId (d.channel[static_cast<std::size_t> (m)] + 1, juce::dontSendNotification);
        transposeBox_[m].setSelectedId (d.transpose[static_cast<std::size_t> (m)] + 25, juce::dontSendNotification);
    }
    splitLower_.setValue (d.splitLower, juce::dontSendNotification);
    splitUpper_.setValue (d.splitUpper, juce::dontSendNotification);
    for (auto* l : leslieSliders_) l->slider.setValue (d.*(l->field), juce::dontSendNotification);
    tuneSlider_.setValue (d.tuningCents, juce::dontSendNotification);
    updating_ = false;
}

void OrganView::refreshFromParameters()
{
    updating_ = true;
    for (auto* link : combos_)
    {
        const int v = static_cast<int> (std::lround (host_.parameters().get (link->id))) + 1;
        if (link->box.getSelectedId() != v) link->box.setSelectedId (v, juce::dontSendNotification);
    }
    updating_ = false;
    for (auto* d : drawbars_) d->repaint();
    for (auto* s : switches_) s->repaint();
    for (auto* s : squares_) s->repaint();
    repaint();
}

void OrganView::placeScaled (juce::Rectangle<int> area)
{
    lastArea_ = area;
    const float s = juce::jmin (1.0f, static_cast<float> (area.getWidth()) / designWidth, static_cast<float> (area.getHeight()) / designHeight);
    const float scale = juce::jmax (0.4f, s);
    setTransform (juce::AffineTransform::scale (scale));
    // la transformation de JUCE agit autour de l'origine du PARENT : les limites sont exprimées en repère du parent / échelle
    const auto x = juce::roundToInt (static_cast<float> (area.getX()) / scale);
    const auto y = juce::roundToInt (static_cast<float> (area.getY()) / scale);
    const auto w = juce::roundToInt (static_cast<float> (area.getWidth()) / scale);
    const auto h = juce::roundToInt (static_cast<float> (area.getHeight()) / scale);
    if (getWidth() == w && getHeight() == h) resized();
    setBounds (x, y, w, h);
}

void OrganView::resized()
{
    sections_.clear();
    keyboardArea_ = {};
    auto r = getLocalBounds().reduced (4, 2);

    // onglets
    auto tabs = r.removeFromTop (28);
    for (int i = 0; i < 4; ++i) pageButtons_[i].setBounds (tabs.removeFromLeft (130).reduced (2));
    r.removeFromTop (6);

    // clavier à l'écran : toujours en bas
    auto bottom = r.removeFromBottom (28 + 8 + 84);
    layoutKeyboard (bottom);
    contentArea_ = r;
    switch (page_)
    {
        case Page::Drawbars: layoutDrawbars (r, false); break;
        case Page::Live: layoutDrawbars (r, true); break;
        case Page::Leslie: layoutLeslie (r); break;
        case Page::Settings: layoutSettings (r); break;
    }
}

void OrganView::layoutKeyboard (juce::Rectangle<int> area)
{
    area.removeFromTop (8);
    auto strip = area.removeFromTop (28);
    octDown_.setBounds (strip.removeFromLeft (60).reduced (2));
    octUp_.setBounds (strip.removeFromLeft (60).reduced (2));
    hold_.setBounds (strip.removeFromLeft (110).reduced (2));
    keyboard_.setBounds (area.removeFromTop (juce::jmin (84, area.getHeight())));
}

// Page des tirettes ; en mode « Live » : sans la configuration des claviers, les commandes de jeu agrandies et les trois carrés du Leslie.
void OrganView::layoutDrawbars (juce::Rectangle<int> r, bool live)
{
    auto row = r.removeFromTop (kDrawbarH + 34);
    int x = row.getX();
    auto place = [&] (int firstIndex, int count, const juce::String& title)
    {
        const int width = count * (kDrawbarW + 4) + 12;
        sections_.push_back ({ title, { x, row.getY(), width, row.getHeight() } });
        for (int i = 0; i < count; ++i)
            drawbars_[firstIndex + i]->setBounds (x + 6 + i * (kDrawbarW + 4), row.getY() + 26, kDrawbarW, kDrawbarH);
        x += width + 14;
    };
    place (0, 9, text ("organ.upper"));
    place (9, 9, text ("organ.lower"));
    place (18, 2, text ("organ.pedal"));

    r.removeFromTop (10);
    auto ctl = r.removeFromTop (live ? 150 : 124);
    auto cell = [&] (int width, const juce::String& title) -> juce::Rectangle<int>
    {
        auto a = ctl.removeFromLeft (width);
        ctl.removeFromLeft (10);
        sections_.push_back ({ title, a });
        return a.withTrimmedTop (24).reduced (4, 2);
    };
    {
        auto a = cell (190, text ("organ.percussion"));
        const int w = a.getWidth() / 2, h = juce::jmin (live ? 48 : 36, a.getHeight() / 2);
        switches_[0]->setBounds (a.getX(), a.getY(), w, h);
        switches_[1]->setBounds (a.getX() + w, a.getY(), w, h);
        switches_[2]->setBounds (a.getX(), a.getY() + h + 2, w, h);
        switches_[3]->setBounds (a.getX() + w, a.getY() + h + 2, w, h);
    }
    {
        auto a = cell (170, text ("organ.vibrato"));
        combos_[0]->box.setBounds (a.removeFromTop (live ? 36 : 28));
        a.removeFromTop (6);
        const int w = a.getWidth() / 2;
        switches_[4]->setBounds (a.getX(), a.getY(), w, live ? 40 : 32);
        switches_[5]->setBounds (a.getX() + w, a.getY(), w, live ? 40 : 32);
    }
    if (live)
    {
        // trois grands carrés pour la vitesse du Leslie, la profondeur du vibrato reste au potard
        auto a = cell (3 * 104 + 16, text ("organ.leslie"));
        for (int i = 0; i < 3; ++i) squares_[i]->setBounds (a.getX() + i * 104, a.getY(), 100, a.getHeight() - 4);
    }
    else
    {
        auto a = cell (170, text ("organ.leslie"));
        combos_[1]->box.setBounds (a.removeFromLeft (90).withHeight (28));
        knobs_[0]->setBounds (a.getX() + 6, a.getY(), 48, 48);
    }
    {
        auto a = cell (260, text ("organ.drive"));
        switches_[6]->setBounds (a.getX(), a.getY() + a.getHeight() - 30, 72, 28);
        for (int i = 0; i < 3; ++i) knobs_[1 + i]->setBounds (a.getX() + 76 + i * 62, a.getY(), 48, 48);
    }
    {
        // réverb, profondeur du vibrato puis les potards d'instrument (gain...)
        const int count = knobs_.size() - 4; // réverb (4), profondeur (5), instrument (6..)
        auto a = cell (70 * count + 16, text ("organ.reverb"));
        for (int i = 0; i < count; ++i) knobs_[4 + i]->setBounds (a.getX() + 6 + i * 70, a.getY(), 48, 48);
    }

    if (! live)
    {
        r.removeFromTop (8);
        auto kb = r.removeFromTop (86);
        keyboardArea_ = kb;
        sections_.push_back ({ text ("organ.keyboards"), kb });
        auto k = kb.withTrimmedTop (26).reduced (6, 2);
        const int colW = 150;
        for (int m = 0; m < 3; ++m)
        {
            channelBox_[m].setBounds (k.getX() + m * colW, k.getY(), colW - 10, 24);
            transposeBox_[m].setBounds (k.getX() + m * colW, k.getY() + 30, colW - 10, 24);
        }
        splitLower_.setBounds (k.getX() + 3 * colW + 20, k.getY(), 190, 24);
        splitUpper_.setBounds (k.getX() + 3 * colW + 20, k.getY() + 30, 190, 24);
    }
}

// Page Leslie : vitesse (grands carrés + dry/wet), cornet, tambour.
void OrganView::layoutLeslie (juce::Rectangle<int> r)
{
    auto top = r.removeFromTop (150);
    sections_.push_back ({ text ("organ.leslie"), top });
    auto a = top.withTrimmedTop (28).reduced (10, 4);
    for (int i = 0; i < 3; ++i) squares_[i]->setBounds (a.getX() + i * 150, a.getY(), 140, a.getHeight() - 6);
    knobs_[0]->setBounds (a.getX() + 3 * 150 + 40, a.getY() + 6, 52, 52);

    r.removeFromTop (10);
    const int colW = (r.getWidth() - 10) / 2;
    auto horn = r.removeFromLeft (colW);
    r.removeFromLeft (10);
    auto drum = r;
    sections_.push_back ({ text ("organ.horn"), horn.withHeight (230) });
    sections_.push_back ({ text ("organ.drum"), drum.withHeight (230) });
    auto place = [&] (juce::Rectangle<int> col, std::initializer_list<int> idx)
    {
        auto c = col.withTrimmedTop (30).reduced (12, 2);
        for (const int i : idx)
        {
            c.removeFromTop (14);
            leslieSliders_[i]->slider.setBounds (c.removeFromTop (24));
            c.removeFromTop (6);
        }
    };
    place (horn, { 0, 1, 4, 5, 8 });
    place (drum, { 2, 3, 6, 7 });
}

// Page Réglages : accord général et rappel des contrôleurs MIDI.
void OrganView::layoutSettings (juce::Rectangle<int> r)
{
    auto box = r.removeFromTop (130);
    sections_.push_back ({ text ("organ.page_settings"), box });
    auto a = box.withTrimmedTop (30).reduced (12, 4);
    tuneLabel_.setBounds (a.removeFromTop (22));
    tuneSlider_.setBounds (a.removeFromTop (28).withWidth (juce::jmin (420, a.getWidth())));
    tuneHint_.setBounds (a.removeFromTop (30));
    r.removeFromTop (10);
    auto midi = r.removeFromTop (150);
    sections_.push_back ({ "MIDI", midi });
    midiHint_.setBounds (midi.withTrimmedTop (30).reduced (12, 4));
}

void OrganView::paint (juce::Graphics& g)
{
    for (const auto& s : sections_)
    {
        g.setColour (colour (Token::BgCard));
        g.fillRoundedRectangle (s.area.toFloat(), 6.0f);
        g.setColour (colour (Token::Border));
        g.drawRoundedRectangle (s.area.toFloat(), 6.0f, 1.0f);
        g.setColour (colour (Token::Accent));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (s.title, s.area.getX() + 10, s.area.getY() + 4, s.area.getWidth() - 20, 18, juce::Justification::centredLeft, true);
    }
    const auto& set = host_.parameters();
    g.setFont (juce::FontOptions (10.0f));
    for (auto* k : knobs_)
    {
        if (! k->isVisible()) continue;
        const auto b = k->getBounds();
        const auto index = set.indexOf (k->paramId());
        if (index == ParameterSet::npos) continue;
        g.setColour (colour (Token::TextDim));
        g.drawText (juce::String::fromUTF8 (set.spec (index).name.c_str()), b.getCentreX() - 38, b.getBottom() + 1, 76, 14, juce::Justification::centred, true);
        g.setColour (colour (Token::Text));
        g.drawText (juce::String::fromUTF8 (set.toText (k->paramId()).c_str()), b.getCentreX() - 38, b.getBottom() + 14, 76, 14, juce::Justification::centred, true);
    }
    if (page_ == Page::Leslie)
    {
        g.setColour (colour (Token::TextDim));
        g.setFont (juce::FontOptions (11.0f));
        for (auto* l : leslieSliders_)
        {
            const auto b = l->slider.getBounds();
            g.drawText (text (l->labelKey), b.getX(), b.getY() - 14, b.getWidth(), 14, juce::Justification::centredLeft, true);
        }
    }
    if (! keyboardArea_.isEmpty())
    {
        const auto kb = keyboardArea_;
        g.setColour (colour (Token::TextDim));
        const int colW = 150;
        g.setFont (juce::FontOptions (10.0f));
        for (int m = 0; m < 3; ++m)
            g.drawText (text (m == 0 ? "organ.upper" : m == 1 ? "organ.lower" : "organ.pedal"), kb.getX() + 6 + m * colW, kb.getBottom() - 22, colW - 10, 14,
                        juce::Justification::centredLeft, true);
        g.drawText (text ("organ.split_lower"), kb.getX() + 6 + 3 * colW + 20 + 195, kb.getY() + 28, 170, 20, juce::Justification::centredLeft, true);
        g.drawText (text ("organ.split_upper"), kb.getX() + 6 + 3 * colW + 20 + 195, kb.getY() + 58, 170, 20, juce::Justification::centredLeft, true);
    }
}
