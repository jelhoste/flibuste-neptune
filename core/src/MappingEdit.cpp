#include "neptune/core/MappingEdit.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace neptune::core::mapping
{
bool View::accepts (const ZoneDescription& z) const noexcept
{
    switch (kind)
    {
        case Kind::Release: return z.onRelease;
        case Kind::Group: return ! z.onRelease && z.group == group;
        default: return ! z.onRelease && z.group.empty();
    }
}

std::vector<Place> places (const InstrumentDescription& d, const View& view)
{
    using Key = std::tuple<int, int, int, int, std::string, bool>;
    std::map<Key, std::size_t> index;
    std::vector<Place> out;
    for (std::size_t i = 0; i < d.zones.size(); ++i)
    {
        const auto& z = d.zones[i];
        if (! view.accepts (z)) continue;
        const Key k { z.keyLow, z.keyHigh, z.velLow, z.velHigh, z.group, z.onRelease };
        auto it = index.find (k);
        if (it == index.end())
        {
            Place p;
            p.keyLow = z.keyLow; p.keyHigh = z.keyHigh; p.velLow = z.velLow; p.velHigh = z.velHigh;
            p.group = z.group; p.onRelease = z.onRelease;
            it = index.emplace (k, out.size()).first;
            out.push_back (std::move (p));
        }
        out[it->second].zones.push_back (static_cast<int> (i));
    }
    std::sort (out.begin(), out.end(), [] (const Place& a, const Place& b)
    {
        return std::tie (a.keyLow, a.velLow, a.keyHigh, a.velHigh) < std::tie (b.keyLow, b.velLow, b.keyHigh, b.velHigh);
    });

    // Chevauchements : balayage par note basse ; deux places se recouvrent si leurs plages de notes ET de vélocités se coupent.
    for (std::size_t i = 0; i < out.size(); ++i)
        for (std::size_t j = i + 1; j < out.size() && out[j].keyLow <= out[i].keyHigh; ++j)
            if (out[j].velLow <= out[i].velHigh && out[i].velLow <= out[j].velHigh)
                out[i].overlaps = out[j].overlaps = true;
    return out;
}

std::vector<std::string> groupNames (const InstrumentDescription& d)
{
    std::set<std::string> names;
    for (const auto& z : d.zones)
        if (! z.onRelease && ! z.group.empty()) names.insert (z.group);
    return { names.begin(), names.end() };
}

bool hasReleaseZones (const InstrumentDescription& d)
{
    return std::any_of (d.zones.begin(), d.zones.end(), [] (const ZoneDescription& z) { return z.onRelease; });
}

namespace
{
std::vector<int> validIndices (const InstrumentDescription& d, const std::vector<int>& zones)
{
    std::vector<int> out;
    std::set<int> seen;
    for (const int i : zones)
        if (i >= 0 && i < static_cast<int> (d.zones.size()) && seen.insert (i).second) out.push_back (i);
    return out;
}
} // namespace

InstrumentDescription moveZones (const InstrumentDescription& base, const std::vector<int>& zones, int dKey, int dVel, bool rootFollows,
                                 int* appliedKey, int* appliedVel)
{
    auto d = base;
    const auto idx = validIndices (base, zones);
    int minKey = 127, maxKey = 0, minVel = 127, maxVel = 1;
    for (const int i : idx)
    {
        const auto& z = base.zones[static_cast<std::size_t> (i)];
        minKey = std::min (minKey, z.keyLow); maxKey = std::max (maxKey, z.keyHigh);
        minVel = std::min (minVel, z.velLow); maxVel = std::max (maxVel, z.velHigh);
    }
    if (idx.empty()) { dKey = 0; dVel = 0; }
    else
    {
        dKey = std::clamp (dKey, -minKey, 127 - maxKey);
        dVel = std::clamp (dVel, 1 - minVel, 127 - maxVel);
    }
    for (const int i : idx)
    {
        auto& z = d.zones[static_cast<std::size_t> (i)];
        z.keyLow += dKey; z.keyHigh += dKey;
        z.velLow += dVel; z.velHigh += dVel;
        if (rootFollows) z.rootNote = std::clamp (z.rootNote + dKey, 0, 127);
    }
    if (appliedKey != nullptr) *appliedKey = dKey;
    if (appliedVel != nullptr) *appliedVel = dVel;
    return d;
}

InstrumentDescription resizeEdge (const InstrumentDescription& base, const std::vector<int>& zones, Edge edge, int delta, int* applied)
{
    auto d = base;
    const auto idx = validIndices (base, zones);
    int lo = std::numeric_limits<int>::min(), hi = std::numeric_limits<int>::max();
    for (const int i : idx)
    {
        const auto& z = base.zones[static_cast<std::size_t> (i)];
        int minD = 0, maxD = 0;
        switch (edge)
        {
            case Edge::KeyLow:  minD = -z.keyLow;      maxD = z.keyHigh - z.keyLow; break;
            case Edge::KeyHigh: minD = z.keyLow - z.keyHigh; maxD = 127 - z.keyHigh; break;
            case Edge::VelLow:  minD = 1 - z.velLow;   maxD = z.velHigh - z.velLow; break;
            case Edge::VelHigh: minD = z.velLow - z.velHigh; maxD = 127 - z.velHigh; break;
        }
        lo = std::max (lo, minD);
        hi = std::min (hi, maxD);
    }
    if (idx.empty() || lo > hi) delta = 0;
    else delta = std::clamp (delta, lo, hi);
    for (const int i : idx)
    {
        auto& z = d.zones[static_cast<std::size_t> (i)];
        switch (edge)
        {
            case Edge::KeyLow:  z.keyLow += delta; break;
            case Edge::KeyHigh: z.keyHigh += delta; break;
            case Edge::VelLow:  z.velLow += delta; break;
            case Edge::VelHigh: z.velHigh += delta; break;
        }
    }
    if (applied != nullptr) *applied = delta;
    return d;
}

InstrumentDescription duplicateZones (const InstrumentDescription& base, const std::vector<int>& zones, int dKey, int dVel,
                                      std::vector<int>* newIndices)
{
    auto d = base;
    const auto idx = validIndices (base, zones);
    // le décalage est borné comme pour un déplacement : les copies restent toutes dans la plage
    int ak = 0, av = 0;
    moveZones (base, idx, dKey, dVel, false, &ak, &av);
    if (newIndices != nullptr) newIndices->clear();
    for (const int i : idx)
    {
        ZoneDescription z = base.zones[static_cast<std::size_t> (i)];
        z.keyLow += ak; z.keyHigh += ak; z.velLow += av; z.velHigh += av;
        if (newIndices != nullptr) newIndices->push_back (static_cast<int> (d.zones.size()));
        d.zones.push_back (std::move (z));
    }
    return d;
}
} // namespace neptune::core::mapping
