#pragma once
// Ce que les composants d'interface génériques (potard, modale, éditeurs de modules stock) attendent de leur hôte :
// un jeu de paramètres et les gestes d'édition (annulation, automation). Neptune l'implémente par son processeur, un insert par
// un adaptateur sur le jeu de paramètres du module, le plugin CLAP séparé par son propre processeur.
#include <string>
#include <string_view>
#include <vector>

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
    // Macros (Neptune seulement) : assigner un paramètre à une ou plusieurs des 32 macros du pool.
    virtual bool canAssignMacro (neptune::core::ParamId) const { return false; }
    virtual std::vector<int> macrosOf (neptune::core::ParamId) const { return {}; }       // indices 0..31
    virtual void uiToggleMacro (neptune::core::ParamId, int /*macro*/) {}
    virtual void uiClearMacros (neptune::core::ParamId) {}
    virtual std::string uiText (std::string_view key) const { return std::string (key); }  // texte de l'interface dans la langue des réglages
    virtual bool uiUndo() { return false; }
    virtual bool uiRedo() { return false; }

    // Taille mémorisée de la modale des potards, par type de terminal.
    virtual neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal) const { return {}; }
    virtual void setModalSize (neptune::core::uistate::Terminal, neptune::core::uistate::Size) {}
};
