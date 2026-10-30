#include "neptune/core/Loudness.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef NEPTUNE_HAS_EBUR128
 #include <ebur128.h>
#endif

namespace neptune::core
{
LoudnessMeter::~LoudnessMeter() { stop(); }

void LoudnessMeter::stop()
{
    quit_.store (true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    quit_.store (false, std::memory_order_release);
}

void LoudnessMeter::prepare (double sampleRate)
{
    stop();
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    // une seconde de tampon : large devant le rythme du thread d'analyse (10 ms)
    frames_ = static_cast<std::size_t> (sampleRate_);
    ring_.assign (frames_ * 2u, 0.0f);
    head_.store (0, std::memory_order_relaxed);
    tail_.store (0, std::memory_order_relaxed);
    momentary_.store (-200.0f);
    shortTerm_.store (-200.0f);
    integrated_.store (-200.0f);
    truePeak_.store (-200.0f);
    range_.store (0.0f);
   #ifdef NEPTUNE_HAS_EBUR128
    thread_ = std::thread ([this] { run(); });
   #endif
}

void LoudnessMeter::push (const float* left, const float* right, int n) noexcept
{
    if (frames_ == 0 || left == nullptr || right == nullptr || n <= 0) return;
    const auto head = head_.load (std::memory_order_relaxed);
    const auto tail = tail_.load (std::memory_order_acquire);
    const std::size_t free = frames_ - (head - tail);
    const std::size_t count = std::min (free, static_cast<std::size_t> (n)); // plein : le surplus est perdu
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t slot = ((head + i) % frames_) * 2u;
        ring_[slot] = left[i];
        ring_[slot + 1u] = right[i];
    }
    head_.store (head + count, std::memory_order_release);
}

LoudnessMeter::Reading LoudnessMeter::read() const noexcept
{
    Reading r;
    r.momentary = momentary_.load (std::memory_order_relaxed);
    r.shortTerm = shortTerm_.load (std::memory_order_relaxed);
    r.integrated = integrated_.load (std::memory_order_relaxed);
    r.truePeakDb = truePeak_.load (std::memory_order_relaxed);
    r.range = range_.load (std::memory_order_relaxed);
    return r;
}

#ifdef NEPTUNE_HAS_EBUR128
namespace
{
constexpr int kModes = EBUR128_MODE_M | EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_LRA | EBUR128_MODE_HISTOGRAM;

float clampLufs (double v) noexcept
{
    return std::isfinite (v) ? static_cast<float> (std::max (v, -200.0)) : -200.0f;
}
} // namespace

void LoudnessMeter::run()
{
    ebur128_state* state = ebur128_init (2, static_cast<unsigned long> (sampleRate_), kModes);
    if (state == nullptr) return;
    std::vector<float> block (frames_ * 2u); // L et R entrelacés
    double peak = 0.0;
    auto lastPublish = std::chrono::steady_clock::now();

    while (! quit_.load (std::memory_order_acquire))
    {
        if (resetRequested_.exchange (false, std::memory_order_acq_rel))
        {
            ebur128_destroy (&state);
            state = ebur128_init (2, static_cast<unsigned long> (sampleRate_), kModes);
            if (state == nullptr) return;
            peak = 0.0;
            range_.store (0.0f, std::memory_order_relaxed);
        }
        const auto tail = tail_.load (std::memory_order_relaxed);
        const auto head = head_.load (std::memory_order_acquire);
        const std::size_t available = head - tail;
        if (available > 0)
        {
            const std::size_t count = std::min (available, frames_);
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t slot = ((tail + i) % frames_) * 2u;
                block[i * 2u] = ring_[slot];
                block[i * 2u + 1u] = ring_[slot + 1u];
            }
            tail_.store (tail + count, std::memory_order_release);
            ebur128_add_frames_float (state, block.data(), count);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPublish >= std::chrono::milliseconds (100))
        {
            lastPublish = now;
            double v = 0.0;
            if (ebur128_loudness_momentary (state, &v) == EBUR128_SUCCESS) momentary_.store (clampLufs (v), std::memory_order_relaxed);
            if (ebur128_loudness_shortterm (state, &v) == EBUR128_SUCCESS) shortTerm_.store (clampLufs (v), std::memory_order_relaxed);
            if (ebur128_loudness_global (state, &v) == EBUR128_SUCCESS) integrated_.store (clampLufs (v), std::memory_order_relaxed);
            if (ebur128_loudness_range (state, &v) == EBUR128_SUCCESS && std::isfinite (v)) range_.store (static_cast<float> (std::max (v, 0.0)), std::memory_order_relaxed);
            for (unsigned c = 0; c < 2; ++c)
            {
                double tp = 0.0;
                if (ebur128_true_peak (state, c, &tp) == EBUR128_SUCCESS) peak = std::max (peak, tp);
            }
            truePeak_.store (peak > 0.0 ? static_cast<float> (20.0 * std::log10 (peak)) : -200.0f, std::memory_order_relaxed);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    ebur128_destroy (&state);
}
#else
void LoudnessMeter::run() {}
#endif
} // namespace neptune::core
