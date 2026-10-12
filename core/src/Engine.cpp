#include "neptune/core/Engine.h"
#include <algorithm>
#include "neptune/core/SamplerParams.h"

namespace neptune::core
{
void Engine::prepare (double sampleRate, int maxBlockSize) noexcept
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    activeNotes_ = 0;
    voices_.prepare (sampleRate);
    lastGain_ = 1.0f;
}

void Engine::publishInstrument (std::shared_ptr<const Instrument> instrument)
{
    if (instrument != nullptr)
        for (const auto& z : instrument->zones)
            if (z.sample != nullptr && z.sample->isStreamed()) { streamer_.start(); break; }
    slot_.publish (std::move (instrument));
}

void Engine::attachParameters (const ParameterSet* parameters) noexcept
{
    params_ = parameters;
    if (parameters == nullptr) return;
    gainIndex_ = parameters->indexOf (sampler::gain);
    tuneSemiIndex_ = parameters->indexOf (sampler::tuneSemitones);
    tuneCentsIndex_ = parameters->indexOf (sampler::tuneCents);
}

void Engine::noteOn (int channel, int key, float velocity) noexcept
{
    ++activeNotes_;
    voices_.noteOn (channel, key, velocity);
}

void Engine::noteOff (int channel, int key) noexcept
{
    activeNotes_ = std::max (0, activeNotes_ - 1);
    voices_.noteOff (channel, key);
}

void Engine::setParameter (std::uint32_t, double) noexcept {}

void Engine::process (float* const* out, int numChannels, int numFrames) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        if (out[c] != nullptr) std::fill (out[c], out[c] + numFrames, 0.0f);

    if (slot_.adoptPending()) voices_.setInstrument (slot_.current());
    if (numChannels < 1 || out[0] == nullptr) return;

    float gain = 1.0f;
    double cents = 0.0;
    if (params_ != nullptr)
    {
        if (gainIndex_ != ParameterSet::npos) gain = static_cast<float> (sampler::dbToGain (params_->getAt (gainIndex_)));
        if (tuneSemiIndex_ != ParameterSet::npos) cents += params_->getAt (tuneSemiIndex_) * 100.0;
        if (tuneCentsIndex_ != ParameterSet::npos) cents += params_->getAt (tuneCentsIndex_);
    }

    float* right = (numChannels >= 2 && out[1] != nullptr) ? out[1] : nullptr;
    voices_.render (out[0], right, numFrames, lastGain_, gain, cents);
    lastGain_ = gain;
}
} // namespace neptune::core
