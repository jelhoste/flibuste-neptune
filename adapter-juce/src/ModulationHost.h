#pragma once
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Modulation.h"
#include "neptune/core/Parameter.h"

// Ce que l'onglet MOD (ModView) demande à son hôte : la matrice de modulation de « la piste en cours », et de quoi l'enregistrer en geste annulable.
// Neptune l'implémente pour la piste sélectionnée du mixeur ; un plugin seul (AnalogSynth...) l'implémente avec une piste unique, la piste 0.
class ModulationHost
{
public:
    virtual ~ModulationHost() = default;

    virtual int modTrack() const noexcept = 0;                                                          // piste dont on édite la matrice
    virtual neptune::core::modulation::Matrix modMatrix (int track) const = 0;                          // la matrice enregistrée
    virtual void modEdit (int track, neptune::core::modulation::Matrix) = 0;                            // l'enregistre (un geste annulable, fusionné s'il se répète)
    virtual void modBeginGesture() = 0;                                                                 // entourent un glissement continu
    virtual void modEndGesture() = 0;
    virtual neptune::core::ParameterSet& modParameters() = 0;                                           // les réglages que les routes peuvent viser
    virtual std::string modDestName (int track, neptune::core::ParamId dest) const = 0;                 // nom affiché d'une destination
    virtual std::vector<std::pair<neptune::core::ParamId, std::string>> modInsertTargets (int track) const { (void) track; return {}; }   // destinations propres aux inserts (aucune hors Neptune)
};
