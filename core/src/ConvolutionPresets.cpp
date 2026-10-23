#include "neptune/core/ConvolutionPresets.h"

#include "neptune/core/ConvolutionModule.h"

namespace neptune::core::convolution
{
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Default", {} },
        // cabine de guitare : on retire le grave inutile et l'aigu rêche
        { "Cabinet Voicing", { { hpOn, 1 }, { hpFreq, 80 }, { hpSlope, 1 }, { lpOn, 1 }, { lpFreq, 6500 }, { lpSlope, 1 } } },
        { "Dark Cabinet", { { hpOn, 1 }, { hpFreq, 90 }, { hpSlope, 1 }, { lpOn, 1 }, { lpFreq, 4500 }, { lpSlope, 2 }, { bandParam (0, kOn), 1 }, { bandParam (0, kType), 1 }, { bandParam (0, kFreq), 200 }, { bandParam (0, kGain), 2.0 } } },
        { "Bright Cabinet", { { hpOn, 1 }, { hpFreq, 100 }, { hpSlope, 1 }, { lpOn, 1 }, { lpFreq, 9000 }, { lpSlope, 1 }, { bandParam (2, kOn), 1 }, { bandParam (2, kType), 2 }, { bandParam (2, kFreq), 4000 }, { bandParam (2, kGain), 3.0 } } },
        // salle ou réverbération à convolution : pré-délai court, plus large, mélange partiel
        { "Room (wide)", { { preDelay, 18 }, { width, 150 }, { hpOn, 1 }, { hpFreq, 120 }, { hpSlope, 0 }, { lpOn, 1 }, { lpFreq, 12000 }, { lpSlope, 0 }, { stock::mix, 35 } } },
    };
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = [] {
        std::vector<ParamId> v;
        for (ParamId id = width; id <= lpSlope; ++id) v.push_back (id);
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
} // namespace neptune::core::convolution
