#include "neptune/core/ZoneImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

namespace neptune::core::import
{
namespace
{
const char* const kRoleKeys[numRoles] = {
    "ignore", "note_number", "note_name", "key_low", "key_high", "key_below", "key_above", "velocity_value",
    "velocity_low", "velocity_high", "velocity_layer", "velocity_layer_count", "dynamics", "round_robin", "group", "release",
};

bool isDigit (char c) { return c >= '0' && c <= '9'; }

// Portée (nom entier ou segment) : renvoie false si le segment demandé n'existe pas.
bool scopeOf (std::string_view stem, const std::string& delimiter, int segment, std::string_view& out)
{
    if (segment == 0 || delimiter.empty()) { out = stem; return segment == 0 || delimiter.empty(); }
    std::vector<std::string_view> parts;
    std::size_t pos = 0;
    while (true)
    {
        const auto next = stem.find (delimiter, pos);
        if (next == std::string_view::npos) { parts.push_back (stem.substr (pos)); break; }
        parts.push_back (stem.substr (pos, next - pos));
        pos = next + delimiter.size();
    }
    const int n = static_cast<int> (parts.size());
    const int idx = segment > 0 ? segment - 1 : n + segment;
    if (idx < 0 || idx >= n) return false;
    out = parts[static_cast<std::size_t> (idx)];
    return true;
}

// Sous-chaîne selon début (1-based, < 0 : depuis la fin) et longueur maximale (0 = jusqu'à la fin).
std::string_view slice (std::string_view scope, int start, int maxLength)
{
    const int n = static_cast<int> (scope.size());
    int from = start > 0 ? start - 1 : (start < 0 ? n + start : 0);
    if (from < 0 || from > n) return {};
    int len = n - from;
    if (maxLength > 0) len = std::min (len, maxLength);
    return scope.substr (static_cast<std::size_t> (from), static_cast<std::size_t> (len));
}

std::optional<int> readInt (std::string_view s, int minLength)
{
    std::size_t i = 0;
    long long v = 0;
    while (i < s.size() && isDigit (s[i]) && i < 9) v = v * 10 + (s[i++] - '0');
    if (static_cast<int> (i) < std::max (1, minLength)) return std::nullopt;
    return static_cast<int> (v);
}

std::string lower (std::string_view s)
{
    std::string r (s);
    std::transform (r.begin(), r.end(), r.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return r;
}
} // namespace

const char* roleKey (Role r) noexcept { return kRoleKeys[static_cast<int> (r)]; }

std::optional<int> noteNameToMidi (std::string_view text, const NoteConvention& c)
{
    if (text.empty()) return std::nullopt;
    const char l = static_cast<char> (std::toupper (static_cast<unsigned char> (text[0])));
    static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    if (l < 'A' || l > 'G') return std::nullopt;
    int pc = pcs[l - 'A'];
    std::size_t i = 1;
    if (i < text.size() && (text[i] == '#' || text[i] == 's' || text[i] == 'S')) { ++pc; ++i; }
    else if (i < text.size() && text[i] == 'b' && i + 1 < text.size() && (isDigit (text[i + 1]) || text[i + 1] == '-')) { --pc; ++i; }
    bool neg = false;
    if (i < text.size() && text[i] == '-') { neg = true; ++i; }
    if (i >= text.size() || ! isDigit (text[i])) return std::nullopt;
    int oct = 0;
    std::size_t digits = 0;
    while (i < text.size() && isDigit (text[i]) && digits < 3) { oct = oct * 10 + (text[i] - '0'); ++i; ++digits; }
    if (i != text.size()) return std::nullopt; // du texte en trop : ce n'est pas (seulement) un nom de note
    if (neg) oct = -oct;
    const int midi = 12 * (oct - c.middleCOctave + 5) + pc;
    if (midi < 0 || midi > 127) return std::nullopt;
    return midi;
}

std::string midiToNoteName (int midi, const NoteConvention& c)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int oct = (midi / 12) - 5 + c.middleCOctave;
    return std::string (names[((midi % 12) + 12) % 12]) + std::to_string (midi >= 0 ? oct : oct - 1);
}

std::optional<int> dynamicsRank (std::string_view text)
{
    static const std::map<std::string, int> ranks = { { "ppp", 0 }, { "pp", 1 }, { "p", 2 }, { "mp", 3 }, { "mf", 4 },
                                                       { "f", 5 }, { "ff", 6 }, { "fff", 7 } };
    const auto it = ranks.find (lower (text));
    if (it == ranks.end()) return std::nullopt;
    return it->second;
}

namespace
{
// Donne un rôle à un texte lu dans le nom (champ positionnel ou chiffres après une expression).
void applyValue (ParsedName& p, Role role, std::string_view text, int minLength, const Template& t)
{
    auto problem = [&] (const std::string& m) { p.problems.push_back (m); };
    const std::string key = roleKey (role);
    auto bounded = [&] (std::optional<int> v, int lo, int hi, std::optional<int>& dst)
    {
        if (! v) { problem (key + ": no number at the given position in \"" + std::string (text) + "\""); return; }
        if (*v < lo || *v > hi) { problem (key + ": " + std::to_string (*v) + " is outside " + std::to_string (lo) + ".." + std::to_string (hi)); return; }
        dst = v;
    };
    switch (role)
    {
        case Role::NoteNumber: bounded (readInt (text, minLength), 0, 127, p.note); break;
        case Role::NoteName:
        {
            // longueur maximale non précisée : on lit le nom de note le plus long possible depuis le début
            std::optional<int> v;
            for (std::size_t len = std::min<std::size_t> (text.size(), 6); len >= 2 && ! v; --len) v = noteNameToMidi (text.substr (0, len), t.notes);
            if (v) p.noteFromName = v;
            else problem (key + ": \"" + std::string (text) + "\" is not a note name");
            break;
        }
        case Role::KeyLow: bounded (readInt (text, minLength), 0, 127, p.keyLow); break;
        case Role::KeyHigh: bounded (readInt (text, minLength), 0, 127, p.keyHigh); break;
        case Role::KeyBelow: bounded (readInt (text, minLength), 0, 127, p.keyBelow); break;
        case Role::KeyAbove: bounded (readInt (text, minLength), 0, 127, p.keyAbove); break;
        case Role::VelocityValue: bounded (readInt (text, minLength), 1, 127, p.velValue); break;
        case Role::VelocityLow: bounded (readInt (text, minLength), 1, 127, p.velLow); break;
        case Role::VelocityHigh: bounded (readInt (text, minLength), 1, 127, p.velHigh); break;
        case Role::VelocityLayer: bounded (readInt (text, minLength), 1, 1000, p.layer); break;
        case Role::VelocityLayerCount: bounded (readInt (text, minLength), 1, 1000, p.layerCount); break;
        case Role::Dynamics:
        {
            std::size_t n = 0;
            while (n < text.size() && std::isalpha (static_cast<unsigned char> (text[n]))) ++n;
            const auto r = dynamicsRank (text.substr (0, n));
            if (r) p.dynamicsRank = r;
            else problem (key + ": \"" + std::string (text.substr (0, n)) + "\" is not a dynamics marking (ppp..fff)");
            break;
        }
        case Role::RoundRobin:
        {
            if (const auto v = readInt (text, minLength)) { bounded (v, 1, 10000, p.roundRobin); break; }
            if (! text.empty() && std::isalpha (static_cast<unsigned char> (text[0])) && (text.size() == 1 || ! std::isalpha (static_cast<unsigned char> (text[1]))))
                p.roundRobin = std::tolower (static_cast<unsigned char> (text[0])) - 'a' + 1; // a, b, c -> 1, 2, 3
            else
                problem (key + ": \"" + std::string (text) + "\" is neither a number nor a single letter");
            break;
        }
        case Role::Group:
            if (! text.empty()) p.group = std::string (text);
            break;
        case Role::Release:
            p.release = ! text.empty();
            break;
        case Role::Ignore: break;
    }
}

// Position (0-based) où commence le mot dans `scope`, selon la portée ; npos si absent.
std::size_t findKeyword (std::string_view stem, const Template& t, const KeywordRule& k, std::string_view& scopeOut)
{
    std::string_view scope = stem;
    if (k.scope == KeywordScope::InSegment && ! scopeOf (stem, t.delimiter, k.segment, scope)) return std::string_view::npos;
    scopeOut = scope;
    const std::string word = lower (k.keyword);
    if (word.empty() || word.size() > scope.size()) return std::string_view::npos;
    const std::string hay = lower (scope);
    if (k.scope == KeywordScope::Anywhere || (k.scope == KeywordScope::InSegment && k.start <= 0))
        return hay.find (word);
    const int n = static_cast<int> (hay.size());
    const int from = k.start > 0 ? k.start - 1 : n + k.start;
    if (from < 0 || from + static_cast<int> (word.size()) > n) return std::string_view::npos;
    return hay.compare (static_cast<std::size_t> (from), word.size(), word) == 0 ? static_cast<std::size_t> (from) : std::string_view::npos;
}
} // namespace

ParsedName parseName (std::string_view stem, const Template& t)
{
    ParsedName p;
    p.stem = std::string (stem);

    for (const auto& f : t.fields)
    {
        if (f.role == Role::Ignore) continue;
        std::string_view scope;
        if (! scopeOf (stem, t.delimiter, f.segment, scope))
        {
            // Un segment absent n'est une erreur que pour la note (sans elle le fichier n'a pas de place). Pour les autres
            // rôles (groupe, relâchement, vélocité, round-robin...) c'est « pas de valeur » : l'aperçu montre le résultat.
            if (f.role == Role::NoteNumber || f.role == Role::NoteName)
                p.problems.push_back (std::string ("segment ") + std::to_string (f.segment) + " not found for " + roleKey (f.role));
            continue;
        }
        applyValue (p, f.role, slice (scope, f.start, f.maxLength), f.minLength, t);
    }

    // Règles par expression : appliquées après les champs, dans l'ordre ; la dernière qui parle l'emporte.
    for (const auto& k : t.keywords)
    {
        std::string_view scope;
        const std::size_t at = findKeyword (stem, t, k, scope);
        if (at == std::string_view::npos) continue; // le mot n'est pas dans ce nom : la règle ne dit rien
        if (k.trigger == Trigger::Release) p.release = true;
        else if (k.trigger == Trigger::Attack) p.release = false;
        if (! k.group.empty()) p.group = k.group;
        if (k.valueRole != Role::Ignore)
        {
            std::size_t i = at + k.keyword.size();
            std::size_t n = 0;
            const std::size_t cap = static_cast<std::size_t> (std::max (1, k.digitsMax));
            while (i + n < scope.size() && n < cap && isDigit (scope[i + n])) ++n;
            if (static_cast<int> (n) < std::max (1, k.digitsMin))
                p.problems.push_back ("\"" + k.keyword + "\" is not followed by " + std::to_string (std::max (1, k.digitsMin)) + " digit(s)");
            else
                applyValue (p, k.valueRole, scope.substr (i, n), 1, t);
        }
    }
    return p;
}

// ---- JSON du gabarit ----

std::string Template::toJson() const
{
    nlohmann::json o;
    o["delimiter"] = delimiter;
    o["middle_c_octave"] = notes.middleCOctave;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : fields)
        arr.push_back ({ { "role", roleKey (f.role) }, { "segment", f.segment }, { "start", f.start },
                         { "min_length", f.minLength }, { "max_length", f.maxLength } });
    o["fields"] = std::move (arr);
    nlohmann::json kws = nlohmann::json::array();
    for (const auto& k : keywords)
    {
        const char* scope = k.scope == KeywordScope::AtPosition ? "position" : (k.scope == KeywordScope::InSegment ? "segment" : "anywhere");
        const char* trig = k.trigger == Trigger::Release ? "release" : (k.trigger == Trigger::Attack ? "attack" : "unchanged");
        kws.push_back ({ { "keyword", k.keyword }, { "scope", scope }, { "segment", k.segment }, { "start", k.start },
                         { "value_role", roleKey (k.valueRole) }, { "digits_min", k.digitsMin }, { "digits_max", k.digitsMax },
                         { "trigger", trig }, { "group", k.group } });
    }
    o["keywords"] = std::move (kws);
    return o.dump();
}

Template Template::fromJson (std::string_view text)
{
    Template t;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return t;
    if (const auto it = doc.find ("delimiter"); it != doc.end() && it->is_string() && it->get<std::string>().size() <= 8) t.delimiter = it->get<std::string>();
    if (const auto it = doc.find ("middle_c_octave"); it != doc.end() && it->is_number_integer())
        t.notes.middleCOctave = static_cast<int> (std::clamp<long long> (it->get<long long>(), 0, 8));
    if (const auto it = doc.find ("fields"); it != doc.end() && it->is_array())
        for (const auto& f : *it)
        {
            if (! f.is_object() || t.fields.size() >= 32) continue;
            FieldRule r;
            const auto role = f.find ("role");
            if (role == f.end() || ! role->is_string()) continue;
            bool known = false;
            for (int i = 0; i < numRoles; ++i)
                if (role->get<std::string>() == kRoleKeys[i]) { r.role = static_cast<Role> (i); known = true; }
            if (! known) continue;
            auto num = [&] (const char* k, int def) { const auto v = f.find (k); return v != f.end() && v->is_number_integer() ? static_cast<int> (std::clamp<long long> (v->get<long long>(), -64, 64)) : def; };
            r.segment = num ("segment", 0);
            r.start = num ("start", 1);
            r.minLength = std::max (1, num ("min_length", 1));
            r.maxLength = std::max (0, num ("max_length", 0));
            t.fields.push_back (r);
        }
    if (const auto it = doc.find ("keywords"); it != doc.end() && it->is_array())
        for (const auto& k : *it)
        {
            if (! k.is_object() || t.keywords.size() >= 32) continue;
            KeywordRule r;
            if (const auto v = k.find ("keyword"); v != k.end() && v->is_string() && ! v->get<std::string>().empty() && v->get<std::string>().size() <= 32)
                r.keyword = v->get<std::string>();
            else
                continue;
            if (const auto v = k.find ("scope"); v != k.end() && v->is_string())
            {
                const auto sc = v->get<std::string>();
                r.scope = sc == "position" ? KeywordScope::AtPosition : (sc == "segment" ? KeywordScope::InSegment : KeywordScope::Anywhere);
            }
            if (const auto v = k.find ("trigger"); v != k.end() && v->is_string())
            {
                const auto tr = v->get<std::string>();
                r.trigger = tr == "release" ? Trigger::Release : (tr == "attack" ? Trigger::Attack : Trigger::Unchanged);
            }
            if (const auto v = k.find ("value_role"); v != k.end() && v->is_string())
                for (int i = 0; i < numRoles; ++i)
                    if (v->get<std::string>() == kRoleKeys[i]) r.valueRole = static_cast<Role> (i);
            auto num = [&] (const char* key, int def, int lo, int hi)
            {
                const auto v = k.find (key);
                return v != k.end() && v->is_number_integer() ? static_cast<int> (std::clamp<long long> (v->get<long long>(), lo, hi)) : def;
            };
            r.segment = num ("segment", 0, -64, 64);
            r.start = num ("start", 0, -64, 64);
            r.digitsMin = num ("digits_min", 1, 1, 9);
            r.digitsMax = std::max (r.digitsMin, num ("digits_max", 3, 1, 9));
            if (const auto v = k.find ("group"); v != k.end() && v->is_string() && v->get<std::string>().size() <= 64) r.group = v->get<std::string>();
            t.keywords.push_back (std::move (r));
        }
    return t;
}

int Plan::errors() const noexcept
{
    return static_cast<int> (std::count_if (diagnostics.begin(), diagnostics.end(), [] (const Diagnostic& d) { return d.severity == Severity::Error; }));
}
int Plan::warnings() const noexcept
{
    return static_cast<int> (std::count_if (diagnostics.begin(), diagnostics.end(), [] (const Diagnostic& d) { return d.severity == Severity::Warning; }));
}

std::vector<ZoneDescription> includedZones (const Plan& plan)
{
    std::vector<ZoneDescription> r;
    for (const auto& z : plan.zones)
        if (z.included) r.push_back (z.zone);
    return r;
}

namespace
{
struct Item
{
    std::string path;
    ParsedName parsed;
    bool included = true;
    int note = -1;
    int keyLow = 0, keyHigh = 127, velLow = 1, velHigh = 127;
    int rr = 0;
};

// Étendues « à mi-chemin » : valeurs triées distinctes -> (bas, haut) par valeur.
std::map<int, std::pair<int, int>> midpointRanges (const std::set<int>& values, int lowest, int highest)
{
    std::map<int, std::pair<int, int>> r;
    std::vector<int> v (values.begin(), values.end());
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        const int low = i == 0 ? lowest : (v[i - 1] + v[i]) / 2 + 1;
        const int high = i + 1 == v.size() ? highest : (v[i] + v[i + 1]) / 2;
        r[v[i]] = { low, high };
    }
    return r;
}
} // namespace

Plan planImport (const std::vector<std::string>& paths, const Template& t, const PlanOptions& options)
{
    Plan plan;
    auto diag = [&] (Severity s, const std::string& file, std::string msg) { plan.diagnostics.push_back ({ s, file, std::move (msg) }); };

    bool templateHasRoundRobin = false;
    for (const auto& f : t.fields) templateHasRoundRobin = templateHasRoundRobin || f.role == Role::RoundRobin;
    for (const auto& k : t.keywords) templateHasRoundRobin = templateHasRoundRobin || k.valueRole == Role::RoundRobin;

    std::vector<Item> items;
    for (const auto& path : paths)
    {
        Item it;
        it.path = path;
        it.parsed = parseName (std::filesystem::u8path (path).stem().string(), t);
        for (const auto& pr : it.parsed.problems) diag (Severity::Error, it.parsed.stem, pr);
        if (! it.parsed.problems.empty()) it.included = false;

        const auto& p = it.parsed;
        if (p.note && p.noteFromName && *p.note != *p.noteFromName)
            diag (Severity::Warning, p.stem, "note number " + std::to_string (*p.note) + " and note name (= " + std::to_string (*p.noteFromName)
                                                 + " with C" + std::to_string (t.notes.middleCOctave) + " = 60) disagree; the number is used");
        const auto note = p.note ? p.note : p.noteFromName;
        if (! note)
        {
            if (it.included) diag (Severity::Error, p.stem, "no note found in the file name");
            it.included = false;
        }
        else
            it.note = *note;
        items.push_back (std::move (it));
    }
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.path < b.path; });

    // ---- couches de vélocité ----
    int layerCount = options.layerCountOverride;
    std::vector<int> dynamicsRanks;
    for (const auto& it : items)
        if (it.included && it.parsed.dynamicsRank) dynamicsRanks.push_back (*it.parsed.dynamicsRank);
    std::sort (dynamicsRanks.begin(), dynamicsRanks.end());
    dynamicsRanks.erase (std::unique (dynamicsRanks.begin(), dynamicsRanks.end()), dynamicsRanks.end());

    auto layerOf = [&] (const Item& it) -> std::optional<int>
    {
        if (it.parsed.layer) return it.parsed.layer;
        if (it.parsed.dynamicsRank)
            return static_cast<int> (std::find (dynamicsRanks.begin(), dynamicsRanks.end(), *it.parsed.dynamicsRank) - dynamicsRanks.begin()) + 1;
        return std::nullopt;
    };
    if (layerCount <= 0)
        for (const auto& it : items)
        {
            if (! it.included) continue;
            if (it.parsed.layerCount) layerCount = std::max (layerCount, *it.parsed.layerCount);
            if (const auto l = layerOf (it)) layerCount = std::max (layerCount, *l);
        }
    plan.detectedLayerCount = layerCount;

    // vélocités données comme une valeur / une borne basse / une borne haute : bornes déduites sur l'ensemble des fichiers
    std::set<int> velValues, velLows, velHighs;
    for (const auto& it : items)
        if (it.included)
        {
            if (it.parsed.velValue) velValues.insert (*it.parsed.velValue);
            if (it.parsed.velLow) velLows.insert (*it.parsed.velLow);
            if (it.parsed.velHigh) velHighs.insert (*it.parsed.velHigh);
        }
    const auto velMid = midpointRanges (velValues, 1, 127);

    for (auto& it : items)
    {
        if (! it.included) continue;
        const auto& p = it.parsed;
        if (p.velLow && p.velHigh)
        {
            it.velLow = std::min (*p.velLow, *p.velHigh);
            it.velHigh = std::max (*p.velLow, *p.velHigh);
        }
        else if (const auto layer = layerOf (it))
        {
            const int n = std::max (1, layerCount);
            if (*layer > n) { diag (Severity::Error, p.stem, "layer " + std::to_string (*layer) + " exceeds the layer count " + std::to_string (n)); it.included = false; continue; }
            it.velLow = static_cast<int> (std::floor ((*layer - 1) * 127.0 / n)) + 1;
            it.velHigh = static_cast<int> (std::floor (*layer * 127.0 / n));
        }
        else if (p.velValue)
        {
            const int v = *p.velValue;
            switch (options.velocities.strategy)
            {
                case RangeStrategy::Exact: it.velLow = it.velHigh = v; break;
                case RangeStrategy::Midpoint: it.velLow = velMid.at (v).first; it.velHigh = velMid.at (v).second; break;
                case RangeStrategy::Fixed:
                    it.velLow = std::clamp (v - options.velocities.below, 1, 127);
                    it.velHigh = std::clamp (v + options.velocities.above, 1, 127);
                    break;
            }
        }
        else if (p.velLow)
        {
            const auto next = velLows.upper_bound (*p.velLow);
            it.velLow = *p.velLow;
            it.velHigh = next == velLows.end() ? 127 : *next - 1;
        }
        else if (p.velHigh)
        {
            const auto prev = velHighs.lower_bound (*p.velHigh);
            it.velHigh = *p.velHigh;
            it.velLow = prev == velHighs.begin() ? 1 : *std::prev (prev) + 1;
        }
    }

    // ---- étendues de notes, par Groupe et par type de déclenchement ----
    std::map<std::pair<std::string, bool>, std::set<int>> rootsOf;
    for (const auto& it : items)
        if (it.included) rootsOf[{ it.parsed.group, it.parsed.release }].insert (it.note);
    std::map<std::pair<std::string, bool>, std::map<int, std::pair<int, int>>> midKeys;
    for (const auto& [k, roots] : rootsOf) midKeys[k] = midpointRanges (roots, 0, 127);

    for (auto& it : items)
    {
        if (! it.included) continue;
        const auto& p = it.parsed;
        const int r = it.note;
        const auto key = std::make_pair (p.group, p.release);
        if (p.keyLow || p.keyHigh)
        {
            it.keyLow = p.keyLow ? *p.keyLow : r;
            it.keyHigh = p.keyHigh ? *p.keyHigh : r;
            if (it.keyLow > it.keyHigh) std::swap (it.keyLow, it.keyHigh);
        }
        else if (p.keyBelow || p.keyAbove)
        {
            it.keyLow = std::clamp (r - (p.keyBelow ? *p.keyBelow : 0), 0, 127);
            it.keyHigh = std::clamp (r + (p.keyAbove ? *p.keyAbove : 0), 0, 127);
        }
        else
            switch (options.keys.strategy)
            {
                case RangeStrategy::Exact: it.keyLow = it.keyHigh = r; break;
                case RangeStrategy::Midpoint: it.keyLow = midKeys[key].at (r).first; it.keyHigh = midKeys[key].at (r).second; break;
                case RangeStrategy::Fixed:
                    it.keyLow = std::clamp (r - options.keys.below, 0, 127);
                    it.keyHigh = std::clamp (r + options.keys.above, 0, 127);
                    break;
            }
    }

    // ---- round-robin et doublons ----
    struct Place
    {
        std::string group; bool release; int note, keyLow, keyHigh, velLow, velHigh;
        bool operator< (const Place& o) const
        {
            return std::tie (group, release, note, keyLow, keyHigh, velLow, velHigh) < std::tie (o.group, o.release, o.note, o.keyLow, o.keyHigh, o.velLow, o.velHigh);
        }
    };
    std::map<Place, std::set<int>> taken;
    std::map<Place, int> counter;
    for (auto& it : items)
    {
        if (! it.included) continue;
        const Place pl { it.parsed.group, it.parsed.release, it.note, it.keyLow, it.keyHigh, it.velLow, it.velHigh };
        auto& used = taken[pl];
        if (it.parsed.roundRobin)
        {
            if (! used.insert (*it.parsed.roundRobin).second)
            {
                diag (Severity::Error, it.parsed.stem, "duplicate: another file already has round-robin " + std::to_string (*it.parsed.roundRobin) + " at the same note and velocity");
                it.included = false;
                continue;
            }
            it.rr = *it.parsed.roundRobin;
        }
        else if (! used.empty() && ! templateHasRoundRobin)
        {
            diag (Severity::Error, it.parsed.stem, "duplicate: another file already occupies the same note and velocity (no round-robin field in the template)");
            it.included = false;
            continue;
        }
        else
        {
            // Pas de numéro dans le nom : c'est l'échantillon principal de cette place (numéro 0), qui joue en
            // alternance avec les autres. Deux « principaux » à la même place : doublon.
            if (used.count (0))
            {
                diag (Severity::Error, it.parsed.stem, "duplicate: another file already is the main sample at the same note and velocity");
                it.included = false;
                continue;
            }
            used.insert (0);
            it.rr = 0;
        }
    }

    // ---- diagnostics d'ensemble ----
    {
        std::map<std::pair<std::string, bool>, std::vector<const Item*>> byGroup;
        for (const auto& it : items)
            if (it.included) byGroup[{ it.parsed.group, it.parsed.release }].push_back (&it);
        for (const auto& [k, list] : byGroup)
        {
            // chevauchement d'étendues de notes entre racines différentes à vélocité commune
            std::map<int, std::pair<int, int>> rangeOfRoot;
            for (const auto* it : list) rangeOfRoot[it->note] = { it->keyLow, it->keyHigh };
            int prevHigh = -1, prevRoot = -1;
            for (const auto& [root, rg] : rangeOfRoot)
            {
                if (prevRoot >= 0 && rg.first <= prevHigh)
                    diag (Severity::Warning, "", "key ranges of notes " + std::to_string (prevRoot) + " and " + std::to_string (root) + " overlap"
                                                  + (k.first.empty() ? "" : " (group \"" + k.first + "\")"));
                prevHigh = std::max (prevHigh, rg.second);
                prevRoot = root;
            }
            // couches manquantes sur certaines notes
            if (layerCount > 1)
            {
                std::map<int, std::set<int>> layersOfNote;
                for (const auto* it : list)
                    if (const auto l = layerOf (*it)) layersOfNote[it->note].insert (*l);
                for (const auto& [note, ls] : layersOfNote)
                    if (static_cast<int> (ls.size()) < layerCount)
                        diag (Severity::Info, "", "note " + std::to_string (note) + " has " + std::to_string (ls.size()) + " of " + std::to_string (layerCount) + " velocity layers");
            }
        }
    }

    // ---- sortie ----
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b)
    {
        return std::tie (a.note, a.velLow, a.rr, a.path) < std::tie (b.note, b.velLow, b.rr, b.path);
    });
    for (const auto& it : items)
    {
        PlannedZone pz;
        pz.path = it.path;
        pz.included = it.included;
        pz.zone.samplePath = it.path;
        pz.zone.rootNote = std::max (0, it.note);
        pz.zone.keyLow = it.keyLow;
        pz.zone.keyHigh = it.keyHigh;
        pz.zone.velLow = it.velLow;
        pz.zone.velHigh = it.velHigh;
        pz.zone.roundRobin = it.rr;
        pz.zone.group = it.parsed.group;
        pz.zone.onRelease = it.parsed.release;
        plan.zones.push_back (std::move (pz));
    }
    return plan;
}
} // namespace neptune::core::import
