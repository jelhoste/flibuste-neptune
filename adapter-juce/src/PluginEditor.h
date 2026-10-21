#pragma once
#include "PluginProcessor.h"
#include "ControlPanel.h"
#include "AudioInView.h"
#include "MixerView.h"
#include "MidiFilterEditor.h"
#include "ScaledRoot.h"
#include "InsertOverlay.h"
#include "InstrumentPanel.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include <map>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Fenêtre de sonde : paramètres en direct, et ce que l'hôte envoie (transport, notes CLAP, expressions, modulation).
class NeptuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeptuneEditor (NeptuneProcessor&);
    ~NeptuneEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void paintContent (juce::Graphics&); // dessiné dans la racine mise à l'échelle
    bool keyPressed (const juce::KeyPress&) override; // Ctrl/Cmd+Z : annuler ; Ctrl/Cmd+Maj+Z ou Ctrl+Y : rétablir

private:
    void timerCallback() override;
    std::unique_ptr<MidiFilterEditor> midiFilterEditor_;
    juce::TextButton midiFilterButton_; // filtre MIDI avancé de la piste sélectionnée
    juce::TextButton controlButton_;    // contrôleurs MIDI (CC) : interrupteur et assignations par défaut
    juce::TextButton oscButton_;        // serveur OSC : interrupteur, réseau local, port
    void showControlMenu();
    void showOscMenu();
    void openMidiFilter();
    juce::Rectangle<int> rootBounds() const noexcept { return { 0, 0, lw_, lh_ }; } // taille logique (avant mise à l'échelle)
    ScaledRoot root_;
    int lw_ = 1160, lh_ = 960;

    NeptuneProcessor& processor_;
    // Langue, thème et contraste viennent de settings.json, lus une fois au démarrage de l'instance.
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (std::string_view key) const { return juce::String::fromUTF8 (tr_.tr (key).data(), static_cast<int> (tr_.tr (key).size())); }
    juce::Label latencyLabel_;
    juce::Label recoveryLabel_;
    juce::Label profileLabel_;
    juce::TextButton profileSwitchButton_, profileLockButton_;
    void updateProfileControls();

    std::unique_ptr<KnobModal> modal_;
    juce::TextButton undoButton_, redoButton_;

    juce::TextButton voiceCountButton_; // « Voix : n / limite » ; un clic règle la limite globale de voix
    int lastVoiceCount_ = -1;
    juce::TextButton viewButton_; // bascule entre la vue Mapping et la vue Sonde (texte de diagnostic de l'hôte)
    juce::TextButton panicButton_;
    bool showProbe_ = false;
    bool showMixer_ = true; // on arrive sur le Mixeur
    // Trois vues : Mixeur (pistes, bus, master), Mapping (l'instrument de la piste sélectionnée), Sonde (diagnostic de l'hôte)
    juce::TextButton mixerButton_, mappingButton_;
    std::unique_ptr<MixerView> mixerView_;
    // Vue Synthé : remplace la vue Mapping quand la piste sélectionnée est un synthé.
    std::unique_ptr<InsertOverlay> insertOverlay_;
    std::unique_ptr<InstrumentPanel> instrumentPanel_;   // éditeur NOVA du synthé ou du FM de la piste sélectionnée (page Edit)
    void updateInstrumentPanel();
    void openInsert (NeptuneProcessor::StripKind, int index, int slot);
    void closeInsert();
    bool instrumentShown_ = false, audioInShown_ = false;
    // Vue Orgue : remplace la vue Mapping quand la piste sélectionnée est de type Orgue.
    std::unique_ptr<AudioInView> audioInView_;
    bool selectedIsAudioIn() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::AudioIn; }
    bool selectedIsOrgan() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Organ; }
    // Vue FM : remplace la vue Mapping quand la piste sélectionnée est de type FM.
    bool selectedIsFm() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Fm; }
    std::array<int, 128> synthKeyTrack_ {}; // piste sur laquelle chaque touche du clavier à l'écran a démarré
    bool selectedIsSynth() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Synth; }
    juce::ComboBox trackBox_;   // piste sélectionnée
    int knobTrack_ = -1;        // piste sur laquelle les potards d'instrument sont branchés
    std::uint32_t trackBoxRevision_ = 0xFFFFFFFFu;
    void setView (bool mixer, bool probe);
    void rebindKnobs();
    void updateTrackBox();
    void updateViewVisibility();
    std::unique_ptr<juce::FileChooser> chooser_, importChooser_;

    bool updatingControls_ = false;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void updateUndoButtons();
    juce::TextButton restoreButton_, discardButton_;
    bool recoveryShown_ = false;
    void updateRecoveryBanner();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
