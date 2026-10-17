#include "PluginEditor.h"
#include "VelocityPanel.h"
#include "PolyphonyPanel.h"
#include "ChokePanel.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/Mixer.h"

using neptune::core::theme::Token;

namespace
{
constexpr int kKnobCell = 118; // largeur d'un potard et de son nom/valeur (7 potards + Annuler/Rétablir tiennent dans 1160 px)

neptune::core::theme::Palette makePalette (const neptune::core::settings::GlobalSettings& st)
{
    const auto id = neptune::core::theme::themeFromString (st.theme).value_or (neptune::core::theme::ThemeId::Dark);
    return neptune::core::theme::withContrast (neptune::core::theme::definition (id), st.contrast);
}
} // namespace

juce::Colour NeptuneEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

// Clic droit sur la case de chargement d'un Groupe : menu « Assigner à un emplacement ».
struct NeptuneEditor::GroupMenuListener : public juce::MouseListener
{
    explicit GroupMenuListener (NeptuneEditor& o) : owner (o) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        for (int i = 0; i < owner.groupToggles_.size(); ++i)
            if (owner.groupToggles_[i] == e.eventComponent && static_cast<std::size_t> (i) < owner.groupNames_.size())
            {
                owner.showSlotAssignMenu (-1, owner.groupNames_[static_cast<std::size_t> (i)]);
                return;
            }
    }
    NeptuneEditor& owner;
};

NeptuneEditor::NeptuneEditor (NeptuneProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language), palette_ (makePalette (p.settings()))
{
    setWantsKeyboardFocus (true);

    {
        using namespace neptune::core;
        const auto& def = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
        for (const ParamId id : { sampler::gain, sampler::tuneSemitones, sampler::tuneCents, sampler::filter1Cutoff, sampler::filter1Resonance,
                                   sampler::filter2Cutoff, sampler::filter2Resonance })
            addAndMakeVisible (knobs_.add (new NeptuneKnob (processor_, id, palette_, def.knob,
                                                            [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); })));
    }
    loadButton_.setButtonText (text ("sampler.load"));
    loadButton_.onClick = [this] { chooseSample(); };
    addAndMakeVisible (loadButton_);
    statusLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    addAndMakeVisible (statusLabel_);
    midiFilterButton_.setButtonText (text ("midi.button"));
    midiFilterButton_.setTooltip (text ("midi.title"));
    midiFilterButton_.onClick = [this] { openMidiFilter(); };
    addAndMakeVisible (midiFilterButton_);
    controlButton_.setButtonText (text ("control.button"));
    controlButton_.setTooltip (text ("control.title"));
    controlButton_.onClick = [this] { showControlMenu(); };
    addAndMakeVisible (controlButton_);
    oscButton_.setButtonText (text ("osc.button"));
    oscButton_.setTooltip (text ("osc.title"));
    oscButton_.onClick = [this] { showOscMenu(); };
    addAndMakeVisible (oscButton_);
    voiceCountButton_.setTooltip (text ("voices.global_limit"));
    voiceCountButton_.onClick = [this]
    {
        juce::PopupMenu menu;
        const int current = processor_.masterSettings().maxVoices;
        menu.addSectionHeader (text ("voices.global_limit"));
        for (const int n : { 16, 32, 64, 128, 256, 512, 1024 })
            menu.addItem (n, juce::String (n), true, n == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&voiceCountButton_), [this] (int chosen)
        {
            if (chosen <= 0) return;
            auto m = processor_.masterSettings();
            m.maxVoices = chosen;
            processor_.uiEditMaster (m, "Voice limit");
            lastVoiceCount_ = -1; // le texte est relu au prochain tour
        });
    };
    addAndMakeVisible (voiceCountButton_);

    importButton_.setButtonText (text ("sampler.import"));
    importButton_.onClick = [this] { startImport(); };
    addAndMakeVisible (importButton_);
    removeButton_.setButtonText (text ("sampler.remove"));
    removeButton_.onClick = [this] { removeSelectedZones(); };
    addAndMakeVisible (removeButton_);
    selectionLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (selectionLabel_);
    loadTitle_.setText (text ("load.title"), juce::dontSendNotification);
    loadTitle_.setColour (juce::Label::textColourId, colour (Token::Text));
    addAndMakeVisible (loadTitle_);
    for (auto* t : { &rrToggle_, &releaseToggle_, &streamToggle_ })
    {
        t->setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        addAndMakeVisible (t);
    }
    rrToggle_.onClick = [this]
    {
        auto d = processor_.instrumentDescription();
        d.mainRoundRobinOnly = ! rrToggle_.getToggleState(); // coché = les alternatives sont chargées
        processor_.uiEditInstrument (std::move (d), "Load round-robin alternates");
    };
    releaseToggle_.onClick = [this]
    {
        auto d = processor_.instrumentDescription();
        d.loadReleaseSamples = releaseToggle_.getToggleState();
        processor_.uiEditInstrument (std::move (d), "Load release samples");
    };
    streamToggle_.onClick = [this]
    {
        auto d = processor_.instrumentDescription();
        d.streamFromDisk = streamToggle_.getToggleState();
        processor_.uiEditInstrument (std::move (d), "Stream from disk");
    };
    preloadLabel_.setText (text ("load.preload"), juce::dontSendNotification);
    preloadLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (preloadLabel_);
    for (const int kb : { 32, 64, 128, 256, 512, 1024, 2048, 4096 })
        preloadBox_.addItem (juce::String (kb) + " KB", kb);
    preloadBox_.onChange = [this]
    {
        if (updatingControls_) return;
        auto d = processor_.instrumentDescription();
        d.preloadKB = preloadBox_.getSelectedId();
        processor_.uiEditInstrument (std::move (d), "Preload size");
    };
    addAndMakeVisible (preloadBox_);

    groupsViewport_.setViewedComponent (&groupsHolder_, false);
    addAndMakeVisible (groupsViewport_);
    zoneTable_ = std::make_unique<ZoneTable> (palette_, tr_);
    zoneTable_->onSelectionChanged = [this] { lastRevision_ = 0xFFFFFFFFu; updateSamplerControls(); };
    zoneTable_->onCellEdited = [this] (int row, int column, int value)
    {
        // la ligne éditée fait partie d'une sélection multiple : la modification s'applique à toute la sélection
        auto rows = zoneTable_->selectedRows();
        if (std::find (rows.begin(), rows.end(), row) == rows.end()) rows = { row };
        auto d = processor_.instrumentDescription();
        for (const int r : rows)
        {
            if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
            auto& z = d.zones[static_cast<std::size_t> (r)];
            if (column == 7)
            {
                z.onRelease = value == 2;
                z.trigger = value == 3 ? neptune::core::TriggerCondition::First
                          : value == 4 ? neptune::core::TriggerCondition::Legato : neptune::core::TriggerCondition::Always;
            }
            else if (column == 8) z.chokeCuts = value;
            else if (column == 9) z.chokeCutBy = value;
        }
        processor_.uiEditInstrument (std::move (d), "Edit zone", "zone");
        lastRevision_ = 0xFFFFFFFFu;
        updateSamplerControls();
    };
    zoneTable_->onPlay = [this] (int row, bool down, bool stop)
    {
        if (stop) processor_.uiPreviewStop();
        else if (down) processor_.uiPreviewZone (row);
        else processor_.uiPreviewRelease();
    };
    addAndMakeVisible (*zoneTable_);

    modeButton_.onClick = [this]
    {
        editZone ([] (neptune::core::ZoneDescription& z)
                  { z.mode = z.mode == neptune::core::PlayMode::Loop ? neptune::core::PlayMode::OneShot : neptune::core::PlayMode::Loop; });
    };
    addAndMakeVisible (modeButton_);

    rootSlider_.setRange (0.0, 127.0, 1.0);
    rootSlider_.onValueChange = [this]
    {
        if (updatingControls_) return;
        const int v = static_cast<int> (rootSlider_.getValue());
        editZone ([v] (neptune::core::ZoneDescription& z) { z.rootNote = v; });
    };
    addAndMakeVisible (rootSlider_);
    rootLabel_.setText (text ("sampler.root"), juce::dontSendNotification);
    rootLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (rootLabel_);

    envelopeEditor_ = std::make_unique<EnvelopeEditor> (palette_, tr_);
    envelopeEditor_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    envelopeEditor_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    envelopeEditor_->onChange = [this] (const neptune::core::EnvelopeDescription& e)
    {
        editZone ([e] (neptune::core::ZoneDescription& z) { z.envelope = e; });
    };
    addAndMakeVisible (*envelopeEditor_);

    matrix_ = std::make_unique<MappingMatrix> (palette_, tr_);
    matrix_->onSelect = [this] (const std::vector<int>& zones) { setZoneSelection (zones); };
    matrix_->onEdit = [this] (neptune::core::InstrumentDescription d, const char* name)
    {
        processor_.uiEditInstrument (std::move (d), name, "matrix");
    };
    matrix_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    matrix_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    matrix_->onDeleteRequested = [this] { removeSelectedZones(); };
    matrix_->onNoteOn = [this] (int key, float velocity) { processor_.uiNoteOn (key, velocity); };
    matrix_->onNoteOff = [this] (int key) { processor_.uiNoteOff (key); };
    matrix_->onOpenVelocity = [this] (juce::Component& anchor) { openVelocityPanel (anchor); };
    matrix_->onOpenPolyphony = [this] (juce::Component& anchor) { openPolyphonyPanel (anchor); };
    addChildComponent (*matrix_);
    listToggleButton_.setButtonText (text ("map.list"));
    listToggleButton_.setClickingTogglesState (true);
    listToggleButton_.setToggleState (true, juce::dontSendNotification);
    listToggleButton_.onClick = [this] { listVisible_ = listToggleButton_.getToggleState(); updateViewVisibility(); resized(); };
    addAndMakeVisible (listToggleButton_);

    voicesView_ = std::make_unique<VoicesView> (palette_, tr_);
    addChildComponent (*voicesView_);
    voicesButton_.setButtonText (text ("voices.button"));
    voicesButton_.setClickingTogglesState (true);
    voicesButton_.onClick = [this]
    {
        showVoices_ = voicesButton_.getToggleState();
        processor_.setVoiceMonitor (showVoices_); // le moteur ne publie les voix que si on les regarde
        updateViewVisibility();
        resized();
    };
    filtersButton_.setButtonText (text ("filt.button"));
    filtersButton_.onClick = [this] { openFilterEditor(); };
    addAndMakeVisible (filtersButton_);
    slotsButton_.setButtonText (text ("slots.button"));
    slotsButton_.onClick = [this] { openSlotsPanel (slotsButton_); };
    addChildComponent (slotsButton_);
    zoneTable_->onContextMenu = [this] (int row) { showSlotAssignMenu (row, {}); };
    groupMenuListener_ = std::make_unique<GroupMenuListener> (*this);
    panicButton_.setButtonText (text ("panic.button"));
    panicButton_.onClick = [this]
    {
        processor_.uiPanic();
        matrix_->clearHeldKeys();
        synthView_->releaseKeys();
        fmView_->releaseKeys();
        organView_->releaseKeys();
    };
    addAndMakeVisible (panicButton_);
    addChildComponent (voicesButton_);

    triggerLabel_.setText (text ("trig.label"), juce::dontSendNotification);
    chokeLabel_.setText (text ("choke.label"), juce::dontSendNotification);
    for (auto* l : { &triggerLabel_, &chokeLabel_ })
    {
        l->setColour (juce::Label::textColourId, colour (Token::TextDim));
        addAndMakeVisible (*l);
    }
    triggerBox_.addItem (text ("trig.attack"), 1);
    triggerBox_.addItem (text ("trig.release"), 2);
    triggerBox_.addItem (text ("trig.first"), 3);
    triggerBox_.addItem (text ("trig.legato"), 4);
    triggerBox_.onChange = [this]
    {
        if (updatingControls_) return;
        const int id = triggerBox_.getSelectedId();
        if (id < 1) return;
        editZone ([id] (neptune::core::ZoneDescription& z)
        {
            z.onRelease = id == 2;
            z.trigger = id == 3 ? neptune::core::TriggerCondition::First : id == 4 ? neptune::core::TriggerCondition::Legato
                                                                                    : neptune::core::TriggerCondition::Always;
        });
    };
    addAndMakeVisible (triggerBox_);
    chokeBox_.addItem (text ("choke.none"), 1);
    for (int g = 1; g <= neptune::core::maxChokeGroup; ++g) chokeBox_.addItem (juce::String (g), g + 1);
    chokeBox_.setTextWhenNothingSelected (text ("choke.advanced"));
    chokeBox_.onChange = [this]
    {
        if (updatingControls_) return;
        const int id = chokeBox_.getSelectedId();
        if (id < 1) return;
        const int g = id - 1; // réglage simple : le même numéro des deux côtés (groupe symétrique)
        editZone ([g] (neptune::core::ZoneDescription& z) { z.chokeCuts = g; z.chokeCutBy = g; });
    };
    addAndMakeVisible (chokeBox_);
    chokeMoreButton_.setButtonText ("...");
    chokeMoreButton_.onClick = [this] { openChokePanel (chokeMoreButton_); };
    addAndMakeVisible (chokeMoreButton_);

    viewButton_.setButtonText ("Probe");
    viewButton_.onClick = [this] { setView (false, true); };
    addAndMakeVisible (viewButton_);
    mixerButton_.setButtonText (text ("mix.tab"));
    mixerButton_.onClick = [this] { setView (true, false); };
    addAndMakeVisible (mixerButton_);
    mappingButton_.setButtonText (text ("map.tab"));
    mappingButton_.onClick = [this] { setView (false, false); };
    addAndMakeVisible (mappingButton_);

    mixerView_ = std::make_unique<MixerView> (processor_, palette_, tr_);
    mixerView_->onSelectTrack = [this] (int t) { processor_.selectTrack (t); };
    mixerView_->onOpenInsert = [this] (NeptuneProcessor::StripKind kind, int index, int slot) { openInsert (kind, index, slot); };
    addChildComponent (*mixerView_);

    {
        using namespace neptune::core;
        const auto& themeDef = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
        synthView_ = std::make_unique<SynthView> (processor_, palette_, tr_, themeDef.knob);
    }
    synthView_->onChange = [this] (const neptune::core::SynthDescription& d) { processor_.uiEditSynth (d, "Synth", "synth"); };
    synthView_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    synthView_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    synthView_->onNoteOn = [this] (int key, float velocity)
    {
        const int track = processor_.selectedTrack();
        synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))] = track;
        processor_.uiNoteOnTrack (track, key, velocity);
    };
    synthView_->onNoteOff = [this] (int key)
    {
        processor_.uiNoteOffTrack (synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))], key);
    };
    addChildComponent (*synthView_);

    {
        using namespace neptune::core;
        const auto& themeDef = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
        fmView_ = std::make_unique<FmView> (processor_, palette_, tr_, themeDef.knob);
    }
    fmView_->onChange = [this] (const neptune::core::fm::FmDescription& d) { processor_.uiEditFm (d, "FM", "fm"); };
    fmView_->onGestureBegin = [this] { processor_.uiBeginGesture(); };
    fmView_->onGestureEnd = [this] { processor_.uiEndGesture(); };
    fmView_->onNoteOn = [this] (int key, float velocity)
    {
        const int track = processor_.selectedTrack();
        synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))] = track;
        processor_.uiNoteOnTrack (track, key, velocity);
    };
    fmView_->onNoteOff = [this] (int key)
    {
        processor_.uiNoteOffTrack (synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))], key);
    };
    sysexIO_ = std::make_unique<FmSysexIO> (*this, processor_, tr_);
    fmView_->onImportSysex = [this]
    {
        sysexIO_->importFile ([this] (const neptune::core::fm::Patch& patch)
                              {
                                  auto d = processor_.fmDescription();
                                  d.patch = patch;
                                  processor_.uiEditFm (d, "Import DX7 voice");
                              });
    };
    fmView_->onExportVoice = [this] { sysexIO_->exportVoice (processor_.fmDescription().patch); };
    fmView_->onExportBank = [this] { sysexIO_->exportBank(); };
    addChildComponent (*fmView_);

    {
        using namespace neptune::core;
        const auto& themeDef = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
        OrganView::KnobIds extra;
        extra.push_back ([] (int t) { return mixer::samplerParam (t, mixer::kGain); }); // le volume est celui de la bande
        organView_ = std::make_unique<OrganView> (processor_, palette_, tr_, themeDef.knob, std::move (extra),
                                                  [this] (neptune::core::ParamId id, neptune::core::uistate::Terminal t) { openModal (id, t); });
        organView_->onChange = [this] (const organ::OrganDescription& d) { processor_.uiEditOrgan (d, "Organ keyboards", "organ"); };
        organView_->onNoteOn = [this] (int key, int channel)
        {
            const int track = processor_.selectedTrack();
            synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))] = track;
            processor_.uiNoteOnTrack (track, key, 0.8f, channel);
        };
        organView_->onNoteOff = [this] (int key, int channel)
        {
            processor_.uiNoteOffTrack (synthKeyTrack_[static_cast<std::size_t> (juce::jlimit (0, 127, key))], key, channel);
        };
        addChildComponent (*organView_);
    }

    presetBar_ = std::make_unique<PresetBar> (processor_, palette_, tr_);
    presetBar_->onOpenManager = [this] (bool saveAs) { openPresetManager (saveAs); };
    addChildComponent (*presetBar_);

    for (int t = 0; t < neptune::core::mixer::numTracks; ++t) trackBox_.addItem (juce::String (t + 1), t + 1);
    trackBox_.onChange = [this]
    {
        if (updatingControls_) return;
        const int id = trackBox_.getSelectedId();
        if (id >= 1) processor_.selectTrack (id - 1);
    };
    addAndMakeVisible (trackBox_);

    undoButton_.setButtonText (text ("undo.undo"));
    redoButton_.setButtonText (text ("undo.redo"));
    undoButton_.onClick = [this] { processor_.uiUndo(); };
    redoButton_.onClick = [this] { processor_.uiRedo(); };
    addAndMakeVisible (undoButton_);
    addAndMakeVisible (redoButton_);

    restoreButton_.setButtonText (text ("recovery.restore"));
    discardButton_.setButtonText (text ("recovery.discard"));
    recoveryLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    addChildComponent (recoveryLabel_);
    restoreButton_.onClick = [this] { processor_.restoreRecovered(); updateRecoveryBanner(); };
    discardButton_.onClick = [this] { processor_.discardRecovered(); updateRecoveryBanner(); };
    addChildComponent (restoreButton_);
    addChildComponent (discardButton_);

    profileLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    addAndMakeVisible (profileLabel_);
    profileSwitchButton_.setButtonText (text ("profile.switch"));
    profileSwitchButton_.onClick = [this]
    {
        auto& shared = processor_.profile().shared();
        shared.set (shared.get() == neptune::core::PerformanceProfile::Live ? neptune::core::PerformanceProfile::Studio
                                                                             : neptune::core::PerformanceProfile::Live);
    };
    addAndMakeVisible (profileSwitchButton_);
    profileLockButton_.onClick = [this]
    {
        auto& p = processor_.profile();
        if (p.locked()) p.unlock(); else p.lockToCurrent();
        processor_.notifyStateChanged();
    };
    addAndMakeVisible (profileLockButton_);

    latencyLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (latencyLabel_);
    for (const int samples : { 0, 64, 512, 2048 })
    {
        auto* b = latencyButtons_.add (new juce::TextButton (juce::String::fromUTF8 (tr_.format ("latency.set", { std::to_string (samples) }).c_str())));
        b->onClick = [this, samples] { processor_.setLatencySamples (samples); }; // thread message
        addAndMakeVisible (b);
    }
    startTimerHz (30);

    // La taille en dernier : resized() place les composants, qui doivent donc exister. (Fixée avant leur création,
    // la disposition n'était calculée qu'au premier redimensionnement par l'hôte : potards absents sous Linux.)
    updateViewVisibility();
    root_.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    root_.adopt (*this); // tout le contenu dans la racine mise à l'échelle
    setResizable (true, true);
    setResizeLimits (420, 300, 3000, 2000); // en dessous de la taille de conception, l'interface est réduite (voir resized)
    {
        // taille de départ : celle de conception, mais jamais plus grande que 90 % de l'écran utile
        int w = 1160, h = 960;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, d->userArea.getWidth() * 9 / 10);
            h = juce::jmin (h, d->userArea.getHeight() * 85 / 100);
        }
        setSize (juce::jmax (420, w), juce::jmax (300, h));
    }
}

void NeptuneEditor::openPresetManager (bool saveAs)
{
    if (presetManager_ || modal_ || insertOverlay_ || wizard_ || filterEditor_) return;
    presetManager_ = std::make_unique<PresetManager> (processor_, palette_, tr_);
    presetManager_->onClose = [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->presetManager_.reset();
            safe->grabKeyboardFocus();
        });
    };
    presetManager_->onChanged = [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
    {
        if (safe != nullptr) safe->presetBar_->refresh();
    };
    root_.addAndMakeVisible (*presetManager_);
    presetManager_->setBounds (rootBounds());
    presetManager_->toFront (true);
    if (saveAs) presetManager_->showSaveAs();
}

void NeptuneEditor::openMidiFilter()
{
    if (midiFilterEditor_ || presetManager_ || modal_ || insertOverlay_ || wizard_ || filterEditor_) return;
    const int track = processor_.selectedTrack();
    midiFilterEditor_ = std::make_unique<MidiFilterEditor> (
        palette_, tr_, track,
        [this, track] { return processor_.trackSettings (track).midiFilter; },
        [this, track] (const neptune::core::midi::FilterDescription& f)
        {
            auto s = processor_.trackSettings (track);
            s.midiFilter = f;
            processor_.uiEditTrack (track, s, "MIDI filter", "midi_filter");
        },
        [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
        {
            juce::MessageManager::callAsync ([safe]
            {
                if (safe == nullptr) return;
                safe->midiFilterEditor_.reset();
                safe->grabKeyboardFocus();
            });
        });
    root_.addAndMakeVisible (*midiFilterEditor_);
    midiFilterEditor_->setBounds (rootBounds());
    midiFilterEditor_->toFront (true);
}

void NeptuneEditor::openModal (neptune::core::ParamId id, neptune::core::uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (processor_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
                                          {
                                              // Fermeture différée : la modale ne se détruit pas depuis son propre gestionnaire.
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    root_.addAndMakeVisible (*modal_);
    modal_->setBounds (rootBounds());
    modal_->toFront (true);
}

// Éditeur d'un insert, superposé à toute la fenêtre.
void NeptuneEditor::openInsert (NeptuneProcessor::StripKind kind, int index, int slot)
{
    if (insertOverlay_ || modal_ || wizard_ || filterEditor_) return;
    using namespace neptune::core;
    const auto& def = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
    auto overlay = std::make_unique<InsertOverlay> (processor_, kind, index, slot, palette_, tr_, def.knob,
                                                    [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
                                                    {
                                                        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeInsert(); });
                                                    });
    if (! overlay->valid()) return;
    insertOverlay_ = std::move (overlay);
    root_.addAndMakeVisible (*insertOverlay_);
    insertOverlay_->setBounds (rootBounds());
    insertOverlay_->toFront (true);
    insertOverlay_->grabKeyboardFocus();
}

void NeptuneEditor::closeInsert()
{
    insertOverlay_.reset();
    grabKeyboardFocus();
}

void NeptuneEditor::closeModal()
{
    if (! modal_) return;
    processor_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

void NeptuneEditor::chooseSample()
{
    chooser_ = std::make_unique<juce::FileChooser> (text ("sampler.choose"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<NeptuneEditor> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return; // annulé
                               const auto path = file.getFullPathName().toStdString();
                               // Un seul sample : on garde les réglages de la première zone et on remplace le reste.
                               auto d = safe->processor_.instrumentDescription();
                               neptune::core::ZoneDescription z = d.zones.empty() ? neptune::core::ZoneDescription {} : d.zones.front();
                               z.samplePath = path;
                               z.keyLow = 0; z.keyHigh = 127; z.velLow = 1; z.velHigh = 127; z.roundRobin = 0; z.group.clear(); z.onRelease = false;
                               d.zones.assign (1, z);
                               safe->processor_.uiEditInstrument (std::move (d), "Replace sample");
                               safe->lastRevision_ = 0xFFFFFFFFu;
                               safe->updateSamplerControls();
                           });
}

// Applique un changement aux zones sélectionnées (toutes si rien n'est sélectionné) et republie l'instrument.
void NeptuneEditor::editZone (const std::function<void (neptune::core::ZoneDescription&)>& change)
{
    auto d = processor_.instrumentDescription();
    if (d.zones.empty()) return; // pas de sample : rien à régler
    const auto selection = zoneTable_->selectedRows();
    if (selection.empty())
        for (auto& z : d.zones) change (z);
    else
        for (const int row : selection)
            if (row >= 0 && row < static_cast<int> (d.zones.size())) change (d.zones[static_cast<std::size_t> (row)]);
    processor_.uiEditInstrument (std::move (d), "Edit zone", "zone");
    updateSamplerControls();
}

void NeptuneEditor::startImport()
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("sampler.import_files"));
    menu.addItem (2, text ("sampler.import_folder"));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&importButton_),
                        [safe = juce::Component::SafePointer<NeptuneEditor> (this)] (int choice)
                        {
                            if (safe == nullptr || choice == 0) return;
                            auto* self = safe.getComponent();
                            const bool folder = choice == 2;
                            // Choix d'un dossier : AUCUN filtre (la boîte système l'appliquerait aux dossiers eux-mêmes et les
                            // rendrait non sélectionnables) ; les fichiers audio sont filtrés ensuite, dans le dossier.
                            self->importChooser_ = std::make_unique<juce::FileChooser> (self->text ("sampler.choose"), juce::File(),
                                                                                       folder ? juce::String ("*") : juce::String ("*.wav;*.aif;*.aiff;*.flac"));
                            const int flags = folder ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                                     : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                                           | juce::FileBrowserComponent::canSelectMultipleItems;
                            self->importChooser_->launchAsync (flags, [safe, folder] (const juce::FileChooser& fc)
                            {
                                if (safe == nullptr) return;
                                std::vector<std::string> paths;
                                if (folder)
                                {
                                    const auto dir = fc.getResult();
                                    if (! dir.isDirectory()) return;
                                    for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
                                        paths.push_back (f.getFullPathName().toStdString());
                                }
                                else
                                    for (const auto& f : fc.getResults()) paths.push_back (f.getFullPathName().toStdString());
                                if (! paths.empty()) safe->openWizard (std::move (paths));
                            });
                        });
}

void NeptuneEditor::openWizard (std::vector<std::string> paths)
{
    if (wizard_ || modal_) return;
    const auto saved = processor_.lastImportTemplate();
    auto templ = saved.empty() ? neptune::core::import::Template {} : neptune::core::import::Template::fromJson (saved);
    wizard_ = std::make_unique<ImportWizard> (std::move (paths), palette_, tr_, std::move (templ),
        processor_.instrumentDescription(), fileInfo_, neptune::core::memory::availablePhysicalBytes(),
        [safe = juce::Component::SafePointer<NeptuneEditor> (this)] (const ImportWizard::Result& r)
        {
            if (safe == nullptr) return;
            safe->applyImport (r);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeWizard(); });
        },
        [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeWizard(); });
        });
    root_.addAndMakeVisible (*wizard_);
    wizard_->setBounds (rootBounds());
    wizard_->toFront (true);
}

void NeptuneEditor::closeWizard()
{
    wizard_.reset();
    grabKeyboardFocus();
}

void NeptuneEditor::applyImport (const ImportWizard::Result& r)
{
    processor_.setLastImportTemplate (r.templ.toJson());
    auto d = processor_.instrumentDescription();
    if (r.replace)
    {
        std::vector<int> all (d.zones.size());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int> (i);
        d.removeZones (std::move (all)); // les emplacements visant des zones sont libérés ; ceux qui visent un Groupe restent
    }
    for (const auto& z : r.zones) d.zones.push_back (z);
    // Import léger : en remplaçant, le choix de l'assistant s'applique tel quel ; en ajoutant, on ne fait que restreindre.
    d.mainRoundRobinOnly = r.replace ? r.mainRoundRobinOnly : (d.mainRoundRobinOnly || r.mainRoundRobinOnly);
    d.loadReleaseSamples = r.replace ? r.loadReleaseSamples : (d.loadReleaseSamples && r.loadReleaseSamples);
    d.streamFromDisk = r.replace ? r.streamFromDisk : (d.streamFromDisk || r.streamFromDisk);
    d.roundRobinMode = r.roundRobinMode;
    processor_.uiEditInstrument (std::move (d), "Import samples");
    lastRevision_ = 0xFFFFFFFFu;
    updateSamplerControls();
}

// Réponse de vélocité de l'instrument : fenêtre flottante ancrée sous le bouton de la matrice.
void NeptuneEditor::openVelocityPanel (juce::Component& anchor)
{
    const auto d = processor_.instrumentDescription();
    auto panel = std::make_unique<VelocityPanel> (palette_, tr_, d.velocityFadeWidth, d.velocityFadeLinear, d.velocityCurve);
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    panel->onGestureBegin = [safe] { if (safe != nullptr) safe->processor_.uiBeginGesture(); };
    panel->onGestureEnd = [safe] { if (safe != nullptr) safe->processor_.uiEndGesture(); };
    panel->onChange = [safe] (int width, bool linear, const neptune::core::VelocityCurve& curve)
    {
        if (safe == nullptr) return;
        auto desc = safe->processor_.instrumentDescription();
        desc.velocityFadeWidth = width;
        desc.velocityFadeLinear = linear;
        desc.velocityCurve = curve;
        safe->processor_.uiEditInstrument (std::move (desc), "Velocity response", "velocity");
    };
    panel->onUndo = [safe] { if (safe != nullptr) safe->processor_.uiUndo(); };
    panel->onRedo = [safe] { if (safe != nullptr) safe->processor_.uiRedo(); };
    velocityPanel_ = panel.get();
    juce::Component::SafePointer<VelocityPanel> focus (panel.get());
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
    juce::MessageManager::callAsync ([focus] { if (focus != nullptr) focus->grabKeyboardFocus(); }); // Ctrl+Z dans la fenêtre
}

// Polyphonie de l'instrument : même principe que la réponse de vélocité (fenêtre flottante, annulable).
void NeptuneEditor::openPolyphonyPanel (juce::Component& anchor)
{
    auto fromDescription = [] (const neptune::core::InstrumentDescription& d)
    {
        PolyphonyPanel::Settings s;
        s.mode = d.polyphony; s.maxVoices = d.maxVoices; s.steal = d.stealMode; s.sameKey = d.sameKey;
        s.cutFadeMs = d.cutFadeMs; s.glideMs = d.glideMs; s.chokeSameZone = d.chokeSameZone;
        return s;
    };
    auto panel = std::make_unique<PolyphonyPanel> (palette_, tr_, fromDescription (processor_.instrumentDescription()));
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    panel->onGestureBegin = [safe] { if (safe != nullptr) safe->processor_.uiBeginGesture(); };
    panel->onGestureEnd = [safe] { if (safe != nullptr) safe->processor_.uiEndGesture(); };
    panel->onUndo = [safe] { if (safe != nullptr) safe->processor_.uiUndo(); };
    panel->onRedo = [safe] { if (safe != nullptr) safe->processor_.uiRedo(); };
    panel->onChange = [safe] (const PolyphonyPanel::Settings& s)
    {
        if (safe == nullptr) return;
        auto desc = safe->processor_.instrumentDescription();
        desc.polyphony = s.mode; desc.maxVoices = s.maxVoices; desc.stealMode = s.steal; desc.sameKey = s.sameKey;
        desc.cutFadeMs = s.cutFadeMs; desc.glideMs = s.glideMs; desc.chokeSameZone = s.chokeSameZone;
        safe->processor_.uiEditInstrument (std::move (desc), "Polyphony", "polyphony");
    };
    polyphonyPanel_ = panel.get();
    juce::Component::SafePointer<PolyphonyPanel> focus (panel.get());
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
    juce::MessageManager::callAsync ([focus] { if (focus != nullptr) focus->grabKeyboardFocus(); });
}

// Emplacements assignables : fenêtre flottante (les 16 lignes) ou menu contextuel d'une zone / d'un Groupe.
void NeptuneEditor::openSlotsPanel (juce::Component& anchor)
{
    auto panel = std::make_unique<SlotsPanel> (processor_, palette_, tr_, processor_.selectedTrack());
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    panel->getDescription = [safe] { return safe != nullptr ? safe->processor_.instrumentDescription() : neptune::core::InstrumentDescription {}; };
    panel->getSelectedZones = [safe] { return safe != nullptr ? safe->zoneTable_->selectedRows() : std::vector<int> {}; };
    panel->onEdit = [safe] (neptune::core::InstrumentDescription d)
    {
        if (safe != nullptr) safe->processor_.uiEditInstrument (std::move (d), "Slots", "slots");
    };
    slotsPanel_ = panel.get();
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

void NeptuneEditor::showSlotAssignMenu (int zone, const std::string& group)
{
    using namespace neptune::core;
    const auto d = processor_.instrumentDescription();
    if (group.empty() && (zone < 0 || zone >= static_cast<int> (d.zones.size()))) return;
    static constexpr int kOpenWindow = 100000;
    juce::PopupMenu menu;
    const juce::String targetName = group.empty() ? text ("slots.zone") + " " + juce::String (zone + 1) : text ("slots.group") + " " + juce::String::fromUTF8 (group.c_str());
    menu.addSectionHeader (targetName);
    for (int s = 0; s < kZoneSlots; ++s)
    {
        juce::PopupMenu sub;
        const auto& cur = d.slots[static_cast<std::size_t> (s)];
        const bool same = cur.assigned() && (group.empty() ? (! cur.isGroup && cur.zone == zone) : (cur.isGroup && cur.group == group));
        for (int p = 1; p < kSlotParams; ++p)
        {
            static const char* const keys[kSlotParams] = { "", "slots.param_attack", "slots.param_decay", "slots.param_sustain", "slots.param_release",
                                                           "slots.param_level", "slots.param_tune" };
            sub.addItem (1 + s * 16 + p, text (keys[p]), true, same && static_cast<int> (cur.param) == p);
        }
        if (same) { sub.addSeparator(); sub.addItem (1 + s * 16, text ("slots.free_slot")); }
        juce::String title = text ("slots.slot") + " " + juce::String (s + 1);
        if (cur.assigned() && ! same) title += "  (" + text ("slots.taken") + ")";
        menu.addSubMenu (title, sub, true, nullptr, same);
    }
    menu.addSeparator();
    menu.addItem (kOpenWindow, text ("slots.open_window"));
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe, zone, group] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == kOpenWindow) { safe->openSlotsPanel (safe->slotsButton_); return; }
        const int slot = (choice - 1) / 16, param = (choice - 1) % 16;
        auto desc = safe->processor_.instrumentDescription();
        if (slot < 0 || slot >= kZoneSlots) return;
        auto& a = desc.slots[static_cast<std::size_t> (slot)];
        if (param == 0) a = SlotAssignment {};   // libérer l'emplacement
        else
        {
            a = SlotAssignment {};
            a.param = static_cast<SlotParam> (param);
            if (group.empty()) a.zone = zone; else { a.isGroup = true; a.group = group; }
        }
        safe->processor_.uiEditInstrument (std::move (desc), "Slots", "slots");
    });
}

// Éditeur des filtres par voie.
void NeptuneEditor::openFilterEditor()
{
    if (filterEditor_ || wizard_ || modal_) return;
    filterEditor_ = std::make_unique<FilterEditor> (palette_, tr_,
                                                    neptune::core::theme::definition (neptune::core::theme::themeFromString (processor_.settings().theme)
                                                                                          .value_or (neptune::core::theme::ThemeId::Dark)).knob);
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    filterEditor_->onGestureBegin = [safe] { if (safe != nullptr) safe->processor_.uiBeginGesture(); };
    filterEditor_->onGestureEnd = [safe] { if (safe != nullptr) safe->processor_.uiEndGesture(); };
    filterEditor_->onUndo = [safe] { if (safe != nullptr) safe->processor_.uiUndo(); };
    filterEditor_->onRedo = [safe] { if (safe != nullptr) safe->processor_.uiRedo(); };
    filterEditor_->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeFilterEditor(); });
    };
    filterEditor_->onDefaultChanged = [safe] (const neptune::core::FilterBlock& b)
    {
        if (safe == nullptr) return;
        auto d = safe->processor_.instrumentDescription();
        d.defaultFilters = b;
        safe->processor_.uiEditInstrument (std::move (d), "Filters", "filter");
    };
    filterEditor_->onZonesChanged = [safe] (bool overrideOn, const neptune::core::FilterBlock& b)
    {
        if (safe == nullptr) return;
        auto d = safe->processor_.instrumentDescription();
        for (const int r : safe->zoneTable_->selectedRows())
        {
            if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
            auto& z = d.zones[static_cast<std::size_t> (r)];
            z.filterOverride = overrideOn;
            if (overrideOn) z.filters = b;
        }
        safe->processor_.uiEditInstrument (std::move (d), "Filters", "filter");
    };
    filterEditor_->onParam = [safe] (int index, double plain, int phase)
    {
        if (safe == nullptr) return;
        using namespace neptune::core;
        const int tr = safe->processor_.selectedTrack();
        const ParamId ids[4] = { mixer::samplerParam (tr, mixer::kFilter1Cutoff), mixer::samplerParam (tr, mixer::kFilter1Resonance),
                                 mixer::samplerParam (tr, mixer::kFilter2Cutoff), mixer::samplerParam (tr, mixer::kFilter2Resonance) };
        const ParamId id = ids[juce::jlimit (0, 3, index)];
        if (phase == 0) safe->processor_.uiBeginEdit (id);
        else if (phase == 2) safe->processor_.uiEndEdit (id);
        else if (const auto* spec = safe->processor_.parameters().find (id))
            safe->processor_.uiSetNormalised (id, spec->toNormalized (plain));
    };
    root_.addAndMakeVisible (*filterEditor_);
    filterEditor_->setBounds (rootBounds());
    filterEditor_->toFront (true);
    updateFilterEditor();
    filterEditor_->grabKeyboardFocus();
}

void NeptuneEditor::closeFilterEditor()
{
    filterEditor_.reset();
    grabKeyboardFocus();
}

void NeptuneEditor::updateFilterEditor()
{
    if (! filterEditor_) return;
    using namespace neptune::core;
    const auto& ps = processor_.parameters();
    FilterEditor::DefaultParams p;
    const int tr = processor_.selectedTrack();
    p.cutoffHz[0] = ps.get (mixer::samplerParam (tr, mixer::kFilter1Cutoff));
    p.cutoffHz[1] = ps.get (mixer::samplerParam (tr, mixer::kFilter2Cutoff));
    p.resonance[0] = ps.get (mixer::samplerParam (tr, mixer::kFilter1Resonance)) / 100.0;
    p.resonance[1] = ps.get (mixer::samplerParam (tr, mixer::kFilter2Resonance)) / 100.0;
    const auto& d = cachedDescription_;
    const auto sel = zoneTable_->selectedRows();
    int count = 0;
    bool overrideOn = false;
    FilterBlock zoneBlock = d.defaultFilters;
    bool first = true;
    for (const int r : sel)
    {
        if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
        const auto& z = d.zones[static_cast<std::size_t> (r)];
        ++count;
        if (first) { overrideOn = z.filterOverride; if (z.filterOverride) zoneBlock = z.filters; first = false; }
    }
    filterEditor_->setState (d.defaultFilters, p, count, overrideOn, zoneBlock);
}

// Moniteur des voix : ce qui sonne, plus (quelques instants) ce qui vient de finir ; surbrillance dans la liste et la matrice.
void NeptuneEditor::updateVoices()
{
    if (! showVoices_) // moniteur éteint : rien n'est publié par le moteur, plus de surbrillance
    {
        if (! seenVoices_.empty()) { seenVoices_.clear(); zoneTable_->setPlayingRows ({}); matrix_->setSoundingZones ({}); }
        return;
    }
    neptune::core::Engine::VoiceInfo info[neptune::core::Engine::kVoiceSnapshot];
    const int n = processor_.readVoices (info, neptune::core::Engine::kVoiceSnapshot);
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& [order, s] : seenVoices_) s.present = false;
    std::vector<int> sounding;
    for (int i = 0; i < n; ++i)
    {
        auto& s = seenVoices_[info[i].order];
        s.info = info[i];
        s.lastSeen = now;
        s.present = true;
        if (info[i].sourceIndex >= 0 && info[i].track == processor_.selectedTrack()) sounding.push_back (info[i].sourceIndex);
    }
    constexpr juce::uint32 kKeepMs = 700;
    for (auto it = seenVoices_.begin(); it != seenVoices_.end();)
        it = (! it->second.present && now - it->second.lastSeen > kKeepMs) ? seenVoices_.erase (it) : std::next (it);
    while (seenVoices_.size() > 160) seenVoices_.erase (seenVoices_.begin()); // les plus anciennes d'abord

    std::sort (sounding.begin(), sounding.end());
    sounding.erase (std::unique (sounding.begin(), sounding.end()), sounding.end());
    zoneTable_->setPlayingRows (sounding);
    matrix_->setSoundingZones (sounding);

    std::array<neptune::core::InstrumentDescription, neptune::core::mixer::numTracks> descs; // une description par piste, lue une fois
    std::array<bool, neptune::core::mixer::numTracks> haveDesc {};
    int held = 0, releasing = 0, fading = 0;
    std::vector<VoicesView::Row> rows;
    for (auto it = seenVoices_.rbegin(); it != seenVoices_.rend(); ++it) // la plus récente en haut
    {
        const auto& s = it->second;
        const auto& v = s.info;
        VoicesView::Row r;
        const auto tr = static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, v.track));
        if (! haveDesc[tr]) { descs[tr] = processor_.instrumentDescription (static_cast<int> (tr)); haveDesc[tr] = true; }
        const auto& d = descs[tr];
        const bool known = v.sourceIndex >= 0 && v.sourceIndex < static_cast<int> (d.zones.size());
        const neptune::core::ZoneDescription* z = known ? &d.zones[static_cast<std::size_t> (v.sourceIndex)] : nullptr;
        r.sample = z != nullptr ? juce::File (juce::String::fromUTF8 (z->samplePath.c_str())).getFileName() : juce::String ("?");
        r.key = juce::String (v.key) + "  " + juce::String::fromUTF8 (neptune::core::import::midiToNoteName (v.key, neptune::core::import::NoteConvention {}).c_str());
        r.velocity = juce::String (v.velocity);
        r.zone = juce::String (v.track + 1) + ":" + (known ? juce::String (v.sourceIndex) : juce::String ("-")); // piste:zone
        if (z != nullptr)
        {
            r.trigger = text (z->onRelease ? "trig.release" : z->trigger == neptune::core::TriggerCondition::First ? "trig.first"
                                                          : z->trigger == neptune::core::TriggerCondition::Legato ? "trig.legato" : "trig.attack");
            r.group = juce::String::fromUTF8 (z->group.c_str());
            r.choke = z->chokeCuts == 0 && z->chokeCutBy == 0 ? juce::String()
                    : z->chokeCuts == z->chokeCutBy ? juce::String (z->chokeCuts)
                                                    : juce::String (z->chokeCuts) + ">" + juce::String (z->chokeCutBy);
        }
        r.finished = ! s.present;
        r.fading = v.fading;
        r.releasing = v.releasing;
        r.preview = v.preview;
        r.state = text (r.finished ? "voices.state_finished" : v.preview ? "voices.state_preview" : v.fading ? "voices.state_fading"
                                  : v.releasing ? "voices.state_release" : "voices.state_held");
        r.layerGain = juce::String (v.layerGain, 2);
        r.level = juce::String (v.level, 2);
        r.progress = v.progress;
        r.source = text (v.streaming ? "voices.source_disk" : "voices.source_ram");
        if (s.present)
        {
            if (v.fading) ++fading; else if (v.releasing) ++releasing; else ++held;
        }
        rows.push_back (std::move (r));
    }
    const int total = held + releasing + fading;
    const auto summary = juce::String::fromUTF8 (tr_.format ("voices.summary", { std::to_string (total), std::to_string (held), std::to_string (releasing),
                                                                                  std::to_string (fading), std::to_string (processor_.streamUnderruns()) }).c_str());
    voicesView_->setRows (std::move (rows), summary);
}

// Choke avancé (coupe / est coupée par) des zones sélectionnées.
void NeptuneEditor::openChokePanel (juce::Component& anchor)
{
    const auto d = processor_.instrumentDescription();
    const auto selection = zoneTable_->selectedRows();
    std::size_t first = 0;
    if (! selection.empty() && selection.front() >= 0 && selection.front() < static_cast<int> (d.zones.size()))
        first = static_cast<std::size_t> (selection.front());
    if (d.zones.empty()) return;
    auto panel = std::make_unique<ChokePanel> (palette_, tr_, d.zones[first].chokeCuts, d.zones[first].chokeCutBy);
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    panel->onUndo = [safe] { if (safe != nullptr) safe->processor_.uiUndo(); };
    panel->onRedo = [safe] { if (safe != nullptr) safe->processor_.uiRedo(); };
    panel->onChange = [safe] (int cuts, int cutBy)
    {
        if (safe == nullptr) return;
        safe->editZone ([cuts, cutBy] (neptune::core::ZoneDescription& z) { z.chokeCuts = cuts; z.chokeCutBy = cutBy; });
    };
    chokePanel_ = panel.get();
    juce::Component::SafePointer<ChokePanel> focus (panel.get());
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
    juce::MessageManager::callAsync ([focus] { if (focus != nullptr) focus->grabKeyboardFocus(); });
}

// Sélection demandée par la matrice : elle passe par la liste (source de vérité de la sélection de l'éditeur).
void NeptuneEditor::setZoneSelection (const std::vector<int>& zones)
{
    zoneTable_->setSelectedRows (zones, /*notify*/ false);
    updateSamplerControls();
}

void NeptuneEditor::removeSelectedZones()
{
    auto selection = zoneTable_->selectedRows();
    if (selection.empty()) return;
    auto d = processor_.instrumentDescription();
    d.removeZones (selection); // les emplacements assignables qui visaient ces zones sont libérés, les autres suivent le décalage
    processor_.uiEditInstrument (std::move (d), "Remove zones");
    lastRevision_ = 0xFFFFFFFFu;
    updateSamplerControls();
}

juce::String NeptuneEditor::statusText() const
{
    auto s = baseStatusText();
    if (cachedDescription_.streamFromDisk)
        s += juce::String::fromUTF8 (tr_.format ("sampler.stream_status",
                                                 { std::to_string (processor_.streamUnderruns()) }).c_str());
    return s;
}

juce::String NeptuneEditor::baseStatusText() const
{
    using Kind = NeptuneProcessor::InstrumentStatus::Kind;
    const auto st = processor_.instrumentStatus();
    switch (st.kind)
    {
        case Kind::None: return text ("sampler.none");
        case Kind::Loading:
            if (st.total > 0)
                return juce::String::fromUTF8 (tr_.format ("sampler.loading_progress", { std::to_string (st.done), std::to_string (st.total) }).c_str());
            return text ("sampler.loading");
        case Kind::Error: return juce::String::fromUTF8 (tr_.format ("sampler.error", { st.error }).c_str());
        case Kind::Loaded:
        {
            if (st.zones > 1)
                return juce::String::fromUTF8 (tr_.format ("sampler.loaded_many", { std::to_string (st.zones), std::to_string (st.files),
                                                                                       juce::String (st.megabytes, 1).toStdString() }).c_str());
            const std::string name = st.name;
            const std::string ch (tr_.tr (st.channels == 1 ? "sampler.mono" : "sampler.stereo"));
            const auto rate = juce::String (st.sampleRate, 0).toStdString();
            const auto secs = juce::String (st.seconds, 2).toStdString();
            const auto bits = std::to_string (st.bitDepth);
            const auto mb = juce::String (st.megabytes, 1).toStdString();
            return juce::String::fromUTF8 (tr_.format ("sampler.loaded", { name, ch, rate, secs, bits, mb }).c_str());
        }
    }
    return {};
}

void NeptuneEditor::updateSamplerControls()
{
    const auto rev = processor_.instrumentRevision();
    if (rev != lastRevision_)
    {
        lastRevision_ = rev;
        cachedDescription_ = processor_.instrumentDescription();
        zoneTable_->setZones (cachedDescription_.zones);
        matrix_->setDescription (cachedDescription_);
        rebuildLoadPanel();
    }
    matrix_->setSelection (zoneTable_->selectedRows());
    if (chokePanel_ != nullptr && ! cachedDescription_.zones.empty())
    {
        const auto sel = zoneTable_->selectedRows();
        const std::size_t f = (! sel.empty() && sel.front() >= 0 && sel.front() < static_cast<int> (cachedDescription_.zones.size())) ? static_cast<std::size_t> (sel.front()) : 0;
        chokePanel_->setValues (cachedDescription_.zones[f].chokeCuts, cachedDescription_.zones[f].chokeCutBy);
    }
    if (polyphonyPanel_ != nullptr)
    {
        PolyphonyPanel::Settings ps;
        const auto& cd = cachedDescription_;
        ps.mode = cd.polyphony; ps.maxVoices = cd.maxVoices; ps.steal = cd.stealMode; ps.sameKey = cd.sameKey;
        ps.cutFadeMs = cd.cutFadeMs; ps.glideMs = cd.glideMs; ps.chokeSameZone = cd.chokeSameZone;
        polyphonyPanel_->setValues (ps);
    }
    if (velocityPanel_ != nullptr) // annuler/rétablir pendant que la fenêtre est ouverte : elle suit
        velocityPanel_->setValues (cachedDescription_.velocityFadeWidth, cachedDescription_.velocityFadeLinear, cachedDescription_.velocityCurve);
    const auto& d = cachedDescription_;
    const auto selection = zoneTable_->selectedRows();
    const std::size_t shown = (! selection.empty() && selection.front() >= 0 && selection.front() < static_cast<int> (d.zones.size()))
                                  ? static_cast<std::size_t> (selection.front()) : 0;
    const neptune::core::ZoneDescription z = d.zones.empty() ? neptune::core::ZoneDescription {} : d.zones[shown];
    const auto modeName = tr_.tr (z.mode == neptune::core::PlayMode::Loop ? "sampler.mode_loop" : "sampler.mode_one_shot");
    modeButton_.setButtonText (juce::String::fromUTF8 (tr_.format ("sampler.mode", { std::string (modeName) }).c_str()));

    updatingControls_ = true;
    if (! rootSlider_.isMouseButtonDown()) rootSlider_.setValue (z.rootNote, juce::dontSendNotification);
    triggerBox_.setSelectedId (z.onRelease ? 2 : z.trigger == neptune::core::TriggerCondition::First ? 3
                                                  : z.trigger == neptune::core::TriggerCondition::Legato ? 4 : 1, juce::dontSendNotification);
    if (z.chokeCuts == z.chokeCutBy) chokeBox_.setSelectedId (z.chokeCuts + 1, juce::dontSendNotification);
    else chokeBox_.setSelectedId (0, juce::dontSendNotification); // réglage avancé : le texte « Avancé » s'affiche
    triggerBox_.setEnabled (! d.zones.empty());
    chokeBox_.setEnabled (! d.zones.empty());
    chokeMoreButton_.setEnabled (! d.zones.empty());
    updatingControls_ = false;

    statusLabel_.setText (statusText(), juce::dontSendNotification);
    const bool has = ! d.zones.empty();
    modeButton_.setEnabled (has);
    rootSlider_.setEnabled (has);
    removeButton_.setEnabled (! selection.empty());
    envelopeEditor_->setEnabled (has);
    envelopeEditor_->setEnvelope (z.envelope);
    selectionLabel_.setText (selection.empty() ? text ("sampler.selection_all")
                                               : juce::String::fromUTF8 (tr_.format ("sampler.selection_n", { std::to_string (selection.size()) }).c_str()),
                             juce::dontSendNotification);
}

void NeptuneEditor::rebuildLoadPanel()
{
    const auto& d = cachedDescription_;
    // Mémoire exacte lue dans l'en-tête des fichiers, selon ce que le mode (tout RAM / streaming) garde en mémoire.
    auto sizeOf = [this, &d] (const neptune::core::ZoneDescription& z) -> std::uint64_t
    {
        const auto& info = fileInfo_.info (z.samplePath);
        if (! info.ok) return 0;
        const auto n = neptune::core::residentFramesNeeded (d, neptune::core::sanitised (z), info.channels, info.keptAsInt16, info.frames);
        return static_cast<std::uint64_t> (n) * static_cast<std::uint64_t> (info.channels) * (info.keptAsInt16 ? 2u : 4u);
    };
    // Ce que chaque case commande, en Mo (mémoire réelle après décodage).
    std::uint64_t alternates = 0, release = 0;
    std::map<std::string, std::uint64_t> groups;
    {
        auto all = d;
        all.mainRoundRobinOnly = true; all.loadReleaseSamples = true; all.unloadedGroups.clear();
        const auto mask = neptune::core::loadedZones (all); // avec « principal seulement » : ce qui reste est le principal
        for (std::size_t i = 0; i < d.zones.size(); ++i)
        {
            const auto& z = d.zones[i];
            const auto bytes = sizeOf (z);
            if (z.onRelease) release += bytes;
            else if (! z.group.empty()) groups[z.group] += bytes;
            if (! mask[i] && ! z.onRelease && z.group.empty()) alternates += bytes;
        }
    }
    auto mb = [] (std::uint64_t b) { return juce::String (static_cast<double> (b) / (1024.0 * 1024.0), 0).toStdString(); };

    std::string signature = std::to_string (alternates) + "/" + std::to_string (release);
    for (const auto& [g, b] : groups) signature += "|" + g + ":" + std::to_string (b);
    for (const auto& u : d.unloadedGroups) signature += "!" + u;
    const bool has = ! d.zones.empty();

    rrToggle_.setButtonText (juce::String::fromUTF8 (tr_.format ("load.round_robin", { mb (alternates) }).c_str()));
    releaseToggle_.setButtonText (juce::String::fromUTF8 (tr_.format ("load.release", { mb (release) }).c_str()));
    rrToggle_.setToggleState (! d.mainRoundRobinOnly, juce::dontSendNotification);
    releaseToggle_.setToggleState (d.loadReleaseSamples, juce::dontSendNotification);
    rrToggle_.setEnabled (has);
    releaseToggle_.setEnabled (has);
    streamToggle_.setButtonText (text ("load.stream"));
    updatingControls_ = true;
    streamToggle_.setToggleState (d.streamFromDisk, juce::dontSendNotification);
    preloadBox_.setSelectedId (d.preloadKB, juce::dontSendNotification);
    if (preloadBox_.getSelectedId() != d.preloadKB) // valeur hors liste (fichier édité à la main) : affichée telle quelle
        preloadBox_.setText (juce::String (d.preloadKB) + " KB", juce::dontSendNotification);
    updatingControls_ = false;
    streamToggle_.setEnabled (has);
    preloadBox_.setEnabled (has && d.streamFromDisk);
    preloadLabel_.setEnabled (has && d.streamFromDisk);

    // Une case par Groupe nommé (articulation, pédale, micro...) : reconstruites seulement si elles changent.
    if (signature != groupsSignature_)
    {
        groupsSignature_ = signature;
        groupToggles_.clear();
        groupNames_.clear();
        for (const auto& [name, bytes] : groups)
        {
            groupNames_.push_back (name);
            auto* t = groupToggles_.add (new juce::ToggleButton());
            t->setButtonText (juce::String::fromUTF8 (tr_.format ("load.group", { name, mb (bytes) }).c_str()));
            t->setColour (juce::ToggleButton::textColourId, colour (Token::Text));
            t->setToggleState (std::find (d.unloadedGroups.begin(), d.unloadedGroups.end(), name) == d.unloadedGroups.end(),
                               juce::dontSendNotification);
            t->onClick = [this, name, t]
            {
                auto desc = processor_.instrumentDescription();
                auto& u = desc.unloadedGroups;
                u.erase (std::remove (u.begin(), u.end(), name), u.end());
                if (! t->getToggleState()) u.push_back (name); // décoché = laissé sur le disque
                processor_.uiEditInstrument (std::move (desc), "Load group");
            };
            t->addMouseListener (groupMenuListener_.get(), false);
            groupsHolder_.addAndMakeVisible (t);
        }
        const int w = juce::jmax (200, groupsViewport_.getWidth() - 20);
        for (int i = 0; i < groupToggles_.size(); ++i) groupToggles_[i]->setBounds (0, i * 28, w, 28);
        groupsHolder_.setSize (w, juce::jmax (1, groupToggles_.size()) * 28);
    }
}

void NeptuneEditor::updateViewVisibility()
{
    // Quatre vues : Mixeur, Mapping (tout l'instrument d'une piste Sampler : matrice, liste, enveloppe, chargement, potards),
    // Synthé (le patch d'une piste Synthé, à la place du Mapping) et Sonde (diagnostic de l'hôte).
    const bool mapping = ! showProbe_ && ! showMixer_;
    const bool synth = mapping && selectedIsSynth();
    const bool fmMode = mapping && selectedIsFm();
    const bool organMode = mapping && selectedIsOrgan();
    const bool sampler = mapping && ! synth && ! fmMode && ! organMode;
    synthShown_ = synth;
    fmShown_ = fmMode;
    organShown_ = organMode;
    mixerView_->setVisible (showMixer_);
    synthView_->setVisible (synth);
    fmView_->setVisible (fmMode);
    organView_->setVisible (organMode);
    presetBar_->setVisible (synth || fmMode || organMode);
    trackBox_.setVisible (! showProbe_);
    loadButton_.setVisible (sampler);
    modeButton_.setVisible (sampler);
    statusLabel_.setVisible (sampler);
    rootLabel_.setVisible (sampler);
    rootSlider_.setVisible (sampler);
    envelopeEditor_->setVisible (sampler && ! showVoices_);
    voicesView_->setVisible (sampler && showVoices_);
    voicesButton_.setVisible (sampler);
    filtersButton_.setVisible (sampler);
    slotsButton_.setVisible (sampler);
    importButton_.setVisible (sampler);
    removeButton_.setVisible (sampler);
    selectionLabel_.setVisible (sampler);
    zoneTable_->setVisible (sampler && listVisible_);
    listToggleButton_.setVisible (sampler);
    triggerLabel_.setVisible (sampler);
    triggerBox_.setVisible (sampler);
    chokeLabel_.setVisible (sampler);
    chokeBox_.setVisible (sampler);
    chokeMoreButton_.setVisible (sampler);
    matrix_->setVisible (sampler);
    loadTitle_.setVisible (sampler);
    rrToggle_.setVisible (sampler);
    releaseToggle_.setVisible (sampler);
    streamToggle_.setVisible (sampler);
    preloadLabel_.setVisible (sampler);
    preloadBox_.setVisible (sampler);
    groupsViewport_.setVisible (sampler);
    // gain et accordage valent pour les deux types ; les filtres du synthé se règlent dans sa page « Filtres »
    int k = 0;
    for (auto* knob : knobs_)
    {
        const bool filterKnob = k >= static_cast<int> (neptune::core::mixer::kFilter1Cutoff);
        knob->setVisible (mapping && ! organMode && ! ((synth || fmMode) && filterKnob)); // le gain de l'orgue est dans sa console
        ++k;
    }
    mixerButton_.setToggleState (showMixer_, juce::dontSendNotification);
    mappingButton_.setToggleState (mapping, juce::dontSendNotification);
    viewButton_.setToggleState (showProbe_, juce::dontSendNotification);
}

void NeptuneEditor::updateSynthView()
{
    if (synthView_->isVisible()) synthView_->setDescription (processor_.synthDescription());
}

void NeptuneEditor::setView (bool mixer, bool probe)
{
    showMixer_ = mixer;
    showProbe_ = probe;
    updateViewVisibility();
    resized();
    repaint();
}

// Piste sélectionnée : la liste déroulante du haut et les potards d'instrument (gain, accordage, filtres) la suivent.
void NeptuneEditor::updateTrackBox()
{
    const auto rev = processor_.instrumentRevision();
    if (rev == trackBoxRevision_ && trackBox_.getSelectedId() == processor_.selectedTrack() + 1) return;
    trackBoxRevision_ = rev;
    updatingControls_ = true;
    trackBox_.clear (juce::dontSendNotification);
    for (int t = 0; t < neptune::core::mixer::numTracks; ++t) // seulement les pistes actives
    {
        if (! processor_.trackActive (t)) continue;
        const auto s = processor_.trackSettings (t);
        const auto tt = processor_.trackType (t);
        const auto kind = tt == neptune::core::TrackType::Organ ? "mix.type_short_organ" : tt == neptune::core::TrackType::Fm ? "mix.type_short_fm"
                        : tt == neptune::core::TrackType::Synth ? "mix.type_short_synth" : "mix.type_short_sampler";
        const auto label = juce::String (t + 1) + "  " + text (kind) + (s.name.empty() ? juce::String() : "  " + juce::String::fromUTF8 (s.name.c_str()));
        trackBox_.addItem (label, t + 1);
    }
    trackBox_.setSelectedId (processor_.selectedTrack() + 1, juce::dontSendNotification);
    updatingControls_ = false;
}

void NeptuneEditor::rebindKnobs()
{
    const int track = processor_.selectedTrack();
    if (track == knobTrack_) return;
    knobTrack_ = track;
    synthView_->setTrack (track);
    fmView_->setTrack (track);
    organView_->setTrack (track);
    organView_->releaseKeys();
    fmView_->releaseKeys();
    synthView_->releaseKeys(); // touches tenues à l'écran : relâchées sur la piste où elles ont démarré
    zoneTable_->setSelectedRows ({}, false); // la sélection de zones appartenait à l'instrument de l'ancienne piste
    int k = 0;
    for (auto* knob : knobs_)
    {
        knob->setParamId (neptune::core::mixer::samplerParam (track, static_cast<neptune::core::mixer::SamplerKind> (k)));
        ++k;
    }
    repaint();
}

void NeptuneEditor::updateUndoButtons()
{
    undoButton_.setEnabled (processor_.undoManager().canUndo());
    redoButton_.setEnabled (processor_.undoManager().canRedo());
}

bool NeptuneEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z')
    {
        if (m.isShiftDown()) processor_.uiRedo(); else processor_.uiUndo();
        return true;
    }
    if (code == 'Y' || code == 'y')
    {
        processor_.uiRedo();
        return true;
    }
    if ((code == 'S' || code == 's') && presetBar_->isVisible())
    {
        presetBar_->quickSave();
        return true;
    }
    return false;
}

// Contrôleurs MIDI : désactivés par défaut. L'interrupteur et les assignations sont enregistrés avec le projet.
void NeptuneEditor::showControlMenu()
{
    const bool on = processor_.ccEnabled();
    const auto cfg = processor_.controlConfig();
    const bool organ = processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Organ;
    juce::PopupMenu menu;
    menu.addItem (1, text ("control.enable"), true, on);
    menu.addSeparator();
    menu.addItem (2, text ("control.assign_organ"), organ);
    menu.addItem (3, text ("control.clear") + " (" + juce::String (static_cast<int> (cfg.bindings.size())) + ")", ! cfg.bindings.empty());
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&controlButton_), [safe, on] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->processor_.uiSetCcEnabled (! on);
        else if (choice == 2)
        {
            safe->processor_.uiAssignOrganControllers();
            safe->processor_.uiSetCcEnabled (true); // assigner sans activer n'aurait pas d'effet audible : on active
        }
        else if (choice == 3) safe->processor_.uiClearControllers();
    });
}

// Serveur OSC : désactivé par défaut, enregistré avec le projet. Écoute sur cet ordinateur seulement, sauf case « réseau local ».
void NeptuneEditor::showOscMenu()
{
    const bool on = processor_.oscEnabled();
    const auto settings = processor_.controlConfig().settings;
    const int port = processor_.oscPort();
    juce::PopupMenu menu;
    menu.addItem (1, text ("osc.enable"), true, on);
    menu.addItem (2, text ("osc.network"), true, settings.oscAllInterfaces);
    if (on)
    {
        menu.addSeparator();
        const auto status = port > 0 ? tr_.format ("osc.listening", { std::to_string (port) }) : tr_.format ("osc.busy", { std::to_string (settings.oscPort) });
        menu.addItem (3, juce::String::fromUTF8 (status.c_str()), false, false);
    }
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&oscButton_), [safe, on, settings] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->processor_.uiSetOscEnabled (! on);
        else if (choice == 2) safe->processor_.uiSetOscAllInterfaces (! settings.oscAllInterfaces);
        safe->updateProfileControls();
    });
}

void NeptuneEditor::updateProfileControls()
{
    using neptune::core::PerformanceProfile;
    auto& p = processor_.profile();
    auto name = [this] (PerformanceProfile v) { return std::string (tr_.tr (v == PerformanceProfile::Live ? "profile.live" : "profile.studio")); };
    const auto locked = p.locked();
    profileLabel_.setText (juce::String::fromUTF8 (tr_.format ("profile.status",
                               { name (p.shared().get()), name (p.effective()), locked ? tr_.tr ("profile.locked_suffix") : std::string_view() }).c_str()),
                           juce::dontSendNotification);
    profileLockButton_.setButtonText (text (locked ? "profile.unlock" : "profile.lock"));
    controlButton_.setToggleState (processor_.ccEnabled(), juce::dontSendNotification); // allumé : les CC agissent
    oscButton_.setToggleState (processor_.oscEnabled(), juce::dontSendNotification);     // allumé : le serveur OSC est demandé
}

void NeptuneEditor::timerCallback()
{
    for (int t = 0, k = 0, v = 0; processor_.popNoteTrace (t, k, v);)
        if (t == processor_.selectedTrack()) matrix_->notePlayed (k, v); // curseur de la matrice (piste sélectionnée)
    updateTrackBox();
    rebindKnobs();
    if (synthShown_ != (! showProbe_ && ! showMixer_ && selectedIsSynth())
        || fmShown_ != (! showProbe_ && ! showMixer_ && selectedIsFm())
        || organShown_ != (! showProbe_ && ! showMixer_ && selectedIsOrgan())) { updateViewVisibility(); resized(); } // type de la piste changé
    updateSynthView();
    if (fmView_->isVisible()) fmView_->setDescription (processor_.fmDescription());
    if (organView_->isVisible())
    {
        organView_->setDescription (processor_.organDescription());
        organView_->refreshFromParameters();
    }
    if (presetBar_->isVisible() && presetBar_->needsRefresh()) presetBar_->refresh();
    {
        const int tr = processor_.selectedTrack();
        synthView_->setHeldKeys (processor_.heldKeyWord (tr, 0), processor_.heldKeyWord (tr, 1)); // notes MIDI reçues
        organView_->setHeldKeys (processor_.heldKeyWord (tr, 0), processor_.heldKeyWord (tr, 1));
        fmView_->setHeldKeys (processor_.heldKeyWord (tr, 0), processor_.heldKeyWord (tr, 1));
    }
    updateRecoveryBanner();
    updateProfileControls();
    updateUndoButtons();
    updateSamplerControls();
    updateVoices();
    {
        // compteur de voix actives, toutes pistes confondues (Sampler et Synthé)
        const int voices = processor_.totalActiveVoices();
        if (voices != lastVoiceCount_)
        {
            lastVoiceCount_ = voices;
            voiceCountButton_.setButtonText (text ("analog.voices") + " " + juce::String (voices) + " / " + juce::String (processor_.masterSettings().maxVoices));
        }
    }
    updateFilterEditor();
    repaint();
}

void NeptuneEditor::updateRecoveryBanner()
{
    const bool show = processor_.hasRecoverableState();
    if (show)
        recoveryLabel_.setText (juce::String::fromUTF8 (tr_.format ("recovery.found", { processor_.recoverableDescription().toStdString() }).c_str()),
                                juce::dontSendNotification);
    if (show != recoveryShown_)
    {
        recoveryShown_ = show;
        recoveryLabel_.setVisible (show);
        restoreButton_.setVisible (show);
        discardButton_.setVisible (show);
    }
}

void NeptuneEditor::resized()
{
    // Taille logique d'au moins 1100 x 760 ; en dessous, tout le contenu est réduit d'un même facteur.
    {
        const auto logical = root_.fit (getWidth(), getHeight(), 1100, 760);
        lw_ = logical.getWidth();
        lh_ = logical.getHeight();
    }
    auto bottom = rootBounds().reduced (32).removeFromBottom (80);
    auto banner = bottom.removeFromTop (36);
    discardButton_.setBounds (banner.removeFromRight (90).reduced (2));
    restoreButton_.setBounds (banner.removeFromRight (90).reduced (2));
    recoveryLabel_.setBounds (banner);
    if (modal_) modal_->setBounds (rootBounds());

    // Vue Mapping : chargement, import, état, mode, note racine en haut ; à gauche la liste des zones (rétractable) et le
    // panneau « Chargé en mémoire » ; au centre la matrice puis l'enveloppe ; les potards en bas.
    viewButton_.setBounds (lw_ - 32 - 100, 32, 100, 36);
    mappingButton_.setBounds (lw_ - 32 - 100 - 4 - 100, 32, 100, 36);
    mixerButton_.setBounds (lw_ - 32 - 100 - 4 - 100 - 4 - 100, 32, 100, 36);
    panicButton_.setBounds (lw_ - 32 - 100 - 4 - 100 - 4 - 100 - 8 - 130, 32, 130, 36);
    trackBox_.setBounds (190, 32, 180, 36);
    mixerView_->placeScaled ({ 32, 80, lw_ - 64, juce::jmax (200, lh_ - 80 - 150) });
    voiceCountButton_.setBounds (376, 32, 150, 36);
    midiFilterButton_.setBounds (530, 32, 44, 36);
    controlButton_.setBounds (578, 32, 44, 36);
    oscButton_.setBounds (626, 32, 44, 36);
    if (midiFilterEditor_) midiFilterEditor_->setBounds (rootBounds());
    presetBar_->setBounds (32, 80, juce::jmin (640, lw_ - 64), 34);
    synthView_->placeScaled ({ 32, 80 + 38, lw_ - 64, juce::jmax (200, lh_ - 270 - 12 - 80 - 38) });
    fmView_->placeScaled ({ 32, 80 + 38, lw_ - 64, juce::jmax (200, lh_ - 270 - 12 - 80 - 38) });
    organView_->placeScaled ({ 32, 80 + 38, lw_ - 64, juce::jmax (200, lh_ - 80 - 38 - 160) });
    if (presetManager_) presetManager_->setBounds (rootBounds());
    {
        auto r = juce::Rectangle<int> (32, 80, lw_ - 64, 36);
        loadButton_.setBounds (r.removeFromLeft (170).reduced (2));
        importButton_.setBounds (r.removeFromLeft (130).reduced (2));
        modeButton_.setBounds (r.removeFromLeft (190).reduced (2));
        listToggleButton_.setBounds (r.removeFromLeft (150).reduced (2));
        voicesButton_.setBounds (r.removeFromLeft (140).reduced (2));
        filtersButton_.setBounds (r.removeFromLeft (110).reduced (2));
        slotsButton_.setBounds (r.removeFromLeft (110).reduced (2));
        statusLabel_.setBounds (r);
        auto r2 = juce::Rectangle<int> (32, 122, lw_ - 64, 36);
        rootLabel_.setBounds (r2.removeFromLeft (200));
        rootSlider_.setBounds (r2.removeFromLeft (150).reduced (2));
        removeButton_.setBounds (r2.removeFromLeft (150).reduced (2));
        triggerLabel_.setBounds (r2.removeFromLeft (100));
        triggerBox_.setBounds (r2.removeFromLeft (130).reduced (2));
        chokeLabel_.setBounds (r2.removeFromLeft (56));
        chokeBox_.setBounds (r2.removeFromLeft (100).reduced (2));
        chokeMoreButton_.setBounds (r2.removeFromLeft (40).reduced (2));
        selectionLabel_.setBounds (r2);

        const int top = 168;
        const int bottom = lh_ - 270 - 12;
        const int h = juce::jmax (160, bottom - top);
        const int leftW = juce::jmin (400, (lw_ - 64) / 3);
        const int panelH = 192;
        const int tableH = juce::jmax (80, h - panelH - 6);
        if (listVisible_) zoneTable_->setBounds (32, top, leftW, tableH);
        auto lp = juce::Rectangle<int> (32, listVisible_ ? top + tableH + 6 : top, leftW, panelH);
        loadTitle_.setBounds (lp.removeFromTop (24));
        rrToggle_.setBounds (lp.removeFromTop (28));
        releaseToggle_.setBounds (lp.removeFromTop (28));
        {
            auto row = lp.removeFromTop (32);
            streamToggle_.setBounds (row.removeFromLeft (juce::jmin (420, row.getWidth() / 2)));
            preloadLabel_.setBounds (row.removeFromLeft (90));
            preloadBox_.setBounds (row.removeFromLeft (120).reduced (2));
        }
        groupsViewport_.setBounds (lp);
        groupsSignature_.clear(); // largeur changée : les cases sont reconstruites à la prochaine mise à jour

        const int x = 32 + leftW + 12;
        const int w = lw_ - 32 - x;
        const int matrixH = juce::jmax (120, static_cast<int> (h * 0.62));
        matrix_->setBounds (x, top, w, matrixH);
        envelopeEditor_->setBounds (x, top + matrixH + 8, w, juce::jmax (80, h - matrixH - 8));
        voicesView_->setBounds (envelopeEditor_->getBounds());
    }
    if (wizard_) wizard_->setBounds (rootBounds());
    if (filterEditor_) filterEditor_->setBounds (rootBounds());
    if (insertOverlay_) insertOverlay_->setBounds (rootBounds());

    // Rangée de potards au-dessus de la bande de profil : nom (18) + potard (zone >= 44 px) + valeur (18).
    {
        const int knobSide = static_cast<int> (neptune::core::ui::effectiveHitSize (48.0, neptune::core::ui::KnobConfig {}));
        const int top = lh_ - 270;
        int x = 32;
        for (auto* k : knobs_)
        {
            k->setBounds (x + (kKnobCell - knobSide) / 2, top + 20, knobSide, knobSide);
            x += kKnobCell;
        }
        // annuler / rétablir : en bas de la fenêtre (jamais sur le mixeur ni sur le master), deux fois plus petits qu'avant
        const int btnY = lh_ - 28;
        redoButton_.setBounds (lw_ - 32 - 64, btnY, 64, 22);
        undoButton_.setBounds (lw_ - 32 - 64 - 4 - 64, btnY, 64, 22);
    }

    auto profileStrip = rootBounds().reduced (32).removeFromBottom (120).removeFromTop (32);
    profileSwitchButton_.setBounds (profileStrip.removeFromLeft (200).reduced (2));
    profileLockButton_.setBounds (profileStrip.removeFromLeft (200).reduced (2));
    profileLabel_.setBounds (profileStrip);
    auto strip = rootBounds().reduced (32).removeFromBottom (32);
    latencyLabel_.setBounds (strip.removeFromLeft (260));
    for (auto* b : latencyButtons_)
    {
        b->setBounds (strip.removeFromLeft (110).reduced (2));
    }
}

void NeptuneEditor::paintContent (juce::Graphics& g)
{
    using namespace neptune::core;
    g.fillAll (colour (Token::BgApp));

    auto area = rootBounds().reduced (32);
    g.setColour (colour (Token::Text));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Neptune", area.removeFromTop (40), juce::Justification::centredLeft);

    if (showProbe_)
    {
    auto& ps = processor_.parameters();
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String (static_cast<int> (ps.size())) + " parameters exposed to the host ("
                    + juce::String (processor_.probeTracks()) + " probe tracks)",
                area.removeFromTop (24), juce::Justification::centredLeft);
    area.removeFromTop (12);

    struct Row { const char* label; ParamId id; };
    const Row rows[] = {
        { "Master Volume",       probe::masterVolume },
        { "Master Pan",          probe::masterPan },
        { "Master Mute",         probe::masterMute },
        { "FM Algorithm (1-32)", probe::algorithm },
        { "Track 1 Volume",      probe::trackParam (0, probe::kVolume) },
    };

    g.setFont (juce::FontOptions (16.0f));
    for (const auto& r : rows)
    {
        auto line = area.removeFromTop (28);
        g.setColour (colour (Token::TextDim));
        g.drawText (r.label, line.removeFromLeft (260), juce::Justification::centredLeft);
        g.setColour (colour (Token::Accent));
        g.drawText (juce::String::fromUTF8 (ps.toText (r.id).c_str()), line.removeFromLeft (160), juce::Justification::centredLeft);
        g.setColour (colour (Token::TextDim2));
        g.drawText ("changes: " + juce::String (static_cast<int> (ps.version (r.id))), line, juce::Justification::centredLeft);
    }

    // --- Ce que l'hôte nous envoie ---
    const auto& hp = processor_.hostProbe();
    area.removeFromTop (14);
    g.setColour (colour (Token::Text));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Host probe", area.removeFromTop (26), juce::Justification::centredLeft);

    static const char* const exprNames[] = { "volume", "pan", "tuning", "vibrato", "expression", "brightness", "pressure" };
    juce::String expr;
    for (int i = 0; i < HostProbe::numExpressions; ++i)
        expr << exprNames[i] << " " << hp.expressionCount[static_cast<size_t> (i)].load() << " ("
             << juce::String (hp.expressionLast[static_cast<size_t> (i)].load(), 3) << ")   ";

    const juce::String lines[] = {
        "Outputs: " + juce::String (processor_.probeOutputs()) + " stereo buses declared; host buffer channels: "
            + juce::String (hp.outputChannels.load()) + "; blocks: " + juce::String (hp.blocks.load())
            + "; block size: " + juce::String (hp.lastBlockSize.load()),
        "Transport: " + juce::String (hp.hasTransport.load() ? "available" : "none")
            + "; playing: " + juce::String (hp.playing.load() ? "yes" : "no")
            + "; tempo: " + juce::String (hp.bpm.load(), 2) + " bpm; position: " + juce::String (hp.ppq.load(), 3)
            + " beats; signature: " + juce::String (hp.timeSigNum.load()) + "/" + juce::String (hp.timeSigDen.load()),
        "Notes: MIDI note-ons " + juce::String (hp.midiNoteOns.load()) + "; CLAP note-ons " + juce::String (hp.clapNoteOns.load())
            + "; CLAP note-offs " + juce::String (hp.clapNoteOffs.load()) + "; NOTE_END sent " + juce::String (hp.noteEndsSent.load())
            + "; last note_id " + juce::String (hp.lastNoteId.load()) + " key " + juce::String (hp.lastKey.load())
            + "  (note_id -2: none yet, -1: host gave none)",
        "Note expressions: " + expr,
        "Parameter modulation: mono " + juce::String (hp.monoModCount.load()) + "; poly " + juce::String (hp.polyModCount.load())
            + "; last amount " + juce::String (hp.lastModAmount.load(), 3) + "; last poly note_id " + juce::String (hp.lastPolyNoteId.load()),
    };
    g.setFont (juce::FontOptions (14.0f));
    for (const auto& l : lines)
    {
        g.setColour (colour (Token::Accent));
        g.drawText (l, area.removeFromTop (24), juce::Justification::centredLeft);
    }

    {
        const auto& st = processor_.settings();
        g.setColour (colour (Token::TextDim2));
        g.drawText ("Settings (" + processor_.settingsFilePath() + "): language " + st.language + ", theme " + st.theme
                        + ", contrast " + juce::String (st.contrast) + ", profile "
                        + (st.profile == neptune::core::settings::PerformanceProfile::Live ? "live" : "studio")
                        + ", autosave " + (st.autosaveIntervalSeconds > 0 ? juce::String (st.autosaveIntervalSeconds) + " s" : juce::String ("off")),
                    area.removeFromTop (24), juce::Justification::centredLeft);
    }

    } // showProbe_

    // Nom et valeur de chaque potard (vue Mapping)
    if (! showProbe_ && ! showMixer_)
    {
        const auto& ps2 = processor_.parameters();
        g.setFont (juce::FontOptions (13.0f));
        for (auto* k : knobs_)
        {
            if (! k->isVisible()) continue;
            const auto b = k->getBounds();
            const auto cell = b.withX (b.getCentreX() - kKnobCell / 2).withWidth (kKnobCell);
            g.setColour (colour (Token::TextDim));
            g.drawText (juce::String::fromUTF8 (ps2.find (k->paramId())->name.c_str()), cell.withY (b.getY() - 20).withHeight (18),
                        juce::Justification::centred);
            g.setColour (colour (Token::Accent));
            g.drawText (juce::String::fromUTF8 (ps2.toText (k->paramId()).c_str()), cell.withY (b.getBottom() + 2).withHeight (18),
                        juce::Justification::centred);
        }
    }

    latencyLabel_.setText (juce::String::fromUTF8 (tr_.format ("latency.reported", { std::to_string (processor_.getLatencySamples()) }).c_str()),
                           juce::dontSendNotification);
}

void NeptuneEditor::paint (juce::Graphics& g) { g.fillAll (colour (neptune::core::theme::Token::BgApp)); }
