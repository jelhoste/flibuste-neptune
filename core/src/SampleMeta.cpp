#include "neptune/core/SampleMeta.h"
#include "neptune/core/WavPackFile.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>
#include <sndfile.h>

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
    if (m.sampleRate > 0.0) o["sample_rate"] = m.sampleRate;
    if (m.hasVolumeMarker) o["volume_marker_ms"] = m.volumeMarkerMs;
    if (m.hasRegion) { o["start_frame"] = m.startFrame; o["end_frame"] = m.endFrame; }
    if (m.hasRoot) { o["root_note"] = m.rootNote; if (m.tuneCents != 0.0) o["tune_cents"] = m.tuneCents; }
    if (! m.loops.empty())
    {
        nlohmann::json loops = nlohmann::json::array();
        for (const auto& l : m.loops)
        {
            nlohmann::json lo;
            lo["start"] = l.start;
            lo["end"] = l.end;
            if (l.crossfade != 0) lo["crossfade"] = l.crossfade;
            if (l.repeats != 0) lo["repeats"] = l.repeats;
            loops.push_back (lo);
        }
        o["loops"] = loops;
    }
    return o.dump();
}

bool sampleMetaFromJson (const std::string& text, SampleMeta& out)
{
    const auto doc = nlohmann::json::parse (text, nullptr, false);
    if (! doc.is_object()) return false;
    out = {};
    if (const auto it = doc.find ("sample_rate"); it != doc.end() && it->is_number()) out.sampleRate = it->get<double>();
    if (const auto it = doc.find ("volume_marker_ms"); it != doc.end() && it->is_number())
    {
        out.hasVolumeMarker = true;
        out.volumeMarkerMs = std::max (0.0, it->get<double>());
    }
    if (const auto s = doc.find ("start_frame"); s != doc.end() && s->is_number_integer())
    {
        out.hasRegion = true;
        out.startFrame = std::max<std::int64_t> (0, s->get<std::int64_t>());
        if (const auto e = doc.find ("end_frame"); e != doc.end() && e->is_number_integer()) out.endFrame = e->get<std::int64_t>();
    }
    if (const auto r = doc.find ("root_note"); r != doc.end() && r->is_number_integer())
    {
        out.hasRoot = true;
        out.rootNote = std::clamp (r->get<int>(), 0, 127);
        if (const auto t = doc.find ("tune_cents"); t != doc.end() && t->is_number()) out.tuneCents = std::clamp (t->get<double>(), -100.0, 100.0);
    }
    if (const auto it = doc.find ("loops"); it != doc.end() && it->is_array())
        for (const auto& lo : *it)
        {
            if (! lo.is_object() || out.loops.size() >= 4) continue;
            MetaLoop l;
            if (const auto v = lo.find ("start"); v != lo.end() && v->is_number_integer()) l.start = std::max<std::int64_t> (0, v->get<std::int64_t>());
            if (const auto v = lo.find ("end"); v != lo.end() && v->is_number_integer()) l.end = v->get<std::int64_t>();
            if (const auto v = lo.find ("crossfade"); v != lo.end() && v->is_number_integer()) l.crossfade = std::max<std::int64_t> (0, v->get<std::int64_t>());
            if (const auto v = lo.find ("repeats"); v != lo.end() && v->is_number_integer()) l.repeats = std::clamp (v->get<int>(), 0, 1000);
            out.loops.push_back (l);
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
    const bool empty = meta.isEmpty();
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

bool readReleaseMarker (const std::filesystem::path& path, std::int64_t& frame)
{
    if (isWavPackFile (path))
    {
        const auto marks = readWavPackMarks (path);
        if (marks.cues.empty()) return false;
        frame = *std::max_element (marks.cues.begin(), marks.cues.end());
        return true;
    }
    SF_INFO info {};
   #if defined (_WIN32)
    SNDFILE* f = sf_wchar_open (path.c_str(), SFM_READ, &info);
   #else
    SNDFILE* f = sf_open (path.c_str(), SFM_READ, &info);
   #endif
    if (f == nullptr) return false;
    bool found = false;
    SF_CUES cues {};
    if (sf_command (f, SFC_GET_CUE, &cues, sizeof cues) == SF_TRUE)
        for (std::uint32_t i = 0; i < cues.cue_count && i < 100; ++i)
        {
            const auto position = static_cast<std::int64_t> (cues.cue_points[i].sample_offset);
            if (! found || position > frame) { frame = position; found = true; }
        }
    sf_close (f);
    return found;
}

bool readFileMarks (const std::filesystem::path& path, SampleMeta& out)
{
    out = {};
    SampleMeta ours;
    const bool haveOurs = readSampleMeta (path, ours);
    if (haveOurs) out = ours;

    // chunk d'instrument standard (smpl d'un WAV, instrument d'un AIFF) : ce que Neptune n'a pas écrit
    double fileRate = 0.0;
    if (isWavPackFile (path))
    {
        // WavPack : le chunk smpl du WAV d'origine est gardé dans l'enveloppe RIFF du fichier
        const auto info = probeWavPackFile (path);
        if (! info.ok) { if (! haveOurs) return false; }
        else
        {
            fileRate = static_cast<double> (info.sampleRate);
            const auto marks = readWavPackMarks (path);
            if (! out.hasRoot && marks.hasRoot)
            {
                out.hasRoot = true;
                out.rootNote = marks.rootNote;
                out.tuneCents = std::clamp (marks.detuneCents, -50.0, 50.0);
            }
            if (out.loops.empty())
                for (const auto& lp : marks.loops)
                {
                    if (out.loops.size() >= 4 || lp.second <= lp.first) continue;
                    MetaLoop l;
                    l.start = lp.first;
                    l.end = lp.second + 1;   // smpl : le dernier échantillon de la boucle est inclus ; Neptune : fin exclusive
                    if (! out.loops.empty() && (l.end <= (out.loops.back().end < 0 ? l.end : out.loops.back().end))) continue;
                    out.loops.push_back (l);
                }
        }
    }
    else
    {
        SF_INFO info {};
       #if defined (_WIN32)
        SNDFILE* f = sf_wchar_open (path.c_str(), SFM_READ, &info);
       #else
        SNDFILE* f = sf_open (path.c_str(), SFM_READ, &info);
       #endif
        if (f != nullptr)
        {
            fileRate = static_cast<double> (info.samplerate);
            SF_INSTRUMENT inst {};
            if (sf_command (f, SFC_GET_INSTRUMENT, &inst, sizeof inst) == SF_TRUE)
            {
                if (! out.hasRoot && inst.basenote >= 0 && inst.basenote <= 127)
                {
                    out.hasRoot = true;
                    out.rootNote = inst.basenote;
                    out.tuneCents = std::clamp (static_cast<double> (inst.detune), -50.0, 50.0);
                }
                if (out.loops.empty())
                    for (int i = 0; i < inst.loop_count && i < 16 && out.loops.size() < 4; ++i)
                    {
                        const auto& lp = inst.loops[i];
                        if (lp.mode == SF_LOOP_NONE || lp.end <= lp.start) continue;
                        MetaLoop l;
                        l.start = static_cast<std::int64_t> (lp.start);
                        l.end = static_cast<std::int64_t> (lp.end) + 1;   // smpl : le dernier échantillon de la boucle est inclus ; Neptune : fin exclusive
                        if (! out.loops.empty() && (l.end <= (out.loops.back().end < 0 ? l.end : out.loops.back().end))) continue;   // les boucles se suivent dans le fichier
                        out.loops.push_back (l);
                    }
            }
            sf_close (f);
        }
        else if (! haveOurs) return false;
    }
    // les positions écrites pour une autre fréquence suivent le fichier tel qu'il est maintenant
    if (haveOurs && ours.sampleRate > 0.0 && fileRate > 0.0 && std::abs (ours.sampleRate - fileRate) > 0.5)
    {
        const double k = fileRate / ours.sampleRate;
        auto scale = [k] (std::int64_t v) { return v < 0 ? v : static_cast<std::int64_t> (std::llround (static_cast<double> (v) * k)); };
        for (auto& l : out.loops) { l.start = scale (l.start); l.end = scale (l.end); l.crossfade = scale (l.crossfade); }
        out.startFrame = scale (out.startFrame);
        out.endFrame = scale (out.endFrame);
    }
    if (fileRate > 0.0) out.sampleRate = fileRate;
    return ! out.isEmpty();
}
} // namespace neptune::core
