#pragma once
// Briques JSON partagées par les descriptions sauvegardées (instrument du Sampler, patch du synthé) : enveloppe et filtre.
// Détail interne du cœur (nlohmann/json ne sort pas de core/).
#include <nlohmann/json.hpp>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"

namespace neptune::core::detail
{
nlohmann::json envelopeJson (const EnvelopeDescription&);
// Lit l'enveloppe stockée sous la clé "envelope" de `parent` (valeurs par défaut si absente) ; rend une enveloppe bornée.
EnvelopeDescription envelopeFrom (const nlohmann::json& parent);
nlohmann::json filterJson (const FilterSettings&);
FilterSettings filterFrom (const nlohmann::json&); // objet absent ou invalide : réglage par défaut
int jsonInt (const nlohmann::json& o, const char* key, int def);
double jsonNum (const nlohmann::json& o, const char* key, double def);
} // namespace neptune::core::detail
