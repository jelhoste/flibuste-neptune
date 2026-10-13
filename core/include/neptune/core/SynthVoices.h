#pragma once
// Voix du synthé soustractif (palier 1, premier jet) : oscillateur à bande limitée (PolyBLEP / PolyBLAMP), enveloppe de volume,
// filtre avec son enveloppe, polyphonie (poly / mono / legato, vol de voix). Aucune allocation côté audio.
#include <cstdint>
#include <vector>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"
#include "neptune/core/Synth.h"

namespace neptune::core
{
class SynthVoices
{
public:
    static constexpr int kMaxVoices = SynthDescription::maxVoicesLimit; // voix tenues
    static constexpr int kFadeSlots = 16;                               // voix volées qui s'éteignent
    static constexpr double kStealFadeSeconds = 0.005;
    static constexpr double kVoiceHeadroom = 0.3;                       // une dent de scie à pleine vélocité : -10,5 dBFS

    // Valeurs automatisables de la piste, lues une fois par bloc.
    struct Params
    {
        double cutoffHz = filterMaxHz;
        double resonance = 0.0;  // 0..1
        double tuneCents = 0.0;  // accordage de la piste (demi-tons et cents)
    };

    void prepare (double sampleRate) noexcept;
    // Patch adopté par le thread audio (nul : silence). Les voix en cours continuent avec les nouveaux réglages.
    void setPatch (const SynthPatch* patch) noexcept;

    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void panic() noexcept;       // coupe tout en fondu et oublie les touches tenues
    void releaseAll() noexcept;  // MIDI All Notes Off : relâche les notes tenues
    void allSoundOff() noexcept; // coupe net (changement de type de piste, préparation)

    // Ajoute le rendu dans `left` et `right` (nuls permis pour `right`). Le gain est interpolé de gainStart à gainEnd.
    void render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const Params& params) noexcept;

    struct VoiceInfo
    {
        int key = 0, channel = 0, velocity = 0; // vélocité 1..127
        bool releasing = false, fading = false;
        float level = 0.0f;                     // vélocité x enveloppe, 0..1
        std::uint64_t order = 0;
    };
    template <typename F>
    void forEachVoice (F&& f) const noexcept
    {
        for (const auto& v : voices_)
            if (v.active) f (describe (v));
    }

    int activeVoices() const noexcept;
    int heldVoices() const noexcept; // ni relâchées ni en fondu de vol : comptées dans la limite

private:
    struct Voice
    {
        bool active = false, releasing = false, stolen = false;
        int channel = 0, key = 0;
        std::uint64_t order = 0;
        double phase = 0.0;
        double note = 60.0;        // hauteur courante, en demi-tons MIDI (glisse vers targetNote)
        double targetNote = 60.0;
        double glideStep = 0.0;    // demi-tons par bloc de contrôle (0 : pas de glissando)
        float velocity = 1.0f;
        EnvelopeRunner env, fenv;
        FilterState fstate;
        FilterCoefs fcoef;
        float fenvValue = 0.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        float level = 0.0f;
    };

    VoiceInfo describe (const Voice& v) const noexcept;
    void steal (Voice& v) noexcept;
    void cutAll() noexcept;
    void cutKey (int channel, int key) noexcept;
    bool retargetHeld (int newKey) noexcept;
    Voice* allocate() noexcept;
    Voice* chooseVictim (int newKey, bool& dropNew) noexcept;
    void startNote (int channel, int key, float velocity) noexcept;
    void pushHeld (int channel, int key, float velocity) noexcept;
    void removeHeld (int channel, int key) noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, const Params& p) noexcept;

    std::vector<Voice> voices_ = std::vector<Voice> (kMaxVoices + kFadeSlots);
    const SynthPatch* patch_ = nullptr;
    double sampleRate_ = 48000.0;
    std::uint64_t counter_ = 0;
    struct HeldKey { int channel = 0, key = 0; float velocity = 0.0f; };
    static constexpr int kMaxHeldKeys = 16;
    HeldKey held_[kMaxHeldKeys] {};  // touches tenues (mono / legato), la plus récente en dernier
    int numHeld_ = 0;
};
} // namespace neptune::core
