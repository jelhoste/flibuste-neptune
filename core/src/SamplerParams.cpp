#include "neptune/core/SamplerParams.h"
#include <cmath>

namespace neptune::core::sampler
{
bool addParams (ParameterSet& set, std::string* error)
{
    auto g = ParamSpec::continuous (gain, "Sampler Gain", Unit::Decibel, silenceDb, 12.0, 0.0);
    g.minText = "-inf";
    return set.add (std::move (g), error)
        && set.add (ParamSpec::stepped (tuneSemitones, "Sampler Tune (semitones)", Unit::Semitones, -24, 24, 0), error)
        && set.add (ParamSpec::continuous (tuneCents, "Sampler Fine Tune", Unit::Cents, -100.0, 100.0, 0.0), error);
}

double dbToGain (double db) noexcept
{
    return db <= silenceDb ? 0.0 : std::pow (10.0, db / 20.0);
}
} // namespace neptune::core::sampler
