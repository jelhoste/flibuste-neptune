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
    static constexpr int kMaxVoices = 256; // plafond de voix tenues ; la limite réelle est un réglage par instrument (64 par défaut)
    static constexpr int kFadeSlots = 64;  // emplacements supplémentaires pour les voix volées qui s'éteignent (un accord vole beaucoup de voix d'un coup)
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
        float level = 0.0f;                 // niveau courant (vélocité x gains x enveloppe) : sert au vol « plus faible »
        double targetRatio = 1.0;           // legato : hauteur visée ; baseRatio y glisse sur glideLeft échantillons
        double glideDelta = 0.0;
        int glideLeft = 0;
        bool streaming = false;             // un emplacement de DiskStreamer est réservé à cette voix
        std::uint32_t streamGen = 0;
    };

    void steal (Voice& v) noexcept;
    void cutKey (int channel, int key) noexcept;       // même touche : coupe l'ancienne (fondu)
    void cutAll() noexcept;                            // mono : coupe tout ce qui sonne (fondu)
    bool retargetHeld (int newKey) noexcept;           // legato : les voix tenues changent de hauteur ; faux s'il n'y en a pas
    Voice* chooseVictim (int newKey, bool& dropNew) noexcept;
    void startNote (int channel, int key, float velocity) noexcept; // choisit les couches et démarre les voix
    void releaseZonesFor (int channel, int key) noexcept; // zones déclenchées au relâchement
    void chokeFor (const Zone& zone) noexcept;           // coupe les voix que cette zone choke (avant de démarrer les siennes)
    bool otherHeldInGroup (int groupId, int channel, int key) const noexcept; // conditions « Premier » / « Legato »
    Instrument::HeldQuery heldQuery (int channel, int key) noexcept;
    void pushHeld (int channel, int key, float velocity) noexcept;
    void removeHeld (int channel, int key) noexcept;
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
    double stealFadeSeconds_ = kStealFadeSeconds;
    struct HeldKey { int channel = 0, key = 0; float velocity = 0.0f; };
    static constexpr int kMaxHeldKeys = 16;
    std::array<HeldKey, kMaxHeldKeys> held_ {};      // touches tenues (modes mono/legato), la plus récente en dernier
    int numHeld_ = 0;
    std::array<std::uint8_t, 16 * 128> keyDown_ {}; // touches physiquement tenues (canal x touche)
    struct QueryContext { const SamplerVoices* self; int channel, key; } queryContext_ {};
    std::uint64_t counter_ = 0;
    std::array<float, 128> heldVelocity_ {}; // vélocité du dernier Note On par touche (zones déclenchées au relâchement)
};
} // namespace neptune::core
