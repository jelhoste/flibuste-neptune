#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/HandoffSlot.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/InstrumentSlot.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"
#include "neptune/core/SamplerVoices.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Synth.h"
#include "neptune/core/SynthVoices.h"

namespace neptune::core
{
// Frontière unique entre le cœur et les adaptateurs (JUCE/CLAP, tests, futur web).
// Palier 1 : N pistes, Sampler ou Synthé (chacune son instrument ou son patch, ses voix, son canal MIDI), 16 bus, un Master ; sorties directes sur
// jusqu'à 32 paires stéréo. Les voix d'une piste ne sont jamais celles d'une autre ; le thread disque et ses emplacements de
// streaming sont, eux, partagés (pool).
enum class StripKind { Track, Bus, Master, Parallel };

class Engine
{
public:
    static constexpr int kTracks = mixer::numTracks;
    static constexpr int kBuses = mixer::numBuses;
    static constexpr int kInserts = mixer::numInserts;
    static constexpr int kParallels = mixer::numParallels;

    Engine() noexcept;

    void prepare (double sampleRate, int maxBlockSize) noexcept;

    // Notes de l'hôte : réparties entre les pistes selon leur canal MIDI (0 = toutes). `channel` : 0..15.
    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    // Notes adressées à UNE piste (clavier de l'interface), sans filtre de canal.
    void noteOnTrack (int track, int channel, int key, float velocity) noexcept;
    void noteOffTrack (int track, int channel, int key) noexcept;
    void panic() noexcept;      // MIDI All Sound Off / bouton « tout arrêter » : toutes les pistes
    void releaseAll() noexcept; // MIDI All Notes Off
    void setParameter (std::uint32_t id, double value) noexcept;
    void process (float* const* out, int numChannels, int numFrames) noexcept;
    // Profil Live/Studio effectif de l'instance ; appelé par l'adaptateur avant chaque bloc (sans coût). Aucun effet audible pour l'instant.
    void setProfile (PerformanceProfile p) noexcept { profile_ = p; }
    PerformanceProfile profile() const noexcept { return profile_; }

    // Paramètres lus à chaque bloc (SamplerParams.h, ProbePool.h, Mixer.h). Appelé hors temps réel, avant l'audio.
    void attachParameters (const ParameterSet* parameters) noexcept;

    // Instrument d'une piste : thread de message. L'audio l'adopte au début du bloc suivant ; les voix en cours s'arrêtent.
    // Démarre le thread disque (et ses tampons) la première fois qu'un instrument en streaming est publié.
    void publishInstrument (int track, std::shared_ptr<const Instrument> instrument);
    void publishInstrument (std::shared_ptr<const Instrument> instrument) { publishInstrument (0, std::move (instrument)); }
    void collectGarbage(); // thread de message, régulièrement : détruit ce que le thread audio a rendu

    // Insert d'un emplacement (piste, bus ou Master ; `index` ignoré pour le Master ; `slot` 0..7) : thread de message.
    // Le module doit être préparé (createInsert avec sampleRate() / maxBlockSize()). Nul : l'emplacement devient vide.
    // L'audio l'adopte au début du bloc suivant ; le thread audio en devient le seul utilisateur du DSP.
    void setInsert (StripKind kind, int index, int slot, std::shared_ptr<const InsertInstance> insert);
    int maxBlockSize() const noexcept { return maxBlockSize_; }

    // Type de génération d'une piste (Sampler / Synthé) et patch du synthé : thread de message, pris en compte au bloc suivant.
    // Un changement de type coupe net les voix de la piste.
    void setTrackType (int track, TrackType type) noexcept;
    void publishSynth (int track, std::shared_ptr<const SynthPatch> patch);
    // Piste active : une piste inactive ne reçoit aucune note et ne produit rien (le mixeur ne l'affiche pas). Par défaut, toutes.
    void setTrackActive (int track, bool active) noexcept;

    // Réglages non automatisables du mixeur (routage, canal MIDI, mode de pan...) : thread de message, pris en compte au bloc suivant.
    void setTrackSettings (int track, const mixer::StripSettings&) noexcept;
    void setBusSettings (int bus, const mixer::StripSettings&) noexcept;
    // Pistes parallèles : `parent` 0..15 = piste, 16..47 = piste parallèle ; `tap` : point de captation dans la chaîne du parent.
    // Une piste parallèle dont le parent est inactif ou qui forme une boucle n'est jamais traitée.
    // `followParentVolume` : le gain de fader du parent s'applique aussi à cette piste (captation avant fader seulement).
    void setParallel (int p, bool active, int parent, mixer::TapPoint tap, bool followParentVolume = false) noexcept;
    void setParallelSettings (int p, const mixer::StripSettings&) noexcept;
    void setMasterSettings (const mixer::MasterSettings&) noexcept;

    // Événements venus de l'interface (clavier de la matrice, bouton d'écoute des zones). Thread de message UNIQUEMENT (un seul
    // producteur) ; le thread audio les applique au début du bloc suivant. Rend false si la file est pleine.
    struct UiEvent
    {
        enum class Type : std::uint8_t { NoteOn, NoteOff, PreviewZone, PreviewRelease, PreviewStop, Panic, ReleaseAll } type = Type::NoteOn;
        int channel = 0, key = 0;      // NoteOn / NoteOff ; PreviewZone : `key` = indice de zone
        float velocity = 0.0f;
        int track = 0;                 // piste visée (NoteOn, NoteOff, Preview*) ; Panic / ReleaseAll : toutes
    };
    bool postUiEvent (const UiEvent&) noexcept;

    // Notes jouées, pour l'interface (curseur de vélocité de la matrice) : piste, touche et vélocité 1..127 APRÈS la courbe de
    // l'instrument (celle qui choisit la couche). Producteur : thread audio ; consommateur : thread de message.
    struct NoteTrace { int track = 0, key = 0, velocity = 0; };
    bool popNoteTrace (NoteTrace&) noexcept;

    // Voix qui sonnent, pour le moniteur de l'interface : copie cohérente du dernier bloc (verrou de séquence : le thread audio
    // n'attend jamais). Thread de message ; rend le nombre de voix copiées.
    using VoiceInfo = SamplerVoices::VoiceInfo;
    static constexpr int kVoiceSnapshot = 512;
    int readVoices (VoiceInfo* out, int maxOut) const noexcept; // VoiceInfo::track est rempli
    // Le moniteur n'est alimenté que lorsqu'il est demandé (thread de message) : éteint, le thread audio ne publie rien.
    void setVoiceMonitor (bool on) noexcept { voiceMonitor_.store (on, std::memory_order_relaxed); if (! on) voiceCount_.store (0, std::memory_order_relaxed); }
    bool voiceMonitor() const noexcept { return voiceMonitor_.load (std::memory_order_relaxed); }

    // Niveaux de crête du dernier bloc (0..~1), pour les vu-mètres ; thread de message.
    struct Meter { float peakL = 0.0f, peakR = 0.0f, rmsL = 0.0f, rmsR = 0.0f; };
    Meter trackMeter (int track) const noexcept;
    Meter busMeter (int bus) const noexcept;
    Meter parallelMeter (int p) const noexcept;
    Meter masterMeter() const noexcept;

    int activeVoices() const noexcept;
    int activeVoices (int track) const noexcept
    {
        const auto& t = tracks_[static_cast<std::size_t> (track)];
        return t.voices.activeVoices() + t.synth.activeVoices();
    }
    double sampleRate() const noexcept { return sampleRate_; }
    // Sous-alimentations du streaming depuis le démarrage (une voix s'est éteinte faute de données du disque).
    std::uint32_t streamUnderruns() const noexcept { return streamer_.underruns(); }
    int activeNotes() const noexcept { return activeNotes_; }

private:
    struct AtomicSend
    {
        std::atomic<int> destination { 0 }, tap { 3 }, mute { 0 };
        std::atomic<float> pan { 0.0f };
    };
    struct AtomicStrip
    {
        std::atomic<int> midiChannel { 0 }, mainDest { 0 }, mainEnabled { 1 }, directOut { 0 }, panMode { 0 };
        std::array<AtomicSend, mixer::numSends> sends;
        void load (const mixer::StripSettings&) noexcept;
    };
    struct ParamIndices
    {
        static constexpr std::size_t N = ParameterSet::npos;
        std::size_t volume = N, pan = N, mute = N, solo = N;
        std::size_t sendLevel[mixer::numSends];
        std::size_t sampler[mixer::kSamplerKinds];
        std::size_t synth[mixer::kSynthKinds];
        ParamIndices() noexcept { for (auto& v : sendLevel) v = N; for (auto& v : sampler) v = N; for (auto& v : synth) v = N; }
    };
    // Un emplacement d'insert : l'instance (remise au thread audio) et l'index de son paramètre de bypass (celui de l'hôte).
    struct InsertSlot
    {
        HandoffSlot<InsertInstance> slot;
        std::size_t bypass = ParameterSet::npos;
    };
    using Inserts = std::array<InsertSlot, mixer::numInserts>;

    struct Track
    {
        Inserts inserts;
        InstrumentSlot slot;
        SamplerVoices voices;
        HandoffSlot<SynthPatch> synthSlot;
        SynthVoices synth;
        std::atomic<int> type { 0 };   // TrackType
        std::atomic<int> active { 1 };
        int appliedType = 0, appliedActive = 1; // thread audio : ce qui a déjà été pris en compte
        AtomicStrip config;
        ParamIndices idx;
        float lastGain = 1.0f;                      // gain de l'instrument (lissé)
        float lastFader = 1.0f, lastPanL = 1.0f, lastPanR = 1.0f;
        std::array<float, mixer::numSends> lastSend {};
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
    };
    struct Parallel
    {
        Inserts inserts;
        AtomicStrip config;
        ParamIndices idx;
        std::atomic<int> active { 0 }, parent { 0 }, tap { 3 }, follow { 0 };
        float lastFader = 1.0f, lastPanL = 1.0f, lastPanR = 1.0f;
        std::array<float, mixer::numSends> lastSend {};
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
    };
    struct Bus
    {
        Inserts inserts;
        AtomicStrip config;
        ParamIndices idx;
        float lastFader = 1.0f, lastPanL = 1.0f, lastPanR = 1.0f;
        std::array<float, mixer::numSends> lastSend {};
        bool hasInput = false;
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
    };

    void processChunk (float* const* out, int numChannels, int frames) noexcept;
    void drainUiEvents() noexcept;
    struct Stereo;
    struct Level;
    void adoptInserts() noexcept;
    // --- traitement d'une bande (un bloc de `chunkN_` échantillons)
    bool runInserts (Inserts&, Stereo&) noexcept;
    static bool hasInserts (const Inserts&) noexcept;
    void applyFaderPan (const Stereo& in, Stereo& postFader, Stereo& fin, const AtomicStrip&, const ParamIndices&, float& lastFader,
                        float& lastPanL, float& lastPanR, bool muted) noexcept;
    void meterOf (const Stereo& out, std::atomic<float>& pkL, std::atomic<float>& pkR, std::atomic<float>& rmsL, std::atomic<float>& rmsR) noexcept;
    void routeStrip (const AtomicStrip&, const ParamIndices&, std::array<float, mixer::numSends>& lastSend, const Stereo& pre, const Stereo& post,
                     const Stereo& postFader, const Stereo& out, bool isBus, int busIndex, bool sumIntoParent) noexcept;
    void processParallel (int p, int depth, bool ancestorSolo, const Stereo& parentPre, const Stereo& parentPost, const Stereo& parentFader,
                          const Stereo& parentFin, Stereo& parentSum, float parentGainFrom, float parentGainTo) noexcept;
    std::pair<float*, float*> channelPair (int output) const noexcept;
    Inserts* insertsOf (StripKind, int index) noexcept;
    void pushTrace (int track, int key, float shapedVelocity) noexcept;
    void publishVoices() noexcept;
    double param (std::size_t index, double fallback) const noexcept
    {
        return params_ != nullptr && index != ParameterSet::npos ? params_->getAt (index) : fallback;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    int activeNotes_ = 0;
    PerformanceProfile profile_ = PerformanceProfile::Live;

    static constexpr std::size_t kUiQueueSize = 256; // puissance de 2
    std::array<UiEvent, kUiQueueSize> uiQueue_ {};
    std::atomic<std::size_t> uiHead_ { 0 }, uiTail_ { 0 }; // écrit par l'interface / par l'audio
    // instantané des voix (2 mots de 64 bits par voix), protégé par un compteur de séquence (impair = écriture en cours)
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t> (kVoiceSnapshot) * 2u> voiceWords_ {};
    std::atomic<std::uint32_t> voiceSeq_ { 0 };
    std::atomic<int> voiceCount_ { 0 };
    std::atomic<bool> voiceMonitor_ { false };
    static constexpr std::size_t kTraceSize = 64;           // puissance de 2
    std::array<std::atomic<std::uint32_t>, kTraceSize> trace_ {};
    std::atomic<std::size_t> traceHead_ { 0 }, traceTail_ { 0 };

    DiskStreamer streamer_;
    std::array<Track, kTracks> tracks_;
    std::array<Bus, kBuses> buses_;
    std::atomic<int> masterOutput_ { 1 }, masterPanMode_ { 0 };
    const ParameterSet* params_ = nullptr;
    std::size_t masterVolume_ = ParameterSet::npos, masterPan_ = ParameterSet::npos, masterMute_ = ParameterSet::npos,
                masterTrim_ = ParameterSet::npos, masterMono_ = ParameterSet::npos;
    Inserts masterInserts_;
    std::atomic<bool> insertsDirty_ { false }; // un insert a été publié : le thread audio les adopte au prochain bloc
    float lastMasterTrim_ = 1.0f, lastMasterFader_ = 1.0f, lastMasterPanL_ = 1.0f, lastMasterPanR_ = 1.0f;
    std::atomic<float> masterPeakL_ { 0.0f }, masterPeakR_ { 0.0f }, masterRmsL_ { 0.0f }, masterRmsR_ { 0.0f };

    // tampons de mixage (alloués dans prepare)
    struct Stereo { std::vector<float> l, r; void resize (std::size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); } };
    struct Level { Stereo pre, post, fader, fin, sum; void resize (std::size_t n) { pre.resize (n); post.resize (n); fader.resize (n); fin.resize (n); sum.resize (n); } };
    Stereo raw_, inserted_, postFader_, final_, sum0_, master_; // niveau 0 : la piste
    std::vector<Level> levels_;                                 // niveau d >= 1 : levels_[d - 1], pour les pistes parallèles
    std::array<Stereo, kBuses> busIn_;
    std::array<Parallel, kParallels> parallels_;
    // contexte du bloc en cours
    float* const* chunkOut_ = nullptr;
    int chunkChannels_ = 0, chunkN_ = 0;
    bool anySolo_ = false;
    bool busSolo_ = false;
    std::array<int, mixer::numTracks + mixer::numParallels> firstChild_ {};
    std::array<int, mixer::numParallels> nextSibling_ {};
    std::array<unsigned char, mixer::numTracks + mixer::numParallels> subtreeSolo_ {}, subtreeBusy_ {};
};
} // namespace neptune::core
