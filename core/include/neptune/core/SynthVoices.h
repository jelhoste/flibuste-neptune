#pragma once
// Voix du synthé soustractif (spec-modules §2) : 3 oscillateurs (unisson stéréo, synchro dure), sous-oscillateur, bruit, mixeur,
// 2 filtres stéréo avec leurs enveloppes, enveloppe de volume et de pitch, polyphonie (poly / mono / legato, vol de voix).
// Oscillateurs à bande limitée (PolyBLEP / PolyBLAMP / tables d'onde par octave). Aucune allocation côté audio.
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
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
    static constexpr double kVoiceHeadroom = 0.15;                      // marge par voix (une dent de scie à pleine vélocité : ~-15 dBFS)

    // Valeurs automatisables de la piste, lues une fois par bloc.
    struct Params
    {
        double cutoffHz[2] = { filterMaxHz, filterMaxHz };
        double resonance[2] = { 0.0, 0.0 };  // 0..1
        double tuneCents = 0.0;              // accordage de la piste (demi-tons et cents)
        double level[numSources] = { 100.0, 0.0, 0.0, 0.0, 0.0 }; // %
        double fineCents[numOscillators] = { 0.0, 0.0, 0.0 };
        double pulse[numOscillators] = { 50.0, 50.0, 50.0 };      // % (largeur d'impulsion)
        double unisonDetune[numOscillators] = { 20.0, 20.0, 20.0 }; // cents, voix extrêmes
        double pitchEnvDepth = 0.0;          // demi-tons à pleine enveloppe
        // Temps et sustain d'une enveloppe (paramètres de la piste) ; -1 : ceux du patch. Pris au Note On (attaque, décroissance,
        // sustain) et au Note Off (relâchement).
        struct EnvParams { double attackMs = -1.0, decayMs = -1.0, sustain = -1.0 /* 0..1 */, releaseMs = -1.0; };
        EnvParams ampEnv, filterEnv[2];
        double glideMs = -1.0;               // -1 : celui du patch
        double filterBalance = -1.0;         // 0..1, mode parallèle ; -1 : celui du patch
    };

    void prepare (double sampleRate) noexcept;
    // Patch adopté par le thread audio (nul : silence). Les voix en cours continuent avec les nouveaux réglages.
    void setPatch (const SynthPatch* patch) noexcept;

    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void panic() noexcept;       // coupe tout en fondu et oublie les touches tenues
    void releaseAll() noexcept;  // MIDI All Notes Off : relâche les notes tenues
    void allSoundOff() noexcept; // coupe net (changement de type de piste, préparation)

    // Ajoute le rendu dans `left` et `right` (`right` peut être nul : mélange mono). Le gain est interpolé de gainStart à gainEnd.
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

    // Capture par voix : génération la plus ancienne parmi les voix figées (max si aucune) et génération du patch adopté ; publiées
    // par l'audio pour que le publieur sache quels patchs garder en vie (SynthPatchKeeper).
    std::uint64_t oldestFrozenGeneration() const noexcept { return oldestFrozen_.load (std::memory_order_acquire); }
    std::uint64_t adoptedGeneration() const noexcept { return adopted_.load (std::memory_order_acquire); }
    int frozenVoices() const noexcept;
    int soundingVoices() const noexcept; // hors fondu de vol : comptées par la limite globale (Engine)
    bool weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept; // voir SamplerVoices
    void stealWeakest() noexcept;

    int activeVoices() const noexcept;
    int heldVoices() const noexcept; // ni relâchées ni en fondu de vol : comptées dans la limite

private:
    struct Voice
    {
        bool active = false, releasing = false, stolen = false;
        int channel = 0, key = 0;
        std::uint64_t order = 0;
        std::array<std::array<double, maxUnison>, numOscillators> phase {};
        std::array<double, maxUnison> masterLast {}; // dernière phase de l'oscillateur 1 du bloc précédent (détection des tours)
        double subPhase = 0.0;
        std::uint32_t rng = 1;
        float pink[3] = { 0.0f, 0.0f, 0.0f };
        double note = 60.0;        // hauteur courante, en demi-tons MIDI (glisse vers targetNote)
        double targetNote = 60.0;
        double glideStep = 0.0;    // demi-tons par bloc de contrôle (0 : pas de glissando)
        float velocity = 1.0f;
        EnvelopeRunner env, penv;
        std::array<EnvelopeRunner, 2> fenv;
        std::array<std::array<FilterState, 2>, 2> fstate {}; // [filtre][canal]
        std::array<FilterCoefs, 2> fcoef;
        std::array<float, 2> fenvValue { 0.0f, 0.0f };
        float fade = 1.0f, fadeStep = 0.0f;
        float level = 0.0f;
        // capture par voix : une voix figée garde son patch et les valeurs de paramètres d'avant le changement de preset
        bool frozen = false;
        const SynthPatch* patch = nullptr;
        Params frozenParams;
        float frozenGain = 1.0f;
    };

    VoiceInfo describe (const Voice& v) const noexcept;
    void steal (Voice& v) noexcept;
    void cutAll() noexcept;
    void cutKey (int channel, int key) noexcept;
    bool retargetHeld (int newKey) noexcept;
    Voice* allocate() noexcept;
    Voice* chooseVictim (int newKey, bool& dropNew) noexcept;
    void startNote (int channel, int key, float velocity) noexcept;
    void initPhases (Voice& v) noexcept;
    void pushHeld (int channel, int key, float velocity) noexcept;
    void removeHeld (int channel, int key) noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, const Params& p) noexcept;

    static EnvelopeTiming timingOf (const EnvelopeRuntime&, const Params::EnvParams&) noexcept;
    void noteOffVoice (Voice& v) noexcept;
    void publishGenerations() noexcept;
    const SynthPatch& patchOf (const Voice& v) const noexcept { return v.frozen && v.patch != nullptr ? *v.patch : *patch_; }

    std::vector<Voice> voices_ = std::vector<Voice> (kMaxVoices + kFadeSlots);
    const SynthPatch* patch_ = nullptr;
    Params lastParams_;                    // valeurs du dernier bloc rendu (point de départ du figement)
    float lastGainEnd_ = 1.0f;
    std::atomic<std::uint64_t> oldestFrozen_ { std::numeric_limits<std::uint64_t>::max() }, adopted_ { 0 };
    double sampleRate_ = 48000.0;
    std::uint64_t counter_ = 0;
    struct HeldKey { int channel = 0, key = 0; float velocity = 0.0f; };
    static constexpr int kMaxHeldKeys = 16;
    HeldKey held_[kMaxHeldKeys] {};  // touches tenues (mono / legato), la plus récente en dernier
    int numHeld_ = 0;
};
} // namespace neptune::core
