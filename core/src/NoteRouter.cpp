#include "neptune/core/NoteRouter.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace neptune::core::router
{
using json = nlohmann::json;

const char* toString (Combine c) noexcept
{
    switch (c)
    {
        case Combine::All: return "and";
        case Combine::Any: return "or";
        case Combine::One: return "xor";
        case Combine::NotAll: return "nand";
        case Combine::None: return "nor";
    }
    return "and";
}

const char* toString (Select s) noexcept
{
    switch (s)
    {
        case Select::All: return "all";
        case Select::Lowest: return "lowest";
        case Select::Highest: return "highest";
    }
    return "all";
}

namespace
{
Select selectFrom (const std::string& s) noexcept
{
    for (int i = 0; i < numSelects; ++i)
        if (s == toString (static_cast<Select> (i))) return static_cast<Select> (i);
    return Select::All;
}

Combine combineFrom (const std::string& s) noexcept
{
    for (int i = 0; i < numCombines; ++i)
        if (s == toString (static_cast<Combine> (i))) return static_cast<Combine> (i);
    return Combine::All;
}
} // namespace

Config sanitised (Config c)
{
    if (c.rules.size() > static_cast<std::size_t> (kMaxRules)) c.rules.resize (kMaxRules);
    for (auto& r : c.rules)
    {
        if (r.name.size() > 64) r.name.resize (64);
        r.srcChannel = std::clamp (r.srcChannel, 0, 16);
        r.dstChannel = std::clamp (r.dstChannel, 1, 16);
        r.keyLo = std::clamp (r.keyLo, 0, 127);
        r.keyHi = std::clamp (r.keyHi, 0, 127);
        if (r.keyHi < r.keyLo) std::swap (r.keyLo, r.keyHi);
        r.transpose = std::clamp (r.transpose, -48, 48);
        if (r.select != Select::All) r.replace = false;   // basse et mélodie ajoutent toujours
        if (r.terms.size() > static_cast<std::size_t> (kMaxTerms)) r.terms.resize (kMaxTerms);
        r.terms.erase (std::remove_if (r.terms.begin(), r.terms.end(), [] (const Term& t) { return t.param == 0; }), r.terms.end());
    }
    return c;
}

std::string toJson (const Config& in)
{
    const Config c = sanitised (in);
    json rules = json::array();
    for (const auto& r : c.rules)
    {
        json o;
        o["on"] = r.on;
        if (! r.name.empty()) o["name"] = r.name;
        o["src"] = r.srcChannel;
        o["lo"] = r.keyLo;
        o["hi"] = r.keyHi;
        o["dst"] = r.dstChannel;
        o["transpose"] = r.transpose;
        o["replace"] = r.replace;
        if (r.select != Select::All) o["select"] = toString (r.select);
        o["combine"] = toString (r.combine);
        json terms = json::array();
        for (const auto& t : r.terms) terms.push_back ({ { "param", t.param }, { "not", t.negate } });
        o["terms"] = std::move (terms);
        rules.push_back (std::move (o));
    }
    json doc;
    doc["rules"] = std::move (rules);
    return doc.dump();
}

Config fromJson (std::string_view text)
{
    Config c;
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (! doc.is_object()) return c;
    const auto it = doc.find ("rules");
    if (it == doc.end() || ! it->is_array()) return c;
    auto num = [] (const json& o, const char* key, int def) { const auto f = o.find (key); return f != o.end() && f->is_number() ? static_cast<int> (f->get<double>()) : def; };
    auto flag = [] (const json& o, const char* key, bool def) { const auto f = o.find (key); return f != o.end() && f->is_boolean() ? f->get<bool>() : def; };
    for (const auto& o : *it)
    {
        if (! o.is_object() || c.rules.size() >= static_cast<std::size_t> (kMaxRules)) continue;
        Rule r;
        r.on = flag (o, "on", true);
        if (const auto f = o.find ("name"); f != o.end() && f->is_string()) r.name = f->get<std::string>();
        r.srcChannel = num (o, "src", 0);
        r.keyLo = num (o, "lo", 0);
        r.keyHi = num (o, "hi", 127);
        r.dstChannel = num (o, "dst", 1);
        r.transpose = num (o, "transpose", 0);
        r.replace = flag (o, "replace", false);
        if (const auto f = o.find ("select"); f != o.end() && f->is_string()) r.select = selectFrom (f->get<std::string>());
        if (r.select != Select::All) r.replace = false;
        if (const auto f = o.find ("combine"); f != o.end() && f->is_string()) r.combine = combineFrom (f->get<std::string>());
        if (const auto f = o.find ("terms"); f != o.end() && f->is_array())
            for (const auto& t : *f)
            {
                if (! t.is_object() || r.terms.size() >= static_cast<std::size_t> (kMaxTerms)) continue;
                const auto p = t.find ("param");
                if (p == t.end() || ! p->is_number_unsigned()) continue;
                Term term;
                term.param = p->get<ParamId>();
                term.negate = flag (t, "not", false);
                r.terms.push_back (term);
            }
        c.rules.push_back (std::move (r));
    }
    return sanitised (std::move (c));
}

bool addSwitchParams (ParameterSet& set, std::string* error)
{
    for (int i = 0; i < kSwitches; ++i)
        if (! set.add (ParamSpec::toggle (switchParam (i), "Router Switch " + std::to_string (i + 1)), error)) return false;
    for (int i = 0; i < kMaxRules; ++i)
        if (! set.add (ParamSpec::stepped (transposeParam (i), "Router Rule " + std::to_string (i + 1) + " Transpose", Unit::Semitones, -48, 48, 0), error)) return false;
    return true;
}

std::shared_ptr<const Compiled> compile (const Config& in, const ParameterSet& set)
{
    const Config c = sanitised (in);
    auto out = std::make_shared<Compiled>();
    for (std::size_t ruleIndex = 0; ruleIndex < c.rules.size(); ++ruleIndex)
    {
        const auto& r = c.rules[ruleIndex];
        if (! r.on) continue;
        CompiledRule cr;
        cr.transposeIndex = set.indexOf (transposeParam (static_cast<int> (ruleIndex)));
        cr.srcChannel = r.srcChannel - 1;   // -1 : tous
        cr.keyLo = r.keyLo;
        cr.keyHi = r.keyHi;
        cr.dstChannel = r.dstChannel - 1;
        cr.transpose = r.transpose;
        cr.replace = r.replace;
        cr.select = r.select;
        cr.combine = r.combine;
        bool usable = true;
        for (const auto& t : r.terms)
        {
            const auto index = set.indexOf (t.param);
            if (index == ParameterSet::npos) { usable = false; break; }   // un paramètre inconnu : la règle est écartée plutôt que de se déclencher à tort
            cr.term[static_cast<std::size_t> (cr.termCount)] = index;
            cr.negate[static_cast<std::size_t> (cr.termCount)] = t.negate;
            ++cr.termCount;
        }
        if (usable)
        {
            if (cr.select != Select::All) out->dynamic.push_back (static_cast<int> (out->rules.size()));
            out->rules.push_back (cr);
        }
    }
    return out;
}

namespace
{
bool active (const CompiledRule& r, const ParameterSet& set) noexcept
{
    if (r.termCount == 0) return true;
    int trues = 0;
    for (int i = 0; i < r.termCount; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const bool v = set.effectiveNormalizedAt (r.term[k]) >= 0.5;   // à deux états : 0 ou 1 ; tout autre paramètre : la moitié de sa course
        if (v != r.negate[k]) ++trues;
    }
    switch (r.combine)
    {
        case Combine::All: return trues == r.termCount;
        case Combine::Any: return trues > 0;
        case Combine::One: return trues == 1;
        case Combine::NotAll: return trues != r.termCount;
        case Combine::None: return trues == 0;
    }
    return false;
}
} // namespace

bool isActive (const CompiledRule& r, const ParameterSet& set) noexcept { return active (r, set); }

int semitonesOf (const CompiledRule& r, const ParameterSet& set) noexcept
{
    int semitones = r.transpose;
    if (r.transposeIndex != ParameterSet::npos) semitones += static_cast<int> (std::lround (set.effectiveAt (r.transposeIndex)));
    return semitones;
}

int expand (const Compiled& c, const ParameterSet& set, int channel, int key, std::array<Target, kMaxTargets>& out, bool* originalKept) noexcept
{
    int n = 0;
    auto add = [&] (int ch, int k)
    {
        if (k < 0 || k > 127 || n >= kMaxTargets) return;
        const Target t { ch, k };
        for (int i = 0; i < n; ++i)
            if (out[static_cast<std::size_t> (i)] == t) return;
        out[static_cast<std::size_t> (n++)] = t;
    };
    bool original = true;
    std::array<Target, kMaxTargets> routed {};
    int routedCount = 0;
    for (const auto& r : c.rules)
    {
        if (r.select != Select::All) continue;   // basse et mélodie : suivies par le moteur (note accouplée dynamique)
        if (r.srcChannel >= 0 && r.srcChannel != channel) continue;
        if (key < r.keyLo || key > r.keyHi) continue;
        if (! active (r, set)) continue;
        if (r.replace) original = false;
        int semitones = r.transpose;
        if (r.transposeIndex != ParameterSet::npos) semitones += static_cast<int> (std::lround (set.effectiveAt (r.transposeIndex)));
        if (routedCount < kMaxTargets) routed[static_cast<std::size_t> (routedCount++)] = { r.dstChannel, key + semitones };
    }
    if (original) add (channel, key);
    if (originalKept != nullptr) *originalKept = original && n > 0;
    for (int i = 0; i < routedCount; ++i) add (routed[static_cast<std::size_t> (i)].channel, routed[static_cast<std::size_t> (i)].key);
    return n;
}
} // namespace neptune::core::router
