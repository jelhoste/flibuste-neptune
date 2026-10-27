#pragma once
// Routeur de notes (mode orgue, palier 7) : une couche entre l'entrée MIDI et les pistes. Chaque règle dit : « une note jouée sur tel canal, dans
// telle étendue de touches, est aussi (ou à la place) donnée à tel autre canal, transposée de tant de demi-tons, tant que sa condition est vraie ».
// C'est un accouplement (clavier à clavier, octave grave ou aiguë) ou une division de clavier (règle « à la place »). Un clavier = une piste = un
// canal MIDI. La condition est une combinaison logique (ET, OU, OU exclusif, NON ET, NON OU) de paramètres à deux états (tirants d'accouplement
// `routerSwitchParam`, pistons, interrupteurs quelconques) ; sans condition, la règle est toujours vraie. Cœur portable, sans JUCE.
// Le Note Off défait exactement ce que le Note On a fait, même si un accouplement a été tiré ou repoussé entre-temps.
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::router
{
constexpr int kMaxRules = 64;
constexpr int kMaxTerms = 4;
constexpr int kMaxTargets = 8;           // destinations d'une même note (celle d'origine comprise)
constexpr int kSwitches = 32;            // tirants d'accouplement (paramètres)

enum class Combine { All = 0, Any, One, NotAll, None };   // ET, OU, OU exclusif (exactement un), NON ET, NON OU
constexpr int numCombines = 5;
const char* toString (Combine) noexcept;

struct Term
{
    ParamId param = 0;
    bool negate = false;
    bool operator== (const Term& o) const noexcept { return param == o.param && negate == o.negate; }
};

// Quelles touches tenues la règle accouple : toutes (accouplement ordinaire) ; la plus grave seulement (accouplement de basse) ; la plus aiguë seulement
// (accouplement de mélodie). Pour les deux derniers, la note accouplée suit la touche tenue la plus grave (ou la plus aiguë) du clavier source : elle change quand
// une touche plus grave (aiguë) est jouée ou quand celle-là est relâchée. Une règle de basse ou de mélodie ajoute toujours (jamais « à la place »).
enum class Select { All = 0, Lowest, Highest };
constexpr int numSelects = 3;
const char* toString (Select) noexcept;

struct Rule
{
    bool on = true;                      // la règle est-elle en service (case de la liste)
    std::string name;
    int srcChannel = 0;                  // 0 : tous les canaux, 1..16
    int keyLo = 0, keyHi = 127;          // touches concernées
    int dstChannel = 1;                  // 1..16
    int transpose = 0;                   // demi-tons, -48..+48
    bool replace = false;                // vrai : la note d'origine n'est pas jouée (division de clavier)
    Select select = Select::All;
    Combine combine = Combine::All;
    std::vector<Term> terms;             // au plus kMaxTerms ; vide : toujours vraie
    bool operator== (const Rule& o) const noexcept
    {
        return on == o.on && name == o.name && srcChannel == o.srcChannel && keyLo == o.keyLo && keyHi == o.keyHi && dstChannel == o.dstChannel
            && transpose == o.transpose && replace == o.replace && select == o.select && combine == o.combine && terms == o.terms;
    }
};

struct Config
{
    std::vector<Rule> rules;
    bool operator== (const Config& o) const noexcept { return rules == o.rules; }
    bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
};

Config sanitised (Config);               // bornes ; au plus kMaxRules règles, kMaxTerms termes
std::string toJson (const Config&);
Config fromJson (std::string_view);      // lecture tolérante

// Paramètres « tirants d'accouplement » : kSwitches interrupteurs automatisables (CC, OSC, macro, bouton de Façade), identifiants `15000 + i`.
constexpr ParamId switchBase = 15000;
constexpr ParamId switchParam (int index) { return switchBase + static_cast<ParamId> (index); }
// Transposition automatisable d'une règle (la règle numéro i, à partir de 0) : un réglage par pas -48..+48, 0 par défaut, qui s'ajoute à la transposition
// écrite dans la règle. Un potard ou un curseur de la Façade, un CC, l'OSC ou la matrice de modulation le pilotent. Identifiants `15100 + i`.
constexpr ParamId transposeBase = 15100;
constexpr ParamId transposeParam (int rule) { return transposeBase + static_cast<ParamId> (rule); }
// Déclare les tirants et les transpositions de règles.
bool addSwitchParams (ParameterSet&, std::string* error = nullptr);

// Règles résolues : indices de paramètres dans un jeu donné.
struct CompiledRule
{
    int srcChannel = 0, keyLo = 0, keyHi = 127, dstChannel = 0, transpose = 0;   // canaux 0..15 (srcChannel -1 : tous)
    std::size_t transposeIndex = ParameterSet::npos;   // paramètre de transposition de la règle (s'ajoute à `transpose`)
    bool replace = false;
    Select select = Select::All;
    Combine combine = Combine::All;
    int termCount = 0;
    std::array<std::size_t, kMaxTerms> term {};
    std::array<bool, kMaxTerms> negate {};
};
struct Compiled
{
    std::vector<CompiledRule> rules;
    std::vector<int> dynamic;   // indices des règles de basse et de mélodie : le moteur les suit lui-même, `expand` les ignore
};
std::shared_ptr<const Compiled> compile (const Config&, const ParameterSet&);

struct Target
{
    int channel = 0;   // 0..15
    int key = 0;       // 0..127
    bool operator== (const Target& o) const noexcept { return channel == o.channel && key == o.key; }
};
// La condition de la règle est-elle vraie, et de combien de demi-tons transpose-t-elle (réglée + paramètre) ? Pour les règles de basse et de mélodie.
bool isActive (const CompiledRule&, const ParameterSet&) noexcept;
int semitonesOf (const CompiledRule&, const ParameterSet&) noexcept;
// Destinations d'une note jouée sur `channel` (0..15), touche `key` : la note d'origine (sauf si une règle en service « à la place » la
// concerne) puis celles des règles en service dont la condition est vraie, sans doublon. Rend leur nombre (au plus kMaxTargets). Sans allocation.
// `originalKept` (facultatif) : vrai si la première destination est la note d'origine.
int expand (const Compiled&, const ParameterSet&, int channel, int key, std::array<Target, kMaxTargets>& out, bool* originalKept = nullptr) noexcept;
} // namespace neptune::core::router
