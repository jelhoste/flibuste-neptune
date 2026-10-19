#include "neptune/core/EqualizerPresets.h"

#include "neptune/core/EqualizerModule.h"

namespace neptune::core::equalizer
{
const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = []
    {
        std::vector<ParamId> v = { outputGain, autoGain };
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < kBandStride; ++k) v.push_back (bandParam (b, k));
        return v;
    }();
    return ids;
}

namespace
{
struct B { int type; int order; double freq, gain, q; };   // order : indice de pente (0 : 6 dB/oct, 1 : 12, 2 : 24, ...)

FactoryPreset make (const char* name, std::initializer_list<B> bands)
{
    FactoryPreset p { name, {} };
    int i = 0;
    for (const auto& b : bands)
    {
        p.values.push_back ({ bandParam (i, kStatus), kOn });
        p.values.push_back ({ bandParam (i, kType), static_cast<double> (b.type) });
        p.values.push_back ({ bandParam (i, kOrder), static_cast<double> (b.order) });
        p.values.push_back ({ bandParam (i, kFreq), b.freq });
        p.values.push_back ({ bandParam (i, kGain), b.gain });
        p.values.push_back ({ bandParam (i, kQ), b.q });
        ++i;
    }
    return p;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = {
        make ("Default", {}),
        make ("Vocal Clarity", { { kHighPass, 2, 90, 0, 0.707 }, { kPeak, 0, 300, -3, 1.2 }, { kPeak, 0, 3000, 2.5, 1.0 }, { kHighShelf, 0, 10000, 2, 0.7 } }),
        make ("Warm Bus", { { kLowShelf, 0, 100, 1.5, 0.7 }, { kPeak, 0, 400, -1.5, 0.8 }, { kHighShelf, 0, 9000, 1.0, 0.7 } }),
        make ("Mud Cut", { { kHighPass, 1, 40, 0, 0.707 }, { kPeak, 0, 250, -4, 1.5 }, { kPeak, 0, 500, -2, 1.0 } }),
        make ("Bass Tighten", { { kHighPass, 2, 35, 0, 0.707 }, { kPeak, 0, 120, 2, 1.0 }, { kPeak, 0, 300, -3, 1.4 }, { kLowPass, 2, 5000, 0, 0.707 } }),
        make ("Air Lift", { { kPeak, 0, 3500, 1.5, 0.8 }, { kHighShelf, 0, 12000, 3.5, 0.7 } }),
        make ("Telephone", { { kHighPass, 3, 400, 0, 0.707 }, { kLowPass, 3, 3400, 0, 0.707 }, { kPeak, 0, 1500, 3, 1.0 } }),
    };
    return list;
}
} // namespace neptune::core::equalizer
