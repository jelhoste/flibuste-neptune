#include "PluginEditor.h"
#include "neptune/core/Version.h"
#include "GlobalPresets.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/Mixer.h"

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

    midiFilterButton_.setButtonText (text ("midi.button"));
    midiFilterButton_.setTooltip (text ("midi.title"));
    midiFilterButton_.onClick = [this] { openMidiFilter(); };
    addAndMakeVisible (midiFilterButton_);
    globalPresetButton_.setButtonText (text ("gpreset.button"));
    globalPresetButton_.setTooltip (text ("gpreset.tip"));
    globalPresetButton_.onClick = [this] { showGlobalPresetMenu(); };
    addAndMakeVisible (globalPresetButton_);
    controlButton_.setButtonText (text ("control.button"));
    controlButton_.setTooltip (text ("control.title"));
    controlButton_.onClick = [this] { showControlMenu(); };
    addAndMakeVisible (controlButton_);
    oscButton_.setButtonText (text ("osc.button"));
    oscButton_.setTooltip (text ("osc.title"));
    oscButton_.onClick = [this] { showOscMenu(); };
    addAndMakeVisible (oscButton_);
    midiSendButton_.setButtonText (text ("midi.send_button"));
    midiSendButton_.setTooltip (text ("midi.send_title"));
    midiSendButton_.onClick = [this] { openMidiSend(); };
    addAndMakeVisible (midiSendButton_);
    voiceCountButton_.setTooltip (text ("voices.global_limit"));
    voiceCountButton_.onClick = [this]
    {
        juce::PopupMenu menu;
        const int current = processor_.masterSettings().maxVoices;
        menu.addSectionHeader (text ("voices.global_limit"));
        for (const int n : { 16, 32, 64, 128, 256, 512, 1024 })
            menu.addItem (n, juce::String (n), true, n == current);
        menu.setLookAndFeel (&voiceCountButton_.getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
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

    panicButton_.setButtonText (text ("panic.button"));
    panicButton_.onClick = [this]
    {
        processor_.uiPanic();
    };
    addAndMakeVisible (panicButton_);

    viewButton_.setButtonText ("Probe");
    viewButton_.onClick = [this] { setView (false, true); };
    addAndMakeVisible (viewButton_);
    mixerButton_.setButtonText (text ("mix.tab"));
    mixerButton_.onClick = [this] { setView (true, false); };
    addAndMakeVisible (mixerButton_);
    mappingButton_.setButtonText (text ("map.tab"));
    mappingButton_.onClick = [this] { setView (false, false); };
    addAndMakeVisible (mappingButton_);
    facadeButton_.setButtonText (text ("facade.tab"));
    facadeButton_.onClick = [this] { setView (false, false, true); };
    addAndMakeVisible (facadeButton_);
    modButton_.setButtonText (text ("mod.tab"));
    modButton_.onClick = [this] { setView (false, false, false, true); };
    addAndMakeVisible (modButton_);
    combiButton_.setButtonText (text ("comb.button"));
    combiButton_.setTooltip (text ("comb.tip"));
    combiButton_.onClick = [this] { openCombinations(); };
    addAndMakeVisible (combiButton_);
    consoleButton_.setButtonText (text ("console.button"));
    consoleButton_.setTooltip (text ("console.tip"));
    consoleButton_.onClick = [this] { showConsoleMenu(); };
    addAndMakeVisible (consoleButton_);
    splitButton_.setButtonText (text ("split.button"));
    splitButton_.setTooltip (text ("split.tip"));
    splitButton_.setClickingTogglesState (false);
    splitButton_.onClick = [this] { if (showSplit_) setView (true, false); else setSplit(); };   // un second clic revient au Mixeur
    addAndMakeVisible (splitButton_);
    splitBar_.onBegin = [this] { splitStartRatio_ = splitRatio_; };
    splitBar_.onDrag = [this] (int dx)
    {
        const double width = juce::jmax (200, lw_ - 64);
        splitRatio_ = juce::jlimit (0.25, 0.75, splitStartRatio_ + dx / width);
        resized();
    };
    addChildComponent (splitBar_);
    unlockButton_.setButtonText (text ("console.unlock"));
    unlockButton_.setTooltip (text ("console.unlock_tip"));
    unlockButton_.onClick = [this] { consoleLocked_ = false; applyConsoleLock(); updateViewVisibility(); resized(); };
    addChildComponent (unlockButton_);
    routerButton_.setButtonText (text ("router.tab"));
    routerButton_.onClick = [this] { setView (false, false, false, false, true); };
    addAndMakeVisible (routerButton_);

    mixerView_ = std::make_unique<MixerView> (processor_, palette_, tr_);
    mixerView_->onSelectTrack = [this] (int t) { processor_.selectTrack (t); };
    mixerView_->onOpenInsert = [this] (NeptuneProcessor::StripKind kind, int index, int slot) { openInsert (kind, index, slot); };
    addChildComponent (*mixerView_);
    facadeView_ = std::make_unique<FacadeView> (processor_, palette_, tr_, neptune::core::theme::definition (neptune::core::theme::themeFromString (p.settings().theme).value_or (neptune::core::theme::ThemeId::Dark)).knob,
                                                [this] (neptune::core::ParamId id, neptune::core::uistate::Terminal t) { openModal (id, t); });
    addChildComponent (*facadeView_);
    modView_ = std::make_unique<ModView> (processor_, palette_, tr_);
    addChildComponent (*modView_);
    routerView_ = std::make_unique<RouterView> (processor_, palette_, tr_);
    addChildComponent (*routerView_);


    {
        audioInView_ = std::make_unique<AudioInView> (processor_, palette_, tr_);
        addChildComponent (*audioInView_);
    }

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
    startTimerHz (30);

    // La taille en dernier : resized() place les composants, qui doivent donc exister. (Fixée avant leur création,
    // la disposition n'était calculée qu'au premier redimensionnement par l'hôte : potards absents sous Linux.)
    if (processor_.consoleMode().enabled)   // mode console : on s'ouvre sur la Façade de la piste choisie, interface verrouillée
    {
        consoleLocked_ = true;
        if (processor_.consoleMode().track >= 0) processor_.selectTrack (processor_.consoleMode().track);
        facadeView_->setGlobal (processor_.consoleMode().track < 0);
        setView (false, false, true);
    }
    applyConsoleLock();
    updateViewVisibility();
    root_.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
    novaLook_.setButtonTextScale (1.5f);
    novaLook_.setPopupScale (1.25f);   // menus de la barre et du mixeur : plus lisibles
    root_.setLookAndFeel (&novaLook_);
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

void NeptuneEditor::openMidiSend()
{
    if (midiSendPanel_ || modal_ || insertOverlay_ || midiFilterEditor_) return;
    MidiMessagePanel::Settings initial;
    initial.message.kind = neptune::core::midisend::Kind::ControlChange;
    initial.message.data1 = 7;
    initial.message.data2 = 100;
    midiSendPanel_ = std::make_unique<MidiMessagePanel> (palette_, tr_, false, initial);
    midiSendPanel_->onSend = [this] (const std::vector<std::uint8_t>& bytes, int targets) { return processor_.postMidi (bytes, targets); };
    midiSendPanel_->onClose = [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->midiSendPanel_.reset();
            safe->grabKeyboardFocus();
        });
    };
    root_.addAndMakeVisible (*midiSendPanel_);
    midiSendPanel_->setBounds (rootBounds());
    midiSendPanel_->toFront (true);
}

void NeptuneEditor::openCombinations()
{
    if (combiPanel_ || modal_ || insertOverlay_ || midiFilterEditor_ || midiSendPanel_) return;
    combiPanel_ = std::make_unique<CombinationsPanel> (processor_, palette_, tr_);
    combiPanel_->onClose = [safe = juce::Component::SafePointer<NeptuneEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->combiPanel_.reset();
            safe->grabKeyboardFocus();
        });
    };
    root_.addAndMakeVisible (*combiPanel_);
    combiPanel_->setBounds (rootBounds());
    combiPanel_->toFront (true);
    combiPanel_->grabKeyboardFocus();
}

void NeptuneEditor::openMidiFilter()
{
    if (midiFilterEditor_ || modal_ || insertOverlay_) return;
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
    modal_->setFacadeAdd ([safe = juce::Component::SafePointer<NeptuneEditor> (this), id] (int type)
    {
        if (safe == nullptr) return;
        safe->facadeView_->addLinked (static_cast<neptune::core::facade::WidgetType> (type), id);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
    });
    root_.addAndMakeVisible (*modal_);
    modal_->setBounds (rootBounds());
    modal_->toFront (true);
}

// Éditeur d'un insert, superposé à toute la fenêtre.
void NeptuneEditor::openInsert (NeptuneProcessor::StripKind kind, int index, int slot)
{
    if (insertOverlay_ || modal_) return;
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

// L'éditeur NOVA de la piste Synthé ou FM sélectionnée occupe la page Edit ; il est reconstruit quand la piste ou son type change.
void NeptuneEditor::updateInstrumentPanel()
{
    const bool wanted = ! showProbe_ && ! showMixer_ && ! showFacade_ && ! showMod_ && ! showRouter_ && ! showSplit_ && ! selectedIsAudioIn();
    if (! wanted) { instrumentPanel_.reset(); return; }
    const int track = processor_.selectedTrack();
    const auto type = processor_.trackType (track);
    if (instrumentPanel_ != nullptr && instrumentPanel_->matches (track, type)) return;
    instrumentPanel_.reset();
    const auto& def = neptune::core::theme::definition (neptune::core::theme::themeFromString (processor_.settings().theme).value_or (neptune::core::theme::ThemeId::Dark));
    auto panel = std::make_unique<InstrumentPanel> (processor_, palette_, tr_, def.knob);
    if (! panel->valid()) return;
    instrumentPanel_ = std::move (panel);
    root_.addAndMakeVisible (*instrumentPanel_);   // dans la racine mise à l'échelle (comme les calques), pas dans la fenêtre : sinon elle passe sous la racine
    instrumentPanel_->toBack();
    resized();
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

void NeptuneEditor::updateViewVisibility()
{
    // Vues : Mixeur, Edit (l'éditeur NOVA de l'instrument de la piste sélectionnée ; l'entrée audio garde sa vue) et Sonde (diagnostic de l'hôte).
    const bool mapping = ! showProbe_ && ! showMixer_ && ! showFacade_ && ! showMod_ && ! showRouter_ && ! showSplit_;
    const bool audioIn = mapping && selectedIsAudioIn();
    audioInShown_ = audioIn;
    audioInView_->setVisible (audioIn);
    instrumentShown_ = mapping && ! audioIn;
    mixerView_->setVisible (showMixer_ || showSplit_);
    facadeView_->setVisible ((showFacade_ && ! showProbe_ && ! showMixer_ && ! showMod_) || showSplit_);
    splitBar_.setVisible (showSplit_);
    splitButton_.setToggleState (showSplit_, juce::dontSendNotification);
    modView_->setVisible (showMod_ && ! showProbe_ && ! showMixer_ && ! showFacade_);
    routerView_->setVisible (showRouter_ && ! showProbe_ && ! showMixer_ && ! showFacade_ && ! showMod_);
    routerButton_.setToggleState (showRouter_ && ! showProbe_ && ! showMixer_ && ! showFacade_ && ! showMod_, juce::dontSendNotification);
    modButton_.setToggleState (showMod_ && ! showProbe_ && ! showMixer_ && ! showFacade_, juce::dontSendNotification);
    facadeButton_.setToggleState (showFacade_ && ! showProbe_ && ! showMixer_ && ! showMod_, juce::dontSendNotification);
    trackBox_.setVisible (! showProbe_ && ! consoleLocked_);
    updateInstrumentPanel();
    mixerButton_.setToggleState (showMixer_, juce::dontSendNotification);
    mappingButton_.setToggleState (mapping, juce::dontSendNotification);
    viewButton_.setToggleState (showProbe_, juce::dontSendNotification);
}

// Verrou du mode console : seules la Façade, Annuler / Rétablir, « Tout arrêter » et « Déverrouiller » restent.
void NeptuneEditor::applyConsoleLock()
{
    const bool visible = ! consoleLocked_;
    for (auto* c : std::initializer_list<juce::Component*> { &viewButton_, &routerButton_, &modButton_, &facadeButton_, &mappingButton_, &mixerButton_, &midiFilterButton_,
                                                              &controlButton_, &oscButton_, &midiSendButton_, &combiButton_, &consoleButton_, &splitButton_, &globalPresetButton_, &profileSwitchButton_,
                                                              &profileLockButton_, &voiceCountButton_, &latencyLabel_, &profileLabel_, &trackBox_ })
        c->setVisible (visible);
    unlockButton_.setVisible (consoleLocked_);
    facadeView_->setScopeVisible (! consoleLocked_);
}

void NeptuneEditor::showConsoleMenu()
{
    const auto mode = processor_.consoleMode();
    const int track = processor_.selectedTrack();
    juce::PopupMenu menu;
    menu.addItem (1, text ("console.enable") + " " + juce::String (track + 1));
    menu.addItem (3, text ("console.enable_global"));
    menu.addItem (2, text ("console.disable"), mode.enabled);
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&consoleButton_), [safe, track] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1 || choice == 3)
        {
            safe->processor_.uiSetConsoleMode (true, choice == 3 ? -1 : track);
            safe->consoleLocked_ = true;
            safe->facadeView_->setGlobal (choice == 3);
            safe->setView (false, false, true);
            safe->applyConsoleLock();
        }
        else safe->processor_.uiSetConsoleMode (false, track);
        safe->updateViewVisibility();
        safe->resized();
    });
}

// Vue double : le Mixeur et la Façade côte à côte.
void NeptuneEditor::setSplit()
{
    showSplit_ = true;
    showRouter_ = showMixer_ = showProbe_ = showFacade_ = showMod_ = false;
    updateViewVisibility();
    resized();
    repaint();
}

void NeptuneEditor::setView (bool mixer, bool probe, bool facade, bool mod, bool router)
{
    showSplit_ = false;
    showRouter_ = router;
    showMixer_ = mixer;
    showProbe_ = probe;
    showFacade_ = facade;
    showMod_ = mod;
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
        const auto kind = tt == neptune::core::TrackType::AudioIn ? "mix.type_short_audio" : tt == neptune::core::TrackType::Organ ? "mix.type_short_organ" : tt == neptune::core::TrackType::Fm ? "mix.type_short_fm"
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
    audioInView_->setTrack (track);
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
    return false;
}

// Contrôleurs MIDI : désactivés par défaut. L'interrupteur et les assignations sont enregistrés avec le projet.
// Presets globaux : le fichier .neptuneset contient tout le mixeur (voir GlobalPresets.h). Enregistrer écrase après confirmation ; charger
// remplace tout (confirmation) ; supprimer envoie le fichier à la corbeille du système.
void NeptuneEditor::showGlobalPresetMenu()
{
    const auto files = global_presets::list (settingsDir());
    juce::PopupMenu menu;
    menu.addItem (1, text ("gpreset.save_as"));
    menu.addSeparator();
    if (files.isEmpty()) menu.addItem (2, text ("gpreset.none"), false);
    for (int i = 0; i < files.size(); ++i) menu.addItem (100 + i, files[i].getFileNameWithoutExtension());
    menu.addSeparator();
    juce::PopupMenu del;
    for (int i = 0; i < files.size(); ++i) del.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    menu.addSubMenu (text ("gpreset.delete"), del, ! files.isEmpty());
    menu.addItem (3, text ("gpreset.open_folder"));
    menu.addSeparator();
    menu.addItem (4, text ("organ.import"));
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.setLookAndFeel (&getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&globalPresetButton_), [safe, files] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1)
        {
            auto* w = new juce::AlertWindow (safe->text ("gpreset.save_as"), safe->text ("gpreset.name_prompt"), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", {});
            w->addButton (safe->text ("gpreset.save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (safe->text ("mix.cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
            {
                if (safe == nullptr || result != 1) return;
                safe->saveGlobalPreset (w->getTextEditorContents ("name"));
            }), true);
        }
        else if (choice == 3) global_presets::folder (safe->settingsDir()).revealToUser();
        else if (choice == 4) safe->importOrgan();
        else if (choice >= 100 && choice < 1000 && choice - 100 < files.size()) safe->loadGlobalPreset (files[choice - 100]);
        else if (choice >= 1000 && choice - 1000 < files.size())
        {
            const auto file = files[choice - 1000];
            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, safe->text ("gpreset.delete"),
                                                                                         safe->text ("gpreset.delete_confirm") + "\n" + file.getFileNameWithoutExtension(),
                                                                                         safe->text ("gpreset.delete"), safe->text ("mix.cancel"), safe.getComponent()),
                                          [file] (int result) { if (result == 1) juce::ignoreUnused (file.moveToTrash()); });
        }
    });
}

void NeptuneEditor::importOrgan()
{
    auto chooser = std::make_shared<juce::FileChooser> (text ("organ.import"), juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.organ;*.orgue;*.Organ");
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, chooser] (const juce::FileChooser& fc)
    {
        const auto picked = fc.getResult();
        if (safe == nullptr || ! picked.existsAsFile()) return;
        juce::File organFile = picked;
        if (picked.hasFileExtension ("orgue"))
        {
            // paquet zip : extrait une fois dans la bibliothèque d'orgues, puis cherche le fichier de définition
            const auto target = safe->settingsDir().getChildFile ("Organs").getChildFile (picked.getFileNameWithoutExtension());
            if (! target.isDirectory())
            {
                juce::ZipFile zip (picked);
                const auto r = zip.uncompressTo (target, true);
                if (! r.wasOk())
                {
                    target.deleteRecursively();
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, safe->text ("organ.import"), r.getErrorMessage());
                    return;
                }
            }
            organFile = juce::File();
            for (const auto& f : target.findChildFiles (juce::File::findFiles, true, "*.organ"))
            {
                organFile = f;
                break;
            }
            if (! organFile.existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, safe->text ("organ.import"), safe->text ("organ.no_odf"));
                return;
            }
        }
        const auto result = safe->processor_.uiImportOrgan (std::filesystem::path (organFile.getFullPathName().toStdString()));
        juce::String message;
        if (! result.ok) message = juce::String::fromUTF8 (result.error.c_str());
        else
        {
            int stops = 0, pipes = 0;
            for (const auto& k : result.keyboards) { stops += k.stops; pipes += k.pipes; }
            message = juce::String::fromUTF8 (result.name.c_str()) + "\n" + juce::String (static_cast<int> (result.keyboards.size())) + " " + safe->text ("organ.imp_keyboards")
                    + ", " + juce::String (stops) + " " + safe->text ("organ.stops") + ", " + juce::String (pipes) + " " + safe->text ("organ.pipes");
            if (! result.warnings.empty())
            {
                message += "\n\n" + safe->text ("organ.warnings") + "\n";
                int shown = 0;
                for (const auto& w : result.warnings)
                {
                    if (++shown > 12) { message += "..."; break; }
                    message += "- " + juce::String::fromUTF8 (w.c_str()) + "\n";
                }
            }
        }
        juce::AlertWindow::showMessageBoxAsync (result.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon, safe->text ("organ.import"), message);
    });
}

void NeptuneEditor::saveGlobalPreset (const juce::String& rawName)
{
    const auto name = global_presets::safeName (rawName);
    if (name.isEmpty()) return;
    const auto file = global_presets::fileFor (settingsDir(), name);
    auto write = [safe = juce::Component::SafePointer<NeptuneEditor> (this), file]
    {
        if (safe == nullptr) return;
        const auto report = global_presets::save (safe->processor_.globalStateText(), file);
        const auto message = report.ok
            ? juce::String::fromUTF8 (safe->tr_.format ("gpreset.saved", { file.getFileNameWithoutExtension().toStdString(), std::to_string (report.images), std::to_string (report.packs), std::to_string (report.missing) }).c_str())
            : juce::String::fromUTF8 (safe->tr_.format ("gpreset.save_failed", { file.getFullPathName().toStdString() }).c_str());
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, safe->text ("gpreset.save_as"), message);
    };
    if (! file.existsAsFile()) { write(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("gpreset.save_as"),
                                                                                 text ("gpreset.overwrite") + "\n" + name, text ("gpreset.save"), text ("mix.cancel"), this),
                                  [write] (int result) { if (result == 1) write(); });
}

void NeptuneEditor::loadGlobalPreset (const juce::File& file)
{
    auto apply = [safe = juce::Component::SafePointer<NeptuneEditor> (this), file]
    {
        if (safe == nullptr) return;
        juce::String error;
        const auto stateText = global_presets::load (file, safe->settingsDir().getChildFile ("facade-assets"), error);
        std::string reason = error.toStdString();
        if (! stateText.empty() && safe->processor_.uiLoadGlobalState (stateText, &reason))
        {
            safe->updateTrackBox();
            return;
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, safe->text ("gpreset.load"),
                                                juce::String::fromUTF8 (safe->tr_.format ("gpreset.load_failed", { reason }).c_str()));
    };
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("gpreset.load"),
                                                                                 text ("gpreset.load_confirm") + "\n" + file.getFileNameWithoutExtension(),
                                                                                 text ("gpreset.load"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); });
}

void NeptuneEditor::showControlMenu()
{
    const bool on = processor_.ccEnabled();
    const auto cfg = processor_.controlConfig();
    const bool organ = processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Organ;
    juce::PopupMenu menu;
    menu.addItem (1, text ("control.enable"), true, on);
    menu.addSeparator();
    menu.addItem (2, text ("control.assign_organ"), organ);
    menu.addItem (5, text ("cpanel.manage"));
    menu.addItem (3, text ("control.clear") + " (" + juce::String (static_cast<int> (cfg.bindings.size())) + ")", ! cfg.bindings.empty());
    menu.addSeparator();
    const bool reduced = processor_.reducedHostList();
    menu.addItem (4, text ("macro.reduced"), true, reduced);
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.setLookAndFeel (&getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&controlButton_), [safe, on, reduced] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->processor_.uiSetCcEnabled (! on);
        else if (choice == 2)
        {
            safe->processor_.uiAssignOrganControllers();
            safe->processor_.uiSetCcEnabled (true); // assigner sans activer n'aurait pas d'effet audible : on active
        }
        else if (choice == 3) safe->processor_.uiClearControllers();
        else if (choice == 4) safe->processor_.uiSetReducedHostList (! reduced);
        else if (choice == 5)
        {
            auto panel = std::make_unique<ControlPanel> (safe->processor_, safe->palette_, safe->tr_);
            juce::CallOutBox::launchAsynchronously (std::move (panel), safe->controlButton_.getScreenBounds(), nullptr);
        }
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
    const auto feedback = processor_.oscFeedbackDestination();
    if (on)
    {
        menu.addSeparator();
        const auto status = port > 0 ? tr_.format ("osc.listening", { std::to_string (port) }) : tr_.format ("osc.busy", { std::to_string (settings.oscPort) });
        menu.addItem (3, juce::String::fromUTF8 (status.c_str()), false, false);
        menu.addItem (4, text ("osc.feedback"));
        if (feedback.isNotEmpty())
        {
            menu.addItem (5, text ("osc.feedback_to") + " " + feedback, false, false);
            menu.addItem (6, text ("osc.feedback_stop"));
        }
    }
    juce::Component::SafePointer<NeptuneEditor> safe (this);
    menu.setLookAndFeel (&getLookAndFeel());   // sinon le menu prend le style JUCE d'origine
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&oscButton_), [safe, on, settings] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->processor_.uiSetOscEnabled (! on);
        else if (choice == 2) safe->processor_.uiSetOscAllInterfaces (! settings.oscAllInterfaces);
        else if (choice == 6) safe->processor_.uiSetOscFeedback ({});
        else if (choice == 4)
        {
            auto* w = new juce::AlertWindow (safe->text ("osc.feedback"), safe->text ("osc.feedback_prompt"), juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("dest", settings.feedbackPort > 0 ? juce::String (settings.feedbackHost) + ":" + juce::String (settings.feedbackPort) : juce::String ("192.168.1.20:9001"), "");
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
            {
                if (safe != nullptr && result == 1) safe->processor_.uiSetOscFeedback (w->getTextEditorContents ("dest"));
            }), true);
        }
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
    if (consoleLocked_)   // la console reste sur sa piste et sur sa Façade
    {
        const int track = processor_.consoleMode().track;
        if (track >= 0 && processor_.selectedTrack() != track) processor_.selectTrack (track);   // -1 : Façade globale, la piste sélectionnée ne compte pas
        if (! showFacade_) setView (false, false, true);
    }
    updateTrackBox();
    rebindKnobs();
    updateInstrumentPanel();   // piste ou type changé : l'éditeur NOVA est reconstruit
    if (audioInShown_ != (! showProbe_ && ! showMixer_ && ! showFacade_ && ! showMod_ && ! showRouter_ && ! showSplit_ && selectedIsAudioIn())) { updateViewVisibility(); resized(); } // type de la piste changé
    if (audioInView_->isVisible()) audioInView_->refresh();
    updateRecoveryBanner();
    updateProfileControls();
    updateUndoButtons();
    {
        // compteur de voix actives, toutes pistes confondues (Sampler et Synthé)
        const int voices = processor_.totalActiveVoices();
        if (voices != lastVoiceCount_)
        {
            lastVoiceCount_ = voices;
            voiceCountButton_.setButtonText (text ("analog.voices") + " " + juce::String (voices) + " / " + juce::String (processor_.masterSettings().maxVoices));
        }
    }
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
    auto banner = juce::Rectangle<int> (32, lh_ - 74, lw_ - 64, 36);
    discardButton_.setBounds (banner.removeFromRight (90).reduced (2));
    restoreButton_.setBounds (banner.removeFromRight (90).reduced (2));
    recoveryLabel_.setBounds (banner);
    if (modal_) modal_->setBounds (rootBounds());

    {
        // barre du haut : boutons resserrés autour de leur texte (28 de haut, centrés sur l'ancienne rangée)
        static constexpr int barY = 36, barH = 28, pad = 22, gap = 6;
        auto fit = [this] (const juce::TextButton& b, int minWidth)
        {
            const juce::Font f (novaSkin_.font (novaLook_.buttonFontSize (barH), true));
            return std::max (minWidth, juce::roundToInt (std::ceil (f.getStringWidthFloat (b.getButtonText()))) + pad);
        };
        int right = lw_ - 32;
        for (auto* b : { &viewButton_, &routerButton_, &modButton_, &facadeButton_, &mappingButton_, &mixerButton_ })
        {
            const int w = fit (*b, 60);
            right -= w;
            b->setBounds (right, barY, w, barH);
            right -= gap;
        }
        const int panicW = fit (panicButton_, 60);
        right -= 4;
        panicButton_.setBounds (right - panicW, barY, panicW, barH);
        int left = 190;
        unlockButton_.setBounds (left, barY, fit (unlockButton_, 120), barH);
        trackBox_.setBounds (left, barY, 80, barH);
        left += 80 + gap + 2;
        for (auto* b : { &midiFilterButton_, &controlButton_, &oscButton_, &midiSendButton_ })
        {
            const int w = fit (*b, 44);
            b->setBounds (left, barY, w, barH);
            left += w + gap;
        }
        const int areaH = juce::jmax (200, lh_ - 80 - 40 - (recoveryShown_ ? 40 : 0));
        if (showSplit_)
        {
            // Mixeur à gauche (réduit pour tenir), Façade à droite, barre de séparation entre les deux
            const int total = lw_ - 64, barW = 10;
            const int leftW = juce::jlimit (160, juce::jmax (160, total - 160 - barW), juce::roundToInt (total * splitRatio_));
            mixerView_->placeScaled ({ 32, 80, leftW, areaH });
            splitBar_.setBounds (32 + leftW, 80, barW, areaH);
            facadeView_->setBounds (32 + leftW + barW, 80, total - leftW - barW, areaH);
        }
        else
        {
            mixerView_->placeScaled ({ 32, 80, lw_ - 64, juce::jmax (200, lh_ - 80 - 40) });
            facadeView_->setBounds (32, 80, lw_ - 64, areaH);
        }
        modView_->setBounds (32, 80, lw_ - 64, juce::jmax (200, lh_ - 80 - 40 - (recoveryShown_ ? 40 : 0)));
        routerView_->setBounds (32, 80, lw_ - 64, juce::jmax (200, lh_ - 80 - 40 - (recoveryShown_ ? 40 : 0)));
    }
    if (midiFilterEditor_) midiFilterEditor_->setBounds (rootBounds());
    if (midiSendPanel_) midiSendPanel_->setBounds (rootBounds());
    if (combiPanel_) combiPanel_->setBounds (rootBounds());
    if (instrumentPanel_ != nullptr)
        instrumentPanel_->setBounds (juce::Rectangle<int> (32, 80, lw_ - 64, juce::jmax (300, lh_ - 40 - 80 - (recoveryShown_ ? 40 : 0))));   // jusqu'à la rangée du bas (et au-dessus du bandeau de récupération)
    audioInView_->setBounds ({ 32, 80, lw_ - 64, juce::jmax (200, lh_ - 80 - 40 - (recoveryShown_ ? 40 : 0)) });
    if (insertOverlay_) insertOverlay_->setBounds (rootBounds());

    {
        // rangée du bas, sur toute la largeur : latence annoncée, profil de performance (bascule et verrou), puis annuler / rétablir ; même hauteur partout
        // Chaque bouton est juste assez large pour son texte (le texte qui change de longueur prend la plus large de ses formes).
        const int rowY = lh_ - 30, rowH = 22, pad = 18, gap = 6;
        const juce::Font font (novaSkin_.font (novaLook_.buttonFontSize (rowH), true));
        auto widthOf = [&font, pad] (std::initializer_list<juce::String> texts)
        {
            float w = 0.0f;
            for (const auto& t : texts) w = std::max (w, font.getStringWidthFloat (t));
            return juce::roundToInt (std::ceil (w)) + pad;
        };
        redoButton_.setBounds (lw_ - 32 - 64, rowY, 64, rowH);
        undoButton_.setBounds (lw_ - 32 - 64 - 4 - 64, rowY, 64, rowH);
        const int presetW = widthOf ({ globalPresetButton_.getButtonText() });
        globalPresetButton_.setBounds (lw_ - 32 - 64 - 4 - 64 - 12 - presetW, rowY, presetW, rowH);
        const int consoleW = widthOf ({ consoleButton_.getButtonText() });   // le mode console est dans la barre du bas, à gauche de Preset global (la barre du haut était pleine)
        consoleButton_.setBounds (globalPresetButton_.getX() - gap - consoleW, rowY, consoleW, rowH);
        const int splitW = widthOf ({ splitButton_.getButtonText() });
        splitButton_.setBounds (consoleButton_.getX() - gap - splitW, rowY, splitW, rowH);
        const int combiW = widthOf ({ combiButton_.getButtonText() });
        combiButton_.setBounds (splitButton_.getX() - gap - combiW, rowY, combiW, rowH);
        auto row = juce::Rectangle<int> (32, rowY, combiButton_.getX() - 32 - 8, rowH);
        latencyLabel_.setBounds (row.removeFromLeft (widthOf ({ juce::String::fromUTF8 (tr_.format ("latency.reported", { "99999" }).c_str()) }) - pad + 12));   // la plus longue forme du texte
        row.removeFromLeft (gap);
        profileSwitchButton_.setBounds (row.removeFromLeft (widthOf ({ profileSwitchButton_.getButtonText() })));
        row.removeFromLeft (gap);
        profileLockButton_.setBounds (row.removeFromLeft (widthOf ({ text ("profile.lock"), text ("profile.unlock") })));
        row.removeFromLeft (gap);
        voiceCountButton_.setBounds (row.removeFromLeft (widthOf ({ text ("analog.voices") + " 1024 / 1024" })));
        row.removeFromLeft (gap + 6);
        profileLabel_.setBounds (row);
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
    g.setColour (colour (Token::TextDim));   // numéro de version de développement, sous le titre : pour vérifier quelle version tourne
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (neptune::core::version::kString, juce::Rectangle<int> (32, 66, 330, 12), juce::Justification::centredLeft);
    if (const auto loading = processor_.loadSummary(); ! loading.empty())   // chargement des échantillons en cours (ou raté) : par piste, pour ne pas jouer trop tôt
    {
        g.setColour (colour (Token::Accent));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 (loading.c_str()), juce::Rectangle<int> (32, 80, lw_ - 64, 18), juce::Justification::centredLeft, true);
    }

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

    latencyLabel_.setText (juce::String::fromUTF8 (tr_.format ("latency.reported", { std::to_string (processor_.getLatencySamples()) }).c_str()),
                           juce::dontSendNotification);
}

void NeptuneEditor::paint (juce::Graphics& g) { g.fillAll (colour (neptune::core::theme::Token::BgApp)); }
