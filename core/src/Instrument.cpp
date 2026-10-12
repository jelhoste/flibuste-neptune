#include "neptune/core/Instrument.h"

#include <algorithm>
#include <cmath>
#include <map>

#include <nlohmann/json.hpp>

#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
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
    z.releaseMs = std::isfinite (z.releaseMs) ? std::clamp (z.releaseMs, 1.0, 10000.0) : 50.0;
    z.tuneCents = std::isfinite (z.tuneCents) ? std::clamp (z.tuneCents, -1200.0, 1200.0) : 0.0;
    z.gainDb = std::isfinite (z.gainDb) ? std::clamp (z.gainDb, -60.0, 60.0) : 0.0;
    if (z.loopStart < 0) z.loopStart = 0;
    if (z.loopEnd >= 0 && z.loopEnd <= z.loopStart) z.loopEnd = -1; // incohérent : tout le fichier
    return z;
}

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
        o["release_ms"] = z.releaseMs;
        o["tune_cents"] = z.tuneCents;
        o["gain_db"] = z.gainDb;
        o["loop_start"] = z.loopStart;
        o["loop_end"] = z.loopEnd;
        zones.push_back (std::move (o));
    }
    nlohmann::json doc;
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
    z.releaseMs = readNum (o, "release_ms", z.releaseMs);
    z.tuneCents = readNum (o, "tune_cents", z.tuneCents);
    z.gainDb = readNum (o, "gain_db", z.gainDb);
    z.loopStart = readI64 (o, "loop_start", z.loopStart);
    z.loopEnd = readI64 (o, "loop_end", z.loopEnd);
    return sanitised (std::move (z));
}
} // namespace

InstrumentDescription instrumentFromJson (std::string_view text)
{
    InstrumentDescription d;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
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
        if (z.sample && key >= z.keyLow && key <= z.keyHigh && v >= z.velLow && v <= z.velHigh) return &z;
    return nullptr;
}

std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings,
                                                   SampleCache* externalCache)
{
    auto inst = std::make_shared<Instrument>();
    SampleCache localCache;
    SampleCache& cache = externalCache != nullptr ? *externalCache : localCache;
    for (const auto& zd0 : description.zones)
    {
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
        z.releaseSeconds = zd.releaseMs / 1000.0;
        z.tuneCents = zd.tuneCents;
        z.gain = static_cast<float> (std::pow (10.0, zd.gainDb / 20.0));
        const auto frames = z.sample->frames;
        z.loopStart = std::min<std::int64_t> (zd.loopStart, frames - 1);
        z.loopEnd = zd.loopEnd < 0 ? frames : std::min<std::int64_t> (zd.loopEnd, frames);
        if (z.loopEnd <= z.loopStart) { z.loopStart = 0; z.loopEnd = frames; }
        inst->zones.push_back (std::move (z));
    }
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
