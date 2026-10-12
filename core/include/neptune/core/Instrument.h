#pragma once
// Modèle d'instrument du Sampler (palier 1).
//
//  - InstrumentDescription : ce qui est sauvegardé dans le projet (chemins de fichiers et réglages de zone), sans audio.
//  - Instrument            : l'instrument prêt à jouer (échantillons chargés), immuable, partagé avec le thread audio.
// Les réglages de zone (note racine, mode, relâchement...) vivent dans la zone, pas dans la couche Paramètre : un
// instrument aura plusieurs zones. Les réglages globaux (gain, accordage) sont des paramètres (SamplerParams.h).
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Envelope.h"
#include "neptune/core/SampleData.h"

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
    // Points de boucle en échantillons ; loopEnd < 0 = jusqu'à la fin du fichier.
    std::int64_t loopStart = 0, loopEnd = -1;

    bool operator== (const ZoneDescription& o) const noexcept
    {
        return samplePath == o.samplePath && keyLow == o.keyLow && keyHigh == o.keyHigh && velLow == o.velLow && velHigh == o.velHigh
            && rootNote == o.rootNote && mode == o.mode && envelope == o.envelope && tuneCents == o.tuneCents && gainDb == o.gainDb
            && loopStart == o.loopStart && loopEnd == o.loopEnd && roundRobin == o.roundRobin && group == o.group && onRelease == o.onRelease;
    }
};

struct InstrumentDescription
{
    std::vector<ZoneDescription> zones;
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;
    bool operator== (const InstrumentDescription& o) const noexcept { return zones == o.zones && roundRobinMode == o.roundRobinMode; }
    bool empty() const noexcept { return zones.empty(); }
};

// Valeurs bornées/rectifiées (note 0..127, vélocité 1..127, enveloppe : voir sanitised(EnvelopeDescription), cents ±1200,
// gain ±60 dB, boucle ordonnée).
ZoneDescription sanitised (ZoneDescription z);

// JSON : objet {"zones":[...]} ; lecture tolérante (champ invalide -> défaut). Pas d'accès disque.
std::string toJson (const InstrumentDescription& d);
InstrumentDescription instrumentFromJson (std::string_view text);

// ---- Instrument prêt à jouer ----

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
    int roundRobin = 0;
    int groupId = 0;      // 0 = aucun ; sinon identifiant interne du nom de Groupe
    bool onRelease = false;
};

struct Instrument
{
    std::vector<Zone> zones;
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;

    // Première zone qui couvre la note et la vélocité (0..1 -> 1..127) ; nul si aucune. Ignore le round-robin et
    // les zones déclenchées au relâchement.
    const Zone* find (int key, float velocity) const noexcept;

    // Zone à jouer : comme find(), puis choix dans le jeu round-robin de cette place selon `roundRobinMode`.
    // `release` : cherche parmi les zones déclenchées au Note Off. Thread audio uniquement (les compteurs sont modifiés).
    const Zone* select (int key, float velocity, bool release) const noexcept;

    // À appeler une fois les zones remplies (construit les jeux round-robin).
    void finalize();

private:
    struct RrSet { std::vector<int> members; }; // indices de zones, dans l'ordre des indices round-robin
    std::vector<RrSet> rrSets_;
    std::vector<int> zoneSet_;
    mutable std::vector<std::uint32_t> rrCounter_, rrLast_;
    mutable std::uint32_t rng_ = 0x9E3779B9u;
};

// Construit l'instrument depuis la description : charge les fichiers (hors temps réel). Une zone dont le fichier ne
// se charge pas est ignorée et signalée dans `warnings`. Un même fichier n'est chargé qu'une fois.
// `cache` (facultatif) : fichiers déjà chargés, par chemin, pour ne pas relire le disque quand seul un réglage change.
// À la fin, il ne contient plus que les fichiers de la description.
using SampleCache = std::map<std::string, std::shared_ptr<const SampleData>>;
// `progress(done, total)` est appelée après chaque zone ; si elle rend false, la construction s'arrête et rend nul.
std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings = nullptr,
                                                   SampleCache* cache = nullptr,
                                                   const std::function<bool (int done, int total)>& progress = {});

// Zone qui couvre tout le clavier à partir d'un sample déjà chargé (tests, outils).
Zone makeFullKeyboardZone (std::shared_ptr<const SampleData> sample, int rootNote, PlayMode mode);
} // namespace neptune::core
