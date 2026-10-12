#pragma once
// Import de samples en zones (palier 1, jalon 3) : lecture des noms de fichiers selon un gabarit défini par l'utilisateur,
// puis plan d'import (étendues de notes et de vélocité, round-robin) avec diagnostics. Sans disque, sans JUCE.
//
// Principe : jamais de choix silencieux. Un fichier dont le nom ne se lit pas est signalé et exclu ; deux fichiers qui
// visent la même place sont signalés ; un numéro de note et un nom de note qui se contredisent sont signalés.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Instrument.h"

namespace neptune::core::import
{
// ---- Gabarit de nom ----

enum class Role
{
    Ignore,
    NoteNumber,         // note racine, numéro MIDI
    NoteName,           // note racine, nom (C3, F#2, Db4...) ; correspondance selon NoteConvention
    KeyLow, KeyHigh,    // limites de la zone, numéro MIDI (ou nom si le texte en est un)
    KeyBelow, KeyAbove, // étendue en demi-tons sous / au-dessus de la racine
    VelocityValue,      // une vélocité (valeur MIDI 1..127) : les bornes en sont déduites selon la stratégie
    VelocityLow, VelocityHigh,
    VelocityLayer,      // numéro de couche (1 = la plus douce)
    VelocityLayerCount, // nombre total de couches
    Dynamics,           // nuance écrite (pp, p, mp, mf, f, ff...), ordonnée ; devient une couche
    RoundRobin,         // numéro ou lettre
    Group,              // texte libre : nom du Groupe (micro, articulation...)
    Release             // marqueur de déclenchement au relâchement (contenu non vide = relâchement)
};
constexpr int numRoles = 16;
const char* roleKey (Role r) noexcept; // identifiant stable pour les gabarits sauvegardés et la traduction

// Où lire un champ dans le nom de fichier (sans l'extension).
struct FieldRule
{
    Role role = Role::Ignore;
    int segment = 0;   // 0 : le nom entier ; n > 0 : n-ième segment (selon le délimiteur) ; n < 0 : depuis la fin
    int start = 1;     // premier caractère dans la portée, à partir de 1 ; < 0 : depuis la fin de la portée
    int minLength = 1; // longueur minimale
    int maxLength = 0; // longueur maximale ; 0 = jusqu'à la fin de la portée (ou de la suite de caractères valides)
    bool operator== (const FieldRule& o) const noexcept
    {
        return role == o.role && segment == o.segment && start == o.start && minLength == o.minLength && maxLength == o.maxLength;
    }
};

// Do central (MIDI 60) = C3, C4 ou C5 selon les systèmes. Le fichier de test de l'utilisateur donne 048 = C3 : C4 = 60.
struct NoteConvention
{
    int middleCOctave = 4;
    bool operator== (const NoteConvention& o) const noexcept { return middleCOctave == o.middleCOctave; }
};

struct Template
{
    std::string delimiter = "_"; // vide : pas de segmentation
    NoteConvention notes;
    std::vector<FieldRule> fields;
    bool operator== (const Template& o) const noexcept { return delimiter == o.delimiter && notes == o.notes && fields == o.fields; }

    std::string toJson() const;
    static Template fromJson (std::string_view text); // tolérant : valeur invalide -> défaut
};

// ---- Lecture d'un nom ----

struct ParsedName
{
    std::string stem;
    std::optional<int> note;          // racine (numéro), si le gabarit en donne une
    std::optional<int> noteFromName;  // racine lue comme nom de note, pour le contrôle de cohérence
    std::optional<int> keyLow, keyHigh, keyBelow, keyAbove;
    std::optional<int> velValue, velLow, velHigh, layer, layerCount, dynamicsRank; // dynamicsRank : rang dans l'échelle des nuances
    std::optional<int> roundRobin;
    std::string group;
    bool release = false;
    std::vector<std::string> problems; // champs illisibles, valeurs hors bornes...
};

// Nom de note -> numéro MIDI ; nullopt si ce n'est pas un nom de note. « C3 » avec do central = C4 vaut 48.
std::optional<int> noteNameToMidi (std::string_view text, const NoteConvention& c);
// Numéro MIDI -> nom, avec la convention (ex. 48 -> "C3").
std::string midiToNoteName (int midi, const NoteConvention& c);
// Rang d'une nuance (ppp=0 ... fff=7) ; nullopt si inconnue. Insensible à la casse.
std::optional<int> dynamicsRank (std::string_view text);

ParsedName parseName (std::string_view stem, const Template& t);

// ---- Plan d'import ----

enum class RangeStrategy
{
    Exact,    // aucune extension : la zone ne couvre que sa valeur (notes) / sa couche (vélocités)
    Midpoint, // à mi-chemin entre deux valeurs voisines ; les extrémités vont jusqu'aux bornes du clavier / de la vélocité
    Fixed     // étendue fixe autour de la valeur (voir below / above)
};

struct RangeOptions
{
    RangeStrategy strategy = RangeStrategy::Exact; // « à la base, ne fait rien » (décision de l'utilisateur)
    int below = 0, above = 0;                      // pour Fixed
};

struct PlanOptions
{
    RangeOptions keys;
    RangeOptions velocities;
    int layerCountOverride = 0;  // 0 : lu dans les noms, sinon déduit du plus grand numéro de couche rencontré
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;
};

enum class Severity { Info, Warning, Error };

struct Diagnostic
{
    Severity severity = Severity::Info;
    std::string file;    // nom de fichier (stem) concerné, vide si général
    std::string message; // en anglais ; l'interface le montre tel quel
};

struct PlannedZone
{
    std::string path;
    ZoneDescription zone;
    bool included = true; // faux : exclu par une erreur (nom illisible, doublon)
};

struct Plan
{
    std::vector<PlannedZone> zones;
    std::vector<Diagnostic> diagnostics;
    int detectedLayerCount = 0;
    int errors() const noexcept;
    int warnings() const noexcept;
};

// `paths` : chemins complets ; seul le nom de fichier (sans extension) est lu.
Plan planImport (const std::vector<std::string>& paths, const Template& t, const PlanOptions& options);

// Zones retenues du plan, prêtes pour l'instrument.
std::vector<ZoneDescription> includedZones (const Plan& plan);
} // namespace neptune::core::import
