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
    if (v.streaming && streamer_ != nullptr) streamer_->release (v.streamSlot);
    v = Voice {};
}

void SamplerVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

SlotMods SamplerVoices::slotModsFor (const Zone& zone) const noexcept
{
    if (instrument_ == nullptr) return SlotMods {};
    return resolveSlotMods (instrument_->slots, slotAmounts_.data(), zone.sourceIndex, zone.groupId);
}

void SamplerVoices::bindZone (Voice& v, const Zone& zone) noexcept
{
    v.zone = &zone;
    v.mods = slotModsFor (zone);
    const double semis = static_cast<double> (v.key - zone.rootNote) + (zone.tuneCents + v.mods.tuneCents) / 100.0;
    v.baseRatio = zone.sample->sampleRate / sampleRate_ * std::exp2 (semis / 12.0);
    v.targetRatio = v.baseRatio;
    v.glideLeft = 0;
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
    stealFadeSeconds_ = instrument != nullptr ? static_cast<double> (std::clamp (instrument->cutFadeMs, 1, InstrumentDescription::maxCutFadeMs)) / 1000.0
                                              : kStealFadeSeconds;
    numHeld_ = 0;
}

void SamplerVoices::allSoundOff() noexcept
{
    numHeld_ = 0;
    numDown_ = 0;
    for (auto& v : voices_) discard (v);
}

SamplerVoices::VoiceInfo SamplerVoices::describe (const Voice& v) const noexcept
{
    VoiceInfo i;
    i.sourceIndex = v.zone->sourceIndex;
    i.key = v.key;
    i.preview = v.channel == kPreviewChannel;
    i.channel = i.preview ? 0 : v.channel;
    i.velocity = std::clamp (static_cast<int> (std::lround (v.velocity * 127.0f)), 1, 127);
    i.releasing = v.releasing;
    i.fading = v.stolen;
    i.streaming = v.streaming;
    i.layerGain = v.layerGain;
    i.level = v.level;
    const double frames = v.zone->sample != nullptr ? static_cast<double> (v.zone->sample->frames) : 0.0;
    i.progress = frames > 0.0 ? static_cast<float> (std::clamp (v.pos / frames, 0.0, 1.0)) : 0.0f;
    i.order = v.order;
    return i;
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

int SamplerVoices::soundingVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active && ! v.stolen; }));
}

namespace
{
// ordre de sacrifice : relâchement d'abord, puis niveau croissant, puis la plus ancienne
bool sacrificedBefore (bool relA, float levA, std::uint64_t ordA, bool relB, float levB, std::uint64_t ordB) noexcept
{
    if (relA != relB) return relA;
    if (levA != levB) return levA < levB;
    return ordA < ordB;
}
} // namespace

bool SamplerVoices::weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept
{
    const Voice* best = nullptr;
    for (const auto& v : voices_)
    {
        if (! v.active || v.stolen) continue;
        if (best == nullptr || sacrificedBefore (v.releasing, v.level, v.order, best->releasing, best->level, best->order)) best = &v;
    }
    if (best == nullptr) return false;
    releasing = best->releasing;
    level = best->level;
    order = best->order;
    return true;
}

void SamplerVoices::stealWeakest() noexcept
{
    Voice* best = nullptr;
    for (auto& v : voices_)
    {
        if (! v.active || v.stolen) continue;
        if (best == nullptr || sacrificedBefore (v.releasing, v.level, v.order, best->releasing, best->level, best->order)) best = &v;
    }
    if (best != nullptr) steal (*best);
}

void SamplerVoices::steal (Voice& v) noexcept
{
    v.stolen = true;
    v.fadeStep = static_cast<float> (1.0 / std::max (1.0, stealFadeSeconds_ * sampleRate_));
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

void SamplerVoices::markDown (int channel, int key) noexcept
{
    for (int i = 0; i < numDown_; ++i)
        if (down_[static_cast<std::size_t> (i)].channel == channel && down_[static_cast<std::size_t> (i)].key == key)
        {
            down_[static_cast<std::size_t> (i)] = { channel, key, 0, {} }; // touche rejouée : ses Groupes repartent de zéro
            return;
        }
    if (numDown_ >= kMaxDownKeys) // pile pleine (ne devrait pas arriver) : on oublie la plus ancienne
    {
        for (int i = 1; i < numDown_; ++i) down_[static_cast<std::size_t> (i - 1)] = down_[static_cast<std::size_t> (i)];
        --numDown_;
    }
    down_[static_cast<std::size_t> (numDown_++)] = { channel, key, 0, {} };
}

void SamplerVoices::markUp (int channel, int key) noexcept
{
    int w = 0;
    for (int i = 0; i < numDown_; ++i)
        if (! (down_[static_cast<std::size_t> (i)].channel == channel && down_[static_cast<std::size_t> (i)].key == key))
            down_[static_cast<std::size_t> (w++)] = down_[static_cast<std::size_t> (i)];
    numDown_ = w;
}

void SamplerVoices::recordGroups (int channel, int key, const Instrument::Pick* picks, int n) noexcept
{
    for (int i = 0; i < numDown_; ++i)
    {
        auto& d = down_[static_cast<std::size_t> (i)];
        if (d.channel != channel || d.key != key) continue;
        for (int p = 0; p < n; ++p)
        {
            const int g = picks[p].zone->groupId;
            bool known = false;
            for (int k = 0; k < d.nGroups; ++k) known = known || d.groups[static_cast<std::size_t> (k)] == g;
            if (! known && d.nGroups < static_cast<int> (d.groups.size())) d.groups[static_cast<std::size_t> (d.nGroups++)] = g;
        }
        return;
    }
}

// Une AUTRE touche tenue a déclenché une zone de ce Groupe (qu'elle sonne encore ou non : c'est la touche qui compte).
bool SamplerVoices::otherHeldInGroup (int groupId, int channel, int key) const noexcept
{
    for (int i = 0; i < numDown_; ++i)
    {
        const auto& d = down_[static_cast<std::size_t> (i)];
        if (d.channel == channel && d.key == key) continue;
        for (int k = 0; k < d.nGroups; ++k)
            if (d.groups[static_cast<std::size_t> (k)] == groupId) return true;
    }
    return false;
}

Instrument::HeldQuery SamplerVoices::heldQuery (int channel, int key) noexcept
{
    queryContext_ = { this, channel, key };
    return { [] (const void* c, int group) noexcept
             {
                 const auto* q = static_cast<const QueryContext*> (c);
                 return q->self->otherHeldInGroup (group, q->channel, q->key);
             },
             &queryContext_ };
}

// Une zone qui se déclenche coupe les voix des zones dont `chokeCutBy` est son `chokeCuts`. Les voix de la même place
// (alternatives round-robin comprises) ne sont coupées que si l'instrument le demande.
void SamplerVoices::chokeFor (const Zone& zone) noexcept
{
    if (zone.chokeCuts == 0 || instrument_ == nullptr) return;
    const int newPlace = instrument_->placeOf (static_cast<int> (&zone - instrument_->zones.data()));
    for (auto& v : voices_)
    {
        if (! v.active || v.stolen || v.zone == nullptr || v.zone->chokeCutBy != zone.chokeCuts) continue;
        if (! instrument_->chokeSameZone && newPlace >= 0 && instrument_->placeOf (v.zoneIndex) == newPlace) continue;
        steal (v);
    }
}

void SamplerVoices::startNote (int channel, int key, float velocity) noexcept
{
    Instrument::Pick picks[Instrument::maxPicks];
    const auto held = heldQuery (channel, key);
    const int n = instrument_->selectLayers (key, velocity, /*release*/ false, picks, Instrument::maxPicks, &held);
    recordGroups (channel, key, picks, n);
    for (int i = 0; i < n; ++i) chokeFor (*picks[i].zone); // avant de démarrer : les nouvelles voix du même coup ne sont pas coupées
    for (int i = 0; i < n; ++i) startVoice (channel, key, velocity, *picks[i].zone, picks[i].gain);
}

void SamplerVoices::pushHeld (int channel, int key, float velocity) noexcept
{
    removeHeld (channel, key); // une même touche n'y figure qu'une fois (la plus récente en dernier)
    if (numHeld_ >= kMaxHeldKeys)
    {
        for (int i = 1; i < numHeld_; ++i) held_[static_cast<std::size_t> (i - 1)] = held_[static_cast<std::size_t> (i)];
        --numHeld_;
    }
    held_[static_cast<std::size_t> (numHeld_++)] = { channel, key, velocity };
}

void SamplerVoices::removeHeld (int channel, int key) noexcept
{
    int w = 0;
    for (int i = 0; i < numHeld_; ++i)
        if (! (held_[static_cast<std::size_t> (i)].channel == channel && held_[static_cast<std::size_t> (i)].key == key))
            held_[static_cast<std::size_t> (w++)] = held_[static_cast<std::size_t> (i)];
    numHeld_ = w;
}

void SamplerVoices::cutKey (int channel, int key) noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen && v.channel == channel && v.key == key) steal (v);
}

void SamplerVoices::cutAll() noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen) steal (v);
}

bool SamplerVoices::retargetHeld (int newKey) noexcept
{
    bool any = false;
    const int glideFrames = instrument_ != nullptr ? static_cast<int> (instrument_->glideMs * 0.001 * sampleRate_) : 0;
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.zone == nullptr) continue;
        v.key = newKey;
        const double semis = static_cast<double> (newKey - v.zone->rootNote) + (v.zone->tuneCents + v.mods.tuneCents) / 100.0;
        v.targetRatio = v.zone->sample->sampleRate / sampleRate_ * std::exp2 (semis / 12.0);
        if (glideFrames > 0)
        {
            v.glideLeft = glideFrames;
            v.glideDelta = (v.targetRatio - v.baseRatio) / static_cast<double> (glideFrames);
        }
        else
        {
            v.baseRatio = v.targetRatio;
            v.glideLeft = 0;
        }
        any = true;
    }
    return any;
}

void SamplerVoices::noteOn (int channel, int key, float velocity) noexcept
{
    if (instrument_ == nullptr) return;
    // Courbe de réponse de l'instrument : avant le choix des couches et le volume.
    const float shaped = std::clamp (instrument_->shapeVelocity (velocity), 0.0f, 1.0f);
    lastShaped_ = shaped;
    if (key >= 0 && key < 128)
    {
        heldVelocity_[static_cast<std::size_t> (key)] = shaped;
    }
    markDown (channel, key);

    if (instrument_->polyphony != PolyphonyMode::Poly)
    {
        // Mono : une seule note à la fois (la plus récente) ; les touches tenues sont mémorisées pour revenir à la précédente.
        const bool otherHeld = numHeld_ > 0;
        pushHeld (channel, key, shaped);
        if (instrument_->polyphony == PolyphonyMode::Legato && otherHeld && retargetHeld (key)) return; // la voix continue
        cutAll();
        startNote (channel, key, shaped);
        return;
    }
    if (instrument_->sameKey == SameKeyMode::Cut) cutKey (channel, key);
    startNote (channel, key, shaped);
}

SamplerVoices::Voice* SamplerVoices::chooseVictim (int newKey, bool& dropNew) noexcept
{
    dropNew = false;
    Voice* victim = nullptr;
    auto better = [] (const Voice& a, const Voice* b, VoiceStealMode mode)
    {
        if (b == nullptr) return true;
        switch (mode)
        {
            case VoiceStealMode::Quietest: return a.level < b->level || (a.level == b->level && a.order < b->order);
            case VoiceStealMode::KeepLow: return a.key > b->key || (a.key == b->key && a.order < b->order);   // vole la plus aiguë
            case VoiceStealMode::KeepHigh: return a.key < b->key || (a.key == b->key && a.order < b->order);  // vole la plus grave
            default: return a.order < b->order;
        }
    };
    const auto mode = instrument_ != nullptr ? instrument_->stealMode : VoiceStealMode::Oldest;
    for (auto& v : voices_)
        if (v.active && ! v.releasing && ! v.stolen && better (v, victim, mode)) victim = &v;
    // « garder les graves/aigus » : si la nouvelle note est elle-même la plus extrême, c'est elle qu'on abandonne
    if (victim != nullptr)
    {
        if (mode == VoiceStealMode::KeepLow && newKey >= victim->key) dropNew = true;
        if (mode == VoiceStealMode::KeepHigh && newKey <= victim->key) dropNew = true;
    }
    return victim;
}

void SamplerVoices::startVoice (int channel, int key, float velocity, const Zone& zone, float layerGain) noexcept
{
    if (zone.sample == nullptr || zone.loopEnd <= zone.loopStart) return;

    // Limite de voix (réglable par instrument ; les modes mono/legato n'ont pas de limite propre) : on fait s'éteindre une voix
    // tenue selon le mode de vol (fondu court, pas de clic).
    const int limit = (instrument_ != nullptr && instrument_->polyphony == PolyphonyMode::Poly)
                          ? std::clamp (instrument_->maxVoices, 1, kMaxVoices) : kMaxVoices;
    for (int guard = 0; heldVoices() >= limit && guard < kMaxVoices; ++guard)
    {
        bool dropNew = false;
        Voice* victim = chooseVictim (key, dropNew);
        if (victim == nullptr) break;
        if (dropNew) return;
        steal (*victim);
    }

    Voice* v = allocate();
    if (v == nullptr) return;
    discard (*v);
    v->active = true;
    v->channel = channel;
    v->key = key;
    v->zoneIndex = static_cast<int> (&zone - instrument_->zones.data());
    v->order = ++counter_;
    {
        const std::int64_t endF = zone.endFrame > zone.startFrame ? zone.endFrame : zone.sample->frames;
        if (! zone.reverse) v->pos = static_cast<double> (zone.startFrame);
        else v->pos = static_cast<double> (zone.mode == PlayMode::Loop ? zone.loopEnd : endF) - 1.0;   // lecture à l'envers : on part de la fin
        v->delayLeft = zone.delayMs > 0.0 ? static_cast<int> (std::lround (zone.delayMs * 0.001 * sampleRate_)) : 0;
        v->fadeInPlayed = 0.0;
    }
    bindZone (*v, zone);
    v->velocity = std::clamp (velocity, 0.0f, 1.0f);
    v->layerGain = layerGain;
    v->level = v->velocity * zone.gain * v->mods.gain * layerGain;
    v->targetRatio = v->baseRatio;
    if (v->mods.any)
    {
        auto timing = EnvelopeTiming::of (zone.env);
        timing.attackSec *= v->mods.attack;
        timing.decaySec *= v->mods.decay;
        timing.sustain = std::clamp (timing.sustain * v->mods.sustain, 0.0f, 1.0f);
        v->env.begin (zone.env, sampleRate_, &timing);
    }
    else
        v->env.begin (zone.env, sampleRate_);
    if (zone.filters != nullptr)
        for (std::size_t i = 0; i < 2; ++i)
            if (zone.filters->settings[i].on) v->fenv[i].begin (zone.filters->env[i], sampleRate_);
    // Échantillon dont seul le début est en mémoire : le reste est lu sur le disque. Les zones en boucle gardent tout ce
    // qui précède la fin de boucle en mémoire (voir residentFramesNeeded) et n'ont donc jamais besoin du flux.
    if (zone.mode == PlayMode::OneShot && zone.sample->isStreamed() && streamer_ != nullptr)
    {
        const int slot = streamer_->claim (*zone.sample);
        if (slot >= 0)
        {
            v->streaming = true;
            v->streamSlot = slot;
            v->streamGen = streamer_->generation (slot);
        }
    }
}

void SamplerVoices::noteOff (int channel, int key) noexcept
{
    markUp (channel, key);
    if (instrument_ != nullptr && instrument_->polyphony != PolyphonyMode::Poly)
    {
        // Mono/legato : la touche relâchée sort de la pile ; s'il en reste une tenue et que c'était celle qui sonnait, on y revient.
        bool wasSounding = false;
        for (const auto& v : voices_)
            if (v.active && ! v.releasing && ! v.stolen && v.channel == channel && v.key == key) { wasSounding = true; break; }
        removeHeld (channel, key);
        if (wasSounding && numHeld_ > 0)
        {
            const HeldKey back = held_[static_cast<std::size_t> (numHeld_ - 1)];
            if (instrument_->polyphony == PolyphonyMode::Legato && retargetHeld (back.key))
            {
                // la voix continue à la hauteur de la touche précédente
            }
            else
            {
                cutAll();
                startNote (back.channel, back.key, back.velocity);
            }
            releaseZonesFor (channel, key);
            return;
        }
    }
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.channel != channel || v.key != key) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            releaseEnvelopes (v);
        }
        // one-shot : le Note Off est ignoré, le sample est joué en entier
    }

    releaseZonesFor (channel, key);
}

// Zones déclenchées au relâchement (bruit de mécanique, fin de note) : jouées à la vélocité du Note On (déjà passée par la
// courbe de l'instrument, voir noteOn).
void SamplerVoices::releaseZonesFor (int channel, int key) noexcept
{
    if (instrument_ == nullptr || key < 0 || key >= 128) return;
    const float vel = heldVelocity_[static_cast<std::size_t> (key)];
    const float v = vel > 0.0f ? vel : 0.5f;
    Instrument::Pick picks[Instrument::maxPicks];
    const int n = instrument_->selectLayers (key, v, /*release*/ true, picks, Instrument::maxPicks);
    for (int i = 0; i < n; ++i) chokeFor (*picks[i].zone);
    for (int i = 0; i < n; ++i) startVoice (channel, key, v, *picks[i].zone, picks[i].gain);
}

void SamplerVoices::renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept
{
    // Quatre variantes figées à la compilation : une voix sans filtre garde la boucle la plus courte possible.
    const bool streamed = v.zone->sample->isStreamed();
    const bool filtered = v.zone->filters != nullptr && v.zone->filters->anyOn;
    if (streamed)
    {
        if (filtered) renderVoiceT<true, true> (v, left, right, numFrames, g0, g1, pitchFactor);
        else renderVoiceT<true, false> (v, left, right, numFrames, g0, g1, pitchFactor);
    }
    else
    {
        if (filtered) renderVoiceT<false, true> (v, left, right, numFrames, g0, g1, pitchFactor);
        else renderVoiceT<false, false> (v, left, right, numFrames, g0, g1, pitchFactor);
    }
}

template <bool Streamed, bool Filtered>
void SamplerVoices::renderVoiceT (Voice& v, float* left, float* right, int numFrames, float g0, float g1, double pitchFactor) noexcept
{
    const Zone& z = *v.zone;
    const SampleData& s = *z.sample;
    const bool stereo = s.channels >= 2;
    const bool loop = z.mode == PlayMode::Loop;
    const FilterRuntimeBlock* fr = z.filters.get(); // non nul quand Filtered
    const bool on0 = fr != nullptr && fr->settings[0].on, on1 = fr != nullptr && fr->settings[1].on;
    const bool parallelFilters = fr != nullptr && fr->parallel;
    const float filterBalance = fr != nullptr ? fr->balance : 0.5f;
    bool envModulated = false; // une enveloppe de filtre qui module vraiment la coupure : coefficients refaits tous les 16 échantillons
    if constexpr (Filtered)
    {
        updateFilterCoefs (v, *fr); // début de bloc : paramètres automatisables, clavier, vélocité
        for (std::size_t f = 0; f < 2; ++f) envModulated = envModulated || (fr->settings[f].on && fr->settings[f].envDepthOctaves != 0.0);
    }
    double ratio = v.baseRatio;
    float lastEnv = 0.0f;
    const bool reverse = z.reverse;
    const double dir = reverse ? -1.0 : 1.0;
    double inc = dir * ratio * pitchFactor;
    const double frames = static_cast<double> (s.frames);
    const std::int64_t startF = z.startFrame, endF = z.endFrame > z.startFrame ? z.endFrame : s.frames;
    const double endLimit = loop ? frames : static_cast<double> (endF);
    const double fadeInOut = z.fadeInMs > 0.0 ? z.fadeInMs * 0.001 * sampleRate_ : 0.0;
    const double fadeOutOut = (! loop && z.fadeOutMs > 0.0) ? z.fadeOutMs * 0.001 * sampleRate_ : 0.0;
    const bool crossfade = loop && ! reverse && z.loopCrossfade > 0;
    const double xfade = static_cast<double> (z.loopCrossfade), loopEndD = static_cast<double> (z.loopEnd), loopLen = static_cast<double> (z.loopEnd - z.loopStart);
    const float gStep = numFrames > 0 ? (g1 - g0) / static_cast<float> (numFrames) : 0.0f;
    float mg = g0;

    // --- streaming : ce qui est disponible au-delà de la partie résidente ---
    const std::int64_t resident = s.resident();
    DiskStreamer::View ring;
    if constexpr (Streamed)
    {
        const int slot = v.streamSlot;
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
        if (v.delayLeft > 0) { --v.delayLeft; continue; } // délai de lecture : rien ne se passe encore (l'enveloppe attend aussi)
        if (reverse ? v.pos < static_cast<double> (startF) && ! loop : v.pos >= endLimit) { v.active = false; return; } // fin de la région jouée (one-shot)

        const std::int64_t i0 = static_cast<std::int64_t> (std::floor (v.pos));
        const float t = static_cast<float> (v.pos - static_cast<double> (i0));
        const std::int64_t a = wrapIndex (z, i0 - 1), b = wrapIndex (z, i0), c = wrapIndex (z, i0 + 1), d = wrapIndex (z, i0 + 2);
        float l = hermite (tap (0, a), tap (0, b), tap (0, c), tap (0, d), t);
        float r = stereo ? hermite (tap (1, a), tap (1, b), tap (1, c), tap (1, d), t) : l;
        if (crossfade && v.pos >= loopEndD - xfade && v.pos < loopEndD)
        {
            // fondu enchaîné de boucle : le son qui précède le début de boucle (une longueur de boucle plus tôt) est mélangé en puissance constante
            const double w = (v.pos - (loopEndD - xfade)) / xfade;
            const float g1 = static_cast<float> (std::cos (w * 1.5707963267948966)), g2 = static_cast<float> (std::sin (w * 1.5707963267948966));
            const std::int64_t j0 = i0 - static_cast<std::int64_t> (loopLen);
            const float l2 = hermite (tap (0, j0 - 1), tap (0, j0), tap (0, j0 + 1), tap (0, j0 + 2), t);
            const float r2 = stereo ? hermite (tap (1, j0 - 1), tap (1, j0), tap (1, j0 + 1), tap (1, j0 + 2), t) : l2;
            l = l * g1 + l2 * g2;
            r = stereo ? r * g1 + r2 * g2 : l;
        }

        if constexpr (Filtered) // filtres par voie : coefficients mis à jour tous les 16 échantillons, enveloppes au rythme de l'échantillon
        {
            if (envModulated && (v.fcount++ & 15) == 15)
            {
                // enveloppes de filtre à cadence réduite : un pas de 16 échantillons, puis nouveaux coefficients
                for (std::size_t f = 0; f < 2; ++f)
                    if (fr->settings[f].on && fr->settings[f].envDepthOctaves != 0.0)
                    {
                        v.fenv[f].advance (fr->env[f], sampleRate_, 16.0);
                        v.fenvValue[f] = v.fenv[f].value();
                    }
                updateFilterCoefs (v, *fr);
            }
            // 1e-15 : empêche les nombres dénormaux quand le signal tombe à zéro (inaudible : -300 dB)
            auto chain = [&] (float x, std::size_t ch) noexcept
            {
                x += 1e-15f;
                if (! parallelFilters)
                {
                    if (on0) x = processFilter (v.fcoef[0], v.fstate[0][ch], x);
                    if (on1) x = processFilter (v.fcoef[1], v.fstate[1][ch], x);
                    return x;
                }
                if (on0 && on1)
                {
                    const float y0 = processFilter (v.fcoef[0], v.fstate[0][ch], x);
                    const float y1 = processFilter (v.fcoef[1], v.fstate[1][ch], x);
                    return y0 + filterBalance * (y1 - y0);
                }
                return on0 ? processFilter (v.fcoef[0], v.fstate[0][ch], x) : processFilter (v.fcoef[1], v.fstate[1][ch], x);
            };
            l = chain (l, 0);
            r = stereo ? chain (r, 1) : l; // échantillon mono : un seul canal à filtrer
        }

        const float envValue = v.env.next (z.env, sampleRate_);
        if (v.env.finished() && envValue <= 0.0f) { v.active = false; return; } // enveloppe terminée (silence)
        float e = v.velocity * z.gain * v.mods.gain * v.layerGain * mg * envValue;
        if (fadeInOut > 0.0 && v.fadeInPlayed < fadeInOut)
        {
            e *= static_cast<float> (v.fadeInPlayed / fadeInOut);
            v.fadeInPlayed += 1.0;
        }
        if (fadeOutOut > 0.0)   // fondu de sortie : sur la fin de la région jouée (durée mesurée en échantillons de sortie)
        {
            const double remaining = (reverse ? v.pos - static_cast<double> (startF) : static_cast<double> (endF) - v.pos) / std::max (std::abs (inc), 1e-9);
            if (remaining < fadeOutOut) e *= static_cast<float> (std::max (0.0, remaining / fadeOutOut));
        }
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
        if (v.glideLeft > 0) // legato : glissando de la hauteur
        {
            ratio += v.glideDelta;
            if (--v.glideLeft == 0) ratio = v.targetRatio;
            inc = dir * ratio * pitchFactor;
        }
        lastEnv = envValue;
        if (loop && ! reverse && v.pos >= static_cast<double> (z.loopEnd))
        {
            const double len = static_cast<double> (z.loopEnd - z.loopStart);
            v.pos = static_cast<double> (z.loopStart) + std::fmod (v.pos - static_cast<double> (z.loopStart), len);
        }
        else if (loop && reverse && v.pos < static_cast<double> (z.loopStart))
        {
            const double len = static_cast<double> (z.loopEnd - z.loopStart);
            v.pos = static_cast<double> (z.loopEnd) - std::fmod (static_cast<double> (z.loopEnd) - v.pos, len);
        }
    }
    v.baseRatio = ratio; // le glissando du legato se poursuit au bloc suivant
    v.level = v.velocity * z.gain * v.mods.gain * v.layerGain * lastEnv; // pour le vol de la voix la plus faible
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
                streamer_->release (v.streamSlot);
                v.streaming = false;
                v.streamSlot = -1;
            }
}

void SamplerVoices::releaseEnvelopes (Voice& v) noexcept
{
    // le relâchement lit les emplacements au moment du Note Off (le charley ouvert qu'on raccourcit pendant qu'on le tient)
    const SlotMods now = slotModsFor (*v.zone);
    if (now.release != 1.0f)
    {
        auto timing = EnvelopeTiming::of (v.zone->env);
        timing.releaseSec *= now.release;
        v.env.noteOff (v.zone->env, sampleRate_, &timing);
    }
    else
        v.env.noteOff (v.zone->env, sampleRate_);
    if (const auto* fr = v.zone->filters.get())
        for (std::size_t i = 0; i < 2; ++i)
            if (fr->settings[i].on) v.fenv[i].noteOff (fr->env[i], sampleRate_);
}

void SamplerVoices::updateFilterCoefs (Voice& v, const FilterRuntimeBlock& fr) const noexcept
{
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& f = fr.settings[i];
        if (! f.on) continue;
        const double base = fr.fromParams[i] ? filterParams_.cutoffHz[i] : f.cutoffHz;
        const double res = fr.fromParams[i] ? filterParams_.resonance[i] : f.resonance;
        const double cutoff = modulatedCutoffHz (f, base, v.key, v.velocity, v.fenvValue[i], sampleRate_);
        v.fcoef[i] = makeFilterCoefs (f, cutoff, res, sampleRate_);
    }
}

void SamplerVoices::panic() noexcept
{
    cutAll();
    numHeld_ = 0;
    numDown_ = 0;
}

void SamplerVoices::releaseAll() noexcept
{
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.zone == nullptr || v.channel == kPreviewChannel) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            releaseEnvelopes (v);
        }
    }
    numHeld_ = 0;
    numDown_ = 0;
}

void SamplerVoices::previewZone (int sourceIndex) noexcept
{
    if (instrument_ == nullptr) return;
    stopPreview(); // une écoute à la fois
    for (const auto& z : instrument_->zones)
    {
        if (z.sourceIndex != sourceIndex || z.sample == nullptr) continue;
        // vélocité : le milieu de la plage de la zone (on entend ce que la zone joue à sa vélocité « typique »)
        const float velocity = static_cast<float> (z.velLow + z.velHigh) / 254.0f;
        startVoice (kPreviewChannel, z.rootNote, std::clamp (velocity, 0.0f, 1.0f), z, 1.0f);
        return;
    }
}

void SamplerVoices::releasePreview() noexcept
{
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.channel != kPreviewChannel || v.zone == nullptr) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            releaseEnvelopes (v);
        }
    }
}

void SamplerVoices::stopPreview() noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen && v.channel == kPreviewChannel) steal (v);
}
} // namespace neptune::core
