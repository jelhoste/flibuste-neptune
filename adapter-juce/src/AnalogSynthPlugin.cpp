#include "AnalogSynthPlugin.h"
#include <nlohmann/json.hpp>
#include "NeptuneParameter.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace analog = neptune::core::analogsynth;

namespace
{
// Même dossier de configuration que Neptune : le thème, le contraste et la langue sont communs à toute la suite.
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
constexpr double kEpsilon = 1.0e-9;
} // namespace

// ---------------------------------------------------------------------------------------------------- processeur

juce::AudioProcessor::BusesProperties AnalogSynthProcessor::makeBuses()
{
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

AnalogSynthProcessor::AnalogSynthProcessor() : juce::AudioProcessor (makeBuses())
{
    jassert (engine_.ok());
    auto& set = engine_.params();
    for (std::size_t i = 0; i < set.size(); ++i)
        addParameter (new NeptuneParameter (set, i, probe_)); // l'AudioProcessor en prend la propriété ; même ordre que le jeu

    std::vector<std::string> warnings;
    const auto dir = configDirectory();
    settings_ = settings::loadFile (dir / "settings.json", &warnings);

    // Bibliothèque de presets : dossier choisi dans les réglages, sinon <config>/AnalogSynth.
    std::filesystem::path root = settings_.presetsDir.empty() ? dir / "AnalogSynth" : std::filesystem::u8path (settings_.presetsDir);
    library_ = std::make_unique<preset::Library> (std::move (root), analog::factoryPresets());
    library_->open (&warnings);
    if (library_->config().parameterCaptureDefaults.empty()) library_->setConfig (analog::defaultConfig());
    for (auto& s : modal_) s = {};
}

bool AnalogSynthProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AnalogSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);
}

void AnalogSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 1) return;
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // Les événements MIDI sont appliqués à leur position dans le bloc : le rendu est découpé aux événements.
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
        else if (m.isController() && m.getControllerNumber() == 0) midiBank_.store (juce::jlimit (0, 127, m.getControllerValue()));
        else if (m.isProgramChange())
        {
            pendingBank_.store (midiBank_.load());
            pendingProgram_.store (m.getProgramChangeNumber());
            triggerAsyncUpdate(); // le chargement d'un preset se fait sur le thread de message
        }
    }
    if (pos < n) engine_.process (left + pos, right != nullptr ? right + pos : nullptr, n - pos);
    midi.clear();
}

// Program Change reçu : emplacement vide -> le preset actuel reste (aucune interruption, aucun choix demandé en concert).
void AnalogSynthProcessor::handleAsyncUpdate()
{
    const int bank = pendingBank_.exchange (-1), program = pendingProgram_.exchange (-1);
    if (bank < 0 || program < 0) return;
    const auto id = preset::Navigator::presetAt (library_->banks(), { bank, program });
    if (! id.empty()) loadPreset (id);
}

// ---- état : JSON, comme Neptune
void AnalogSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto text = analog::stateToJson (engine_.params(), engine_.description(), currentPresetId_);
    dest.append (text.data(), text.size());
}

void AnalogSynthProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    SynthDescription d;
    std::string presetId;
    const std::string text (static_cast<const char*> (data), static_cast<std::size_t> (size));
    if (! analog::stateFromJson (text, engine_.params(), d, presetId)) return;
    engine_.setDescription (d);
    currentPresetId_ = presetId;
    undo_.clear();
    ++presetRevision_;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

// ---- gestes d'édition (annulation + automation de l'hôte)
void AnalogSynthProcessor::uiBeginEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    getParameters()[static_cast<int> (index)]->beginChangeGesture();
    undo_.beginGesture();
}

void AnalogSynthProcessor::uiSetNormalised (ParamId id, double n)
{
    auto& set = engine_.params();
    const auto index = set.indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.setParameter (set, id, set.spec (index).fromNormalized (n));
    getParameters()[static_cast<int> (index)]->setValueNotifyingHost (static_cast<float> (set.getNormalizedAt (index)));
}

void AnalogSynthProcessor::uiEndEdit (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.endGesture();
    getParameters()[static_cast<int> (index)]->endChangeGesture();
}

void AnalogSynthProcessor::uiResetToDefault (ParamId id)
{
    const auto index = engine_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    const auto& spec = engine_.params().spec (index);
    uiBeginEdit (id);
    uiSetNormalised (id, spec.toNormalized (spec.defaultValue));
    uiEndEdit (id);
}

bool AnalogSynthProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

bool AnalogSynthProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

void AnalogSynthProcessor::uiEditSynth (SynthDescription d, std::string name, std::string mergeKey)
{
    d = sanitised (std::move (d));
    const auto previous = engine_.description();
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<SynthDescription>> (
        previous, d, [this] (const SynthDescription& v) { engine_.setDescription (v); updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true)); },
        std::move (name), std::move (mergeKey)));
}

// ---- presets
std::vector<float> AnalogSynthProcessor::snapshotNormalised() const
{
    std::vector<float> v;
    const auto& set = engine_.params();
    v.reserve (set.size());
    for (std::size_t i = 0; i < set.size(); ++i) v.push_back (static_cast<float> (set.getNormalizedAt (i)));
    return v;
}

// Les valeurs ont changé hors des gestes de l'interface : l'hôte est prévenu pour chaque paramètre modifié.
void AnalogSynthProcessor::notifyHostOfParameterChanges (const std::vector<float>& before)
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

bool AnalogSynthProcessor::loadPreset (const std::string& id)
{
    const auto* p = library_->find (id);
    if (p == nullptr) return false;
    const auto before = snapshotNormalised();
    auto d = engine_.description();
    if (! analog::applyPreset (*p, engine_.params(), d, library_->config())) return false;
    engine_.setDescription (d);
    currentPresetId_ = id;
    undo_.clear();
    notifyHostOfParameterChanges (before);
    ++presetRevision_;
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return true;
}

bool AnalogSynthProcessor::stepPreset (int direction)
{
    if (direction == 0) return false;
    const auto& banks = library_->banks();
    if (const auto slot = preset::Navigator::locate (banks, currentPresetId_))
    {
        if (const auto next = preset::Navigator::step (banks, *slot, direction))
        {
            const auto id = preset::Navigator::presetAt (banks, *next);
            return ! id.empty() && loadPreset (id);
        }
        return false;
    }
    // preset hors banques (ou aucun) : on parcourt la liste complète, triée par nom
    const auto all = preset::filter (library_->allPresets(), {});
    if (all.empty()) return false;
    int index = -1;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i]->id == currentPresetId_) index = static_cast<int> (i);
    const int count = static_cast<int> (all.size());
    const int next = index < 0 ? (direction > 0 ? 0 : count - 1) : (index + (direction > 0 ? 1 : -1) + count) % count;
    return loadPreset (all[static_cast<std::size_t> (next)]->id);
}

bool AnalogSynthProcessor::isModified() const
{
    const auto* p = currentPreset();
    if (p == nullptr) return false;
    const auto& cfg = library_->config();
    const auto& set = engine_.params();
    for (const auto& [key, value] : p->parameters)
    {
        const auto id = analog::paramIdForKey (key);
        if (id == 0 || value.isText || preset::captureOf (key, *p, cfg) == preset::Capture::Free) continue;
        const auto index = set.indexOf (id);
        if (index != ParameterSet::npos && std::fabs (set.getAt (index) - value.number) > kEpsilon) return true;
    }
    if (! p->state.empty())
    {
        auto stored = synthFromJson (p->state);
        auto current = engine_.description();
        if (preset::captureOf (analog::glideKey, *p, cfg) == preset::Capture::Free) stored.glideMs = current.glideMs;
        if (! (stored == current)) return true;
    }
    return false;
}

bool AnalogSynthProcessor::canSaveCurrent() const
{
    const auto* p = currentPreset();
    return p != nullptr && ! p->factory && ! p->locked;
}

bool AnalogSynthProcessor::overwritePreset (const std::string& id, std::string* error)
{
    const auto* base = library_->find (id);
    if (base == nullptr || base->factory) { if (error) *error = "factory preset is read-only"; return false; }
    if (base->locked) { if (error) *error = "preset is locked"; return false; }
    auto p = analog::capturePreset (engine_.params(), engine_.description(), library_->config(), base->name, base->type, base);
    const bool ok = library_->save (std::move (p), error);
    if (ok) ++presetRevision_;
    return ok;
}

bool AnalogSynthProcessor::saveCurrent (std::string* error)
{
    if (currentPresetId_.empty()) { if (error) *error = "no current preset"; return false; }
    return overwritePreset (currentPresetId_, error);
}

bool AnalogSynthProcessor::saveCurrentAs (const std::string& name, const std::string& type, preset::Preset* out, std::string* error)
{
    auto p = analog::capturePreset (engine_.params(), engine_.description(), library_->config(), name, type);
    if (const auto* cur = currentPreset())
    {
        p.characteristics = cur->characteristics;
        p.customFields = cur->customFields;
        p.rating = cur->rating;
        p.captureOverrides = cur->captureOverrides;
        if (p.type.empty()) p.type = cur->type;
        p.derivedFrom = cur->factory ? cur->id : cur->derivedFrom;
        // les exceptions de capture reviennent dans les paramètres stockés
        p = analog::capturePreset (engine_.params(), engine_.description(), library_->config(), name, p.type, &p);
    }
    preset::Preset stored;
    if (! library_->importPreset (std::move (p), &stored, error)) return false;
    currentPresetId_ = stored.id;
    ++presetRevision_;
    if (out != nullptr) *out = stored;
    return true;
}

bool AnalogSynthProcessor::saveConfig (const preset::LibraryConfig& c)
{
    const bool ok = library_->setConfig (c);
    if (ok) ++presetRevision_;
    return ok;
}

juce::AudioProcessorEditor* AnalogSynthProcessor::createEditor() { return new AnalogSynthEditor (*this); }

// ---------------------------------------------------------------------------------------------------- éditeur

juce::Colour AnalogSynthEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

AnalogSynthEditor::AnalogSynthEditor (AnalogSynthProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language), palette_ (makePalette (p.settings()))
{
    setWantsKeyboardFocus (true);
    const auto& def = theme::definition (theme::themeFromString (p.settings().theme).value_or (theme::ThemeId::Dark));
    knobStyle_ = def.knob;

    synthView_ = std::make_unique<SynthView> (processor_, palette_, tr_, knobStyle_);
    synthView_->onChange = [this] (const SynthDescription& d) { processor_.uiEditSynth (d, "Synth", "synth"); };
    synthView_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    synthView_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    synthView_->onNoteOn = [this] (int key, float velocity) { processor_.uiNoteOn (key, velocity); };
    synthView_->onNoteOff = [this] (int key) { processor_.uiNoteOff (key); };
    synthView_->setTrack (0);
    addAndMakeVisible (*synthView_);

    for (const ParamId id : { analog::volume, analog::pan, sampler::gain, sampler::tuneSemitones, sampler::tuneCents })
        addAndMakeVisible (knobs_.add (new NeptuneKnob (processor_, id, palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); })));

    brandLabel_.setText ("ANALOGSYNTH", juce::dontSendNotification);
    brandLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    brandLabel_.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (brandLabel_);
    infoLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    infoLabel_.setFont (juce::FontOptions (11.0f));
    infoLabel_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (infoLabel_);

    prevButton_.setButtonText ("<");
    prevButton_.setTooltip (text ("preset.prev"));
    prevButton_.onClick = [this] { processor_.stepPreset (-1); refreshPresetBar(); };
    nextButton_.setButtonText (">");
    nextButton_.setTooltip (text ("preset.next"));
    nextButton_.onClick = [this] { processor_.stepPreset (+1); refreshPresetBar(); };
    nameButton_.onClick = [this] { openManager(); };
    saveButton_.setButtonText (text ("preset.save"));
    saveButton_.onClick = [this] { quickSave(); };
    managerButton_.setButtonText (text ("preset.manager"));
    managerButton_.onClick = [this] { openManager(); };
    undoButton_.setButtonText (text ("undo.undo"));
    undoButton_.onClick = [this] { processor_.uiUndo(); };
    redoButton_.setButtonText (text ("undo.redo"));
    redoButton_.onClick = [this] { processor_.uiRedo(); };
    panicButton_.setButtonText (text ("panic.button"));
    panicButton_.onClick = [this] { processor_.uiPanic(); synthView_->releaseKeys(); };
    for (auto* b : { &prevButton_, &nextButton_, &nameButton_, &saveButton_, &managerButton_, &undoButton_, &redoButton_, &panicButton_ })
        addAndMakeVisible (b);

    refreshPresetBar();
    synthView_->setDescription (processor_.synthDescription());
    startTimerHz (15);

    setResizable (true, true);
    setResizeLimits (900, 560, 1800, 1100);
    setSize (1180, 700); // en dernier : resized() place les composants créés ci-dessus
}

void AnalogSynthEditor::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    auto top = r.removeFromTop (40);
    brandLabel_.setBounds (top.removeFromLeft (190));
    prevButton_.setBounds (top.removeFromLeft (40).reduced (2));
    nameButton_.setBounds (top.removeFromLeft (juce::jmin (360, juce::jmax (180, top.getWidth() / 3))).reduced (2));
    nextButton_.setBounds (top.removeFromLeft (40).reduced (2));
    saveButton_.setBounds (top.removeFromLeft (90).reduced (2));
    managerButton_.setBounds (top.removeFromLeft (120).reduced (2));
    panicButton_.setBounds (top.removeFromRight (110).reduced (2));
    infoLabel_.setBounds (top);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (kBottomHeight);
    synthView_->placeScaled (r);

    const int knobSide = static_cast<int> (ui::effectiveHitSize (48.0, ui::KnobConfig {}));
    int x = bottom.getX();
    const int y = bottom.getY() + 22;
    for (auto* k : knobs_)
    {
        k->setBounds (x + (kKnobCell - knobSide) / 2, y, knobSide, knobSide);
        x += kKnobCell;
    }
    // annuler / rétablir : petits, sur la rangée des potards, à droite
    redoButton_.setBounds (bottom.getRight() - 64, y + (knobSide - 22) / 2, 64, 22);
    undoButton_.setBounds (bottom.getRight() - 64 - 4 - 64, y + (knobSide - 22) / 2, 64, 22);

    if (modal_) modal_->setBounds (getLocalBounds());
    if (manager_) manager_->setBounds (getLocalBounds());
}

void AnalogSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgApp));
    // noms et valeurs sous / sur les potards de la rangée du bas
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

bool AnalogSynthEditor::keyPressed (const juce::KeyPress& k)
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
    if (code == 'S' || code == 's') { quickSave(); return true; }
    return false;
}

void AnalogSynthEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_ || manager_) return;
    modal_ = std::make_unique<KnobModal> (processor_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<AnalogSynthEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    addAndMakeVisible (*modal_);
    modal_->setBounds (getLocalBounds());
    modal_->toFront (true);
}

void AnalogSynthEditor::closeModal()
{
    if (! modal_) return;
    processor_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

void AnalogSynthEditor::openManager()
{
    if (manager_ || modal_) return;
    manager_ = std::make_unique<PresetManager> (processor_, palette_, tr_);
    manager_->onClose = [safe = juce::Component::SafePointer<AnalogSynthEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->manager_.reset();
            safe->refreshPresetBar();
            safe->grabKeyboardFocus();
        });
    };
    manager_->onChanged = [safe = juce::Component::SafePointer<AnalogSynthEditor> (this)]
    {
        if (safe != nullptr) { safe->refreshPresetBar(); safe->synthView_->setDescription (safe->processor_.synthDescription()); }
    };
    addAndMakeVisible (*manager_);
    manager_->setBounds (getLocalBounds());
    manager_->toFront (true);
}

// « Enregistrer » : sur le preset courant s'il est enregistrable ; sinon (factory, verrouillé, aucun) c'est « Enregistrer sous ».
void AnalogSynthEditor::quickSave()
{
    if (processor_.canSaveCurrent())
    {
        std::string error;
        if (processor_.saveCurrent (&error)) { infoLabel_.setText (text ("preset.saved"), juce::dontSendNotification); refreshPresetBar(); return; }
    }
    openManager();
    if (manager_) manager_->showSaveAs();
}

void AnalogSynthEditor::refreshPresetBar()
{
    const auto* p = processor_.currentPreset();
    const bool modified = processor_.isModified();
    juce::String name = p != nullptr ? juce::String::fromUTF8 (p->name.c_str()) : text ("preset.none");
    if (modified) name += " *";
    if (p != nullptr && p->locked) name = text ("preset.tag_locked") + "  " + name;
    nameButton_.setButtonText (name);
    saveButton_.setEnabled (true);
    lastModified_ = modified;
    lastPresetId_ = processor_.currentPresetId();
    lastPresetRevision_ = processor_.presetRevision();
}

void AnalogSynthEditor::timerCallback()
{
    const bool modified = processor_.isModified();
    if (modified != lastModified_ || lastPresetId_ != processor_.currentPresetId() || lastPresetRevision_ != processor_.presetRevision())
    {
        const auto before = lastPresetRevision_;
        refreshPresetBar();
        if (before != processor_.presetRevision()) synthView_->setDescription (processor_.synthDescription());
    }
    const auto rev = static_cast<std::uint32_t> (processor_.undoManager().revision());
    if (rev != lastUndoRevision_)
    {
        lastUndoRevision_ = rev;
        synthView_->setDescription (processor_.synthDescription()); // annuler / rétablir ont pu changer le patch
    }
    synthView_->setHeldKeys (processor_.engine().heldKeyWord (0), processor_.engine().heldKeyWord (1)); // notes MIDI reçues
    undoButton_.setEnabled (processor_.undoManager().canUndo());
    redoButton_.setEnabled (processor_.undoManager().canRedo());
    const int voices = processor_.engine().voiceCount();
    if (voices != lastVoices_)
    {
        lastVoices_ = voices;
        infoLabel_.setText (text ("analog.voices") + " " + juce::String (voices), juce::dontSendNotification);
    }
    repaint (0, getHeight() - kBottomHeight - 12, getWidth(), kBottomHeight + 12); // valeurs des potards (automation de l'hôte)
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnalogSynthProcessor(); }
