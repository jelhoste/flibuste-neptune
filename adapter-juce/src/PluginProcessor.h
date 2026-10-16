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
#include <set>
#include <string>
#include "HostProbe.h"
#include "ParamHost.h"
#include "PresetHost.h"
#include "neptune/core/AnalogSynth.h"
#include "neptune/core/FmPreset.h"
#include "neptune/core/Autosave.h"
#include "neptune/core/Engine.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/MixerState.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Synth.h"
#include "neptune/core/Undo.h"
#include "neptune/core/UiState.h"

// Fine couche d'adaptation : traduit JUCE/CLAP <-> cœur. Aucune logique métier ici.
class NeptuneProcessor : public juce::AudioProcessor,
                         public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                         public ParamHost,
                         public PresetHost // presets du synthé de la piste sélectionnée (mêmes presets qu'AnalogSynth)
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

    neptune::core::ParameterSet& parameters() override { return params_; }
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
    void uiBeginEdit (neptune::core::ParamId) override;
    void uiSetNormalised (neptune::core::ParamId, double normalised) override;
    void uiEndEdit (neptune::core::ParamId) override;
    void uiResetToDefault (neptune::core::ParamId) override;
    bool uiUndo() override;
    bool uiRedo() override;
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
    // Piste sélectionnée : toutes les fonctions « instrument » ci-dessous agissent sur elle (sauf mention d'une piste).
    int selectedTrack() const noexcept { return selectedTrack_.load(); }
    void selectTrack (int track); // thread de message ; pas annulable ; n'altère ni l'instrument ni l'audio
    neptune::core::InstrumentDescription instrumentDescription() const { return instrumentDescription (selectedTrack()); }
    neptune::core::InstrumentDescription instrumentDescription (int track) const; // copie
    // Thread de message. `forceReload` : relire les fichiers même si le chemin n'a pas changé. Marque le projet modifié.
    void setInstrumentDescription (neptune::core::InstrumentDescription d, bool forceReload = false) { setInstrumentDescription (selectedTrack(), std::move (d), forceReload); }
    void setInstrumentDescription (int track, neptune::core::InstrumentDescription, bool forceReload = false);
    // Même chose, mais annulable : passe par l'historique (une seule entrée par geste si `mergeKey` est non vide et que
    // uiBeginGesture/uiEndGesture entourent le geste). À utiliser pour toute modification venant de l'utilisateur.
    void uiEditInstrument (neptune::core::InstrumentDescription, std::string name, std::string mergeKey = {});
    void uiBeginGesture() { undo_.beginGesture(); }

    // Pistes : type de génération (Sampler / Synthé), pistes actives, patch du synthé.
    neptune::core::TrackType trackType (int track) const;
    bool trackActive (int track) const;
    int activeTrackCount() const;
    bool trackHasContent (int track) const; // instrument chargé ou patch modifié : un changement de type le perdrait (confirmation)
    neptune::core::SynthDescription synthDescription() const { return synthDescription (selectedTrack()); }
    neptune::core::SynthDescription synthDescription (int track) const; // copie
    // Module FM (type de piste FM) : description de la piste, édition annulable, remplacement par un patch importé.
    neptune::core::fm::FmDescription fmDescription() const { return fmDescription (selectedTrack()); }
    neptune::core::fm::FmDescription fmDescription (int track) const;
    void uiEditFm (neptune::core::fm::FmDescription, std::string name, std::string mergeKey = {}, bool presetChange = false);
    // Type Orgue : canaux des claviers, split, transposition (les tirettes et réglages sont des paramètres de la piste).
    neptune::core::organ::OrganDescription organDescription() const { return organDescription (selectedTrack()); }
    neptune::core::organ::OrganDescription organDescription (int track) const;
    void uiEditOrgan (neptune::core::organ::OrganDescription, std::string name, std::string mergeKey = {});
    // Édition du patch de la piste sélectionnée : annulable, fusionnée par geste.
    // `presetChange` : changement de preset (les notes en cours gardent l'ancien son : capture par voix).
    void uiEditSynth (neptune::core::SynthDescription, std::string name, std::string mergeKey = {}, bool presetChange = false);

    // ---- presets de synthé (PresetHost : agit sur la piste sélectionnée, si elle est de type Synthé) ----
    neptune::core::preset::Library& library() override { return *presetLibrary_; }
    std::string currentPresetId() const override;
    const neptune::core::preset::Preset* currentPreset() const override;
    std::uint32_t presetRevision() const override { return presetRevision_.load(); }
    void touchPresets() override { ++presetRevision_; }
    bool loadPreset (const std::string& id) override;
    bool stepPreset (int direction) override;
    bool isModified() const override;
    bool canSaveCurrent() const override;
    bool saveCurrent (std::string* error = nullptr) override;
    bool overwritePreset (const std::string& id, std::string* error = nullptr) override;
    bool saveCurrentAs (const std::string& name, const std::string& type, neptune::core::preset::Preset* out = nullptr,
                        std::string* error = nullptr) override;
    bool saveConfig (const neptune::core::preset::LibraryConfig&) override;
    const neptune::core::preset::LibraryConfig& presetConfig() const override { return presetLibrary_->config(); }
    bool presetsAvailable() const override
    {
        const auto t = trackType (selectedTrack());
        return t == neptune::core::TrackType::Synth || t == neptune::core::TrackType::Fm || t == neptune::core::TrackType::Organ;
    }
    bool acceptsPreset (const neptune::core::preset::Preset& p) const override;
    std::vector<std::string> presetKeys() const override;
    // Change le type de la piste : la configuration d'instrument est perdue (spec §1), les réglages de bande sont conservés.
    // Vide l'historique d'annulation.
    void uiSetTrackType (int track, neptune::core::TrackType);
    // Active la première piste libre (un synthé par défaut) et la sélectionne ; rend son indice, ou -1 si les 16 sont actives.
    int uiAddTrack();
    // Retire une piste (remise à l'état d'origine). Faux s'il n'en reste qu'une. Vide l'historique d'annulation.
    bool uiRemoveTrack (int track);

    // Inserts (modules stock) : 8 emplacements par piste, par bus et sur le Master. La source de vérité est la table des instances
    // vivantes ; leurs valeurs sont recopiées dans l'état à la sauvegarde. Le bypass d'un emplacement est un paramètre de l'hôte.
    using StripKind = neptune::core::StripKind;
    std::string insertType (StripKind, int index, int slot) const; // "" : emplacement vide
    neptune::core::ParamId insertBypassParam (StripKind, int index, int slot) const;
    bool insertBypassed (StripKind, int index, int slot) const;
    // Pose (ou retire, type vide) un module. Remet le bypass à zéro et vide l'historique d'annulation.
    void uiSetInsert (StripKind, int index, int slot, const std::string& type);
    struct InsertHandle
    {
        std::shared_ptr<neptune::core::InsertInstance> instance; // garde le module vivant (vu-mètres, paramètres)
        std::unique_ptr<ParamHost> host;                          // édition annulable des paramètres du module
    };
    InsertHandle openInsert (StripKind, int index, int slot); // instance nulle si l'emplacement est vide

    // Pistes parallèles (spec §1) : jusqu'à 32, en arbre. `parent` : 0..15 = piste, 16..47 = piste parallèle (16 + indice).
    bool parallelActive (int p) const;
    neptune::core::mixer::ParallelSettings parallelSettings (int p) const;
    int parallelDepth (int p) const; // 1 : fille directe d'une piste ; 0 si inactive
    std::vector<int> parallelChildren (int parentId) const;
    // Crée une piste parallèle sous `parent` ; rend son indice, ou -1 (pool plein, parent invalide). Vide l'historique d'annulation.
    int uiAddParallel (int parent);
    // Retire la piste et tous ses descendants ; rend le nombre de pistes retirées. Vide l'historique d'annulation.
    int uiRemoveParallel (int p);
    void uiEditParallel (int p, neptune::core::mixer::ParallelSettings, std::string name, std::string mergeKey = {});

    // Mixeur : réglages de bande (nom, canal MIDI, routage, envois, mode de pan), bus, master. Annulables.
    neptune::core::MixerState mixerState() const; // copie
    neptune::core::mixer::StripSettings trackSettings (int track) const;
    neptune::core::mixer::StripSettings busSettings (int bus) const;
    neptune::core::mixer::MasterSettings masterSettings() const;
    void uiEditTrack (int track, neptune::core::mixer::StripSettings, std::string name, std::string mergeKey = {});
    void uiEditBus (int bus, neptune::core::mixer::StripSettings, std::string name, std::string mergeKey = {});
    void uiEditMaster (neptune::core::mixer::MasterSettings, std::string name, std::string mergeKey = {});
    neptune::core::Engine::Meter trackMeter (int track) const { return engine_.trackMeter (track); }
    neptune::core::Engine::Meter busMeter (int bus) const { return engine_.busMeter (bus); }
    neptune::core::Engine::Meter parallelMeter (int p) const { return engine_.parallelMeter (p); }
    neptune::core::Engine::Meter masterMeter() const { return engine_.masterMeter(); }
    neptune::core::LoudnessMeter::Reading masterLoudness() const { return engine_.masterLoudness(); } // LUFS du Master
    void resetLoudness() { engine_.resetLoudness(); }

    // Jeu depuis l'interface (clavier de la matrice, bouton d'écoute) : thread de message ; appliqué au bloc audio suivant.
    // Toujours sur la piste sélectionnée.
    void uiNoteOn (int key, float velocity) { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::NoteOn, 0, key, velocity, selectedTrack() }); }
    void uiNoteOff (int key) { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::NoteOff, 0, key, 0.0f, selectedTrack() }); }
    // Même chose sur une piste précise (le clavier du synthé : une touche tenue doit s'arrêter sur la piste où elle a démarré).
    void uiNoteOnTrack (int track, int key, float velocity, int channel = 0) { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::NoteOn, channel, key, velocity, track }); }
    void uiNoteOffTrack (int track, int key, int channel = 0) { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::NoteOff, channel, key, 0.0f, track }); }
    void uiPreviewZone (int zoneIndex) { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::PreviewZone, 0, zoneIndex, 0.0f, selectedTrack() }); }
    void uiPreviewRelease() { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::PreviewRelease, 0, 0, 0.0f, selectedTrack() }); }
    // Notes jouées (piste, touche, vélocité après courbe) pour le curseur de la matrice ; thread de message.
    bool popNoteTrace (int& track, int& key, int& velocity)
    {
        neptune::core::Engine::NoteTrace t;
        if (! engine_.popNoteTrace (t)) return false;
        track = t.track; key = t.key; velocity = t.velocity;
        return true;
    }
    // Voix qui sonnent (moniteur) : thread de message.
    int readVoices (neptune::core::Engine::VoiceInfo* out, int maxOut) const { return engine_.readVoices (out, maxOut); }
    void uiPanic() { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::Panic, 0, 0, 0.0f, 0 }); }
    void setVoiceMonitor (bool on) { engine_.setVoiceMonitor (on); }
    std::uint64_t heldKeyWord (int track, int word) const noexcept { return engine_.heldKeyWord (track, word); }
    int totalActiveVoices() const noexcept { return engine_.totalActiveVoices(); } // toutes pistes ; lisible depuis l'interface
    void uiPreviewStop() { engine_.postUiEvent ({ neptune::core::Engine::UiEvent::Type::PreviewStop, 0, 0, 0.0f, selectedTrack() }); }
    void uiEndGesture() { undo_.endGesture(); }
    InstrumentStatus instrumentStatus() const { return instrumentStatus (selectedTrack()); }
    InstrumentStatus instrumentStatus (int track) const;
    // Voix éteintes faute de données du disque (streaming) depuis le chargement du plugin.
    std::uint32_t streamUnderruns() const noexcept { return engine_.streamUnderruns(); }
    // Change à chaque modification (instrument de n'importe quelle piste, réglages du mixeur, sélection) : l'interface compare
    // ce nombre au lieu de comparer tout l'état.
    std::uint32_t instrumentRevision() const noexcept { return revision_.load(); }

    // Taille mémorisée de la modale des potards (par type de terminal), enregistrée dans ui_state.json.
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const override { return uiState_.modalSize (t); }
    void setModalSize (neptune::core::uistate::Terminal, neptune::core::uistate::Size) override;
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

    void seedHostParameters (const std::vector<std::pair<neptune::core::ParamId, double>>&);
    bool notifyHostOnSeed_ = true;       // faux pendant le chargement d'un état (publishSynthPatch)
    neptune::core::ParameterSet params_; // déclaré avant le moteur : les paramètres survivent à tout ce qui les lit
    neptune::core::Engine engine_;
    neptune::core::InstanceProfile profile_;
    neptune::core::UndoManager undo_; // thread message uniquement
    mutable juce::CriticalSection instrumentLock_;
    neptune::core::MixerState mixer_;                                   // protégé par instrumentLock_
    std::array<InstrumentStatus, neptune::core::mixer::numTracks> status_; // protégé par instrumentLock_
    std::atomic<int> selectedTrack_ { 0 };
    std::atomic<std::uint32_t> revision_ { 0 };
    neptune::core::uistate::UiState uiState_;
    std::filesystem::path uiStateFile_;
    int probeTracks_ = 0;
    int probeOutputs_ = 0;
    HostProbe hostProbe_;

    void autosaveNow();
    class InstrumentLoader;
    void adoptMixerFromState (const std::optional<neptune::core::MixerState>&);
    class InsertHost;
    static int insertFlatIndex (StripKind, int index) noexcept;
    using InsertRow = std::array<std::shared_ptr<neptune::core::InsertInstance>, neptune::core::mixer::numInserts>;
    std::array<InsertRow, neptune::core::mixer::numTracks + neptune::core::mixer::numBuses + 1 + neptune::core::mixer::numParallels> insertInstances_; // protégé par instrumentLock_
    void dropParallel (int p);                                  // arrête une piste parallèle retirée : moteur, inserts, paramètres
    std::set<std::string> otherTracksPaths (int track) const;
    void setStatus (int track, InstrumentStatus);
    void pushMixerSettingsToEngine();
    void publishSynthPatch (int track, const neptune::core::SynthDescription&, bool freezeRunningVoices = false);
    void publishFmDescription (int track, const neptune::core::fm::FmDescription&);
    void publishOrganDescription (int track, const neptune::core::organ::OrganDescription&);
    void setTrackPresetId (int track, const std::string& id);
    std::unique_ptr<neptune::core::preset::Library> presetLibrary_;
    std::atomic<std::uint32_t> presetRevision_ { 1 };
    void resetTrackParameters (int track, bool includeMixer); // valeurs d'origine (instrument ; fader, pan, mute, solo, envois si includeMixer)
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
