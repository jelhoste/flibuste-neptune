#pragma once
// Paramètres globaux du Sampler (palier 1) : valeurs d'instrument, automatisables. Les réglages propres à une zone
// (note racine, mode, relâchement...) sont dans Instrument.h. Identifiants stables : ne jamais les renuméroter.
#include "neptune/core/Parameter.h"

namespace neptune::core::sampler
{
constexpr ParamId gain = 100;         // dB, -60 (= silence) .. +12
constexpr ParamId tuneSemitones = 101; // -24..+24 (spec : tuning global ±2 octaves)
constexpr ParamId tuneCents = 102;     // -100..+100

// Retourne false (et remplit `error`) si une définition est refusée.
bool addParams (ParameterSet& set, std::string* error = nullptr);

constexpr double silenceDb = -60.0; // en deçà ou égal : gain nul
double dbToGain (double db) noexcept;
} // namespace neptune::core::sampler
