#include "neptune/core/CompressorPresets.h"

#include "neptune/core/CompressorModule.h"

namespace neptune::core::compressor
{
const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = { style, direction, threshold, ratio, kneeWidth, curve, floorDb, attack, release, pump, smooth, range,
                                              rangeInf, hold, makeup, wet, clipperDrive, rmsOn, rmsLength, rmsSpeed, rmsMix };
    return ids;
}

// Valeurs de départ à caler à l'écoute (style : 0 Clean, 1 Classic, 2 Optical, 3 Vocal ; direction : 0 Compress, 1 Inflate, 2 Expand, 3 Shape).
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = {
        { "Default", {} },
        { "Soft Vocal Master", { { style, 3 }, { threshold, -26 }, { ratio, 3 }, { kneeWidth, 12 }, { attack, 15 }, { release, 180 }, { makeup, 3.2 } } },
        { "Drum Bus Glue", { { style, 1 }, { threshold, -18 }, { ratio, 4 }, { kneeWidth, 6 }, { attack, 30 }, { release, 120 }, { makeup, 2 } } },
        { "Bass Leveler", { { style, 0 }, { threshold, -24 }, { ratio, 6 }, { kneeWidth, 4 }, { attack, 20 }, { release, 250 }, { makeup, 3 } } },
        { "Gentle Mix Bus", { { style, 2 }, { threshold, -16 }, { ratio, 2 }, { kneeWidth, 18 }, { attack, 40 }, { release, 400 }, { makeup, 1 } } },
        { "Parallel Punch", { { style, 0 }, { threshold, -30 }, { ratio, 8 }, { kneeWidth, 2 }, { attack, 5 }, { release, 90 }, { wet, 50 }, { makeup, 6 } } },
        { "Upward Lift", { { direction, 1 }, { threshold, -40 }, { ratio, 2.5 }, { attack, 20 }, { release, 300 } } },
        { "Soft Expander", { { direction, 2 }, { threshold, -45 }, { ratio, 3 }, { attack, 2 }, { release, 120 } } },
    };
    return list;
}
} // namespace neptune::core::compressor
