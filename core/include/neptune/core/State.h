#pragma once
// Format d'état JSON versionné (palier 0, point 3).
// Texte UTF-8 : {"format":"neptune-state","schema_version":N,"parameters":{"<id>":<valeur réelle>,...}}.
// Aucune dépendance à JUCE ni à CLAP : l'adaptateur ne fait que transporter le texte.
// Ne pas appeler depuis le thread audio (allocations).
#include <optional>
#include <string>
#include <string_view>

#include "neptune/core/Instrument.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Profile.h"

namespace neptune::core::state
{
// Version de schéma écrite par cette version du plugin. À incrémenter à chaque changement de structure,
// avec une migration (voir StateMigration.h) de la version précédente vers la nouvelle.
constexpr int currentSchemaVersion = 3; // v2 : champ optionnel "profile_lock" ; v3 : champ optionnel "instrument" (Sampler)

enum class Status
{
    Ok,
    InvalidJson,   // texte illisible ou structure inattendue
    NotNeptune,    // JSON valide mais pas un état Neptune
    TooNew,        // écrit par une version plus récente du plugin : refusé, rien n'est modifié
    MigrationFailed
};

struct LoadResult
{
    Status status = Status::Ok;
    std::string message; // en anglais, destiné au journal/UI ; vide si Ok
    int fileVersion = 0; // version de schéma lue dans le fichier (0 si illisible)
    int applied = 0;     // paramètres appliqués
    int ignored = 0;     // entrées ignorées : id inconnu du pool, ou valeur non numérique
    std::string instanceId; // identifiant d'instance enregistré dans l'état (vide si absent) ; non validé, voir AutosaveStore::isValidId
    std::optional<PerformanceProfile> profileLock; // verrou de profil de l'instance ; absent ou illisible = non verrouillé
    std::optional<InstrumentDescription> instrument; // description de l'instrument ; absente du document = aucune (inchangée par le plugin)

    bool ok() const noexcept { return status == Status::Ok; }
};

// Écrit l'état complet : toutes les valeurs courantes (pas seulement celles qui diffèrent du défaut),
// afin qu'un changement futur de valeur par défaut ne modifie pas un preset existant.
// `instanceId` (optionnel) est inscrit dans le document : il relie l'état du projet à l'autosave de l'instance (Autosave.h).
// `profileLock` : profil sur lequel l'instance est verrouillée (absent du document si non verrouillée).
// `instrument` : description de l'instrument du Sampler (chemins et réglages de zone, sans audio).
std::string save (const ParameterSet& set, std::string_view instanceId = {}, std::optional<PerformanceProfile> profileLock = std::nullopt,
                  const InstrumentDescription* instrument = nullptr);

// Charge un état complet. Tout ou rien : si le statut n'est pas Ok, `set` n'est pas modifié.
// Les paramètres absents du fichier reviennent à leur valeur par défaut.
LoadResult load (ParameterSet& set, std::string_view text);
} // namespace neptune::core::state
