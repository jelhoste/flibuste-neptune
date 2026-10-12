#include "neptune/core/Instrument.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <nlohmann/json.hpp>

#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
const char* toString (RoundRobinMode m) noexcept
{
    switch (m)
    {
        case RoundRobinMode::Random: return "random";
        case RoundRobinMode::RandomNoRepeat: return "random_no_repeat";
        default: return "sequential";
    }
}

bool roundRobinModeFromString (std::string_view s, RoundRobinMode& out)
{
    if (s == "sequential") { out = RoundRobinMode::Sequential; return true; }
    if (s == "random") { out = RoundRobinMode::Random; return true; }
    if (s == "random_no_repeat") { out = RoundRobinMode::RandomNoRepeat; return true; }
    return false;
}

const char* toString (PlayMode m) noexcept { return m == PlayMode::Loop ? "loop" : "one_shot"; }

bool playModeFromString (std::string_view s, PlayMode& out)
{
    if (s == "one_shot") { out = PlayMode::OneShot; return true; }
    if (s == "loop") { out = PlayMode::Loop; return true; }
    return false;
}

ZoneDescription sanitised (ZoneDescription z)
{
    z.keyLow = std::clamp (z.keyLow, 0, 127);
    z.keyHigh = std::clamp (z.keyHigh, 0, 127);
    if (z.keyLow > z.keyHigh) std::swap (z.keyLow, z.keyHigh);
    z.velLow = std::clamp (z.velLow, 1, 127);
    z.velHigh = std::clamp (z.velHigh, 1, 127);
    if (z.velLow > z.velHigh) std::swap (z.velLow, z.velHigh);
    z.rootNote = std::clamp (z.rootNote, 0, 127);
    z.envelope = sanitised (z.envelope);
    z.tuneCents = std::isfinite (z.tuneCents) ? std::clamp (z.tuneCents, -1200.0, 1200.0) : 0.0;
    z.gainDb = std::isfinite (z.gainDb) ? std::clamp (z.gainDb, -60.0, 60.0) : 0.0;
    z.roundRobin = std::clamp (z.roundRobin, 0, 100000);
    if (z.group.size() > 128) z.group.resize (128);
    if (z.loopStart < 0) z.loopStart = 0;
    if (z.loopEnd >= 0 && z.loopEnd <= z.loopStart) z.loopEnd = -1; // incohérent : tout le fichier
    return z;
}

namespace
{
nlohmann::json segmentToJson (const EnvelopeSegment& s, bool withCurve)
{
    nlohmann::json o;
    o["on"] = s.on;
    o["ms"] = s.ms;
    if (withCurve)
    {
        o["shape"] = s.curve.shape;
        o["db"] = s.curve.db;
    }
    return o;
}

nlohmann::json envelopeToJson (const EnvelopeDescription& e)
{
    nlohmann::json o;
    o["delay"] = segmentToJson (e.delay, false);
    o["attack"] = segmentToJson (e.attack, true);
    o["hold"] = segmentToJson (e.hold, false);
    o["decay"] = segmentToJson (e.decay, true);
    o["sustain"] = e.sustain;
    o["release"] = segmentToJson (e.release, true);
    return o;
}

void segmentFromJson (const nlohmann::json& parent, const char* key, EnvelopeSegment& s)
{
    const auto it = parent.find (key);
    if (it == parent.end() || ! it->is_object()) return;
    if (const auto v = it->find ("on"); v != it->end() && v->is_boolean()) s.on = v->get<bool>();
    if (const auto v = it->find ("ms"); v != it->end() && v->is_number() && std::isfinite (v->get<double>())) s.ms = v->get<double>();
    if (const auto v = it->find ("shape"); v != it->end() && v->is_number() && std::isfinite (v->get<double>())) s.curve.shape = v->get<double>();
    if (const auto v = it->find ("db"); v != it->end() && v->is_boolean()) s.curve.db = v->get<bool>();
}

EnvelopeDescription envelopeFromJson (const nlohmann::json& o)
{
    EnvelopeDescription e;
    const auto it = o.find ("envelope");
    if (it != o.end() && it->is_object())
    {
        segmentFromJson (*it, "delay", e.delay);
        segmentFromJson (*it, "attack", e.attack);
        segmentFromJson (*it, "hold", e.hold);
        segmentFromJson (*it, "decay", e.decay);
        segmentFromJson (*it, "release", e.release);
        if (const auto v = it->find ("sustain"); v != it->end() && v->is_number() && std::isfinite (v->get<double>()))
            e.sustain = v->get<double>();
    }
    else if (const auto r = o.find ("release_ms"); r != o.end() && r->is_number() && std::isfinite (r->get<double>()))
    {
        e.release.on = true; // projets du jalon 1 : le relâchement seul
        e.release.ms = r->get<double>();
    }
    return sanitised (std::move (e));
}
} // namespace

std::string toJson (const InstrumentDescription& d)
{
    nlohmann::json zones = nlohmann::json::array();
    for (const auto& z : d.zones)
    {
        nlohmann::json o;
        o["sample"] = z.samplePath;
        o["key_low"] = z.keyLow;
        o["key_high"] = z.keyHigh;
        o["vel_low"] = z.velLow;
        o["vel_high"] = z.velHigh;
        o["root_note"] = z.rootNote;
        o["mode"] = toString (z.mode);
        o["envelope"] = envelopeToJson (z.envelope);
        o["tune_cents"] = z.tuneCents;
        o["gain_db"] = z.gainDb;
        o["loop_start"] = z.loopStart;
        o["loop_end"] = z.loopEnd;
        if (z.roundRobin != 0) o["round_robin"] = z.roundRobin;
        if (! z.group.empty()) o["group"] = z.group;
        if (z.onRelease) o["on_release"] = true;
        zones.push_back (std::move (o));
    }
    nlohmann::json doc;
    doc["round_robin_mode"] = toString (d.roundRobinMode);
    doc["zones"] = std::move (zones);
    return doc.dump();
}

namespace
{
int readInt (const nlohmann::json& o, const char* k, int def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_number_integer() ? static_cast<int> (std::clamp<long long> (it->get<long long>(), -1000000, 1000000)) : def;
}
double readNum (const nlohmann::json& o, const char* k, double def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_number() && std::isfinite (it->get<double>()) ? it->get<double>() : def;
}
std::int64_t readI64 (const nlohmann::json& o, const char* k, std::int64_t def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_number_integer() ? it->get<std::int64_t>() : def;
}

ZoneDescription zoneFromJson (const nlohmann::json& o)
{
    ZoneDescription z;
    if (const auto it = o.find ("sample"); it != o.end() && it->is_string()) z.samplePath = it->get<std::string>();
    z.keyLow = readInt (o, "key_low", z.keyLow);
    z.keyHigh = readInt (o, "key_high", z.keyHigh);
    z.velLow = readInt (o, "vel_low", z.velLow);
    z.velHigh = readInt (o, "vel_high", z.velHigh);
    z.rootNote = readInt (o, "root_note", z.rootNote);
    if (const auto it = o.find ("mode"); it != o.end() && it->is_string()) playModeFromString (it->get<std::string>(), z.mode);
    z.envelope = envelopeFromJson (o);
    z.tuneCents = readNum (o, "tune_cents", z.tuneCents);
    z.gainDb = readNum (o, "gain_db", z.gainDb);
    z.loopStart = readI64 (o, "loop_start", z.loopStart);
    z.loopEnd = readI64 (o, "loop_end", z.loopEnd);
    z.roundRobin = readInt (o, "round_robin", 0);
    if (const auto it = o.find ("group"); it != o.end() && it->is_string()) z.group = it->get<std::string>();
    if (const auto it = o.find ("on_release"); it != o.end() && it->is_boolean()) z.onRelease = it->get<bool>();
    return sanitised (std::move (z));
}
} // namespace

InstrumentDescription instrumentFromJson (std::string_view text)
{
    InstrumentDescription d;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
    if (const auto m = doc.find ("round_robin_mode"); m != doc.end() && m->is_string())
        roundRobinModeFromString (m->get<std::string>(), d.roundRobinMode);
    const auto zones = doc.find ("zones");
    if (zones == doc.end() || ! zones->is_array()) return d;
    constexpr std::size_t kMaxZones = 4096;
    for (const auto& z : *zones)
    {
        if (d.zones.size() >= kMaxZones) break;
        if (z.is_object()) d.zones.push_back (zoneFromJson (z));
    }
    return d;
}

const Zone* Instrument::find (int key, float velocity) const noexcept
{
    const int v = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127);
    for (const auto& z : zones)
        if (z.sample && ! z.onRelease && key >= z.keyLow && key <= z.keyHigh && v >= z.velLow && v <= z.velHigh) return &z;
    return nullptr;
}

void Instrument::finalize()
{
    rrSets_.clear();
    zoneSet_.assign (zones.size(), -1);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const Zone& z = zones[i];
        int found = -1;
        for (std::size_t s = 0; s < rrSets_.size() && found < 0; ++s)
        {
            const Zone& first = zones[static_cast<std::size_t> (rrSets_[s].members.front())];
            if (first.keyLow == z.keyLow && first.keyHigh == z.keyHigh && first.velLow == z.velLow && first.velHigh == z.velHigh
                && first.groupId == z.groupId && first.onRelease == z.onRelease)
                found = static_cast<int> (s);
        }
        if (found < 0)
        {
            rrSets_.push_back ({});
            found = static_cast<int> (rrSets_.size()) - 1;
        }
        rrSets_[static_cast<std::size_t> (found)].members.push_back (static_cast<int> (i));
        zoneSet_[i] = found;
    }
    for (auto& set : rrSets_) // ordre des indices round-robin, puis ordre de déclaration
        std::stable_sort (set.members.begin(), set.members.end(),
                          [this] (int a, int b) { return zones[static_cast<std::size_t> (a)].roundRobin < zones[static_cast<std::size_t> (b)].roundRobin; });
    rrCounter_.assign (rrSets_.size(), 0);
    rrLast_.assign (rrSets_.size(), 0xFFFFFFFFu);
}

const Zone* Instrument::select (int key, float velocity, bool release) const noexcept
{
    const int v = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const Zone& z = zones[i];
        if (! z.sample || z.onRelease != release || key < z.keyLow || key > z.keyHigh || v < z.velLow || v > z.velHigh) continue;
        if (zoneSet_.size() != zones.size()) return &z; // finalize() non appelé : première zone
        const auto& members = rrSets_[static_cast<std::size_t> (zoneSet_[i])].members;
        if (members.size() <= 1) return &z;
        const auto set = static_cast<std::size_t> (zoneSet_[i]);
        const auto n = static_cast<std::uint32_t> (members.size());
        std::uint32_t pick = 0;
        auto next = [this] { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; };
        switch (roundRobinMode)
        {
            case RoundRobinMode::Sequential: pick = rrCounter_[set]++ % n; break;
            case RoundRobinMode::Random: pick = next() % n; break;
            case RoundRobinMode::RandomNoRepeat:
                pick = next() % n;
                if (pick == rrLast_[set]) pick = (pick + 1 + next() % (n - 1)) % n;
                break;
        }
        rrLast_[set] = pick;
        return &zones[static_cast<std::size_t> (members[pick])];
    }
    return nullptr;
}

std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings,
                                                   SampleCache* externalCache, const std::function<bool (int, int)>& progress)
{
    auto inst = std::make_shared<Instrument>();
    inst->roundRobinMode = description.roundRobinMode;
    std::map<std::string, int> groupIds;
    SampleCache localCache;
    SampleCache& cache = externalCache != nullptr ? *externalCache : localCache;
    int done = 0;
    const int total = static_cast<int> (description.zones.size());
    for (const auto& zd0 : description.zones)
    {
        if (progress && ! progress (done, total)) return nullptr;
        ++done;
        const ZoneDescription zd = sanitised (zd0);
        auto it = cache.find (zd.samplePath);
        if (it == cache.end())
        {
            auto r = loadSampleFile (std::filesystem::u8path (zd.samplePath));
            if (! r.ok())
            {
                if (warnings) warnings->push_back (zd.samplePath + ": " + r.error);
                continue;
            }
            it = cache.emplace (zd.samplePath, std::move (r.sample)).first;
        }
        Zone z;
        z.sample = it->second;
        z.keyLow = zd.keyLow; z.keyHigh = zd.keyHigh; z.velLow = zd.velLow; z.velHigh = zd.velHigh;
        z.rootNote = zd.rootNote;
        z.mode = zd.mode;
        z.env = EnvelopeRuntime (zd.envelope);
        z.tuneCents = zd.tuneCents;
        z.gain = static_cast<float> (std::pow (10.0, zd.gainDb / 20.0));
        const auto frames = z.sample->frames;
        z.roundRobin = zd.roundRobin;
        z.onRelease = zd.onRelease;
        if (! zd.group.empty())
            z.groupId = groupIds.emplace (zd.group, static_cast<int> (groupIds.size()) + 1).first->second;
        z.loopStart = std::min<std::int64_t> (zd.loopStart, frames - 1);
        z.loopEnd = zd.loopEnd < 0 ? frames : std::min<std::int64_t> (zd.loopEnd, frames);
        if (z.loopEnd <= z.loopStart) { z.loopStart = 0; z.loopEnd = frames; }
        inst->zones.push_back (std::move (z));
    }
    inst->finalize();
    // On ne garde en cache que ce que la description utilise encore.
    for (auto it = cache.begin(); it != cache.end();)
    {
        const bool used = std::any_of (description.zones.begin(), description.zones.end(),
                                       [&] (const ZoneDescription& zd) { return zd.samplePath == it->first; });
        it = used ? std::next (it) : cache.erase (it);
    }
    return inst;
}

Zone makeFullKeyboardZone (std::shared_ptr<const SampleData> sample, int rootNote, PlayMode mode)
{
    Zone z;
    z.rootNote = rootNote;
    z.mode = mode;
    z.loopStart = 0;
    z.loopEnd = sample ? sample->frames : 0;
    z.sample = std::move (sample);
    return z;
}
} // namespace neptune::core
