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

void SamplerVoices::discard (Voice& v) noexcept
{
    if (v.streaming && streamer_ != nullptr) streamer_->release (static_cast<int> (&v - voices_.data()));
    v = Voice {};
}

void SamplerVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

void SamplerVoices::bindZone (Voice& v, const Zone& zone) noexcept
{
    v.zone = &zone;
    const double semis = static_cast<double> (v.key - zone.rootNote) + zone.tuneCents / 100.0;
    v.baseRatio = zone.sample->sampleRate / sampleRate_ * std::exp2 (semis / 12.0);
}

void SamplerVoices::setInstrument (const Instrument* instrument) noexcept
{
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        const SampleData* current = v.zone != nullptr ? v.zone->sample.get() : nullptr;
        const bool keep = instrument != nullptr && v.zoneIndex >= 0 && static_cast<std::size_t> (v.zoneIndex) < instrument->zones.size()
                       && current != nullptr && instrument->zones[static_cast<std::size_t> (v.zoneIndex)].sample.get() == current;
        if (keep) bindZone (v, instrument->zones[static_cast<std::size_t> (v.zoneIndex)]);
        else discard (v);
    }
    instrument_ = instrument;
}

void SamplerVoices::allSoundOff() noexcept
{
    for (auto& v : voices_) discard (v);
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

void SamplerVoices::steal (Voice& v) noexcept
{
    v.stolen = true;
    v.fadeStep = static_cast<float> (1.0 / std::max (1.0, kStealFadeSeconds * sampleRate_));
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
    // Courbe de réponse de l'instrument : avant le choix des couches et le volume.
    const float shaped = std::clamp (instrument_->shapeVelocity (velocity), 0.0f, 1.0f);
    if (key >= 0 && key < 128) heldVelocity_[static_cast<std::size_t> (key)] = shaped;
    Instrument::Pick picks[Instrument::maxPicks];
    const int n = instrument_->selectLayers (key, shaped, /*release*/ false, picks, Instrument::maxPicks);
    for (int i = 0; i < n; ++i) startVoice (channel, key, shaped, *picks[i].zone, picks[i].gain);
}

void SamplerVoices::startVoice (int channel, int key, float velocity, const Zone& zone, float layerGain) noexcept
{
    if (zone.sample == nullptr || zone.loopEnd <= zone.loopStart) return;

    // Limite de voix : on fait s'éteindre la plus ancienne voix tenue (fondu court, pas de clic).
    if (heldVoices() >= kMaxVoices)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices_)
            if (v.active && ! v.releasing && ! v.stolen && (oldest == nullptr || v.order < oldest->order)) oldest = &v;
        if (oldest != nullptr) steal (*oldest);
    }

    Voice* v = allocate();
    if (v == nullptr) return;
    discard (*v);
    v->active = true;
    v->channel = channel;
    v->key = key;
    v->zoneIndex = static_cast<int> (&zone - instrument_->zones.data());
    v->order = ++counter_;
    v->pos = 0.0;
    bindZone (*v, zone);
    v->velocity = std::clamp (velocity, 0.0f, 1.0f);
    v->layerGain = layerGain;
    v->env.begin (zone.env, sampleRate_);
    // Échantillon dont seul le début est en mémoire : le reste est lu sur le disque. Les zones en boucle gardent tout ce
    // qui précède la fin de boucle en mémoire (voir residentFramesNeeded) et n'ont donc jamais besoin du flux.
    if (zone.mode == PlayMode::OneShot && zone.sample->isStreamed() && streamer_ != nullptr)
    {
        const int slot = static_cast<int> (v - voices_.data());
        if (streamer_->claim (slot, *zone.sample))
        {
            v->streaming = true;
            v->streamGen = streamer_->generation (slot);
        }
    }
}

void SamplerVoices::noteOff (int channel, int key) noexcept
{
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.channel != channel || v.key != key) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            v.env.noteOff (v.zone->env, sampleRate_);
        }
        // one-shot : le Note Off est ignoré, le sample est joué en entier
    }

    // Zones déclenchées au relâchement (bruit de mécanique, fin de note) : jouées à la vélocité du Note On.
    if (instrument_ != nullptr && key >= 0 && key < 128)
    {
        const float vel = heldVelocity_[static_cast<std::size_t> (key)];
        // vel est déjà passée par la courbe de l'instrument (voir noteOn)
        const float v = vel > 0.0f ? vel : 0.5f;
        Instrument::Pick picks[Instrument::maxPicks];
        const int n = instrument_->selectLayers (key, v, /*release*/ true, picks, Instrument::maxPicks);
        for (int i = 0; i < n; ++i) startVoice (channel, key, v, *picks[i].zone, picks[i].gain);
    }
}

void SamplerVoices::renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept
{
    if (v.zone->sample->isStreamed()) renderVoiceT<true> (v, left, right, numFrames, g0, g1, pitchFactor);
    else renderVoiceT<false> (v, left, right, numFrames, g0, g1, pitchFactor);
}

template <bool Streamed>
void SamplerVoices::renderVoiceT (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept
{
    const Zone& z = *v.zone;
    const SampleData& s = *z.sample;
    const bool stereo = s.channels >= 2;
    const bool loop = z.mode == PlayMode::Loop;
    const double inc = v.baseRatio * pitchFactor;
    const double frames = static_cast<double> (s.frames);
    const float gStep = numFrames > 0 ? (g1 - g0) / static_cast<float> (numFrames) : 0.0f;
    float mg = g0;

    // --- streaming : ce qui est disponible au-delà de la partie résidente ---
    const std::int64_t resident = s.resident();
    DiskStreamer::View ring;
    if constexpr (Streamed)
    {
        const int slot = static_cast<int> (&v - voices_.data());
        if (v.streaming && streamer_ != nullptr)
        {
            streamer_->setReadPos (slot, std::max<std::int64_t> (resident, static_cast<std::int64_t> (v.pos) - 1));
            ring = streamer_->view (slot, v.streamGen);
        }
        // Sous-alimentation : les frames dont ce bloc aura besoin ne sont pas encore là. La voix s'éteint en fondu court sur
        // ce qui est disponible (sans clic) ; l'incident est compté.
        if (! v.stolen && ! loop)
        {
            const std::int64_t lastNeeded = std::min<std::int64_t> (s.frames - 1, static_cast<std::int64_t> (v.pos + inc * numFrames) + 3);
            if (lastNeeded >= resident)
            {
                const std::int64_t have = ring.ready ? ring.writePos : resident; // frames < have sont lisibles
                if (have <= lastNeeded && have < s.frames)
                {
                    const double outLeft = (static_cast<double> (have) - 4.0 - v.pos) / std::max (inc, 1e-9);
                    if (streamer_ != nullptr) streamer_->countUnderrun();
                    if (outLeft < 2.0) { v.active = false; return; }
                    v.stolen = true;
                    v.fadeStep = static_cast<float> (1.0 / std::max (1.0, std::min (kStealFadeSeconds * sampleRate_, outLeft)));
                }
            }
        }
    }

    auto tap = [&] (int channel, std::int64_t k) noexcept -> float
    {
        if constexpr (Streamed)
        {
            if (k >= resident)
            {
                if (k >= ring.writePos || ! ring.ready) return 0.0f;
                return (channel == 0 ? ring.left : ring.right)[k & (DiskStreamer::kRingFrames - 1)];
            }
        }
        return s.at (channel, k);
    };

    for (int i = 0; i < numFrames; ++i)
    {
        if (v.pos >= frames) { v.active = false; return; } // fin du sample (one-shot)

        const std::int64_t i0 = static_cast<std::int64_t> (v.pos);
        const float t = static_cast<float> (v.pos - static_cast<double> (i0));
        const std::int64_t a = wrapIndex (z, i0 - 1), b = wrapIndex (z, i0), c = wrapIndex (z, i0 + 1), d = wrapIndex (z, i0 + 2);
        const float l = hermite (tap (0, a), tap (0, b), tap (0, c), tap (0, d), t);
        const float r = stereo ? hermite (tap (1, a), tap (1, b), tap (1, c), tap (1, d), t) : l;

        const float envValue = v.env.next (z.env, sampleRate_);
        if (v.env.finished() && envValue <= 0.0f) { v.active = false; return; } // enveloppe terminée (silence)
        float e = v.velocity * z.gain * v.layerGain * mg * envValue;
        if (v.stolen)
        {
            v.fade -= v.fadeStep;
            if (v.fade <= 0.0f) { v.active = false; return; }
            e *= v.fade;
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
    // Les voix terminées rendent leur emplacement de streaming (le thread disque ferme le fichier).
    if (streamer_ != nullptr)
        for (auto& v : voices_)
            if (v.streaming && ! v.active)
            {
                streamer_->release (static_cast<int> (&v - voices_.data()));
                v.streaming = false;
            }
}
} // namespace neptune::core
