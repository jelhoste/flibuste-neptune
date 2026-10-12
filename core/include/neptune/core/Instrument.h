#pragma once
// Modèle d'instrument du Sampler (palier 1).
//
//  - InstrumentDescription : ce qui est sauvegardé dans le projet (chemins de fichiers et réglages de zone), sans audio.
//  - Instrument            : l'instrument prêt à jouer (échantillons chargés), immuable, partagé avec le thread audio.
// Les réglages de zone (note racine, mode, relâchement...) vivent dans la zone, pas dans la couche Paramètre : un
// instrument aura plusieurs zones. Les réglages globaux (gain, accordage) sont des paramètres (SamplerParams.h).
#include <cstdint>
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
    // Points de boucle en échantillons ; loopEnd < 0 = jusqu'à la fin du fichier.
    std::int64_t loopStart = 0, loopEnd = -1;

    bool operator== (const ZoneDescription& o) const noexcept
    {
        return samplePath == o.samplePath && keyLow == o.keyLow && keyHigh == o.keyHigh && velLow == o.velLow && velHigh == o.velHigh
            && rootNote == o.rootNote && mode == o.mode && envelope == o.envelope && tuneCents == o.tuneCents && gainDb == o.gainDb
            && loopStart == o.loopStart && loopEnd == o.loopEnd;
    }
};

struct InstrumentDescription
{
    std::vector<ZoneDescription> zones;
    bool operator== (const InstrumentDescription& o) const noexcept { return zones == o.zones; }
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
};

struct Instrument
{
    std::vector<Zone> zones;

    // Première zone qui couvre la note et la vélocité (0..1 -> 1..127) ; nul si aucune.
    const Zone* find (int key, float velocity) const noexcept;
};

// Construit l'instrument depuis la description : charge les fichiers (hors temps réel). Une zone dont le fichier ne
// se charge pas est ignorée et signalée dans `warnings`. Un même fichier n'est chargé qu'une fois.
// `cache` (facultatif) : fichiers déjà chargés, par chemin, pour ne pas relire le disque quand seul un réglage change.
// À la fin, il ne contient plus que les fichiers de la description.
using SampleCache = std::map<std::string, std::shared_ptr<const SampleData>>;
std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings = nullptr,
                                                   SampleCache* cache = nullptr);

// Zone qui couvre tout le clavier à partir d'un sample déjà chargé (tests, outils).
Zone makeFullKeyboardZone (std::shared_ptr<const SampleData> sample, int rootNote, PlayMode mode);
} // namespace neptune::core
