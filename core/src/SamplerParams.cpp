#include "neptune/core/SamplerParams.h"
#include <cmath>

namespace neptune::core::sampler
{
bool addParams (ParameterSet& set, std::string* error)
{
    auto g = ParamSpec::continuous (gain, "Gain", Unit::Decibel, silenceDb, 12.0, 0.0);
    g.minText = "-inf";
    return set.add (std::move (g), error)
        && set.add (ParamSpec::stepped (tuneSemitones, "Tune (semitones)", Unit::Semitones, -24, 24, 0), error)
        && set.add (ParamSpec::continuous (tuneCents, "Fine tune", Unit::Cents, -100.0, 100.0, 0.0), error)
        && set.add (ParamSpec::continuous (filter1Cutoff, "Filter 1 Cutoff", Unit::Hertz, 20.0, 20000.0, 20000.0, Curve::Exponential), error)
        && set.add (ParamSpec::continuous (filter1Resonance, "Filter 1 Reso", Unit::Percent, 0.0, 100.0, 0.0), error)
        && set.add (ParamSpec::continuous (filter2Cutoff, "Filter 2 Cutoff", Unit::Hertz, 20.0, 20000.0, 20000.0, Curve::Exponential), error)
        && set.add (ParamSpec::continuous (filter2Resonance, "Filter 2 Reso", Unit::Percent, 0.0, 100.0, 0.0), error);
}

double dbToGain (double db) noexcept
{
    return db <= silenceDb ? 0.0 : std::pow (10.0, db / 20.0);
}
} // namespace neptune::core::sampler
