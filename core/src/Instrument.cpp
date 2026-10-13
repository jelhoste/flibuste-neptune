#include "neptune/core/Instrument.h"
#include "neptune/core/DescriptionJson.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

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

const char* toString (PolyphonyMode m) noexcept
{
    switch (m) { case PolyphonyMode::Mono: return "mono"; case PolyphonyMode::Legato: return "legato"; default: return "poly"; }
}
const char* toString (VoiceStealMode m) noexcept
{
    switch (m)
    {
        case VoiceStealMode::Quietest: return "quietest";
        case VoiceStealMode::KeepLow: return "keep_low";
        case VoiceStealMode::KeepHigh: return "keep_high";
        default: return "oldest";
    }
}
const char* toString (SameKeyMode m) noexcept { return m == SameKeyMode::Cut ? "cut" : "stack"; }

const char* toString (TriggerCondition t) noexcept
{
    switch (t) { case TriggerCondition::First: return "first"; case TriggerCondition::Legato: return "legato"; default: return "always"; }
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
    z.chokeCuts = std::clamp (z.chokeCuts, 0, maxChokeGroup);
    z.chokeCutBy = std::clamp (z.chokeCutBy, 0, maxChokeGroup);
    z.filters = sanitised (std::move (z.filters));
    if (z.onRelease) z.trigger = TriggerCondition::Always; // la condition ne concerne que les zones d'attaque
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

const char* filterModeName (FilterMode m) { return m == FilterMode::HighPass ? "high_pass" : m == FilterMode::BandPass ? "band_pass" : m == FilterMode::Notch ? "notch" : "low_pass"; }

nlohmann::json envelopeToJson (const EnvelopeDescription& e);

nlohmann::json filterToJson (const FilterSettings& f)
{
    nlohmann::json o;
    o["on"] = f.on;
    o["model"] = f.model == FilterModel::Ladder ? "ladder" : "svf";
    o["mode"] = filterModeName (f.mode);
    o["slope"] = f.slopeDb;
    o["cutoff_hz"] = f.cutoffHz;
    o["resonance"] = f.resonance;
    o["key_track"] = f.keyTrack;
    o["velocity_octaves"] = f.velocityOctaves;
    o["env_depth_octaves"] = f.envDepthOctaves;
    o["drive"] = f.drive;
    o["envelope"] = envelopeToJson (f.env);
    return o;
}

nlohmann::json filterBlockToJson (const FilterBlock& b)
{
    nlohmann::json o;
    o["filter1"] = filterToJson (b.filter[0]);
    o["filter2"] = filterToJson (b.filter[1]);
    o["parallel"] = b.parallel;
    o["balance"] = b.balance;
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
        if (z.trigger != TriggerCondition::Always) o["trigger"] = toString (z.trigger);
        if (z.chokeCuts != 0) o["choke_cuts"] = z.chokeCuts;
        if (z.chokeCutBy != 0) o["choke_cut_by"] = z.chokeCutBy;
        if (z.filterOverride) o["filters"] = filterBlockToJson (z.filters);
        zones.push_back (std::move (o));
    }
    nlohmann::json doc;
    doc["round_robin_mode"] = toString (d.roundRobinMode);
    if (d.mainRoundRobinOnly) doc["main_round_robin_only"] = true;
    if (! d.loadReleaseSamples) doc["load_release_samples"] = false;
    if (! d.unloadedGroups.empty()) doc["unloaded_groups"] = d.unloadedGroups;
    if (d.polyphony != PolyphonyMode::Poly) doc["polyphony"] = toString (d.polyphony);
    if (d.maxVoices != InstrumentDescription::defaultMaxVoices) doc["max_voices"] = d.maxVoices;
    if (d.stealMode != VoiceStealMode::Oldest) doc["voice_steal"] = toString (d.stealMode);
    if (d.sameKey != SameKeyMode::Stack) doc["same_key"] = toString (d.sameKey);
    if (d.chokeSameZone) doc["choke_same_zone"] = true;
    if (d.cutFadeMs != 3) doc["cut_fade_ms"] = d.cutFadeMs;
    if (d.glideMs != 0) doc["glide_ms"] = d.glideMs;
    if (d.velocityFadeWidth != 0) doc["velocity_fade_width"] = d.velocityFadeWidth;
    if (d.velocityFadeLinear) doc["velocity_fade_linear"] = true;
    if (! d.velocityCurve.isLinear())
    {
        nlohmann::json c;
        c["shape"] = d.velocityCurve.shape;
        nlohmann::json pts = nlohmann::json::array();
        for (const auto& p : d.velocityCurve.points) pts.push_back ({ p.first, p.second });
        c["points"] = std::move (pts);
        doc["velocity_curve"] = std::move (c);
    }
    if (! (d.defaultFilters == FilterBlock {})) doc["default_filters"] = filterBlockToJson (d.defaultFilters);
    if (d.streamFromDisk) doc["stream_from_disk"] = true;
    if (d.preloadKB != InstrumentDescription::defaultPreloadKB) doc["preload_kb"] = d.preloadKB;
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

FilterSettings filterFromJson (const nlohmann::json& o)
{
    FilterSettings f;
    if (! o.is_object()) return f;
    if (const auto it = o.find ("on"); it != o.end() && it->is_boolean()) f.on = it->get<bool>();
    if (const auto it = o.find ("model"); it != o.end() && it->is_string() && it->get<std::string>() == "ladder") f.model = FilterModel::Ladder;
    if (const auto it = o.find ("mode"); it != o.end() && it->is_string())
    {
        const auto m = it->get<std::string>();
        f.mode = m == "high_pass" ? FilterMode::HighPass : m == "band_pass" ? FilterMode::BandPass : m == "notch" ? FilterMode::Notch : FilterMode::LowPass;
    }
    f.slopeDb = readInt (o, "slope", 12);
    f.cutoffHz = readNum (o, "cutoff_hz", f.cutoffHz);
    f.resonance = readNum (o, "resonance", 0.0);
    f.keyTrack = readNum (o, "key_track", 0.0);
    f.velocityOctaves = readNum (o, "velocity_octaves", 0.0);
    f.envDepthOctaves = readNum (o, "env_depth_octaves", 0.0);
    f.drive = readNum (o, "drive", 0.0);
    f.env = envelopeFromJson (o);
    return sanitised (std::move (f));
}

FilterBlock filterBlockFromJson (const nlohmann::json& o)
{
    FilterBlock b;
    if (! o.is_object()) return b;
    if (const auto it = o.find ("filter1"); it != o.end()) b.filter[0] = filterFromJson (*it);
    if (const auto it = o.find ("filter2"); it != o.end()) b.filter[1] = filterFromJson (*it);
    if (const auto it = o.find ("parallel"); it != o.end() && it->is_boolean()) b.parallel = it->get<bool>();
    b.balance = readNum (o, "balance", 0.5);
    return sanitised (std::move (b));
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
    if (const auto it = o.find ("trigger"); it != o.end() && it->is_string())
    {
        if (it->get<std::string>() == "first") z.trigger = TriggerCondition::First;
        else if (it->get<std::string>() == "legato") z.trigger = TriggerCondition::Legato;
    }
    if (const auto it = o.find ("filters"); it != o.end() && it->is_object()) { z.filterOverride = true; z.filters = filterBlockFromJson (*it); }
    z.chokeCuts = readInt (o, "choke_cuts", 0);
    z.chokeCutBy = readInt (o, "choke_cut_by", 0);
    return sanitised (std::move (z));
}
} // namespace

namespace detail
{
nlohmann::json envelopeJson (const EnvelopeDescription& e) { return envelopeToJson (e); }
EnvelopeDescription envelopeFrom (const nlohmann::json& parent) { return envelopeFromJson (parent); }
nlohmann::json filterJson (const FilterSettings& f) { return filterToJson (f); }
FilterSettings filterFrom (const nlohmann::json& o) { return filterFromJson (o); }
int jsonInt (const nlohmann::json& o, const char* key, int def) { return readInt (o, key, def); }
double jsonNum (const nlohmann::json& o, const char* key, double def) { return readNum (o, key, def); }
} // namespace detail

InstrumentDescription instrumentFromJson (std::string_view text)
{
    InstrumentDescription d;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
    if (const auto m = doc.find ("round_robin_mode"); m != doc.end() && m->is_string())
        roundRobinModeFromString (m->get<std::string>(), d.roundRobinMode);
    if (const auto m = doc.find ("main_round_robin_only"); m != doc.end() && m->is_boolean()) d.mainRoundRobinOnly = m->get<bool>();
    if (const auto m = doc.find ("load_release_samples"); m != doc.end() && m->is_boolean()) d.loadReleaseSamples = m->get<bool>();
    auto readString = [&] (const char* k) -> std::string
    {
        const auto it = doc.find (k);
        return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    if (const auto v = readString ("polyphony"); v == "mono") d.polyphony = PolyphonyMode::Mono;
    else if (v == "legato") d.polyphony = PolyphonyMode::Legato;
    d.maxVoices = std::clamp (readInt (doc, "max_voices", InstrumentDescription::defaultMaxVoices), 1, InstrumentDescription::maxVoicesLimit);
    if (const auto v = readString ("voice_steal"); v == "quietest") d.stealMode = VoiceStealMode::Quietest;
    else if (v == "keep_low") d.stealMode = VoiceStealMode::KeepLow;
    else if (v == "keep_high") d.stealMode = VoiceStealMode::KeepHigh;
    if (readString ("same_key") == "cut") d.sameKey = SameKeyMode::Cut;
    if (const auto m = doc.find ("choke_same_zone"); m != doc.end() && m->is_boolean()) d.chokeSameZone = m->get<bool>();
    d.cutFadeMs = std::clamp (readInt (doc, "cut_fade_ms", 3), 1, InstrumentDescription::maxCutFadeMs);
    d.glideMs = std::clamp (readInt (doc, "glide_ms", 0), 0, InstrumentDescription::maxGlideMs);
    d.velocityFadeWidth = std::clamp (readInt (doc, "velocity_fade_width", 0), 0, InstrumentDescription::maxVelocityFadeWidth);
    if (const auto m = doc.find ("velocity_fade_linear"); m != doc.end() && m->is_boolean()) d.velocityFadeLinear = m->get<bool>();
    if (const auto m = doc.find ("velocity_curve"); m != doc.end() && m->is_object())
    {
        d.velocityCurve.shape = readNum (*m, "shape", 0.0);
        if (const auto pts = m->find ("points"); pts != m->end() && pts->is_array())
            for (const auto& p : *pts)
                if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number() && d.velocityCurve.points.size() < 64)
                    d.velocityCurve.points.push_back ({ p[0].get<double>(), p[1].get<double>() });
        d.velocityCurve = sanitised (d.velocityCurve);
    }
    if (const auto m = doc.find ("default_filters"); m != doc.end() && m->is_object()) d.defaultFilters = filterBlockFromJson (*m);
    if (const auto m = doc.find ("stream_from_disk"); m != doc.end() && m->is_boolean()) d.streamFromDisk = m->get<bool>();
    d.preloadKB = std::clamp (readInt (doc, "preload_kb", InstrumentDescription::defaultPreloadKB),
                              InstrumentDescription::minPreloadKB, InstrumentDescription::maxPreloadKB);
    if (const auto m = doc.find ("unloaded_groups"); m != doc.end() && m->is_array())
        for (const auto& g : *m)
            if (g.is_string() && g.get<std::string>().size() <= 128 && d.unloadedGroups.size() < 256) d.unloadedGroups.push_back (g.get<std::string>());
    const auto zones = doc.find ("zones");
    if (zones == doc.end() || ! zones->is_array()) return d;
    constexpr std::size_t kMaxZones = 32768; // un piano 88 notes x ~30 vélocités x round-robin dépasse 4096
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
    zoneFamily_.assign (zones.size(), -1);
    familySets_.clear();
    for (auto& k : keyZones_) k.clear();

    using PlaceKey = std::tuple<int, int, int, int, int, bool, int>;
    using FamilyKey = std::tuple<int, int, int, bool, int>;
    std::map<PlaceKey, int> setIndex;
    std::map<FamilyKey, int> familyIndex;
    std::vector<LayerKey> keys (zones.size());
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const Zone& z = zones[i];
        keys[i] = { z.keyLow, z.keyHigh, z.velLow, z.velHigh, z.groupId, z.onRelease, static_cast<int> (z.trigger) };
        const int trig = static_cast<int> (z.trigger);
        auto it = setIndex.find ({ z.keyLow, z.keyHigh, z.velLow, z.velHigh, z.groupId, z.onRelease, trig });
        if (it == setIndex.end())
        {
            it = setIndex.emplace (PlaceKey { z.keyLow, z.keyHigh, z.velLow, z.velHigh, z.groupId, z.onRelease, trig }, static_cast<int> (rrSets_.size())).first;
            rrSets_.push_back ({});
            const FamilyKey fk { z.keyLow, z.keyHigh, z.groupId, z.onRelease, trig };
            auto fit = familyIndex.find (fk);
            if (fit == familyIndex.end())
            {
                fit = familyIndex.emplace (fk, static_cast<int> (familySets_.size())).first;
                familySets_.emplace_back();
            }
            familySets_[static_cast<std::size_t> (fit->second)].push_back (it->second);
        }
        rrSets_[static_cast<std::size_t> (it->second)].members.push_back (static_cast<int> (i));
        zoneSet_[i] = it->second;
        zoneFamily_[i] = familyIndex[FamilyKey { z.keyLow, z.keyHigh, z.groupId, z.onRelease, trig }];
        for (int k = std::max (0, z.keyLow); k <= std::min (127, z.keyHigh); ++k) keyZones_[static_cast<std::size_t> (k)].push_back (static_cast<int> (i));
    }
    for (auto& set : rrSets_) // ordre des indices round-robin, puis ordre de déclaration
        std::stable_sort (set.members.begin(), set.members.end(),
                          [this] (int a, int b) { return zones[static_cast<std::size_t> (a)].roundRobin < zones[static_cast<std::size_t> (b)].roundRobin; });
    rrCounter_.assign (rrSets_.size(), 0);
    rrLast_.assign (rrSets_.size(), 0xFFFFFFFFu);
    fades_ = computeLayerFades (keys, fadeWidth);
}

const Zone* Instrument::pickFromSet (std::size_t set) const noexcept
{
    const auto& members = rrSets_[set].members;
    if (members.size() <= 1) return &zones[static_cast<std::size_t> (members.front())];
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

const Zone* Instrument::select (int key, float velocity, bool release) const noexcept
{
    const int v = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const Zone& z = zones[i];
        if (! z.sample || z.onRelease != release || key < z.keyLow || key > z.keyHigh || v < z.velLow || v > z.velHigh) continue;
        if (zoneSet_.size() != zones.size()) return &z; // finalize() non appelé : première zone
        return pickFromSet (static_cast<std::size_t> (zoneSet_[i]));
    }
    return nullptr;
}

int Instrument::selectLayers (int key, float velocity, bool release, Pick* out, int maxOut, const HeldQuery* held) const noexcept
{
    if (out == nullptr || maxOut <= 0 || key < 0 || key > 127) return 0;
    const float vCont = std::clamp (velocity * 127.0f, 1.0f, 127.0f);
    const int vInt = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127);

    if (zoneSet_.size() != zones.size()) // finalize() non appelé : comme select()
    {
        const Zone* z = select (key, velocity, release);
        if (z == nullptr) return 0;
        out[0] = { z, 1.0f };
        return 1;
    }
    // Deux passes : les zones à condition (« Legato », puis « Premier ») dont la condition est satisfaite passent avant les
    // zones d'attaque normales, QUEL QUE SOIT l'ordre de déclaration. Dans une passe, la première zone qui couvre la note gagne.
    int family = -1;
    for (int pass = 0; pass < 3 && family < 0; ++pass) // 0 : Legato, 1 : Premier, 2 : toujours (et relâchement)
    {
        for (const int zi : keyZones_[static_cast<std::size_t> (key)])
        {
            const Zone& z = zones[static_cast<std::size_t> (zi)];
            if (! z.sample || z.onRelease != release || vInt < z.velLow || vInt > z.velHigh) continue;
            const TriggerCondition t = release ? TriggerCondition::Always : z.trigger;
            if (pass == 0 && t != TriggerCondition::Legato) continue;
            if (pass == 1 && t != TriggerCondition::First) continue;
            if (pass == 2 && t != TriggerCondition::Always) continue;
            if (pass < 2)
            {
                const bool other = held != nullptr && held->otherHeld != nullptr && held->otherHeld (held->context, z.groupId);
                if (t == TriggerCondition::First && other) continue;    // une autre note du Groupe est tenue
                if (t == TriggerCondition::Legato && ! other) continue; // rien de tenu : pas de transition liée
            }
            family = zoneFamily_[static_cast<std::size_t> (zi)];
            break;
        }
    }
    if (family < 0) return 0;

    int n = 0;
    for (const int set : familySets_[static_cast<std::size_t> (family)])
    {
        const int rep = rrSets_[static_cast<std::size_t> (set)].members.front();
        const float g = layerGain (fades_[static_cast<std::size_t> (rep)], vCont, fadeLinear);
        if (g <= 1e-4f) continue;
        const Zone* z = pickFromSet (static_cast<std::size_t> (set));
        if (z == nullptr || ! z->sample) continue;
        out[n++] = { z, g };
        if (n >= maxOut) break;
    }
    return n;
}

std::vector<bool> loadedZones (const InstrumentDescription& d)
{
    std::vector<bool> mask (d.zones.size(), true);
    for (std::size_t i = 0; i < d.zones.size(); ++i)
    {
        const auto& z = d.zones[i];
        if (z.onRelease && ! d.loadReleaseSamples) mask[i] = false;
        else if (! z.group.empty() && std::find (d.unloadedGroups.begin(), d.unloadedGroups.end(), z.group) != d.unloadedGroups.end()) mask[i] = false;
    }
    if (d.mainRoundRobinOnly)
    {
        using Place = std::tuple<std::string, bool, int, int, int, int, int>;
        std::map<Place, int> best;
        auto place = [] (const ZoneDescription& z) { return Place { z.group, z.onRelease, z.keyLow, z.keyHigh, z.velLow, z.velHigh, static_cast<int> (z.trigger) }; };
        for (std::size_t i = 0; i < d.zones.size(); ++i)
            if (mask[i])
            {
                const auto p = place (d.zones[i]);
                const auto f = best.find (p);
                if (f == best.end() || d.zones[i].roundRobin < f->second) best[p] = d.zones[i].roundRobin;
            }
        std::map<Place, bool> taken;
        for (std::size_t i = 0; i < d.zones.size(); ++i)
            if (mask[i])
            {
                const auto p = place (d.zones[i]);
                if (d.zones[i].roundRobin != best[p] || taken[p]) mask[i] = false; // un seul par place
                else taken[p] = true;
            }
    }
    return mask;
}

std::int64_t residentFramesNeeded (const InstrumentDescription& d, const ZoneDescription& zone, int channels, bool keptAsInt16,
                                   std::int64_t totalFrames)
{
    if (! d.streamFromDisk || totalFrames <= 0) return totalFrames;
    if (zone.mode == PlayMode::Loop)
    {
        // Même règle de bornage que buildInstrument : la boucle lit jusqu'à loopEnd (+ 4 pour l'interpolation).
        std::int64_t loopEnd = zone.loopEnd < 0 ? totalFrames : std::min<std::int64_t> (zone.loopEnd, totalFrames);
        const std::int64_t loopStart = std::min<std::int64_t> (zone.loopStart, totalFrames - 1);
        if (loopEnd <= loopStart) loopEnd = totalFrames;
        return std::min<std::int64_t> (totalFrames, loopEnd + 4);
    }
    const std::int64_t bytes = static_cast<std::int64_t> (std::clamp (d.preloadKB, InstrumentDescription::minPreloadKB,
                                                                      InstrumentDescription::maxPreloadKB)) * 1024;
    const std::int64_t perFrame = static_cast<std::int64_t> (std::max (1, channels)) * (keptAsInt16 ? 2 : 4);
    return std::min<std::int64_t> (totalFrames, std::max<std::int64_t> (1, bytes / perFrame));
}

std::shared_ptr<const Instrument> buildInstrument (const InstrumentDescription& description, std::vector<std::string>* warnings,
                                                   SampleCache* externalCache, const std::function<bool (int, int)>& progress,
                                                   const std::set<std::string>* keep)
{
    auto inst = std::make_shared<Instrument>();
    inst->roundRobinMode = description.roundRobinMode;
    inst->velocityLut = buildLut (description.velocityCurve);
    inst->velocityLinear = sanitised (description.velocityCurve).isLinear();
    inst->fadeWidth = std::clamp (description.velocityFadeWidth, 0, InstrumentDescription::maxVelocityFadeWidth);
    inst->fadeLinear = description.velocityFadeLinear;
    inst->polyphony = description.polyphony;
    inst->maxVoices = std::clamp (description.maxVoices, 1, InstrumentDescription::maxVoicesLimit);
    inst->stealMode = description.stealMode;
    inst->sameKey = description.sameKey;
    inst->cutFadeMs = std::clamp (description.cutFadeMs, 1, InstrumentDescription::maxCutFadeMs);
    inst->glideMs = std::clamp (description.glideMs, 0, InstrumentDescription::maxGlideMs);
    inst->chokeSameZone = description.chokeSameZone;
    std::map<std::string, int> groupIds;
    SampleCache localCache;
    SampleCache& cache = externalCache != nullptr ? *externalCache : localCache;
    const std::vector<bool> mask = loadedZones (description);
    // Filtres : un seul exemplaire des tables pour toutes les zones qui héritent du réglage d'instrument.
    auto makeFilters = [] (const FilterBlock& block, bool fromParams) -> std::shared_ptr<const FilterRuntimeBlock>
    {
        const FilterBlock b = sanitised (block);
        if (! b.anyOn()) return nullptr;
        auto rt = std::make_shared<FilterRuntimeBlock>();
        for (int i = 0; i < 2; ++i)
        {
            rt->settings[static_cast<std::size_t> (i)] = b.filter[static_cast<std::size_t> (i)];
            rt->env[static_cast<std::size_t> (i)] = EnvelopeRuntime (b.filter[static_cast<std::size_t> (i)].env);
            rt->fromParams[static_cast<std::size_t> (i)] = fromParams;
        }
        rt->parallel = b.parallel;
        rt->balance = static_cast<float> (b.balance);
        rt->anyOn = true;
        return rt;
    };
    const auto defaultFilters = makeFilters (description.defaultFilters, true);
    int done = 0;
    const int total = static_cast<int> (std::count (mask.begin(), mask.end(), true));
    // Besoin en mémoire par fichier (streaming) : le maximum des zones qui l'utilisent. -1 = fichier entier.
    std::map<std::string, std::int64_t> need;
    if (description.streamFromDisk)
        for (std::size_t zi = 0; zi < description.zones.size(); ++zi)
        {
            if (! mask[zi]) continue;
            const ZoneDescription zd = sanitised (description.zones[zi]);
            std::int64_t frames = 0;
            int channels = 0;
            bool int16 = false;
            if (auto c = cache.find (zd.samplePath); c != cache.end() && c->second)
            {
                frames = c->second->frames; channels = c->second->channels; int16 = c->second->isInt16();
            }
            else
            {
                const auto info = probeAudioFile (std::filesystem::u8path (zd.samplePath));
                if (! info.ok) continue; // l'échec sera signalé au chargement
                frames = info.frames; channels = info.channels; int16 = info.keptAsInt16;
            }
            const auto n = residentFramesNeeded (description, zd, channels, int16, frames);
            auto& cur = need[zd.samplePath];
            cur = std::max (cur, n);
        }

    for (std::size_t zi = 0; zi < description.zones.size(); ++zi)
    {
        if (! mask[zi]) continue; // laissée sur le disque (principal seulement, Groupe ou relâchement non chargés)
        if (progress && ! progress (done, total)) return nullptr;
        ++done;
        const ZoneDescription zd = sanitised (description.zones[zi]);
        const auto needIt = need.find (zd.samplePath);
        const std::int64_t wanted = needIt != need.end() ? needIt->second : -1; // -1 : tout
        auto it = cache.find (zd.samplePath);
        // Un échantillon déjà lu n'est réutilisé que s'il a exactement la partie résidente voulue (changer le
        // préchargement ou le mode relit le fichier et libère le surplus).
        if (it != cache.end() && it->second)
        {
            const auto& cached = *it->second;
            const std::int64_t expected = wanted < 0 ? cached.frames : std::min (cached.frames, wanted);
            if (cached.resident() != expected) { cache.erase (it); it = cache.end(); }
        }
        if (it == cache.end())
        {
            auto r = loadSampleFile (std::filesystem::u8path (zd.samplePath), wanted);
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
        z.sourceIndex = static_cast<int> (zi);
        z.filters = zd.filterOverride ? makeFilters (zd.filters, false) : defaultFilters;
        z.trigger = zd.trigger;
        z.chokeCuts = zd.chokeCuts;
        z.chokeCutBy = zd.chokeCutBy;
        if (! zd.group.empty())
            z.groupId = groupIds.emplace (zd.group, static_cast<int> (groupIds.size()) + 1).first->second;
        z.loopStart = std::min<std::int64_t> (zd.loopStart, frames - 1);
        z.loopEnd = zd.loopEnd < 0 ? frames : std::min<std::int64_t> (zd.loopEnd, frames);
        if (z.loopEnd <= z.loopStart) { z.loopStart = 0; z.loopEnd = frames; }
        inst->zones.push_back (std::move (z));
    }
    inst->finalize();
    // On ne garde en cache que ce qui est chargé : décharger une table libère vraiment la mémoire.
    {
        std::set<std::string> used;
        if (keep != nullptr) used = *keep;
        for (std::size_t zi = 0; zi < description.zones.size(); ++zi)
            if (mask[zi]) used.insert (description.zones[zi].samplePath);
        for (auto it = cache.begin(); it != cache.end();)
            it = used.count (it->first) ? std::next (it) : cache.erase (it);
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
