#include "neptune/core/SampleMeta.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "neptune/core/FileUtil.h"
#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
namespace
{
enum class Container { Other, Riff, Iff };

struct ChunkRef { char id[4]; std::uint32_t size; std::uint64_t dataPos; };   // dataPos : début des données

std::uint32_t rd32 (const unsigned char* p, bool big) noexcept
{
    return big ? (static_cast<std::uint32_t> (p[0]) << 24) | (static_cast<std::uint32_t> (p[1]) << 16) | (static_cast<std::uint32_t> (p[2]) << 8) | p[3]
               : (static_cast<std::uint32_t> (p[3]) << 24) | (static_cast<std::uint32_t> (p[2]) << 16) | (static_cast<std::uint32_t> (p[1]) << 8) | p[0];
}
void wr32 (unsigned char* p, std::uint32_t v, bool big) noexcept
{
    if (big) { p[0] = static_cast<unsigned char> (v >> 24); p[1] = static_cast<unsigned char> (v >> 16); p[2] = static_cast<unsigned char> (v >> 8); p[3] = static_cast<unsigned char> (v); }
    else { p[3] = static_cast<unsigned char> (v >> 24); p[2] = static_cast<unsigned char> (v >> 16); p[1] = static_cast<unsigned char> (v >> 8); p[0] = static_cast<unsigned char> (v); }
}

// Reconnaît WAV (RIFF/WAVE) et AIFF (FORM/AIFF ou AIFC) ; liste les chunks de premier niveau. Faux si ce n'est ni l'un ni l'autre ou si la structure est incohérente.
bool scan (std::ifstream& f, std::uint64_t fileSize, Container& kind, std::vector<ChunkRef>& chunks)
{
    unsigned char head[12];
    f.seekg (0);
    if (! f.read (reinterpret_cast<char*> (head), 12)) return false;
    if (std::memcmp (head, "RIFF", 4) == 0 && std::memcmp (head + 8, "WAVE", 4) == 0) kind = Container::Riff;
    else if (std::memcmp (head, "FORM", 4) == 0 && (std::memcmp (head + 8, "AIFF", 4) == 0 || std::memcmp (head + 8, "AIFC", 4) == 0)) kind = Container::Iff;
    else return false;
    const bool big = kind == Container::Iff;
    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize)
    {
        unsigned char h[8];
        f.seekg (static_cast<std::streamoff> (pos));
        if (! f.read (reinterpret_cast<char*> (h), 8)) return false;
        ChunkRef c {};
        std::memcpy (c.id, h, 4);
        c.size = rd32 (h + 4, big);
        c.dataPos = pos + 8;
        if (c.dataPos + c.size > fileSize) return false;   // tronqué ou RF64 : on ne touche pas
        chunks.push_back (c);
        pos = c.dataPos + c.size + (c.size & 1u);
    }
    return ! chunks.empty();
}

const char* chunkId (Container k) noexcept { return k == Container::Iff ? "NPTN" : "nptn"; }

std::filesystem::path sidecarPath (const std::filesystem::path& p) { auto s = p; s += ".neptune.json"; return s; }
} // namespace

std::string sampleMetaToJson (const SampleMeta& m)
{
    nlohmann::json o;
    o["schema_version"] = 1;
    if (m.hasVolumeMarker) o["volume_marker_ms"] = m.volumeMarkerMs;
    return o.dump();
}

bool sampleMetaFromJson (const std::string& text, SampleMeta& out)
{
    const auto doc = nlohmann::json::parse (text, nullptr, false);
    if (! doc.is_object()) return false;
    out = {};
    if (const auto it = doc.find ("volume_marker_ms"); it != doc.end() && it->is_number())
    {
        out.hasVolumeMarker = true;
        out.volumeMarkerMs = std::max (0.0, it->get<double>());
    }
    return true;
}

bool readSampleMeta (const std::filesystem::path& path, SampleMeta& out, MetaStore* where)
{
    out = {};
    if (where != nullptr) *where = MetaStore::None;
    std::error_code ec;
    const auto size = std::filesystem::file_size (path, ec);
    if (ec) return false;
    std::ifstream f (path, std::ios::binary);
    Container kind = Container::Other;
    std::vector<ChunkRef> chunks;
    if (f && scan (f, size, kind, chunks))
        for (const auto& c : chunks)
            if (std::memcmp (c.id, chunkId (kind), 4) == 0 && c.size < (1u << 20))
            {
                std::string text (c.size, '\0');
                f.seekg (static_cast<std::streamoff> (c.dataPos));
                if (f.read (text.data(), static_cast<std::streamsize> (text.size())) && sampleMetaFromJson (text, out))
                {
                    if (where != nullptr) *where = MetaStore::Chunk;
                    return true;
                }
            }
    std::string text;
    if (fileutil::readAll (sidecarPath (path), text) && sampleMetaFromJson (text, out))
    {
        if (where != nullptr) *where = MetaStore::Sidecar;
        return true;
    }
    return false;
}

bool writeSampleMeta (const std::filesystem::path& path, const SampleMeta& meta, MetaStore* outStore, std::string* error)
{
    if (outStore != nullptr) *outStore = MetaStore::None;
    const bool empty = ! meta.hasVolumeMarker;
    std::error_code ec;
    const auto size = std::filesystem::file_size (path, ec);
    if (ec) { if (error != nullptr) *error = "file not found"; return false; }

    // 1. dans le fichier (WAV / AIFF), en recopiant tout le reste tel quel puis en remplaçant le fichier d'un bloc
    bool wrote = false;
    {
        std::ifstream in (path, std::ios::binary);
        Container kind = Container::Other;
        std::vector<ChunkRef> chunks;
        if (in && scan (in, size, kind, chunks))
        {
            const bool big = kind == Container::Iff;
            bool had = false;
            for (const auto& c : chunks) had = had || std::memcmp (c.id, chunkId (kind), 4) == 0;
            if (empty && ! had)
            {
                // rien à retirer du fichier ; reste un éventuel fichier voisin
            }
            else
            {
                auto tmp = path;
                tmp += ".nptn.tmp";
                std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
                bool ok = static_cast<bool> (out);
                unsigned char head[12];
                in.seekg (0);
                ok = ok && in.read (reinterpret_cast<char*> (head), 12);
                std::uint64_t total = 4;   // taille du conteneur : tout ce qui suit les 8 premiers octets
                if (ok) out.write (reinterpret_cast<const char*> (head), 12);
                std::vector<char> buf (1 << 16);
                for (const auto& c : chunks)
                {
                    if (! ok) break;
                    if (std::memcmp (c.id, chunkId (kind), 4) == 0) continue;   // l'ancien chunk Neptune disparaît
                    unsigned char h[8];
                    std::memcpy (h, c.id, 4);
                    wr32 (h + 4, c.size, big);
                    out.write (reinterpret_cast<const char*> (h), 8);
                    in.seekg (static_cast<std::streamoff> (c.dataPos));
                    std::uint64_t left = c.size;
                    while (left > 0 && ok)
                    {
                        const auto n = static_cast<std::streamsize> (std::min<std::uint64_t> (left, buf.size()));
                        ok = static_cast<bool> (in.read (buf.data(), n));
                        if (ok) out.write (buf.data(), n);
                        left -= static_cast<std::uint64_t> (n);
                    }
                    if (c.size & 1u) out.put ('\0');
                    total += 8 + c.size + (c.size & 1u);
                }
                if (ok && ! empty)
                {
                    const std::string json = sampleMetaToJson (meta);
                    unsigned char h[8];
                    std::memcpy (h, chunkId (kind), 4);
                    wr32 (h + 4, static_cast<std::uint32_t> (json.size()), big);
                    out.write (reinterpret_cast<const char*> (h), 8);
                    out.write (json.data(), static_cast<std::streamsize> (json.size()));
                    if (json.size() & 1u) out.put ('\0');
                    total += 8 + json.size() + (json.size() & 1u);
                }
                if (ok && total < 0xFFFFFFF0ull)
                {
                    unsigned char s[4];
                    wr32 (s, static_cast<std::uint32_t> (total), big);
                    out.seekp (4);
                    out.write (reinterpret_cast<const char*> (s), 4);
                }
                else ok = false;
                out.flush();
                ok = ok && static_cast<bool> (out);
                out.close();
                in.close();
                if (ok)
                {
                    // vérification : même longueur et mêmes canaux que l'original avant de remplacer
                    const auto a = probeAudioFile (path), b = probeAudioFile (tmp);
                    ok = a.ok && b.ok && a.frames == b.frames && a.channels == b.channels;
                }
                if (ok)
                {
                    std::filesystem::rename (tmp, path, ec);
                    ok = ! ec;
                }
                if (! ok) std::filesystem::remove (tmp, ec);
                wrote = ok;
                if (wrote && outStore != nullptr) *outStore = empty ? MetaStore::None : MetaStore::Chunk;
            }
        }
    }

    // 2. le fichier voisin : pour les autres formats, ou quand le fichier n'a pas pu être réécrit
    const auto side = sidecarPath (path);
    if (wrote || (empty && ! std::filesystem::exists (side)))
    {
        if (wrote && std::filesystem::exists (side)) std::filesystem::remove (side, ec);   // une seule source de vérité
        if (wrote || empty) return true;
    }
    if (empty)
    {
        std::filesystem::remove (side, ec);
        if (outStore != nullptr) *outStore = MetaStore::None;
        return true;
    }
    if (! fileutil::writeAtomic (side, sampleMetaToJson (meta), error)) return false;
    if (outStore != nullptr) *outStore = MetaStore::Sidecar;
    return true;
}
} // namespace neptune::core
