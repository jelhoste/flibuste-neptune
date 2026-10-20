#include "neptune/core/MicrodecalePresets.h"

#include "neptune/core/MicrodecaleModule.h"

namespace neptune::core::microdecale
{
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Default", {} },
        { "Micro-Pitch Slap (approx.)", { { shiftL, 9 }, { delayL, 0 }, { shiftR, -9 }, { delayR, 90 }, { levelShiftL, 100 }, { levelShiftR, 100 } } },
        { "Slap / Micro-Pitch (approx.)", { { shiftL, 9 }, { delayL, 90 }, { shiftR, -9 }, { delayR, 0 } } },
        { "Mono Thickener", { { mode, 1 }, { shiftL, 7 }, { shiftR, -7 }, { delayL, 6 }, { delayR, 14 } } },
    };
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = [] {
        std::vector<ParamId> v;
        for (ParamId id = mode; id <= modWave; ++id) v.push_back (id);
        v.push_back (stock::mix);
        v.push_back (stock::outputTrim);
        return v;
    }();
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
} // namespace neptune::core::microdecale
