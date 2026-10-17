#pragma once
#include "PluginProcessor.h"
#include "EnvelopeEditor.h"
#include "ImportWizard.h"
#include "MappingMatrix.h"
#include "VelocityPanel.h"
#include "PolyphonyPanel.h"
#include "SlotsPanel.h"
#include "ChokePanel.h"
#include "VoicesView.h"
#include "FilterEditor.h"
#include "MixerView.h"
#include "MidiFilterEditor.h"
#include "FmSysexIO.h"
#include "FmView.h"
#include "OrganView.h"
#include "PresetBar.h"
#include "PresetManager.h"
#include "ScaledRoot.h"
#include "SynthView.h"
#include "InsertOverlay.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ZoneTable.h"
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
    void openPresetManager (bool saveAs);
    std::unique_ptr<PresetBar> presetBar_;
    std::unique_ptr<PresetManager> presetManager_;
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

    // Potards génériques (palier 0) et leur modale.
    juce::OwnedArray<NeptuneKnob> knobs_;
    std::unique_ptr<KnobModal> modal_;
    juce::TextButton undoButton_, redoButton_;

    // Sampler (palier 1) : chargement d'un sample et réglages de la zone (le premier jalon n'a qu'une zone).
    juce::TextButton loadButton_, modeButton_;
    juce::Label statusLabel_, rootLabel_;
    juce::Slider rootSlider_ { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    std::unique_ptr<EnvelopeEditor> envelopeEditor_;
    juce::TextButton voiceCountButton_; // « Voix : n / limite » ; un clic règle la limite globale de voix
    int lastVoiceCount_ = -1;
    juce::TextButton viewButton_; // bascule entre la vue Mapping et la vue Sonde (texte de diagnostic de l'hôte)
    // Vue Mapping (jalon 5) : matrice notes × vélocité plein écran, liste des zones rétractable.
    std::unique_ptr<MappingMatrix> matrix_;
    juce::TextButton listToggleButton_;
    // Moniteur des voix (débogage) : remplace l'enveloppe dans la zone en bas à droite quand il est affiché
    std::unique_ptr<VoicesView> voicesView_;
    juce::TextButton voicesButton_, panicButton_;
    bool showVoices_ = false;
    struct SeenVoice { neptune::core::Engine::VoiceInfo info; juce::uint32 lastSeen = 0; bool present = false; };
    std::map<std::uint64_t, SeenVoice> seenVoices_;
    void updateVoices();
    // Éditeur des filtres par voie (superposé, comme l'assistant d'import)
    std::unique_ptr<FilterEditor> filterEditor_;
    juce::TextButton filtersButton_;
    void openFilterEditor();
    void closeFilterEditor();
    void updateFilterEditor();
    // Déclenchement et choke des zones sélectionnées
    juce::Label triggerLabel_, chokeLabel_;
    juce::ComboBox triggerBox_, chokeBox_;
    juce::TextButton chokeMoreButton_;
    void openChokePanel (juce::Component& anchor);
    juce::Component::SafePointer<ChokePanel> chokePanel_;
    bool listVisible_ = true;
    void setZoneSelection (const std::vector<int>&);
    void openVelocityPanel (juce::Component& anchor);
    juce::Component::SafePointer<VelocityPanel> velocityPanel_;
    void openPolyphonyPanel (juce::Component& anchor);
    juce::Component::SafePointer<PolyphonyPanel> polyphonyPanel_;
    void openSlotsPanel (juce::Component& anchor);
    juce::Component::SafePointer<SlotsPanel> slotsPanel_;
    juce::TextButton slotsButton_;
    // Menu contextuel « Assigner à un emplacement » pour une zone (`zone` >= 0) ou un Groupe (`group` non vide).
    void showSlotAssignMenu (int zone, const std::string& group);
    struct GroupMenuListener;
    std::vector<std::string> groupNames_;   // noms des Groupes, dans l'ordre des cases de chargement
    std::unique_ptr<juce::MouseListener> groupMenuListener_;
    bool showProbe_ = false;
    bool showMixer_ = true; // on arrive sur le Mixeur
    // Trois vues : Mixeur (pistes, bus, master), Mapping (l'instrument de la piste sélectionnée), Sonde (diagnostic de l'hôte)
    juce::TextButton mixerButton_, mappingButton_;
    std::unique_ptr<MixerView> mixerView_;
    // Vue Synthé : remplace la vue Mapping quand la piste sélectionnée est un synthé.
    std::unique_ptr<SynthView> synthView_;
    std::unique_ptr<InsertOverlay> insertOverlay_;
    void openInsert (NeptuneProcessor::StripKind, int index, int slot);
    void closeInsert();
    bool synthShown_ = false, fmShown_ = false, organShown_ = false;
    // Vue Orgue : remplace la vue Mapping quand la piste sélectionnée est de type Orgue.
    std::unique_ptr<OrganView> organView_;
    bool selectedIsOrgan() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Organ; }
    // Vue FM : remplace la vue Mapping quand la piste sélectionnée est de type FM.
    std::unique_ptr<FmView> fmView_;
    std::unique_ptr<FmSysexIO> sysexIO_;
    bool selectedIsFm() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Fm; }
    std::array<int, 128> synthKeyTrack_ {}; // piste sur laquelle chaque touche du clavier à l'écran a démarré
    bool selectedIsSynth() const { return processor_.trackType (processor_.selectedTrack()) == neptune::core::TrackType::Synth; }
    void updateSynthView();
    juce::ComboBox trackBox_;   // piste sélectionnée
    int knobTrack_ = -1;        // piste sur laquelle les potards d'instrument sont branchés
    std::uint32_t trackBoxRevision_ = 0xFFFFFFFFu;
    void setView (bool mixer, bool probe);
    void rebindKnobs();
    void updateTrackBox();
    void updateViewVisibility();
    std::unique_ptr<juce::FileChooser> chooser_, importChooser_;

    // Zones multiples (jalon 3) : liste, import par gabarit de nom, suppression ; les réglages agissent sur la sélection.
    std::unique_ptr<ZoneTable> zoneTable_;
    std::unique_ptr<ImportWizard> wizard_;
    juce::TextButton importButton_, removeButton_;
    juce::Label selectionLabel_;
    std::uint32_t lastRevision_ = 0xFFFFFFFFu;
    neptune::core::InstrumentDescription cachedDescription_;
    void startImport();
    void openWizard (std::vector<std::string> paths);
    void closeWizard();
    void applyImport (const ImportWizard::Result&);
    void removeSelectedZones();

    // Tables chargées en mémoire : alternatives round-robin, relâchements, un Groupe à la fois (vidage réversible).
    juce::Label loadTitle_;
    juce::ToggleButton rrToggle_, releaseToggle_, streamToggle_;
    juce::Label preloadLabel_;
    juce::ComboBox preloadBox_; // Ko de PCM gardés en mémoire par sample en mode streaming
    juce::OwnedArray<juce::ToggleButton> groupToggles_;
    juce::Component groupsHolder_;
    juce::Viewport groupsViewport_;
    neptune::core::memory::FileInfoCache fileInfo_; // en-têtes des fichiers : mémoire exacte, un fichier n'est ouvert qu'une fois
    std::string groupsSignature_;
    void rebuildLoadPanel();
    bool updatingControls_ = false;
    void chooseSample();
    void editZone (const std::function<void (neptune::core::ZoneDescription&)>&);
    void updateSamplerControls();
    juce::String statusText() const;
    juce::String baseStatusText() const;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void updateUndoButtons();
    juce::TextButton restoreButton_, discardButton_;
    bool recoveryShown_ = false;
    void updateRecoveryBanner();
    juce::OwnedArray<juce::TextButton> latencyButtons_; // test de la latence modifiable à l'exécution
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
