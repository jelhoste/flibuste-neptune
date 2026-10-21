#include "neptune/core/WaveformPeaks.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
WaveformPeaks computePeaks (const std::filesystem::path& path, std::int64_t from, std::int64_t to, int bins, const std::atomic<bool>* cancel)
{
    WaveformPeaks out;
    AudioStreamReader reader;
    if (bins < 1 || ! reader.open (path, &out.error)) { if (out.error.empty()) out.error = "invalid request"; return out; }
    const int ch = reader.channels();
    out.channels = ch;
    out.totalFrames = reader.totalFrames();
    out.from = std::clamp<std::int64_t> (from, 0, out.totalFrames);
    out.to = to < 0 ? out.totalFrames : std::clamp<std::int64_t> (to, out.from, out.totalFrames);
    if (out.to <= out.from) { out.error = "empty region"; return out; }
    out.bins = bins;
    out.lo.assign (static_cast<std::size_t> (ch) * static_cast<std::size_t> (bins), std::numeric_limits<float>::max());
    out.hi.assign (out.lo.size(), std::numeric_limits<float>::lowest());

    if (! reader.seek (out.from)) { out.error = "seek failed"; return out; }
    constexpr std::int64_t kChunk = 32768;
    std::vector<float> buf (static_cast<std::size_t> (kChunk) * static_cast<std::size_t> (ch));
    const double span = static_cast<double> (out.to - out.from);
    std::int64_t pos = out.from;
    while (pos < out.to)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed)) { out.error = "cancelled"; return out; }
        const auto want = std::min<std::int64_t> (kChunk, out.to - pos);
        const auto got = reader.read (buf.data(), want);
        if (got <= 0) break;
        for (std::int64_t i = 0; i < got; ++i)
        {
            const int bin = std::min (bins - 1, static_cast<int> (static_cast<double> (pos + i - out.from) * static_cast<double> (bins) / span));
            for (int c = 0; c < ch; ++c)
            {
                const float v = buf[static_cast<std::size_t> (i) * static_cast<std::size_t> (ch) + static_cast<std::size_t> (c)];
                auto& l = out.lo[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (bin)];
                auto& h = out.hi[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (bin)];
                l = std::min (l, v);
                h = std::max (h, v);
            }
        }
        pos += got;
    }
    // cases restées vides (région plus petite que le nombre de cases) : recopie de la voisine
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < bins; ++i)
        {
            auto& l = out.lo[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (i)];
            auto& h = out.hi[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (i)];
            if (l > h)
            {
                const int j = i > 0 ? i - 1 : std::min (bins - 1, i + 1);
                l = out.lo[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (j)];
                h = out.hi[static_cast<std::size_t> (c) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (j)];
                if (l > h) l = h = 0.0f;
            }
        }
    out.ok = true;
    return out;
}
} // namespace neptune::core
