#include "SynthView.h"

using namespace neptune::core;
using neptune::core::theme::Token;

// ------------------------------------------------------------------------------------------------ clavier

SynthKeyboard::SynthKeyboard (const theme::Palette& palette) : palette_ (palette) {}

juce::Colour SynthKeyboard::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

bool SynthKeyboard::isBlack (int key) const noexcept
{
    const int n = key % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

void SynthKeyboard::shiftOctave (int octaves)
{
    const int next = juce::jlimit (12, 84, firstKey_ + 12 * octaves);
    if (next == firstKey_) return;
    releaseAll();
    firstKey_ = next;
    repaint();
}

juce::Rectangle<float> SynthKeyboard::keyRect (int key) const
{
    int whites = 0;
    for (int k = firstKey_; k < firstKey_ + numKeys; ++k) whites += isBlack (k) ? 0 : 1;
    const float w = static_cast<float> (getWidth()) / static_cast<float> (whites);
    int index = 0; // rang de la touche blanche (ou de la blanche qui précède une noire)
    for (int k = firstKey_; k < key; ++k) index += isBlack (k) ? 0 : 1;
    const float h = static_cast<float> (getHeight());
    if (! isBlack (key)) return { static_cast<float> (index) * w, 0.0f, w, h };
    return { static_cast<float> (index) * w - 0.3f * w, 0.0f, 0.6f * w, 0.6f * h };
}

int SynthKeyboard::keyAt (juce::Point<int> p) const
{
    const auto pf = p.toFloat();
    for (int k = firstKey_; k < firstKey_ + numKeys; ++k) // noires d'abord : elles sont au-dessus
        if (isBlack (k) && keyRect (k).contains (pf)) return k;
    for (int k = firstKey_; k < firstKey_ + numKeys; ++k)
        if (! isBlack (k) && keyRect (k).contains (pf)) return k;
    return -1;
}

void SynthKeyboard::paint (juce::Graphics& g)
{
    const auto accent = colour (Token::Accent);
    for (int pass = 0; pass < 2; ++pass)
        for (int k = firstKey_; k < firstKey_ + numKeys; ++k)
        {
            const bool black = isBlack (k);
            if (black != (pass == 1)) continue;
            const auto r = keyRect (k);
            const bool on = down_.count (k) > 0 || latched_.count (k) > 0;
            g.setColour (on ? accent : (black ? juce::Colour (0xff1a1a1a) : juce::Colour (0xfff2f2f2)));
            g.fillRect (r.reduced (0.5f));
            g.setColour (juce::Colour (0xff555555));
            g.drawRect (r, 1.0f);
            if (k % 12 == 0 && ! black) // do : nom de la note (do central = C4 = 60)
            {
                g.setColour (on ? juce::Colour (0xff000000) : juce::Colour (0xff666666));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("C" + juce::String (k / 12 - 1), r.withTrimmedBottom (4.0f).toNearestInt(), juce::Justification::centredBottom);
            }
        }
}

void SynthKeyboard::press (int source, int key, bool latch)
{
    if (key < 0) return;
    if (latch) // maintien : bascule la touche
    {
        if (latched_.count (key) > 0)
        {
            latched_.erase (key);
            if (onNoteOff) onNoteOff (key);
        }
        else
        {
            latched_.insert (key);
            if (onNoteOn) onNoteOn (key, 0.8f);
        }
        sourceKey_.erase (source);
    }
    else
    {
        sourceKey_[source] = key;
        down_.insert (key);
        if (onNoteOn) onNoteOn (key, 0.8f);
    }
    repaint();
}

void SynthKeyboard::release (int source)
{
    const auto it = sourceKey_.find (source);
    if (it == sourceKey_.end()) return;
    const int key = it->second;
    sourceKey_.erase (it);
    // une autre source peut tenir la même touche : on ne la coupe que si plus personne ne la tient
    bool stillDown = false;
    for (const auto& kv : sourceKey_) stillDown = stillDown || kv.second == key;
    if (! stillDown)
    {
        down_.erase (key);
        if (onNoteOff) onNoteOff (key);
    }
    repaint();
}

void SynthKeyboard::mouseDown (const juce::MouseEvent& e)
{
    press (e.source.getIndex(), keyAt (e.getPosition()), hold_ || e.mods.isShiftDown());
}

void SynthKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    const int source = e.source.getIndex();
    const auto it = sourceKey_.find (source);
    if (it == sourceKey_.end()) return; // clic de maintien : pas de glissé
    const int key = keyAt (e.getPosition());
    if (key < 0 || key == it->second) return;
    release (source);
    press (source, key, false);
}

void SynthKeyboard::mouseUp (const juce::MouseEvent& e) { release (e.source.getIndex()); }

void SynthKeyboard::releaseAll()
{
    for (const int k : latched_) if (onNoteOff) onNoteOff (k);
    std::set<int> keys;
    for (const auto& kv : sourceKey_) keys.insert (kv.second);
    for (const int k : keys) if (onNoteOff) onNoteOff (k);
    latched_.clear();
    sourceKey_.clear();
    down_.clear();
    repaint();
}

// ------------------------------------------------------------------------------------------------ vue

SynthView::SynthView (const theme::Palette& palette, const i18n::Translator& tr)
    : palette_ (palette), tr_ (tr), ampEnv_ (palette, tr), filterEnv_ (palette, tr), keyboard_ (palette)
{
    auto label = [this] (juce::Label& l, const char* key, bool dim = true)
    {
        l.setText (text (key), juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colour (dim ? Token::TextDim : Token::Text));
        addAndMakeVisible (l);
    };
    label (waveLabel_, "synth.waveform");
    label (octaveLabel_, "synth.octave");
    label (modeLabel_, "poly.mode");
    label (voicesLabel_, "poly.limit");
    label (stealLabel_, "poly.steal");
    label (glideLabel_, "poly.glide");
    label (ampTitle_, "synth.amp_env", false);
    label (filterTitle_, "synth.filter", false);
    label (filterEnvTitle_, "synth.filter_env", true);
    label (keyLabel_, "filt.key_track");
    label (velLabel_, "filt.velocity");
    label (depthLabel_, "filt.env_depth");
    label (driveLabel_, "filt.drive");
    for (auto* l : { &ampTitle_, &filterTitle_ }) l->setFont (juce::FontOptions (16.0f, juce::Font::bold));

    hint_.setText (text ("synth.cutoff_hint"), juce::dontSendNotification);
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint_);
    keysHint_.setText (text ("synth.keys_hint"), juce::dontSendNotification);
    keysHint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    keysHint_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (keysHint_);

    // forme d'onde : quatre boutons exclusifs
    const char* waveKeys[numWaveforms] = { "synth.wave_sine", "synth.wave_triangle", "synth.wave_saw", "synth.wave_square" };
    for (int i = 0; i < numWaveforms; ++i)
    {
        auto& b = waveButtons_[i];
        b.setButtonText (text (waveKeys[i]));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4701);
        b.setColour (juce::TextButton::buttonOnColourId, colour (Token::Accent));
        b.onClick = [this, i]
        {
            if (updating_ || ! waveButtons_[i].getToggleState()) return;
            d_.waveform = static_cast<Waveform> (i);
            if (onGestureBegin) onGestureBegin();
            emit();
            if (onGestureEnd) onGestureEnd();
        };
        addAndMakeVisible (b);
    }

    octave_.setRange (SynthDescription::minOctave, SynthDescription::maxOctave, 1);
    octave_.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    octave_.onValueChange = [this] { if (! updating_) discrete(); };
    addAndMakeVisible (octave_);

    mode_.addItem (text ("poly.mode_poly"), 1);
    mode_.addItem (text ("poly.mode_mono"), 2);
    mode_.addItem (text ("poly.mode_legato"), 3);
    steal_.addItem (text ("poly.steal_oldest"), 1);
    steal_.addItem (text ("poly.steal_quietest"), 2);
    steal_.addItem (text ("poly.steal_keep_low"), 3);
    steal_.addItem (text ("poly.steal_keep_high"), 4);
    model_.addItem (text ("filt.model_svf"), 1);
    model_.addItem (text ("filt.model_ladder"), 2);
    filterMode_.addItem (text ("filt.mode_lp"), 1);
    filterMode_.addItem (text ("filt.mode_hp"), 2);
    filterMode_.addItem (text ("filt.mode_bp"), 3);
    filterMode_.addItem (text ("filt.mode_notch"), 4);
    slope_.addItem ("12 dB", 1);
    slope_.addItem ("24 dB", 2);
    for (auto* c : { &mode_, &steal_, &model_, &filterMode_, &slope_ })
    {
        c->onChange = [this] { if (! updating_) discrete(); };
        addAndMakeVisible (*c);
    }
    filterOn_.setButtonText (text ("filt.on"));
    filterOn_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    filterOn_.onClick = [this] { if (! updating_) discrete(); };
    addAndMakeVisible (filterOn_);

    auto setup = [this] (juce::Slider& s, double lo, double hi, double step, const char* suffix, double reset)
    {
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setDoubleClickReturnValue (true, reset);
        s.onDragStart = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
        s.onDragEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
        s.onValueChange = [this] { if (! updating_) continuous(); };
        addAndMakeVisible (s);
    };
    setup (voices_, 1, SynthDescription::maxVoicesLimit, 1, "", SynthDescription::defaultMaxVoices);
    setup (glide_, 0, SynthDescription::maxGlideMs, 10, " ms", 0.0);
    setup (keyTrack_, -100.0, 200.0, 1.0, " %", 0.0);
    setup (velocity_, -6.0, 6.0, 0.1, " oct", 0.0);
    setup (depth_, -8.0, 8.0, 0.1, " oct", 0.0);
    setup (drive_, 0.0, 100.0, 1.0, " %", 0.0);

    ampEnv_.onGestureBegin = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
    ampEnv_.onGestureEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
    ampEnv_.onChange = [this] (const EnvelopeDescription& e) { if (updating_) return; d_.ampEnv = e; emit(); };
    filterEnv_.onGestureBegin = ampEnv_.onGestureBegin;
    filterEnv_.onGestureEnd = ampEnv_.onGestureEnd;
    filterEnv_.onChange = [this] (const EnvelopeDescription& e) { if (updating_) return; d_.filter.env = e; emit(); };
    addAndMakeVisible (ampEnv_);
    addAndMakeVisible (filterEnv_);

    keyboard_.onNoteOn = [this] (int key, float v) { if (onNoteOn) onNoteOn (key, v); };
    keyboard_.onNoteOff = [this] (int key) { if (onNoteOff) onNoteOff (key); };
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

    refreshControls();
}

SynthView::~SynthView() = default;

juce::Colour SynthView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String SynthView::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void SynthView::emit()
{
    if (onChange) onChange (d_);
}

// Commandes à choix (combos, bascule, octave) : un geste complet par changement.
void SynthView::discrete()
{
    d_.octave = static_cast<int> (octave_.getValue());
    d_.polyphony = static_cast<PolyphonyMode> (juce::jmax (1, mode_.getSelectedId()) - 1);
    d_.stealMode = static_cast<VoiceStealMode> (juce::jmax (1, steal_.getSelectedId()) - 1);
    auto& f = d_.filter;
    f.on = filterOn_.getToggleState();
    f.model = model_.getSelectedId() == 2 ? FilterModel::Ladder : FilterModel::SVF;
    const int m = filterMode_.getSelectedId();
    f.mode = m == 2 ? FilterMode::HighPass : m == 3 ? FilterMode::BandPass : m == 4 ? FilterMode::Notch : FilterMode::LowPass;
    f.slopeDb = slope_.getSelectedId() == 2 ? 24 : 12;
    if (onGestureBegin) onGestureBegin();
    emit();
    if (onGestureEnd) onGestureEnd();
    refreshControls();
}

void SynthView::continuous()
{
    d_.maxVoices = static_cast<int> (voices_.getValue());
    d_.glideMs = static_cast<int> (glide_.getValue());
    d_.filter.keyTrack = keyTrack_.getValue() / 100.0;
    d_.filter.velocityOctaves = velocity_.getValue();
    d_.filter.envDepthOctaves = depth_.getValue();
    d_.filter.drive = drive_.getValue() / 100.0;
    emit();
}

void SynthView::setDescription (const SynthDescription& d)
{
    if (dragging_ || d == d_) return;
    d_ = d;
    refreshControls();
}

void SynthView::refreshControls()
{
    updating_ = true;
    for (int i = 0; i < numWaveforms; ++i) waveButtons_[i].setToggleState (static_cast<int> (d_.waveform) == i, juce::dontSendNotification);
    octave_.setValue (d_.octave, juce::dontSendNotification);
    mode_.setSelectedId (static_cast<int> (d_.polyphony) + 1, juce::dontSendNotification);
    steal_.setSelectedId (static_cast<int> (d_.stealMode) + 1, juce::dontSendNotification);
    voices_.setValue (d_.maxVoices, juce::dontSendNotification);
    glide_.setValue (d_.glideMs, juce::dontSendNotification);
    const auto& f = d_.filter;
    filterOn_.setToggleState (f.on, juce::dontSendNotification);
    model_.setSelectedId (f.model == FilterModel::Ladder ? 2 : 1, juce::dontSendNotification);
    filterMode_.setSelectedId (f.mode == FilterMode::HighPass ? 2 : f.mode == FilterMode::BandPass ? 3 : f.mode == FilterMode::Notch ? 4 : 1,
                               juce::dontSendNotification);
    slope_.setSelectedId (f.slopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
    keyTrack_.setValue (f.keyTrack * 100.0, juce::dontSendNotification);
    velocity_.setValue (f.velocityOctaves, juce::dontSendNotification);
    depth_.setValue (f.envDepthOctaves, juce::dontSendNotification);
    drive_.setValue (f.drive * 100.0, juce::dontSendNotification);
    ampEnv_.setEnvelope (d_.ampEnv);
    filterEnv_.setEnvelope (f.env);
    updating_ = false;

    const bool poly = d_.polyphony == PolyphonyMode::Poly;
    voices_.setEnabled (poly);
    voicesLabel_.setEnabled (poly);
    steal_.setEnabled (poly);
    stealLabel_.setEnabled (poly);
    glide_.setEnabled (d_.polyphony == PolyphonyMode::Legato);
    glideLabel_.setEnabled (d_.polyphony == PolyphonyMode::Legato);
    const bool fon = f.on;
    for (auto* c : std::initializer_list<juce::Component*> { &model_, &filterMode_, &slope_, &keyTrack_, &velocity_, &depth_, &drive_,
                                                              &filterEnv_, &keyLabel_, &velLabel_, &depthLabel_, &driveLabel_ })
        c->setEnabled (fon);
    filterMode_.setEnabled (fon && f.model == FilterModel::SVF); // l'échelle est toujours passe-bas
    drive_.setEnabled (fon && f.model == FilterModel::Ladder);
    driveLabel_.setEnabled (fon && f.model == FilterModel::Ladder);
}

void SynthView::resized()
{
    auto r = getLocalBounds();
    // rangée 1 : forme d'onde, octave
    {
        auto row = r.removeFromTop (36);
        waveLabel_.setBounds (row.removeFromLeft (110));
        for (auto& b : waveButtons_) b.setBounds (row.removeFromLeft (120).reduced (2));
        row.removeFromLeft (24);
        octaveLabel_.setBounds (row.removeFromLeft (70));
        octave_.setBounds (row.removeFromLeft (150).reduced (2));
    }
    // rangée 2 : polyphonie
    {
        auto row = r.removeFromTop (36);
        modeLabel_.setBounds (row.removeFromLeft (110));
        mode_.setBounds (row.removeFromLeft (230).reduced (2));
        row.removeFromLeft (10);
        voicesLabel_.setBounds (row.removeFromLeft (110));
        voices_.setBounds (row.removeFromLeft (190).reduced (2));
        row.removeFromLeft (10);
        stealLabel_.setBounds (row.removeFromLeft (60));
        steal_.setBounds (row.removeFromLeft (220).reduced (2));
        row.removeFromLeft (10);
        glideLabel_.setBounds (row.removeFromLeft (110));
        glide_.setBounds (row.removeFromLeft (190).reduced (2));
    }
    r.removeFromTop (6);
    // bas : clavier
    auto bottom = r.removeFromBottom (170);
    {
        auto line = bottom.removeFromTop (32);
        octDown_.setBounds (line.removeFromLeft (80).reduced (2));
        octUp_.setBounds (line.removeFromLeft (80).reduced (2));
        hold_.setBounds (line.removeFromLeft (120).reduced (2));
        keysHint_.setBounds (line);
        keyboard_.setBounds (bottom.reduced (0, 2));
    }
    // milieu : enveloppe de volume à gauche, filtre à droite
    auto left = r.removeFromLeft ((r.getWidth() - 12) / 2);
    r.removeFromLeft (12);
    ampTitle_.setBounds (left.removeFromTop (26));
    ampEnv_.setBounds (left);
    filterTitle_.setBounds (r.removeFromTop (26));
    {
        auto row = r.removeFromTop (34);
        filterOn_.setBounds (row.removeFromLeft (70));
        model_.setBounds (row.removeFromLeft (180).reduced (2));
        filterMode_.setBounds (row.removeFromLeft (120).reduced (2));
        slope_.setBounds (row.removeFromLeft (90).reduced (2));
    }
    {
        auto grid = r.removeFromTop (84);
        auto top = grid.removeFromTop (42), low = grid;
        const int w = top.getWidth() / 2;
        keyLabel_.setBounds (top.removeFromLeft (110));
        keyTrack_.setBounds (top.removeFromLeft (w - 110).reduced (2));
        velLabel_.setBounds (top.removeFromLeft (80));
        velocity_.setBounds (top.reduced (2));
        depthLabel_.setBounds (low.removeFromLeft (110));
        depth_.setBounds (low.removeFromLeft (w - 110).reduced (2));
        driveLabel_.setBounds (low.removeFromLeft (80));
        drive_.setBounds (low.reduced (2));
    }
    hint_.setBounds (r.removeFromBottom (32));
    filterEnvTitle_.setBounds (r.removeFromTop (22));
    filterEnv_.setBounds (r);
}

void SynthView::paint (juce::Graphics&) {}
