#include "PluginEditor.h"
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
    envelopeEditor_->onChange = [this] (const neptune::core::EnvelopeDescription& e)
    {
        editZone ([e] (neptune::core::ZoneDescription& z) { z.envelope = e; });
    };
    addAndMakeVisible (*envelopeEditor_);

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
                               safe->editZone ([&path] (neptune::core::ZoneDescription& z) { z.samplePath = path; });
                           });
}

// Modifie la zone 0 (la crée avec les réglages par défaut si l'instrument est vide) et la republie.
void NeptuneEditor::editZone (const std::function<void (neptune::core::ZoneDescription&)>& change)
{
    auto d = processor_.instrumentDescription();
    const bool created = d.zones.empty();
    if (created) d.zones.emplace_back();
    change (d.zones.front());
    if (created && d.zones.front().samplePath.empty()) return; // pas de sample : rien à jouer
    processor_.setInstrumentDescription (std::move (d), /*forceReload*/ false);
    updateSamplerControls();
}

juce::String NeptuneEditor::statusText() const
{
    using Kind = NeptuneProcessor::InstrumentStatus::Kind;
    const auto st = processor_.instrumentStatus();
    switch (st.kind)
    {
        case Kind::None: return text ("sampler.none");
        case Kind::Loading: return text ("sampler.loading");
        case Kind::Error: return juce::String::fromUTF8 (tr_.format ("sampler.error", { st.error }).c_str());
        case Kind::Loaded:
        {
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
    const auto d = processor_.instrumentDescription();
    const neptune::core::ZoneDescription z = d.zones.empty() ? neptune::core::ZoneDescription {} : d.zones.front();
    const auto modeName = tr_.tr (z.mode == neptune::core::PlayMode::Loop ? "sampler.mode_loop" : "sampler.mode_one_shot");
    modeButton_.setButtonText (juce::String::fromUTF8 (tr_.format ("sampler.mode", { std::string (modeName) }).c_str()));

    updatingControls_ = true;
    if (! rootSlider_.isMouseButtonDown()) rootSlider_.setValue (z.rootNote, juce::dontSendNotification);
    updatingControls_ = false;

    statusLabel_.setText (statusText(), juce::dontSendNotification);
    const bool has = ! d.zones.empty();
    modeButton_.setEnabled (has);
    rootSlider_.setEnabled (has);
    envelopeEditor_->setEnabled (has);
    envelopeEditor_->setEnvelope (z.envelope);
}

void NeptuneEditor::updateViewVisibility()
{
    const bool sampler = ! showProbe_;
    loadButton_.setVisible (sampler);
    modeButton_.setVisible (sampler);
    statusLabel_.setVisible (sampler);
    rootLabel_.setVisible (sampler);
    rootSlider_.setVisible (sampler);
    envelopeEditor_->setVisible (sampler);
    for (auto* k : knobs_) k->setVisible (sampler);
    viewButton_.setButtonText (showProbe_ ? "Sampler" : "Probe");
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

    // Vue Sampler : chargement, état, mode, note racine, enveloppe graphique.
    viewButton_.setBounds (getWidth() - 32 - 110, 32, 110, 36);
    {
        auto r = juce::Rectangle<int> (32, 80, getWidth() - 64, 36);
        loadButton_.setBounds (r.removeFromLeft (170).reduced (2));
        modeButton_.setBounds (r.removeFromLeft (190).reduced (2));
        statusLabel_.setBounds (r);
        auto r2 = juce::Rectangle<int> (32, 122, getWidth() - 64, 36);
        rootLabel_.setBounds (r2.removeFromLeft (200));
        rootSlider_.setBounds (r2.removeFromLeft (170).reduced (2));
        const int envTop = 168;
        const int envBottom = getHeight() - 270 - 12;
        envelopeEditor_->setBounds (32, envTop, getWidth() - 64, juce::jmax (120, envBottom - envTop));
    }

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

    // Nom et valeur de chaque potard (vue Sampler)
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
