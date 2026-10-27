#include "neptune/core/OrganImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include "neptune/core/Mixer.h"
#include "neptune/core/SampleMeta.h"

namespace neptune::core::organimport
{
namespace fs = std::filesystem;

namespace
{
std::string lowered (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

std::string trimmed (std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace (static_cast<unsigned char> (s[a]))) ++a;
    while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1]))) --b;
    return std::string (s.substr (a, b - a));
}

bool validUtf8 (const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        const auto c = static_cast<unsigned char> (s[i]);
        std::size_t extra = 0;
        if (c < 0x80) extra = 0;
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        for (std::size_t k = 1; k <= extra; ++k)
            if (i + k >= s.size() || (static_cast<unsigned char> (s[i + k]) & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}
} // namespace

std::string ensureUtf8 (std::string text)
{
    if (validUtf8 (text)) return text;
    std::string out;
    out.reserve (text.size() + text.size() / 8);
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c < 0x80) out.push_back (ch);
        else { out.push_back (static_cast<char> (0xC0 | (c >> 6))); out.push_back (static_cast<char> (0x80 | (c & 0x3F))); }
    }
    return out;
}

std::string numbered (std::string_view prefix, int number)
{
    char digits[16];
    std::snprintf (digits, sizeof digits, "%03d", number);
    return std::string (prefix) + digits;
}

IniFile IniFile::parse (std::string_view textIn)
{
    std::string text = ensureUtf8 (std::string (textIn));
    if (text.size() >= 3 && static_cast<unsigned char> (text[0]) == 0xEF && static_cast<unsigned char> (text[1]) == 0xBB && static_cast<unsigned char> (text[2]) == 0xBF) text.erase (0, 3);
    IniFile ini;
    std::string section;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        const auto eol = text.find ('\n', pos);
        const std::string line = trimmed (std::string_view (text).substr (pos, (eol == std::string::npos ? text.size() : eol) - pos));
        pos = eol == std::string::npos ? text.size() + 1 : eol + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[')
        {
            const auto close = line.find (']');
            if (close == std::string::npos) continue;
            section = trimmed (std::string_view (line).substr (1, close - 1));
            const std::string key = lowered (section);
            if (ini.data_.find (key) == ini.data_.end()) { ini.order_.push_back (section); ini.data_[key]; }
            continue;
        }
        const auto eq = line.find ('=');
        if (eq == std::string::npos || section.empty()) continue;
        std::string value = line.substr (eq + 1);
        for (std::size_t i = 1; i < value.size(); ++i)   // commentaire de fin de ligne : un « ; » précédé d'un espace
            if (value[i] == ';' && std::isspace (static_cast<unsigned char> (value[i - 1]))) { value.resize (i); break; }
        ini.data_[lowered (section)][lowered (trimmed (std::string_view (line).substr (0, eq)))] = trimmed (value);
    }
    return ini;
}

bool IniFile::hasSection (std::string_view section) const { return data_.find (lowered (std::string (section))) != data_.end(); }

bool IniFile::has (std::string_view section, std::string_view key) const
{
    const auto s = data_.find (lowered (std::string (section)));
    return s != data_.end() && s->second.find (lowered (std::string (key))) != s->second.end();
}

std::string IniFile::get (std::string_view section, std::string_view key, const std::string& def) const
{
    const auto s = data_.find (lowered (std::string (section)));
    if (s == data_.end()) return def;
    const auto k = s->second.find (lowered (std::string (key)));
    return k == s->second.end() ? def : k->second;
}

int IniFile::getInt (std::string_view section, std::string_view key, int def) const
{
    const std::string v = get (section, key);
    if (v.empty()) return def;
    char* end = nullptr;
    const long n = std::strtol (v.c_str(), &end, 10);
    if (end == v.c_str()) return def;
    return static_cast<int> (std::clamp<long> (n, -1000000000L, 1000000000L));
}

double IniFile::getDouble (std::string_view section, std::string_view key, double def) const
{
    std::string v = get (section, key);
    if (v.empty()) return def;
    std::replace (v.begin(), v.end(), ',', '.');
    char* end = nullptr;
    const double d = std::strtod (v.c_str(), &end);
    return end == v.c_str() || ! std::isfinite (d) ? def : d;
}

bool IniFile::getBool (std::string_view section, std::string_view key, bool def) const
{
    const std::string v = get (section, key);
    if (v.empty()) return def;
    switch (std::tolower (static_cast<unsigned char> (v[0])))
    {
        case 'y': case '1': case 't': return true;
        case 'n': case '0': case 'f': return false;
        default: return def;
    }
}

namespace
{
// Fichier d'échantillon : chemin du fichier d'orgue (séparateurs Windows) retrouvé sur le disque, la casse des noms étant tolérée.
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

struct FileMarks
{
    bool exists = false;
    std::vector<MetaLoop> loops;
    std::int64_t releaseMarker = -1;
};

// Mêmes noms que les clefs du fichier d'orgue : « sw007 » (interrupteur 7), « st101 » (le jeu 101 est son propre interrupteur), « cp003 » (idem pour un accouplement).
std::string switchKey (int number) { return "sw" + std::to_string (number); }

int combineOf (const std::string& fn, bool& known)
{
    const std::string f = lowered (fn);
    known = true;
    if (f.empty() || f == "and") return static_cast<int> (router::Combine::All);
    if (f == "or") return static_cast<int> (router::Combine::Any);
    if (f == "xor") return static_cast<int> (router::Combine::One);
    if (f == "nand") return static_cast<int> (router::Combine::NotAll);
    if (f == "nor") return static_cast<int> (router::Combine::None);
    known = false;
    return static_cast<int> (router::Combine::All);
}

class Builder
{
public:
    Builder (const IniFile& ini, const Options& options) : ini_ (ini), opt_ (options) {}

    Result run()
    {
        Result& r = result_;
        if (! ini_.hasSection ("Organ")) { r.error = "not an organ definition file (no [Organ] section)"; return r; }
        r.name = ini_.get ("Organ", "ChurchName", "Organ");
        organAmp_ = ini_.getInt ("Organ", "AmplitudeLevel", 100);
        organTuning_ = ini_.getDouble ("Organ", "PitchTuning", 0.0);
        organGain_ = ini_.getDouble ("Organ", "Gain", 0.0);
        const int manuals = ini_.getInt ("Organ", "NumberOfManuals", 0);
        const bool pedals = ini_.getBool ("Organ", "HasPedals", false);
        std::vector<int> indices;
        if (pedals) indices.push_back (0);
        for (int m = 1; m <= manuals; ++m) indices.push_back (m);
        if (indices.empty()) { r.error = "the organ has no keyboard"; return r; }
        // canaux MIDI : les manuels à la suite, puis le pédalier
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            const int m = indices[i];
            channelOf_[m] = std::clamp ((m == 0 ? opt_.firstChannel + manuals : opt_.firstChannel + m - 1), 1, 16);
        }
        const std::size_t room = static_cast<std::size_t> (std::max (0, mixer::numTracks - opt_.firstTrack));
        for (std::size_t i = 0; i < indices.size() && i < room; ++i) buildKeyboard (indices[i], i);
        if (indices.size() > room) warn ("the organ has more keyboards than Neptune has tracks: the extra ones are not imported");
        buildCouplers();
        buildCombinations();
        otherThings();
        for (const auto& [key, ids] : params_)
        {
            const bool on = initial_.count (key) != 0 && initial_.at (key);
            for (const ParamId id : ids) r.initialValues.emplace_back (id, valueFor (id, on));
        }
        r.ok = true;
        return r;
    }

private:
    struct ManualInfo { int firstMidi = 36, accessible = 61, firstLogical = 1; std::size_t keyboard = 0; };

    void warn (std::string text) { if (result_.warnings.size() < 200) result_.warnings.push_back (std::move (text)); }

    static double valueFor (ParamId id, bool on)
    {
        const bool tirant = id >= router::switchBase && id < router::switchBase + static_cast<ParamId> (router::kSwitches);
        return on ? (tirant ? 1.0 : 100.0) : 0.0;
    }

    std::string switchName (const std::string& key, const std::string& fallback)
    {
        if (key.rfind ("sw", 0) == 0)
        {
            const std::string name = ini_.get (numbered ("Switch", std::atoi (key.c_str() + 2)), "Name");
            if (! name.empty()) return name;
        }
        return fallback;
    }

    // La clef d'un interrupteur et son état initial.
    void noteInitial (const std::string& key, bool engaged) { initial_[key] = engaged; }

    int modulatorFor (std::size_t keyboard, const std::string& key, const std::string& name)
    {
        auto& table = modulators_[keyboard];
        const auto it = table.find (key);
        if (it != table.end()) return it->second;
        const int index = static_cast<int> (table.size());
        if (index >= kModulators) return -1;
        table[key] = index;
        result_.keyboards[keyboard].instrument.modulators[static_cast<std::size_t> (index)].name = name.substr (0, 40);
        params_[key].push_back (mixer::modulatorParam (opt_.firstTrack + static_cast<int> (keyboard), index));
        return index;
    }

    // Le paramètre qui représente un interrupteur dans une condition du routeur : le premier modulateur qui le porte, sinon un tirant du routeur.
    ParamId paramFor (const std::string& key)
    {
        const auto it = params_.find (key);
        if (it != params_.end() && ! it->second.empty()) return it->second.front();
        if (routerSwitches_ >= router::kSwitches) return 0;
        const ParamId id = router::switchParam (routerSwitches_++);
        params_[key].push_back (id);
        return id;
    }

    const FileMarks& marksOf (const fs::path& path)
    {
        const std::string key = path.u8string();
        const auto it = marks_.find (key);
        if (it != marks_.end()) return it->second;
        FileMarks m;
        std::error_code ec;
        m.exists = fs::exists (path, ec);
        if (! m.exists) { if (opt_.readSampleMarks) warn ("sample not found: " + key); }
        else if (opt_.readSampleMarks)
        {
            SampleMeta meta;
            if (readFileMarks (path, meta)) m.loops = meta.loops;
            std::int64_t cue = -1;
            if (readReleaseMarker (path, cue)) m.releaseMarker = cue;
        }
        return marks_.emplace (key, std::move (m)).first->second;
    }

    // « REF:manuel:jeu:tuyau » : le tuyau est celui d'un autre jeu (le jeu = sa place dans le clavier, 1..). Il garde les réglages du tuyau visé (fichier, accord, volume).
    bool followReference (std::string& section, int& pipe, double& ampFactor, double& tuning, double& gainDb)
    {
        for (int depth = 0; depth < 6; ++depth)
        {
            const std::string value = ini_.get (section, numbered ("Pipe", pipe));
            if (value.rfind ("REF:", 0) != 0) return true;
            int manual = -1, stop = 0, target = 0;
            if (std::sscanf (value.c_str(), "REF:%d:%d:%d", &manual, &stop, &target) != 3) return false;
            const std::string manualSection = numbered ("Manual", manual);
            const int stopNumber = ini_.getInt (manualSection, numbered ("Stop", stop), 0);
            const std::string stopSection = numbered ("Stop", stopNumber);
            if (stopNumber <= 0 || ! ini_.hasSection (stopSection)) return false;
            ampFactor = organAmp_ / 100.0 * ini_.getInt (stopSection, "AmplitudeLevel", 100) / 100.0;
            tuning = organTuning_ + ini_.getDouble (stopSection, "PitchTuning", 0.0);
            gainDb = organGain_ + ini_.getDouble (stopSection, "Gain", 0.0);
            section = stopSection;
            pipe = target;
            if (ini_.getInt (stopSection, "NumberOfRanks", 0) > 0)   // un jeu à rangs : le tuyau est celui de son premier rang
            {
                const std::string rankSection = numbered ("Rank", ini_.getInt (stopSection, numbered ("Rank", 1), 0));
                if (! ini_.hasSection (rankSection)) return false;
                ampFactor *= ini_.getInt (rankSection, "AmplitudeLevel", 100) / 100.0;
                tuning += ini_.getDouble (rankSection, "PitchTuning", 0.0);
                gainDb += ini_.getDouble (rankSection, "Gain", 0.0);
                section = rankSection;
            }
        }
        return false;
    }

    void addPipe (Keyboard& kb, const ManualInfo& info, std::string section, int pipe, int logicalKey, const std::string& group, double ampFactor, double tuning, double gainDb)
    {
        if (ini_.get (section, numbered ("Pipe", pipe)).rfind ("REF:", 0) == 0 && ! followReference (section, pipe, ampFactor, tuning, gainDb))
        {
            warn ("keyboard \"" + kb.name + "\": a pipe reference of group \"" + group + "\" cannot be resolved");
            return;
        }
        const std::string p = numbered ("Pipe", pipe);
        const std::string file = ini_.get (section, p);
        if (file.empty() || file.rfind ("REF:", 0) == 0 || lowered (file) == "dummy") return;
        if (logicalKey < info.firstLogical || logicalKey >= info.firstLogical + info.accessible) return;   // une touche que le clavier n'a pas
        const int note = info.firstMidi + (logicalKey - info.firstLogical);
        if (note < 0 || note > 127) return;
        const double amp = ampFactor * ini_.getInt (section, p + "AmplitudeLevel", 100) / 100.0;
        ZoneDescription z;
        const fs::path path = resolvePath (opt_.baseDir, file);
        z.samplePath = path.u8string();
        z.keyLow = z.keyHigh = note;
        z.rootNote = note;   // l'échantillon joue à sa hauteur d'origine (comme l'accord « original » de GrandOrgue) ; l'accord du tuyau s'y ajoute
        z.tuneCents = std::clamp (tuning + ini_.getDouble (section, p + "PitchTuning", 0.0), -1200.0, 1200.0);
        z.gainDb = std::clamp (20.0 * std::log10 (std::max (amp, 1.0e-6)) + gainDb + ini_.getDouble (section, p + "Gain", 0.0), -60.0, 60.0);
        z.group = group;
        const auto& marks = marksOf (path);
        if (! opt_.readSampleMarks || (marks.exists && ! marks.loops.empty()))   // sans lecture des fichiers : un tuyau d'orgue se tient (boucle)
        {
            z.mode = PlayMode::Loop;
            if (! marks.loops.empty())
            {
                z.loopStart = marks.loops[0].start;
                z.loopEnd = marks.loops[0].end;
                for (std::size_t i = 1; i < marks.loops.size() && z.extraLoops.size() < ZoneDescription::maxExtraLoops; ++i)
                    z.extraLoops.push_back ({ marks.loops[i].start, marks.loops[i].end, marks.loops[i].crossfade, marks.loops[i].repeats });
                const std::int64_t lastEnd = marks.loops.back().end;
                if (marks.releaseMarker > 0 && lastEnd >= 0 && marks.releaseMarker >= lastEnd) z.releaseStart = marks.releaseMarker;
            }
        }
        kb.instrument.zones.push_back (std::move (z));
        ++kb.pipes;
    }

    void buildStop (Keyboard& kb, std::size_t keyboard, const ManualInfo& info, int stopNumber)
    {
        const std::string section = numbered ("Stop", stopNumber);
        if (! ini_.hasSection (section)) { warn ("stop " + std::to_string (stopNumber) + " is listed but missing"); return; }
        const std::string name = ini_.get (section, "Name", section);
        std::vector<std::string> keys;
        const int count = ini_.getInt (section, "SwitchCount", 0);
        for (int j = 1; j <= count; ++j)
            if (const int sw = ini_.getInt (section, numbered ("Switch", j), 0); sw > 0) keys.push_back (switchKey (sw));
        const std::string own = "st" + std::to_string (stopNumber);
        if (keys.empty())
        {
            keys.push_back (own);   // un jeu sans interrupteur est son propre tirant
            noteInitial (own, ini_.getBool (section, "DefaultToEngaged", false));
        }
        bool known = true;
        combineOf (ini_.get (section, "Function"), known);
        if (! known || (count > 1 && lowered (ini_.get (section, "Function")) != "and" && ! ini_.get (section, "Function").empty()))
            warn ("stop \"" + name + "\": the switch function \"" + ini_.get (section, "Function") + "\" is treated as AND");
        ModMask mask = 0;
        for (const auto& key : keys)
        {
            if (key.rfind ("sw", 0) == 0) noteInitial (key, ini_.getBool (numbered ("Switch", std::atoi (key.c_str() + 2)), "DefaultToEngaged", false));
            const int index = modulatorFor (keyboard, key, switchName (key, name));
            if (index < 0) { warn ("keyboard \"" + kb.name + "\": more than " + std::to_string (kModulators) + " switches, stop \"" + name + "\" not imported"); return; }
            mask |= static_cast<ModMask> (1u) << index;
        }
        std::string group = name;
        for (int n = 2; usedGroups_[keyboard].count (group) != 0; ++n) group = name + " (" + std::to_string (n) + ")";
        usedGroups_[keyboard].insert (group);
        const std::size_t zonesBefore = kb.instrument.zones.size();
        const double stopAmp = ini_.getInt (section, "AmplitudeLevel", 100) / 100.0;
        const double stopTuning = ini_.getDouble (section, "PitchTuning", 0.0);
        const double stopGain = ini_.getDouble (section, "Gain", 0.0);
        const double base = organAmp_ / 100.0 * stopAmp;
        const int nRanks = ini_.getInt (section, "NumberOfRanks", 0);
        const int accessible = ini_.getInt (section, "NumberOfAccessiblePipes", ini_.getInt (section, "NumberOfLogicalPipes", 0));
        const int firstKey = ini_.getInt (section, "FirstAccessiblePipeLogicalKeyNumber", 1);
        bool attackWarned = false;
        auto attackCheck = [&] (const std::string& sec, int pipe)
        {
            if (! attackWarned && ini_.getInt (sec, numbered ("Pipe", pipe) + "AttackCount", 1) > 1)
            {
                attackWarned = true;
                warn ("stop \"" + name + "\": attack variants of the pipes are not imported (only the main sample)");
            }
        };
        if (nRanks > 0)
        {
            for (int r = 1; r <= nRanks; ++r)
            {
                const int rankNumber = ini_.getInt (section, numbered ("Rank", r), 0);
                const std::string rankSection = numbered ("Rank", rankNumber);
                if (! ini_.hasSection (rankSection)) { warn ("stop \"" + name + "\": rank " + std::to_string (rankNumber) + " is missing"); continue; }
                const std::string prefix = numbered ("Rank", r);
                const int firstPipe = ini_.getInt (section, prefix + "FirstPipeNumber", 1);
                const int rankPipes = ini_.getInt (rankSection, "NumberOfLogicalPipes", 0);
                const int pipeCount = ini_.getInt (section, prefix + "PipeCount", std::min (accessible, rankPipes - firstPipe + 1));
                const int rankFirstKey = ini_.getInt (section, prefix + "FirstAccessibleKeyNumber", firstKey);
                const double rankAmp = ini_.getInt (rankSection, "AmplitudeLevel", 100) / 100.0;
                const double tuning = organTuning_ + stopTuning + ini_.getDouble (rankSection, "PitchTuning", 0.0);
                const double gain = organGain_ + stopGain + ini_.getDouble (rankSection, "Gain", 0.0);
                for (int j = 0; j < pipeCount; ++j)
                {
                    attackCheck (rankSection, firstPipe + j);
                    addPipe (kb, info, rankSection, firstPipe + j, rankFirstKey + j, group, base * rankAmp, tuning, gain);
                }
            }
        }
        else
        {
            const int firstPipe = ini_.getInt (section, "FirstAccessiblePipeLogicalPipeNumber", 1);
            for (int j = 0; j < accessible; ++j)
            {
                attackCheck (section, firstPipe + j);
                addPipe (kb, info, section, firstPipe + j, firstKey + j, group, base, organTuning_ + stopTuning, organGain_ + stopGain);
            }
        }
        if (kb.instrument.zones.size() == zonesBefore) { warn ("stop \"" + name + "\" has no playable pipe"); return; }
        kb.instrument.groups[group].modulators = mask;
        ++kb.stops;
    }

    void buildKeyboard (int odfIndex, std::size_t keyboard)
    {
        const std::string section = numbered ("Manual", odfIndex);
        Keyboard kb;
        kb.odfIndex = odfIndex;
        kb.midiChannel = channelOf_[odfIndex];
        kb.name = ini_.get (section, "Name", odfIndex == 0 ? "Pedal" : "Manual " + std::to_string (odfIndex));
        kb.instrument.maxVoices = InstrumentDescription::maxVoicesLimit;
        kb.instrument.streamFromDisk = false;
        ManualInfo info;
        info.keyboard = keyboard;
        info.firstMidi = ini_.getInt (section, "FirstAccessibleKeyMIDINoteNumber", 36);
        info.firstLogical = ini_.getInt (section, "FirstAccessibleKeyLogicalKeyNumber", 1);
        const int logical = ini_.getInt (section, "NumberOfLogicalKeys", 61);
        info.accessible = ini_.getInt (section, "NumberOfAccessibleKeys", logical);
        manuals_[odfIndex] = info;
        if (! ini_.hasSection (section)) warn ("keyboard " + std::to_string (odfIndex) + " is missing from the file");
        result_.keyboards.push_back (std::move (kb));
        modulators_.emplace_back();
        usedGroups_.emplace_back();
        Keyboard& placed = result_.keyboards.back();
        const int stops = ini_.getInt (section, "NumberOfStops", 0);
        for (int i = 1; i <= stops; ++i)
            if (const int number = ini_.getInt (section, numbered ("Stop", i), 0); number > 0) buildStop (placed, keyboard, info, number);
    }

    void buildCouplers()
    {
        for (auto& kb : result_.keyboards)
        {
            const std::string section = numbered ("Manual", kb.odfIndex);
            const auto& info = manuals_[kb.odfIndex];
            const int count = ini_.getInt (section, "NumberOfCouplers", 0);
            for (int i = 1; i <= count; ++i)
            {
                const int number = ini_.getInt (section, numbered ("Coupler", i), 0);
                const std::string cs = numbered ("Coupler", number);
                if (number <= 0 || ! ini_.hasSection (cs)) continue;
                const std::string name = ini_.get (cs, "Name", cs);
                const int dest = ini_.getInt (cs, "DestinationManual", kb.odfIndex);
                const auto channel = channelOf_.find (dest);
                if (channel == channelOf_.end()) { warn ("coupler \"" + name + "\": destination keyboard " + std::to_string (dest) + " does not exist"); continue; }
                if (result_.router.rules.size() >= static_cast<std::size_t> (router::kMaxRules)) { warn ("too many couplers: \"" + name + "\" and the following ones are not imported"); break; }
                router::Rule rule;
                rule.name = name;
                rule.srcChannel = kb.midiChannel;
                rule.dstChannel = channel->second;
                rule.keyLo = std::clamp (info.firstMidi, 0, 127);
                rule.keyHi = std::clamp (info.firstMidi + info.accessible - 1, 0, 127);
                rule.transpose = std::clamp (ini_.getInt (cs, "DestinationKeyshift", 0), -48, 48);
                const std::string type = lowered (ini_.get (cs, "CouplerType", "normal"));
                rule.select = type == "bass" ? router::Select::Lowest : type == "melody" ? router::Select::Highest : router::Select::All;
                bool known = true;
                rule.combine = static_cast<router::Combine> (combineOf (ini_.get (cs, "Function"), known));
                if (! known) warn ("coupler \"" + name + "\": the switch function \"" + ini_.get (cs, "Function") + "\" is treated as AND");
                std::vector<std::string> keys;
                const int switches = ini_.getInt (cs, "SwitchCount", 0);
                for (int j = 1; j <= switches; ++j)
                    if (const int sw = ini_.getInt (cs, numbered ("Switch", j), 0); sw > 0) keys.push_back (switchKey (sw));
                if (keys.empty())
                {
                    keys.push_back ("cp" + std::to_string (number));
                    noteInitial (keys.back(), ini_.getBool (cs, "DefaultToEngaged", false));
                }
                for (const auto& key : keys)
                {
                    if (key.rfind ("sw", 0) == 0) noteInitial (key, ini_.getBool (numbered ("Switch", std::atoi (key.c_str() + 2)), "DefaultToEngaged", false));
                    if (rule.terms.size() >= static_cast<std::size_t> (router::kMaxTerms)) { warn ("coupler \"" + name + "\": more than " + std::to_string (router::kMaxTerms) + " switches, the extra ones are ignored"); break; }
                    if (const ParamId id = paramFor (key); id != 0) rule.terms.push_back ({ id, false });
                    else warn ("coupler \"" + name + "\": no free router switch left for one of its switches");
                }
                result_.router.rules.push_back (std::move (rule));
            }
        }
    }

    // Mémoire de combinaison : tout interrupteur de `scope` (ou tous) y est tiré s'il est cité en positif, repoussé sinon.
    combi::Memory memoryOf (const std::string& name, const std::set<std::string>& on, const std::vector<std::string>* onlyKeys)
    {
        combi::Memory m;
        m.used = true;
        m.name = name.substr (0, combi::kMaxName);
        for (const auto& [key, ids] : params_)
        {
            if (onlyKeys != nullptr && std::find (onlyKeys->begin(), onlyKeys->end(), key) == onlyKeys->end()) continue;
            for (const ParamId id : ids)
                if (m.values.size() < combi::kMaxValues) m.values.emplace_back (id, valueFor (id, on.count (key) != 0));
        }
        return m;
    }

    void buildCombinations()
    {
        // généraux : tout l'état de la console
        const int generals = ini_.getInt ("Organ", "NumberOfGenerals", 0);
        for (int g = 1; g <= generals; ++g)
        {
            const std::string section = numbered ("General", g);
            if (! ini_.hasSection (section)) continue;
            const int index = g - 1;
            if (index >= combi::kBanks * combi::kPistons) { warn ("more than " + std::to_string (combi::kBanks * combi::kPistons) + " generals: the extra ones are not imported"); break; }
            std::set<std::string> on;
            const int switches = ini_.getInt (section, "NumberOfSwitches", 0);
            for (int i = 1; i <= switches; ++i)
                if (const int n = ini_.getInt (section, numbered ("SwitchNumber", i), 0); n > 0) on.insert (switchKey (n));
            const int stops = ini_.getInt (section, "NumberOfStops", 0);
            for (int i = 1; i <= stops; ++i)
                if (const int n = ini_.getInt (section, numbered ("StopNumber", i), 0); n > 0) on.insert ("st" + std::to_string (n));
            result_.combinations.generals[combi::generalKey (index / combi::kPistons, index % combi::kPistons)] = memoryOf (ini_.get (section, "Name", std::to_string (g)), on, nullptr);
        }
        // divisionnels : seulement les interrupteurs du clavier
        for (std::size_t k = 0; k < result_.keyboards.size() && opt_.firstTrack + static_cast<int> (k) < combi::kTracks; ++k)
        {
            const auto& kb = result_.keyboards[k];
            const std::string manual = numbered ("Manual", kb.odfIndex);
            const int count = ini_.getInt (manual, "NumberOfDivisionals", 0);
            std::vector<std::string> own;
            for (const auto& [key, index] : modulators_[k]) own.push_back (key);
            for (int d = 1; d <= count; ++d)
            {
                const std::string section = manual + numbered ("Divisional", d);
                if (! ini_.hasSection (section)) continue;
                const int index = d - 1;
                if (index >= combi::kBanks * combi::kPistons) break;
                std::set<std::string> on;
                const int switches = ini_.getInt (section, "NumberOfSwitches", 0);
                for (int i = 1; i <= switches; ++i)
                    if (const int n = ini_.getInt (section, numbered ("Switch", i), 0); n > 0) on.insert (switchKey (n));
                const int stops = ini_.getInt (section, "NumberOfStops", 0);
                for (int i = 1; i <= stops; ++i)
                    if (const int n = ini_.getInt (section, numbered ("Stop", i), 0); n > 0) on.insert ("st" + std::to_string (n));
                auto memory = memoryOf (ini_.get (section, "Name", std::to_string (d)), on, &own);
                // les valeurs d'un divisionnel ne visent que les modulateurs de ce clavier
                memory.values.erase (std::remove_if (memory.values.begin(), memory.values.end(), [&] (const auto& v)
                {
                    for (int i = 0; i < kModulators; ++i)
                        if (v.first == mixer::modulatorParam (opt_.firstTrack + static_cast<int> (k), i)) return false;
                    return true;
                }), memory.values.end());
                result_.combinations.divisionals[combi::divisionalKey (opt_.firstTrack + static_cast<int> (k), index / combi::kPistons, index % combi::kPistons)] = std::move (memory);
            }
        }
    }

    void otherThings()
    {
        const int tremulants = ini_.getInt ("Organ", "NumberOfTremulants", 0);
        for (int t = 1; t <= tremulants; ++t)
            warn ("tremulant \"" + ini_.get (numbered ("Tremulant", t), "Name", numbered ("Tremulant", t)) + "\" is not imported");
        const int enclosures = ini_.getInt ("Organ", "NumberOfEnclosures", 0);
        if (enclosures > 0) warn (std::to_string (enclosures) + " expression box(es) are not imported");
        if (ini_.getInt ("Organ", "NumberOfPanels", 0) > 0) warn ("the graphical panels are not imported");
        if (ini_.getInt ("Organ", "NumberOfReversiblePistons", 0) > 0) warn ("reversible pistons are not imported");
        if (ini_.getInt ("Organ", "NumberOfDivisionalCouplers", 0) > 0) warn ("divisional couplers are not imported");
    }

    const IniFile& ini_;
    Options opt_;
    Result result_;
    int organAmp_ = 100;
    double organTuning_ = 0.0, organGain_ = 0.0;
    std::map<int, int> channelOf_;
    std::map<int, ManualInfo> manuals_;
    std::vector<std::map<std::string, int>> modulators_;     // par clavier : interrupteur -> indice de modulateur
    std::vector<std::set<std::string>> usedGroups_;
    std::map<std::string, std::vector<ParamId>> params_;     // interrupteur -> ses paramètres (un par clavier qui le porte, ou un tirant du routeur)
    std::map<std::string, bool> initial_;
    std::map<std::string, FileMarks> marks_;
    int routerSwitches_ = 0;
};
} // namespace

Result importOrgan (std::string_view odfText, const Options& options)
{
    const IniFile ini = IniFile::parse (odfText);
    return Builder (ini, options).run();
}

Result importOrganFile (const fs::path& odfFile, Options options)
{
    std::ifstream in (odfFile, std::ios::binary);
    if (! in)
    {
        Result r;
        r.error = "cannot read " + odfFile.u8string();
        return r;
    }
    const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    if (options.baseDir.empty()) options.baseDir = odfFile.parent_path();
    return importOrgan (text, options);
}
} // namespace neptune::core::organimport
