#include "PluginEditor.h"
#include "VelocityPanel.h"
#include "PolyphonyPanel.h"
#include "ChokePanel.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

using neptune::core::theme::Token;

namespace
{
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

NeptuneEditor::NeptuneEditor (NeptuneProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language), palette_ (makePalette (p.settings()))
{
    setWantsKeyboardFocus (true);

    {
        using namespace neptune::core;
        const auto& def = theme::definition (theme::themeFromString (processor_.settings().theme).value_or (theme::ThemeId::Dark));
        for (const ParamId id : { sampler::gain, sampler::tuneSemitones, sampler::tuneCents })
            addAndMakeVisible (knobs_.add (new NeptuneKnob (processor_, id, palette_, def.knob,
                                                            [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); })));
    }
    loadButton_.setButtonText (text ("sampler.load"));
    loadButton_.onClick = [this] { chooseSample(); };
    addAndMakeVisible (loadButton_);
    statusLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    addAndMakeVisible (statusLabel_);

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
    matrix_->onOpenVelocity = [this] (juce::Component& anchor) { openVelocityPanel (anchor); };
    matrix_->onOpenPolyphony = [this] (juce::Component& anchor) { openPolyphonyPanel (anchor); };
    addChildComponent (*matrix_);
    listToggleButton_.setButtonText (text ("map.list"));
    listToggleButton_.setClickingTogglesState (true);
    listToggleButton_.setToggleState (true, juce::dontSendNotification);
    listToggleButton_.onClick = [this] { listVisible_ = listToggleButton_.getToggleState(); updateViewVisibility(); resized(); };
    addAndMakeVisible (listToggleButton_);

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

    viewButton_.onClick = [this] { showProbe_ = ! showProbe_; updateViewVisibility(); resized(); repaint(); };
    addAndMakeVisible (viewButton_);

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
    setResizable (true, true);
    setSize (1160, 920);
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
    addAndMakeVisible (*modal_);
    modal_->setBounds (getLocalBounds());
    modal_->toFront (true);
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
    addAndMakeVisible (*wizard_);
    wizard_->setBounds (getLocalBounds());
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
    if (r.replace) d.zones.clear();
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
    std::sort (selection.begin(), selection.end(), std::greater<int>());
    auto d = processor_.instrumentDescription();
    for (const int row : selection)
        if (row >= 0 && row < static_cast<int> (d.zones.size())) d.zones.erase (d.zones.begin() + row);
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
        for (const auto& [name, bytes] : groups)
        {
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
            groupsHolder_.addAndMakeVisible (t);
        }
        const int w = juce::jmax (200, groupsViewport_.getWidth() - 20);
        for (int i = 0; i < groupToggles_.size(); ++i) groupToggles_[i]->setBounds (0, i * 28, w, 28);
        groupsHolder_.setSize (w, juce::jmax (1, groupToggles_.size()) * 28);
    }
}

void NeptuneEditor::updateViewVisibility()
{
    // Deux vues : Mapping (tout l'instrument : matrice, liste, enveloppe, chargement, potards) et Sonde (diagnostic de l'hôte).
    const bool mapping = ! showProbe_;
    loadButton_.setVisible (mapping);
    modeButton_.setVisible (mapping);
    statusLabel_.setVisible (mapping);
    rootLabel_.setVisible (mapping);
    rootSlider_.setVisible (mapping);
    envelopeEditor_->setVisible (mapping);
    importButton_.setVisible (mapping);
    removeButton_.setVisible (mapping);
    selectionLabel_.setVisible (mapping);
    zoneTable_->setVisible (mapping && listVisible_);
    listToggleButton_.setVisible (mapping);
    triggerLabel_.setVisible (mapping);
    triggerBox_.setVisible (mapping);
    chokeLabel_.setVisible (mapping);
    chokeBox_.setVisible (mapping);
    chokeMoreButton_.setVisible (mapping);
    matrix_->setVisible (mapping);
    loadTitle_.setVisible (mapping);
    rrToggle_.setVisible (mapping);
    releaseToggle_.setVisible (mapping);
    streamToggle_.setVisible (mapping);
    preloadLabel_.setVisible (mapping);
    preloadBox_.setVisible (mapping);
    groupsViewport_.setVisible (mapping);
    for (auto* k : knobs_) k->setVisible (mapping);
    viewButton_.setButtonText (showProbe_ ? text ("map.tab") : juce::String ("Probe"));
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
    return false;
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
}

void NeptuneEditor::timerCallback()
{
    updateRecoveryBanner();
    updateProfileControls();
    updateUndoButtons();
    updateSamplerControls();
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
    auto bottom = getLocalBounds().reduced (32).removeFromBottom (80);
    auto banner = bottom.removeFromTop (36);
    discardButton_.setBounds (banner.removeFromRight (90).reduced (2));
    restoreButton_.setBounds (banner.removeFromRight (90).reduced (2));
    recoveryLabel_.setBounds (banner);
    if (modal_) modal_->setBounds (getLocalBounds());

    // Vue Mapping : chargement, import, état, mode, note racine en haut ; à gauche la liste des zones (rétractable) et le
    // panneau « Chargé en mémoire » ; au centre la matrice puis l'enveloppe ; les potards en bas.
    viewButton_.setBounds (getWidth() - 32 - 110, 32, 110, 36);
    {
        auto r = juce::Rectangle<int> (32, 80, getWidth() - 64, 36);
        loadButton_.setBounds (r.removeFromLeft (170).reduced (2));
        importButton_.setBounds (r.removeFromLeft (130).reduced (2));
        modeButton_.setBounds (r.removeFromLeft (190).reduced (2));
        listToggleButton_.setBounds (r.removeFromLeft (150).reduced (2));
        statusLabel_.setBounds (r);
        auto r2 = juce::Rectangle<int> (32, 122, getWidth() - 64, 36);
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
        const int bottom = getHeight() - 270 - 12;
        const int h = juce::jmax (160, bottom - top);
        const int leftW = juce::jmin (400, (getWidth() - 64) / 3);
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
        const int w = getWidth() - 32 - x;
        const int matrixH = juce::jmax (120, static_cast<int> (h * 0.62));
        matrix_->setBounds (x, top, w, matrixH);
        envelopeEditor_->setBounds (x, top + matrixH + 8, w, juce::jmax (80, h - matrixH - 8));
    }
    if (wizard_) wizard_->setBounds (getLocalBounds());

    // Rangée de potards au-dessus de la bande de profil : nom (18) + potard (zone >= 44 px) + valeur (18).
    {
        const int knobSide = static_cast<int> (neptune::core::ui::effectiveHitSize (64.0, neptune::core::ui::KnobConfig {}));
        const int top = getHeight() - 270;
        int x = 32;
        for (auto* k : knobs_)
        {
            k->setBounds (x + (140 - knobSide) / 2, top + 20, knobSide, knobSide);
            x += 140;
        }
        const int btnY = top + 20 + (knobSide - 44) / 2;
        redoButton_.setBounds (getWidth() - 32 - 110, btnY, 110, 44);
        undoButton_.setBounds (getWidth() - 32 - 230, btnY, 110, 44);
    }

    auto profileStrip = getLocalBounds().reduced (32).removeFromBottom (120).removeFromTop (32);
    profileSwitchButton_.setBounds (profileStrip.removeFromLeft (200).reduced (2));
    profileLockButton_.setBounds (profileStrip.removeFromLeft (200).reduced (2));
    profileLabel_.setBounds (profileStrip);
    auto strip = getLocalBounds().reduced (32).removeFromBottom (32);
    latencyLabel_.setBounds (strip.removeFromLeft (260));
    for (auto* b : latencyButtons_)
    {
        b->setBounds (strip.removeFromLeft (110).reduced (2));
    }
}

void NeptuneEditor::paint (juce::Graphics& g)
{
    using namespace neptune::core;
    g.fillAll (colour (Token::BgApp));

    auto area = getLocalBounds().reduced (32);
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
    if (! showProbe_)
    {
        const auto& ps2 = processor_.parameters();
        g.setFont (juce::FontOptions (13.0f));
        for (auto* k : knobs_)
        {
            const auto b = k->getBounds();
            const auto cell = b.withX (b.getCentreX() - 70).withWidth (140);
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
