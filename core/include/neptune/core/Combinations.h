#pragma once
// Combinaisons d'orgue (palier 7, voir docs/specification-combinaisons.md) : des mémoires qui rappellent d'un coup l'état de la console.
//  - Général : jeux (modulateurs des pistes), tirants d'accouplement du routeur et transpositions de règles, toute la console.
//  - Divisionnel : les jeux d'un seul clavier (une piste).
//  - Annulation générale : repousse tout (jeux, tirants, transpositions à 0).
//  - Séquenceur : liste ordonnée de généraux parcourue pas à pas.
// `Library` est la donnée (dans le projet, voir MixerState) ; `Controller` est l'état de jeu (banque courante, touche Set, position du
// séquenceur) et exécute les actions. Les valeurs sont des valeurs réelles de paramètres. Cœur portable, sans JUCE.
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::combi
{
constexpr int kBanks = 10;               // banques ; la banque courante est commune aux généraux et aux divisionnels
constexpr int kPistons = 10;             // pistons par banque (numérotés 1 à 10 à l'écran, 0 à 9 ici)
constexpr int kTracks = 16;              // claviers (pistes) qui ont des divisionnels
constexpr int kMaxSequence = 64;         // pas du séquenceur
constexpr std::size_t kMaxName = 64;
constexpr std::size_t kMaxValues = 1024; // valeurs d'une mémoire

// Clefs de stockage (clairsemé : seules les mémoires enregistrées existent).
constexpr int generalKey (int bank, int piston) noexcept { return bank * kPistons + piston; }
constexpr int divisionalKey (int track, int bank, int piston) noexcept { return (track * kBanks + bank) * kPistons + piston; }
constexpr int numGeneralKeys = kBanks * kPistons;
constexpr int numDivisionalKeys = kTracks * kBanks * kPistons;

struct Memory
{
    bool used = false;                                   // enregistrée (une mémoire sans valeur est valide : console sans jeux)
    std::string name;
    std::vector<std::pair<ParamId, double>> values;      // valeurs réelles
    bool operator== (const Memory& o) const noexcept { return used == o.used && name == o.name && values == o.values; }
};

struct Library
{
    std::map<int, Memory> generals;       // generalKey
    std::map<int, Memory> divisionals;    // divisionalKey
    std::vector<int> sequence;            // clefs de généraux

    const Memory* general (int bank, int piston) const;
    const Memory* divisional (int track, int bank, int piston) const;
    bool empty() const noexcept { return generals.empty() && divisionals.empty() && sequence.empty(); }
    bool operator== (const Library& o) const noexcept { return generals == o.generals && divisionals == o.divisionals && sequence == o.sequence; }
    bool operator!= (const Library& o) const noexcept { return ! (*this == o); }
};

Library sanitised (Library);                  // bornes, noms et listes tronqués, clefs hors plage retirées
std::string toJson (const Library&);
Library fromJson (std::string_view);          // lecture tolérante

// Paramètres concernés, ceux que le jeu connaît.
//  stopParams : les jeux d'un clavier (les 16 modulateurs de la piste `track`, 0..15).
//  consoleParams : les jeux de tous les claviers, les 32 tirants du routeur et les transpositions de règles.
std::vector<ParamId> stopParams (const ParameterSet&, int track);
std::vector<ParamId> consoleParams (const ParameterSet&);

struct Change
{
    ParamId param = 0;
    double before = 0.0, after = 0.0;
};

// Mémorise les valeurs courantes de `ids` (ceux que le jeu ne connaît pas sont ignorés). Le nom est laissé vide.
Memory capture (const ParameterSet&, const std::vector<ParamId>& ids);
// Règle les paramètres de la mémoire ; rend ce qui a réellement changé (valeur bornée au paramètre ; id inconnu : ignoré).
std::vector<Change> recall (ParameterSet&, const Memory&);
// Annulation générale : tout repousser (valeur 0 de chaque paramètre de la console).
std::vector<Change> cancelAll (ParameterSet&);

enum class Result { Ignored, Empty, Recalled, Stored };
struct Outcome
{
    Result result = Result::Ignored;
    std::vector<Change> changes;   // Recalled : ce qui a changé
};

class Controller
{
public:
    int bank() const noexcept { return bank_; }
    void setBank (int bank) noexcept;                 // bornée 0..kBanks-1
    bool nextBank() noexcept;                         // false aux extrémités (pas de bouclage)
    bool previousBank() noexcept;

    // Touche Set : tenue (setHeld) ou armée (arm). Un piston enregistre quand l'une des deux est active ; l'armement se défait après un
    // enregistrement, pas la touche tenue.
    void setHeld (bool held) noexcept { held_ = held; }
    void arm (bool armed) noexcept { armed_ = armed; }
    bool held() const noexcept { return held_; }
    bool armed() const noexcept { return armed_; }
    bool setActive() const noexcept { return held_ || armed_; }

    // Piston `piston` (0..kPistons-1) de la banque courante. Set actif : enregistre l'état courant (le nom existant est conservé).
    Outcome general (Library&, ParameterSet&, int piston);
    Outcome divisional (Library&, ParameterSet&, int track, int piston);
    Outcome cancel (ParameterSet&);

    // Séquenceur : -1 avant le premier pas. Aux extrémités la position reste en place (Ignored). Un général rappelé directement
    // qui figure dans la liste cale la position sur lui.
    int sequencePosition() const noexcept { return seq_; }
    void resetSequence() noexcept { seq_ = -1; }
    Outcome sequenceNext (const Library&, ParameterSet&);
    Outcome sequencePrevious (const Library&, ParameterSet&);

    // Changement de programme MIDI : 0..9 = piston 1..10 de la banque courante (général). Au-delà : ignoré.
    Outcome programChange (Library&, ParameterSet&, int program);

private:
    Outcome recallKey (const Library&, ParameterSet&, int generalKeyValue);
    int bank_ = 0;
    bool held_ = false, armed_ = false;
    int seq_ = -1;
};
} // namespace neptune::core::combi
