#pragma once
// Voix du Sampler (palier 1) : lecture polyphonique de zones avec transposition (interpolation de Hermite), modes
// one-shot / boucle, fondu de relâchement, vol de voix. Aucune allocation : tout est dimensionné à la construction.
#include <array>
#include <cstdint>

#include "neptune/core/Instrument.h"

namespace neptune::core
{
class SamplerVoices
{
public:
    static constexpr int kMaxVoices = 64;  // notes simultanées (limite de voix ; un réglage par instrument viendra)
    static constexpr int kFadeSlots = 16;  // emplacements supplémentaires pour les voix volées qui s'éteignent
    static constexpr double kStealFadeSeconds = 0.003;

    void prepare (double sampleRate) noexcept;
    // Remplace l'instrument : toutes les voix s'arrêtent net (les zones de l'ancien peuvent disparaître).
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
        int channel = 0, key = 0;
        std::uint64_t order = 0;
        double pos = 0.0;       // en échantillons du fichier
        double baseRatio = 1.0; // avance par échantillon de sortie, sans l'accordage global
        float gain = 1.0f;
        float env = 1.0f, envStep = 0.0f;
    };

    void startRelease (Voice& v, double seconds) noexcept;
    Voice* allocate() noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept;

    std::array<Voice, kMaxVoices + kFadeSlots> voices_ {};
    const Instrument* instrument_ = nullptr;
    double sampleRate_ = 48000.0;
    std::uint64_t counter_ = 0;
};
} // namespace neptune::core
