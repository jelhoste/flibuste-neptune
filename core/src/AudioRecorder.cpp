#include "neptune/core/AudioRecorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <sndfile.h>

namespace neptune::core
{
bool AudioRecorder::start (const std::filesystem::path& path, double sampleRate, std::string* error)
{
    if (recording_.load()) return false;
    if (thread_.joinable()) thread_.join();
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories (path.parent_path(), ec);
    SF_INFO info {};
    info.samplerate = static_cast<int> (std::lround (sampleRate > 0.0 ? sampleRate : 48000.0));
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
    if (f == nullptr) { if (error != nullptr) *error = sf_strerror (nullptr); return false; }
    file_ = f;
    path_ = path;
    sampleRate_ = static_cast<double>(info.samplerate);
    ring_.assign (kRingFrames * 2u, 0.0f);
    head_.store (0); tail_.store (0); written_.store (0); dropped_.store (0); peak_.store (0.0f);
    quit_.store (false);
    recording_.store (true, std::memory_order_release);
    thread_ = std::thread ([this] { run(); });
    return true;
}

std::int64_t AudioRecorder::stop()
{
    if (! recording_.load (std::memory_order_acquire) && ! thread_.joinable()) return 0;
    recording_.store (false, std::memory_order_release);   // le thread audio cesse de déposer
    quit_.store (true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    return written_.load();
}

void AudioRecorder::push (const float* left, const float* right, int n) noexcept
{
    if (! recording_.load (std::memory_order_acquire) || left == nullptr || n <= 0) return;
    const auto head = head_.load (std::memory_order_relaxed);
    const auto tail = tail_.load (std::memory_order_acquire);
    if (head - tail + static_cast<std::uint64_t> (n) > kRingFrames) { dropped_.fetch_add (n, std::memory_order_relaxed); return; }
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right != nullptr ? right[i] : l;
        const std::size_t slot = static_cast<std::size_t> ((head + static_cast<std::uint64_t> (i)) % kRingFrames) * 2u;
        ring_[slot] = l;
        ring_[slot + 1u] = r;
        pk = std::max ({ pk, std::fabs (l), std::fabs (r) });
    }
    head_.store (head + static_cast<std::uint64_t> (n), std::memory_order_release);
    if (pk > peak_.load (std::memory_order_relaxed)) peak_.store (pk, std::memory_order_relaxed);
}

void AudioRecorder::run()
{
    auto* f = static_cast<SNDFILE*> (file_);
    std::vector<float> block (16384u * 2u);
    auto drain = [&]
    {
        for (;;)
        {
            const auto tail = tail_.load (std::memory_order_relaxed);
            const auto head = head_.load (std::memory_order_acquire);
            const std::size_t avail = static_cast<std::size_t> (head - tail);
            if (avail == 0) return;
            const std::size_t count = std::min<std::size_t> (avail, 16384u);
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t slot = static_cast<std::size_t> ((tail + i) % kRingFrames) * 2u;
                block[i * 2u] = ring_[slot];
                block[i * 2u + 1u] = ring_[slot + 1u];
            }
            sf_writef_float (f, block.data(), static_cast<sf_count_t> (count));
            written_.fetch_add (static_cast<std::int64_t> (count), std::memory_order_relaxed);
            tail_.store (tail + count, std::memory_order_release);
        }
    };
    while (! quit_.load (std::memory_order_acquire))
    {
        drain();
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    drain();
    sf_close (f);
    file_ = nullptr;
}
} // namespace neptune::core
