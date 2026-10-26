#pragma once
#include <string>

// Classement des paramètres par portée (piste, piste parallèle, bus, master, global) et par famille (bande, envois, inserts, synthé,
// filtres, FM, orgue, sampler, emplacements, modulateurs), déduit du nom du paramètre. Sert aux filtres des listes de choix de paramètres.
namespace neptune::core::paramgroups
{
enum class Scope { Track, Parallel, Bus, Master, Global };
enum class Family { Strip, Sends, Inserts, Synth, Filters, Fm, Organ, Sampler, Zones, Modulators, Router, Other };
constexpr int numFamilies = 12;

struct Group
{
    Scope scope = Scope::Global;
    int index = 0;                      // numéro (à partir de 0) de la piste, de la piste parallèle ou du bus ; sans objet sinon
    Family family = Family::Other;
};

Group classify (const std::string& name);

// Clé de traduction du nom d'une famille.
const char* familyKey (Family) noexcept;
}
