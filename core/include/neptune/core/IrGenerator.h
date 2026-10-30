#pragma once
// Réponses impulsionnelles « d'usine » de Convolution, calculées par le programme (aucun fichier, aucune licence) : ambiance, petite et grande salle, salle de concert,
// grande salle d'église, plaque, ressort. Le chemin d'IR d'un projet les désigne par « builtin:<clé> » ; le chargeur de Convolution appelle `generate` à la place de la lecture d'un fichier.
//
// Réverbérations (tout sauf le ressort) : une première partie de réflexions précoces (quelques impulsions par voie, à des instants tirés au hasard), puis un champ diffus de bruit
// gaussien indépendant sur chaque voie, qui décroît en exponentielle (60 dB à `rt60`) et dont la bande passante se réduit avec le temps (l'air absorbe les aigus). Ressort : un train
// d'échos tous les ~36 ms, chacun passé dans une chaîne de filtres passe-tout qui l'étale en « boing » (dispersion), de plus en plus sourd. Le tirage est déterministe (même clé, même IR).
// Chaque voie est ramenée à une énergie de 1 (le bruit blanc gardé en entrée garde sa valeur efficace).
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/conv/ir_processor.hpp"

namespace neptune::core::irgen
{
constexpr std::string_view kPrefix = "builtin:";

struct Entry
{
    std::string_view key;       // « hall » : enregistré dans les projets
    std::string_view nameKey;   // clé i18n : « ir.hall »
};
const std::vector<Entry>& entries();

bool isBuiltinPath (std::string_view path) noexcept;
std::string pathFor (std::string_view key);                  // « builtin:hall »
std::string keyOfPath (std::string_view path);               // « hall » ; vide si ce n'est pas un chemin d'usine
// Calcule l'IR stéréo à 48 kHz (le chargeur la rééchantillonne comme une IR de fichier). Faux si la clé est inconnue.
bool generate (std::string_view key, llc::IRBuffer& out);
} // namespace neptune::core::irgen
