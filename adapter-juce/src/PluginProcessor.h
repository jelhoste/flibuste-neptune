#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "HostProbe.h"
#include "neptune/core/Autosave.h"
#include "neptune/core/Engine.h"
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

    // Taille mémorisée de la modale des potards (par type de terminal), enregistrée dans ui_state.json.
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const { return uiState_.modalSize (t); }
    void setModalSize (neptune::core::uistate::Terminal, neptune::core::uistate::Size);

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
    neptune::core::uistate::UiState uiState_;
    std::filesystem::path uiStateFile_;
    int probeTracks_ = 0;
    int probeOutputs_ = 0;
    HostProbe hostProbe_;

    void autosaveNow();
    std::string currentStateText() const; // état complet tel qu'écrit dans le projet et dans l'autosave
    void adoptStateIdentity (const std::string& idFromState);

    neptune::core::settings::GlobalSettings settings_;
    std::filesystem::path settingsFile_;
    std::unique_ptr<neptune::core::AutosaveStore> autosave_;
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
