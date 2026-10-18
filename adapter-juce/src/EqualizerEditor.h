#pragma once
#include <memory>
#include "GenericModuleEditor.h"
#include "neptune/core/EqualizerModule.h"

// Interface provisoire de l'égaliseur : réponse en fréquence en direct (courbe de chaque bande active, courbe totale avec les gains dynamiques du
// moment, points des bandes) et vu-mètres, puis tous les paramètres par sections (GenericModuleEditor). L'interface soignée viendra ensuite.
std::unique_ptr<GenericModuleEditor> makeEqualizerEditor (ParamHost&, const neptune::core::EqualizerModule*, const neptune::core::theme::Palette&,
                                                          const neptune::core::i18n::Translator&);
