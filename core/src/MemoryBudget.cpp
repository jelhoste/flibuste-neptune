#include "neptune/core/MemoryBudget.h"

#include "neptune/core/DiskStreamer.h"

#include <map>
#include <set>

#if defined (_WIN32)
 #define NOMINMAX
 #include <windows.h>
#elif defined (__linux__)
 #include <cstdio>
 #include <cstring>
#endif

namespace neptune::core::memory
{
const AudioFileInfo& FileInfoCache::info (const std::string& path)
{
    auto it = cache_.find (path);
    if (it == cache_.end())
        it = cache_.emplace (path, probeAudioFile (std::filesystem::u8path (path))).first;
    return it->second;
}

std::uint64_t estimateInstrumentBytes (const InstrumentDescription& d, FileInfoCache& cache, int* unreadable)
{
    const auto loaded = loadedZones (d);
    // Par fichier : le maximum de ce que ses zones gardent en mémoire (même règle que buildInstrument).
    std::map<std::string, std::int64_t> frames;
    std::map<std::string, const AudioFileInfo*> infos;
    int bad = 0;
    bool anyStreamed = false;
    for (std::size_t i = 0; i < d.zones.size() && i < loaded.size(); ++i)
    {
        if (! loaded[i]) continue;
        const auto& path = d.zones[i].samplePath;
        const auto& info = cache.info (path);
        if (! info.ok)
        {
            if (frames.emplace (path, -1).second) ++bad;
            continue;
        }
        const auto n = residentFramesNeeded (d, sanitised (d.zones[i]), info.channels, info.keptAsInt16, info.frames);
        auto it = frames.emplace (path, 0).first;
        it->second = std::max (it->second, n);
        infos[path] = &info;
        anyStreamed = anyStreamed || n < info.frames;
    }
    std::uint64_t total = 0;
    for (const auto& [path, n] : frames)
    {
        const auto it = infos.find (path);
        if (n < 0 || it == infos.end()) continue;
        total += static_cast<std::uint64_t> (n) * static_cast<std::uint64_t> (it->second->channels) * (it->second->keptAsInt16 ? 2u : 4u);
    }
    if (anyStreamed) total += streamBufferBytes();
    if (unreadable != nullptr) *unreadable = bad;
    return total;
}

std::uint64_t availablePhysicalBytes()
{
   #if defined (_WIN32)
    MEMORYSTATUSEX m {};
    m.dwLength = sizeof (m);
    return GlobalMemoryStatusEx (&m) ? static_cast<std::uint64_t> (m.ullAvailPhys) : 0;
   #elif defined (__linux__)
    std::FILE* f = std::fopen ("/proc/meminfo", "r");
    if (f == nullptr) return 0;
    std::uint64_t kb = 0;
    char line[256];
    while (std::fgets (line, sizeof line, f) != nullptr)
    {
        unsigned long long v = 0;
        if (std::sscanf (line, "MemAvailable: %llu kB", &v) == 1) { kb = v; break; }
    }
    std::fclose (f);
    return kb * 1024ull;
   #else
    return 0;
   #endif
}

Limits limitsFor (std::uint64_t headroomBytes)
{
    Limits l;
    if (headroomBytes == 0)
    {
        l.warnBytes = fallbackWarnBytes;
        l.refuseBytes = fallbackRefuseBytes;
        return l;
    }
    l.fromAvailable = true;
    l.warnBytes = static_cast<std::uint64_t> (static_cast<double> (headroomBytes) * warnFraction);
    l.refuseBytes = static_cast<std::uint64_t> (static_cast<double> (headroomBytes) * refuseFraction);
    return l;
}

Verdict judge (std::uint64_t instrumentBytes, const Limits& l)
{
    if (instrumentBytes > l.refuseBytes) return Verdict::Refuse;
    if (instrumentBytes > l.warnBytes) return Verdict::Warn;
    return Verdict::Ok;
}
} // namespace neptune::core::memory
