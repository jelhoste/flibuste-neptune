#include "neptune/core/ShaperPresets.h"

#include "neptune/core/ShaperModule.h"

namespace neptune::core::shaper
{
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Default", {} },
        { "Punchy Kick", { { attackGain1, 60 }, { attackGain2, 60 }, { sustainGain1, -20 }, { sustainGain2, -20 }, { attackTime, 8 }, { sustainTime, 120 }, { range, 15 } } },
        { "Tight Snare", { { attackGain1, 40 }, { attackGain2, 40 }, { sustainGain1, -55 }, { sustainGain2, -55 }, { attackTime, 6 }, { sustainTime, 90 },
                           { levelDependency, 30 }, { levelThreshold, -24 }, { range, 15 } } },
        { "Roomy Overheads", { { attackGain1, -10 }, { attackGain2, -10 }, { sustainGain1, 50 }, { sustainGain2, 50 }, { attackTime, 20 }, { sustainTime, 400 },
                               { shapeSmoothing, 10 } } },
        { "Soft Sustain", { { attackGain1, -25 }, { attackGain2, -25 }, { sustainGain1, 30 }, { sustainGain2, 30 }, { attackTime, 15 }, { sustainTime, 250 } } },
    };
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = { attackGain1, attackGain2, sustainGain1, sustainGain2, levelDependency, levelThreshold, levelKnee, attackTime,
                                              sustainTime, shapeSmoothing, range, stereoLink, stock::outputTrim, stock::mix, scEnable, scSource, scGain,
                                              scIntVol, scExtVol, scPhaseInt, scPhaseExt, scEqOn, scHpFreq, scHpSlope, scLpFreq, scLpSlope,
                                              scBand1Freq, scBand1Gain, scBand1Q, scBand2Freq, scBand2Gain, scBand2Q, scBand3Freq, scBand3Gain, scBand3Q };
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
} // namespace neptune::core::shaper
