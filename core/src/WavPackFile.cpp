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

WavPackData decodeWavPackFile (const std::filesystem::path& path, std::int64_t maxFrames)
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
    const std::int64_t total = maxFrames > 0 ? std::min (maxFrames, d.info.frames) : d.info.frames;
    const bool partial = total < d.info.frames;
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
    d.decodedFrames = done;
    if (! partial) takeWrapper (wpc, d.trailer);   // les chunks de queue ne sont connus qu'après le dernier échantillon
    return d;
}

struct WavPackReader::Impl
{
    WavpackContext* wpc = nullptr;
    std::vector<std::int32_t> block;
    double scale = 1.0;
};

WavPackReader::WavPackReader() : impl_ (new Impl) {}
WavPackReader::~WavPackReader() { close(); delete impl_; }
bool WavPackReader::isOpen() const noexcept { return impl_->wpc != nullptr; }

void WavPackReader::close()
{
    if (impl_->wpc != nullptr) { WavpackCloseFile (impl_->wpc); impl_->wpc = nullptr; }
    impl_->block.clear();
    impl_->block.shrink_to_fit();
    info_ = {};
}

bool WavPackReader::open (const std::filesystem::path& path, std::string* error)
{
    close();
    std::string message;
    impl_->wpc = openFile (path, 0, message);
    if (impl_->wpc == nullptr) { if (error != nullptr) *error = message; return false; }
    info_ = infoOf (impl_->wpc);
    if (! info_.ok)
    {
        if (error != nullptr) *error = info_.error;
        close();
        return false;
    }
    impl_->scale = info_.isFloat ? 1.0 : 1.0 / std::ldexp (1.0, info_.bitsPerSample - 1);
    return true;
}

bool WavPackReader::seek (std::int64_t frame)
{
    if (impl_->wpc == nullptr || frame < 0 || frame > info_.frames) return false;
    return WavpackSeekSample64 (impl_->wpc, frame) != 0;
}

std::int64_t WavPackReader::read (float* out, std::int64_t frames)
{
    if (impl_->wpc == nullptr || frames <= 0) return 0;
    const std::size_t channels = static_cast<std::size_t> (info_.channels);
    constexpr std::int64_t kBlock = 4096;
    impl_->block.resize (static_cast<std::size_t> (kBlock) * channels);
    std::int64_t done = 0;
    while (done < frames)
    {
        const std::uint32_t want = static_cast<std::uint32_t> (std::min<std::int64_t> (kBlock, frames - done));
        const std::uint32_t got = WavpackUnpackSamples (impl_->wpc, impl_->block.data(), want);
        if (got == 0) break;
        float* dst = out + static_cast<std::size_t> (done) * channels;
        const std::size_t n = static_cast<std::size_t> (got) * channels;
        if (info_.isFloat)
            for (std::size_t k = 0; k < n; ++k) { float v; std::memcpy (&v, &impl_->block[k], sizeof v); dst[k] = v; }
        else
            for (std::size_t k = 0; k < n; ++k) dst[k] = static_cast<float> (static_cast<double> (impl_->block[k]) * impl_->scale);
        done += got;
    }
    return done;
}

#else   // sans libwavpack : les fichiers sont reconnus mais refusés

WavPackInfo probeWavPackFile (const std::filesystem::path&)
{
    WavPackInfo i;
    i.error = "WavPack support is not built into this version";
    return i;
}

WavPackData decodeWavPackFile (const std::filesystem::path&, std::int64_t)
{
    WavPackData d;
    d.info.error = "WavPack support is not built into this version";
    return d;
}

struct WavPackReader::Impl {};
WavPackReader::WavPackReader() : impl_ (new Impl) {}
WavPackReader::~WavPackReader() { delete impl_; }
bool WavPackReader::isOpen() const noexcept { return false; }
void WavPackReader::close() { info_ = {}; }
bool WavPackReader::open (const std::filesystem::path&, std::string* error)
{
    if (error != nullptr) *error = "WavPack support is not built into this version";
    return false;
}
bool WavPackReader::seek (std::int64_t) { return false; }
std::int64_t WavPackReader::read (float*, std::int64_t) { return 0; }
#endif

// Les repères (smpl, cue) sont dans l'enveloppe RIFF du fichier : sous-blocs 0x21 (en-tête) et 0x22 (queue) des blocs WavPack. On parcourt les blocs en sautant les
// données audio, sans rien décoder : lire les repères d'un orgue de 3 000 fichiers doit prendre des secondes, pas des minutes.
WavPackMarks readWavPackMarks (const std::filesystem::path& path)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return {};
    std::vector<std::uint8_t> header, trailer;
    std::uint8_t h[32];
    while (f.read (reinterpret_cast<char*> (h), 32))
    {
        if (std::memcmp (h, "wvpk", 4) != 0) break;
        const std::uint32_t ckSize = le32 (h + 4);
        if (ckSize < 24) break;
        std::uint64_t left = ckSize - 24;   // octets de sous-blocs qui restent dans ce bloc
        while (left >= 2)
        {
            std::uint8_t sub[4];
            if (! f.read (reinterpret_cast<char*> (sub), 2)) return parseRiffMarks (header, trailer);
            std::uint64_t words = sub[1], consumed = 2;
            if ((sub[0] & 0x80) != 0)
            {
                if (! f.read (reinterpret_cast<char*> (sub + 2), 2)) return parseRiffMarks (header, trailer);
                words = sub[1] | (static_cast<std::uint64_t> (sub[2]) << 8) | (static_cast<std::uint64_t> (sub[3]) << 16);
                consumed = 4;
            }
            const std::uint64_t bytes = words * 2;
            if (consumed + bytes > left) return parseRiffMarks (header, trailer);
            const int function = sub[0] & 0x3f;
            if (function == 0x21 || function == 0x22)
            {
                std::vector<std::uint8_t> data (static_cast<std::size_t> (bytes));
                if (bytes > 0 && ! f.read (reinterpret_cast<char*> (data.data()), static_cast<std::streamsize> (bytes))) return parseRiffMarks (header, trailer);
                if ((sub[0] & 0x40) != 0 && ! data.empty()) data.pop_back();   // taille impaire : le dernier octet n'est que du bourrage
                (function == 0x21 ? header : trailer).insert ((function == 0x21 ? header : trailer).end(), data.begin(), data.end());
            }
            else f.seekg (static_cast<std::streamoff> (bytes), std::ios::cur);
            left -= consumed + bytes;
        }
        if (left > 0) f.seekg (static_cast<std::streamoff> (left), std::ios::cur);
    }
    return parseRiffMarks (header, trailer);
}
} // namespace neptune::core
