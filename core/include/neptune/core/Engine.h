#pragma once
#include <array>
#include <atomic>
#include <cstdint>

#include <memory>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/InstrumentSlot.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"
#include "neptune/core/SamplerVoices.h"

namespace neptune::core
{
// Frontière unique entre le cœur et les adaptateurs (JUCE/CLAP, tests, futur web).
// Palier 1 : lecture de samples (SamplerVoices) sur la sortie 1 (canaux 0 et 1) ; les autres sorties restent silencieuses.
class Engine
{
public:
    Engine() noexcept { voices_.setStreamer (&streamer_); }

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void panic() noexcept { activeNotes_ = 0; voices_.panic(); }      // MIDI All Sound Off / bouton « tout arrêter »
    void releaseAll() noexcept { activeNotes_ = 0; voices_.releaseAll(); } // MIDI All Notes Off
    void setParameter (std::uint32_t id, double value) noexcept;
    void process (float* const* out, int numChannels, int numFrames) noexcept;
    // Profil Live/Studio effectif de l'instance ; appelé par l'adaptateur avant chaque bloc (sans coût). Aucun effet audible pour l'instant.
    void setProfile (PerformanceProfile p) noexcept { profile_ = p; }
    PerformanceProfile profile() const noexcept { return profile_; }

    // Paramètres lus à chaque bloc (gain et accordage globaux, SamplerParams.h). Appelé hors temps réel, avant l'audio.
    void attachParameters (const ParameterSet* parameters) noexcept;

    // Instrument : thread de message. L'audio l'adopte au début du bloc suivant ; les voix en cours s'arrêtent.
    // Démarre le thread disque (et ses tampons) la première fois qu'un instrument en streaming est publié.
    void publishInstrument (std::shared_ptr<const Instrument> instrument);
    void collectGarbage() { slot_.collect(); } // thread de message, régulièrement

    // Événements venus de l'interface (clavier de la matrice, bouton d'écoute des zones). Thread de message UNIQUEMENT (un seul
    // producteur) ; le thread audio les applique au début du bloc suivant. Rend false si la file est pleine.
    struct UiEvent
    {
        enum class Type : std::uint8_t { NoteOn, NoteOff, PreviewZone, PreviewRelease, PreviewStop, Panic, ReleaseAll } type = Type::NoteOn;
        int channel = 0, key = 0;      // NoteOn / NoteOff ; PreviewZone : `key` = indice de zone
        float velocity = 0.0f;
    };
    bool postUiEvent (const UiEvent&) noexcept;

    // Notes jouées, pour l'interface (curseur de vélocité de la matrice) : touche et vélocité 1..127 APRÈS la courbe de
    // l'instrument (celle qui choisit la couche). Producteur : thread audio ; consommateur : thread de message.
    struct NoteTrace { int key = 0, velocity = 0; };
    bool popNoteTrace (NoteTrace&) noexcept;

    // Voix qui sonnent, pour le moniteur de l'interface : copie cohérente du dernier bloc (verrou de séquence : le thread audio
    // n'attend jamais). Thread de message ; rend le nombre de voix copiées.
    using VoiceInfo = SamplerVoices::VoiceInfo;
    static constexpr int kVoiceSnapshot = SamplerVoices::kMaxVoices + SamplerVoices::kFadeSlots;
    int readVoices (VoiceInfo* out, int maxOut) const noexcept;
    // Le moniteur n'est alimenté que lorsqu'il est demandé (thread de message) : éteint, le thread audio ne publie rien.
    void setVoiceMonitor (bool on) noexcept { voiceMonitor_.store (on, std::memory_order_relaxed); if (! on) voiceCount_.store (0, std::memory_order_relaxed); }
    bool voiceMonitor() const noexcept { return voiceMonitor_.load (std::memory_order_relaxed); }

    int activeVoices() const noexcept { return voices_.activeVoices(); }
    double sampleRate() const noexcept { return sampleRate_; }
    // Sous-alimentations du streaming depuis le démarrage (une voix s'est éteinte faute de données du disque).
    std::uint32_t streamUnderruns() const noexcept { return streamer_.underruns(); }
    int activeNotes() const noexcept { return activeNotes_; }

private:
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
    void publishVoices() noexcept;
    static constexpr std::size_t kTraceSize = 64;           // puissance de 2
    std::array<std::atomic<std::uint16_t>, kTraceSize> trace_ {};
    std::atomic<std::size_t> traceHead_ { 0 }, traceTail_ { 0 };

    InstrumentSlot slot_;
    DiskStreamer streamer_;
    SamplerVoices voices_;
    const ParameterSet* params_ = nullptr;
    std::size_t gainIndex_ = ParameterSet::npos, tuneSemiIndex_ = ParameterSet::npos, tuneCentsIndex_ = ParameterSet::npos;
    std::size_t filterIndex_[4] = { ParameterSet::npos, ParameterSet::npos, ParameterSet::npos, ParameterSet::npos }; // coupure 1, résonance 1, coupure 2, résonance 2
    float lastGain_ = 1.0f;
};
} // namespace neptune::core
