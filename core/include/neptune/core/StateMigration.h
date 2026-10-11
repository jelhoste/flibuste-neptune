#pragma once
// Migrations du format d'état : une fonction par saut de version (n -> n+1), appliquée sur le document JSON.
// Séparé de State.h pour que le reste du code n'ait pas à voir nlohmann/json.
#include <functional>
#include <map>

#include <nlohmann/json.hpp>

namespace neptune::core::state
{
// Clé = version de départ ; la fonction transforme un document de version `clé` en version `clé + 1`
// (elle ne touche pas à "schema_version", c'est migrate() qui le met à jour).
using MigrationTable = std::map<int, std::function<bool (nlohmann::json&)>>;

// Table des migrations réellement enregistrées (vide tant que le schéma est en version 1).
const MigrationTable& registeredMigrations();

// Fait passer `doc` de `from` à `to` en enchaînant les sauts. false si un saut manque ou échoue.
bool migrate (nlohmann::json& doc, int from, int to, const MigrationTable& table, std::string* error = nullptr);
} // namespace neptune::core::state
