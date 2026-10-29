#include "neptune/core/SoundFont.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdio.h>
#include <math.h>

extern "C"
{
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
}

namespace fs = std::filesystem;

namespace neptune::core::soundfont
{
namespace
{
// Numéros de générateurs de la spécification SoundFont 2.04 (section 8.1.2).
enum Gen
{
    kStartAddrs = 0, kEndAddrs = 1, kStartLoopAddrs = 2, kEndLoopAddrs = 3, kStartAddrsCoarse = 4, kModLfoToPitch = 5, kVibLfoToPitch = 6, kModEnvToPitch = 7,
    kFilterFc = 8, kFilterQ = 9, kModLfoToFilterFc = 10, kModEnvToFilterFc = 11, kEndAddrsCoarse = 12, kModLfoToVolume = 13, kChorusSend = 15, kReverbSend = 16,
    kPan = 17, kDelayVolEnv = 33, kAttackVolEnv = 34, kHoldVolEnv = 35, kDecayVolEnv = 36, kSustainVolEnv = 37, kReleaseVolEnv = 38,
    kKeynumToVolEnvHold = 39, kKeynumToVolEnvDecay = 40, kInstrument = 41, kKeyRange = 43, kVelRange = 44, kStartLoopAddrsCoarse = 45, kKeynum = 46, kVelocity = 47,
    kAttenuation = 48, kEndLoopAddrsCoarse = 50, kCoarseTune = 51, kFineTune = 52, kSampleId = 53, kSampleModes = 54, kScaleTuning = 56, kExclusiveClass = 57,
    kRootKey = 58, kKeynumToModEnvHold = 31, kKeynumToModEnvDecay = 32, kNumGens = 64
};

struct GenVal
{
    bool set = false;
    std::int16_t v = 0;      // montant signé (ou non signé tel quel pour les identifiants)
    std::uint8_t lo = 0, hi = 0;
};
using GenSet = std::array<GenVal, kNumGens>;

struct Phdr { std::string name; int preset = 0, bank = 0, bagNdx = 0; };
struct BagRec { int gen = 0, mod = 0; };
struct GenRec { int oper = 0; std::uint8_t lo = 0, hi = 0; std::int16_t v = 0; };
struct InstRec { std::string name; int bagNdx = 0; };
struct Shdr
{
    std::string name;
    std::uint32_t start = 0, end = 0, startLoop = 0, endLoop = 0, rate = 0;
    int pitch = 60, correction = 0, link = 0, type = 1;
};

constexpr int kRomFlag = 0x8000, kVorbisFlag = 0x10;

struct Bank
{
    fs::path file;
    std::string name;
    int major = 2;
    bool compressed = false;
    std::uint64_t smplOffset = 0, smplSize = 0;
    std::vector<Phdr> phdr;
    std::vector<BagRec> pbag, ibag;
    std::vector<GenRec> pgen, igen;
    std::vector<InstRec> inst;
    std::vector<Shdr> shdr;
    bool hasModulators = false;
    mutable std::mutex mutex;
    mutable std::vector<std::int64_t> vorbisFrames;   // SF3 : longueur de chaque échantillon une fois connue, -1 sinon
};

// ----- lecture du fichier RIFF -----
std::uint32_t le32 (const std::uint8_t* p) { return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8) | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24); }
int le16 (const std::uint8_t* p) { return static_cast<int> (p[0]) | (static_cast<int> (p[1]) << 8); }

std::string nameOf (const std::uint8_t* p)
{
    std::string s (reinterpret_cast<const char*> (p), 20);
    s.resize (std::strlen (s.c_str()));
    while (! s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

struct SubChunk { std::string id; std::uint64_t pos = 0, size = 0; };

bool readExact (std::ifstream& in, std::uint64_t pos, void* dst, std::uint64_t n)
{
    in.clear();
    in.seekg (static_cast<std::streamoff> (pos));
    in.read (static_cast<char*> (dst), static_cast<std::streamsize> (n));
    return static_cast<std::uint64_t> (in.gcount()) == n;
}

// Sous-blocs entre `start` et `end` (hors en-têtes) : identifiant, position des données, taille.
std::vector<SubChunk> subChunks (std::ifstream& in, std::uint64_t start, std::uint64_t end)
{
    std::vector<SubChunk> out;
    std::uint64_t pos = start;
    while (pos + 8 <= end)
    {
        std::uint8_t h[8];
        if (! readExact (in, pos, h, 8)) break;
        SubChunk c;
        c.id.assign (reinterpret_cast<const char*> (h), 4);
        c.size = le32 (h + 4);
        c.pos = pos + 8;
        if (c.pos + c.size > end) c.size = end > c.pos ? end - c.pos : 0;
        out.push_back (c);
        pos = c.pos + c.size + (c.size & 1u);
    }
    return out;
}

bool parseBank (const fs::path& file, Bank& b, std::string& err)
{
    std::ifstream in (file, std::ios::binary);
    if (! in) { err = "cannot open " + file.u8string(); return false; }
    std::error_code ec;
    const std::uint64_t fileSize = fs::file_size (file, ec);
    std::uint8_t head[12];
    if (ec || fileSize < 12 || ! readExact (in, 0, head, 12) || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "sfbk", 4) != 0)
    { err = "not a SoundFont file"; return false; }

    b.file = file;
    b.name = file.stem().u8string();
    std::map<std::string, std::vector<std::uint8_t>> pdta;
    bool haveSmpl = false;
    for (const auto& top : subChunks (in, 12, std::min<std::uint64_t> (fileSize, 8 + static_cast<std::uint64_t> (le32 (head + 4)))))
    {
        if (top.id != "LIST" || top.size < 4) continue;
        char type[4];
        if (! readExact (in, top.pos, type, 4)) continue;
        const std::string t (type, 4);
        for (const auto& c : subChunks (in, top.pos + 4, top.pos + top.size))
        {
            if (t == "INFO")
            {
                std::vector<std::uint8_t> d (static_cast<std::size_t> (std::min<std::uint64_t> (c.size, 4096)));
                if (! d.empty() && readExact (in, c.pos, d.data(), d.size()))
                {
                    if (c.id == "ifil" && d.size() >= 4) b.major = le16 (d.data());
                    else if (c.id == "INAM")
                    {
                        std::string s (reinterpret_cast<const char*> (d.data()), d.size());
                        s.resize (std::strlen (s.c_str()));
                        if (! s.empty()) b.name = s;
                    }
                }
            }
            else if (t == "sdta" && c.id == "smpl") { b.smplOffset = c.pos; b.smplSize = c.size; haveSmpl = true; }
            else if (t == "pdta")
            {
                std::vector<std::uint8_t> d (static_cast<std::size_t> (c.size));
                if (! d.empty() && ! readExact (in, c.pos, d.data(), d.size())) { err = "truncated SoundFont file"; return false; }
                pdta[c.id] = std::move (d);
            }
        }
    }
    if (! haveSmpl) { err = "the SoundFont has no sample data"; return false; }
    for (const char* need : { "phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr" })
        if (! pdta.count (need)) { err = std::string ("the SoundFont is missing its '") + need + "' block"; return false; }
    b.compressed = b.major >= 3;

    auto records = [&] (const char* id, std::size_t size, auto fn)
    {
        const auto& d = pdta[id];
        for (std::size_t i = 0; i + size <= d.size(); i += size) fn (d.data() + i);
    };
    records ("phdr", 38, [&] (const std::uint8_t* p) { b.phdr.push_back ({ nameOf (p), le16 (p + 20), le16 (p + 22), le16 (p + 24) }); });
    records ("pbag", 4, [&] (const std::uint8_t* p) { b.pbag.push_back ({ le16 (p), le16 (p + 2) }); });
    records ("ibag", 4, [&] (const std::uint8_t* p) { b.ibag.push_back ({ le16 (p), le16 (p + 2) }); });
    auto genRec = [] (const std::uint8_t* p) { return GenRec { le16 (p), p[2], p[3], static_cast<std::int16_t> (le16 (p + 2)) }; };
    records ("pgen", 4, [&] (const std::uint8_t* p) { b.pgen.push_back (genRec (p)); });
    records ("igen", 4, [&] (const std::uint8_t* p) { b.igen.push_back (genRec (p)); });
    records ("inst", 22, [&] (const std::uint8_t* p) { b.inst.push_back ({ nameOf (p), le16 (p + 20) }); });
    records ("shdr", 46, [&] (const std::uint8_t* p)
    {
        Shdr s;
        s.name = nameOf (p);
        s.start = le32 (p + 20); s.end = le32 (p + 24); s.startLoop = le32 (p + 28); s.endLoop = le32 (p + 32); s.rate = le32 (p + 36);
        s.pitch = p[40]; s.correction = static_cast<std::int8_t> (p[41]); s.link = le16 (p + 42); s.type = le16 (p + 44);
        b.shdr.push_back (s);
    });
    b.hasModulators = (pdta.count ("pmod") && pdta["pmod"].size() > 10) || (pdta.count ("imod") && pdta["imod"].size() > 10);
    if (b.phdr.size() < 2 || b.shdr.size() < 2) { err = "the SoundFont has no preset or no sample"; return false; }
    b.vorbisFrames.assign (b.shdr.size(), -1);
    return true;
}

// ----- cache des banques -----
struct CacheEntry
{
    std::int64_t size = 0;
    fs::file_time_type mtime {};
    std::shared_ptr<Bank> bank;
};
std::mutex cacheMutex;
std::map<std::string, CacheEntry> cache;

std::shared_ptr<Bank> getBank (const fs::path& file, std::string& err)
{
    std::error_code ec;
    const auto size = static_cast<std::int64_t> (fs::file_size (file, ec));
    const auto mtime = fs::last_write_time (file, ec);
    const auto key = file.u8string();
    std::lock_guard<std::mutex> lock (cacheMutex);
    if (const auto it = cache.find (key); it != cache.end() && it->second.size == size && it->second.mtime == mtime) return it->second.bank;
    auto bank = std::make_shared<Bank>();
    if (! parseBank (file, *bank, err)) return nullptr;
    cache[key] = { size, mtime, bank };
    return bank;
}

bool isSampleUsable (const Bank& b, int idx)
{
    return idx >= 0 && static_cast<std::size_t> (idx) + 1 < b.shdr.size() && (b.shdr[static_cast<std::size_t> (idx)].type & kRomFlag) == 0;
}

// ----- décodage d'un échantillon -----
bool readSlice (const Bank& b, std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& out)
{
    std::ifstream in (b.file, std::ios::binary);
    if (! in) return false;
    out.resize (static_cast<std::size_t> (bytes));
    return bytes == 0 || readExact (in, b.smplOffset + offset, out.data(), bytes);
}

struct VorbisClip
{
    std::vector<std::uint8_t> bytes;
    stb_vorbis* v = nullptr;
    ~VorbisClip() { if (v != nullptr) stb_vorbis_close (v); }
    bool open (std::string& err)
    {
        int e = 0;
        v = stb_vorbis_open_memory (bytes.data(), static_cast<int> (bytes.size()), &e, nullptr);
        if (v == nullptr) { err = "cannot decode a compressed sample (Ogg Vorbis error " + std::to_string (e) + ")"; return false; }
        return true;
    }
};

bool sampleBytes (const Bank& b, int idx, std::vector<std::uint8_t>& out, std::string& err)
{
    const auto& s = b.shdr[static_cast<std::size_t> (idx)];
    const std::uint64_t unit = b.compressed ? 1 : 2;
    if (s.end <= s.start || static_cast<std::uint64_t> (s.end) * unit > b.smplSize) { err = "sample '" + s.name + "' lies outside the sample data"; return false; }
    if (! readSlice (b, static_cast<std::uint64_t> (s.start) * unit, static_cast<std::uint64_t> (s.end - s.start) * unit, out)) { err = "cannot read sample '" + s.name + "'"; return false; }
    return true;
}

// Nombre de frames d'un échantillon (SF3 : longueur du flux Vorbis, sans décoder).
std::int64_t frameCount (const Bank& b, int idx, std::string& err)
{
    const auto& s = b.shdr[static_cast<std::size_t> (idx)];
    if (! b.compressed) return s.end > s.start ? static_cast<std::int64_t> (s.end - s.start) : 0;
    {
        std::lock_guard<std::mutex> lock (b.mutex);
        if (b.vorbisFrames[static_cast<std::size_t> (idx)] >= 0) return b.vorbisFrames[static_cast<std::size_t> (idx)];
    }
    VorbisClip clip;
    if (! sampleBytes (b, idx, clip.bytes, err) || ! clip.open (err)) return 0;
    const auto n = static_cast<std::int64_t> (stb_vorbis_stream_length_in_samples (clip.v));
    std::lock_guard<std::mutex> lock (b.mutex);
    b.vorbisFrames[static_cast<std::size_t> (idx)] = n;
    return n;
}

bool decodeMono (const Bank& b, int idx, std::vector<std::int16_t>& pcm, std::string& err)
{
    std::vector<std::uint8_t> bytes;
    if (! b.compressed)
    {
        if (! sampleBytes (b, idx, bytes, err)) return false;
        pcm.resize (bytes.size() / 2);
        for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<std::int16_t> (le16 (bytes.data() + 2 * i));
        return true;
    }
    VorbisClip clip;
    if (! sampleBytes (b, idx, clip.bytes, err) || ! clip.open (err)) return false;
    const int channels = std::max (1, stb_vorbis_get_info (clip.v).channels);
    const auto total = static_cast<std::size_t> (std::max (0u, stb_vorbis_stream_length_in_samples (clip.v)));
    pcm.assign (total, 0);
    std::vector<std::int16_t> buf (static_cast<std::size_t> (channels) * 4096);
    std::size_t done = 0;
    while (done < total)
    {
        const int n = stb_vorbis_get_samples_short_interleaved (clip.v, channels, buf.data(), static_cast<int> (buf.size()));
        if (n <= 0) break;
        for (int i = 0; i < n && done < total; ++i) pcm[done++] = buf[static_cast<std::size_t> (i) * static_cast<std::size_t> (channels)];
    }
    pcm.resize (done);
    return ! pcm.empty();
}

// ----- zones d'un preset -----
struct ZoneGens
{
    GenSet g {};
    bool has (int k) const { return g[static_cast<std::size_t> (k)].set; }
    int val (int k, int def) const { return has (k) ? g[static_cast<std::size_t> (k)].v : def; }
};

std::vector<ZoneGens> zonesOf (const std::vector<BagRec>& bags, const std::vector<GenRec>& gens, int firstBag, int lastBag)
{
    std::vector<ZoneGens> out;
    for (int bg = firstBag; bg < lastBag && static_cast<std::size_t> (bg) + 1 < bags.size(); ++bg)
    {
        ZoneGens z;
        const int g0 = bags[static_cast<std::size_t> (bg)].gen, g1 = std::min<int> (bags[static_cast<std::size_t> (bg) + 1].gen, static_cast<int> (gens.size()));
        for (int gi = g0; gi < g1; ++gi)
        {
            const auto& r = gens[static_cast<std::size_t> (gi)];
            if (r.oper < 0 || r.oper >= kNumGens) continue;
            z.g[static_cast<std::size_t> (r.oper)] = { true, r.v, r.lo, r.hi };
        }
        out.push_back (z);
    }
    return out;
}

// Type d'un échantillon sans les drapeaux (1 mono, 2 droite, 4 gauche).
int baseType (const Shdr& s) { return s.type & 0x7fff & ~kVorbisFlag; }

std::string stereoStem (std::string n)
{
    for (const char* tail : { "(L)", "(R)", "_L", "_R", " L", " R", "-L", "-R", ".L", ".R" })
    {
        const std::string t (tail);
        if (n.size() > t.size() && n.compare (n.size() - t.size(), t.size(), t) == 0) { n.resize (n.size() - t.size()); break; }
    }
    return n;
}

// L'échantillon associé de l'autre côté : le lien du fichier (SF2) ; les SF3 écrits par MuseScore n'en portent plus (lien à 0) : on prend alors le voisin direct de type
// opposé dont le nom est le même au suffixe gauche/droite près.
int stereoPartner (const Bank& b, int idx)
{
    const auto& s = b.shdr[static_cast<std::size_t> (idx)];
    const int type = baseType (s);
    if (type != 2 && type != 4) return -1;
    const int other = type == 4 ? 2 : 4;
    const int link = s.link;
    if (link != idx && isSampleUsable (b, link) && baseType (b.shdr[static_cast<std::size_t> (link)]) == other && b.shdr[static_cast<std::size_t> (link)].link == idx) return link;
    if (! b.compressed) return -1;
    for (const int n : { idx - 1, idx + 1 })
        if (isSampleUsable (b, n) && baseType (b.shdr[static_cast<std::size_t> (n)]) == other && stereoStem (b.shdr[static_cast<std::size_t> (n)].name) == stereoStem (s.name)) return n;
    return -1;
}

double timecentsMs (double tc) { return tc <= -12000.0 ? 0.0 : std::min (envelopeMaxMs, 1000.0 * std::pow (2.0, tc / 1200.0)); }

struct Built
{
    ZoneDescription z;
    int sample = -1;
    int presetLayer = 0;
    std::int64_t frames = 0;
    bool merged = false;
};

void note (std::set<std::string>& s, const char* what) { s.insert (what); }
} // namespace

// ===========================================================================================================================================================
std::string samplePath (const fs::path& bank, int left, int right)
{
    return bank.u8string() + "#" + std::to_string (left) + (right >= 0 ? "+" + std::to_string (right) : std::string());
}

bool parseSamplePath (const std::string& s, SampleRef& out)
{
    const auto h = s.rfind ('#');
    if (h == std::string::npos || h + 1 >= s.size() || h < 4) return false;
    std::string ext = s.substr (h - 4, 4);
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    if (ext != ".sf2" && ext != ".sf3") return false;
    int a = 0, b = -1;
    std::size_t i = h + 1;
    auto number = [&] (int& v)
    {
        const std::size_t st = i;
        long long x = 0;
        while (i < s.size() && std::isdigit (static_cast<unsigned char> (s[i])) && i - st < 9) x = x * 10 + (s[i++] - '0');
        v = static_cast<int> (x);
        return i > st;
    };
    if (! number (a)) return false;
    if (i < s.size() && s[i] == '+') { ++i; if (! number (b)) return false; }
    if (i != s.size()) return false;
    out.bank = fs::u8path (s.substr (0, h));
    out.left = a;
    out.right = b;
    return true;
}

BankInfo readBank (const fs::path& file)
{
    BankInfo info;
    std::string err;
    const auto b = getBank (file, err);
    if (b == nullptr) { info.error = err; return info; }
    info.ok = true;
    info.name = b->name;
    info.versionMajor = b->major;
    info.compressed = b->compressed;
    info.samples = static_cast<int> (b->shdr.size()) - 1;
    for (std::size_t p = 0; p + 1 < b->phdr.size(); ++p)
    {
        PresetInfo pi;
        pi.name = b->phdr[p].name;
        pi.bank = b->phdr[p].bank;
        pi.program = b->phdr[p].preset;
        pi.zones = std::max (0, b->phdr[p + 1].bagNdx - b->phdr[p].bagNdx);
        info.presets.push_back (pi);
    }
    return info;
}

SampleProbe probeSample (const SampleRef& ref)
{
    SampleProbe p;
    std::string err;
    const auto b = getBank (ref.bank, err);
    if (b == nullptr) { p.error = err; return p; }
    if (! isSampleUsable (*b, ref.left) || (ref.right >= 0 && ! isSampleUsable (*b, ref.right))) { p.error = "sample number out of range"; return p; }
    std::int64_t frames = frameCount (*b, ref.left, err);
    if (ref.right >= 0) frames = std::min (frames, frameCount (*b, ref.right, err));
    if (frames <= 0) { p.error = err.empty() ? "empty sample" : err; return p; }
    p.ok = true;
    p.channels = ref.right >= 0 ? 2 : 1;
    p.frames = frames;
    p.sampleRate = static_cast<double> (std::max<std::uint32_t> (1u, b->shdr[static_cast<std::size_t> (ref.left)].rate));
    return p;
}

DecodedSample decodeSample (const SampleRef& ref)
{
    DecodedSample d;
    std::string err;
    const auto b = getBank (ref.bank, err);
    if (b == nullptr) { d.error = err; return d; }
    if (! isSampleUsable (*b, ref.left) || (ref.right >= 0 && ! isSampleUsable (*b, ref.right))) { d.error = "sample number out of range"; return d; }
    std::vector<std::int16_t> l, r;
    if (! decodeMono (*b, ref.left, l, err)) { d.error = err; return d; }
    if (ref.right >= 0 && ! decodeMono (*b, ref.right, r, err)) { d.error = err; return d; }
    d.sampleRate = static_cast<double> (std::max<std::uint32_t> (1u, b->shdr[static_cast<std::size_t> (ref.left)].rate));
    if (ref.right < 0)
    {
        d.channels = 1;
        d.frames = static_cast<std::int64_t> (l.size());
        d.interleaved = std::move (l);
    }
    else
    {
        d.channels = 2;
        d.frames = static_cast<std::int64_t> (std::min (l.size(), r.size()));
        d.interleaved.resize (static_cast<std::size_t> (d.frames) * 2);
        for (std::size_t i = 0; i < static_cast<std::size_t> (d.frames); ++i) { d.interleaved[2 * i] = l[i]; d.interleaved[2 * i + 1] = r[i]; }
    }
    d.ok = d.frames > 0;
    if (! d.ok) d.error = "empty sample";
    return d;
}

// ===========================================================================================================================================================
Result importPreset (const fs::path& file, int presetIndex)
{
    Result res;
    std::string err;
    const auto bankPtr = getBank (file, err);
    if (bankPtr == nullptr) { res.error = err; return res; }
    const Bank& bank = *bankPtr;
    if (presetIndex < 0 || static_cast<std::size_t> (presetIndex) + 1 >= bank.phdr.size()) { res.error = "preset number out of range"; return res; }

    const auto& ph = bank.phdr[static_cast<std::size_t> (presetIndex)];
    res.name = ph.name.empty() ? bank.name : ph.name;
    const auto presetZones = zonesOf (bank.pbag, bank.pgen, ph.bagNdx, bank.phdr[static_cast<std::size_t> (presetIndex) + 1].bagNdx);
    const ZoneGens* presetGlobal = ! presetZones.empty() && ! presetZones[0].has (kInstrument) ? &presetZones[0] : nullptr;

    std::set<std::string> unconverted;
    std::vector<Built> built;
    int presetLayerCount = 0;
    int skipped = 0;

    for (const auto& pz : presetZones)
    {
        if (! pz.has (kInstrument)) continue;
        const int ii = pz.g[kInstrument].v & 0xffff;
        if (ii < 0 || static_cast<std::size_t> (ii) + 1 >= bank.inst.size()) { ++skipped; continue; }
        const auto instZones = zonesOf (bank.ibag, bank.igen, bank.inst[static_cast<std::size_t> (ii)].bagNdx, bank.inst[static_cast<std::size_t> (ii) + 1].bagNdx);
        const ZoneGens* instGlobal = ! instZones.empty() && ! instZones[0].has (kSampleId) ? &instZones[0] : nullptr;
        const int presetLayer = presetLayerCount++;   // une couche par zone de preset : les zones de couches différentes jouent ensemble

        for (const auto& iz : instZones)
        {
            if (! iz.has (kSampleId)) continue;
            const int sampleIdx = iz.g[kSampleId].v & 0xffff;
            if (! isSampleUsable (bank, sampleIdx)) { ++skipped; continue; }
            const auto& sh = bank.shdr[static_cast<std::size_t> (sampleIdx)];

            // valeur effective : instrument (zone, sinon globale, sinon défaut) + preset (zone, sinon globale)
            auto instVal = [&] (int k, int def) { return iz.has (k) ? iz.val (k, def) : instGlobal != nullptr ? instGlobal->val (k, def) : def; };
            auto anySet = [&] (int k) { return iz.has (k) || (instGlobal != nullptr && instGlobal->has (k)) || pz.has (k) || (presetGlobal != nullptr && presetGlobal->has (k)); };
            auto presetAdd = [&] (int k) { return pz.has (k) ? pz.val (k, 0) : presetGlobal != nullptr ? presetGlobal->val (k, 0) : 0; };
            auto val = [&] (int k, int def) { return instVal (k, def) + presetAdd (k); };
            auto range = [&] (int k, int& lo, int& hi)
            {
                lo = 0; hi = 127;
                const ZoneGens* in = iz.has (k) ? &iz : instGlobal != nullptr && instGlobal->has (k) ? instGlobal : nullptr;
                if (in != nullptr) { lo = in->g[static_cast<std::size_t> (k)].lo; hi = in->g[static_cast<std::size_t> (k)].hi; }
                const ZoneGens* pr = pz.has (k) ? &pz : presetGlobal != nullptr && presetGlobal->has (k) ? presetGlobal : nullptr;
                if (pr != nullptr) { lo = std::max<int> (lo, pr->g[static_cast<std::size_t> (k)].lo); hi = std::min<int> (hi, pr->g[static_cast<std::size_t> (k)].hi); }
            };

            Built bz;
            bz.sample = sampleIdx;
            bz.presetLayer = presetLayer;
            bz.frames = frameCount (bank, sampleIdx, err);
            if (bz.frames <= 0) { ++skipped; continue; }
            ZoneDescription& z = bz.z;

            int klo, khi, vlo, vhi;
            range (kKeyRange, klo, khi);
            range (kVelRange, vlo, vhi);
            if (klo > khi || vlo > vhi) { ++skipped; continue; }
            z.keyLow = klo; z.keyHigh = khi;
            z.velLow = std::clamp (vlo, 1, 127); z.velHigh = std::clamp (vhi, z.velLow, 127);
            int root = instVal (kRootKey, -1);
            if (root < 0 || root > 127) root = sh.pitch <= 127 ? sh.pitch : 60;
            z.rootNote = root;
            z.samplePath = samplePath (file, sampleIdx);

            z.tuneCents = 100.0 * val (kCoarseTune, 0) + val (kFineTune, 0) + sh.correction;
            z.gainDb = -std::clamp (val (kAttenuation, 0), 0, 1440) / 10.0;
            z.pan = std::clamp (val (kPan, 0) / 500.0, -1.0, 1.0);

            // début, fin et boucle, relatifs au début de l'échantillon (les boucles du SF2 sont comptées depuis le début de la zone d'échantillons, celles du SF3 depuis le sien)
            const std::int64_t base = bank.compressed ? 0 : static_cast<std::int64_t> (sh.start);
            const std::int64_t startOff = instVal (kStartAddrs, 0) + 32768LL * instVal (kStartAddrsCoarse, 0);
            const std::int64_t endOff = instVal (kEndAddrs, 0) + 32768LL * instVal (kEndAddrsCoarse, 0);
            const std::int64_t loopStartOff = instVal (kStartLoopAddrs, 0) + 32768LL * instVal (kStartLoopAddrsCoarse, 0);
            const std::int64_t loopEndOff = instVal (kEndLoopAddrs, 0) + 32768LL * instVal (kEndLoopAddrsCoarse, 0);
            if (startOff > 0) z.startFrame = std::min (startOff, bz.frames - 1);
            if (endOff != 0) z.endFrame = std::clamp<std::int64_t> (bz.frames + endOff, z.startFrame + 1, bz.frames);
            const std::int64_t ls = std::clamp<std::int64_t> (static_cast<std::int64_t> (sh.startLoop) - base + loopStartOff, 0, bz.frames);
            const std::int64_t le = std::clamp<std::int64_t> (static_cast<std::int64_t> (sh.endLoop) - base + loopEndOff, 0, bz.frames);
            const int modes = instVal (kSampleModes, 0);
            if ((modes == 1 || modes == 3) && le > ls + 1)
            {
                z.mode = PlayMode::Loop;
                z.loopStart = ls;
                z.loopEnd = le;
            }
            else z.releaseCuts = true;   // sans boucle : joué en entier, la touche relâchée l'interrompt par le relâchement de l'enveloppe, comme dans un SoundFont

            // enveloppe de volume. Les temps de déclin et de relâchement sont donnés pour une variation complète (100 dB) : le déclin jusqu'au sustain n'en prend que la part
            if (anySet (kDelayVolEnv) || anySet (kAttackVolEnv) || anySet (kHoldVolEnv) || anySet (kDecayVolEnv) || anySet (kSustainVolEnv) || anySet (kReleaseVolEnv))
            {
                z.envelopeOverride = true;
                const double sustainCb = std::clamp (val (kSustainVolEnv, 0), 0, 1440);
                z.envelope.delay.ms = timecentsMs (val (kDelayVolEnv, -12000));
                z.envelope.attack.ms = timecentsMs (val (kAttackVolEnv, -12000));
                z.envelope.hold.ms = timecentsMs (val (kHoldVolEnv, -12000));
                z.envelope.decay.ms = std::min (envelopeMaxMs, timecentsMs (val (kDecayVolEnv, -12000)) * std::min (1.0, sustainCb / 1000.0));
                z.envelope.sustain = std::clamp (std::pow (10.0, -sustainCb / 200.0), 0.0, 1.0);
                if (anySet (kReleaseVolEnv)) z.envelope.release.ms = std::max (1.0, timecentsMs (val (kReleaseVolEnv, -12000)));
            }

            // filtre passe-bas statique (à 13 500 cents ou plus, le filtre est ouvert)
            const int fc = val (kFilterFc, 13500);
            if (fc < 13490)
            {
                z.filterOverride = true;
                auto& f = z.filters.filter[0];
                f.on = true;
                f.mode = FilterMode::LowPass;
                f.slopeDb = 12;
                f.cutoffHz = std::clamp (8.176 * std::pow (2.0, fc / 1200.0), filterMinHz, filterMaxHz);
                f.resonance = std::clamp (std::max (0, val (kFilterQ, 0)) / 10.0 / 40.0, 0.0, 1.0);
            }

            z.chokeCuts = z.chokeCutBy = std::max (0, instVal (kExclusiveClass, 0));

            for (const int k : { kModLfoToPitch, kVibLfoToPitch, kModEnvToPitch, kModLfoToFilterFc, kModEnvToFilterFc, kModLfoToVolume })
                if (val (k, 0) != 0) { note (unconverted, "LFO and modulation envelope"); break; }
            if (val (kReverbSend, 0) != 0 || val (kChorusSend, 0) != 0) note (unconverted, "reverb and chorus sends");
            for (const int k : { kKeynumToVolEnvHold, kKeynumToVolEnvDecay })
                if (val (k, 0) != 0) { note (unconverted, "key tracking of the envelope"); break; }
            if (iz.has (kScaleTuning) || (instGlobal != nullptr && instGlobal->has (kScaleTuning)) || pz.has (kScaleTuning))
                if (val (kScaleTuning, 100) != 100) note (unconverted, "scale tuning");
            if (instVal (kKeynum, -1) >= 0 || instVal (kVelocity, -1) >= 0) note (unconverted, "fixed key or velocity");
            built.push_back (std::move (bz));
        }
    }

    // paires stéréo : la zone d'un échantillon gauche (type 4) et celle de son échantillon droit lié (type 2), identiques par ailleurs, ne font plus qu'une zone stéréo
    for (std::size_t i = 0; i < built.size(); ++i)
    {
        auto& a = built[i];
        if (a.merged || baseType (bank.shdr[static_cast<std::size_t> (a.sample)]) != 4) continue;
        const int partner = stereoPartner (bank, a.sample);
        if (partner < 0) continue;
        for (std::size_t j = 0; j < built.size(); ++j)
        {
            auto& c = built[j];
            if (j == i || c.merged || c.sample != partner || c.frames != a.frames) continue;
            ZoneDescription lz = a.z, rz = c.z;
            const std::string both = samplePath (file, a.sample, c.sample);
            lz.samplePath = rz.samplePath = both;
            lz.pan = rz.pan = 0.0;
            if (! (lz == rz)) continue;
            a.z.samplePath = both;
            a.z.pan = std::clamp ((a.z.pan + c.z.pan) * 0.5, -1.0, 1.0);
            c.merged = true;
            break;
        }
    }
    // Couches : dans un SoundFont, des zones de couches différentes jouent ENSEMBLE, même avec la même étendue de notes (et deux zones de même étendue dans un même instrument
    // aussi). Neptune joue ensemble des zones de couches différentes, et prend pour des alternatives (une seule joue) des zones de même étendue dans une même couche.
    {
        std::map<std::string, int> seen;                    // couche du preset + étendue -> combien de zones déjà vues
        std::map<std::pair<int, int>, int> layerId;         // (couche du preset, rang de la zone dans son étendue) -> couche de Neptune
        bool tooMany = false;
        for (auto& b : built)
        {
            if (b.merged) continue;
            const std::string key = std::to_string (b.presetLayer) + "|" + std::to_string (b.z.keyLow) + "-" + std::to_string (b.z.keyHigh) + "|" + std::to_string (b.z.velLow) + "-" + std::to_string (b.z.velHigh);
            const int rank = seen[key]++;
            const auto id = layerId.emplace (std::make_pair (b.presetLayer, rank), static_cast<int> (layerId.size())).first->second;
            if (id >= ZoneDescription::maxLayers) tooMany = true;
            b.z.layer = std::min (id, ZoneDescription::maxLayers - 1);
        }
        if (tooMany) res.warnings.push_back ("More than " + std::to_string (ZoneDescription::maxLayers) + " layers: the last ones are merged into the last layer");
    }
    for (auto& b : built)
        if (! b.merged) res.instrument.zones.push_back (std::move (b.z));

    res.zones = static_cast<int> (res.instrument.zones.size());
    res.instrument.soundfontFile = file.u8string();
    res.instrument.soundfontPreset = presetIndex;
    if (bank.hasModulators) note (unconverted, "modulators");
    if (skipped > 0) res.warnings.push_back (std::to_string (skipped) + " zone(s) skipped: unusable sample or empty range");
    if (! unconverted.empty())
    {
        std::string names;
        for (const auto& n : unconverted) names += (names.empty() ? "" : ", ") + n;
        res.warnings.push_back ("Not converted: " + names);
    }
    if (res.zones == 0) { res.error = "This preset has no playable zone"; return res; }
    res.ok = true;
    return res;
}
} // namespace neptune::core::soundfont
