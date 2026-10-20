#include "neptune/core/DelayPresets.h"

#include "neptune/core/DelayModule.h"

namespace neptune::core::delay
{
namespace
{
using Values = std::vector<std::pair<ParamId, double>>;
// Une ligne en service : temps (ms) ou division (indice, avec SYNC), niveau (dB), panoramique (%) et feedback (%, négatif ou > 100 : inchangé).
void line (Values& v, int n, double time, double level, double pan, double fb = -1000.0, double div = -1.0)
{
    v.push_back ({ lineParam (n, kOn), 1.0 });
    v.push_back ({ lineParam (n, kTime), time });
    if (div >= 0.0) v.push_back ({ lineParam (n, kDiv), div });
    v.push_back ({ lineParam (n, kLevel), level });
    v.push_back ({ lineParam (n, kPan), pan });
    if (fb > -999.0) v.push_back ({ lineParam (n, kFeedback), fb });
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> p;
        p.push_back ({ "Default", {} });
        {
            Values v;
            line (v, 0, 110.0, 0.0, 0.0, 0.0);
            v.push_back ({ drive, 10.0 });
            v.push_back ({ stock::mix, 30.0 });
            p.push_back ({ "Slapback", v });
        }
        {
            Values v;
            line (v, 0, 500.0, 0.0, -100.0, 35.0, 7.0);   // 1/8
            line (v, 1, 500.0, -2.0, 100.0, 35.0, 4.0);   // 1/4
            v.push_back ({ sync, 1.0 });
            v.push_back ({ pingPong, 60.0 });
            v.push_back ({ stock::mix, 30.0 });
            p.push_back ({ "Ping-Pong 1/8", v });
        }
        {
            Values v;
            line (v, 0, 190.0, 0.0, -30.0);
            line (v, 1, 380.0, -4.0, 30.0);
            line (v, 2, 570.0, -8.0, 0.0, 45.0);
            for (int n = 0; n < 3; ++n)
            {
                v.push_back ({ lineParam (n, kLowCut), 150.0 });
                v.push_back ({ lineParam (n, kHighCut), 4000.0 });
                v.push_back ({ lineParam (n, kModDepth), 20.0 });
                v.push_back ({ lineParam (n, kModRate), 0.6 });
            }
            v.push_back ({ drive, 35.0 });
            v.push_back ({ stock::mix, 28.0 });
            p.push_back ({ "Tape Echo", v });
        }
        {
            Values v;
            line (v, 0, 500.0, 0.0, -50.0, -1000.0, 8.0);   // 1/8.
            line (v, 1, 500.0, -3.0, 50.0, -1000.0, 4.0);   // 1/4
            line (v, 2, 500.0, -6.0, 0.0, -1000.0, 10.0);   // 1/16
            v.push_back ({ sync, 1.0 });
            v.push_back ({ stock::mix, 30.0 });
            p.push_back ({ "Dotted Rhythm", v });
        }
        {
            Values v;
            const double times[8] = { 90, 170, 260, 380, 520, 700, 930, 1250 };
            const double highs[8] = { 12000, 10500, 9000, 7500, 6500, 5000, 4000, 3000 };
            for (int n = 0; n < 8; ++n)
            {
                line (v, n, times[n], -16.0 * n / 7.0, n % 2 == 0 ? -70.0 : 70.0);
                v.push_back ({ lineParam (n, kHighCut), highs[n] });
            }
            v.push_back ({ stock::mix, 40.0 });
            p.push_back ({ "Multi-Tap Bounce", v });
        }
        {
            Values v;
            line (v, 0, 25.0, 0.0, -100.0, 0.0);
            line (v, 1, 35.0, 0.0, 100.0, 0.0);
            v.push_back ({ width, 150.0 });
            v.push_back ({ stock::mix, 50.0 });
            p.push_back ({ "Stereo Haas", v });
        }
        return p;
    }();
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = [] {
        std::vector<ParamId> v = { sync, routing, freeze, duckAmount, duckThreshold, duckRelease, feedbackGlobal, pingPong, drive, width, stock::mix, stock::outputTrim };
        for (int n = 0; n < kLines; ++n)
            for (int k = 0; k < kLineParams; ++k)
                if (k != kPitchReserved) v.push_back (lineParam (n, k));
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
} // namespace neptune::core::delay
