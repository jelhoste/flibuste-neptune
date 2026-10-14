#pragma once
// Ce que les composants d'interface génériques (potard, modale, éditeurs de modules stock) attendent de leur hôte :
// un jeu de paramètres et les gestes d'édition (annulation, automation). Neptune l'implémente par son processeur, un insert par
// un adaptateur sur le jeu de paramètres du module, le plugin CLAP séparé par son propre processeur.
#include "neptune/core/Parameter.h"
#include "neptune/core/UiState.h"

class ParamHost
{
public:
    virtual ~ParamHost() = default;

    virtual neptune::core::ParameterSet& parameters() = 0;
    virtual void uiBeginEdit (neptune::core::ParamId) = 0;
    virtual void uiSetNormalised (neptune::core::ParamId, double normalised) = 0;
    virtual void uiEndEdit (neptune::core::ParamId) = 0;
    virtual void uiResetToDefault (neptune::core::ParamId) = 0;
    virtual bool uiUndo() { return false; }
    virtual bool uiRedo() { return false; }

    // Taille mémorisée de la modale des potards, par type de terminal.
    virtual neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal) const { return {}; }
    virtual void setModalSize (neptune::core::uistate::Terminal, neptune::core::uistate::Size) {}
};
