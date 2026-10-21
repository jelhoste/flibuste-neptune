#pragma once
// Modèle d'instrument du Sampler (palier 1).
//
//  - InstrumentDescription : ce qui est sauvegardé dans le projet (chemins de fichiers et réglages de zone), sans audio.
//  - Instrument            : l'instrument prêt à jouer (échantillons chargés), immuable, partagé avec le thread audio.
// Les réglages de zone (note racine, mode, relâchement...) vivent dans la zone, pas dans la couche Paramètre : un
// instrument aura plusieurs zones. Les réglages globaux (gain, accordage) sont des paramètres (SamplerParams.h).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"
#include "neptune/core/SampleData.h"
#include "neptune/core/VelocityResponse.h"
#include "neptune/core/ZoneSlots.h"

namespace neptune::core
{
enum class PlayMode
{
    OneShot, // joué en entier, le Note Off est ignoré
    Loop     // boucle entre les points de boucle tant que la touche est tenue ; le Note Off lance le relâchement
};

const char* toString (PlayMode m) noexcept;                  // "one_shot" / "loop"
bool playModeFromString (std::string_view s, PlayMode& out);

// Alternance des échantillons d'une même place (spec-modules §3, round-robin). Le compteur est propre à chaque jeu
// (même étendue de notes et de vélocité, donc indépendant par couche de vélocité).
enum class RoundRobinMode { Sequential, Random, RandomNoRepeat };
const char* toString (RoundRobinMode m) noexcept; // "sequential" / "random" / "random_no_repeat"
bool roundRobinModeFromString (std::string_view s, RoundRobinMode& out);

// Condition de déclenchement d'une zone d'attaque (spec §zone « type de déclenchement » ; le relâchement est `onRelease`) :
//  Always : à chaque Note On ; First : seulement si aucune autre note du Groupe n'est tenue ; Legato : seulement si une
//  autre note du Groupe est déjà tenue (transitions liées). Le « Groupe » est celui de la zone (vide = toutes les zones sans Groupe).
enum class TriggerCondition { Always, First, Legato };
const char* toString (TriggerCondition) noexcept;

// Groupes de choke : une zone qui se déclenche coupe les voix des zones dont `chokeCutBy` égale son `chokeCuts` (0 = aucun).
// Un seul numéro N sur les deux champs = groupe symétrique (les zones du groupe se coupent entre elles).
constexpr int maxChokeGroup = 32;

// Boucle supplémentaire d'une zone (spec §13.5 : jusqu'à 4 boucles par zone, jouées à la suite). La première boucle reste loopStart / loopEnd / loopCrossfade.
struct LoopRegion
{
    std::int64_t start = 0, end = -1;   // end < 0 : jusqu'à la fin du fichier
    std::int64_t crossfade = 0;         // fondu enchaîné de cette boucle, en échantillons
    int repeats = 0;                    // retours en arrière avant de passer à la boucle suivante ; 0 = jusqu'au relâchement (toujours le cas de la dernière)
    bool operator== (const LoopRegion& o) const noexcept { return start == o.start && end == o.end && crossfade == o.crossfade && repeats == o.repeats; }
};

struct ZoneDescription
{
    std::string samplePath;        // chemin absolu (la résolution relative/recherche par nom viendra avec les presets, spec §20)
    int keyLow = 0, keyHigh = 127; // étendue de notes MIDI
    int velLow = 1, velHigh = 127; // étendue de vélocité MIDI (1..127)
    int rootNote = 60;
    PlayMode mode = PlayMode::OneShot;
    EnvelopeDescription envelope;  // enveloppe d'amplitude de la zone (voir Envelope.h)
    double tuneCents = 0.0;        // accordage fin de la zone
    double gainDb = 0.0;
    int roundRobin = 0;            // indice dans le jeu round-robin (0 = non précisé)
    std::string group;             // nom de Groupe (micro, articulation...) ; vide = aucun
    bool onRelease = false;        // déclenchée au Note Off (bruits de mécanique, fins de note), pas au Note On
    TriggerCondition trigger = TriggerCondition::Always; // condition d'une zone d'attaque (ignorée si onRelease)
    // Filtres par voie : faux = la zone hérite des filtres de l'instrument (InstrumentDescription::defaultFilters) ; vrai = les siens.
    bool filterOverride = false;
    FilterBlock filters;
    int chokeCuts = 0;             // groupe de choke que le déclenchement de cette zone coupe (0 = aucun)
    int chokeCutBy = 0;            // groupe de choke par lequel cette zone peut être coupée (0 = aucun)
    // Points de boucle en échantillons ; loopEnd < 0 = jusqu'à la fin du fichier.
    std::int64_t loopStart = 0, loopEnd = -1;
    // Région jouée (en échantillons) : la lecture commence à startFrame et, hors boucle, s'arrête à endFrame (exclusif) ; endFrame < 0 = fin du fichier.
    std::int64_t startFrame = 0, endFrame = -1;
    // Fondu enchaîné de boucle (en échantillons, avant la fin de boucle : le son qui précède le début de boucle y est mélangé en puissance constante) ; 0 = aucun.
    std::int64_t loopCrossfade = 0;
    // Fondus d'entrée et de sortie (ms) : rampes de gain appliquées en direct (rien n'est figé dans le sample) ; petites par défaut, contre les clics.
    // Le fondu de sortie ne vaut que pour une lecture jusqu'au bout (one-shot) ; une boucle se termine par le relâchement de l'enveloppe.
    double fadeInMs = defaultFadeInMs, fadeOutMs = defaultFadeOutMs;
    // Boucles 2 à 4 (au plus 3), dans l'ordre du fichier : la lecture parcourt la boucle 1 `loopRepeats` fois (0 = sans fin), puis continue vers la boucle 2, etc.
    // La dernière boucle tourne jusqu'au relâchement. Sans effet en lecture inversée (seule la boucle 1 compte).
    static constexpr std::size_t maxExtraLoops = 3;
    std::vector<LoopRegion> extraLoops;
    int loopRepeats = 0;
    // Volume de relâchement lié au niveau (zones déclenchées au Note Off) : le niveau réellement entendu des voix interrompues, capturé au Note Off, règle le volume
    // de la zone de relâchement. `releaseMeter` : 0 crête, 1 RMS, 2 LUFS (pondération K). Correspondance : niveau en dB de `releaseFloorDb` (silence) à 0 dB -> 0..1, puis
    // élevé à `releaseCurve` (1 = linéaire en dB, > 1 = plus doux en bas, < 1 = plus fort en bas).
    bool releaseFollowsLevel = false;
    int releaseMeter = 2;
    double releaseFloorDb = -48.0;
    double releaseCurve = 1.0;
    double pan = 0.0;              // -1 (gauche) .. +1 (droite) ; loi de balance en cosinus, neutre au centre
    bool reverse = false;          // lecture à l'envers (le sample est alors gardé entièrement en mémoire)
    double delayMs = 0.0;          // retard du déclenchement du son après le Note On
    static constexpr double defaultFadeInMs = 1.0, defaultFadeOutMs = 2.0, maxFadeMs = 5000.0, maxDelayMs = 10000.0;

    bool operator== (const ZoneDescription& o) const noexcept
    {
        return samplePath == o.samplePath && keyLow == o.keyLow && keyHigh == o.keyHigh && velLow == o.velLow && velHigh == o.velHigh
            && rootNote == o.rootNote && mode == o.mode && envelope == o.envelope && tuneCents == o.tuneCents && gainDb == o.gainDb
            && loopStart == o.loopStart && loopEnd == o.loopEnd && roundRobin == o.roundRobin && group == o.group && onRelease == o.onRelease
            && trigger == o.trigger && chokeCuts == o.chokeCuts && chokeCutBy == o.chokeCutBy
            && filterOverride == o.filterOverride && filters == o.filters
            && startFrame == o.startFrame && endFrame == o.endFrame && loopCrossfade == o.loopCrossfade && fadeInMs == o.fadeInMs
            && fadeOutMs == o.fadeOutMs && reverse == o.reverse && delayMs == o.delayMs
            && extraLoops == o.extraLoops && loopRepeats == o.loopRepeats && pan == o.pan
            && releaseFollowsLevel == o.releaseFollowsLevel && releaseMeter == o.releaseMeter && releaseFloorDb == o.releaseFloorDb && releaseCurve == o.releaseCurve;
    }
};

// Polyphonie de l'instrument (palier 1, jalon 6).
enum class PolyphonyMode { Poly, Mono, Legato };       // Mono : une note à la fois, la nouvelle repart ; Legato : la voix continue et change de hauteur
enum class VoiceStealMode { Oldest, Quietest, KeepLow, KeepHigh }; // voix volée quand la limite est atteinte (KeepLow : on garde les graves)
enum class SameKeyMode { Stack, Cut };                 // même touche rejouée : empiler ou couper l'ancienne voix
const char* toString (PolyphonyMode) noexcept;
const char* toString (VoiceStealMode) noexcept;
const char* toString (SameKeyMode) noexcept;

struct InstrumentDescription
{
    std::vector<ZoneDescription> zones;
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;

    // Ce qui est chargé en mémoire (réglable en cours d'utilisation ; les zones restent toutes dans la description) :
    bool compensateOnReplace = true;           // remplacer le fichier d'une zone compense la différence de niveau (gain de la zone)
    bool mainRoundRobinOnly = false;           // ne charger que l'échantillon principal de chaque place
    bool loadReleaseSamples = true;            // zones déclenchées au relâchement
    std::vector<std::string> unloadedGroups;   // noms de Groupe (articulations, pédales...) laissés sur le disque

    // Streaming disque (réglage par instrument, sauvegardé avec lui) : en mémoire seulement le début de chaque sample
    // (`preloadKB` Ko de PCM par fichier), le reste est lu sur le disque pendant le jeu. Les zones en boucle gardent en
    // mémoire tout ce qui précède la fin de boucle. Faux = tout en RAM (comportement historique).
    bool streamFromDisk = false;
    int preloadKB = defaultPreloadKB;

    static constexpr int defaultPreloadKB = 128;
    static constexpr int minPreloadKB = 16, maxPreloadKB = 65536;

    // Réponse de vélocité (voir VelocityResponse.h) : courbe appliquée à la vélocité reçue, et fondu enchaîné entre couches.
    VelocityCurve velocityCurve;
    int velocityFadeWidth = 0;        // largeur (en vélocités) du fondu automatique aux frontières ; 0 = commutation franche
    bool velocityFadeLinear = false;  // faux : puissance constante ; vrai : linéaire en amplitude
    static constexpr int maxVelocityFadeWidth = 64;

    // Polyphonie (voir ci-dessus). Les modes Mono/Legato ignorent la limite et le mode de vol de voix.
    PolyphonyMode polyphony = PolyphonyMode::Poly;
    int maxVoices = defaultMaxVoices;                // limite de voix tenues, 1..maxVoicesLimit
    VoiceStealMode stealMode = VoiceStealMode::Oldest;
    SameKeyMode sameKey = SameKeyMode::Stack;
    int cutFadeMs = 3;                               // fondu d'une voix volée / coupée (1..maxCutFadeMs)
    int glideMs = 0;                                 // glissando du legato (0..maxGlideMs)
    // Filtres par défaut de l'instrument, hérités par les zones qui ne les surchargent pas. Leur coupure et leur résonance sont
    // les PARAMÈTRES automatisables (SamplerParams : filterCutoff/filterResonance) : les valeurs stockées ici sont ignorées.
    FilterBlock defaultFilters;
    bool chokeSameZone = false;                      // un choke coupe aussi les voix de la MÊME place (alternatives round-robin comprises)
    static constexpr int defaultMaxVoices = 64, maxVoicesLimit = 256, maxCutFadeMs = 200, maxGlideMs = 2000;

    // Emplacements assignables (ZoneSlots.h) : qui vise quoi. Ils font référence aux zones par leur indice (retirés ou décalés quand des
    // zones disparaissent, voir removeZones) et aux Groupes par leur nom.
    SlotAssignments slots;

    // Copie tout sauf les zones (réglages de chargement, de streaming, de réponse de vélocité, emplacements assignables).
    void copySettingsFrom (const InstrumentDescription& o)
    {
        roundRobinMode = o.roundRobinMode; mainRoundRobinOnly = o.mainRoundRobinOnly; loadReleaseSamples = o.loadReleaseSamples;
        unloadedGroups = o.unloadedGroups; streamFromDisk = o.streamFromDisk; preloadKB = o.preloadKB;
        velocityCurve = o.velocityCurve; velocityFadeWidth = o.velocityFadeWidth; velocityFadeLinear = o.velocityFadeLinear;
        polyphony = o.polyphony; maxVoices = o.maxVoices; stealMode = o.stealMode; sameKey = o.sameKey;
        cutFadeMs = o.cutFadeMs; glideMs = o.glideMs; chokeSameZone = o.chokeSameZone; defaultFilters = o.defaultFilters;
        slots = o.slots;
    }

    bool operator== (const InstrumentDescription& o) const noexcept
    {
        return zones == o.zones && roundRobinMode == o.roundRobinMode && mainRoundRobinOnly == o.mainRoundRobinOnly
            && loadReleaseSamples == o.loadReleaseSamples && unloadedGroups == o.unloadedGroups
            && streamFromDisk == o.streamFromDisk && preloadKB == o.preloadKB
            && velocityCurve == o.velocityCurve && velocityFadeWidth == o.velocityFadeWidth && velocityFadeLinear == o.velocityFadeLinear
            && polyphony == o.polyphony && maxVoices == o.maxVoices && stealMode == o.stealMode && sameKey == o.sameKey
            && cutFadeMs == o.cutFadeMs && glideMs == o.glideMs && chokeSameZone == o.chokeSameZone
            && defaultFilters == o.defaultFilters && slots == o.slots && compensateOnReplace == o.compensateOnReplace;
    }
    bool empty() const noexcept { return zones.empty(); }

    // Retire des zones (indices dans l'ordre quelconque) ; les emplacements qui les visaient sont libérés, les autres suivent le décalage.
    void removeZones (std::vector<int> indices);
    // Libère les emplacements qui visent une zone inexistante.
    void pruneSlots();
};

// Quelles zones de la description sont chargées en mémoire, selon les options ci-dessus. Le principal d'une place
// (mêmes étendues, même Groupe, même déclenchement) est la zone sans numéro de round-robin, sinon le plus petit numéro.
std::vector<bool> loadedZones (const InstrumentDescription& d);

// Frames de ce fichier à garder en mémoire pour cette zone (toutes si le streaming est coupé). Règle unique, utilisée par
// le chargement ET par l'estimation de mémoire : un fichier utilisé par plusieurs zones garde le maximum de leurs besoins.
std::int64_t residentFramesNeeded (const InstrumentDescription&, const ZoneDescription&, int channels, bool keptAsInt16, std::int64_t totalFrames);

// Valeurs bornées/rectifiées (note 0..127, vélocité 1..127, enveloppe : voir sanitised(EnvelopeDescription), cents ±1200,
// gain ±60 dB, boucle ordonnée).
ZoneDescription sanitised (ZoneDescription z);

// JSON : objet {"zones":[...]} ; lecture tolérante (champ invalide -> défaut). Pas d'accès disque.
std::string toJson (const InstrumentDescription& d);
InstrumentDescription instrumentFromJson (std::string_view text);

// ---- Instrument prêt à jouer ----

// Filtres résolus d'une zone (partagés entre toutes les zones qui héritent du même réglage : un seul exemplaire des tables).
struct FilterRuntimeBlock
{
    std::array<FilterSettings, 2> settings;
    std::array<EnvelopeRuntime, 2> env;
    std::array<bool, 2> fromParams { false, false }; // coupure/résonance lues dans les paramètres automatisables (héritage)
    bool parallel = false;
    float balance = 0.5f;
    bool anyOn = false;
};

// Boucle supplémentaire résolue (bornée au fichier) : voir ZoneDescription::extraLoops.
struct LoopRt { std::int64_t start = 0, end = 0, crossfade = 0; int repeats = 0; };

struct Zone
{
    std::shared_ptr<const SampleData> sample;
    int keyLow = 0, keyHigh = 127, velLow = 1, velHigh = 127;
    int rootNote = 60;
    PlayMode mode = PlayMode::OneShot;
    EnvelopeRuntime env;
    double tuneCents = 0.0;
    float gain = 1.0f; // linéaire
    std::int64_t loopStart = 0, loopEnd = 0; // loopEnd exclusif, > loopStart, <= frames
    std::int64_t startFrame = 0, endFrame = 0; // région jouée : [startFrame, endFrame) ; 0 / 0 = tout le fichier (construit par buildInstrument)
    std::int64_t loopCrossfade = 0;            // en échantillons, <= loopStart et <= la moitié de la boucle
    double fadeInMs = 0.0, fadeOutMs = 0.0;
    bool reverse = false;
    double delayMs = 0.0;
    std::array<LoopRt, 3> moreLoops {};   // boucles 2 à 4, strictement croissantes (fin après la fin de la précédente)
    int moreLoopCount = 0;
    int loopRepeats = 0;                  // répétitions de la boucle 1 quand il y en a d'autres (0 = sans fin)
    float panL = 1.0f, panR = 1.0f;       // gains de balance
    bool releaseFollowsLevel = false;     // voir ZoneDescription
    int releaseMeter = 2;
    float releaseFloorDb = -48.0f, releaseCurve = 1.0f;
    int roundRobin = 0;
    int groupId = 0;      // 0 = aucun ; sinon identifiant interne du nom de Groupe
    bool onRelease = false;
    TriggerCondition trigger = TriggerCondition::Always;
    int chokeCuts = 0, chokeCutBy = 0;
    std::shared_ptr<const FilterRuntimeBlock> filters; // nul : aucun filtre
    int sourceIndex = -1; // indice de la zone dans la description (écoute depuis la liste) ; -1 = construite à la main
};

struct Instrument
{
    std::vector<Zone> zones;
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;
    bool trackLevels = false;   // une zone de relâchement suit le niveau des voix : les voix mesurent leur niveau entendu (rempli par finalize)
    SlotRuntimes slots;   // emplacements assignables résolus (voir ZoneSlots.h)

    // Première zone qui couvre la note et la vélocité (0..1 -> 1..127) ; nul si aucune. Ignore le round-robin et
    // les zones déclenchées au relâchement.
    const Zone* find (int key, float velocity) const noexcept;

    // Zone à jouer : comme find(), puis choix dans le jeu round-robin de cette place selon `roundRobinMode`.
    // `release` : cherche parmi les zones déclenchées au Note Off. Thread audio uniquement (les compteurs sont modifiés).
    const Zone* select (int key, float velocity, bool release) const noexcept;

    // Réponse de vélocité et fondu entre couches : à régler AVANT finalize().
    VelocityLut velocityLut = buildLut (VelocityCurve {});
    bool velocityLinear = true; // courbe linéaire : la vélocité passe telle quelle (exacte, sans table)
    int fadeWidth = 0;
    bool fadeLinear = false;

    // Polyphonie (recopiée de la description par buildInstrument)
    PolyphonyMode polyphony = PolyphonyMode::Poly;
    int maxVoices = 64;
    VoiceStealMode stealMode = VoiceStealMode::Oldest;
    SameKeyMode sameKey = SameKeyMode::Stack;
    int cutFadeMs = 3;
    int glideMs = 0;
    bool chokeSameZone = false;

    // Vélocité (0..1) après la courbe de l'instrument.
    float shapeVelocity (float velocity) const noexcept { return velocityLinear ? velocity : lookup (velocityLut, velocity); }

    // Une zone choisie pour une note, avec son gain de couche (fondu) ; le round-robin est résolu.
    struct Pick { const Zone* zone = nullptr; float gain = 1.0f; };
    static constexpr int maxPicks = 16;
    // Les couches de la FAMILLE de la première zone qui couvre la note et la vélocité jouent ensemble, avec leur fondu.
    // `velocity` (0..1) est la vélocité déjà passée par shapeVelocity. Rend le nombre de zones écrites dans `out`.
    // Thread audio uniquement (les compteurs round-robin sont modifiés).
    // `held` (facultatif) répond aux conditions « Premier » / « Legato » : une autre note du Groupe est-elle tenue ? Sans lui,
    // « Premier » est toujours vrai et « Legato » toujours faux. Parmi les zones qui couvrent la note, la première dont la
    // condition est satisfaite fixe la famille.
    struct HeldQuery { bool (*otherHeld) (const void* context, int groupId) noexcept = nullptr; const void* context = nullptr; };
    int selectLayers (int key, float velocity, bool release, Pick* out, int maxOut, const HeldQuery* held = nullptr) const noexcept;
    int placeOf (int zoneIndex) const noexcept { return zoneIndex >= 0 && static_cast<std::size_t> (zoneIndex) < zoneSet_.size() ? zoneSet_[static_cast<std::size_t> (zoneIndex)] : -1; }

    // À appeler une fois les zones remplies (construit les jeux round-robin, les fondus, l'index par note).
    void finalize();

private:
    struct RrSet { std::vector<int> members; }; // indices de zones, dans l'ordre des indices round-robin
    std::vector<RrSet> rrSets_;
    std::vector<int> zoneSet_;
    std::vector<LayerFade> fades_;                  // par zone
    std::vector<std::vector<int>> familySets_;      // par famille : indices de jeux round-robin (une couche chacun)
    std::vector<int> zoneFamily_;                   // par zone
    std::array<std::vector<int>, 128> keyZones_;    // par note : zones qui la couvrent, dans l'ordre de déclaration
    const Zone* pickFromSet (std::size_t set) const noexcept;
    mutable std::vector<std::uint32_t> rrCounter_, rrLast_;
    mutable std::uint32_t rng_ = 0x9E3779B9u;
};

// Construit l'instrument depuis la description : charge les fichiers (hors temps réel). Une zone dont le fichier ne
// se charge pas est ignorée et signalée dans `warnings`. Un même fichier n'est chargé qu'une fois.
// `cache` (facultatif) : fichiers déjà chargés, par chemin, pour ne pas relire le disque quand seul un réglage change.
// À la fin, il ne contient plus que les fichiers de la description.
using SampleCache = std::map<std::string, std::shared_ptr<const SampleData>>;
// `progress(done, total)` est appelée après chaque zone ; si elle rend false, la construction s'arrête et rend nul.
// `keep` (facultatif) : chemins à CONSERVER dans le cache même s'ils ne sont pas dans cette description (cache partagé entre pistes).
std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings = nullptr,
                                                   SampleCache* cache = nullptr,
                                                   const std::function<bool (int done, int total)>& progress = {},
                                                   const std::set<std::string>* keep = nullptr);

// Zone qui couvre tout le clavier à partir d'un sample déjà chargé (tests, outils).
Zone makeFullKeyboardZone (std::shared_ptr<const SampleData> sample, int rootNote, PlayMode mode);
} // namespace neptune::core
