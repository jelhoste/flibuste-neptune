#include "neptune/core/SfzImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <locale>
#include <map>
#include <set>
#include <sstream>

#include "neptune/core/Filter.h"
#include "neptune/core/OrganImport.h"
#include "neptune/core/SampleMeta.h"

namespace neptune::core::sfzimport
{
namespace fs = std::filesystem;

namespace
{
using Ops = std::map<std::string, std::string>;

std::string lowered (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

std::string trimmed (const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace (static_cast<unsigned char> (s[a]))) ++a;
    while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1]))) --b;
    return s.substr (a, b - a);
}

bool parseDouble (const std::string& text, double& out)
{
    std::istringstream in (trimmed (text));
    in.imbue (std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail() || ! std::isfinite (v)) return false;
    out = v;
    return true;
}

// Chemin relatif (séparateurs Windows ou Unix) retrouvé sur le disque, la casse des noms étant tolérée.
fs::path resolvePath (const fs::path& base, std::string relative)
{
    std::replace (relative.begin(), relative.end(), '\\', '/');
    const fs::path direct = base / fs::u8path (relative);
    std::error_code ec;
    if (fs::exists (direct, ec)) return direct;
    fs::path current = base;
    std::stringstream parts (relative);
    std::string part;
    while (std::getline (parts, part, '/'))
    {
        if (part.empty() || part == ".") continue;
        fs::path next = current / fs::u8path (part);
        if (! fs::exists (next, ec))
        {
            bool found = false;
            for (const auto& e : fs::directory_iterator (current, ec))
                if (lowered (e.path().filename().u8string()) == lowered (part)) { next = e.path(); found = true; break; }
            if (! found) return direct;
        }
        current = next;
    }
    return current;
}

// ---- Préprocesseur : commentaires, #define (substitution de $NOM), #include ----
class Preprocessor
{
public:
    explicit Preprocessor (std::vector<std::string>& warnings) : warnings_ (warnings) {}

    std::string run (std::string text, const fs::path& dir, int depth)
    {
        text = organimport::ensureUtf8 (std::move (text));
        // commentaires /* ... */
        for (std::size_t a = text.find ("/*"); a != std::string::npos; a = text.find ("/*"))
        {
            const auto b = text.find ("*/", a + 2);
            text.erase (a, b == std::string::npos ? std::string::npos : b + 2 - a);
        }
        std::string out;
        std::istringstream lines (text);
        std::string line;
        while (std::getline (lines, line))
        {
            if (const auto c = line.find ("//"); c != std::string::npos) line.erase (c);
            line = trimmed (line);
            if (line.empty()) continue;
            if (line.rfind ("#define", 0) == 0)
            {
                std::istringstream in (line.substr (7));
                std::string name;
                in >> name;
                std::string value;
                std::getline (in, value);
                if (! name.empty()) defines_[name] = trimmed (value);
                continue;
            }
            if (line.rfind ("#include", 0) == 0)
            {
                const auto q1 = line.find ('"');
                const auto q2 = q1 == std::string::npos ? q1 : line.find ('"', q1 + 1);
                if (q2 == std::string::npos || depth >= 8) { warnings_.push_back ("#include ignored: " + line); continue; }
                const auto file = resolvePath (dir, line.substr (q1 + 1, q2 - q1 - 1));
                std::ifstream in (file, std::ios::binary);
                if (! in) { warnings_.push_back ("include not found: " + line.substr (q1 + 1, q2 - q1 - 1)); continue; }
                std::stringstream body;
                body << in.rdbuf();
                out += run (body.str(), file.parent_path(), depth + 1);
                continue;
            }
            out += substituted (line);
            out += '\n';
        }
        return out;
    }

private:
    std::string substituted (std::string line) const
    {
        if (defines_.empty() || line.find ('$') == std::string::npos) return line;
        std::vector<const std::pair<const std::string, std::string>*> byLength;
        for (const auto& d : defines_) byLength.push_back (&d);
        std::sort (byLength.begin(), byLength.end(), [] (const auto* a, const auto* b) { return a->first.size() > b->first.size(); });
        for (const auto* d : byLength)
            for (std::size_t at = line.find (d->first); at != std::string::npos; at = line.find (d->first, at + d->second.size()))
                line.replace (at, d->first.size(), d->second);
        return line;
    }

    std::vector<std::string>& warnings_;
    std::map<std::string, std::string> defines_;
};

// ---- Découpage en en-têtes `<region>` et en opcodes `nom=valeur` (la valeur d'un `sample=` peut contenir des espaces) ----
struct Token { bool header = false; std::string name, value; };

bool opcodeStartsAt (const std::string& s, std::size_t at)
{
    std::size_t m = at;
    while (m < s.size() && (std::isalnum (static_cast<unsigned char> (s[m])) || s[m] == '_' || s[m] == '$')) ++m;
    return m > at && m < s.size() && s[m] == '=';
}

std::vector<Token> tokenize (const std::string& s)
{
    std::vector<Token> tokens;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n)
    {
        if (std::isspace (static_cast<unsigned char> (s[i]))) { ++i; continue; }
        if (s[i] == '<')
        {
            const auto j = s.find ('>', i);
            if (j == std::string::npos) break;
            tokens.push_back ({ true, lowered (trimmed (s.substr (i + 1, j - i - 1))), {} });
            i = j + 1;
            continue;
        }
        const std::size_t nameStart = i;
        while (i < n && ! std::isspace (static_cast<unsigned char> (s[i])) && s[i] != '=' && s[i] != '<') ++i;
        if (i >= n || s[i] != '=') { if (i == nameStart) ++i; continue; }   // mot isolé : ignoré
        const std::string name = lowered (s.substr (nameStart, i - nameStart));
        ++i;
        const std::size_t valueStart = i;
        std::size_t end = n;
        for (std::size_t j = i; j < n; ++j)
        {
            if (s[j] == '<') { end = j; break; }
            if (! std::isspace (static_cast<unsigned char> (s[j]))) continue;
            std::size_t k = j;
            while (k < n && std::isspace (static_cast<unsigned char> (s[k]))) ++k;
            if (k >= n || s[k] == '<' || opcodeStartsAt (s, k)) { end = j; break; }
            j = k - 1;
        }
        tokens.push_back ({ false, name, trimmed (s.substr (valueStart, end - valueStart)) });
        i = end;
    }
    return tokens;
}

std::string canonicalName (const std::string& n)
{
    static const std::map<std::string, std::string> aliases = {
        { "loopmode", "loop_mode" }, { "loopstart", "loop_start" }, { "loopend", "loop_end" }, { "offby", "off_by" },
        { "filtype", "fil_type" }, { "fil1_type", "fil_type" }, { "cutoff1", "cutoff" }, { "resonance1", "resonance" },
        { "fil1_keytrack", "fil_keytrack" }, { "fil1_veltrack", "fil_veltrack" }, { "pitch_keycenter", "pitch_keycenter" } };
    const auto it = aliases.find (n);
    return it == aliases.end() ? n : it->second;
}

const std::set<std::string>& handledOpcodes()
{
    static const std::set<std::string> handled = {
        "sample", "key", "lokey", "hikey", "pitch_keycenter", "lovel", "hivel", "tune", "transpose", "volume", "pan", "delay", "offset", "end",
        "loop_mode", "loop_start", "loop_end", "trigger", "group", "off_by", "seq_length", "seq_position", "lorand", "hirand",
        "ampeg_delay", "ampeg_attack", "ampeg_hold", "ampeg_decay", "ampeg_sustain", "ampeg_release", "amp_veltrack",
        "cutoff", "resonance", "fil_type", "fil_keytrack", "fil_veltrack", "group_label" };
    return handled;
}
} // namespace

int noteFromText (std::string_view text)
{
    std::string t = lowered (trimmed (std::string (text)));
    if (t.empty()) return -1;
    const bool numeric = std::all_of (t.begin(), t.end(), [] (char c) { return std::isdigit (static_cast<unsigned char> (c)) || c == '-'; });
    if (numeric)
    {
        double v = 0.0;
        return parseDouble (t, v) && v >= 0.0 && v <= 127.0 ? static_cast<int> (v) : -1;
    }
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    if (t[0] < 'a' || t[0] > 'g') return -1;
    int semitone = base[t[0] - 'a'];
    std::size_t i = 1;
    if (i < t.size() && (t[i] == '#' || t[i] == 'b' ) && i + 1 < t.size() && (std::isdigit (static_cast<unsigned char> (t[i + 1])) || t[i + 1] == '-'))
    {
        semitone += t[i] == '#' ? 1 : -1;
        ++i;
    }
    double octave = 0.0;
    if (i >= t.size() || ! parseDouble (t.substr (i), octave)) return -1;
    const int note = (static_cast<int> (octave) + 1) * 12 + semitone;
    return note >= 0 && note <= 127 ? note : -1;
}

Result importSfz (std::string_view sfzText, const Options& options)
{
    Result r;
    Preprocessor pre (r.warnings);
    const auto tokens = tokenize (pre.run (std::string (sfzText), options.baseDir, 0));

    // ---- héritage <control> < <global> < <master> < <group> < <region> ----
    Ops control, global, master, group, ignored;
    std::vector<Ops> regions;
    Ops* target = &ignored;
    std::set<std::string> skippedHeaders;
    auto overlay = [] (Ops& dst, const Ops& src) { for (const auto& kv : src) dst[kv.first] = kv.second; };
    for (const auto& t : tokens)
    {
        if (t.header)
        {
            if (t.name == "control") { control.clear(); target = &control; }
            else if (t.name == "global") { global.clear(); master.clear(); group.clear(); target = &global; }
            else if (t.name == "master") { master.clear(); group.clear(); target = &master; }
            else if (t.name == "group") { group.clear(); target = &group; }
            else if (t.name == "region")
            {
                Ops merged = global;
                overlay (merged, master);
                overlay (merged, group);
                regions.push_back (std::move (merged));
                target = &regions.back();
            }
            else { skippedHeaders.insert (t.name); ignored.clear(); target = &ignored; }
        }
        else (*target)[canonicalName (t.name)] = t.value;
    }
    r.regions = static_cast<int> (regions.size());
    if (regions.empty())
    {
        r.error = "No <region> in this SFZ file";
        return r;
    }
    if (! skippedHeaders.empty())
    {
        std::string names;
        for (const auto& h : skippedHeaders) names += (names.empty() ? "<" : ", <") + h + ">";
        r.warnings.push_back ("Not converted: " + names);
    }

    const std::string defaultPath = control.count ("default_path") ? control["default_path"] : std::string();
    int noteOffset = 0;
    {
        double v = 0.0;
        if (control.count ("octave_offset") && parseDouble (control["octave_offset"], v)) noteOffset += static_cast<int> (v) * 12;
        if (control.count ("note_offset") && parseDouble (control["note_offset"], v)) noteOffset += static_cast<int> (v);
    }

    std::map<std::string, int> unsupported;   // opcode -> nombre de régions où il apparaît
    int missingFiles = 0, generators = 0, badRanges = 0;
    std::vector<double> randomLow;            // lorand de chaque zone (hors zones à lorand : -1), pour numéroter les alternances au hasard
    std::vector<std::string> rangeKey;        // clef d'étendue de chaque zone
    for (const auto& ops : regions)
    {
        auto has = [&ops] (const char* k) { return ops.count (k) != 0; };
        auto num = [&ops] (const char* k, double def)
        {
            const auto it = ops.find (k);
            double v = 0.0;
            return it != ops.end() && parseDouble (it->second, v) ? v : def;
        };
        auto text = [&ops] (const char* k) { const auto it = ops.find (k); return it == ops.end() ? std::string() : lowered (it->second); };
        for (const auto& kv : ops)
        {
            if (handledOpcodes().count (kv.first)) continue;
            double v = 0.0;
            const bool numeric = parseDouble (kv.second, v);
            if (numeric && ((kv.first == "width" && v == 100.0) || (kv.first == "position" && v == 0.0))) continue;   // la valeur par défaut de SFZ : rien à convertir
            ++unsupported[kv.first];
        }

        const auto sampleName = has ("sample") ? ops.at ("sample") : std::string();
        if (sampleName.empty()) continue;
        if (sampleName[0] == '*') { ++generators; continue; }
        ZoneDescription z;
        const fs::path file = resolvePath (options.baseDir, defaultPath + sampleName);
        std::error_code ec;
        const bool exists = fs::exists (file, ec);
        if (options.readSampleFiles && ! exists)
        {
            if (++missingFiles <= 8) r.warnings.push_back ("sample not found: " + (defaultPath + sampleName));
            continue;
        }
        z.samplePath = file.u8string();

        SampleMeta meta;
        const bool haveMeta = options.readSampleFiles && exists && readFileMarks (file, meta);

        // notes
        auto noteOf = [&ops] (const char* k, int def) { const auto it = ops.find (k); if (it == ops.end()) return def; const int n = noteFromText (it->second); return n < 0 ? def : n; };
        int lo = noteOf ("lokey", 0), hi = noteOf ("hikey", 127), root = noteOf ("pitch_keycenter", haveMeta && meta.hasRoot ? meta.rootNote : 60);
        if (has ("key")) { lo = hi = root = noteOf ("key", 60); }
        lo = std::clamp (lo + noteOffset, 0, 127);
        hi = std::clamp (hi + noteOffset, 0, 127);
        root = std::clamp (root + noteOffset, 0, 127);
        if (lo > hi) { ++badRanges; continue; }
        z.keyLow = lo;
        z.keyHigh = hi;
        z.rootNote = root;
        z.velLow = std::clamp (static_cast<int> (std::lround (num ("lovel", 0))), 1, 127);
        z.velHigh = std::clamp (static_cast<int> (std::lround (num ("hivel", 127))), z.velLow, 127);

        // hauteur, volume, placement
        z.tuneCents = num ("tune", 0.0) + 100.0 * num ("transpose", 0.0);
        z.gainDb = num ("volume", 0.0);
        z.pan = std::clamp (num ("pan", 0.0) / 100.0, -1.0, 1.0);
        z.delayMs = std::clamp (num ("delay", 0.0) * 1000.0, 0.0, ZoneDescription::maxDelayMs);
        z.startFrame = std::max<std::int64_t> (0, static_cast<std::int64_t> (num ("offset", 0.0)));
        if (const double e = num ("end", -1.0); e >= 0.0) z.endFrame = static_cast<std::int64_t> (e) + 1;   // `end` de SFZ est inclusif
        if (has ("amp_veltrack")) z.velTrackPct = std::clamp (num ("amp_veltrack", 100.0), -100.0, 100.0);

        // déclenchement
        const auto trig = text ("trigger");
        if (trig == "release" || trig == "release_key") z.onRelease = true;
        else if (trig == "first") z.trigger = TriggerCondition::First;
        else if (trig == "legato") z.trigger = TriggerCondition::Legato;

        // boucle : le mode demandé, sinon celle que le fichier porte. La fin de boucle de SFZ est inclusive, celle de Neptune exclusive.
        const auto loopMode = text ("loop_mode");
        const bool explicitLoop = has ("loop_start") || has ("loop_end");
        const bool fileLoop = haveMeta && ! meta.loops.empty();
        const bool wantLoop = loopMode == "loop_continuous" || loopMode == "loop_sustain" || (loopMode.empty() && (explicitLoop || fileLoop));
        if (wantLoop && ! z.onRelease)
        {
            z.mode = PlayMode::Loop;
            z.loopStart = has ("loop_start") ? std::max<std::int64_t> (0, static_cast<std::int64_t> (num ("loop_start", 0.0))) : (fileLoop ? meta.loops[0].start : 0);
            if (has ("loop_end")) z.loopEnd = static_cast<std::int64_t> (num ("loop_end", -1.0)) + 1;
            else if (fileLoop) z.loopEnd = meta.loops[0].end;
            if (! explicitLoop && fileLoop) z.loopCrossfade = meta.loops[0].crossfade;
        }
        else if (loopMode != "one_shot" && ! z.onRelease) z.releaseCuts = true;   // no_loop (le défaut) : joué en entier, mais la touche relâchée l'interrompt

        // enveloppe d'amplitude (secondes -> ms ; le relâchement garde ses 50 ms d'origine tant qu'il n'est pas donné)
        if (has ("ampeg_delay") || has ("ampeg_attack") || has ("ampeg_hold") || has ("ampeg_decay") || has ("ampeg_sustain") || has ("ampeg_release"))
        {
            z.envelopeOverride = true;
            auto ms = [&num] (const char* k, double def) { return std::clamp (num (k, def) * 1000.0, 0.0, envelopeMaxMs); };
            z.envelope.delay.ms = ms ("ampeg_delay", 0.0);
            z.envelope.attack.ms = ms ("ampeg_attack", 0.0);
            z.envelope.hold.ms = ms ("ampeg_hold", 0.0);
            z.envelope.decay.ms = ms ("ampeg_decay", 0.0);
            z.envelope.sustain = std::clamp (num ("ampeg_sustain", 100.0) / 100.0, 0.0, 1.0);
            if (has ("ampeg_release")) z.envelope.release.ms = ms ("ampeg_release", 0.05);
        }

        // filtre (un seul : le premier de SFZ)
        if (has ("cutoff"))
        {
            z.filterOverride = true;
            auto& f = z.filters.filter[0];
            f.on = true;
            const auto type = text ("fil_type").empty() ? std::string ("lpf_2p") : text ("fil_type");
            f.mode = type.rfind ("hpf", 0) == 0 ? FilterMode::HighPass : type.rfind ("bpf", 0) == 0 ? FilterMode::BandPass
                   : type.rfind ("brf", 0) == 0 ? FilterMode::Notch : FilterMode::LowPass;
            f.slopeDb = (type.find ("4p") != std::string::npos || type.find ("6p") != std::string::npos) ? 24 : 12;
            f.cutoffHz = std::clamp (num ("cutoff", 20000.0), filterMinHz, filterMaxHz);
            f.resonance = std::clamp (num ("resonance", 0.0) / 40.0, 0.0, 1.0);
            f.keyTrack = std::clamp (num ("fil_keytrack", 0.0) / 100.0, -1.0, 2.0);
            f.velocityOctaves = std::clamp (num ("fil_veltrack", 0.0) / 2400.0, -6.0, 6.0);
        }

        // coupure (hi-hats...), alternances
        z.chokeCuts = std::max (0, static_cast<int> (num ("group", 0.0)));
        z.chokeCutBy = std::max (0, static_cast<int> (num ("off_by", 0.0)));
        if (has ("group_label")) z.group = ops.at ("group_label");
        if (has ("seq_length") || has ("seq_position"))
        {
            z.roundRobin = std::max (1, static_cast<int> (num ("seq_position", 1.0)));
            z.rrMode = 0;
        }
        randomLow.push_back (has ("lorand") || has ("hirand") ? num ("lorand", 0.0) : -1.0);
        rangeKey.push_back (std::to_string (z.keyLow) + "-" + std::to_string (z.keyHigh) + "/" + std::to_string (z.velLow) + "-" + std::to_string (z.velHigh)
                            + "/" + (z.onRelease ? "r" : "a") + std::to_string (static_cast<int> (z.trigger)));
        r.instrument.zones.push_back (std::move (z));
    }

    // alternances au hasard (lorand / hirand) : les zones d'une même étendue, numérotées dans l'ordre de leur lorand
    {
        std::map<std::string, std::vector<std::size_t>> sets;
        for (std::size_t i = 0; i < randomLow.size(); ++i)
            if (randomLow[i] >= 0.0) sets[rangeKey[i]].push_back (i);
        for (auto& kv : sets)
        {
            auto& ids = kv.second;
            if (ids.size() < 2) continue;
            std::stable_sort (ids.begin(), ids.end(), [&randomLow] (std::size_t a, std::size_t b) { return randomLow[a] < randomLow[b]; });
            for (std::size_t k = 0; k < ids.size(); ++k)
            {
                r.instrument.zones[ids[k]].roundRobin = static_cast<int> (k) + 1;
                r.instrument.zones[ids[k]].rrMode = 1;
            }
        }
    }

    r.imported = static_cast<int> (r.instrument.zones.size());
    if (missingFiles > 8) r.warnings.push_back (std::to_string (missingFiles - 8) + " more samples not found");
    if (missingFiles > 0) r.warnings.push_back (std::to_string (missingFiles) + " region(s) skipped: sample file missing");
    if (generators > 0) r.warnings.push_back (std::to_string (generators) + " region(s) skipped: built-in oscillators (*sine, *saw...) are not converted");
    if (badRanges > 0) r.warnings.push_back (std::to_string (badRanges) + " region(s) skipped: lokey above hikey");
    if (! unsupported.empty())
    {
        std::vector<std::pair<std::string, int>> sorted (unsupported.begin(), unsupported.end());
        std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        std::string names;
        for (std::size_t i = 0; i < sorted.size() && i < 16; ++i) names += (i ? ", " : "") + sorted[i].first;
        if (sorted.size() > 16) names += ", ...";
        r.warnings.push_back ("Opcodes not converted: " + names);
    }
    if (r.imported == 0)
    {
        r.error = "No region could be imported";
        return r;
    }
    r.ok = true;
    return r;
}

Result importSfzFile (const fs::path& sfzFile, Options options)
{
    std::ifstream in (sfzFile, std::ios::binary);
    if (! in)
    {
        Result r;
        r.error = "Cannot read " + sfzFile.u8string();
        return r;
    }
    std::stringstream body;
    body << in.rdbuf();
    if (options.baseDir.empty()) options.baseDir = sfzFile.parent_path();
    auto r = importSfz (body.str(), options);
    r.name = sfzFile.stem().u8string();
    return r;
}
} // namespace neptune::core::sfzimport
