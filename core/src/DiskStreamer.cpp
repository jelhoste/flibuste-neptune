#include "neptune/core/DiskStreamer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
namespace
{
constexpr std::int64_t kRingMask = DiskStreamer::kRingFrames - 1;
static_assert ((DiskStreamer::kRingFrames & (DiskStreamer::kRingFrames - 1)) == 0, "ring size must be a power of two");
}

DiskStreamer::~DiskStreamer()
{
    stop_.store (true);
    if (thread_.joinable()) thread_.join();
}

void DiskStreamer::start()
{
    if (running_.load (std::memory_order_acquire)) return;
    storage_.assign (static_cast<std::size_t> (kSlots) * 2u * static_cast<std::size_t> (kRingFrames), 0.0f);
    for (int i = 0; i < kSlots; ++i)
    {
        slots_[static_cast<std::size_t> (i)].ring[0] = storage_.data() + static_cast<std::size_t> (i) * 2u * kRingFrames;
        slots_[static_cast<std::size_t> (i)].ring[1] = slots_[static_cast<std::size_t> (i)].ring[0] + kRingFrames;
    }
    freeSlots_.clear();
    freeSlots_.reserve (kSlots);
    for (int i = kSlots - 1; i >= 0; --i) freeSlots_.push_back (i);
    thread_ = std::thread ([this] { threadMain(); });
    running_.store (true, std::memory_order_release);
}

int DiskStreamer::claim (const SampleData& sample) noexcept
{
    if (! running_.load (std::memory_order_acquire) || freeSlots_.empty()) return -1;
    if (sample.path.empty() || sample.path.size() >= maxStreamPathBytes || sample.channels < 1 || sample.channels > 2) return -1;

    const int slot = freeSlots_.back();
    auto& s = slots_[static_cast<std::size_t> (slot)];
    const std::uint32_t r = s.request.load (std::memory_order_relaxed);
    const std::uint32_t next = (r & 1u) != 0 ? r + 2u : r + 1u; // toujours impair, et différent du précédent
    auto& p = s.params[(next >> 1) & 1u];
    std::memcpy (p.path, sample.path.c_str(), sample.path.size() + 1);
    p.channels = sample.channels;
    p.startFrame = sample.resident();
    p.totalFrames = sample.frames;
    s.readPos.store (p.startFrame, std::memory_order_relaxed);
    s.request.store (next, std::memory_order_release);
    freeSlots_.pop_back();
    return slot;
}

void DiskStreamer::release (int slot) noexcept
{
    if (slot < 0 || slot >= kSlots) return;
    auto& s = slots_[static_cast<std::size_t> (slot)];
    const std::uint32_t r = s.request.load (std::memory_order_relaxed);
    if ((r & 1u) == 0) return; // déjà libre
    s.request.store (r + 1u, std::memory_order_release);
    freeSlots_.push_back (slot);
}

DiskStreamer::View DiskStreamer::view (int slot, std::uint32_t gen) const noexcept
{
    View v;
    if (slot < 0 || slot >= kSlots) return v;
    const auto& s = slots_[static_cast<std::size_t> (slot)];
    if (s.ready.load (std::memory_order_acquire) != gen || s.request.load (std::memory_order_acquire) != gen) return v;
    v.ready = true;
    v.left = s.ring[0];
    v.right = s.params[(gen >> 1) & 1u].channels >= 2 ? s.ring[1] : s.ring[0];
    v.writePos = s.writePos.load (std::memory_order_acquire);
    return v;
}

void DiskStreamer::threadMain()
{
    struct Local
    {
        AudioStreamReader reader;
        std::uint32_t gen = 0;      // valeur de `request` pour laquelle le lecteur est ouvert (0 = fermé)
        bool failed = false;
        int channels = 0;
        std::int64_t total = 0;
    };
    std::vector<Local> local (static_cast<std::size_t> (kSlots));
    std::vector<float> tmp (static_cast<std::size_t> (kChunkFrames) * 2u);

    while (! stop_.load (std::memory_order_relaxed))
    {
        bool worked = false;
        for (int i = 0; i < kSlots; ++i)
        {
            auto& s = slots_[static_cast<std::size_t> (i)];
            auto& l = local[static_cast<std::size_t> (i)];
            const std::uint32_t r = s.request.load (std::memory_order_acquire);

            if ((r & 1u) == 0)
            {
                if (l.gen != 0) { l.reader.close(); l.gen = 0; l.failed = false; }
                continue;
            }
            if (r != l.gen) // nouveau flux : ouverture et placement au début
            {
                l.reader.close();
                l.gen = r;
                l.failed = true;
                const Params& p = s.params[(r >> 1) & 1u];
                l.channels = p.channels;
                l.total = p.totalFrames;
                const std::int64_t start = p.startFrame;
                if (l.reader.open (std::filesystem::u8path (std::string (p.path))) && l.reader.channels() == l.channels
                    && l.reader.totalFrames() == l.total && l.reader.seek (start))
                {
                    l.failed = false;
                    s.writePos.store (start, std::memory_order_relaxed);
                    s.ready.store (r, std::memory_order_release);
                }
                worked = true;
                continue;
            }
            if (l.failed) continue;

            const std::int64_t wp = s.writePos.load (std::memory_order_relaxed);
            const std::int64_t rp = s.readPos.load (std::memory_order_acquire);
            if (wp >= l.total) continue;
            const std::int64_t room = static_cast<std::int64_t> (kRingFrames) - (wp - rp);
            const std::int64_t want = std::min<std::int64_t> ({ room, kChunkFrames, l.total - wp });
            if (want < kChunkFrames / 4 && wp + want < l.total) continue; // trop peu de place : on attend un vrai bloc

            const std::int64_t got = l.reader.read (tmp.data(), want);
            if (got <= 0) { l.failed = true; continue; }
            if (s.request.load (std::memory_order_acquire) != r) continue; // annulé entre-temps : on jette

            const int ch = l.channels;
            for (std::int64_t k = 0; k < got; ++k)
            {
                const std::size_t pos = static_cast<std::size_t> ((wp + k) & kRingMask);
                s.ring[0][pos] = tmp[static_cast<std::size_t> (k) * static_cast<std::size_t> (ch)];
                if (ch >= 2) s.ring[1][pos] = tmp[static_cast<std::size_t> (k) * 2u + 1u];
            }
            s.writePos.store (wp + got, std::memory_order_release);
            worked = true;
        }
        if (! worked) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}
} // namespace neptune::core
