#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "HostProbe.h"
#include "neptune/core/Autosave.h"
#include "neptune/core/Engine.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Undo.h"
#include "neptune/core/UiState.h"

// Fine couche d'adaptation : traduit JUCE/CLAP <-> cœur. Aucune logique métier ici.
class NeptuneProcessor : public juce::AudioProcessor,
                         public clap_juce_extensions::clap_juce_audio_processor_capabilities
{
public:
    NeptuneProcessor();
    ~NeptuneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Neptune"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // État = JSON versionné produit/lu par le cœur (neptune/core/State.h).
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    neptune::core::ParameterSet& parameters() { return params_; }
    int probeTracks() const { return probeTracks_; }
    HostProbe& hostProbe() { return hostProbe_; }

    // Réglages globaux (fichier du dossier de configuration utilisateur) et récupération après crash.
    const neptune::core::settings::GlobalSettings& settings() const { return settings_; }
    juce::String settingsFilePath() const { return juce::String::fromUTF8 (settingsFile_.u8string().c_str()); }
    bool hasRecoverableState() const;
    juce::String recoverableDescription() const; // date de l'autosave proposé
    void restoreRecovered();                     // thread message
    void discardRecovered();
    int probeOutputs() const { return probeOutputs_; }

    // Profil Live/Studio : réglage partagé du processus + verrou propre à l'instance (sauvegardé dans le projet).
    neptune::core::InstanceProfile& profile() { return profile_; }
    void notifyStateChanged(); // thread message : le verrou fait partie de l'état du projet

    // Édition depuis l'interface (thread message). Chaque geste prévient l'hôte (automation) ET alimente l'annulation.
    void uiBeginEdit (neptune::core::ParamId);
    void uiSetNormalised (neptune::core::ParamId, double normalised);
    void uiEndEdit (neptune::core::ParamId);
    void uiResetToDefault (neptune::core::ParamId);
    bool uiUndo();
    bool uiRedo();
    const neptune::core::UndoManager& undoManager() const { return undo_; }

    // Instrument du Sampler (palier 1). La description (chemins + réglages de zone) est sauvegardée dans le projet ;
    // le chargement des fichiers se fait sur un thread dédié, jamais sur le thread audio ni sur le thread de message.
    struct InstrumentStatus
    {
        enum class Kind { None, Loading, Loaded, Error } kind = Kind::None;
        std::string name, error;
        int zones = 0, files = 0;       // Loaded : nombre de zones et de fichiers distincts
        int done = 0, total = 0;        // Loading : avancement
        int channels = 0, bitDepth = 0;
        double sampleRate = 0.0, seconds = 0.0, megabytes = 0.0;
    };
    neptune::core::InstrumentDescription instrumentDescription() const; // copie
    // Thread de message. `forceReload` : relire les fichiers même si le chemin n'a pas changé. Marque le projet modifié.
    void setInstrumentDescription (neptune::core::InstrumentDescription, bool forceReload = false);
    // Même chose, mais annulable : passe par l'historique (une seule entrée par geste si `mergeKey` est non vide et que
    // uiBeginGesture/uiEndGesture entourent le geste). À utiliser pour toute modification venant de l'utilisateur.
    void uiEditInstrument (neptune::core::InstrumentDescription, std::string name, std::string mergeKey = {});
    void uiBeginGesture() { undo_.beginGesture(); }
    void uiEndGesture() { undo_.endGesture(); }
    InstrumentStatus instrumentStatus() const;
    // Voix éteintes faute de données du disque (streaming) depuis le chargement du plugin.
    std::uint32_t streamUnderruns() const noexcept { return engine_.streamUnderruns(); }
    // Change à chaque modification de la description : l'interface compare ce nombre au lieu de comparer tout l'instrument.
    std::uint32_t instrumentRevision() const noexcept { return revision_.load(); }

    // Taille mémorisée de la modale des potards (par type de terminal), enregistrée dans ui_state.json.
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const { return uiState_.modalSize (t); }
    void setModalSize (neptune::core::uistate::Terminal, neptune::core::uistate::Size);
    // Dernier gabarit de nom utilisé à l'import (JSON), mémorisé dans ui_state.json.
    std::string lastImportTemplate() const { return uiState_.lastImportTemplate; }
    void setLastImportTemplate (const std::string&);

    // Extensions CLAP (clap-juce-extensions) : notes au dialecte CLAP avec note_id, expressions de note, fin de note.
    bool supportsNoteDialectClap (bool isInput) override { return isInput; }
    bool supportsDirectEvent (uint16_t spaceId, uint16_t type) override;
    void handleDirectEvent (const clap_event_header_t* event, int sampleOffset) override;
    bool supportsOutboundEvents() override { return true; }
    void addOutboundEventsToQueue (const clap_output_events* out, const juce::MidiBuffer& midi, int sampleOffset) override;

private:
    static BusesProperties makeBuses (int numOutputs);

    neptune::core::ParameterSet params_; // déclaré avant le moteur : les paramètres survivent à tout ce qui les lit
    neptune::core::Engine engine_;
    neptune::core::InstanceProfile profile_;
    neptune::core::UndoManager undo_; // thread message uniquement
    mutable juce::CriticalSection instrumentLock_;
    neptune::core::InstrumentDescription instrument_; // protégé par instrumentLock_
    InstrumentStatus status_;                          // protégé par instrumentLock_
    std::atomic<std::uint32_t> revision_ { 0 };
    neptune::core::uistate::UiState uiState_;
    std::filesystem::path uiStateFile_;
    int probeTracks_ = 0;
    int probeOutputs_ = 0;
    HostProbe hostProbe_;

    void autosaveNow();
    class InstrumentLoader;
    void adoptInstrumentFromState (const std::optional<neptune::core::InstrumentDescription>&);
    void setStatus (InstrumentStatus);
    std::string currentStateText() const; // état complet tel qu'écrit dans le projet et dans l'autosave
    void adoptStateIdentity (const std::string& idFromState);

    neptune::core::settings::GlobalSettings settings_;
    std::filesystem::path settingsFile_;
    std::unique_ptr<neptune::core::AutosaveStore> autosave_;
    std::unique_ptr<InstrumentLoader> loader_; // après engine_ : détruit avant lui
    mutable juce::CriticalSection recoveryLock_;
    std::optional<neptune::core::RecoveredState> recovered_; // protégé par recoveryLock_

    struct AutosaveTimer : juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    } autosaveTimer_;

    // Notes terminées à signaler à l'hôte (NOTE_END). Écrit et lu sur le thread audio uniquement.
    struct PendingNoteEnd { std::int32_t noteId; std::int16_t port, channel, key; };
    std::array<PendingNoteEnd, 256> pendingEnds_ {};
    int numPendingEnds_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneProcessor)
};
