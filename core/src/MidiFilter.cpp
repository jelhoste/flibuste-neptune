#include "neptune/core/MidiFilter.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace neptune::core::midi
{
using nlohmann::json;

const char* toString (MsgKind k) noexcept
{
    switch (k)
    {
        case MsgKind::Note: return "note";
        case MsgKind::NoteOn: return "note_on";
        case MsgKind::NoteOff: return "note_off";
        case MsgKind::ControlChange: return "cc";
        case MsgKind::PitchBend: return "pitch_bend";
        case MsgKind::ChannelPressure: return "channel_pressure";
        case MsgKind::PolyPressure: return "poly_pressure";
        case MsgKind::ProgramChange: return "program_change";
    }
    return "note";
}

const char* toString (Op o) noexcept
{
    switch (o)
    {
        case Op::Leaf: return "leaf";
        case Op::And: return "and";
        case Op::Or: return "or";
        case Op::Xor: return "xor";
        case Op::Nand: return "nand";
        case Op::Nor: return "nor";
        case Op::Not: return "not";
    }
    return "leaf";
}

namespace
{
bool msgKindFromString (std::string_view s, MsgKind& out)
{
    for (int i = 0; i < numMsgKinds; ++i)
        if (s == toString (static_cast<MsgKind> (i))) { out = static_cast<MsgKind> (i); return true; }
    return false;
}

bool opFromString (std::string_view s, Op& out)
{
    for (int i = 0; i < numOps; ++i)
        if (s == toString (static_cast<Op> (i))) { out = static_cast<Op> (i); return true; }
    return false;
}

// Une condition utilise-t-elle la première / la seconde donnée du message ?
bool usesData1 (MsgKind k) noexcept
{
    return k == MsgKind::Note || k == MsgKind::NoteOn || k == MsgKind::NoteOff || k == MsgKind::ControlChange || k == MsgKind::PolyPressure
        || k == MsgKind::ProgramChange;
}
bool usesData2 (MsgKind k) noexcept { return k != MsgKind::ProgramChange; }

int clamp127 (int v) noexcept { return std::clamp (v, 0, 127); }
} // namespace

bool Condition::matches (const Message& m) const noexcept
{
    const bool kindOk = kind == MsgKind::Note ? (m.kind == MsgKind::NoteOn || m.kind == MsgKind::NoteOff) : kind == m.kind;
    if (! kindOk) return false;
    if (channel != 0 && channel != m.channel + 1) return false;
    if (usesData1 (kind) && (m.data1 < lo1 || m.data1 > hi1)) return false;
    if (usesData2 (kind) && (m.data2 < lo2 || m.data2 > hi2)) return false;
    return true;
}

void State::update (const Message& m) noexcept
{
    const auto ch = static_cast<std::size_t> (m.channel & 15);
    const auto d1 = static_cast<std::size_t> (m.data1 & 127);
    const auto d2 = static_cast<std::uint8_t> (std::clamp (m.data2, 0, 127));
    switch (m.kind)
    {
        case MsgKind::ControlChange: cc[ch][d1] = d2; break;
        case MsgKind::PitchBend: bend[ch] = d2; break;
        case MsgKind::ChannelPressure: pressure[ch] = d2; break;
        case MsgKind::PolyPressure: poly[ch][d1] = d2; break;
        case MsgKind::ProgramChange: program[ch] = static_cast<std::uint8_t> (d1); break;
        default: break;
    }
}

bool Condition::matchesState (const State& st, const Message& trigger) const noexcept
{
    auto channelOk = [&] (auto&& test) noexcept
    {
        // « tous les canaux » : le canal de la note qui se déclenche (la pédale de ce canal), pas n'importe quel canal
        return test (static_cast<std::size_t> (channel != 0 ? channel - 1 : trigger.channel & 15));
    };
    auto in2 = [&] (int v) noexcept { return v >= lo2 && v <= hi2; };
    switch (kind)
    {
        case MsgKind::Note:
        case MsgKind::NoteOn: return matches (trigger);
        case MsgKind::NoteOff: return false;
        case MsgKind::ControlChange:
            return channelOk ([&] (std::size_t c) noexcept { for (int n = lo1; n <= hi1; ++n) if (in2 (st.cc[c][static_cast<std::size_t> (n)])) return true; return false; });
        case MsgKind::PolyPressure:
            return channelOk ([&] (std::size_t c) noexcept { for (int n = lo1; n <= hi1; ++n) if (in2 (st.poly[c][static_cast<std::size_t> (n)])) return true; return false; });
        case MsgKind::PitchBend: return channelOk ([&] (std::size_t c) noexcept { return in2 (st.bend[c]); });
        case MsgKind::ChannelPressure: return channelOk ([&] (std::size_t c) noexcept { return in2 (st.pressure[c]); });
        case MsgKind::ProgramChange: return channelOk ([&] (std::size_t c) noexcept { return st.program[c] >= lo1 && st.program[c] <= hi1; });
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------- description

int countNodes (const Rule& r) noexcept
{
    int n = 1;
    for (const auto& c : r.children) n += countNodes (c);
    return n;
}

namespace
{
void sanitiseRule (Rule& r, int depth, int& budget)
{
    --budget;
    r.condition.channel = std::clamp (r.condition.channel, 0, 16);
    r.condition.lo1 = clamp127 (r.condition.lo1);
    r.condition.hi1 = clamp127 (r.condition.hi1);
    r.condition.lo2 = clamp127 (r.condition.lo2);
    r.condition.hi2 = clamp127 (r.condition.hi2);
    if (r.condition.lo1 > r.condition.hi1) std::swap (r.condition.lo1, r.condition.hi1);
    if (r.condition.lo2 > r.condition.hi2) std::swap (r.condition.lo2, r.condition.hi2);
    if (r.op == Op::Leaf || depth >= maxDepth)
    {
        r.children.clear();
        if (r.op != Op::Leaf) r.op = Op::Leaf; // trop profond : devient une feuille
        return;
    }
    const std::size_t limit = r.op == Op::Not ? 1 : static_cast<std::size_t> (maxChildren);
    if (r.children.size() > limit) r.children.resize (limit);
    std::vector<Rule> kept;
    for (auto& c : r.children)
    {
        if (budget <= 0) break;
        sanitiseRule (c, depth + 1, budget);
        kept.push_back (std::move (c));
    }
    r.children = std::move (kept);
}
} // namespace

FilterDescription sanitised (FilterDescription d)
{
    if (d.root.op == Op::Leaf) // la racine est un groupe ; une feuille seule est enveloppée
    {
        Rule leaf = std::move (d.root);
        d.root = Rule {};
        d.root.op = Op::And;
        d.root.children.push_back (std::move (leaf));
    }
    int budget = maxNodes;
    sanitiseRule (d.root, 0, budget);
    return d;
}

namespace
{
json ruleJson (const Rule& r)
{
    json o;
    o["op"] = toString (r.op);
    if (r.op == Op::Leaf)
    {
        o["kind"] = toString (r.condition.kind);
        o["channel"] = r.condition.channel;
        o["lo1"] = r.condition.lo1;
        o["hi1"] = r.condition.hi1;
        o["lo2"] = r.condition.lo2;
        o["hi2"] = r.condition.hi2;
    }
    else
    {
        json kids = json::array();
        for (const auto& c : r.children) kids.push_back (ruleJson (c));
        o["children"] = std::move (kids);
    }
    return o;
}

int intOf (const json& o, const char* k, int def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_number() ? static_cast<int> (std::clamp (it->get<double>(), -1.0e6, 1.0e6)) : def;
}

Rule ruleFrom (const json& o, int depth, int& budget)
{
    Rule r;
    if (! o.is_object() || budget <= 0 || depth > maxDepth + 1) return r;
    --budget;
    if (const auto it = o.find ("op"); it != o.end() && it->is_string()) opFromString (it->get<std::string>(), r.op);
    if (r.op == Op::Leaf)
    {
        if (const auto it = o.find ("kind"); it != o.end() && it->is_string()) msgKindFromString (it->get<std::string>(), r.condition.kind);
        r.condition.channel = intOf (o, "channel", 0);
        r.condition.lo1 = intOf (o, "lo1", 0);
        r.condition.hi1 = intOf (o, "hi1", 127);
        r.condition.lo2 = intOf (o, "lo2", 0);
        r.condition.hi2 = intOf (o, "hi2", 127);
    }
    else if (const auto it = o.find ("children"); it != o.end() && it->is_array())
        for (const auto& c : *it)
        {
            if (budget <= 0 || r.children.size() >= static_cast<std::size_t> (maxChildren)) break;
            r.children.push_back (ruleFrom (c, depth + 1, budget));
        }
    return r;
}
} // namespace

std::string toJson (const FilterDescription& in)
{
    const auto d = sanitised (in);
    json o;
    o["enabled"] = d.enabled;
    o["root"] = ruleJson (d.root);
    return o.dump();
}

FilterDescription filterFromJson (std::string_view text)
{
    FilterDescription d;
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
    if (const auto it = doc.find ("enabled"); it != doc.end() && it->is_boolean()) d.enabled = it->get<bool>();
    if (const auto it = doc.find ("root"); it != doc.end())
    {
        int budget = maxNodes;
        d.root = ruleFrom (*it, 0, budget);
    }
    return sanitised (std::move (d));
}

namespace
{
std::string rangeText (const char* label, int lo, int hi)
{
    if (lo == 0 && hi == 127) return {};
    return std::string (label) + " " + (lo == hi ? std::to_string (lo) : std::to_string (lo) + "-" + std::to_string (hi));
}

std::string leafText (const Condition& c)
{
    std::string s;
    auto add = [&s] (const std::string& part)
    {
        if (part.empty()) return;
        if (! s.empty()) s += ' ';
        s += part;
    };
    switch (c.kind)
    {
        case MsgKind::Note: add ("note"); add (rangeText ("n", c.lo1, c.hi1)); add (rangeText ("vel", c.lo2, c.hi2)); break;
        case MsgKind::NoteOn: add ("note on"); add (rangeText ("n", c.lo1, c.hi1)); add (rangeText ("vel", c.lo2, c.hi2)); break;
        case MsgKind::NoteOff: add ("note off"); add (rangeText ("n", c.lo1, c.hi1)); break;
        case MsgKind::ControlChange: add ("cc"); add (c.lo1 == 0 && c.hi1 == 127 ? "*" : rangeText ("", c.lo1, c.hi1)); add (rangeText ("val", c.lo2, c.hi2)); break;
        case MsgKind::PitchBend: add ("pitch bend"); add (rangeText ("val", c.lo2, c.hi2)); break;
        case MsgKind::ChannelPressure: add ("aftertouch"); add (rangeText ("val", c.lo2, c.hi2)); break;
        case MsgKind::PolyPressure: add ("poly aftertouch"); add (rangeText ("n", c.lo1, c.hi1)); add (rangeText ("val", c.lo2, c.hi2)); break;
        case MsgKind::ProgramChange: add ("program"); add (rangeText ("", c.lo1, c.hi1)); break;
    }
    if (c.channel != 0) add ("ch " + std::to_string (c.channel));
    return s;
}
} // namespace

std::string describe (const Rule& r)
{
    if (r.op == Op::Leaf) return leafText (r.condition);
    if (r.children.empty()) return "()";
    if (r.op == Op::Not) return "NOT " + describe (r.children.front());
    static const char* const names[] = { "", "AND", "OR", "XOR", "NAND", "NOR", "" };
    std::string s = "(";
    for (std::size_t i = 0; i < r.children.size(); ++i)
    {
        if (i > 0) s += std::string (" ") + names[static_cast<int> (r.op)] + " ";
        s += describe (r.children[i]);
    }
    return s + ")";
}

// ---------------------------------------------------------------------------------------------------- compilé

CompiledFilter::CompiledFilter (const FilterDescription& in)
{
    const auto d = sanitised (in);
    enabled_ = d.enabled;
    // préordre : le noeud 0 est la racine ; les enfants sont écrits après leur parent
    struct Item { const Rule* rule; int slot; };
    std::vector<Item> stack;
    stack.push_back ({ &d.root, 0 });
    count_ = 1;
    while (! stack.empty())
    {
        const auto [rule, slot] = stack.back();
        stack.pop_back();
        auto& n = nodes_[static_cast<std::size_t> (slot)];
        n.op = rule->op;
        n.cond = rule->condition;
        n.childCount = 0;
        for (const auto& c : rule->children)
        {
            if (count_ >= maxNodes || n.childCount >= maxChildren) break;
            const int idx = count_++;
            n.child[static_cast<std::size_t> (n.childCount++)] = idx;
            stack.push_back ({ &c, idx });
        }
    }
}

bool CompiledFilter::eval (int index, const Message& m, int depth) const noexcept
{
    const auto& n = nodes_[static_cast<std::size_t> (index)];
    if (n.op == Op::Leaf) return n.cond.matches (m);
    if (depth > maxDepth + 1) return false;
    int trueCount = 0;
    for (int i = 0; i < n.childCount; ++i)
        if (eval (n.child[static_cast<std::size_t> (i)], m, depth + 1)) ++trueCount;
    switch (n.op)
    {
        case Op::And: return n.childCount > 0 && trueCount == n.childCount;
        case Op::Or: return trueCount > 0;
        case Op::Xor: return trueCount == 1;
        case Op::Nand: return n.childCount > 0 && trueCount != n.childCount;
        case Op::Nor: return trueCount == 0;
        case Op::Not: return n.childCount > 0 && trueCount == 0;
        case Op::Leaf: break;
    }
    return false;
}

bool CompiledFilter::evalState (int index, const State& st, const Message& trigger, int depth) const noexcept
{
    const auto& n = nodes_[static_cast<std::size_t> (index)];
    if (n.op == Op::Leaf) return n.cond.matchesState (st, trigger);
    if (depth > maxDepth + 1) return false;
    int trueCount = 0;
    for (int i = 0; i < n.childCount; ++i)
        if (evalState (n.child[static_cast<std::size_t> (i)], st, trigger, depth + 1)) ++trueCount;
    switch (n.op)
    {
        case Op::And: return n.childCount > 0 && trueCount == n.childCount;
        case Op::Or: return trueCount > 0;
        case Op::Xor: return trueCount == 1;
        case Op::Nand: return n.childCount > 0 && trueCount != n.childCount;
        case Op::Nor: return trueCount == 0;
        case Op::Not: return n.childCount > 0 && trueCount == 0;
        case Op::Leaf: break;
    }
    return false;
}

bool CompiledFilter::passesState (const State& st, const Message& trigger) const noexcept
{
    if (! enabled()) return true;
    return evalState (0, st, trigger, 0);
}

bool CompiledFilter::passes (const Message& m) const noexcept
{
    if (! enabled()) return true;
    return eval (0, m, 0);
}
} // namespace neptune::core::midi
