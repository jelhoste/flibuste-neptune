#include "neptune/core/ParamGroups.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace neptune::core::paramgroups
{
namespace
{
// Mot suivant à partir de `pos` (saute les espaces) ; avance `pos` après lui.
std::string_view nextWord (const std::string& s, std::size_t& pos)
{
    while (pos < s.size() && s[pos] == ' ') ++pos;
    const auto start = pos;
    while (pos < s.size() && s[pos] != ' ') ++pos;
    return std::string_view (s).substr (start, pos - start);
}

bool isNumber (std::string_view w)
{
    if (w.empty()) return false;
    for (const char c : w)
        if (! std::isdigit (static_cast<unsigned char> (c))) return false;
    return true;
}

bool oneOf (std::string_view w, std::initializer_list<std::string_view> list)
{
    for (const auto v : list)
        if (w == v) return true;
    return false;
}

Family familyOfWord (std::string_view w)
{
    if (oneOf (w, { "Volume", "Pan", "Mute", "Solo", "Type", "Trim", "Mono" })) return Family::Strip;
    if (w == "Send") return Family::Sends;
    if (w == "Insert") return Family::Inserts;
    if (oneOf (w, { "Osc", "Sub", "Noise", "Pitch", "Amp", "Glide", "Unison" })) return Family::Synth;
    if (w == "Filter") return Family::Filters;
    if (w == "FM") return Family::Fm;
    if (oneOf (w, { "Upper", "Lower", "Vibrato", "Percussion", "Drive", "Pedal", "Leslie", "Reverb" })) return Family::Organ;
    if (oneOf (w, { "Gain", "Tune", "Fine" })) return Family::Sampler;
    if (w == "Slot") return Family::Zones;
    if (w == "Modulator") return Family::Modulators;
    if (w == "Router") return Family::Router;
    return Family::Other;
}
}

Group classify (const std::string& name)
{
    Group g;
    std::size_t pos = 0;
    const auto first = nextWord (name, pos);
    const auto afterFirst = pos;
    if (oneOf (first, { "Track", "Parallel", "Bus" }))
    {
        const auto number = nextWord (name, pos);
        if (isNumber (number))
        {
            g.scope = first == "Track" ? Scope::Track : first == "Parallel" ? Scope::Parallel : Scope::Bus;
            g.index = std::max (0, std::atoi (std::string (number).c_str()) - 1);
            g.family = familyOfWord (nextWord (name, pos));
            return g;
        }
        pos = afterFirst;
    }
    if (first == "Master")
    {
        g.scope = Scope::Master;
        g.family = familyOfWord (nextWord (name, pos));
        return g;
    }
    // Sans préfixe : les réglages d'instrument de la piste 1 (Gain, Tune, Fine tune, Filter…) ; le reste est global.
    g.family = familyOfWord (first);
    if (g.family == Family::Sampler || g.family == Family::Filters)
    {
        g.scope = Scope::Track;
        g.index = 0;
    }
    return g;
}

const char* familyKey (Family f) noexcept
{
    switch (f)
    {
        case Family::Strip: return "pfilter.strip";
        case Family::Sends: return "pfilter.sends";
        case Family::Inserts: return "pfilter.inserts";
        case Family::Synth: return "pfilter.synth";
        case Family::Filters: return "pfilter.filters";
        case Family::Fm: return "pfilter.fm";
        case Family::Organ: return "pfilter.organ";
        case Family::Sampler: return "pfilter.sampler";
        case Family::Zones: return "pfilter.zones";
        case Family::Modulators: return "pfilter.modulators";
        case Family::Router: return "pfilter.router";
        case Family::Other: break;
    }
    return "pfilter.other";
}
}
