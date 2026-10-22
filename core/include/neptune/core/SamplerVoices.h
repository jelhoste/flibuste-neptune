#pragma once
// Voix du Sampler (palier 1) : lecture polyphonique de zones avec transposition (interpolation de Hermite), modes
// one-shot / boucle, fondu de relâchement, vol de voix. Aucune allocation : tout est dimensionné à la construction.
#include <array>
#include <cstdint>
#include <vector>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/Envelope.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/LevelTracker.h"

namespace neptune::core
{
class SamplerVoices
{
public:
    static constexpr int kMaxVoices = 256; // plafond de voix tenues ; la limite réelle est un réglage par instrument (64 par défaut)
    static constexpr int kFadeSlots = 64;  // emplacements supplémentaires pour les voix volées qui s'éteignent (un accord vole beaucoup de voix d'un coup)
    static constexpr double kStealFadeSeconds = 0.003;

    // Coupure et résonance des filtres hérités de l'instrument (paramètres automatisables), lues une fois par bloc.
    struct FilterParams
    {
        double cutoffHz[2] = { filterMaxHz, filterMaxHz };
        double resonance[2] = { 0.0, 0.0 }; // 0..1
    };
    void setFilterParams (const FilterParams& p) noexcept { filterParams_ = p; }
    // Valeurs des 16 emplacements assignables (-1..+1, voir ZoneSlots.h), lues une fois par bloc. Prises au début de chaque note.
    void setSlotAmounts (const float* amounts) noexcept { for (int i = 0; i < kZoneSlots; ++i) slotAmounts_[static_cast<std::size_t> (i)] = amounts[i]; }

    void prepare (double sampleRate) noexcept;
    // Streaming disque (facultatif) : les échantillons dont seul le début est en mémoire y sont lus. Nul = jamais de streaming.
    void setStreamer (DiskStreamer* streamer) noexcept { streamer_ = streamer; }
    // Remplace l'instrument. Les voix dont la zone existe encore (même indice, même sample) continuent avec les nouveaux
    // réglages (édition en direct) ; les autres s'arrêtent net : les zones de l'ancien instrument vont disparaître.
    void setInstrument (const Instrument* instrument) noexcept;

    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    float lastShapedVelocity() const noexcept { return lastShaped_; } // vélocité du dernier Note On après la courbe de l'instrument

    // Écoute d'une zone (bouton de lecture de la liste) : démarre une voix de CETTE zone à sa note racine, sans passer par la
    // sélection des couches ni le choke. Le canal est hors de la plage MIDI : aucune confusion avec les notes de l'hôte.
    static constexpr int kPreviewChannel = 1000;
    // Ce que l'écoute joue : la zone telle quelle (région, boucle, comme au clavier), le sample brut en entier, la région START-END une fois, depuis un point (jusqu'à END),
    // une boucle une seule fois, ou une boucle telle qu'on l'entend (avec ses retours, jusqu'à l'arrêt).
    struct PreviewSpec
    {
        enum class Source : std::uint8_t { Zone, Raw, Region, FromFrame, LoopOnce, LoopHeard } source = Source::Zone;
        std::int64_t frame = -1;   // Zone / FromFrame : point de départ (-1 : le début de la région)
        int loopIndex = 0;         // LoopOnce / LoopHeard : boucle visée (0 = la première)
    };
    void previewZone (int sourceIndex, const PreviewSpec& spec) noexcept;
    void previewZone (int sourceIndex) noexcept { previewZone (sourceIndex, PreviewSpec {}); } // sourceIndex : indice de la zone dans la DESCRIPTION (Zone::sourceIndex)
    void releasePreview() noexcept; // Note Off de l'écoute (les boucles se relâchent ; un one-shot va au bout)
    void stopPreview() noexcept;    // coupe l'écoute en fondu
    void allSoundOff() noexcept;
    // « Panique » : coupe tout ce qui sonne en fondu (le fondu de coupure de l'instrument), oublie les touches tenues. Sans clic.
    void panic() noexcept;
    // MIDI « All Notes Off » : relâche les notes tenues (une boucle se relâche, un one-shot va au bout) et oublie les touches
    // tenues, sans déclencher de zones de relâchement.
    void releaseAll() noexcept;

    // Ajoute le rendu dans `left`/`right` (right peut être nul : mélange mono). Gain interpolé linéairement
    // de gainStart à gainEnd sur le bloc. masterTuneCents transpose toutes les voix.
    void render (float* left, float* right, int numFrames, float gainStart, float gainEnd, double masterTuneCents) noexcept;

    // Ce qui sonne en ce moment (moniteur de voix de l'interface, débogage). Thread audio.
    struct VoiceInfo
    {
        int track = 0;              // piste (rempli par Engine)
        int sourceIndex = -1;       // indice de la zone dans la description (-1 : zone construite à la main)
        int key = 0, channel = 0;
        int velocity = 0;           // 1..127, après la courbe de l'instrument
        bool releasing = false;     // en relâchement (enveloppe)
        bool fading = false;        // en fondu rapide (vol de voix, coupure, choke)
        bool streaming = false;     // lue depuis le disque au-delà du préchargement
        bool preview = false;       // écoute depuis la liste des zones
        float layerGain = 1.0f;     // gain de couche (fondu entre couches de vélocité)
        float level = 0.0f;         // niveau courant (vélocité x gains x enveloppe), 0..1 environ
        float progress = 0.0f;      // position dans l'échantillon, 0..1
        std::uint64_t order = 0;    // ordre de démarrage
    };
    // Appelle `f(const VoiceInfo&)` pour chaque voix qui sonne (sans tampon intermédiaire : appelé à chaque bloc audio).
    template <typename F>
    void forEachVoice (F&& f) const noexcept
    {
        for (const auto& v : voices_)
            if (v.active && v.zone != nullptr) f (describe (v));
    }

    int activeVoices() const noexcept;    // voix qui sonnent (y compris en relâchement)
    int soundingVoices() const noexcept;  // voix qui sonnent hors fondu de vol : comptées par la limite globale (Engine)
    // Vol global (limite de voix du plugin) : la voix à sacrifier en premier ; les voix en relâchement d'abord, puis la plus faible.
    // Retourne faux s'il n'y en a pas. `releasing` / `level` / `order` permettent à l'appelant de comparer entre pistes.
    bool weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept;
    void stealWeakest() noexcept;
    int heldVoices() const noexcept;      // voix comptées dans la limite (ni relâchées ni en fondu de vol)

private:
    LevelCoefs levelCoefs_;   // pondération K et lissages du suivi de niveau (voir LevelTracker.h) ; refaits à prepare()
    float heardGain (const Zone&, int channel, int key, float velocity) const noexcept;   // volume de relâchement lié au niveau (0 : rien ne sonnait)

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
        // filtres par voie (voir Filter.h) : un état par filtre et par canal, une enveloppe par filtre
        std::array<std::array<FilterState, 2>, 2> fstate {};
        std::array<EnvelopeRunner, 2> fenv {};
        std::array<FilterCoefs, 2> fcoef {};
        std::array<float, 2> fenvValue { 0.0f, 0.0f };
        SlotMods mods;                      // effet des emplacements assignables, fixé au Note On
        int fcount = 0;
        struct Override { bool on = false, loop = false; std::int64_t start = 0, end = 0; } ovr;   // écoute : région et boucle imposées à la place de celles de la zone
        LevelTracker tracker;               // niveau entendu de la voix (suivi seulement si une zone de relâchement le demande)
        int loopIdx = 0;                    // boucle en cours (0 = la première) quand la zone en a plusieurs
        int repsLeft = -1;                  // retours restants de la boucle en cours ; -1 = sans fin, 0 = passer à la suivante
        int delayLeft = 0;                  // délai de lecture : échantillons de silence restant avant le début du son
        double fadeInPlayed = 0.0;          // échantillons de sortie déjà joués (fondu d'entrée)
        bool streaming = false;             // un emplacement de DiskStreamer est réservé à cette voix
        int streamSlot = -1;
        std::uint32_t streamGen = 0;
    };

    VoiceInfo describe (const Voice& v) const noexcept;
    SlotMods slotModsFor (const Zone& zone) const noexcept;
    std::array<float, kZoneSlots> slotAmounts_ {};
    void releaseEnvelopes (Voice& v) noexcept; // Note Off des enveloppes de volume et de filtres
    void updateFilterCoefs (Voice& v, const FilterRuntimeBlock& fr) const noexcept;
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
    template <bool Streamed, bool Filtered>
    void renderVoiceT (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept;
    void startVoice (int channel, int key, float velocity, const Zone& zone, float layerGain = 1.0f) noexcept;
    void bindZone (Voice& v, const Zone& zone) noexcept;
    Voice* allocate() noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept;

    // Sur le tas : 320 voix avec leurs états de filtre pèsent plusieurs centaines de Ko (pile de 1 Mo sous Windows).
    std::vector<Voice> voices_ = std::vector<Voice> (kMaxVoices + kFadeSlots);
    const Instrument* instrument_ = nullptr;
    DiskStreamer* streamer_ = nullptr;
    double sampleRate_ = 48000.0;
    double stealFadeSeconds_ = kStealFadeSeconds;
    FilterParams filterParams_;
    struct HeldKey { int channel = 0, key = 0; float velocity = 0.0f; };
    static constexpr int kMaxHeldKeys = 16;
    std::array<HeldKey, kMaxHeldKeys> held_ {};      // touches tenues (modes mono/legato), la plus récente en dernier
    int numHeld_ = 0;
    float lastShaped_ = 0.0f;
    // Touches physiquement tenues, avec les Groupes des zones qu'elles ont déclenchées (conditions « Premier » / « Legato » :
    // indépendantes des voix, qui peuvent être coupées par le mode mono ou avoir fini leur échantillon).
    struct DownKey { int channel = 0, key = 0; int nGroups = 0; std::array<int, 4> groups {}; };
    static constexpr int kMaxDownKeys = 64;
    std::array<DownKey, kMaxDownKeys> down_ {};
    int numDown_ = 0;
    struct QueryContext { const SamplerVoices* self; int channel, key; } queryContext_ {};
    void markDown (int channel, int key) noexcept;
    void markUp (int channel, int key) noexcept;
    void recordGroups (int channel, int key, const Instrument::Pick* picks, int n) noexcept;
    std::uint64_t counter_ = 0;
    std::array<float, 128> heldVelocity_ {}; // vélocité du dernier Note On par touche (zones déclenchées au relâchement)
};
} // namespace neptune::core
