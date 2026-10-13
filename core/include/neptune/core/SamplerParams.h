#pragma once
// Paramètres globaux du Sampler (palier 1) : valeurs d'instrument, automatisables. Les réglages propres à une zone
// (note racine, mode, relâchement...) sont dans Instrument.h. Identifiants stables : ne jamais les renuméroter.
#include "neptune/core/Parameter.h"

namespace neptune::core::sampler
{
constexpr ParamId gain = 100;         // dB, -60 (= silence) .. +12
constexpr ParamId tuneSemitones = 101; // -24..+24 (spec : tuning global ±2 octaves)
constexpr ParamId tuneCents = 102;     // -100..+100
// Filtres par défaut de l'instrument (hérités par les zones qui ne les surchargent pas) : coupure et résonance, automatisables.
constexpr ParamId filter1Cutoff = 103;    // Hz, 20..20000 (exponentiel)
constexpr ParamId filter1Resonance = 104; // %, 0..100
constexpr ParamId filter2Cutoff = 105;
constexpr ParamId filter2Resonance = 106;

// Retourne false (et remplit `error`) si une définition est refusée.
bool addParams (ParameterSet& set, std::string* error = nullptr);

constexpr double silenceDb = -60.0; // en deçà ou égal : gain nul
double dbToGain (double db) noexcept;
} // namespace neptune::core::sampler
