#include "neptune/core/MappingEdit.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace neptune::core::mapping
{
View viewOf (const ZoneDescription& z) noexcept
{
    View v;
    v.group = z.group;
    if (z.onRelease) v.category = View::Category::Release;
    else if (z.trigger == TriggerCondition::First) v.category = View::Category::First;
    else if (z.trigger == TriggerCondition::Legato) v.category = View::Category::Legato;
    return v;
}

bool View::accepts (const ZoneDescription& z) const noexcept { return viewOf (z) == *this; }

std::vector<View> views (const InstrumentDescription& d)
{
    std::set<std::pair<std::string, int>> present;
    for (const auto c : { View::Category::Attack, View::Category::First, View::Category::Legato, View::Category::Release })
        present.insert ({ std::string(), static_cast<int> (c) });
    for (const auto& z : d.zones)
    {
        const auto v = viewOf (z);
        present.insert ({ v.group, static_cast<int> (v.category) });
    }
    // sans Groupe d'abord (ordre : attaque, premier, legato, relâchement), puis chaque Groupe
    std::vector<View> out;
    for (const auto& p : present)
    {
        View v;
        v.group = p.first;
        v.category = static_cast<View::Category> (p.second);
        out.push_back (v);
    }
    return out;
}

InstrumentDescription assignToView (const InstrumentDescription& base, const std::vector<int>& zones, const View& view, int firstKey)
{
    auto d = base;
    int offset = 0;
    for (const int i : zones)
    {
        if (i < 0 || i >= static_cast<int> (d.zones.size())) continue;
        auto& z = d.zones[static_cast<std::size_t> (i)];
        z.keyLow = z.keyHigh = std::clamp (firstKey + offset, 0, 127);
        z.velLow = 1;
        z.velHigh = 127;
        z.onRelease = view.category == View::Category::Release;
        z.trigger = view.category == View::Category::First ? TriggerCondition::First : view.category == View::Category::Legato ? TriggerCondition::Legato : TriggerCondition::Always;
        z.group = view.group;
        ++offset;
    }
    return d;
}

std::vector<Place> places (const InstrumentDescription& d, const View& view)
{
    using Key = std::tuple<int, int, int, int, std::string, bool, int>;
    std::map<Key, std::size_t> index;
    std::vector<Place> out;
    for (std::size_t i = 0; i < d.zones.size(); ++i)
    {
        const auto& z = d.zones[i];
        if (! view.accepts (z)) continue;
        const Key k { z.keyLow, z.keyHigh, z.velLow, z.velHigh, z.group, z.onRelease, static_cast<int> (z.trigger) };
        auto it = index.find (k);
        if (it == index.end())
        {
            Place p;
            p.keyLow = z.keyLow; p.keyHigh = z.keyHigh; p.velLow = z.velLow; p.velHigh = z.velHigh;
            p.group = z.group; p.onRelease = z.onRelease; p.trigger = z.trigger;
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

// ---- Groupes ----
std::vector<std::string> groupNames (const InstrumentDescription& d)
{
    std::vector<std::string> out = d.declaredGroups;
    std::set<std::string> used;
    for (const auto& z : d.zones)
        if (! z.group.empty() && std::find (out.begin(), out.end(), z.group) == out.end()) used.insert (z.group);
    out.insert (out.end(), used.begin(), used.end());
    return out;
}

std::string cleanGroupName (std::string name)
{
    const auto isSpace = [] (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (! name.empty() && isSpace (name.back())) name.pop_back();
    std::size_t first = 0;
    while (first < name.size() && isSpace (name[first])) ++first;
    name.erase (0, first);
    if (name.size() > 64)
    {
        std::size_t cut = 64;
        while (cut > 0 && (static_cast<unsigned char> (name[cut]) & 0xC0u) == 0x80u) --cut;   // pas au milieu d'un caractère UTF-8
        name.resize (cut);
    }
    return name;
}

namespace
{
// Retire ce qui visait des Groupes qui n'ont plus aucune zone.
void pruneGroups (InstrumentDescription& d)
{
    const auto alive = groupNames (d);
    auto isAlive = [&alive] (const std::string& g) { return std::find (alive.begin(), alive.end(), g) != alive.end(); };
    for (auto it = d.groups.begin(); it != d.groups.end();)
        it = isAlive (it->first) ? std::next (it) : d.groups.erase (it);
    for (auto& s : d.slots)
        if (s.isGroup && ! isAlive (s.group)) s = SlotAssignment {};
    d.unloadedGroups.erase (std::remove_if (d.unloadedGroups.begin(), d.unloadedGroups.end(), [&isAlive] (const std::string& g) { return ! isAlive (g); }),
                            d.unloadedGroups.end());
}
} // namespace

InstrumentDescription assignGroup (const InstrumentDescription& base, const std::vector<int>& zones, const std::string& group)
{
    InstrumentDescription d = base;
    const auto name = cleanGroupName (group);
    for (const int i : zones)
        if (i >= 0 && i < static_cast<int> (d.zones.size())) d.zones[static_cast<std::size_t> (i)].group = name;
    pruneGroups (d);
    return d;
}

bool renameGroup (InstrumentDescription& d, const std::string& from, const std::string& to)
{
    const auto name = cleanGroupName (to);
    if (name.empty() || name == from) return false;
    const auto names = groupNames (d);
    const auto has = [&names] (const std::string& g) { return std::find (names.begin(), names.end(), g) != names.end(); };
    if (! has (from) || has (name)) return false;
    for (auto& g : d.declaredGroups)
        if (g == from) g = name;
    for (auto& z : d.zones)
        if (z.group == from) z.group = name;
    if (const auto it = d.groups.find (from); it != d.groups.end())
    {
        d.groups[name] = it->second;
        d.groups.erase (from);
    }
    for (auto& s : d.slots)
        if (s.isGroup && s.group == from) s.group = name;
    for (auto& g : d.unloadedGroups)
        if (g == from) g = name;
    return true;
}

bool addGroup (InstrumentDescription& d, const std::string& name)
{
    const auto clean = cleanGroupName (name);
    if (clean.empty()) return false;
    const auto names = groupNames (d);
    if (std::find (names.begin(), names.end(), clean) != names.end()) return false;
    d.declaredGroups.push_back (clean);
    return true;
}

InstrumentDescription deleteGroup (const InstrumentDescription& base, const std::string& group)
{
    InstrumentDescription d = base;
    d.declaredGroups.erase (std::remove (d.declaredGroups.begin(), d.declaredGroups.end(), group), d.declaredGroups.end());
    for (auto& z : d.zones)
        if (z.group == group) z.group.clear();
    pruneGroups (d);
    return d;
}
int groupSwitch (InstrumentDescription& d, const std::string& group)
{
    const auto names = groupNames (d);
    if (group.empty() || std::find (names.begin(), names.end(), group) == names.end()) return -1;
    const auto it = d.groups.find (group);
    const std::uint16_t mask = it != d.groups.end() ? it->second.modulators : 0;
    for (int i = 0; i < kModulators; ++i)
        if (mask == static_cast<std::uint16_t> (1u << i)) return i;
    std::uint16_t used = d.trackModulators;
    for (const auto& z : d.zones) used = static_cast<std::uint16_t> (used | z.modulators);
    for (const auto& entry : d.groups) used = static_cast<std::uint16_t> (used | entry.second.modulators);
    for (int i = 0; i < kModulators; ++i)
    {
        const auto bit = static_cast<std::uint16_t> (1u << i);
        if ((used & bit) != 0 || ! d.modulators[static_cast<std::size_t> (i)].isDefault()) continue;
        d.modulators[static_cast<std::size_t> (i)].name = group;
        d.groups[group].modulators = static_cast<std::uint16_t> (d.groups[group].modulators | bit);
        return i;
    }
    return -1;
}
} // namespace neptune::core::mapping
