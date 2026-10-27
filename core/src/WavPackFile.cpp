#include "neptune/core/WavPackFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#if defined (NEPTUNE_HAS_WAVPACK)
 #include <wavpack.h>
#endif

namespace neptune::core
{
bool wavPackDecodingAvailable() noexcept
{
   #if defined (NEPTUNE_HAS_WAVPACK)
    return true;
   #else
    return false;
   #endif
}

bool isWavPackFile (const std::filesystem::path& path)
{
    std::ifstream f (path, std::ios::binary);
    char magic[4] = {};
    return f.read (magic, 4) && std::memcmp (magic, "wvpk", 4) == 0;
}

namespace
{
std::uint32_t le32 (const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t> (p[0]) | (static_cast<std::uint32_t> (p[1]) << 8) | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[3]) << 24);
}

// Parcourt une suite de chunks RIFF [id (4) taille (4) données, octet de bourrage si la taille est impaire] à partir de `pos`. L'enveloppe de WavPack ne garde pas
// les échantillons : le chunk `data` n'y est qu'un en-tête de 8 octets, et ce qui suit (s'il y a quelque chose) sont les chunks de queue.
void walkChunks (const std::vector<std::uint8_t>& b, std::size_t pos, WavPackMarks& out)
{
    while (pos + 8 <= b.size())
    {
        const std::uint8_t* id = b.data() + pos;
        const std::uint32_t size = le32 (b.data() + pos + 4);
        if (std::memcmp (id, "data", 4) == 0) { pos += 8; continue; }
        const std::size_t body = pos + 8;
        const std::size_t available = std::min<std::size_t> (size, b.size() - body);
        const std::uint8_t* p = b.data() + body;
        if (std::memcmp (id, "smpl", 4) == 0 && available >= 36)
        {
            const std::uint32_t unity = le32 (p + 12), fraction = le32 (p + 16), count = le32 (p + 28);
            if (unity <= 127)
            {
                double cents = static_cast<double> (fraction) / 4294967296.0 * 100.0;
                int note = static_cast<int> (unity);
                if (cents > 50.0) { cents -= 100.0; ++note; }
                out.hasRoot = true;
                out.rootNote = std::clamp (note, 0, 127);
                out.detuneCents = cents;
            }
            for (std::uint32_t i = 0; i < count && 36 + 24 * (static_cast<std::size_t> (i) + 1) <= available; ++i)
            {
                const std::uint8_t* lp = p + 36 + 24 * static_cast<std::size_t> (i);
                out.loops.emplace_back (static_cast<std::int64_t> (le32 (lp + 8)), static_cast<std::int64_t> (le32 (lp + 12)));
            }
        }
        else if (std::memcmp (id, "cue ", 4) == 0 && available >= 4)
        {
            const std::uint32_t count = le32 (p);
            for (std::uint32_t i = 0; i < count && 4 + 24 * (static_cast<std::size_t> (i) + 1) <= available; ++i)
            {
                const std::uint8_t* cp = p + 4 + 24 * static_cast<std::size_t> (i);
                const std::uint32_t sampleOffset = le32 (cp + 20), position = le32 (cp + 4);
                out.cues.push_back (static_cast<std::int64_t> (sampleOffset != 0 ? sampleOffset : position));
            }
        }
        pos = body + static_cast<std::size_t> (size) + (size & 1u);
    }
}
} // namespace

WavPackMarks parseRiffMarks (const std::vector<std::uint8_t>& header, const std::vector<std::uint8_t>& trailer)
{
    // WavPack range l'en-tête et les chunks de queue dans des blocs différents, ou dans le même quand le fichier n'a qu'un bloc : on les lit comme une seule suite.
    std::vector<std::uint8_t> all (header);
    all.insert (all.end(), trailer.begin(), trailer.end());
    WavPackMarks m;
    if (all.size() >= 12 && std::memcmp (all.data(), "RIFF", 4) == 0 && std::memcmp (all.data() + 8, "WAVE", 4) == 0) walkChunks (all, 12, m);
    return m;
}

#if defined (NEPTUNE_HAS_WAVPACK)
namespace
{
struct Closer
{
    WavpackContext* wpc;
    ~Closer() { if (wpc != nullptr) WavpackCloseFile (wpc); }
};

WavPackInfo infoOf (WavpackContext* wpc)
{
    WavPackInfo i;
    i.channels = WavpackGetNumChannels (wpc);
    i.frames = WavpackGetNumSamples64 (wpc);
    i.sampleRate = static_cast<int> (WavpackGetSampleRate (wpc));
    i.bitsPerSample = WavpackGetBitsPerSample (wpc);
    i.isFloat = (WavpackGetMode (wpc) & MODE_FLOAT) != 0;
    if (i.channels < 1 || i.channels > 2) i.error = "WavPack: only mono and stereo files are supported";
    else if (i.frames <= 0) i.error = "WavPack: unknown or empty length";
    else if (i.sampleRate <= 0) i.error = "WavPack: invalid sample rate";
    else if (i.bitsPerSample < 1 || i.bitsPerSample > 32) i.error = "WavPack: unsupported bit depth";
    i.ok = i.error.empty();
    return i;
}

WavpackContext* openFile (const std::filesystem::path& path, int flags, std::string& error)
{
    char message[128] = {};
    const std::string utf8 = path.u8string();
    WavpackContext* wpc = WavpackOpenFileInput (utf8.c_str(), message, flags | OPEN_FILE_UTF8, 0);
    if (wpc == nullptr) error = message[0] != '\0' ? std::string ("WavPack: ") + message : "WavPack: cannot open file";
    return wpc;
}

void takeWrapper (WavpackContext* wpc, std::vector<std::uint8_t>& into)
{
    const std::uint32_t n = WavpackGetWrapperBytes (wpc);
    const unsigned char* p = WavpackGetWrapperData (wpc);
    if (n > 0 && p != nullptr) into.assign (p, p + n);
    WavpackFreeWrapper (wpc);
}
} // namespace

WavPackInfo probeWavPackFile (const std::filesystem::path& path)
{
    WavPackInfo i;
    std::string error;
    WavpackContext* wpc = openFile (path, 0, error);
    if (wpc == nullptr) { i.error = error; return i; }
    Closer closer { wpc };
    return infoOf (wpc);
}

WavPackData decodeWavPackFile (const std::filesystem::path& path)
{
    WavPackData d;
    std::string error;
    WavpackContext* wpc = openFile (path, OPEN_WRAPPER, error);
    if (wpc == nullptr) { d.info.error = error; return d; }
    Closer closer { wpc };
    d.info = infoOf (wpc);
    if (! d.info.ok) return d;
    takeWrapper (wpc, d.header);

    const int channels = d.info.channels;
    const std::int64_t total = d.info.frames;
    if (static_cast<std::uint64_t> (total) > (std::uint64_t (1) << 31) / static_cast<std::uint64_t> (channels)) { d.info.ok = false; d.info.error = "WavPack: file too long to load in memory"; return d; }
    d.samples.resize (static_cast<std::size_t> (total) * static_cast<std::size_t> (channels));
    constexpr std::uint32_t kBlock = 4096;
    std::vector<std::int32_t> block (static_cast<std::size_t> (kBlock) * static_cast<std::size_t> (channels));
    const double scale = d.info.isFloat ? 1.0 : 1.0 / std::ldexp (1.0, d.info.bitsPerSample - 1);
    std::int64_t done = 0;
    while (done < total)
    {
        const std::uint32_t want = static_cast<std::uint32_t> (std::min<std::int64_t> (kBlock, total - done));
        const std::uint32_t got = WavpackUnpackSamples (wpc, block.data(), want);
        if (got == 0) break;
        float* out = d.samples.data() + static_cast<std::size_t> (done) * static_cast<std::size_t> (channels);
        const std::size_t n = static_cast<std::size_t> (got) * static_cast<std::size_t> (channels);
        if (d.info.isFloat)
            for (std::size_t k = 0; k < n; ++k) { float v; std::memcpy (&v, &block[k], sizeof v); out[k] = v; }
        else
            for (std::size_t k = 0; k < n; ++k) out[k] = static_cast<float> (static_cast<double> (block[k]) * scale);
        done += got;
    }
    if (done < total || WavpackGetNumErrors (wpc) > 0)
    {
        d.info.ok = false;
        d.info.error = done < total ? "WavPack: file is truncated or damaged" : "WavPack: checksum errors while decoding";
        return d;
    }
    takeWrapper (wpc, d.trailer);   // les chunks de queue ne sont connus qu'après le dernier échantillon
    return d;
}

#else   // sans libwavpack : les fichiers sont reconnus mais refusés

WavPackInfo probeWavPackFile (const std::filesystem::path&)
{
    WavPackInfo i;
    i.error = "WavPack support is not built into this version";
    return i;
}

WavPackData decodeWavPackFile (const std::filesystem::path&)
{
    WavPackData d;
    d.info.error = "WavPack support is not built into this version";
    return d;
}
#endif

WavPackMarks readWavPackMarks (const std::filesystem::path& path)
{
    const auto d = decodeWavPackFile (path);
    if (! d.info.ok) return {};
    return parseRiffMarks (d.header, d.trailer);
}
} // namespace neptune::core
