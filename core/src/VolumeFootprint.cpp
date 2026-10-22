#include "neptune/core/VolumeFootprint.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/LevelTracker.h"
#include "neptune/core/SampleLoader.h"

#ifdef NEPTUNE_HAS_EBUR128
 #include <ebur128.h>
#endif

namespace neptune::core
{
namespace
{
constexpr float kFloorDb = -120.0f;
float toDb (double amplitude) noexcept { return amplitude <= 1.0e-6 ? kFloorDb : static_cast<float> (20.0 * std::log10 (amplitude)); }
}

VolumeFootprint computeFootprint (const std::filesystem::path& path, const std::atomic<bool>* cancel)
{
    VolumeFootprint out;
    AudioStreamReader reader;
    if (! reader.open (path, &out.error)) return out;
    const auto info = probeAudioFile (path);
    out.sampleRate = info.ok && info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const int ch = reader.channels();
    out.totalFrames = reader.totalFrames();
    out.hopFrames = std::max<std::int64_t> (1, static_cast<std::int64_t> (std::llround (out.sampleRate * 0.010)));
    const auto points = static_cast<std::size_t> ((out.totalFrames + out.hopFrames - 1) / out.hopFrames);
    if (points == 0) { out.error = "empty file"; return out; }
    out.peakDb.assign (points, kFloorDb);
    out.rmsDb.assign (points, kFloorDb);

   #ifdef NEPTUNE_HAS_EBUR128
    ebur128_state* state = ebur128_init (static_cast<unsigned> (ch), static_cast<unsigned long> (out.sampleRate), EBUR128_MODE_M | EBUR128_MODE_I);
    if (state != nullptr)
    {
        ebur128_set_max_window (state, 400);
        out.lufs.assign (points, kFloorDb);
    }
   #endif

    std::vector<float> buf (static_cast<std::size_t> (out.hopFrames) * static_cast<std::size_t> (ch));
    std::vector<double> hopEnergy (points, 0.0);   // somme des carrés par point (tous canaux) pour le RMS glissant
    for (std::size_t p = 0; p < points; ++p)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
        {
           #ifdef NEPTUNE_HAS_EBUR128
            if (state != nullptr) ebur128_destroy (&state);
           #endif
            out.error = "cancelled";
            out.peakDb.clear(); out.rmsDb.clear(); out.lufs.clear();
            return out;
        }
        const auto got = reader.read (buf.data(), out.hopFrames);
        if (got <= 0) break;
        float peak = 0.0f;
        double energy = 0.0;
        for (std::int64_t i = 0; i < got * ch; ++i) { const float v = buf[static_cast<std::size_t> (i)]; peak = std::max (peak, std::fabs (v)); energy += static_cast<double> (v) * v; }
        out.peakDb[p] = toDb (peak);
        hopEnergy[p] = energy / static_cast<double> (ch);   // énergie moyenne par canal : le RMS d'un signal identique sur les deux voies reste celui d'une voie
        // RMS sur 20 ms : ce point et le précédent
        const double e2 = hopEnergy[p] + (p > 0 ? hopEnergy[p - 1] : 0.0);
        const double frames2 = static_cast<double> (got) + (p > 0 ? static_cast<double> (out.hopFrames) : 0.0);
        out.rmsDb[p] = toDb (std::sqrt (e2 / frames2));
       #ifdef NEPTUNE_HAS_EBUR128
        if (state != nullptr)
        {
            ebur128_add_frames_float (state, buf.data(), static_cast<std::size_t> (got));
            double l = 0.0;
            if (ebur128_loudness_window (state, 100, &l) == EBUR128_SUCCESS && std::isfinite (l)) out.lufs[p] = static_cast<float> (std::max (l, -120.0));
        }
       #endif
    }
   #ifdef NEPTUNE_HAS_EBUR128
    if (state != nullptr) ebur128_destroy (&state);
   #endif
    out.ok = true;
    return out;
}

bool measureLevelAt (const std::filesystem::path& path, std::int64_t frame, int meter, double& db)
{
    AudioStreamReader reader;
    if (! reader.open (path)) return false;
    const auto info = probeAudioFile (path);
    const double rate = info.ok && info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    frame = std::clamp<std::int64_t> (frame, 0, reader.totalFrames() - 1);
    const std::int64_t from = std::max<std::int64_t> (0, frame - static_cast<std::int64_t> (rate));   // 1 s d'amorce : les filtres et lissages se stabilisent
    if (! reader.seek (from)) return false;
    LevelCoefs coefs;
    coefs.prepare (rate);
    LevelTracker t;
    const int ch = reader.channels();
    std::vector<float> buf (static_cast<std::size_t> (4096) * static_cast<std::size_t> (ch));
    std::int64_t pos = from;
    while (pos <= frame)
    {
        const auto want = std::min<std::int64_t> (4096, frame + 1 - pos);
        const auto got = reader.read (buf.data(), want);
        if (got <= 0) break;
        for (std::int64_t i = 0; i < got; ++i)
        {
            const float l = buf[static_cast<std::size_t> (i * ch)], r = ch > 1 ? buf[static_cast<std::size_t> (i * ch + 1)] : l;
            t.process (coefs, l, r);
        }
        pos += got;
    }
    db = t.db (meter);
    return db > -90.0;
}

int loudestPoint (const std::vector<float>& curve, int from, int to) noexcept
{
    from = std::max (0, from);
    to = std::min (static_cast<int> (curve.size()), to);
    if (to <= from) return -1;
    return static_cast<int> (std::max_element (curve.begin() + from, curve.begin() + to) - curve.begin());
}

std::vector<int> localPeaks (const std::vector<float>& curve, double prominenceDb, int hopsPerSecond)
{
    std::vector<int> peaks;
    const int n = static_cast<int> (curve.size());
    const int reach = std::max (1, hopsPerSecond * 3);
    for (int i = 1; i + 1 < n; ++i)
    {
        const float v = curve[static_cast<std::size_t> (i)];
        if (v <= -119.0f || v < curve[static_cast<std::size_t> (i - 1)] || v <= curve[static_cast<std::size_t> (i + 1)]) continue;   // maximum local (le premier d'un plateau)
        auto side = [&] (int dir)
        {
            float lowest = v;
            for (int j = i + dir, k = 0; j >= 0 && j < n && k < reach; j += dir, ++k)
            {
                const float w = curve[static_cast<std::size_t> (j)];
                if (w > v) return lowest;     // un maximum plus haut : le creux rencontré est le bon
                lowest = std::min (lowest, w);
            }
            return lowest;
        };
        const float valley = std::max (side (-1), side (1));
        if (static_cast<double> (v - valley) >= prominenceDb) peaks.push_back (i);
    }
    return peaks;
}

int neighbourPeak (const std::vector<int>& peaks, int from, int direction) noexcept
{
    if (direction > 0)
    {
        const auto it = std::upper_bound (peaks.begin(), peaks.end(), from);
        return it == peaks.end() ? -1 : *it;
    }
    const auto it = std::lower_bound (peaks.begin(), peaks.end(), from);
    return it == peaks.begin() ? -1 : *(it - 1);
}
} // namespace neptune::core
