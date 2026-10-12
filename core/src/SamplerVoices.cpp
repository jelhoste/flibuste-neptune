#include "neptune/core/SamplerVoices.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
namespace
{
inline float hermite (float y0, float y1, float y2, float y3, float t) noexcept
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

// Indice réellement lu : en boucle, ce qui dépasse la fin de boucle revient dans la boucle.
inline std::int64_t wrapIndex (const Zone& z, std::int64_t k) noexcept
{
    if (z.mode == PlayMode::Loop && k >= z.loopEnd)
    {
        const std::int64_t len = z.loopEnd - z.loopStart;
        k = z.loopStart + (k - z.loopStart) % len;
    }
    return k;
}
} // namespace

void SamplerVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

void SamplerVoices::setInstrument (const Instrument* instrument) noexcept
{
    allSoundOff();
    instrument_ = instrument;
}

void SamplerVoices::allSoundOff() noexcept
{
    for (auto& v : voices_) v = Voice {};
}

int SamplerVoices::activeVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active; }));
}

int SamplerVoices::heldVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(),
                                            [] (const Voice& v) { return v.active && ! v.releasing && ! v.stolen; }));
}

void SamplerVoices::startRelease (Voice& v, double seconds) noexcept
{
    const double frames = std::max (1.0, seconds * sampleRate_);
    v.envStep = static_cast<float> (v.env / frames);
    if (v.envStep <= 0.0f) v.envStep = 1.0f;
}

SamplerVoices::Voice* SamplerVoices::allocate() noexcept
{
    for (auto& v : voices_)
        if (! v.active) return &v;
    // Plus de place du tout (rafale de vols) : on prend la plus ancienne voix en fondu, sinon la plus ancienne.
    Voice* best = nullptr;
    for (auto& v : voices_)
        if (v.stolen && (best == nullptr || v.order < best->order)) best = &v;
    if (best == nullptr)
        for (auto& v : voices_)
            if (best == nullptr || v.order < best->order) best = &v;
    return best;
}

void SamplerVoices::noteOn (int channel, int key, float velocity) noexcept
{
    if (instrument_ == nullptr) return;
    const Zone* zone = instrument_->find (key, velocity);
    if (zone == nullptr || zone->sample == nullptr || zone->loopEnd <= zone->loopStart) return;

    // Limite de voix : on fait s'éteindre la plus ancienne voix tenue (fondu court, pas de clic).
    if (heldVoices() >= kMaxVoices)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices_)
            if (v.active && ! v.releasing && ! v.stolen && (oldest == nullptr || v.order < oldest->order)) oldest = &v;
        if (oldest != nullptr)
        {
            oldest->stolen = true;
            startRelease (*oldest, kStealFadeSeconds);
        }
    }

    Voice* v = allocate();
    if (v == nullptr) return;
    *v = Voice {};
    v->zone = zone;
    v->active = true;
    v->channel = channel;
    v->key = key;
    v->order = ++counter_;
    v->pos = 0.0;
    const double semis = static_cast<double> (key - zone->rootNote) + zone->tuneCents / 100.0;
    v->baseRatio = zone->sample->sampleRate / sampleRate_ * std::exp2 (semis / 12.0);
    v->gain = std::clamp (velocity, 0.0f, 1.0f) * zone->gain;
}

void SamplerVoices::noteOff (int channel, int key) noexcept
{
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.channel != channel || v.key != key) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            startRelease (v, v.zone->releaseSeconds);
        }
        // one-shot : le Note Off est ignoré, le sample est joué en entier
    }
}

void SamplerVoices::renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept
{
    const Zone& z = *v.zone;
    const SampleData& s = *z.sample;
    const bool stereo = s.channels >= 2;
    const bool loop = z.mode == PlayMode::Loop;
    const double inc = v.baseRatio * pitchFactor;
    const double frames = static_cast<double> (s.frames);
    const float gStep = numFrames > 0 ? (g1 - g0) / static_cast<float> (numFrames) : 0.0f;
    float mg = g0;

    for (int i = 0; i < numFrames; ++i)
    {
        if (v.pos >= frames) { v.active = false; return; } // fin du sample (one-shot)

        const std::int64_t i0 = static_cast<std::int64_t> (v.pos);
        const float t = static_cast<float> (v.pos - static_cast<double> (i0));
        const std::int64_t a = wrapIndex (z, i0 - 1), b = wrapIndex (z, i0), c = wrapIndex (z, i0 + 1), d = wrapIndex (z, i0 + 2);
        const float l = hermite (s.at (0, a), s.at (0, b), s.at (0, c), s.at (0, d), t);
        const float r = stereo ? hermite (s.at (1, a), s.at (1, b), s.at (1, c), s.at (1, d), t) : l;

        float e = v.gain * mg;
        if (v.releasing || v.stolen)
        {
            v.env -= v.envStep;
            if (v.env <= 0.0f) { v.active = false; return; }
            e *= v.env;
        }
        if (right != nullptr)
        {
            left[i] += l * e;
            right[i] += r * e;
        }
        else
            left[i] += 0.5f * (l + r) * e;

        mg += gStep;
        v.pos += inc;
        if (loop && v.pos >= static_cast<double> (z.loopEnd))
        {
            const double len = static_cast<double> (z.loopEnd - z.loopStart);
            v.pos = static_cast<double> (z.loopStart) + std::fmod (v.pos - static_cast<double> (z.loopStart), len);
        }
    }
}

void SamplerVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, double masterTuneCents) noexcept
{
    if (left == nullptr || numFrames <= 0) return;
    const double pitchFactor = masterTuneCents == 0.0 ? 1.0 : std::exp2 (masterTuneCents / 1200.0);
    for (auto& v : voices_)
        if (v.active) renderVoice (v, left, right, numFrames, gainStart, gainEnd, pitchFactor);
}
} // namespace neptune::core
