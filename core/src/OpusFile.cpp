#include "neptune/core/OpusFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

#if defined (NEPTUNE_HAS_OPUS)
 #include <opus.h>
#endif

namespace neptune::core
{
namespace
{
// ----- démultiplexeur Ogg minimal : un seul flux logique (le premier), paquets reconstitués à travers les pages -----
struct OggPacket
{
    std::vector<unsigned char> bytes;
};

struct OggStream
{
    std::vector<OggPacket> packets;
    std::int64_t lastGranule = -1;
    std::string error;
};

std::uint32_t le32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t> (p[3]) << 24); }
std::int64_t le64 (const unsigned char* p) { return static_cast<std::int64_t> (static_cast<std::uint64_t> (le32 (p)) | (static_cast<std::uint64_t> (le32 (p + 4)) << 32)); }

bool readAll (const std::filesystem::path& path, std::vector<unsigned char>& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    out.assign (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
    return true;
}

// `wantAudio` faux : s'arrête après les deux paquets d'en-tête (probe rapide), mais lit quand même la dernière page.
OggStream demux (const std::vector<unsigned char>& file, bool wantAudio)
{
    OggStream s;
    std::size_t pos = 0;
    bool haveSerial = false;
    std::uint32_t serial = 0;
    std::vector<unsigned char> partial;
    while (pos + 27 <= file.size())
    {
        const unsigned char* h = file.data() + pos;
        if (std::memcmp (h, "OggS", 4) != 0)
        {
            // pas une page : on avance d'un octet jusqu'à la prochaine
            ++pos;
            continue;
        }
        const int nseg = h[26];
        if (pos + 27 + static_cast<std::size_t> (nseg) > file.size()) break;
        const unsigned char* lace = h + 27;
        std::size_t body = 0;
        for (int i = 0; i < nseg; ++i) body += lace[i];
        const std::size_t total = 27 + static_cast<std::size_t> (nseg) + body;
        if (pos + total > file.size()) break;   // page tronquée : on garde ce qui précède
        const std::uint32_t ser = le32 (h + 14);
        const bool bos = (h[5] & 0x02) != 0;
        if (! haveSerial && bos) { haveSerial = true; serial = ser; }
        if (haveSerial && ser == serial)
        {
            const std::int64_t granule = le64 (h + 6);
            const unsigned char* data = lace + nseg;
            if ((h[5] & 0x01) == 0) partial.clear();   // pas une continuation : un paquet incomplet précédent est perdu
            for (int i = 0; i < nseg; ++i)
            {
                partial.insert (partial.end(), data, data + lace[i]);
                data += lace[i];
                if (lace[i] < 255)
                {
                    OggPacket p;
                    p.bytes = std::move (partial);
                    partial.clear();
                    if (wantAudio || s.packets.size() < 2) s.packets.push_back (std::move (p));
                }
            }
            if (granule >= 0) s.lastGranule = granule;
            if ((h[5] & 0x04) != 0) break;   // fin de flux
        }
        pos += total;
    }
    return s;
}

struct Head
{
    int channels = 0;
    int preSkip = 0;
    int inputRate = 0;
    double gainDb = 0.0;
};

bool parseHead (const OggStream& s, Head& h, std::string& error)
{
    if (s.packets.empty() || s.packets[0].bytes.size() < 19 || std::memcmp (s.packets[0].bytes.data(), "OpusHead", 8) != 0)
    {
        error = "not an Ogg Opus file";
        return false;
    }
    const auto& b = s.packets[0].bytes;
    if ((b[8] >> 4) != 0) { error = "unsupported Opus version"; return false; }
    h.channels = b[9];
    h.preSkip = b[10] | (b[11] << 8);
    h.inputRate = static_cast<int> (le32 (b.data() + 12));
    h.gainDb = static_cast<double> (static_cast<std::int16_t> (b[16] | (b[17] << 8))) / 256.0;
    if (b[18] != 0) { error = "unsupported Opus channel mapping (surround): only mono and stereo are read"; return false; }
    if (h.channels < 1 || h.channels > 2) { error = "unsupported channel count: " + std::to_string (h.channels) + " (mono and stereo only)"; return false; }
    return true;
}

OpusFileInfo infoFrom (const OggStream& s, const Head& h)
{
    OpusFileInfo i;
    i.channels = h.channels;
    i.inputRate = h.inputRate;
    i.outputGainDb = h.gainDb;
    i.frames = s.lastGranule - h.preSkip;
    if (i.frames <= 0) { i.error = "empty Opus file"; return i; }
    i.ok = true;
    return i;
}
} // namespace

bool opusDecodingAvailable() noexcept
{
   #if defined (NEPTUNE_HAS_OPUS)
    return true;
   #else
    return false;
   #endif
}

bool isOpusFile (const std::filesystem::path& path)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    unsigned char b[64] = {};
    f.read (reinterpret_cast<char*> (b), sizeof (b));
    if (f.gcount() < 47 || std::memcmp (b, "OggS", 4) != 0) return false;
    const int nseg = b[26];
    return 27 + nseg + 8 <= f.gcount() && std::memcmp (b + 27 + nseg, "OpusHead", 8) == 0;
}

OpusFileInfo probeOpusFile (const std::filesystem::path& path)
{
    std::vector<unsigned char> file;
    OpusFileInfo bad;
    if (! readAll (path, file)) { bad.error = "cannot open Opus file"; return bad; }
    const auto s = demux (file, false);
    Head h;
    if (! parseHead (s, h, bad.error)) return bad;
    return infoFrom (s, h);
}

OpusFileData decodeOpusFile (const std::filesystem::path& path)
{
    OpusFileData out;
   #if ! defined (NEPTUNE_HAS_OPUS)
    (void) path;
    out.info.error = "Opus support is not built into this version";
    return out;
   #else
    std::vector<unsigned char> file;
    if (! readAll (path, file)) { out.info.error = "cannot open Opus file"; return out; }
    const auto s = demux (file, true);
    Head h;
    if (! parseHead (s, h, out.info.error)) return out;
    out.info = infoFrom (s, h);
    if (! out.info.ok) return out;
    if (s.packets.size() < 3) { out.info.ok = false; out.info.error = "Opus file has no audio"; return out; }

    int err = 0;
    OpusDecoder* dec = opus_decoder_create (48000, h.channels, &err);
    if (dec == nullptr || err != OPUS_OK) { out.info.ok = false; out.info.error = "cannot create the Opus decoder"; return out; }
    struct Closer { OpusDecoder* d; ~Closer() { opus_decoder_destroy (d); } } closer { dec };

    const std::int64_t ch = h.channels, total = out.info.frames;
    constexpr int kMaxFrame = 5760;   // 120 ms à 48 kHz
    std::vector<float> pcm (static_cast<std::size_t> (kMaxFrame) * static_cast<std::size_t> (ch));
    out.samples.assign (static_cast<std::size_t> (total * ch), 0.0f);
    std::int64_t skip = h.preSkip, written = 0;
    for (std::size_t i = 2; i < s.packets.size() && written < total; ++i)
    {
        const auto& p = s.packets[i].bytes;
        const int n = opus_decode_float (dec, p.empty() ? nullptr : p.data(), static_cast<opus_int32> (p.size()), pcm.data(), kMaxFrame, 0);
        if (n < 0) continue;   // paquet corrompu : on passe (le trou reste muet)
        std::int64_t from = 0;
        if (skip > 0) { from = std::min<std::int64_t> (skip, n); skip -= from; }
        const std::int64_t take = std::min<std::int64_t> (n - from, total - written);
        std::copy (pcm.begin() + static_cast<std::ptrdiff_t> (from * ch), pcm.begin() + static_cast<std::ptrdiff_t> ((from + take) * ch),
                   out.samples.begin() + static_cast<std::ptrdiff_t> (written * ch));
        written += take;
    }
    if (written < total / 2) { out.info.ok = false; out.info.error = "Opus file is truncated or unreadable"; return out; }
    if (h.gainDb != 0.0)
    {
        const auto g = static_cast<float> (std::pow (10.0, h.gainDb / 20.0));
        for (auto& v : out.samples) v *= g;
    }
    return out;
   #endif
}
} // namespace neptune::core
