#pragma once
// Voix du Sampler (palier 1) : lecture polyphonique de zones avec transposition (interpolation de Hermite), modes
// one-shot / boucle, fondu de relâchement, vol de voix. Aucune allocation : tout est dimensionné à la construction.
#include <array>
#include <cstdint>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/Envelope.h"
#include "neptune/core/Instrument.h"

namespace neptune::core
{
class SamplerVoices
{
public:
    static constexpr int kMaxVoices = 64;  // notes simultanées (limite de voix ; un réglage par instrument viendra)
    static constexpr int kFadeSlots = 16;  // emplacements supplémentaires pour les voix volées qui s'éteignent
    static constexpr double kStealFadeSeconds = 0.003;

    static_assert (kMaxVoices + kFadeSlots == DiskStreamer::kSlots, "un emplacement de streaming par voix");

    void prepare (double sampleRate) noexcept;
    // Streaming disque (facultatif) : les échantillons dont seul le début est en mémoire y sont lus. Nul = jamais de streaming.
    void setStreamer (DiskStreamer* streamer) noexcept { streamer_ = streamer; }
    // Remplace l'instrument. Les voix dont la zone existe encore (même indice, même sample) continuent avec les nouveaux
    // réglages (édition en direct) ; les autres s'arrêtent net : les zones de l'ancien instrument vont disparaître.
    void setInstrument (const Instrument* instrument) noexcept;

    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void allSoundOff() noexcept;

    // Ajoute le rendu dans `left`/`right` (right peut être nul : mélange mono). Gain interpolé linéairement
    // de gainStart à gainEnd sur le bloc. masterTuneCents transpose toutes les voix.
    void render (float* left, float* right, int numFrames, float gainStart, float gainEnd, double masterTuneCents) noexcept;

    int activeVoices() const noexcept;    // voix qui sonnent (y compris en relâchement)
    int heldVoices() const noexcept;      // voix comptées dans la limite (ni relâchées ni en fondu de vol)

private:
    struct Voice
    {
        const Zone* zone = nullptr;
        bool active = false;
        bool releasing = false; // relâchement (Note Off en mode boucle)
        bool stolen = false;    // en fondu rapide après un vol
        int zoneIndex = 0;
        int channel = 0, key = 0;
        std::uint64_t order = 0;
        double pos = 0.0;       // en échantillons du fichier
        double baseRatio = 1.0; // avance par échantillon de sortie, sans l'accordage global
        float velocity = 1.0f;  // le gain de la zone est relu à chaque bloc (édition en direct)
        float layerGain = 1.0f; // fondu entre couches de vélocité (fixé au Note On)
        EnvelopeRunner env;
        float fade = 1.0f, fadeStep = 0.0f; // fondu rapide d'une voix volée
        bool streaming = false;             // un emplacement de DiskStreamer est réservé à cette voix
        std::uint32_t streamGen = 0;
    };

    void steal (Voice& v) noexcept;
    void discard (Voice& v) noexcept; // libère son flux disque s'il y en a un, puis remet la voix à zéro
    template <bool Streamed>
    void renderVoiceT (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept;
    void startVoice (int channel, int key, float velocity, const Zone& zone, float layerGain = 1.0f) noexcept;
    void bindZone (Voice& v, const Zone& zone) noexcept;
    Voice* allocate() noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept;

    std::array<Voice, kMaxVoices + kFadeSlots> voices_ {};
    const Instrument* instrument_ = nullptr;
    DiskStreamer* streamer_ = nullptr;
    double sampleRate_ = 48000.0;
    std::uint64_t counter_ = 0;
    std::array<float, 128> heldVelocity_ {}; // vélocité du dernier Note On par touche (zones déclenchées au relâchement)
};
} // namespace neptune::core
