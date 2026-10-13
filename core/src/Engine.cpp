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
    publishVoices();
}

void Engine::publishVoices() noexcept
{
    const auto seq = voiceSeq_.load (std::memory_order_relaxed);
    voiceSeq_.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    int n = 0;
    voices_.forEachVoice ([this, &n] (const VoiceInfo& v)
    {
        if (n >= kVoiceSnapshot) return;
        const auto q = [] (float x) { return static_cast<std::uint64_t> (std::clamp (x, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const std::uint64_t flags = (v.releasing ? 1u : 0u) | (v.fading ? 2u : 0u) | (v.streaming ? 4u : 0u) | (v.preview ? 8u : 0u);
        std::uint64_t w0 = static_cast<std::uint64_t> (v.sourceIndex + 1) & 0xFFFFFFull;
        w0 |= static_cast<std::uint64_t> (v.key & 0xFF) << 24;
        w0 |= static_cast<std::uint64_t> (v.velocity & 0xFF) << 32;
        w0 |= flags << 40;
        w0 |= q (v.layerGain) << 48;
        w0 |= q (v.level) << 56;
        std::uint64_t w1 = v.order & 0xFFFFFFFFFFull;
        w1 |= static_cast<std::uint64_t> (v.channel & 0xFF) << 40;
        w1 |= q (v.progress) << 48;
        voiceWords_[static_cast<std::size_t> (n) * 2].store (w0, std::memory_order_relaxed);
        voiceWords_[static_cast<std::size_t> (n) * 2 + 1].store (w1, std::memory_order_relaxed);
        ++n;
    });
    voiceCount_.store (n, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    voiceSeq_.store (seq + 2, std::memory_order_release);
}

int Engine::readVoices (VoiceInfo* out, int maxOut) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = voiceSeq_.load (std::memory_order_acquire);
        if ((before & 1u) != 0) continue; // écriture en cours : on réessaie
        const int n = std::min (voiceCount_.load (std::memory_order_relaxed), std::min (maxOut, kVoiceSnapshot));
        for (int i = 0; i < n; ++i)
        {
            const auto w0 = voiceWords_[static_cast<std::size_t> (i) * 2].load (std::memory_order_relaxed);
            const auto w1 = voiceWords_[static_cast<std::size_t> (i) * 2 + 1].load (std::memory_order_relaxed);
            VoiceInfo& v = out[i];
            v.sourceIndex = static_cast<int> (w0 & 0xFFFFFFull) - 1;
            v.key = static_cast<int> ((w0 >> 24) & 0xFF);
            v.velocity = static_cast<int> ((w0 >> 32) & 0xFF);
            const auto flags = (w0 >> 40) & 0xFF;
            v.releasing = (flags & 1u) != 0;
            v.fading = (flags & 2u) != 0;
            v.streaming = (flags & 4u) != 0;
            v.preview = (flags & 8u) != 0;
            v.layerGain = static_cast<float> ((w0 >> 48) & 0xFF) / 255.0f;
            v.level = static_cast<float> ((w0 >> 56) & 0xFF) / 255.0f;
            v.order = w1 & 0xFFFFFFFFFFull;
            v.channel = static_cast<int> ((w1 >> 40) & 0xFF);
            v.progress = static_cast<float> ((w1 >> 48) & 0xFF) / 255.0f;
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (voiceSeq_.load (std::memory_order_relaxed) == before) return n;
    }
    return 0; // trop de collisions d'écriture (très improbable) : cette image est ignorée
}
} // namespace neptune::core
