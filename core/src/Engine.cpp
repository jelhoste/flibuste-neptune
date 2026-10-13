#include "neptune/core/Engine.h"
#include <algorithm>
#include <cmath>
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
    if (key >= 0 && key < 128) // trace pour l'interface (file à producteur unique : le thread audio)
    {
        const auto head = traceHead_.load (std::memory_order_relaxed);
        if (head - traceTail_.load (std::memory_order_acquire) < kTraceSize)
        {
            const int v = std::clamp (static_cast<int> (std::lround (voices_.lastShapedVelocity() * 127.0f)), 1, 127);
            trace_[head & (kTraceSize - 1)].store (static_cast<std::uint16_t> ((key << 8) | v), std::memory_order_relaxed);
            traceHead_.store (head + 1, std::memory_order_release);
        }
    }
}

void Engine::noteOff (int channel, int key) noexcept
{
    activeNotes_ = std::max (0, activeNotes_ - 1);
    voices_.noteOff (channel, key);
}

void Engine::setParameter (std::uint32_t, double) noexcept {}

bool Engine::popNoteTrace (NoteTrace& out) noexcept
{
    const auto tail = traceTail_.load (std::memory_order_relaxed);
    if (tail == traceHead_.load (std::memory_order_acquire)) return false;
    const auto packed = trace_[tail & (kTraceSize - 1)].load (std::memory_order_relaxed);
    out.key = packed >> 8;
    out.velocity = packed & 0xFF;
    traceTail_.store (tail + 1, std::memory_order_release);
    return true;
}

bool Engine::postUiEvent (const UiEvent& e) noexcept
{
    const auto head = uiHead_.load (std::memory_order_relaxed);
    if (head - uiTail_.load (std::memory_order_acquire) >= kUiQueueSize) return false;
    uiQueue_[head & (kUiQueueSize - 1)] = e;
    uiHead_.store (head + 1, std::memory_order_release);
    return true;
}

void Engine::process (float* const* out, int numChannels, int numFrames) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        if (out[c] != nullptr) std::fill (out[c], out[c] + numFrames, 0.0f);

    if (slot_.adoptPending()) voices_.setInstrument (slot_.current());

    // événements de l'interface (après l'adoption : ils s'adressent à l'instrument courant)
    for (auto tail = uiTail_.load (std::memory_order_relaxed); tail != uiHead_.load (std::memory_order_acquire); ++tail)
    {
        const UiEvent& e = uiQueue_[tail & (kUiQueueSize - 1)];
        switch (e.type)
        {
            case UiEvent::Type::NoteOn: noteOn (e.channel, e.key, e.velocity); break;
            case UiEvent::Type::NoteOff: noteOff (e.channel, e.key); break;
            case UiEvent::Type::PreviewZone: voices_.previewZone (e.key); break;
            case UiEvent::Type::PreviewRelease: voices_.releasePreview(); break;
            case UiEvent::Type::PreviewStop: voices_.stopPreview(); break;
        }
        uiTail_.store (tail + 1, std::memory_order_release);
    }
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
