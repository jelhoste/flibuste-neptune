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
    const auto dir = configDirectory();
    settings_ = settings::loadFile (dir / "settings.json", &warnings);
    // la même bibliothèque que Neptune et les autres synthés (dossier choisi dans les réglages, sinon <config>/AnalogSynth)
    std::filesystem::path root = settings_.presetsDir.empty() ? dir / "AnalogSynth" : std::filesystem::u8path (settings_.presetsDir);
    library_ = std::make_unique<preset::Library> (std::move (root), organ::factoryPresets());
    library_->open (&warnings);
    {
        auto cfg = library_->config();
        if (cfg.parameterCaptureDefaults.count (organ::leslieSpeedKey) == 0)
        {
            cfg.parameterCaptureDefaults[organ::leslieSpeedKey] = preset::Capture::Free; // la vitesse du Leslie : libre par défaut
            library_->setConfig (cfg);
        }
    }
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
        else if (m.isController() && m.getControllerNumber() == 0) midiBank_.store (juce::jlimit (0, 127, m.getControllerValue()));
        else if (m.isController()) engine_.controlChange (m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue());
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

void OrganProcessor::handleAsyncUpdate()
{
    const int bank = pendingBank_.exchange (-1), program = pendingProgram_.exchange (-1);
    if (bank < 0 || program < 0) return;
    const auto id = preset::Navigator::presetAt (library_->banks(), { bank, program });
    if (! id.empty()) loadPreset (id);
}

void OrganProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = organ::stateToJson (engine_.params(), engine_.description(), currentPresetId_);
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
    currentPresetId_ = presetId;
    undo_.clear();
    ++presetRevision_;
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

// ---- presets
std::vector<float> OrganProcessor::snapshotNormalised() const
{
    std::vector<float> v;
    const auto& set = engine_.params();
    v.reserve (set.size());
    for (std::size_t i = 0; i < set.size(); ++i) v.push_back (static_cast<float> (set.getNormalizedAt (i)));
    return v;
}

void OrganProcessor::notifyHostOfParameterChanges (const std::vector<float>& before)
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

bool OrganProcessor::loadPreset (const std::string& id)
{
    const auto* p = library_->find (id);
    if (p == nullptr || ! acceptsPreset (*p)) return false;
    const auto before = snapshotNormalised();
    organ::applyPresetValues (*p, engine_.params(), library_->config());
    currentPresetId_ = id;
    undo_.clear();
    notifyHostOfParameterChanges (before);
    ++presetRevision_;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return true;
}

bool OrganProcessor::stepPreset (int direction)
{
    const auto next = library_->stepFrom (currentPresetId_, direction, [this] (const preset::Preset& p) { return acceptsPreset (p); });
    return ! next.empty() && loadPreset (next);
}

bool OrganProcessor::isModified() const
{
    const auto* p = currentPreset();
    return p != nullptr && organ::differsFromPreset (*p, engine_.params(), library_->config());
}

bool OrganProcessor::canSaveCurrent() const
{
    const auto* p = currentPreset();
    return p != nullptr && ! p->factory && ! p->locked;
}

bool OrganProcessor::overwritePreset (const std::string& id, std::string* error)
{
    const auto* base = library_->find (id);
    if (base == nullptr || base->factory) { if (error) *error = "factory preset is read-only"; return false; }
    if (base->locked) { if (error) *error = "preset is locked"; return false; }
    auto p = organ::capturePreset (engine_.params(), library_->config(), base->name, base->type, base);
    const bool ok = library_->save (std::move (p), error);
    if (ok) ++presetRevision_;
    return ok;
}

bool OrganProcessor::saveCurrent (std::string* error)
{
    if (currentPresetId_.empty()) { if (error) *error = "no current preset"; return false; }
    return overwritePreset (currentPresetId_, error);
}

bool OrganProcessor::saveCurrentAs (const std::string& name, const std::string& type, preset::Preset* out, std::string* error)
{
    auto p = organ::capturePreset (engine_.params(), library_->config(), name, type);
    if (const auto* cur = currentPreset())
    {
        p.characteristics = cur->characteristics;
        p.customFields = cur->customFields;
        p.rating = cur->rating;
        p.captureOverrides = cur->captureOverrides;
        if (p.type.empty()) p.type = cur->type;
        p.derivedFrom = cur->factory ? cur->id : cur->derivedFrom;
        p = organ::capturePreset (engine_.params(), library_->config(), name, p.type, &p);
    }
    preset::Preset stored;
    if (! library_->importPreset (std::move (p), &stored, error)) return false;
    currentPresetId_ = stored.id;
    ++presetRevision_;
    if (out != nullptr) *out = stored;
    return true;
}

bool OrganProcessor::saveConfig (const preset::LibraryConfig& c)
{
    const bool ok = library_->setConfig (c);
    if (ok) ++presetRevision_;
    return ok;
}

juce::AudioProcessorEditor* OrganProcessor::createEditor() { return new OrganEditor (*this); }

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

    // volume et gain du plugin : les paramètres de la piste 0 (mêmes identifiants que dans Neptune)
    OrganView::KnobIds extra;
    extra.push_back ([] (int) { return organ::synthVolume; });
    extra.push_back ([] (int) { return static_cast<ParamId> (sampler::gain); });
    view_ = std::make_unique<OrganView> (processor_, palette_, tr_, knobStyle_, std::move (extra),
                                         [this] (ParamId id, uistate::Terminal t) { openModal (id, t); });
    view_->onChange = [this] (const organ::OrganDescription& d) { processor_.uiEditOrgan (d, "Organ keyboards", "organ"); };
    view_->onNoteOn = [this] (int key, int channel) { processor_.uiNoteOn (channel, key); };
    view_->onNoteOff = [this] (int key, int channel) { processor_.uiNoteOff (channel, key); };
    view_->setDescription (processor_.organDescription());
    addAndMakeVisible (*view_);

    presetBar_ = std::make_unique<PresetBar> (processor_, palette_, tr_);
    presetBar_->onOpenManager = [this] (bool saveAs) { openManager (saveAs); };
    presetBar_->onStatus = [this] (const juce::String& m) { infoLabel_.setText (m, juce::dontSendNotification); };
    addAndMakeVisible (*presetBar_);

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
    panicButton_.onClick = [this] { processor_.uiPanic(); view_->releaseKeys(); };
    for (auto* b : { &undoButton_, &redoButton_, &panicButton_ }) addAndMakeVisible (b);

    startTimerHz (15);

    root_.onPaint = [this] (juce::Graphics& g) { g.fillAll (colour (Token::BgApp)); };
    root_.adopt (*this);
    setResizable (true, true);
    setResizeLimits (360, 240, 2400, 1400);
    {
        int w = OrganView::designWidth + 40, h = OrganView::designHeight + 70;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, d->userArea.getWidth() * 9 / 10);
            h = juce::jmin (h, d->userArea.getHeight() * 85 / 100);
        }
        setSize (juce::jmax (360, w), juce::jmax (240, h));
    }
}

void OrganEditor::resized()
{
    const auto logical = root_.fit (getWidth(), getHeight(), OrganView::designWidth + 40, OrganView::designHeight + 70);
    auto r = logical.reduced (20, 10);
    auto top = r.removeFromTop (36);
    brandLabel_.setBounds (top.removeFromLeft (150));
    redoButton_.setBounds (top.removeFromRight (64).reduced (2));
    undoButton_.setBounds (top.removeFromRight (64).reduced (2));
    panicButton_.setBounds (top.removeFromRight (120).reduced (2));
    presetBar_->setBounds (top.removeFromLeft (juce::jmin (560, top.getWidth() - 120)));
    infoLabel_.setBounds (top);
    r.removeFromTop (8);
    view_->placeScaled (r);
    if (modal_) modal_->setBounds (logical);
    if (manager_) manager_->setBounds (logical);
}

void OrganEditor::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgApp)); }

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
    if (code == 'S' || code == 's') { presetBar_->quickSave(); return true; }
    return false;
}

void OrganEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_ || manager_) return;
    modal_ = std::make_unique<KnobModal> (processor_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<OrganEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    root_.addAndMakeVisible (*modal_);
    modal_->setBounds (root_.getLocalBounds());
    modal_->toFront (true);
}

void OrganEditor::openManager (bool saveAs)
{
    if (manager_ || modal_) return;
    manager_ = std::make_unique<PresetManager> (processor_, palette_, tr_);
    manager_->onClose = [safe = juce::Component::SafePointer<OrganEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->manager_.reset();
            safe->presetBar_->refresh();
            safe->grabKeyboardFocus();
        });
    };
    manager_->onChanged = [safe = juce::Component::SafePointer<OrganEditor> (this)] { if (safe != nullptr) safe->presetBar_->refresh(); };
    root_.addAndMakeVisible (*manager_);
    manager_->setBounds (root_.getLocalBounds());
    manager_->toFront (true);
    if (saveAs) manager_->showSaveAs();
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
    processor_.engine().collect(); // libère l'orgue remplacé après un changement d'accord ou de câblage
    if (presetBar_->needsRefresh()) presetBar_->refresh();
    const auto rev = static_cast<std::uint32_t> (processor_.undoManager().revision());
    if (rev != lastUndoRevision_)
    {
        lastUndoRevision_ = rev;
        view_->setDescription (processor_.organDescription());
    }
    view_->refreshFromParameters();
    view_->setHeldKeys (processor_.engine().heldKeyWord (0), processor_.engine().heldKeyWord (1)); // notes MIDI reçues
    undoButton_.setEnabled (processor_.undoManager().canUndo());
    redoButton_.setEnabled (processor_.undoManager().canRedo());
    infoLabel_.setText ("Orgamon - " + juce::String (processor_.engine().heldKeys()), juce::dontSendNotification);
    root_.repaint();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrganProcessor(); }
