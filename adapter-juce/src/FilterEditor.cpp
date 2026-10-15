#include "FilterEditor.h"
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;

// Un filtre : ses commandes et son enveloppe.
struct FilterEditor::Column
{
    Column (FilterEditor& o, int index) : owner (o), idx (index), env (o.palette_, o.tr_)
    {
        auto label = [this] (juce::Label& l, const char* key)
        {
            l.setText (owner.text (key), juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
            owner.addAndMakeVisible (l);
        };
        title.setText (owner.text (idx == 0 ? "filt.filter1" : "filt.filter2"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        owner.addAndMakeVisible (title);
        on.setButtonText (owner.text ("filt.on"));
        on.setColour (juce::ToggleButton::textColourId, owner.colour (Token::Text));
        owner.addAndMakeVisible (on);

        label (modelLabel, "filt.model");
        label (modeLabel, "filt.mode");
        label (slopeLabel, "filt.slope");
        label (cutoffLabel, "filt.cutoff");
        label (resLabel, "filt.resonance");
        label (keyLabel, "filt.key_track");
        label (velLabel, "filt.velocity");
        label (depthLabel, "filt.env_depth");
        label (driveLabel, "filt.drive");

        model.addItem (owner.text ("filt.model_svf"), 1);
        model.addItem (owner.text ("filt.model_ladder"), 2);
        mode.addItem (owner.text ("filt.mode_lp"), 1);
        mode.addItem (owner.text ("filt.mode_hp"), 2);
        mode.addItem (owner.text ("filt.mode_bp"), 3);
        mode.addItem (owner.text ("filt.mode_notch"), 4);
        slope.addItem ("12 dB", 1);
        slope.addItem ("24 dB", 2);
        for (auto* c : { &model, &mode, &slope }) owner.addAndMakeVisible (*c);

        auto setup = [this] (juce::Slider& s, double lo, double hi, double step, const char* suffix)
        {
            s.setRange (lo, hi, step);
            s.setTextValueSuffix (suffix);
            s.setDoubleClickReturnValue (true, 0.0);
            s.onDragStart = [this] { owner.dragging_ = true; if (owner.onGestureBegin) owner.onGestureBegin(); };
            s.onDragEnd = [this] { owner.dragging_ = false; if (owner.onGestureEnd) owner.onGestureEnd(); };
            owner.addAndMakeVisible (s);
        };
        setup (cutoff, filterMinHz, filterMaxHz, 0.1, " Hz");
        cutoff.setSkewFactorFromMidPoint (1000.0);
        cutoff.setDoubleClickReturnValue (true, filterMaxHz);
        setup (resonance, 0.0, 100.0, 1.0, " %");
        setup (keyTrack, -100.0, 200.0, 1.0, " %");
        setup (velocity, -6.0, 6.0, 0.1, " oct");
        setup (depth, -8.0, 8.0, 0.1, " oct");
        setup (drive, 0.0, 100.0, 1.0, " %");

        for (auto* dial : { &keyTrack, &velocity, &depth, &drive }) // potards plutôt que curseurs
        {
            dial->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            dial->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            dial->setColour (juce::Slider::rotarySliderFillColourId, owner.colour (Token::Accent));
            dial->setColour (juce::Slider::rotarySliderOutlineColourId, owner.colour (Token::KnobTrack));
            dial->setColour (juce::Slider::thumbColourId, owner.colour (Token::KnobDot));
            dial->setColour (juce::Slider::textBoxTextColourId, owner.colour (Token::Accent));
            dial->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
        for (auto* l : { &keyLabel, &velLabel, &depthLabel, &driveLabel }) l->setJustificationType (juce::Justification::centred);

        auto hook = [this] { return [this] { if (! owner.updating_) apply(); }; };
        on.onClick = hook();
        model.onChange = hook();
        mode.onChange = hook();
        slope.onChange = hook();
        keyTrack.onValueChange = hook();
        velocity.onValueChange = hook();
        depth.onValueChange = hook();
        drive.onValueChange = hook();
        cutoff.onValueChange = [this]
        {
            if (owner.updating_) return;
            if (owner.target_ == 0)
            {
                if (owner.onParam) owner.onParam (idx * 2, cutoff.getValue(), 1);
            }
            else apply();
        };
        resonance.onValueChange = [this]
        {
            if (owner.updating_) return;
            if (owner.target_ == 0)
            {
                if (owner.onParam) owner.onParam (idx * 2 + 1, resonance.getValue() / 100.0 * 100.0, 1); // paramètre en %
            }
            else apply();
        };
        // coupure et résonance de l'instrument : paramètres (début / fin de geste pour l'hôte)
        cutoff.onDragStart = [this]
        {
            owner.dragging_ = true;
            if (owner.target_ == 0) { if (owner.onParam) owner.onParam (idx * 2, cutoff.getValue(), 0); }
            else if (owner.onGestureBegin) owner.onGestureBegin();
        };
        cutoff.onDragEnd = [this]
        {
            owner.dragging_ = false;
            if (owner.target_ == 0) { if (owner.onParam) owner.onParam (idx * 2, cutoff.getValue(), 2); }
            else if (owner.onGestureEnd) owner.onGestureEnd();
        };
        resonance.onDragStart = [this]
        {
            owner.dragging_ = true;
            if (owner.target_ == 0) { if (owner.onParam) owner.onParam (idx * 2 + 1, resonance.getValue(), 0); }
            else if (owner.onGestureBegin) owner.onGestureBegin();
        };
        resonance.onDragEnd = [this]
        {
            owner.dragging_ = false;
            if (owner.target_ == 0) { if (owner.onParam) owner.onParam (idx * 2 + 1, resonance.getValue(), 2); }
            else if (owner.onGestureEnd) owner.onGestureEnd();
        };

        env.onGestureBegin = [this] { owner.dragging_ = true; if (owner.onGestureBegin) owner.onGestureBegin(); };
        env.onGestureEnd = [this] { owner.dragging_ = false; if (owner.onGestureEnd) owner.onGestureEnd(); };
        env.onChange = [this] (const EnvelopeDescription& e)
        {
            if (owner.updating_ || ! owner.editable()) return;
            auto b = owner.current();
            b.filter[static_cast<std::size_t> (idx)].env = e;
            owner.emit (b);
        };
        owner.addAndMakeVisible (env);
    }

    // Lit les commandes et rend le bloc modifié.
    void apply()
    {
        if (! owner.editable()) return;
        auto b = owner.current();
        auto& f = b.filter[static_cast<std::size_t> (idx)];
        f.on = on.getToggleState();
        f.model = model.getSelectedId() == 2 ? FilterModel::Ladder : FilterModel::SVF;
        const int m = mode.getSelectedId();
        f.mode = m == 2 ? FilterMode::HighPass : m == 3 ? FilterMode::BandPass : m == 4 ? FilterMode::Notch : FilterMode::LowPass;
        f.slopeDb = slope.getSelectedId() == 2 ? 24 : 12;
        f.keyTrack = keyTrack.getValue() / 100.0;
        f.velocityOctaves = velocity.getValue();
        f.envDepthOctaves = depth.getValue();
        f.drive = drive.getValue() / 100.0;
        if (owner.target_ != 0)
        {
            f.cutoffHz = cutoff.getValue();
            f.resonance = resonance.getValue() / 100.0;
        }
        owner.emit (b);
    }

    void show (const FilterSettings& f, bool paramControlled, double paramCutoff, double paramRes, bool enabled)
    {
        on.setToggleState (f.on, juce::dontSendNotification);
        model.setSelectedId (f.model == FilterModel::Ladder ? 2 : 1, juce::dontSendNotification);
        mode.setSelectedId (f.mode == FilterMode::HighPass ? 2 : f.mode == FilterMode::BandPass ? 3 : f.mode == FilterMode::Notch ? 4 : 1, juce::dontSendNotification);
        slope.setSelectedId (f.slopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
        cutoff.setValue (paramControlled ? paramCutoff : f.cutoffHz, juce::dontSendNotification);
        resonance.setValue ((paramControlled ? paramRes : f.resonance) * 100.0, juce::dontSendNotification);
        keyTrack.setValue (f.keyTrack * 100.0, juce::dontSendNotification);
        velocity.setValue (f.velocityOctaves, juce::dontSendNotification);
        depth.setValue (f.envDepthOctaves, juce::dontSendNotification);
        drive.setValue (f.drive * 100.0, juce::dontSendNotification);
        env.setEnvelope (f.env);
        const bool ladder = f.model == FilterModel::Ladder;
        for (auto* c : std::initializer_list<juce::Component*> { &on, &model, &mode, &slope, &cutoff, &resonance, &keyTrack, &velocity, &depth, &drive, &env })
            c->setEnabled (enabled);
        mode.setEnabled (enabled && ! ladder);   // le Ladder est toujours passe-bas
        drive.setEnabled (enabled && ladder);
        driveLabel.setEnabled (ladder);
        modeLabel.setEnabled (! ladder);
    }

    void layout (juce::Rectangle<int> r)
    {
        auto head = r.removeFromTop (30);
        title.setBounds (head.removeFromLeft (160));
        on.setBounds (head);
        auto row = [&r] (juce::Label& l, juce::Component& c, int w = 130)
        {
            auto line = r.removeFromTop (30);
            l.setBounds (line.removeFromLeft (w));
            c.setBounds (line.reduced (2));
        };
        row (modelLabel, model);
        row (modeLabel, mode);
        row (slopeLabel, slope);
        row (cutoffLabel, cutoff);
        row (resLabel, resonance);
        {
            auto dials = r.removeFromTop (100); // suivi du clavier, vélocité, profondeur d'enveloppe, drive
            const int cw = dials.getWidth() / 4;
            auto place = [&dials, cw] (juce::Label& l, juce::Slider& s)
            {
                auto cell = dials.removeFromLeft (cw);
                l.setBounds (cell.removeFromTop (18));
                s.setBounds (cell.reduced (4, 0));
            };
            place (keyLabel, keyTrack);
            place (velLabel, velocity);
            place (depthLabel, depth);
            place (driveLabel, drive);
        }
        r.removeFromTop (6);
        env.setBounds (r);
    }

    FilterEditor& owner;
    int idx;
    juce::Label title, modelLabel, modeLabel, slopeLabel, cutoffLabel, resLabel, keyLabel, velLabel, depthLabel, driveLabel;
    juce::ToggleButton on;
    juce::ComboBox model, mode, slope;
    juce::Slider cutoff { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider resonance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider keyTrack { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider velocity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider depth { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider drive { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    EnvelopeEditor env;
};

FilterEditor::FilterEditor (const theme::Palette& palette, const i18n::Translator& tr) : palette_ (palette), tr_ (tr)
{
    setWantsKeyboardFocus (true);
    titleLabel_.setText (text ("filt.title"), juce::dontSendNotification);
    titleLabel_.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel_.setColour (juce::Label::textColourId, colour (Token::Text));
    addAndMakeVisible (titleLabel_);

    targetLabel_.setText (text ("filt.target"), juce::dontSendNotification);
    targetLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (targetLabel_);
    targetBox_.addItem (text ("filt.target_instrument"), 1);
    targetBox_.addItem (text ("filt.target_zones"), 2);
    targetBox_.setSelectedId (1, juce::dontSendNotification);
    targetBox_.onChange = [this] { target_ = targetBox_.getSelectedId() - 1; refresh(); };
    addAndMakeVisible (targetBox_);

    overrideToggle_.setButtonText (text ("filt.override"));
    overrideToggle_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    overrideToggle_.onClick = [this]
    {
        if (updating_ || target_ != 1) return;
        zoneOverride_ = overrideToggle_.getToggleState();
        // « Personnaliser » part d'une copie du réglage d'instrument : rien ne change d'un coup à l'oreille
        if (zoneOverride_) zoneBlock_ = defaultBlock_;
        if (onZonesChanged) onZonesChanged (zoneOverride_, zoneBlock_);
        refresh();
    };
    addAndMakeVisible (overrideToggle_);

    routingLabel_.setText (text ("filt.routing"), juce::dontSendNotification);
    routingLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (routingLabel_);
    routingBox_.addItem (text ("filt.series"), 1);
    routingBox_.addItem (text ("filt.parallel"), 2);
    routingBox_.onChange = [this]
    {
        if (updating_ || ! editable()) return;
        auto b = current();
        b.parallel = routingBox_.getSelectedId() == 2;
        emit (b);
    };
    addAndMakeVisible (routingBox_);
    balanceLabel_.setText (text ("filt.balance"), juce::dontSendNotification);
    balanceLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (balanceLabel_);
    balance_.setRange (0.0, 100.0, 1.0);
    balance_.setTextValueSuffix (" %");
    balance_.onDragStart = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
    balance_.onDragEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
    balance_.onValueChange = [this]
    {
        if (updating_ || ! editable()) return;
        auto b = current();
        b.balance = balance_.getValue() / 100.0;
        emit (b);
    };
    addAndMakeVisible (balance_);

    noteLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    noteLabel_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (noteLabel_);

    closeButton_.setButtonText (text ("filt.close"));
    closeButton_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton_);

    columns_[0] = std::make_unique<Column> (*this, 0);
    columns_[1] = std::make_unique<Column> (*this, 1);
}

FilterEditor::~FilterEditor() = default;

juce::Colour FilterEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String FilterEditor::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void FilterEditor::setState (const FilterBlock& defaultBlock, const DefaultParams& params, int selectedZones, bool zonesOverride,
                             const FilterBlock& zoneBlock)
{
    if (dragging_) return;
    defaultBlock_ = defaultBlock;
    params_ = params;
    selectedZones_ = selectedZones;
    zoneOverride_ = zonesOverride;
    zoneBlock_ = zoneBlock;
    if (selectedZones_ == 0 && target_ == 1) { target_ = 0; }
    refresh();
}

void FilterEditor::refresh()
{
    updating_ = true;
    targetBox_.setSelectedId (target_ + 1, juce::dontSendNotification);
    targetBox_.setItemEnabled (2, selectedZones_ > 0);
    overrideToggle_.setVisible (target_ == 1);
    overrideToggle_.setToggleState (zoneOverride_, juce::dontSendNotification);
    const auto& b = current();
    const bool paramControlled = target_ == 0;
    for (int i = 0; i < 2; ++i)
        columns_[i]->show (b.filter[static_cast<std::size_t> (i)], paramControlled, params_.cutoffHz[i], params_.resonance[i], editable());
    routingBox_.setSelectedId (b.parallel ? 2 : 1, juce::dontSendNotification);
    balance_.setValue (b.balance * 100.0, juce::dontSendNotification);
    routingBox_.setEnabled (editable());
    balance_.setEnabled (editable() && b.parallel);
    balanceLabel_.setEnabled (b.parallel);
    noteLabel_.setText (target_ == 0 ? text ("filt.note_instrument")
                                     : (zoneOverride_ ? juce::String::fromUTF8 (tr_.format ("filt.note_zones_own", { std::to_string (selectedZones_) }).c_str())
                                                      : text ("filt.note_zones_inherit")),
                        juce::dontSendNotification);
    updating_ = false;
}

void FilterEditor::emit (const FilterBlock& b)
{
    if (target_ == 0)
    {
        defaultBlock_ = b;
        if (onDefaultChanged) onDefaultChanged (b);
    }
    else
    {
        zoneBlock_ = b;
        if (onZonesChanged) onZonesChanged (true, b);
    }
}

void FilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    g.setColour (colour (Token::BgCard));
    g.fillRoundedRectangle (panelBounds_.toFloat(), 8.0f);
    g.setColour (colour (Token::Border));
    g.drawRoundedRectangle (panelBounds_.toFloat(), 8.0f, 1.0f);
}

void FilterEditor::resized()
{
    panelBounds_ = getLocalBounds().reduced (24, 16);
    auto r = panelBounds_.reduced (14, 10);
    auto top = r.removeFromTop (34);
    closeButton_.setBounds (top.removeFromRight (110).reduced (2));
    titleLabel_.setBounds (top.removeFromLeft (200));
    targetLabel_.setBounds (top.removeFromLeft (70));
    targetBox_.setBounds (top.removeFromLeft (260).reduced (2));
    overrideToggle_.setBounds (top.removeFromLeft (300));

    auto route = r.removeFromTop (34);
    routingLabel_.setBounds (route.removeFromLeft (90));
    routingBox_.setBounds (route.removeFromLeft (160).reduced (2));
    route.removeFromLeft (16);
    balanceLabel_.setBounds (route.removeFromLeft (130));
    balance_.setBounds (route.removeFromLeft (300).reduced (2));
    noteLabel_.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);

    const int gap = 18;
    const int w = (r.getWidth() - gap) / 2;
    columns_[0]->layout (r.removeFromLeft (w));
    r.removeFromLeft (gap);
    columns_[1]->layout (r);
}

bool FilterEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (! (m.isCommandDown() || m.isCtrlDown())) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z')
    {
        if (m.isShiftDown()) { if (onRedo) onRedo(); } else if (onUndo) onUndo();
        return true;
    }
    if (code == 'Y' || code == 'y') { if (onRedo) onRedo(); return true; }
    return false;
}
