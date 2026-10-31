#include "neptune/core/OverdrivePresets.h"

#include "neptune/core/OverdriveModule.h"

namespace neptune::core::overdrive
{
namespace
{
using Values = std::vector<std::pair<ParamId, double>>;
// Filtres : passe-haut d'entrée (Hz) et passe-bas de sortie (Hz) ; une valeur négative laisse le filtre coupé.
void filters (Values& v, double inHighPass, double outLowPass)
{
    if (inHighPass > 0.0) { v.push_back ({ inHpOn, 1.0 }); v.push_back ({ inHp, inHighPass }); }
    if (outLowPass > 0.0) { v.push_back ({ outLpOn, 1.0 }); v.push_back ({ outLp, outLowPass }); }
}
// Une bande d'égaliseur : indice, type (0 cloche, 1 plateau grave, 2 plateau aigu), fréquence, gain (dB), Q.
void band (Values& v, int b, int type, double freq, double gain, double q = 1.0)
{
    v.push_back ({ eqOn, 1.0 });
    v.push_back ({ bandParam (b, kOn), 1.0 });
    v.push_back ({ bandParam (b, kType), static_cast<double> (type) });
    v.push_back ({ bandParam (b, kFreq), freq });
    v.push_back ({ bandParam (b, kGain), gain });
    v.push_back ({ bandParam (b, kQ), q });
}
Values baseSettings (int m, double drv, double bi, double ch, double fe, double to, double sm, double ti, double im)
{
    return { { model, static_cast<double> (m) }, { drive, drv }, { bias, bi }, { character, ch }, { feel, fe }, { tone, to }, { smooth, sm }, { tight, ti }, { imperfection, im } };
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> p;
        p.push_back ({ "Default", {} });
        {   // clavier : grain léger et chaleur
            auto v = baseSettings (kTape, 5.0, 0.0, 20.0, 30.0, 0.0, 70.0, 0.0, 35.0);
            p.push_back ({ "Light Grain (keys)", v });
        }
        {
            auto v = baseSettings (kTube, 7.0, 30.0, 20.0, 55.0, -15.0, 75.0, 0.0, 40.0);
            filters (v, 70.0, -1.0);
            p.push_back ({ "Warm Rhodes", v });
        }
        {
            auto v = baseSettings (kTube, 15.0, 45.0, 35.0, 45.0, 0.0, 65.0, 0.0, 35.0);
            filters (v, 120.0, -1.0);
            band (v, 1, 0, 2000.0, 2.5, 0.9);
            p.push_back ({ "Wurly Bark", v });
        }
        {
            auto v = baseSettings (kTube, 20.0, 15.0, 40.0, 30.0, 10.0, 55.0, 25.0, 25.0);
            filters (v, 100.0, 7000.0);
            p.push_back ({ "Organ Growl", v });
        }
        {
            auto v = baseSettings (kTube, 12.0, 20.0, 30.0, 50.0, 0.0, 65.0, 20.0, 30.0);
            filters (v, 90.0, 8000.0);
            p.push_back ({ "Edge of Breakup", v });
        }
        {
            auto v = baseSettings (kTube, 24.0, 25.0, 40.0, 40.0, 0.0, 60.0, 35.0, 25.0);
            filters (v, 100.0, 6500.0);
            p.push_back ({ "Crunch", v });
        }
        {
            auto v = baseSettings (kTransistor, 28.0, 10.0, 40.0, 40.0, 5.0, 55.0, 45.0, 20.0);
            filters (v, 110.0, 6000.0);
            p.push_back ({ "Rock Rhythm", v });
        }
        {
            auto v = baseSettings (kTube, 40.0, 30.0, 55.0, 35.0, -5.0, 55.0, 40.0, 20.0);
            filters (v, 120.0, 5500.0);
            p.push_back ({ "Lead Sustain", v });
        }
        {
            auto v = baseSettings (kTape, 12.0, 0.0, 40.0, 50.0, -5.0, 50.0, 0.0, 30.0);
            p.push_back ({ "Tape Glue", v });
        }
        {
            auto v = baseSettings (kTransistor, 22.0, 0.0, 35.0, 30.0, -5.0, 65.0, 70.0, 15.0);
            filters (v, 40.0, 5000.0);
            p.push_back ({ "Bass Overdrive", v });
        }
        {
            auto v = baseSettings (kFuzz, 30.0, 15.0, 20.0, 10.0, -20.0, 75.0, 30.0, 25.0);
            filters (v, -1.0, 5000.0);
            p.push_back ({ "Round Fuzz", v });
        }
        {
            auto v = baseSettings (kFuzz, 44.0, 5.0, 80.0, 30.0, 0.0, 40.0, 20.0, 20.0);
            p.push_back ({ "Square Fuzz", v });
        }
        return p;
    }();
    return presets;
}

const std::vector<ParamId>& presetParams()
{
    static const std::vector<ParamId> ids = [] {
        std::vector<ParamId> v { model, drive, bias, character, feel, tone, level, autoGain, smooth, tight, imperfection, stock::mix };
        for (ParamId id = inHpOn; id <= keyHp; ++id) v.push_back (id);   // filtres, égaliseur, entrée latérale
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
} // namespace neptune::core::overdrive
