#include "FmPlugin.h"
#include "NeptuneParameter.h"
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

constexpr int kKnobCell = 118;
constexpr int kBottomHeight = 120;
} // namespace

// ---------------------------------------------------------------------------------------------------- processeur

juce::AudioProcessor::BusesProperties FmSynthProcessor::makeBuses()
{
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

FmSynthProcessor::FmSynthProcessor() : juce::AudioProcessor (makeBuses())
{
    jassert (engine_.ok());
    auto& set = engine_.params();
    for (std::size_t i = 0; i < set.size(); ++i) addParameter (new NeptuneParameter (set, i, probe_));

    std::vector<std::string> warnings;
    const auto dir = configDirectory();
    settings_ = settings::loadFile (dir / "settings.json", &warnings);
    std::filesystem::path root = settings_.presetsDir.empty() ? dir / "AnalogSynth" : std::filesystem::u8path (settings_.presetsDir);
    library_ = std::make_unique<preset::Library> (std::move (root), fm::factoryPresets());
    library_->open (&warnings);
    for (auto& s : modal_) s = {};
}

bool FmSynthProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FmSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) { engine_.prepare (sampleRate, samplesPerBlock); }

void FmSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
        if (m.isNoteOn()) engine_.noteOn (m.getChannel() - 1, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine_.noteOff (m.getChannel() - 1, m.getNoteNumber());
        else if (m.isAllNotesOff()) engine_.releaseAll();
        else if (m.isAllSoundOff()) engine_.panic();
        else if (m.isPitchWheel()) engine_.setPitchBend (juce::jlimit (0, 16383, m.getPitchWheelValue()));
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(), v = juce::jlimit (0, 127, m.getControllerValue());
            if (cc == 0) midiBank_.store (v);
            else if (cc == 1) engine_.setModWheel (v);
            else if (cc == 64) engine_.setSustain (v >= 64);
        }
        else if (m.isProgramChange())
        {
            pendingBank_.store (midiBank_.load());
            pendingProgram_.store (m.getProgramChangeNumber());
            triggerAsyncUpdate();
        }
    }
    if (pos < n) engine_.process (left + pos, right != nullptr ? right + pos : nullptr, n - pos);
    midi.clear();
}

void FmSynthProcessor::handleAsyncUpdate()
{
    const int bank = pendingBank_.exchange (-1), program = pendingProgram_.exchange (-1);
    if (bank < 0 || program < 0) return;
    const auto id = preset::Navigator::presetAt (library_->banks(), { bank, program });
    if (! id.empty()) loadPreset (id);
}

void FmSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = fm::stateToJson (engine_.params(), engine_.description(), currentPresetId_);
    dest.append (text.data(), text.size());
}

void FmSynthProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    fm::FmDescription d;
    std::string presetId;
    const std::string text (static_cast<const char*> (data), static_cast<std::size_t> (size));
    if (! fm::stateFromJson (text, engine_.params(), d, presetId)) return;
    engine_.setDescription (d);
    currentPresetId_ = presetId;
    undo_.clear();
    ++presetRevision_;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

void FmSynthProcessor::uiBeginEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    getParameters()[static_cast<int> (index)]->beginChangeGesture();
    undo_.beginGesture();
}

void FmSynthProcessor::uiSetNormalised (ParamId id, double n)
{
    auto& set = engine_.params();
    const auto index = set.indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.setParameter (set, id, set.spec (index).fromNormalized (n));
    getParameters()[static_cast<int> (index)]->setValueNotifyingHost (static_cast<float> (set.getNormalizedAt (index)));
}

void FmSynthProcessor::uiEndEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.endGesture();
    getParameters()[static_cast<int> (index)]->endChangeGesture();
}

void FmSynthProcessor::uiResetToDefault (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    const auto& spec = engine_.params().spec (index);
    uiBeginEdit (id);
    uiSetNormalised (id, spec.toNormalized (spec.defaultValue));
    uiEndEdit (id);
}

bool FmSynthProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

bool FmSynthProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

void FmSynthProcessor::uiEditFm (fm::FmDescription d, std::string name, std::string mergeKey)
{
    d = fm::sanitised (std::move (d));
    const auto previous = engine_.description();
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<fm::FmDescription>> (
        previous, d,
        [this] (const fm::FmDescription& v)
        {
            const auto before = snapshotNormalised();
            engine_.setDescription (v); // recopie aussi LFO, bend, molette et glissando dans les paramètres
            notifyHostOfParameterChanges (before);
            updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
        },
        std::move (name), std::move (mergeKey)));
}

// ---- presets
std::vector<float> FmSynthProcessor::snapshotNormalised() const
{
    std::vector<float> v;
    const auto& set = engine_.params();
    v.reserve (set.size());
    for (std::size_t i = 0; i < set.size(); ++i) v.push_back (static_cast<float> (set.getNormalizedAt (i)));
    return v;
}

void FmSynthProcessor::notifyHostOfParameterChanges (const std::vector<float>& before)
{
    const auto& set = engine_.params();
    for (std::size_t i = 0; i < set.size() && i < before.size(); ++i)
    {
        const auto now = static_cast<float> (set.getNormalizedAt (i));
        if (now == before[i]) continue;
        auto* p = getParameters()[static_cast<int> (i)];
        p->beginChangeGesture();
        p->setValueNotifyingHost (now);
        p->endChangeGesture();
    }
}

bool FmSynthProcessor::loadPreset (const std::string& id)
{
    const auto* p = library_->find (id);
    if (p == nullptr || ! acceptsPreset (*p)) return false;
    const auto before = snapshotNormalised();
    const auto& cfg = library_->config();
    engine_.setDescription (fm::presetDescription (*p, engine_.description(), cfg));
    fm::applyPresetValues (*p, engine_.params(), cfg);
    currentPresetId_ = id;
    undo_.clear();
    notifyHostOfParameterChanges (before);
    ++presetRevision_;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return true;
}

bool FmSynthProcessor::stepPreset (int direction)
{
    const auto next = library_->stepFrom (currentPresetId_, direction, [this] (const preset::Preset& p) { return acceptsPreset (p); });
    return ! next.empty() && loadPreset (next);
}

bool FmSynthProcessor::isModified() const
{
    const auto* p = currentPreset();
    return p != nullptr && fm::differsFromPreset (*p, engine_.params(), engine_.description(), library_->config());
}

bool FmSynthProcessor::canSaveCurrent() const
{
    const auto* p = currentPreset();
    return p != nullptr && ! p->factory && ! p->locked;
}

bool FmSynthProcessor::overwritePreset (const std::string& id, std::string* error)
{
    const auto* base = library_->find (id);
    if (base == nullptr || base->factory) { if (error) *error = "factory preset is read-only"; return false; }
    if (base->locked) { if (error) *error = "preset is locked"; return false; }
    auto p = fm::capturePreset (engine_.params(), engine_.description(), library_->config(), base->name, base->type, base);
    const bool ok = library_->save (std::move (p), error);
    if (ok) ++presetRevision_;
    return ok;
}

bool FmSynthProcessor::saveCurrent (std::string* error)
{
    if (currentPresetId_.empty()) { if (error) *error = "no current preset"; return false; }
    return overwritePreset (currentPresetId_, error);
}

bool FmSynthProcessor::saveCurrentAs (const std::string& name, const std::string& type, preset::Preset* out, std::string* error)
{
    auto p = fm::capturePreset (engine_.params(), engine_.description(), library_->config(), name, type);
    if (const auto* cur = currentPreset())
    {
        p.characteristics = cur->characteristics;
        p.customFields = cur->customFields;
        p.rating = cur->rating;
        p.captureOverrides = cur->captureOverrides;
        if (p.type.empty()) p.type = cur->type;
        p.derivedFrom = cur->factory ? cur->id : cur->derivedFrom;
        p = fm::capturePreset (engine_.params(), engine_.description(), library_->config(), name, p.type, &p);
    }
    preset::Preset stored;
    if (! library_->importPreset (std::move (p), &stored, error)) return false;
    currentPresetId_ = stored.id;
    ++presetRevision_;
    if (out != nullptr) *out = stored;
    return true;
}

bool FmSynthProcessor::saveConfig (const preset::LibraryConfig& c)
{
    const bool ok = library_->setConfig (c);
    if (ok) ++presetRevision_;
    return ok;
}

juce::AudioProcessorEditor* FmSynthProcessor::createEditor() { return new FmSynthEditor (*this); }

// ---------------------------------------------------------------------------------------------------- éditeur

juce::Colour FmSynthEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

FmSynthEditor::FmSynthEditor (FmSynthProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language), palette_ (makePalette (p.settings()))
{
    setWantsKeyboardFocus (true);
    const auto& def = theme::definition (theme::themeFromString (p.settings().theme).value_or (theme::ThemeId::Dark));
    knobStyle_ = def.knob;

    view_ = std::make_unique<FmView> (processor_, palette_, tr_, knobStyle_);
    view_->onChange = [this] (const fm::FmDescription& d) { processor_.uiEditFm (d, "FM", "fm"); };
    view_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    view_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    view_->onNoteOn = [this] (int key, float velocity) { processor_.uiNoteOn (key, velocity); };
    view_->onNoteOff = [this] (int key) { processor_.uiNoteOff (key); };
    view_->setTrack (0);
    sysex_ = std::make_unique<FmSysexIO> (*this, processor_, tr_);
    view_->onImportSysex = [this]
    {
        sysex_->importFile ([this] (const fm::Patch& patch)
                            {
                                auto d = processor_.fmDescription();
                                d.patch = patch;
                                processor_.uiEditFm (d, "Import DX7 voice");
                            });
    };
    view_->onExportVoice = [this] { sysex_->exportVoice (processor_.fmDescription().patch); };
    view_->onExportBank = [this] { sysex_->exportBank(); };
    addAndMakeVisible (*view_);

    for (const ParamId id : { fm::synthVolume, fm::synthPan, sampler::gain, sampler::tuneSemitones, sampler::tuneCents })
        addAndMakeVisible (knobs_.add (new NeptuneKnob (processor_, id, palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); })));

    brandLabel_.setText ("FM6OP", juce::dontSendNotification);
    brandLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    brandLabel_.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (brandLabel_);
    infoLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    infoLabel_.setFont (juce::FontOptions (11.0f));
    infoLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (infoLabel_);

    presetBar_ = std::make_unique<PresetBar> (processor_, palette_, tr_);
    presetBar_->onOpenManager = [this] (bool saveAs) { openManager (saveAs); };
    presetBar_->onLoaded = [this] { view_->setDescription (processor_.fmDescription()); };
    presetBar_->onStatus = [this] (const juce::String& m) { infoLabel_.setText (m, juce::dontSendNotification); };
    addAndMakeVisible (*presetBar_);

    undoButton_.setButtonText (text ("undo.undo"));
    undoButton_.onClick = [this] { processor_.uiUndo(); };
    redoButton_.setButtonText (text ("undo.redo"));
    redoButton_.onClick = [this] { processor_.uiRedo(); };
    panicButton_.setButtonText (text ("panic.button"));
    panicButton_.onClick = [this] { processor_.uiPanic(); view_->releaseKeys(); };
    for (auto* b : { &undoButton_, &redoButton_, &panicButton_ }) addAndMakeVisible (b);

    view_->setDescription (processor_.fmDescription());
    startTimerHz (15);

    root_.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    root_.adopt (*this);
    setResizable (true, true);
    setResizeLimits (360, 240, 2400, 1400);
    {
        int w = 1180, h = 760;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, d->userArea.getWidth() * 9 / 10);
            h = juce::jmin (h, d->userArea.getHeight() * 85 / 100);
        }
        setSize (juce::jmax (360, w), juce::jmax (240, h));
    }
}

void FmSynthEditor::resized()
{
    const auto logical = root_.fit (getWidth(), getHeight(), 860, 640);
    auto r = logical.reduced (24, 12);
    auto top = r.removeFromTop (40);
    brandLabel_.setBounds (top.removeFromLeft (150));
    presetBar_->setBounds (top.removeFromLeft (juce::jmin (620, top.getWidth() - 110 - 140)));
    panicButton_.setBounds (top.removeFromRight (110).reduced (2));
    infoLabel_.setBounds (top);
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (kBottomHeight);
    view_->placeScaled (r);

    const int knobSide = static_cast<int> (ui::effectiveHitSize (48.0, ui::KnobConfig {}));
    int x = bottom.getX();
    const int y = bottom.getY() + 22;
    for (auto* k : knobs_)
    {
        k->setBounds (x + (kKnobCell - knobSide) / 2, y, knobSide, knobSide);
        x += kKnobCell;
    }
    redoButton_.setBounds (bottom.getRight() - 64, y + (knobSide - 22) / 2, 64, 22);
    undoButton_.setBounds (bottom.getRight() - 64 - 4 - 64, y + (knobSide - 22) / 2, 64, 22);

    if (modal_) modal_->setBounds (logical);
    if (manager_) manager_->setBounds (logical);
}

void FmSynthEditor::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgApp)); }

void FmSynthEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgApp));
    const auto& set = processor_.parameters();
    g.setFont (juce::FontOptions (11.0f));
    for (auto* k : knobs_)
    {
        const auto b = k->getBounds();
        const auto index = set.indexOf (k->paramId());
        if (index == ParameterSet::npos) continue;
        g.setColour (colour (Token::TextDim));
        g.drawText (juce::String::fromUTF8 (set.spec (index).name.c_str()), b.getCentreX() - kKnobCell / 2, b.getY() - 20, kKnobCell, 18,
                    juce::Justification::centred, true);
        g.setColour (colour (Token::Text));
        g.drawText (juce::String::fromUTF8 (set.toText (k->paramId()).c_str()), b.getCentreX() - kKnobCell / 2, b.getBottom() + 1, kKnobCell, 18,
                    juce::Justification::centred, true);
    }
}

bool FmSynthEditor::keyPressed (const juce::KeyPress& k)
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
    if (code == 'S' || code == 's') { presetBar_->quickSave(); return true; }
    return false;
}

void FmSynthEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_ || manager_) return;
    modal_ = std::make_unique<KnobModal> (processor_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<FmSynthEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    root_.addAndMakeVisible (*modal_);
    modal_->setBounds (root_.getLocalBounds());
    modal_->toFront (true);
}

void FmSynthEditor::closeModal()
{
    if (! modal_) return;
    processor_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

void FmSynthEditor::openManager (bool saveAs)
{
    if (manager_ || modal_) return;
    manager_ = std::make_unique<PresetManager> (processor_, palette_, tr_);
    manager_->onClose = [safe = juce::Component::SafePointer<FmSynthEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->manager_.reset();
            safe->presetBar_->refresh();
            safe->grabKeyboardFocus();
        });
    };
    manager_->onChanged = [safe = juce::Component::SafePointer<FmSynthEditor> (this)]
    {
        if (safe != nullptr) { safe->presetBar_->refresh(); safe->view_->setDescription (safe->processor_.fmDescription()); }
    };
    root_.addAndMakeVisible (*manager_);
    manager_->setBounds (root_.getLocalBounds());
    manager_->toFront (true);
    if (saveAs) manager_->showSaveAs();
}

void FmSynthEditor::timerCallback()
{
    if (presetBar_->needsRefresh())
    {
        presetBar_->refresh();
        view_->setDescription (processor_.fmDescription());
    }
    const auto rev = static_cast<std::uint32_t> (processor_.undoManager().revision());
    if (rev != lastUndoRevision_)
    {
        lastUndoRevision_ = rev;
        view_->setDescription (processor_.fmDescription());
    }
    // automation du LFO, du bend, de la molette, du glissando : la vue suit
    {
        std::uint32_t sum = 0;
        for (int k = 0; k < neptune::core::mixer::kFmExtraKinds; ++k)
            sum += processor_.engine().params().version (neptune::core::mixer::fmExtraParam (0, static_cast<neptune::core::mixer::FmExtraKind> (k)));
        if (sum != lastExtraVersions_)
        {
            lastExtraVersions_ = sum;
            view_->setDescription (processor_.fmDescription());
        }
    }
    view_->setHeldKeys (processor_.engine().heldKeyWord (0), processor_.engine().heldKeyWord (1));
    undoButton_.setEnabled (processor_.undoManager().canUndo());
    redoButton_.setEnabled (processor_.undoManager().canRedo());
    const int voices = processor_.engine().voiceCount();
    if (voices != lastVoices_)
    {
        lastVoices_ = voices;
        infoLabel_.setText (text ("analog.voices") + " " + juce::String (voices), juce::dontSendNotification);
    }
    root_.repaint();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FmSynthProcessor(); }
