#include "neptune/core/ProbePool.h"

namespace neptune::core::probe
{
bool buildPool (ParameterSet& set, int tracks, std::string* error)
{
    auto level = [] (ParamId id, std::string name, double def)
    {
        auto s = ParamSpec::continuous (id, std::move (name), Unit::Decibel, -100.0, 6.0, def);
        s.minText = "-inf";
        return s;
    };
    auto unmodulated = [] (ParamSpec s)   // type de piste et algorithme : ne se modulent pas
    {
        s.modulatable = false;
        return s;
    };
    auto pan = [] (ParamId id, std::string name)
    {
        return ParamSpec::continuous (id, std::move (name), Unit::Percent, -100.0, 100.0, 0.0);
    };

    bool ok = set.add (level (masterVolume, "Master Volume", 0.0), error)
           && set.add (pan (masterPan, "Master Pan"), error)
           && set.add (ParamSpec::toggle (masterMute, "Master Mute"), error)
           && set.add (unmodulated (ParamSpec::stepped (algorithm, "FM Algorithm", Unit::None, 1, 32, 1)), error);

    for (int t = 0; ok && t < tracks; ++t)
    {
        const std::string p = "Track " + std::to_string (t + 1) + " ";
        ok = set.add (level (trackParam (t, kVolume), p + "Volume", 0.0), error)
          && set.add (pan (trackParam (t, kPan), p + "Pan"), error)
          && set.add (ParamSpec::toggle (trackParam (t, kMute), p + "Mute"), error)
          && set.add (ParamSpec::toggle (trackParam (t, kSolo), p + "Solo"), error)
          && set.add (unmodulated (ParamSpec::choice (trackParam (t, kType), p + "Type",
                                                      { "Sampler", "Subtractive", "FM", "Audio In" })), error);

        for (int i = 0; ok && i < insertsPerTrack; ++i)
            ok = set.add (ParamSpec::toggle (trackParam (t, kInsertBypass + static_cast<ParamId> (i)),
                                             p + "Insert " + std::to_string (i + 1) + " Bypass"), error);

        for (int i = 0; ok && i < sendsPerTrack; ++i)
            ok = set.add (level (trackParam (t, kSendLevel + static_cast<ParamId> (i)),
                                 p + "Send " + std::to_string (i + 1) + " Level", -100.0), error);
    }
    return ok;
}
} // namespace neptune::core::probe
