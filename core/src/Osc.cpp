#include "neptune/core/Osc.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

namespace neptune::core::osc
{
namespace
{
constexpr std::size_t kMaxAddress = 256;
constexpr std::size_t kMaxArgs = 16;
constexpr int kMaxDepth = 4;

// ---------------------------------------------------------------------------------------------------------------------
// Codec

struct Reader
{
    const std::uint8_t* p;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    bool need (std::size_t n) { if (pos + n > size) ok = false; return ok; }
    std::uint32_t u32()
    {
        if (! need (4)) return 0;
        const auto v = (std::uint32_t (p[pos]) << 24) | (std::uint32_t (p[pos + 1]) << 16) | (std::uint32_t (p[pos + 2]) << 8) | std::uint32_t (p[pos + 3]);
        pos += 4;
        return v;
    }
    std::uint64_t u64() { const auto hi = u32(); const auto lo = u32(); return (std::uint64_t (hi) << 32) | lo; }
    // Texte terminé par un zéro, complété jusqu'à un multiple de 4 octets.
    std::string str (std::size_t maxLength)
    {
        std::string out;
        if (! ok) return out;
        std::size_t end = pos;
        while (end < size && p[end] != 0) ++end;
        if (end >= size || end - pos > maxLength) { ok = false; return out; }
        out.assign (reinterpret_cast<const char*> (p + pos), end - pos);
        const std::size_t padded = (end - pos + 4) & ~std::size_t (3);   // zéro final compris
        if (! need (padded)) return {};
        pos += padded;
        return out;
    }
};

bool parseMessage (const std::uint8_t* data, std::size_t size, std::vector<Message>& out, std::size_t maxMessages)
{
    if (out.size() >= maxMessages) return true;
    Reader r { data, size };
    Message m;
    m.address = r.str (kMaxAddress);
    if (! r.ok || m.address.empty() || m.address[0] != '/') return false;
    std::string tags;
    if (r.pos < size) tags = r.str (kMaxArgs + 1);   // un message sans étiquettes (très ancien) n'a pas d'arguments
    if (! r.ok) return false;
    if (! tags.empty())
    {
        if (tags[0] != ',') return false;
        for (std::size_t i = 1; i < tags.size(); ++i)
        {
            Arg a;
            a.type = tags[i];
            switch (tags[i])
            {
                case 'i': a.number = static_cast<double> (static_cast<std::int32_t> (r.u32())); break;
                case 'h': a.number = static_cast<double> (static_cast<std::int64_t> (r.u64())); break;
                case 'f': { const auto bits = r.u32(); float f; std::memcpy (&f, &bits, 4); a.number = static_cast<double> (f); break; }
                case 'd': { const auto bits = r.u64(); double d; std::memcpy (&d, &bits, 8); a.number = d; break; }
                case 's': case 'S': a.type = 's'; a.text = r.str (4096); break;
                case 'T': a.number = 1.0; break;
                case 'F': a.number = 0.0; break;
                case 'N': case 'I': break;
                case 't': r.u64(); break;                // étiquette de temps : lue, ignorée
                case 'b': { const auto n = r.u32(); if (r.need ((std::size_t (n) + 3) & ~std::size_t (3))) r.pos += (std::size_t (n) + 3) & ~std::size_t (3); break; }
                default: return false;                   // type inconnu : la suite est illisible
            }
            if (! r.ok) return false;
            if (tags[i] != 't' && tags[i] != 'b') m.args.push_back (std::move (a));
        }
    }
    out.push_back (std::move (m));
    return true;
}

bool parseAny (const std::uint8_t* data, std::size_t size, std::vector<Message>& out, std::size_t maxMessages, int depth)
{
    if (size < 4) return false;
    if (size >= 8 && std::memcmp (data, "#bundle", 8) == 0)
    {
        if (depth >= kMaxDepth || size < 16) return false;
        Reader r { data, size, 16 };
        bool all = true;
        while (r.pos < size && out.size() < maxMessages)
        {
            const auto n = r.u32();
            if (! r.ok || n == 0 || ! r.need (n)) return false;
            all = parseAny (data + r.pos, n, out, maxMessages, depth + 1) && all;
            r.pos += n;
        }
        return all;
    }
    return parseMessage (data, size, out, maxMessages);
}

void putU32 (std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back (std::uint8_t (x >> 24)); v.push_back (std::uint8_t (x >> 16)); v.push_back (std::uint8_t (x >> 8)); v.push_back (std::uint8_t (x));
}
void putStr (std::vector<std::uint8_t>& v, const std::string& s)
{
    v.insert (v.end(), s.begin(), s.end());
    v.push_back (0);
    while (v.size() % 4 != 0) v.push_back (0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Table des adresses

struct Table
{
    std::vector<ParamId> ids;
    std::unordered_map<ParamId, std::string> byId;
    std::unordered_map<std::string, ParamId> byPath;

    void add (ParamId id, const std::string& path)
    {
        const std::string full = std::string (kPrefix) + path;
        if (byId.count (id) != 0 || byPath.count (full) != 0) return;   // jamais de doublon : la première déclaration gagne
        ids.push_back (id);
        byId.emplace (id, full);
        byPath.emplace (full, id);
    }
};

std::string num (int n) { return std::to_string (n); }

// Réglages d'une bande (piste, bus ou piste parallèle) : volume, pan, mute, solo, inserts, envois.
void addStrip (Table& t, ParamId base, const std::string& prefix, bool withType)
{
    t.add (base + 0, prefix + "/volume");
    t.add (base + 1, prefix + "/pan");
    t.add (base + 2, prefix + "/mute");
    t.add (base + 3, prefix + "/solo");
    if (withType) t.add (base + 4, prefix + "/type");
    for (int i = 0; i < mixer::numInserts; ++i) t.add (base + 10 + i, prefix + "/insert/" + num (i + 1) + "/bypass");
    for (int i = 0; i < mixer::numSends; ++i) t.add (base + 20 + i, prefix + "/send/" + num (i + 1) + "/level");
}

Table buildTable()
{
    Table t;
    t.add (probe::masterVolume, "/master/volume");
    t.add (probe::masterPan, "/master/pan");
    t.add (probe::masterMute, "/master/mute");
    t.add (probe::algorithm, "/master/algorithm");
    t.add (mixer::masterTrim, "/master/trim");
    t.add (mixer::masterMono, "/master/mono");
    for (int i = 0; i < mixer::numInserts; ++i) t.add (mixer::masterInsertBypassId (i), "/master/insert/" + num (i + 1) + "/bypass");

    static const char* const samplerKinds[mixer::kSamplerKinds] = { "gain", "tune_semitones", "tune_cents", "filter1_cutoff", "filter1_resonance", "filter2_cutoff", "filter2_resonance" };
    static const char* const synthKinds[mixer::kSynthKinds] = {
        "level_osc1", "level_osc2", "level_osc3", "level_sub", "level_noise", "fine_osc1", "fine_osc2", "fine_osc3",
        "pulse_osc1", "pulse_osc2", "pulse_osc3", "detune_osc1", "detune_osc2", "detune_osc3", "pitch_env_depth" };
    static const char* const synthEnvKinds[mixer::kSynthEnvKinds] = {
        "amp_attack", "amp_decay", "amp_sustain", "amp_release", "filter1_attack", "filter1_decay", "filter1_sustain", "filter1_release",
        "filter2_attack", "filter2_decay", "filter2_sustain", "filter2_release", "glide", "filter_balance" };
    static const char* const fmKinds[mixer::kFmKinds] = {
        "brightness", "env_speed", "feedback_drive", "level_op1", "level_op2", "level_op3", "level_op4", "level_op5", "level_op6",
        "unison_detune", "unison_spread" };
    static const char* const fmExtraKinds[mixer::kFmExtraKinds] = { "lfo_speed", "lfo_pmd", "lfo_amd", "bend_range", "wheel_pitch", "wheel_amp", "glide" };

    for (int k = 0; k < mixer::kSamplerKinds; ++k) t.add (mixer::samplerParam (0, static_cast<mixer::SamplerKind> (k)), std::string ("/track/1/sampler/") + samplerKinds[k]);

    for (int tr = 0; tr < mixer::numTracks; ++tr)
    {
        const std::string p = "/track/" + num (tr + 1);
        addStrip (t, probe::trackParam (tr, 0), p, true);
        for (int k = 0; k < mixer::kSamplerKinds; ++k) t.add (mixer::samplerParam (tr, static_cast<mixer::SamplerKind> (k)), p + "/sampler/" + samplerKinds[k]);
        for (int k = 0; k < mixer::kSynthKinds; ++k) t.add (mixer::synthParam (tr, static_cast<mixer::SynthKind> (k)), p + "/synth/" + synthKinds[k]);
        for (int k = 0; k < mixer::kSynthEnvKinds; ++k) t.add (mixer::synthEnvParam (tr, static_cast<mixer::SynthEnvKind> (k)), p + "/synth/" + synthEnvKinds[k]);
        for (int k = 0; k < mixer::kFmKinds; ++k) t.add (mixer::fmParam (tr, static_cast<mixer::FmKind> (k)), p + "/fm/" + fmKinds[k]);
        for (int k = 0; k < mixer::kFmExtraKinds; ++k) t.add (mixer::fmExtraParam (tr, static_cast<mixer::FmExtraKind> (k)), p + "/fm/" + fmExtraKinds[k]);

        // Orgue : tirettes 1..9 du supérieur et de l'inférieur, 1..2 du pédalier, puis les commandes par groupe.
        for (int d = 0; d < 9; ++d) t.add (mixer::organParam (tr, mixer::kOrgUpper0 + d), p + "/organ/upper/drawbar/" + num (d + 1));
        for (int d = 0; d < 9; ++d) t.add (mixer::organParam (tr, mixer::kOrgLower0 + d), p + "/organ/lower/drawbar/" + num (d + 1));
        t.add (mixer::organParam (tr, mixer::kOrgPedal16), p + "/organ/pedal/drawbar/1");
        t.add (mixer::organParam (tr, mixer::kOrgPedal8), p + "/organ/pedal/drawbar/2");
        t.add (mixer::organParam (tr, mixer::kOrgPercOn), p + "/organ/percussion/on");
        t.add (mixer::organParam (tr, mixer::kOrgPercSoft), p + "/organ/percussion/soft");
        t.add (mixer::organParam (tr, mixer::kOrgPercFast), p + "/organ/percussion/fast");
        t.add (mixer::organParam (tr, mixer::kOrgPercThird), p + "/organ/percussion/third");
        t.add (mixer::organParam (tr, mixer::kOrgVibMode), p + "/organ/vibrato/mode");
        t.add (mixer::organParam (tr, mixer::kOrgVibUpper), p + "/organ/vibrato/upper");
        t.add (mixer::organParam (tr, mixer::kOrgVibLower), p + "/organ/vibrato/lower");
        t.add (mixer::organParam (tr, mixer::kOrgVibDepth), p + "/organ/vibrato/depth");
        t.add (mixer::organParam (tr, mixer::kOrgLeslieSpeed), p + "/organ/leslie/speed");
        t.add (mixer::organParam (tr, mixer::kOrgLeslieMix), p + "/organ/leslie/mix");
        t.add (mixer::organParam (tr, mixer::kOrgDriveOn), p + "/organ/overdrive/on");
        t.add (mixer::organParam (tr, mixer::kOrgDriveInput), p + "/organ/overdrive/input");
        t.add (mixer::organParam (tr, mixer::kOrgDriveCharacter), p + "/organ/overdrive/character");
        t.add (mixer::organParam (tr, mixer::kOrgDriveOutput), p + "/organ/overdrive/output");
        t.add (mixer::organParam (tr, mixer::kOrgReverb), p + "/organ/reverb");

        for (int s = 0; s < 16; ++s) t.add (mixer::zoneSlotParam (tr, s), p + "/slot/" + num (s + 1));
    }
    for (int m = 0; m < control::kMacros; ++m) t.add (control::macroParamId (m), "/macro/" + num (m + 1));
    for (int b = 0; b < mixer::numBuses; ++b) addStrip (t, mixer::busParam (b, 0), "/bus/" + num (b + 1), false);
    for (int q = 0; q < mixer::numParallels; ++q) addStrip (t, mixer::parallelParam (q, 0), "/parallel/" + num (q + 1), false);
    return t;
}

const Table& table()
{
    static const Table t = buildTable();   // initialisation unique (thread-safe), avant tout usage temps réel
    return t;
}

bool parseUnsigned (std::string_view s, std::uint32_t& out)
{
    if (s.empty() || s.size() > 9) return false;
    std::uint32_t v = 0;
    for (const char c : s)
    {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint32_t> (c - '0');
    }
    out = v;
    return true;
}

bool numericArg (const Message& m, double& out)
{
    if (m.args.empty()) return false;
    const auto& a = m.args.front();
    if (a.type == 's' || a.type == 'N' || a.type == 'I') return false;
    out = a.number;
    return std::isfinite (out);
}

const char* kindName (Kind k) noexcept
{
    switch (k)
    {
        case Kind::Continuous: return "continuous";
        case Kind::Stepped: return "stepped";
        case Kind::Toggle: return "toggle";
        case Kind::Choice: return "choice";
    }
    return "continuous";
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------

bool parsePacket (const std::uint8_t* data, std::size_t size, std::vector<Message>& out, std::size_t maxMessages)
{
    if (data == nullptr) return false;
    return parseAny (data, size, out, maxMessages, 0);
}

std::vector<std::uint8_t> encode (const Message& m)
{
    std::vector<std::uint8_t> v;
    putStr (v, m.address);
    std::string tags = ",";
    for (const auto& a : m.args) tags += a.type;
    putStr (v, tags);
    for (const auto& a : m.args)
    {
        switch (a.type)
        {
            case 'i': putU32 (v, static_cast<std::uint32_t> (static_cast<std::int32_t> (a.number))); break;
            case 'f': { const float f = static_cast<float> (a.number); std::uint32_t bits; std::memcpy (&bits, &f, 4); putU32 (v, bits); break; }
            case 'd': { std::uint64_t bits; std::memcpy (&bits, &a.number, 8); putU32 (v, static_cast<std::uint32_t> (bits >> 32)); putU32 (v, static_cast<std::uint32_t> (bits)); break; }
            case 'h': { const auto x = static_cast<std::uint64_t> (static_cast<std::int64_t> (a.number)); putU32 (v, static_cast<std::uint32_t> (x >> 32)); putU32 (v, static_cast<std::uint32_t> (x)); break; }
            case 's': putStr (v, a.text); break;
            default: break;   // T F N I : pas de donnée
        }
    }
    return v;
}

std::vector<std::uint8_t> encodeBundle (const std::vector<std::vector<std::uint8_t>>& messages)
{
    std::vector<std::uint8_t> v;
    putStr (v, "#bundle");
    putU32 (v, 0);
    putU32 (v, 1);   // étiquette de temps « immédiat »
    for (const auto& m : messages)
    {
        putU32 (v, static_cast<std::uint32_t> (m.size()));
        v.insert (v.end(), m.begin(), m.end());
    }
    return v;
}

const std::string& addressFor (ParamId id)
{
    static const std::string none;
    const auto& t = table();
    const auto it = t.byId.find (id);
    return it == t.byId.end() ? none : it->second;
}

bool paramForAddress (std::string_view address, ParamId& out)
{
    const auto& t = table();
    const auto it = t.byPath.find (std::string (address));
    if (it == t.byPath.end()) return false;
    out = it->second;
    return true;
}

const std::vector<ParamId>& catalogIds() { return table().ids; }

bool RateLimiter::allow (std::int64_t nowMs) noexcept
{
    if (nowMs - windowStart_ >= windowMs_ || nowMs < windowStart_)
    {
        windowStart_ = nowMs;
        used_ = 0;
    }
    if (used_ >= perWindow_)
    {
        ++dropped_;
        return false;
    }
    ++used_;
    return true;
}

Dispatcher::Dispatcher (control::ControlEngine& c, ParameterSet& s) : control_ (c), set_ (s) {}

int Dispatcher::handlePacket (const std::uint8_t* data, std::size_t size, std::vector<std::vector<std::uint8_t>>& replies, RateLimiter* limiter, std::int64_t nowMs)
{
    std::vector<Message> messages;
    parsePacket (data, size, messages, 1024);   // un paquet à moitié illisible garde ses messages bien formés
    int applied = 0;
    for (const auto& m : messages)
    {
        if (limiter != nullptr && ! limiter->allow (nowMs)) continue;
        applied += handleMessage (m, replies);
    }
    return applied;
}

int Dispatcher::handleMessage (const Message& m, std::vector<std::vector<std::uint8_t>>& replies)
{
    const std::string_view address = m.address;
    int applied = 0;
    double value = 0.0;
    const bool hasValue = numericArg (m, value);

    if (address == "/neptune/list")
    {
        std::string_view prefix;
        if (! m.args.empty() && m.args.front().type == 's') prefix = m.args.front().text;
        catalogue (prefix, replies);
        return 1;
    }

    if (hasValue && address.size() > kPrefix.size() && address.substr (0, kPrefix.size()) == kPrefix)
    {
        bool real = false;
        std::string_view path = address;
        if (path.size() > 5 && path.substr (path.size() - 5) == "/real")
        {
            real = true;
            path.remove_suffix (5);
        }
        ParamId id = 0;
        bool found = false;
        constexpr std::string_view idPrefix = "/neptune/id/";
        if (path.size() > idPrefix.size() && path.substr (0, idPrefix.size()) == idPrefix) found = parseUnsigned (path.substr (idPrefix.size()), id);
        else found = paramForAddress (path, id);
        if (found && control_.setParameter (id, value, real, set_)) ++applied;
    }

    // Adresses assignées explicitement (table des assignations) : en plus de l'accès direct.
    if (hasValue) applied += control_.oscValue (m.address, std::clamp (value, 0.0, 1.0), set_);
    return applied;
}

void Dispatcher::catalogue (std::string_view prefix, std::vector<std::vector<std::uint8_t>>& replies)
{
    std::vector<std::vector<std::uint8_t>> batch;
    std::size_t batchBytes = 0;
    int count = 0;
    const auto flush = [&]
    {
        if (batch.empty()) return;
        replies.push_back (encodeBundle (batch));
        batch.clear();
        batchBytes = 0;
    };
    for (const auto id : catalogIds())
    {
        const auto index = set_.indexOf (id);
        if (index == ParameterSet::npos) continue;           // adressable mais absent de ce plugin
        const auto& address = addressFor (id);
        if (! prefix.empty() && address.compare (0, prefix.size(), prefix) != 0) continue;
        const auto& spec = set_.spec (index);
        Message item;
        item.address = "/neptune/list/item";
        item.args = { { 's', 0.0, address }, { 'i', static_cast<double> (id), {} }, { 's', 0.0, spec.name }, { 's', 0.0, kindName (spec.kind) },
                      { 'f', spec.min, {} }, { 'f', spec.max, {} }, { 'f', spec.defaultValue, {} } };
        auto bytes = encode (item);
        if (batchBytes + bytes.size() + 4 > 1100) flush();   // un datagramme reste sous la taille d'un paquet Ethernet
        batchBytes += bytes.size() + 4;
        batch.push_back (std::move (bytes));
        ++count;
    }
    flush();
    Message done;
    done.address = "/neptune/list/done";
    done.args = { { 'i', static_cast<double> (count), {} } };
    replies.push_back (encode (done));
}
} // namespace neptune::core::osc
