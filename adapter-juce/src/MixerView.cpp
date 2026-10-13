#include "MixerView.h"
#include <cmath>
#include "neptune/core/ProbePool.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kStripW = 58;
constexpr int kStripH = 292;
constexpr int kMasterW = 112;
constexpr double kFaderMinDb = -100.0, kFaderMaxDb = 6.0;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// Curseur lié à un paramètre : début / valeur / fin de geste passent par le processeur (annulation + automation de l'hôte).
struct BoundSlider : juce::Slider
{
    BoundSlider (NeptuneProcessor& p, juce::Slider::SliderStyle style) : juce::Slider (style, juce::Slider::NoTextBox), proc (p)
    {
        onDragStart = [this] { dragging = true; if (id != 0) proc.uiBeginEdit (id); };
        onDragEnd = [this] { dragging = false; if (id != 0) proc.uiEndEdit (id); };
        onValueChange = [this]
        {
            if (updating || id == 0) return;
            const auto* spec = proc.parameters().find (id);
            if (spec == nullptr) return;
            if (! dragging) proc.uiBeginEdit (id); // changement ponctuel (molette, clavier) : un geste complet
            proc.uiSetNormalised (id, spec->toNormalized (getValue()));
            if (! dragging) proc.uiEndEdit (id);
        };
    }
    void bind (ParamId pid, double resetValue)
    {
        id = pid;
        setDoubleClickReturnValue (true, resetValue);
    }
    void sync()
    {
        if (id == 0 || dragging || isMouseButtonDown()) return;
        updating = true;
        setValue (proc.parameters().get (id), juce::dontSendNotification);
        updating = false;
    }

    NeptuneProcessor& proc;
    ParamId id = 0;
    bool dragging = false, updating = false;
};

// Bouton à bascule lié à un paramètre 0/1 (mute, solo, mono).
struct BoundToggle : juce::TextButton
{
    BoundToggle (NeptuneProcessor& p, const juce::String& label) : proc (p)
    {
        setButtonText (label);
        setClickingTogglesState (false);
        onClick = [this] { if (onToggle) onToggle (juce::ModifierKeys::currentModifiers.isShiftDown()); };
    }
    void sync()
    {
        const bool on = id != 0 && proc.parameters().get (id) >= 0.5;
        if (on != getToggleState()) setToggleState (on, juce::dontSendNotification);
    }
    void setValueFromUi (bool on)
    {
        if (id == 0) return;
        const auto* spec = proc.parameters().find (id);
        if (spec == nullptr) return;
        proc.uiBeginEdit (id);
        proc.uiSetNormalised (id, spec->toNormalized (on ? 1.0 : 0.0));
        proc.uiEndEdit (id);
    }
    NeptuneProcessor& proc;
    ParamId id = 0;
    std::function<void (bool shift)> onToggle;
};

float dbOf (float lin) { return lin <= 1e-5f ? -100.0f : 20.0f * std::log10 (lin); }
} // namespace

// ------------------------------------------------------------------------------------------------ fenêtre de routage

// Routage d'une bande : sortie principale, sortie directe, mode de pan, 8 envois (bus, point de captation, niveau, pan, mute).
class RoutingPanel : public juce::Component
{
public:
    RoutingPanel (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr, bool isBus, int index)
        : proc_ (p), palette_ (palette), tr_ (tr), isBus_ (isBus), index_ (index)
    {
        s_ = isBus ? p.busSettings (index) : p.trackSettings (index);
        auto text = [this] (const char* k) { return u8 (std::string (tr_.tr (k))); };
        auto label = [this] (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, colour (Token::Text));
            addAndMakeVisible (l);
        };
        label (mainLabel_, text ("mix.main_out"));
        label (directLabel_, text ("mix.direct_out"));
        label (panLabel_, text ("mix.pan_mode"));
        mainBox_.addItem ("Master", 1);
        for (int b = 1; b <= mixer::numBuses; ++b)
        {
            mainBox_.addItem ("Bus " + juce::String (b), b + 1);
            if (isBus_ && b <= index_ + 1) mainBox_.setItemEnabled (b + 1, false); // un bus ne route que vers un bus supérieur
        }
        mainBox_.setSelectedId (s_.routing.mainDest + 1, juce::dontSendNotification);
        mainBox_.onChange = [this] { s_.routing.mainDest = mainBox_.getSelectedId() - 1; push(); };
        addAndMakeVisible (mainBox_);
        mainOn_.setButtonText (text ("mix.main_on"));
        mainOn_.setToggleState (s_.routing.mainEnabled, juce::dontSendNotification);
        mainOn_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        mainOn_.onClick = [this] { s_.routing.mainEnabled = mainOn_.getToggleState(); push(); };
        addAndMakeVisible (mainOn_);
        directBox_.addItem (text ("mix.none"), 1);
        for (int o = 1; o <= mixer::numOutputs; ++o) directBox_.addItem (text ("mix.output") + " " + juce::String (o), o + 1);
        directBox_.setSelectedId (s_.routing.directOut + 1, juce::dontSendNotification);
        directBox_.onChange = [this] { s_.routing.directOut = directBox_.getSelectedId() - 1; push(); };
        addAndMakeVisible (directBox_);
        panBox_.addItem (text ("mix.pan_balance"), 1);
        panBox_.addItem (text ("mix.pan_true"), 2);
        panBox_.setSelectedId (s_.panMode == mixer::PanMode::True ? 2 : 1, juce::dontSendNotification);
        panBox_.onChange = [this] { s_.panMode = panBox_.getSelectedId() == 2 ? mixer::PanMode::True : mixer::PanMode::Balance; push(); };
        addAndMakeVisible (panBox_);

        for (int i = 0; i < mixer::numSends; ++i)
        {
            auto& r = rows_[static_cast<std::size_t> (i)];
            r.title.setText (text ("mix.send") + " " + juce::String (i + 1), juce::dontSendNotification);
            r.title.setColour (juce::Label::textColourId, colour (Token::TextDim));
            addAndMakeVisible (r.title);
            r.dest.addItem (text ("mix.none"), 1);
            for (int b = 1; b <= mixer::numBuses; ++b)
            {
                r.dest.addItem ("Bus " + juce::String (b), b + 1);
                if (isBus_ && b <= index_ + 1) r.dest.setItemEnabled (b + 1, false);
            }
            r.dest.setSelectedId (s_.sends[static_cast<std::size_t> (i)].destination + 1, juce::dontSendNotification);
            r.dest.onChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].destination = rows_[static_cast<std::size_t> (i)].dest.getSelectedId() - 1; push(); };
            addAndMakeVisible (r.dest);
            r.tap.addItem (text ("mix.tap_pre_insert"), 1);
            r.tap.addItem (text ("mix.tap_pre_fader"), 2);
            r.tap.addItem (text ("mix.tap_post_fader"), 3);
            r.tap.addItem (text ("mix.tap_post_pan"), 4);
            r.tap.setSelectedId (static_cast<int> (s_.sends[static_cast<std::size_t> (i)].tap) + 1, juce::dontSendNotification);
            r.tap.onChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].tap = static_cast<mixer::TapPoint> (rows_[static_cast<std::size_t> (i)].tap.getSelectedId() - 1); push(); };
            addAndMakeVisible (r.tap);
            const ParamId lid = isBus_ ? mixer::busParam (index_, mixer::kBusSendLevel + static_cast<ParamId> (i))
                                       : probe::trackParam (index_, probe::kSendLevel + static_cast<ParamId> (i));
            r.level = std::make_unique<BoundSlider> (proc_, juce::Slider::LinearHorizontal);
            r.level->setRange (kFaderMinDb, 6.0, 0.1);
            r.level->setSkewFactorFromMidPoint (-24.0);
            r.level->bind (lid, -100.0);
            r.level->sync();
            addAndMakeVisible (*r.level);
            r.pan.setSliderStyle (juce::Slider::LinearHorizontal);
            r.pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            r.pan.setRange (-1.0, 1.0, 0.01);
            r.pan.setDoubleClickReturnValue (true, 0.0);
            r.pan.setValue (s_.sends[static_cast<std::size_t> (i)].pan, juce::dontSendNotification);
            r.pan.onValueChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].pan = rows_[static_cast<std::size_t> (i)].pan.getValue(); push(); };
            addAndMakeVisible (r.pan);
            r.mute.setButtonText ("M");
            r.mute.setClickingTogglesState (true);
            r.mute.setToggleState (s_.sends[static_cast<std::size_t> (i)].mute, juce::dontSendNotification);
            r.mute.onClick = [this, i] { s_.sends[static_cast<std::size_t> (i)].mute = rows_[static_cast<std::size_t> (i)].mute.getToggleState(); push(); };
            addAndMakeVisible (r.mute);
        }
        setSize (620, 400);
    }

    void paint (juce::Graphics& g) override { g.fillAll (colour (Token::BgCard)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto row = [&r] (juce::Label& l, juce::Component& c, int lw = 130)
        {
            auto line = r.removeFromTop (30);
            l.setBounds (line.removeFromLeft (lw));
            c.setBounds (line.removeFromLeft (200).reduced (2));
            return line;
        };
        auto extra = row (mainLabel_, mainBox_);
        mainOn_.setBounds (extra.reduced (2));
        row (directLabel_, directBox_);
        row (panLabel_, panBox_);
        r.removeFromTop (8);
        for (auto& row2 : rows_)
        {
            auto line = r.removeFromTop (30);
            row2.title.setBounds (line.removeFromLeft (60));
            row2.dest.setBounds (line.removeFromLeft (100).reduced (1));
            row2.tap.setBounds (line.removeFromLeft (130).reduced (1));
            row2.level->setBounds (line.removeFromLeft (150).reduced (1));
            row2.pan.setBounds (line.removeFromLeft (90).reduced (1));
            row2.mute.setBounds (line.removeFromLeft (30).reduced (1));
        }
    }

    void refreshLevels() { for (auto& r : rows_) r.level->sync(); }

private:
    juce::Colour colour (Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    void push()
    {
        if (isBus_) proc_.uiEditBus (index_, s_, "Bus routing", "routing");
        else proc_.uiEditTrack (index_, s_, "Track routing", "routing");
    }

    struct Row
    {
        juce::Label title;
        juce::ComboBox dest, tap;
        std::unique_ptr<BoundSlider> level;
        juce::Slider pan;
        juce::TextButton mute;
    };

    NeptuneProcessor& proc_;
    theme::Palette palette_;
    const i18n::Translator& tr_;
    bool isBus_;
    int index_;
    mixer::StripSettings s_;
    juce::Label mainLabel_, directLabel_, panLabel_;
    juce::ComboBox mainBox_, directBox_, panBox_;
    juce::ToggleButton mainOn_;
    std::array<Row, mixer::numSends> rows_;
};

// ------------------------------------------------------------------------------------------------ une bande

class MixerView::Strip : public juce::Component
{
public:
    Strip (MixerView& owner, bool isBus, int index)
        : owner_ (owner), isBus_ (isBus), index_ (index),
          pan_ (owner.processor_, juce::Slider::LinearHorizontal), fader_ (owner.processor_, juce::Slider::LinearVertical),
          mute_ (owner.processor_, "M"), solo_ (owner.processor_, "S")
    {
        auto& proc = owner.processor_;
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (juce::FontOptions (11.0f));
        name_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        name_.setEditable (false, true, false);
        name_.onTextChange = [this]
        {
            auto s = settings();
            s.name = name_.getText().toStdString();
            commit (s);
        };
        name_.addMouseListener (this, false);
        addAndMakeVisible (name_);

        if (! isBus)
        {
            typeButton_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.type_tip"))));
            typeButton_.onClick = [this] { showTypeMenu(); };
            addAndMakeVisible (typeButton_);
            removeButton_.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
            removeButton_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.remove_tip"))));
            removeButton_.onClick = [this] { owner_.requestRemove (index_); };
            addAndMakeVisible (removeButton_);
            for (const auto* item : { "All" }) midi_.addItem (item, 1);
            for (int c = 1; c <= 16; ++c) midi_.addItem (juce::String (c), c + 1);
            midi_.onChange = [this]
            {
                auto s = settings();
                s.midiChannel = midi_.getSelectedId() - 1;
                commit (s);
            };
            addAndMakeVisible (midi_);
        }
        routing_.setButtonText (juce::String::fromUTF8 (std::string (owner.tr_.tr ("mix.routing_short")).c_str()));
        routing_.onClick = [this]
        {
            auto panel = std::make_unique<RoutingPanel> (owner_.processor_, owner_.palette_, owner_.tr_, isBus_, index_);
            panelRef_ = panel.get();
            juce::CallOutBox::launchAsynchronously (std::move (panel), routing_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (routing_);

        pan_.setRange (-100.0, 100.0, 1.0);
        fader_.setRange (kFaderMinDb, kFaderMaxDb, 0.1);
        fader_.setSkewFactorFromMidPoint (-24.0);
        const ParamId pv = isBus ? mixer::busParam (index, mixer::kBusVolume) : probe::trackParam (index, probe::kVolume);
        const ParamId pp = isBus ? mixer::busParam (index, mixer::kBusPan) : probe::trackParam (index, probe::kPan);
        const ParamId pm = isBus ? mixer::busParam (index, mixer::kBusMute) : probe::trackParam (index, probe::kMute);
        const ParamId ps = isBus ? mixer::busParam (index, mixer::kBusSolo) : probe::trackParam (index, probe::kSolo);
        pan_.bind (pp, 0.0);
        fader_.bind (pv, 0.0);
        addAndMakeVisible (pan_);
        addAndMakeVisible (fader_);
        mute_.id = pm;
        solo_.id = ps;
        mute_.onToggle = [this, &proc] (bool)
        {
            mute_.setValueFromUi (proc.parameters().get (mute_.id) < 0.5);
        };
        solo_.onToggle = [this, &proc] (bool shift)
        {
            const bool turnOn = proc.parameters().get (solo_.id) < 0.5;
            if (shift && turnOn) owner_.exclusiveSolo (isBus_, index_); // solo exclusif : annule les autres solos
            solo_.setValueFromUi (turnOn);
        };
        mute_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::KnobHot));
        solo_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::Accent));
        addAndMakeVisible (mute_);
        addAndMakeVisible (solo_);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isBus_ && owner_.onSelectTrack && e.eventComponent != &fader_ && e.eventComponent != &pan_) owner_.onSelectTrack (index_);
    }

    void refresh (bool selected)
    {
        selected_ = selected;
        if (! isBus_)
        {
            const bool synth = owner_.processor_.trackType (index_) == TrackType::Synth;
            typeButton_.setButtonText (u8 (std::string (owner_.tr_.tr (synth ? "mix.type_short_synth" : "mix.type_short_sampler"))));
        }
        const auto s = settings();
        const juce::String fallback = (isBus_ ? "Bus " : "") + juce::String (index_ + 1);
        name_.setText (s.name.empty() ? fallback : u8 (s.name), juce::dontSendNotification);
        if (! isBus_) midi_.setSelectedId (s.midiChannel + 1, juce::dontSendNotification);
        colourIndex_ = s.color;
        repaint();
    }

    void syncValues()
    {
        pan_.sync();
        fader_.sync();
        mute_.sync();
        solo_.sync();
        if (panelRef_ != nullptr) panelRef_->refreshLevels();
    }

    void updateMeter (const Engine::Meter& m)
    {
        auto fall = [] (float current, float incoming) { return incoming > current ? incoming : current * 0.88f; };
        peakL_ = fall (peakL_, m.peakL);
        peakR_ = fall (peakR_, m.peakR);
        repaint (meterArea());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        auto head = r.removeFromTop (24);
        if (! isBus_)
        {
            removeButton_.setBounds (head.removeFromRight (20).reduced (0, 1));
            typeButton_.setBounds (head.reduced (0, 1));
        }
        name_.setBounds (r.removeFromTop (18));
        if (! isBus_) midi_.setBounds (r.removeFromTop (22).reduced (0, 1));
        routing_.setBounds (r.removeFromTop (22).reduced (0, 1));
        pan_.setBounds (r.removeFromTop (20));
        auto bottom = r.removeFromBottom (24);
        mute_.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1));
        solo_.setBounds (bottom.reduced (1));
        r.removeFromBottom (16); // valeur en dB
        faderArea_ = r;
        fader_.setBounds (r.removeFromLeft (r.getWidth() - 14));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (owner_.colour (selected_ ? Token::Nav1 : Token::BgCard));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (owner_.colour (selected_ ? Token::Accent : Token::Border));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, selected_ ? 2.0f : 1.0f);
        if (isBus_) // le haut d'une bande de bus : l'étiquette que les pistes remplacent par leur générateur
        {
            g.setColour (owner_.colour (Token::TextDim));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("BUS", 2, 6, getWidth() - 4, 14, juce::Justification::centred);
        }
        // pastille de couleur
        static const juce::uint32 palette[16] = { 0xff5c6bc0, 0xff26a69a, 0xffffa726, 0xffef5350, 0xff8d6e63, 0xff66bb6a, 0xffab47bc, 0xff42a5f5,
                                                 0xffffee58, 0xffec407a, 0xff78909c, 0xff26c6da, 0xffd4e157, 0xffff7043, 0xff7e57c2, 0xffbdbdbd };
        g.setColour (juce::Colour (palette[colourIndex_ & 15]));
        g.fillRect (juce::Rectangle<int> (4, 3, getWidth() - 8, 3));

        // vu-mètre stéréo
        const auto m = meterArea();
        auto bar = [&] (float lin, int x)
        {
            const float db = dbOf (lin);
            const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            g.setColour (owner_.colour (Token::Border));
            g.fillRect (x, m.getY(), 5, m.getHeight());
            g.setColour (db > 0.0f ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent2));
            g.fillRect (x, m.getBottom() - static_cast<int> (h), 5, static_cast<int> (h));
        };
        bar (peakL_, m.getX());
        bar (peakR_, m.getX() + 6);
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (fader_.getValue() <= kFaderMinDb ? juce::String::fromUTF8 ("-inf") : juce::String (fader_.getValue(), 1)),
                    2, getHeight() - 42, getWidth() - 4, 14, juce::Justification::centred);
    }

private:
    void showTypeMenu()
    {
        const auto current = owner_.processor_.trackType (index_);
        juce::PopupMenu menu;
        menu.addItem (1, u8 (std::string (owner_.tr_.tr ("mix.type_sampler"))), true, current == TrackType::Sampler);
        menu.addItem (2, u8 (std::string (owner_.tr_.tr ("mix.type_synth"))), true, current == TrackType::Synth);
        menu.addSeparator();
        menu.addItem (3, u8 (std::string (owner_.tr_.tr ("mix.type_fm"))), false);          // plus tard
        menu.addItem (4, u8 (std::string (owner_.tr_.tr ("mix.type_audio"))), false);       // plus tard
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typeButton_).withMinimumWidth (160),
                            [safe = juce::Component::SafePointer<Strip> (this)] (int choice)
                            {
                                if (safe == nullptr || choice == 0) return;
                                safe->owner_.requestTypeChange (safe->index_, choice == 2 ? TrackType::Synth : TrackType::Sampler);
                            });
    }

    juce::Rectangle<int> meterArea() const { return { getWidth() - 16, faderArea_.getY(), 12, faderArea_.getHeight() }; }
    mixer::StripSettings settings() const { return isBus_ ? owner_.processor_.busSettings (index_) : owner_.processor_.trackSettings (index_); }
    void commit (mixer::StripSettings s)
    {
        if (isBus_) owner_.processor_.uiEditBus (index_, std::move (s), "Bus settings", "strip");
        else owner_.processor_.uiEditTrack (index_, std::move (s), "Track settings", "strip");
    }
    static juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    MixerView& owner_;
    bool isBus_;
    int index_;
    bool selected_ = false;
    int colourIndex_ = 0;
    float peakL_ = 0.0f, peakR_ = 0.0f;
    juce::Label name_;
    juce::ComboBox midi_;
    juce::TextButton typeButton_, removeButton_;
    juce::TextButton routing_;
    BoundSlider pan_, fader_;
    BoundToggle mute_, solo_;
    juce::Rectangle<int> faderArea_;
    juce::Component::SafePointer<RoutingPanel> panelRef_;

    friend class MixerView;
};

// ------------------------------------------------------------------------------------------------ master

class MixerView::MasterStrip : public juce::Component
{
public:
    explicit MasterStrip (MixerView& owner)
        : owner_ (owner), pan_ (owner.processor_, juce::Slider::LinearHorizontal), trim_ (owner.processor_, juce::Slider::LinearHorizontal),
          fader_ (owner.processor_, juce::Slider::LinearVertical), mute_ (owner.processor_, "M"), mono_ (owner.processor_, "Mono")
    {
        title_.setText ("Master", juce::dontSendNotification);
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        addAndMakeVisible (title_);
        pan_.setRange (-100.0, 100.0, 1.0);
        pan_.bind (probe::masterPan, 0.0);
        trim_.setRange (-12.0, 12.0, 0.1);
        trim_.bind (mixer::masterTrim, 0.0);
        fader_.setRange (kFaderMinDb, kFaderMaxDb, 0.1);
        fader_.setSkewFactorFromMidPoint (-24.0);
        fader_.bind (probe::masterVolume, 0.0);
        mute_.id = probe::masterMute;
        mono_.id = mixer::masterMono;
        mute_.onToggle = [this] (bool) { mute_.setValueFromUi (owner_.processor_.parameters().get (mute_.id) < 0.5); };
        mono_.onToggle = [this] (bool) { mono_.setValueFromUi (owner_.processor_.parameters().get (mono_.id) < 0.5); };
        mute_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::KnobHot));
        mono_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::Accent));
        for (auto* c : std::initializer_list<juce::Component*> { &pan_, &trim_, &fader_, &mute_, &mono_ }) addAndMakeVisible (c);
        trimLabel_.setText ("Trim", juce::dontSendNotification);
        panLabel_.setText ("Pan", juce::dontSendNotification);
        for (auto* l : { &trimLabel_, &panLabel_ })
        {
            l->setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
            l->setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (*l);
        }
        for (int o = 1; o <= mixer::numOutputs; ++o) output_.addItem ("Out " + juce::String (o), o);
        output_.onChange = [this]
        {
            auto m = owner_.processor_.masterSettings();
            m.output = output_.getSelectedId();
            owner_.processor_.uiEditMaster (m, "Master output", "master");
        };
        addAndMakeVisible (output_);
        panMode_.addItem ("Balance", 1);
        panMode_.addItem ("True pan", 2);
        panMode_.onChange = [this]
        {
            auto m = owner_.processor_.masterSettings();
            m.panMode = panMode_.getSelectedId() == 2 ? mixer::PanMode::True : mixer::PanMode::Balance;
            owner_.processor_.uiEditMaster (m, "Master pan mode", "master");
        };
        addAndMakeVisible (panMode_);
    }

    void refresh()
    {
        const auto m = owner_.processor_.masterSettings();
        output_.setSelectedId (m.output, juce::dontSendNotification);
        panMode_.setSelectedId (m.panMode == mixer::PanMode::True ? 2 : 1, juce::dontSendNotification);
    }

    void syncValues()
    {
        pan_.sync(); trim_.sync(); fader_.sync(); mute_.sync(); mono_.sync();
    }

    void updateMeter (const Engine::Meter& m)
    {
        auto fall = [] (float current, float incoming) { return incoming > current ? incoming : current * 0.88f; };
        peakL_ = fall (peakL_, m.peakL);
        peakR_ = fall (peakR_, m.peakR);
        rmsL_ = 0.8f * rmsL_ + 0.2f * m.rmsL;
        rmsR_ = 0.8f * rmsR_ + 0.2f * m.rmsR;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (3);
        title_.setBounds (r.removeFromTop (20));
        output_.setBounds (r.removeFromTop (24).reduced (0, 1));
        panMode_.setBounds (r.removeFromTop (24).reduced (0, 1));
        auto line = r.removeFromTop (18);
        trimLabel_.setBounds (line);
        trim_.setBounds (r.removeFromTop (18));
        line = r.removeFromTop (18);
        panLabel_.setBounds (line);
        pan_.setBounds (r.removeFromTop (18));
        auto bottom = r.removeFromBottom (26);
        mute_.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1));
        mono_.setBounds (bottom.reduced (1));
        r.removeFromBottom (16);
        faderArea_ = r;
        fader_.setBounds (r.removeFromLeft (r.getWidth() - 40));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (owner_.colour (Token::BgCard));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (owner_.colour (Token::Accent));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
        const juce::Rectangle<int> m (getWidth() - 38, faderArea_.getY(), 34, faderArea_.getHeight());
        auto bar = [&] (float lin, float rms, int x)
        {
            const float db = dbOf (lin), rdb = dbOf (rms);
            const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            const float rh = juce::jlimit (0.0f, 1.0f, (rdb + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            g.setColour (owner_.colour (Token::Border));
            g.fillRect (x, m.getY(), 12, m.getHeight());
            g.setColour (db > 0.0f ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent2));
            g.fillRect (x, m.getBottom() - static_cast<int> (h), 12, static_cast<int> (h));
            g.setColour (owner_.colour (Token::Text).withAlpha (0.6f)); // RMS
            g.fillRect (x + 3, m.getBottom() - static_cast<int> (rh), 6, static_cast<int> (rh));
        };
        bar (peakL_, rmsL_, m.getX());
        bar (peakR_, rmsR_, m.getX() + 15);
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (fader_.getValue() <= kFaderMinDb ? juce::String::fromUTF8 ("-inf") : juce::String (fader_.getValue(), 1),
                    2, getHeight() - 42, getWidth() - 4, 14, juce::Justification::centred);
    }

private:
    MixerView& owner_;
    juce::Label title_, trimLabel_, panLabel_;
    juce::ComboBox output_, panMode_;
    BoundSlider pan_, trim_, fader_;
    BoundToggle mute_, mono_;
    juce::Rectangle<int> faderArea_;
    float peakL_ = 0.0f, peakR_ = 0.0f, rmsL_ = 0.0f, rmsR_ = 0.0f;
};

// ------------------------------------------------------------------------------------------------ la vue

MixerView::MixerView (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr)
    : processor_ (p), palette_ (palette), tr_ (tr)
{
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        tracks_[static_cast<std::size_t> (t)] = std::make_unique<Strip> (*this, false, t);
        addAndMakeVisible (*tracks_[static_cast<std::size_t> (t)]);
    }
    for (int b = 0; b < mixer::numBuses; ++b)
    {
        buses_[static_cast<std::size_t> (b)] = std::make_unique<Strip> (*this, true, b);
        addAndMakeVisible (*buses_[static_cast<std::size_t> (b)]);
    }
    master_ = std::make_unique<MasterStrip> (*this);
    addAndMakeVisible (*master_);
    addTrack_.setButtonText (juce::String::fromUTF8 (std::string (tr.tr ("mix.add_track")).c_str()));
    addTrack_.onClick = [this]
    {
        if (processor_.uiAddTrack() >= 0) refresh();
    };
    addAndMakeVisible (addTrack_);
    tracksTitle_.setText (juce::String::fromUTF8 (std::string (tr.tr ("mix.tracks")).c_str()), juce::dontSendNotification);
    busesTitle_.setText (juce::String::fromUTF8 (std::string (tr.tr ("mix.buses")).c_str()), juce::dontSendNotification);
    for (auto* l : { &tracksTitle_, &busesTitle_ })
    {
        l->setColour (juce::Label::textColourId, colour (Token::TextDim));
        addAndMakeVisible (*l);
    }
    startTimerHz (30);
    refresh();
}

MixerView::~MixerView() { stopTimer(); }

juce::Colour MixerView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void MixerView::resized()
{
    layoutTracks();
}

// Pistes actives packées à gauche, le bouton « + Piste » juste après la dernière ; bus et master à leur place.
void MixerView::layoutTracks()
{
    auto r = getLocalBounds();
    auto masterArea = r.removeFromRight (kMasterW + 6);
    master_->setBounds (masterArea.withTrimmedLeft (6).withHeight (kStripH * 2 + 40));
    const int gap = 1;
    tracksTitle_.setBounds (r.removeFromTop (20));
    {
        auto area = r.removeFromTop (kStripH);
        int x = area.getX();
        for (int t = 0; t < mixer::numTracks; ++t)
        {
            auto& s = *tracks_[static_cast<std::size_t> (t)];
            if (! shownTracks_[static_cast<std::size_t> (t)]) continue;
            s.setBounds (x, area.getY(), kStripW, kStripH);
            x += kStripW + gap;
        }
        addTrack_.setVisible (processor_.activeTrackCount() < mixer::numTracks);
        addTrack_.setBounds (x + 4, area.getY() + 4, kStripW + 20, 36);
    }
    busesTitle_.setBounds (r.removeFromTop (20));
    {
        auto area = r.removeFromTop (kStripH);
        int x = area.getX();
        for (auto& s : buses_)
        {
            s->setBounds (x, area.getY(), kStripW, kStripH);
            x += kStripW + gap;
        }
    }
}

void MixerView::paint (juce::Graphics&) {}

void MixerView::refresh()
{
    const int selected = processor_.selectedTrack();
    bool layoutChanged = false;
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        const bool active = processor_.trackActive (t);
        auto& s = *tracks_[static_cast<std::size_t> (t)];
        if (active != shownTracks_[static_cast<std::size_t> (t)]) { shownTracks_[static_cast<std::size_t> (t)] = active; layoutChanged = true; }
        s.setVisible (active);
        if (active) s.refresh (t == selected);
    }
    for (int b = 0; b < mixer::numBuses; ++b) buses_[static_cast<std::size_t> (b)]->refresh (false);
    master_->refresh();
    if (layoutChanged) layoutTracks();
}

void MixerView::timerCallback()
{
    if (! isShowing()) return;
    const auto rev = processor_.instrumentRevision();
    if (rev != lastRevision_)
    {
        lastRevision_ = rev;
        refresh();
    }
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        if (! shownTracks_[static_cast<std::size_t> (t)]) continue;
        auto& s = *tracks_[static_cast<std::size_t> (t)];
        s.syncValues();
        s.updateMeter (processor_.trackMeter (t));
    }
    for (int b = 0; b < mixer::numBuses; ++b)
    {
        auto& s = *buses_[static_cast<std::size_t> (b)];
        s.syncValues();
        s.updateMeter (processor_.busMeter (b));
    }
    master_->syncValues();
    master_->updateMeter (processor_.masterMeter());
}

// Changement de générateur : la configuration d'instrument est perdue (spec §1), donc une confirmation si la piste en a une.
void MixerView::requestTypeChange (int track, TrackType type)
{
    if (processor_.trackType (track) == type) return;
    auto apply = [safe = juce::Component::SafePointer<MixerView> (this), track, type]
    {
        if (safe == nullptr) return;
        safe->processor_.uiSetTrackType (track, type);
        safe->refresh();
    };
    if (! processor_.trackHasContent (track)) { apply(); return; }
    auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("mix.confirm_title"),
                                                                                text ("mix.confirm_type"), text ("mix.continue"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); }); // 1 : premier bouton
}

void MixerView::requestRemove (int track)
{
    if (processor_.activeTrackCount() <= 1) return; // le mixeur garde toujours une piste
    auto apply = [safe = juce::Component::SafePointer<MixerView> (this), track]
    {
        if (safe == nullptr) return;
        safe->processor_.uiRemoveTrack (track);
        safe->refresh();
    };
    if (! processor_.trackHasContent (track)) { apply(); return; }
    auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    const auto message = juce::String::fromUTF8 (tr_.format ("mix.confirm_remove", { std::to_string (track + 1) }).c_str());
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("mix.confirm_title"),
                                                                                message, text ("mix.continue"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); });
}

// Solo exclusif (Maj+clic) : coupe les solos et les mutes des autres bandes du même domaine (pistes ou bus).
void MixerView::exclusiveSolo (bool isBus, int index)
{
    auto setParam = [this] (ParamId id, double v)
    {
        const auto* spec = processor_.parameters().find (id);
        if (spec == nullptr || processor_.parameters().get (id) == v) return;
        processor_.uiBeginEdit (id);
        processor_.uiSetNormalised (id, spec->toNormalized (v));
        processor_.uiEndEdit (id);
    };
    if (! isBus)
        for (int t = 0; t < mixer::numTracks; ++t)
        {
            if (t == index) continue;
            setParam (probe::trackParam (t, probe::kSolo), 0.0);
        }
    else
        for (int b = 0; b < mixer::numBuses; ++b)
        {
            if (b == index) continue;
            setParam (mixer::busParam (b, mixer::kBusSolo), 0.0);
        }
}
