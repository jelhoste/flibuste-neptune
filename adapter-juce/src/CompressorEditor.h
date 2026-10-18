#pragma once
#include <memory>
#include "GenericModuleEditor.h"
#include "neptune/core/CompressorModule.h"

// Interface provisoire du compresseur : courbe de transfert en direct et vu-mètres de réduction en tête, puis tous les paramètres par sections
// (GenericModuleEditor). L'interface soignée, au style commun de la série, viendra ensuite.
std::unique_ptr<GenericModuleEditor> makeCompressorEditor (ParamHost&, const neptune::core::CompressorModule*, const neptune::core::theme::Palette&,
                                                           const neptune::core::i18n::Translator&);
