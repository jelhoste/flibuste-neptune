#include "neptune/core/Preset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <random>

#include <nlohmann/json.hpp>

namespace neptune::core::preset
{
using nlohmann::json;

const char* toString (Capture c) noexcept { return c == Capture::Free ? "free" : "captured"; }
const char* toString (PedalBehavior p) noexcept
{
    return p == PedalBehavior::Isolate ? "isolate" : p == PedalBehavior::Immediate ? "immediate" : "default";
}

namespace
{
constexpr std::size_t kMaxName = 128, kMaxType = 64, kMaxText = 96, kMaxCharacteristics = 16, kMaxFields = 32, kMaxParams = 1024;
constexpr std::size_t kMaxState = 4u * 1024u * 1024u;

std::string clipped (std::string s, std::size_t n)
{
    if (s.size() > n)
    {
        s.resize (n);
        while (! s.empty() && (static_cast<unsigned char> (s.back()) & 0xC0) == 0x80) s.pop_back(); // pas de séquence UTF-8 coupée
        if (! s.empty() && static_cast<unsigned char> (s.back()) >= 0xC0) s.pop_back();
    }
    return s;
}

std::string lower (std::string s)
{
    for (auto& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}

std::string strOf (const json& o, const char* k, const std::string& def = {})
{
    const auto it = o.find (k);
    return it != o.end() && it->is_string() ? it->get<std::string>() : def;
}

bool boolOf (const json& o, const char* k, bool def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_boolean() ? it->get<bool>() : def;
}

double numOf (const json& o, const char* k, double def)
{
    const auto it = o.find (k);
    if (it == o.end() || ! it->is_number()) return def;
    const double v = it->get<double>();
    return std::isfinite (v) ? v : def;
}

int intOf (const json& o, const char* k, int def, int lo, int hi)
{
    return static_cast<int> (std::clamp (numOf (o, k, def), static_cast<double> (lo), static_cast<double> (hi)));
}

json fieldsJson (const std::vector<CustomField>& f)
{
    json a = json::array();
    for (const auto& c : f) a.push_back ({ { "key", c.key }, { "value", c.value } });
    return a;
}

std::vector<CustomField> fieldsFrom (const json& o, const char* k)
{
    std::vector<CustomField> out;
    const auto it = o.find (k);
    if (it == o.end() || ! it->is_array()) return out;
    for (const auto& e : *it)
    {
        if (! e.is_object() || out.size() >= kMaxFields) continue;
        CustomField c { clipped (strOf (e, "key"), kMaxText), clipped (strOf (e, "value"), kMaxText) };
        if (! c.key.empty() && std::find (out.begin(), out.end(), c) == out.end()) out.push_back (std::move (c));
    }
    return out;
}

bool parseObject (std::string_view text, json& out)
{
    out = json::parse (text.begin(), text.end(), nullptr, false);
    return ! out.is_discarded() && out.is_object();
}

Capture captureFrom (const json& v, Capture def)
{
    if (v.is_string()) return v.get<std::string>() == "free" ? Capture::Free : v.get<std::string>() == "captured" ? Capture::Captured : def;
    return def;
}

PedalBehavior pedalFrom (const json& o, const char* k)
{
    const auto it = o.find (k);
    if (it == o.end() || ! it->is_string()) return PedalBehavior::Default;
    const auto s = it->get<std::string>();
    return s == "isolate" ? PedalBehavior::Isolate : s == "immediate" ? PedalBehavior::Immediate : PedalBehavior::Default;
}
} // namespace

// ---------------------------------------------------------------------------------------------------- Preset

bool Preset::operator== (const Preset& o) const noexcept
{
    return id == o.id && name == o.name && type == o.type && characteristics == o.characteristics && rating == o.rating
        && locked == o.locked && customFields == o.customFields && derivedFrom == o.derivedFrom && parameters == o.parameters
        && captureOverrides == o.captureOverrides && module == o.module && state == o.state && facade == o.facade && reverbEngine == o.reverbEngine
        && reverbIrRef == o.reverbIrRef && declaredLatencySamples == o.declaredLatencySamples
        && pedalHeldOverride == o.pedalHeldOverride;
}

std::string newId()
{
    static thread_local std::mt19937_64 rng { std::random_device {}() };
    std::uint64_t hi = rng(), lo = rng();
    hi = (hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull; // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull; // variante RFC 4122
    static const char* hex = "0123456789abcdef";
    std::string s;
    auto put = [&] (std::uint64_t v, int nibbles)
    {
        for (int i = nibbles - 1; i >= 0; --i) s += hex[(v >> (4 * i)) & 0xF];
    };
    put (hi >> 32, 8); s += '-'; put (hi >> 16, 4); s += '-'; put (hi, 4); s += '-'; put (lo >> 48, 4); s += '-'; put (lo, 12);
    return s;
}

Preset sanitised (Preset p)
{
    if (p.id.empty() || p.id.size() > 64) p.id = newId();
    p.name = clipped (p.name, kMaxName);
    if (p.name.empty()) p.name = "Init";
    p.type = clipped (p.type, kMaxType);
    std::vector<std::string> ch;
    for (auto& c : p.characteristics)
    {
        auto t = clipped (c, kMaxText);
        if (! t.empty() && ch.size() < kMaxCharacteristics && std::find (ch.begin(), ch.end(), t) == ch.end()) ch.push_back (std::move (t));
    }
    p.characteristics = std::move (ch);
    p.rating = std::clamp (p.rating, 0, maxRating);
    std::vector<CustomField> f;
    for (auto& c : p.customFields)
    {
        CustomField n { clipped (c.key, kMaxText), clipped (c.value, kMaxText) };
        if (! n.key.empty() && f.size() < kMaxFields && std::find (f.begin(), f.end(), n) == f.end()) f.push_back (std::move (n));
    }
    p.customFields = std::move (f);
    p.derivedFrom = clipped (p.derivedFrom, 64);
    for (auto it = p.parameters.begin(); it != p.parameters.end();)
        it = (! it->second.isText && ! std::isfinite (it->second.number)) || it->first.empty() || it->first.size() > kMaxText ? p.parameters.erase (it) : std::next (it);
    while (p.parameters.size() > kMaxParams) p.parameters.erase (std::prev (p.parameters.end()));
    p.module = clipped (p.module, 32);
    if (p.state.size() > kMaxState) p.state.clear();
    if (p.facade.size() > kMaxState) p.facade.clear();
    p.reverbEngine = p.reverbEngine == "light" || p.reverbEngine == "convolution" ? p.reverbEngine : std::string();
    p.reverbIrRef = clipped (p.reverbIrRef, 64);
    p.declaredLatencySamples = std::clamp (p.declaredLatencySamples, 0, 1 << 20);
    p.version = schemaVersion;
    return p;
}

std::string toJson (const Preset& in)
{
    const Preset p = sanitised (in);
    json o;
    o["schemaVersion"] = schemaVersion;
    o["id"] = p.id;
    o["name"] = p.name;
    o["type"] = p.type;
    o["characteristics"] = p.characteristics;
    o["rating"] = p.rating;
    o["locked"] = p.locked;
    o["customFields"] = fieldsJson (p.customFields);
    o["derivedFrom"] = p.derivedFrom.empty() ? json (nullptr) : json (p.derivedFrom);
    json params = json::object();
    for (const auto& [k, v] : p.parameters) params[k] = v.isText ? json (v.text) : json (v.number);
    o["parameters"] = std::move (params);
    json over = json::object();
    for (const auto& [k, c] : p.captureOverrides) over[k] = toString (c);
    o["parameterCaptureOverrides"] = std::move (over);
    o["reverbEngine"] = p.reverbEngine.empty() ? json (nullptr) : json (p.reverbEngine);
    o["reverbIrRef"] = p.reverbIrRef.empty() ? json (nullptr) : json (p.reverbIrRef);
    o["declaredLatencySamples"] = p.declaredLatencySamples;
    o["pedalHeldBehaviorOverride"] = p.pedalHeldOverride == PedalBehavior::Default ? json (nullptr) : json (toString (p.pedalHeldOverride));
    if (! p.module.empty()) o["module"] = p.module;
    if (! p.state.empty())
    {
        const auto st = json::parse (p.state, nullptr, false);
        if (! st.is_discarded() && st.is_object()) o["state"] = st;
    }
    if (! p.facade.empty())
    {
        const auto f = json::parse (p.facade, nullptr, false);
        if (! f.is_discarded() && f.is_object()) o["facade"] = f;
    }
    return o.dump (2);
}

Preset presetFromJson (std::string_view text, bool* ok)
{
    json o;
    Preset p;
    if (! parseObject (text, o))
    {
        if (ok != nullptr) *ok = false;
        return sanitised (p);
    }
    if (ok != nullptr) *ok = true;
    p.id = strOf (o, "id");
    p.name = strOf (o, "name", p.name);
    p.type = strOf (o, "type");
    if (const auto it = o.find ("characteristics"); it != o.end() && it->is_array())
        for (const auto& e : *it)
            if (e.is_string()) p.characteristics.push_back (e.get<std::string>());
    p.rating = intOf (o, "rating", 0, 0, maxRating);
    p.locked = boolOf (o, "locked", false);
    p.customFields = fieldsFrom (o, "customFields");
    p.derivedFrom = strOf (o, "derivedFrom");
    if (const auto it = o.find ("parameters"); it != o.end() && it->is_object())
        for (const auto& [k, v] : it->items())
        {
            if (v.is_number() && std::isfinite (v.get<double>())) p.parameters[k] = ParamValue::ofNumber (v.get<double>());
            else if (v.is_boolean()) p.parameters[k] = ParamValue::ofNumber (v.get<bool>() ? 1.0 : 0.0);
            else if (v.is_string()) p.parameters[k] = ParamValue::ofText (v.get<std::string>());
        }
    if (const auto it = o.find ("parameterCaptureOverrides"); it != o.end() && it->is_object())
        for (const auto& [k, v] : it->items())
            if (v.is_string() && (v.get<std::string>() == "captured" || v.get<std::string>() == "free"))
                p.captureOverrides[k] = captureFrom (v, Capture::Captured);
    p.reverbEngine = strOf (o, "reverbEngine");
    p.reverbIrRef = strOf (o, "reverbIrRef");
    p.declaredLatencySamples = intOf (o, "declaredLatencySamples", 0, 0, 1 << 20);
    p.pedalHeldOverride = pedalFrom (o, "pedalHeldBehaviorOverride");
    p.module = strOf (o, "module");
    if (const auto it = o.find ("state"); it != o.end() && it->is_object()) p.state = it->dump();
    if (const auto it = o.find ("facade"); it != o.end() && it->is_object()) p.facade = it->dump();
    return sanitised (std::move (p));
}

// ---------------------------------------------------------------------------------------------------- configuration

std::string toJson (const LibraryConfig& c)
{
    json o;
    o["schemaVersion"] = schemaVersion;
    json defs = json::object();
    for (const auto& [k, v] : c.parameterCaptureDefaults) defs[k] = toString (v);
    o["parameterCaptureDefaults"] = std::move (defs);
    o["pedalHeldBehaviorDefault"] = c.pedalHeldBehaviorDefault == PedalBehavior::Immediate ? "immediate" : "isolate";
    o["reverbPool"] = { { "maxConvolutionInstances", c.reverbPool.maxConvolutionInstances },
                        { "maxLightInstances", c.reverbPool.maxLightInstances },
                        { "forcedFadeOutMs", { { "light", c.reverbPool.forcedFadeOutMsLight },
                                               { "convolution", c.reverbPool.forcedFadeOutMsConvolution } } } };
    o["trash"] = { { "retentionDays", c.trashRetentionDays }, { "maxEntries", c.trashMaxEntries } };
    return o.dump (2);
}

LibraryConfig configFromJson (std::string_view text)
{
    LibraryConfig c;
    json o;
    if (! parseObject (text, o)) return c;
    if (const auto it = o.find ("parameterCaptureDefaults"); it != o.end() && it->is_object())
        for (const auto& [k, v] : it->items())
            if (v.is_string() && (v.get<std::string>() == "captured" || v.get<std::string>() == "free") && ! k.empty() && k.size() <= kMaxText)
                c.parameterCaptureDefaults[k] = captureFrom (v, Capture::Captured);
    c.pedalHeldBehaviorDefault = strOf (o, "pedalHeldBehaviorDefault") == "immediate" ? PedalBehavior::Immediate : PedalBehavior::Isolate;
    if (const auto it = o.find ("reverbPool"); it != o.end() && it->is_object())
    {
        c.reverbPool.maxConvolutionInstances = intOf (*it, "maxConvolutionInstances", c.reverbPool.maxConvolutionInstances, 0, 16);
        c.reverbPool.maxLightInstances = intOf (*it, "maxLightInstances", c.reverbPool.maxLightInstances, 0, 16);
        if (const auto f = it->find ("forcedFadeOutMs"); f != it->end() && f->is_object())
        {
            c.reverbPool.forcedFadeOutMsLight = intOf (*f, "light", c.reverbPool.forcedFadeOutMsLight, 1, 5000);
            c.reverbPool.forcedFadeOutMsConvolution = intOf (*f, "convolution", c.reverbPool.forcedFadeOutMsConvolution, 1, 5000);
        }
    }
    if (const auto it = o.find ("trash"); it != o.end() && it->is_object())
    {
        c.trashRetentionDays = intOf (*it, "retentionDays", c.trashRetentionDays, 0, 3650);
        c.trashMaxEntries = intOf (*it, "maxEntries", c.trashMaxEntries, 0, 10000);
    }
    return c;
}

Capture captureOf (const std::string& key, const Preset& p, const LibraryConfig& c) noexcept
{
    if (const auto it = p.captureOverrides.find (key); it != p.captureOverrides.end()) return it->second;
    if (const auto it = c.parameterCaptureDefaults.find (key); it != c.parameterCaptureDefaults.end()) return it->second;
    return Capture::Captured;
}

PedalBehavior pedalBehaviorOf (const Preset& p, const LibraryConfig& c) noexcept
{
    if (p.pedalHeldOverride != PedalBehavior::Default) return p.pedalHeldOverride;
    return c.pedalHeldBehaviorDefault == PedalBehavior::Immediate ? PedalBehavior::Immediate : PedalBehavior::Isolate;
}

// ---------------------------------------------------------------------------------------------------- banques

std::string defaultBankName (int index)
{
    index = std::clamp (index, 0, numBanks - 1);
    std::string s (1, static_cast<char> ('A' + index % 26));
    if (index >= 26) s += std::to_string (index / 26);
    return s;
}

std::string displayName (const Bank& b) { return b.name.empty() ? defaultBankName (b.index) : b.name; }

Bank* BankManifest::find (int index) noexcept
{
    for (auto& b : banks)
        if (b.index == index) return &b;
    return nullptr;
}

const Bank* BankManifest::find (int index) const noexcept
{
    for (const auto& b : banks)
        if (b.index == index) return &b;
    return nullptr;
}

Bank& BankManifest::ensure (int index)
{
    index = std::clamp (index, 0, numBanks - 1);
    if (auto* b = find (index)) return *b;
    Bank b;
    b.index = index;
    const auto pos = std::find_if (banks.begin(), banks.end(), [index] (const Bank& o) { return o.index > index; });
    return *banks.insert (pos, std::move (b));
}

int BankManifest::forget (const std::string& id)
{
    int n = 0;
    if (id.empty()) return 0;
    for (auto& b : banks)
        for (auto& p : b.presets)
            if (p == id) { p.clear(); ++n; }
    return n;
}

std::string toJson (const BankManifest& m)
{
    json o;
    o["schemaVersion"] = schemaVersion;
    json banks = json::array();
    for (const auto& b : m.banks)
    {
        json j;
        j["index"] = b.index;
        j["name"] = b.name.empty() ? defaultBankName (b.index) : b.name;
        j["customFields"] = fieldsJson (b.customFields);
        j["wrapToNextBank"] = b.wrapToNextBank;
        json list = json::array();
        for (const auto& id : b.presets) list.push_back (id.empty() ? json (nullptr) : json (id));
        j["presets"] = std::move (list);
        banks.push_back (std::move (j));
    }
    o["banks"] = std::move (banks);
    return o.dump (2);
}

BankManifest banksFromJson (std::string_view text)
{
    BankManifest m;
    json o;
    if (! parseObject (text, o)) return m;
    const auto it = o.find ("banks");
    if (it == o.end() || ! it->is_array()) return m;
    for (const auto& e : *it)
    {
        if (! e.is_object()) continue;
        const double rawIndex = numOf (e, "index", -1.0);
        if (rawIndex < 0.0 || rawIndex >= numBanks || rawIndex != std::floor (rawIndex)) continue; // hors limites : ignorée
        const int index = static_cast<int> (rawIndex);
        if (m.find (index) != nullptr) continue;
        Bank& b = m.ensure (index);
        const auto name = clipped (strOf (e, "name"), kMaxName);
        b.name = name == defaultBankName (index) ? std::string() : name; // le nom calculé n'est pas figé
        b.customFields = fieldsFrom (e, "customFields");
        b.wrapToNextBank = boolOf (e, "wrapToNextBank", false);
        if (const auto p = e.find ("presets"); p != e.end() && p->is_array())
            for (std::size_t i = 0; i < p->size() && i < static_cast<std::size_t> (programsPerBank); ++i)
                if ((*p)[i].is_string()) b.presets[i] = clipped ((*p)[i].get<std::string>(), 64);
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------- grille

std::string Grid::assign (int position, const std::string& presetId)
{
    position = std::clamp (position, 0, maxTiles - 1);
    for (auto& t : tiles)
        if (t.position == position)
        {
            auto old = std::move (t.presetRef);
            t.presetRef = presetId;
            return old;
        }
    const auto pos = std::find_if (tiles.begin(), tiles.end(), [position] (const GridTile& t) { return t.position > position; });
    tiles.insert (pos, GridTile { position, presetId });
    return {};
}

bool Grid::swapTiles (int a, int b)
{
    if (a < 0 || b < 0 || a >= maxTiles || b >= maxTiles) return false;
    if (a == b) return true;
    auto at = [this] (int p) -> const std::string
    {
        for (const auto& t : tiles)
            if (t.position == p) return t.presetRef;
        return {};
    };
    const auto ra = at (a), rb = at (b);
    assign (a, rb);
    assign (b, ra);
    return true;
}

std::vector<std::string> Grid::missing (const std::vector<std::string>& known) const
{
    std::vector<std::string> out;
    for (const auto& t : tiles)
        if (! t.presetRef.empty() && std::find (known.begin(), known.end(), t.presetRef) == known.end()
            && std::find (out.begin(), out.end(), t.presetRef) == out.end())
            out.push_back (t.presetRef);
    return out;
}

std::string toJson (const Grid& g)
{
    json o;
    o["schemaVersion"] = schemaVersion;
    o["gridId"] = g.gridId;
    o["name"] = g.name;
    o["locked"] = g.locked;
    o["linkedConcertField"] = g.linkedConcertField.empty() ? json (nullptr) : json (g.linkedConcertField);
    json tiles = json::array();
    for (const auto& t : g.tiles) tiles.push_back ({ { "position", t.position }, { "presetRef", t.presetRef.empty() ? json (nullptr) : json (t.presetRef) } });
    o["tiles"] = std::move (tiles);
    return o.dump (2);
}

Grid gridFromJson (std::string_view text)
{
    Grid g;
    json o;
    if (! parseObject (text, o)) { g.gridId = newId(); return g; }
    g.gridId = clipped (strOf (o, "gridId"), 64);
    if (g.gridId.empty()) g.gridId = newId();
    g.name = clipped (strOf (o, "name"), kMaxName);
    g.locked = boolOf (o, "locked", true);
    g.linkedConcertField = clipped (strOf (o, "linkedConcertField"), kMaxText);
    if (const auto it = o.find ("tiles"); it != o.end() && it->is_array())
        for (const auto& e : *it)
        {
            if (! e.is_object()) continue;
            const double rawPos = numOf (e, "position", -1.0);
            if (rawPos < 0.0 || rawPos >= Grid::maxTiles || rawPos != std::floor (rawPos)) continue;
            const int pos = static_cast<int> (rawPos);
            const bool dup = std::any_of (g.tiles.begin(), g.tiles.end(), [pos] (const GridTile& t) { return t.position == pos; });
            if (! dup) g.assign (pos, clipped (strOf (e, "presetRef"), 64));
        }
    return g;
}

// ---------------------------------------------------------------------------------------------------- bundle

std::string toJson (const BundleManifest& b)
{
    json o;
    o["schemaVersion"] = schemaVersion;
    o["bankName"] = b.bankName;
    o["bankCustomFields"] = fieldsJson (b.bankCustomFields);
    o["presetOrder"] = b.presetOrder;
    return o.dump (2);
}

BundleManifest bundleFromJson (std::string_view text)
{
    BundleManifest b;
    json o;
    if (! parseObject (text, o)) return b;
    b.bankName = clipped (strOf (o, "bankName"), kMaxName);
    b.bankCustomFields = fieldsFrom (o, "bankCustomFields");
    if (const auto it = o.find ("presetOrder"); it != o.end() && it->is_array())
        for (const auto& e : *it)
            if (e.is_string() && b.presetOrder.size() < static_cast<std::size_t> (programsPerBank)) b.presetOrder.push_back (clipped (e.get<std::string>(), 64));
    return b;
}

// ---------------------------------------------------------------------------------------------------- recherche

bool matches (const Preset& p, const Query& q)
{
    if (! q.text.empty() && lower (p.name).find (lower (q.text)) == std::string::npos) return false;
    if (! q.type.empty() && lower (p.type) != lower (q.type)) return false;
    if (p.rating < q.minRating) return false;
    if (q.lockedOnly && ! p.locked) return false;
    if (! q.characteristics.empty())
    {
        bool any = false;
        for (const auto& c : q.characteristics)
            for (const auto& have : p.characteristics) any = any || lower (have) == lower (c);
        if (! any) return false;
    }
    // facettes : pour chaque clé demandée, au moins une des valeurs demandées doit être présente (OR) ; toutes les clés (AND)
    std::vector<std::string> keys;
    for (const auto& f : q.facets)
        if (std::find (keys.begin(), keys.end(), f.key) == keys.end()) keys.push_back (f.key);
    for (const auto& key : keys)
    {
        bool ok = false;
        for (const auto& f : q.facets)
        {
            if (f.key != key) continue;
            for (const auto& have : p.customFields) ok = ok || (have.key == f.key && lower (have.value) == lower (f.value));
        }
        if (! ok) return false;
    }
    return true;
}

std::vector<const Preset*> filter (const std::vector<Preset>& all, const Query& q)
{
    std::vector<const Preset*> out;
    for (const auto& p : all)
        if (matches (p, q)) out.push_back (&p);
    std::stable_sort (out.begin(), out.end(), [] (const Preset* a, const Preset* b) { return lower (a->name) < lower (b->name); });
    return out;
}

// ---------------------------------------------------------------------------------------------------- navigation

std::string Navigator::presetAt (const BankManifest& m, Slot s)
{
    const auto* b = m.find (s.bank);
    if (b == nullptr || s.program < 0 || s.program >= programsPerBank) return {};
    return b->presets[static_cast<std::size_t> (s.program)];
}

std::optional<Slot> Navigator::locate (const BankManifest& m, const std::string& id)
{
    if (id.empty()) return std::nullopt;
    for (const auto& b : m.banks)
        for (int i = 0; i < programsPerBank; ++i)
            if (b.presets[static_cast<std::size_t> (i)] == id) return Slot { b.index, i };
    return std::nullopt;
}

std::optional<Slot> Navigator::step (const BankManifest& m, Slot cur, int dir)
{
    if (dir == 0 || m.banks.empty()) return std::nullopt;
    const int d = dir > 0 ? 1 : -1;
    // Parcours linéaire de tous les emplacements (banque, programme) ; la fin d'une banque mène à la suivante si
    // wrapToNextBank, sinon on reboucle dans la même banque.
    Slot s = cur;
    for (int guard = 0; guard < numBanks * programsPerBank; ++guard)
    {
        s.program += d;
        if (s.program >= programsPerBank || s.program < 0)
        {
            const Bank* here = m.find (s.bank);
            if (here != nullptr && here->wrapToNextBank)
            {
                // banque suivante (ou précédente) existante, en bouclant sur la liste
                int pos = 0;
                for (int i = 0; i < static_cast<int> (m.banks.size()); ++i)
                    if (m.banks[static_cast<std::size_t> (i)].index == s.bank) pos = i;
                pos = (pos + d + static_cast<int> (m.banks.size())) % static_cast<int> (m.banks.size());
                s.bank = m.banks[static_cast<std::size_t> (pos)].index;
            }
            s.program = d > 0 ? 0 : programsPerBank - 1;
        }
        if (! presetAt (m, s).empty()) return s == cur ? std::nullopt : std::optional<Slot> (s);
    }
    return std::nullopt;
}

std::string Navigator::programChange (const BankManifest& m, int program) const
{
    return presetAt (m, Slot { bank_, program });
}
} // namespace neptune::core::preset
