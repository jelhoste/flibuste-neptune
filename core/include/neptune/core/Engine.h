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
        enum class Type : std::uint8_t { NoteOn, NoteOff, PreviewZone, PreviewRelease, PreviewStop } type = Type::NoteOn;
        int channel = 0, key = 0;      // NoteOn / NoteOff ; PreviewZone : `key` = indice de zone
        float velocity = 0.0f;
    };
    bool postUiEvent (const UiEvent&) noexcept;

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

    InstrumentSlot slot_;
    DiskStreamer streamer_;
    SamplerVoices voices_;
    const ParameterSet* params_ = nullptr;
    std::size_t gainIndex_ = ParameterSet::npos, tuneSemiIndex_ = ParameterSet::npos, tuneCentsIndex_ = ParameterSet::npos;
    float lastGain_ = 1.0f;
};
} // namespace neptune::core
