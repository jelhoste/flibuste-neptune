#pragma once
// Voix du module FM (spec-modules §17) : polyphonie (poly / mono / legato, vol de voix, glissando), unisson (voix, détune, répartition
// stéréo), macros (brillance, vitesse des enveloppes, feedback), niveaux d'opérateur et contrôleurs (pitch bend, molette).
// Le son vient de msfa (FmEngine.h) ; ce fichier n'en dépend pas. Aucune allocation côté audio : les notes sont réservées par
// `reserve()` (thread de message) avant que la piste ne passe en type FM.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/FmEngine.h"
#include "neptune/core/FmPatch.h"
#include "neptune/core/Instrument.h" // PolyphonyMode, VoiceStealMode

namespace neptune::core::fm
{
constexpr int kMaxVoices = 32;
constexpr int kMaxUnison = 8;
constexpr int kMaxGlideMs = 2000;

// Réglages sauvegardés dans le projet (par piste FM) : le patch DX7 et les paramètres de « function » du DX7 plus unisson.
struct FmDescription
{
    Patch patch = Patch::initVoice();
    int masterTuneCents = 0;                // -100..+100 : accordage général
    PolyphonyMode polyphony = PolyphonyMode::Poly;
    int maxVoices = 16;                     // 1..32 (notes ; l'unisson multiplie le coût)
    VoiceStealMode stealMode = VoiceStealMode::Oldest;
    int glideMs = 0;                        // 0..2000, mono et legato
    int pitchBendRange = 2;                 // 0..12 demi-tons
    int wheelPitchSens = 0;                 // 0..15 : vibrato de la molette
    int wheelAmpSens = 0;                   // 0..15 : trémolo de la molette
    int unisonVoices = 1;                   // 1..8

    bool operator== (const FmDescription& o) const noexcept
    {
        return patch == o.patch && masterTuneCents == o.masterTuneCents && polyphony == o.polyphony && maxVoices == o.maxVoices
            && stealMode == o.stealMode && glideMs == o.glideMs && pitchBendRange == o.pitchBendRange
            && wheelPitchSens == o.wheelPitchSens && wheelAmpSens == o.wheelAmpSens && unisonVoices == o.unisonVoices;
    }
    bool operator!= (const FmDescription& o) const noexcept { return ! (*this == o); }
};

FmDescription sanitised (FmDescription);
std::string toJson (const FmDescription&);
FmDescription fmFromJson (std::string_view);   // lecture tolérante

// Valeurs automatisables lues à chaque bloc.
struct FmLive
{
    float brightness = 0.0f;                    // -1..1 : niveau des modulateurs (±12 dB)
    float envSpeed = 0.0f;                      // -1..1 : vitesse des enveloppes (x 0,25 à x 4), prise au début de chaque note
    float feedbackDrive = 0.0f;                 // 0..1 : feedback ajouté (jusqu'à +7)
    std::array<float, kOperators> opLevel {};   // décalage de niveau de sortie en pas DX7 (-99..+99, 1 pas = 0,75 dB)
    float tuneCents = 0.0f;                     // accordage de la piste (demi-tons et cents)
    float unisonDetuneCents = 0.0f;             // 0..100 : écart des voix extrêmes
    float unisonSpread = 0.0f;                  // 0..1 : répartition stéréo
    int modWheel = 0;                           // 0..127
    int pitchBend = 8192;                       // 0..16383
};

class FmVoices
{
public:
    FmVoices();
    ~FmVoices();
    FmVoices (const FmVoices&) = delete;
    FmVoices& operator= (const FmVoices&) = delete;

    void reserve();                             // thread de message : alloue les notes (idempotent)
    bool reserved() const noexcept { return reserved_.load (std::memory_order_acquire); }
    void prepare (double sampleRate) noexcept;
    // Description adoptée par le thread audio (nul : silence). Les notes en cours gardent le patch avec lequel elles ont démarré.
    void setDescription (const FmDescription* d) noexcept;

    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void setSustain (bool on) noexcept;
    void releaseAll() noexcept;
    void panic() noexcept;                      // coupe tout en fondu
    void allSoundOff() noexcept;                // coupe net

    // Ajoute le rendu (volume appliqué en rampe de gainStart à gainEnd). `right` peut être nul : mélange mono.
    void render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const FmLive& live) noexcept;

    int activeVoices() const noexcept;
    int soundingVoices() const noexcept;        // hors fondu de vol (limite globale de voix)
    int heldVoices() const noexcept;
    bool weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept;
    void stealWeakest() noexcept;

    struct VoiceInfo { int key = 0, channel = 0, velocity = 0; bool releasing = false, fading = false; float level = 0.0f; std::uint64_t order = 0; };
    template <typename F>
    void forEachVoice (F&& f) const noexcept
    {
        for (const auto& v : voices_)
            if (v.active) f (VoiceInfo { v.key, v.channel, v.velocity, v.releasing, v.stolen, v.level, v.order });
    }

private:
    struct Voice
    {
        bool active = false, keydown = false, sustained = false, releasing = false, stolen = false;
        int channel = 0, key = 0, velocity = 0;
        int baseKey = 0;                        // touche avec laquelle les notes ont démarré (le glissando est relatif à elle)
        int unison = 1;
        std::uint64_t order = 0;
        float level = 0.0f;                     // crête du dernier bloc
        float fade = 1.0f, fadeStep = 0.0f;
        double glide = 0.0;                     // décalage courant (demi-tons) par rapport à baseKey
        double glideTarget = 0.0;
        int silentBlocks = 0;
        int releasedBlocks = 0;
        std::array<std::unique_ptr<Note>, kMaxUnison> notes;
    };

    void startVoice (Voice&, int channel, int key, int velocity, double startGlide) noexcept;
    void releaseVoice (Voice&) noexcept;
    Voice* allocate (int key, bool& dropNew) noexcept;
    void renderBlock (const FmLive&) noexcept;
    void pushHeld (int key, int channel, int velocity) noexcept;
    void removeHeld (int key, int channel) noexcept;
    void steal (Voice&) noexcept;

    std::vector<Voice> voices_;
    std::atomic<bool> reserved_ { false };
    const FmDescription* desc_ = nullptr;
    Lfo lfo_;
    double sampleRate_ = 48000.0;
    std::uint64_t counter_ = 0;
    bool sustain_ = false;
    int heldKeys_ = 0;                          // touches enfoncées (le LFO redémarre à la première)

    struct Held { int key = 0, channel = 0, velocity = 0; };
    static constexpr int kMaxHeld = 16;
    Held held_[kMaxHeld] {};
    int numHeld_ = 0;

    static constexpr int kBlock = fm::kBlock;
    alignas (16) std::array<float, kBlock> blockL_ {}, blockR_ {};
    int blockPos_ = kBlock;                     // kBlock : bloc épuisé
    alignas (16) std::array<std::int32_t, kBlock> scratch_ {};
    FmLive blockLive_;
};
} // namespace neptune::core::fm
