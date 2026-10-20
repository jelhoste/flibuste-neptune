#include "neptune/core/ChorusPresets.h"

#include "neptune/core/ChorusModule.h"

namespace neptune::core::chorus
{
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Default", {} },
        { "TAL Chorus I", { { mode, 1 }, { talI, 1 }, { stock::mix, 100 } } },
        { "TAL Chorus II", { { mode, 1 }, { talII, 1 }, { stock::mix, 100 } } },
        { "TAL Chorus I+II", { { mode, 1 }, { talI, 1 }, { talII, 1 }, { stock::mix, 100 } } },
        { "Classic Chorus", { { rate, 0.8 }, { depth, 35 }, { delayMs, 12 }, { lfoShape, 1 }, { voices, 1 }, { width, 80 }, { stock::mix, 50 } } },
        { "Slow Ensemble", { { rate, 0.25 }, { depth, 45 }, { delayMs, 15 }, { lfoShape, 1 }, { voices, 3 }, { width, 100 }, { bbd, 15 }, { stock::mix, 45 } } },
        { "Subtle Doubler", { { rate, 0.6 }, { depth, 15 }, { delayMs, 18 }, { lfoShape, 1 }, { voices, 0 }, { width, 60 }, { stock::mix, 35 } } },
        { "Vibrato", { { rate, 5.5 }, { depth, 20 }, { delayMs, 4 }, { lfoShape, 1 }, { voices, 0 }, { width, 0 }, { stock::mix, 100 } } },
        { "Slow Flanger", { { rate, 0.1 }, { depth, 80 }, { delayMs, 3 }, { lfoShape, 0 }, { voices, 0 }, { width, 100 }, { feedback, 60 }, { stock::mix, 50 } } },
    };
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = { mode, rate, sync, syncDiv, depth, delayMs, lfoShape, voices, width, feedback, lowCut, lowCutSlope, lowCutOn, highCut, highCutSlope,
                                              highCutOn, bbd, bbdOn, talI, talII, talRate1, talRate2, stock::mix, stock::outputTrim };
    return ids;
}

int applyPreset (ParameterSet& set, const FactoryPreset& preset)
{
    int changed = 0;
    for (const auto id : presetParams())
        if (const auto* spec = set.find (id))
        {
            double v = spec->defaultValue;
            for (const auto& [pid, value] : preset.values)
                if (pid == id) v = value;
            if (set.set (id, v)) ++changed;
        }
    return changed;
}
} // namespace neptune::core::chorus
