#pragma once
// Remise d'un Instrument du thread qui le construit vers le thread audio (voir HandoffSlot.h).
#include "neptune/core/HandoffSlot.h"
#include "neptune/core/Instrument.h"

namespace neptune::core
{
using InstrumentSlot = HandoffSlot<Instrument>;
} // namespace neptune::core
