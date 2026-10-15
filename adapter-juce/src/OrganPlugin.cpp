#include "OrganPlugin.h"
#include "NeptuneParameter.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
std::filesystem::path configDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return std::filesystem::u8path (base.getChildFile ("Neptune").getFullPathName().toStdString());
}

theme::Palette makePalette (const settings::GlobalSettings& st)
{
    const auto id = theme::themeFromString (st.theme).value_or (theme::ThemeId::Dark);
    return theme::withContrast (theme::definition (id), st.contrast);
}

constexpr int kDesignWidth = 980, kDesignHeight = 640;
constexpr int kDrawbarW = 42, kDrawbarH = 190, kKnobCell = 92;
const char* const kFootage[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
// couleurs des tirettes : brun, brun, blanc, blanc, noir, blanc, noir, noir, blanc
const int kDrawbarKind[9] = { 0, 0, 1, 1, 2, 1, 2, 2, 1 };
} // namespace

// ---------------------------------------------------------------------------------------------------- processeur

juce::AudioProcessor::BusesProperties OrganProcessor::makeBuses()
{
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

OrganProcessor::OrganProcessor() : juce::AudioProcessor (makeBuses())
{
    jassert (engine_.ok());
    auto& set = engine_.params();
    for (std::size_t i = 0; i < set.size(); ++i) addParameter (new NeptuneParameter (set, i, probe_));
    std::vector<std::string> warnings;
    settings_ = settings::loadFile (configDirectory() / "settings.json", &warnings);
    for (auto& s : modal_) s = {};
}

bool OrganProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OrganProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) { engine_.prepare (sampleRate, samplesPerBlock); }

void OrganProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 1) return;
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        if (at > pos)
        {
            engine_.process (left + pos, right != nullptr ? right + pos : nullptr, at - pos);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine_.noteOn (m.getChannel() - 1, m.getNoteNumber());
        else if (m.isNoteOff()) engine_.noteOff (m.getChannel() - 1, m.getNoteNumber());
        else if (m.isAllNotesOff()) engine_.allNotesOff();
        else if (m.isAllSoundOff()) engine_.panic();
    }
    if (pos < n) engine_.process (left + pos, right != nullptr ? right + pos : nullptr, n - pos);
    midi.clear();
}

void OrganProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = organ::stateToJson (engine_.params(), engine_.description(), {});
    dest.append (text.data(), text.size());
}

void OrganProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    organ::OrganDescription d;
    std::string presetId;
    const std::string text (static_cast<const char*> (data), static_cast<std::size_t> (size));
    if (! organ::stateFromJson (text, engine_.params(), d, presetId)) return;
    engine_.setDescription (d);
    undo_.clear();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

void OrganProcessor::uiBeginEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    getParameters()[static_cast<int> (index)]->beginChangeGesture();
    undo_.beginGesture();
}

void OrganProcessor::uiSetNormalised (ParamId id, double n)
{
    auto& set = engine_.params();
    const auto index = set.indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.setParameter (set, id, set.spec (index).fromNormalized (n));
    getParameters()[static_cast<int> (index)]->setValueNotifyingHost (static_cast<float> (set.getNormalizedAt (index)));
}

void OrganProcessor::uiEndEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.endGesture();
    getParameters()[static_cast<int> (index)]->endChangeGesture();
}

void OrganProcessor::uiResetToDefault (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    const auto& spec = engine_.params().spec (index);
    uiBeginEdit (id);
    uiSetNormalised (id, spec.toNormalized (spec.defaultValue));
    uiEndEdit (id);
}

bool OrganProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

bool OrganProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

void OrganProcessor::uiEditOrgan (organ::OrganDescription d, std::string name, std::string mergeKey)
{
    d = organ::sanitised (std::move (d));
    const auto previous = engine_.description();
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<organ::OrganDescription>> (
        previous, d,
        [this] (const organ::OrganDescription& v)
        {
            engine_.setDescription (v);
            updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
        },
        std::move (name), std::move (mergeKey)));
}

juce::AudioProcessorEditor* OrganProcessor::createEditor() { return new OrganEditor (*this); }

// ---------------------------------------------------------------------------------------------------- composants

// Tirette : le haut est « rentrée » (0), le bas « sortie » (8), comme sur la console.
class OrganEditor::Drawbar : public juce::Component
{
public:
    Drawbar (ParamHost& host, ParamId id, int kind, const juce::String& footage, const theme::Palette& pal)
        : host_ (host), id_ (id), kind_ (kind), footage_ (footage), palette_ (pal) {}

    ParamId paramId() const { return id_; }

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
        g.setColour (bodyColour);
        g.fillRoundedRectangle (b.getX() + 3.0f, juce::jmax (slot.getY(), y - 3.0f), b.getWidth() - 6.0f, slot.getBottom() - juce::jmax (slot.getY(), y - 3.0f), 3.0f);
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
    ParamId id_;
    int kind_;
    juce::String footage_;
    const theme::Palette& palette_;
};

class OrganEditor::Switch : public juce::Component
{
public:
    Switch (ParamHost& host, ParamId id, const juce::String& label, const theme::Palette& pal)
        : host_ (host), id_ (id), label_ (label), palette_ (pal) {}

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
    ParamId id_;
    juce::String label_;
    const theme::Palette& palette_;
};

// ---------------------------------------------------------------------------------------------------- éditeur

juce::Colour OrganEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

OrganEditor::OrganEditor (OrganProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language), palette_ (makePalette (p.settings()))
{
    setWantsKeyboardFocus (true);
    const auto& def = theme::definition (theme::themeFromString (p.settings().theme).value_or (theme::ThemeId::Dark));
    knobStyle_ = def.knob;
    namespace mx = neptune::core::mixer;

    // tirettes : supérieur (9), inférieur (9), pédalier (2)
    for (int i = 0; i < 9; ++i)
        addAndMakeVisible (drawbars_.add (new Drawbar (processor_, mx::organParam (0, mx::kOrgUpper0 + i), kDrawbarKind[i], kFootage[i], palette_)));
    for (int i = 0; i < 9; ++i)
        addAndMakeVisible (drawbars_.add (new Drawbar (processor_, mx::organParam (0, mx::kOrgLower0 + i), kDrawbarKind[i], kFootage[i], palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (processor_, mx::organParam (0, mx::kOrgPedal16), 0, "16'", palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (processor_, mx::organParam (0, mx::kOrgPedal8), 1, "8'", palette_)));

    // interrupteurs
    auto sw = [this] (int kind, const char* key)
    {
        addAndMakeVisible (switches_.add (new Switch (processor_, mixer::organParam (0, kind), text (key), palette_)));
    };
    sw (mx::kOrgPercOn, "organ.perc_on");
    sw (mx::kOrgPercSoft, "organ.perc_soft");
    sw (mx::kOrgPercFast, "organ.perc_fast");
    sw (mx::kOrgPercThird, "organ.perc_third");
    sw (mx::kOrgVibUpper, "organ.vib_upper");
    sw (mx::kOrgVibLower, "organ.vib_lower");
    sw (mx::kOrgDriveOn, "organ.drive");

    // listes : vibrato / chorus, vitesse du Leslie
    for (const int kind : { static_cast<int> (mx::kOrgVibMode), static_cast<int> (mx::kOrgLeslieSpeed) })
    {
        auto* link = combos_.add (new ComboLink);
        link->id = mx::organParam (0, kind);
        const auto index = processor_.parameters().indexOf (link->id);
        if (index != ParameterSet::npos)
        {
            int itemId = 1;
            for (const auto& choice : processor_.parameters().spec (index).choices)
                link->box.addItem (juce::String::fromUTF8 (choice.c_str()), itemId++);
        }
        link->box.onChange = [this, link]
        {
            if (updating_) return;
            const int count = link->box.getNumItems();
            if (count < 2) return;
            processor_.uiBeginEdit (link->id);
            processor_.uiSetNormalised (link->id, static_cast<double> (link->box.getSelectedId() - 1) / static_cast<double> (count - 1));
            processor_.uiEndEdit (link->id);
        };
        addAndMakeVisible (link->box);
    }

    // potards
    for (const ParamId id : { mx::organParam (0, mx::kOrgLeslieMix), mx::organParam (0, mx::kOrgDriveInput),
                              mx::organParam (0, mx::kOrgDriveCharacter), mx::organParam (0, mx::kOrgDriveOutput),
                              mx::organParam (0, mx::kOrgReverb), organ::synthVolume, sampler::gain })
        addAndMakeVisible (knobs_.add (new NeptuneKnob (processor_, id, palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); })));

    // claviers : canaux, transpositions, split
    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].addItem (text ("organ.off"), 1);
        for (int c = 1; c <= 16; ++c) channelBox_[m].addItem (juce::String (c), c + 1);
        transposeBox_[m].clear();
        for (int t = -24; t <= 24; ++t) transposeBox_[m].addItem ((t > 0 ? "+" : "") + juce::String (t), t + 25);
        channelBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = processor_.organDescription();
            d.channel[static_cast<std::size_t> (m)] = channelBox_[m].getSelectedId() - 1;
            processor_.uiEditOrgan (d, "Organ channel");
        };
        transposeBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = processor_.organDescription();
            d.transpose[static_cast<std::size_t> (m)] = transposeBox_[m].getSelectedId() - 25;
            processor_.uiEditOrgan (d, "Organ transpose");
        };
        addAndMakeVisible (channelBox_[m]);
        addAndMakeVisible (transposeBox_[m]);
    }
    for (auto* s : { &splitLower_, &splitUpper_ })
    {
        s->setRange (0.0, 127.0, 1.0);
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextValueSuffix ("");
        addAndMakeVisible (*s);
    }
    splitLower_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = processor_.organDescription();
        d.splitLower = static_cast<int> (splitLower_.getValue());
        processor_.uiEditOrgan (d, "Organ split", "split-lower");
    };
    splitUpper_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = processor_.organDescription();
        d.splitUpper = static_cast<int> (splitUpper_.getValue());
        processor_.uiEditOrgan (d, "Organ split", "split-upper");
    };

    brandLabel_.setText ("ORGAMON", juce::dontSendNotification);
    brandLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    brandLabel_.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (brandLabel_);
    infoLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    infoLabel_.setFont (juce::FontOptions (11.0f));
    infoLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (infoLabel_);
    undoButton_.setButtonText (text ("undo.undo"));
    undoButton_.onClick = [this] { processor_.uiUndo(); };
    redoButton_.setButtonText (text ("undo.redo"));
    redoButton_.onClick = [this] { processor_.uiRedo(); };
    panicButton_.setButtonText (text ("panic.button"));
    panicButton_.onClick = [this] { processor_.uiPanic(); };
    for (auto* b : { &undoButton_, &redoButton_, &panicButton_ }) addAndMakeVisible (b);

    refreshDescriptionControls();
    startTimerHz (15);

    root_.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    root_.adopt (*this);
    setResizable (true, true);
    setResizeLimits (360, 240, 2400, 1400);
    {
        int w = kDesignWidth, h = kDesignHeight;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, d->userArea.getWidth() * 9 / 10);
            h = juce::jmin (h, d->userArea.getHeight() * 85 / 100);
        }
        setSize (juce::jmax (360, w), juce::jmax (240, h));
    }
}

void OrganEditor::refreshDescriptionControls()
{
    const auto d = processor_.organDescription();
    updating_ = true;
    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].setSelectedId (d.channel[static_cast<std::size_t> (m)] + 1, juce::dontSendNotification);
        transposeBox_[m].setSelectedId (d.transpose[static_cast<std::size_t> (m)] + 25, juce::dontSendNotification);
    }
    splitLower_.setValue (d.splitLower, juce::dontSendNotification);
    splitUpper_.setValue (d.splitUpper, juce::dontSendNotification);
    updating_ = false;
}

void OrganEditor::resized()
{
    const auto logical = root_.fit (getWidth(), getHeight(), kDesignWidth, kDesignHeight);
    sections_.clear();
    auto r = logical.reduced (20, 10);
    auto top = r.removeFromTop (36);
    brandLabel_.setBounds (top.removeFromLeft (170));
    redoButton_.setBounds (top.removeFromRight (64).reduced (2));
    undoButton_.setBounds (top.removeFromRight (64).reduced (2));
    panicButton_.setBounds (top.removeFromRight (120).reduced (2));
    infoLabel_.setBounds (top);
    r.removeFromTop (8);

    // tirettes
    auto row = r.removeFromTop (kDrawbarH + 34);
    int x = row.getX();
    auto place = [&] (int firstIndex, int count, const juce::String& title)
    {
        const int width = count * (kDrawbarW + 4) + 12;
        const juce::Rectangle<int> area (x, row.getY(), width, row.getHeight());
        sections_.push_back ({ title, area });
        for (int i = 0; i < count; ++i)
            drawbars_[firstIndex + i]->setBounds (x + 6 + i * (kDrawbarW + 4), row.getY() + 26, kDrawbarW, kDrawbarH);
        x += width + 14;
    };
    place (0, 9, text ("organ.upper"));
    place (9, 9, text ("organ.lower"));
    place (18, 2, text ("organ.pedal"));

    r.removeFromTop (10);
    // rangée des commandes : percussion | vibrato | Leslie | overdrive | réverb, volume, gain
    auto ctl = r.removeFromTop (124);
    auto cell = [&] (int width, const juce::String& title) -> juce::Rectangle<int>
    {
        auto a = ctl.removeFromLeft (width);
        ctl.removeFromLeft (10);
        sections_.push_back ({ title, a });
        return a.withTrimmedTop (24).reduced (4, 2);
    };
    {   // percussion : 4 interrupteurs en 2 x 2
        auto a = cell (190, text ("organ.percussion"));
        const int w = a.getWidth() / 2, h = juce::jmin (36, a.getHeight() / 2);
        switches_[0]->setBounds (a.getX(), a.getY(), w, h);
        switches_[1]->setBounds (a.getX() + w, a.getY(), w, h);
        switches_[2]->setBounds (a.getX(), a.getY() + h + 2, w, h);
        switches_[3]->setBounds (a.getX() + w, a.getY() + h + 2, w, h);
    }
    {   // vibrato / chorus
        auto a = cell (170, text ("organ.vibrato"));
        combos_[0]->box.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        const int w = a.getWidth() / 2;
        switches_[4]->setBounds (a.getX(), a.getY(), w, 32);
        switches_[5]->setBounds (a.getX() + w, a.getY(), w, 32);
    }
    {   // Leslie : vitesse + dry/wet
        auto a = cell (kKnobCell + 90, text ("organ.leslie"));
        combos_[1]->box.setBounds (a.removeFromLeft (90).withHeight (28));
        knobs_[0]->setBounds (a.getX() + 6, a.getY(), 48, 48);
    }
    {   // overdrive : interrupteur + 3 potards
        auto a = cell (kKnobCell * 3 + 20, text ("organ.drive"));
        switches_[6]->setBounds (a.getX(), a.getY() + a.getHeight() - 30, 72, 28);
        for (int i = 0; i < 3; ++i) knobs_[1 + i]->setBounds (a.getX() + 76 + i * 62, a.getY(), 48, 48);
    }
    {   // réverb, volume, gain
        auto a = cell (kKnobCell * 3 - 10, text ("organ.reverb"));
        for (int i = 0; i < 3; ++i) knobs_[4 + i]->setBounds (a.getX() + 6 + i * 70, a.getY(), 48, 48);
    }

    r.removeFromTop (8);
    // claviers
    auto kb = r.removeFromTop (86);
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

    if (modal_) modal_->setBounds (logical);
}

void OrganEditor::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgApp)); }

void OrganEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgApp));
    g.setFont (juce::FontOptions (11.0f));
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
    // légendes des potards
    const auto& set = processor_.parameters();
    g.setFont (juce::FontOptions (10.0f));
    for (auto* k : knobs_)
    {
        const auto b = k->getBounds();
        const auto index = set.indexOf (k->paramId());
        if (index == ParameterSet::npos) continue;
        g.setColour (colour (Token::TextDim));
        g.drawText (juce::String::fromUTF8 (set.spec (index).name.c_str()), b.getCentreX() - 38, b.getBottom() + 1, 76, 14, juce::Justification::centred, true);
        g.setColour (colour (Token::Text));
        g.drawText (juce::String::fromUTF8 (set.toText (k->paramId()).c_str()), b.getCentreX() - 38, b.getBottom() + 14, 76, 14, juce::Justification::centred, true);
    }
    // légendes des claviers
    if (! sections_.empty())
    {
        const auto kb = sections_.back().area;
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

bool OrganEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z')
    {
        if (m.isShiftDown()) processor_.uiRedo(); else processor_.uiUndo();
        return true;
    }
    if (code == 'Y' || code == 'y') { processor_.uiRedo(); return true; }
    return false;
}

void OrganEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (processor_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<OrganEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    root_.addAndMakeVisible (*modal_);
    modal_->setBounds (root_.getLocalBounds());
    modal_->toFront (true);
}

void OrganEditor::closeModal()
{
    if (! modal_) return;
    processor_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

void OrganEditor::timerCallback()
{
    const auto rev = static_cast<std::uint32_t> (processor_.undoManager().revision());
    if (rev != lastUndoRevision_)
    {
        lastUndoRevision_ = rev;
        refreshDescriptionControls();
    }
    // les listes et les tirettes suivent l'automation
    updating_ = true;
    for (auto* link : combos_)
    {
        const int v = static_cast<int> (std::lround (processor_.parameters().get (link->id))) + 1;
        if (link->box.getSelectedId() != v) link->box.setSelectedId (v, juce::dontSendNotification);
    }
    updating_ = false;
    for (auto* d : drawbars_) d->repaint();
    for (auto* s : switches_) s->repaint();
    repaint();
    undoButton_.setEnabled (processor_.undoManager().canUndo());
    redoButton_.setEnabled (processor_.undoManager().canRedo());
    const int keys = processor_.engine().heldKeys();
    infoLabel_.setText ("Orgamon - " + juce::String (keys), juce::dontSendNotification);
}


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrganProcessor(); }
