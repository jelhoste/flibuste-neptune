#include "neptune/core/Engine.h"
#include <algorithm>

namespace neptune::core
{
void Engine::prepare (double sampleRate, int maxBlockSize) noexcept
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    activeNotes_ = 0;
}

void Engine::noteOn (int, int, float) noexcept { ++activeNotes_; }
void Engine::noteOff (int, int) noexcept { activeNotes_ = std::max (0, activeNotes_ - 1); }
void Engine::setParameter (std::uint32_t, double) noexcept {}

void Engine::process (float* const* out, int numChannels, int numFrames) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        if (out[c] != nullptr) std::fill (out[c], out[c] + numFrames, 0.0f);
}
} // namespace neptune::core
