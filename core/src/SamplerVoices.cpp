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

} // namespace

void SamplerVoices::discard (Voice& v) noexcept
{
    if (v.streaming && streamer_ != nullptr) streamer_->release (v.streamSlot);
    if (v.active) --activeCount_;
    v = Voice {};
}

void SamplerVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    levelCoefs_.prepare (sampleRate_);
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
    const double semis = static_cast<double> (v.key - zone.rootNote) + (zone.tuneCents + v.mods.tuneCents + v.humanCents) / 100.0;
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
    droneOn_.fill (false);
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

bool SamplerVoices::modAllows (ModMask mask) const noexcept
{
    if (mask == 0 || instrument_ == nullptr) return true;
    for (int i = 0; i < kModulators; ++i)
    {
        if ((mask & (1u << i)) == 0) continue;
        const float v = modEff_[static_cast<std::size_t> (i)];
        const auto& r = instrument_->modulators[static_cast<std::size_t> (i)];
        if (r.continuous ? (r.floorLevel <= 0.0f && v <= 0.001f) : v < 0.5f) return false;
    }
    return true;
}

float SamplerVoices::modFactor (ModMask mask) const noexcept
{
    if (mask == 0 || instrument_ == nullptr) return 1.0f;
    float f = 1.0f;
    for (int i = 0; i < kModulators; ++i)
    {
        if ((mask & (1u << i)) == 0) continue;
        const float v = modEff_[static_cast<std::size_t> (i)];
        const auto& r = instrument_->modulators[static_cast<std::size_t> (i)];
        f *= r.continuous ? r.floorLevel + (1.0f - r.floorLevel) * v : (v >= 0.5f ? 1.0f : 0.0f);
    }
    return f;
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

Instrument::HeldQuery SamplerVoices::heldQuery (int channel, int key, int velocity, int heldMs) noexcept
{
    queryContext_ = { this, channel, key, velocity, heldMs };
    return { [] (const void* c, int group) noexcept
             {
                 const auto* q = static_cast<const QueryContext*> (c);
                 return q->self->otherHeldInGroup (group, q->channel, q->key);
             },
             &queryContext_,
             // condition MIDI d'une zone : jugée au déclenchement, sur l'état des contrôleurs et sur la note qui se déclenche
             [] (const void* c, const Zone& z) noexcept
             {
                 const auto* q = static_cast<const QueryContext*> (c);
                 if (! q->self->modAllows (z.modMask)) return false;   // un modulateur qui la commande est coupé
                 if (z.onRelease && q->heldMs >= 0)                    // durée d'appui d'une zone de relâchement
                 {
                     if (q->heldMs < z.heldMinMs) return false;
                     if (z.heldMaxMs > 0 && q->heldMs > z.heldMaxMs) return false;
                 }
                 if (z.midiCond == nullptr) return true;
                 static const midi::State empty;
                 midi::Message m;
                 m.kind = midi::MsgKind::NoteOn;
                 m.channel = q->channel & 15;
                 m.data1 = q->key;
                 m.data2 = q->velocity;
                 return z.midiCond->passesState (q->self->midiState_ != nullptr ? *q->self->midiState_ : empty, m);
             } };
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
    const auto held = heldQuery (channel, key, std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127));
    const int n = instrument_->selectLayers (key, velocity, /*release*/ false, picks, Instrument::maxPicks, &held);
    lastPicks_.store (n, std::memory_order_relaxed);
    int covering = 0;
    for (const auto& z : instrument_->zones)
        if (! z.onRelease && key >= z.keyLow && key <= z.keyHigh) ++covering;
    lastKeyZones_.store (covering, std::memory_order_relaxed);
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
        if (v.active && ! v.stolen && v.channel != kDroneChannel) steal (v);   // un son continu ne se coupe pas à chaque note (mono)
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
        heldAtFrames_[static_cast<std::size_t> (key)] = clockFrames_;
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
    ++activeCount_;
    v->channel = channel;
    v->key = key;
    v->zoneIndex = instrument_ != nullptr && channel != kPreviewChannel ? static_cast<int> (&zone - instrument_->zones.data()) : -1;
    v->order = ++counter_;
    {
        const std::int64_t endF = zone.endFrame > zone.startFrame ? zone.endFrame : zone.sample->frames;
        if (! zone.reverse) v->pos = static_cast<double> (zone.startFrame);
        else v->pos = static_cast<double> (zone.mode == PlayMode::Loop ? zone.loopEnd : endF) - 1.0;   // lecture à l'envers : on part de la fin
        v->delayLeft = zone.delayMs > 0.0 ? static_cast<int> (std::lround (zone.delayMs * 0.001 * sampleRate_)) : 0;
        v->fadeInPlayed = 0.0;
    }
    // humanize : petites variations aléatoires de hauteur, de volume et de départ à chaque déclenchement (pas pour l'écoute depuis l'interface)
    float humanDb = 0.0f;
    int humanDelay = 0;
    v->humanCents = 0.0;
    if (instrument_ != nullptr && channel != kPreviewChannel && zone.humanizeAmount > 0.0f
        && (instrument_->humanizePitchCents > 0.0f || instrument_->humanizeVolumeDb > 0.0f || instrument_->humanizeTimingMs > 0.0f))
    {
        auto unit = [this] () noexcept { humanRng_ ^= humanRng_ << 13; humanRng_ ^= humanRng_ >> 17; humanRng_ ^= humanRng_ << 5; return static_cast<float> (humanRng_ & 0xFFFFFFu) / 8388608.0f - 1.0f; };   // [-1, 1)
        const float a = zone.humanizeAmount;
        v->humanCents = static_cast<double> (instrument_->humanizePitchCents * a * unit());
        humanDb = instrument_->humanizeVolumeDb * a * unit();
        humanDelay = static_cast<int> (std::lround (static_cast<double> (instrument_->humanizeTimingMs * a * 0.5f * (unit() + 1.0f)) * 0.001 * sampleRate_));
    }
    bindZone (*v, zone);
    v->delayLeft += humanDelay;
    v->loopIdx = 0;
    v->repsLeft = (zone.moreLoopCount > 0 && zone.loopRepeats > 0) ? zone.loopRepeats : -1;
    v->modGain = modFactor (zone.modMask);
    v->velocity = std::clamp (velocity, 0.0f, 1.0f);
    {
        // suivi de la vélocité (1 : le volume suit la vélocité ; 0 : constant ; < 0 : plus fort en jouant doucement) et suivi du clavier (dB par octave autour de la note centrale)
        const float t = std::clamp (zone.velTrack, -1.0f, 1.0f);
        float amp = std::max (0.0f, (1.0f - t) + t * v->velocity);
        if (zone.keyTrackDbPerOct != 0.0f)
            amp *= static_cast<float> (std::pow (10.0, static_cast<double> (zone.keyTrackDbPerOct) * static_cast<double> (key - zone.keyTrackCenter) / 12.0 / 20.0));
        if (humanDb != 0.0f) amp *= static_cast<float> (std::pow (10.0, static_cast<double> (humanDb) / 20.0));
        v->ampVel = amp;
    }
    v->layerGain = layerGain;
    v->level = v->ampVel * zone.gain * v->mods.gain * layerGain;
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
            if (v.zone->hasTail) steal (v);   // la queue enregistrée du fichier prend le relais (zone de relâchement implicite) : la boucle s'éteint en fondu rapide
            else
            {
                v.releasing = true;
                releaseEnvelopes (v);
            }
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
    const double heldFrames = static_cast<double> (clockFrames_ - std::min (clockFrames_, heldAtFrames_[static_cast<std::size_t> (key)]));
    const int heldMs = static_cast<int> (std::min (heldFrames * 1000.0 / std::max (1.0, sampleRate_), static_cast<double> (maxHeldMs)));   // durée d'appui de cette touche
    const auto held = heldQuery (channel, key, std::clamp (static_cast<int> (std::lround (v * 127.0f)), 1, 127), heldMs);
    const int n = instrument_->selectLayers (key, v, /*release*/ true, picks, Instrument::maxPicks, &held);
    for (int i = 0; i < n; ++i) chokeFor (*picks[i].zone);
    for (int i = 0; i < n; ++i)
    {
        float g = picks[i].gain;
        if (picks[i].zone->releaseFollowsLevel)
        {
            g *= heardGain (*picks[i].zone, channel, key, v);   // volume lié au niveau réellement entendu de la voix interrompue
            if (g < 1.0e-4f) continue;                       // rien ne sonnait plus : pas de bruit de relâchement
        }
        startVoice (channel, key, v, *picks[i].zone, g);
    }
}

float SamplerVoices::heardGain (const Zone& zone, int channel, int key, float velocity) const noexcept
{
    float peak = 0.0f, ms = 0.0f, msK = 0.0f;
    bool any = false;
    for (const auto& v : voices_)
    {
        if (! v.active || v.zone == nullptr || v.zone->onRelease || v.channel != channel || v.key != key) continue;
        any = true;
        peak = std::max (peak, v.tracker.peak);
        ms = std::max (ms, v.tracker.ms);
        msK = std::max (msK, v.tracker.msK);
    }
    if (! any) return 0.0f;
    const double db = LevelTracker::toDb (zone.releaseMeter, peak, ms, msK);
    const double floorDb = std::min (-1.0, static_cast<double> (zone.releaseFloorDb));
    if (zone.releaseRefDb > -900.0f)
    {
        // marqueur de volume : le niveau du sample de relâchement à son marqueur est ramené au niveau entendu de la voix d'attaque (plus le décalage réglé)
        if (db < floorDb) return 0.0f;
        const double gDb = std::min (24.0, db - static_cast<double> (zone.releaseRefDb) + static_cast<double> (zone.releaseOffsetDb));
        // la vélocité de la voix de relâchement multiplie déjà son volume : le niveau entendu de l'attaque la contient, on la retire
        return static_cast<float> (std::min (16.0, std::pow (10.0, gDb / 20.0) / std::max (0.05f, velocity)));
    }
    const double t = std::clamp ((db - floorDb) / (0.0 - floorDb), 0.0, 1.0);
    return static_cast<float> (std::pow (t, static_cast<double> (std::max (0.05f, zone.releaseCurve))));
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
    const bool loop = v.ovr.on ? v.ovr.loop : z.mode == PlayMode::Loop;
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
    const std::int64_t startF = v.ovr.on ? v.ovr.start : z.startFrame, endF = v.ovr.on ? v.ovr.end : (z.endFrame > z.startFrame ? z.endFrame : s.frames);
    const double endLimit = loop ? frames : static_cast<double> (endF);
    const double fadeInOut = z.fadeInMs > 0.0 ? z.fadeInMs * 0.001 * sampleRate_ : 0.0;
    const double fadeOutOut = (! loop && z.fadeOutMs > 0.0) ? z.fadeOutMs * 0.001 * sampleRate_ : 0.0;
    // boucle en cours de la voix (la première, ou l'une des suivantes quand la zone en a plusieurs)
    std::int64_t ls = z.loopStart, le = z.loopEnd, cxl = z.loopCrossfade;
    auto useLoop = [&] (int idx) noexcept
    {
        if (idx <= 0 || idx > z.moreLoopCount) { ls = z.loopStart; le = z.loopEnd; cxl = z.loopCrossfade; return; }
        const auto& m = z.moreLoops[static_cast<std::size_t> (idx - 1)];
        ls = m.start; le = m.end; cxl = m.crossfade;
    };
    useLoop (v.loopIdx);
    // la boucle revient en arrière tant qu'il reste des retours, ou si c'est la dernière
    auto wrapping = [&] () noexcept { return loop && (v.repsLeft != 0 || v.loopIdx >= z.moreLoopCount); };
    const bool trackLevels = instrument_ != nullptr && instrument_->trackLevels;
    const float gStep = numFrames > 0 ? (g1 - g0) / static_cast<float> (numFrames) : 0.0f;
    float mg = g0;
    // modulateurs : le volume suit en direct leur état (rampe sur le bloc) ; une zone sans modulateur n'a rien à calculer
    const float modTarget = z.modMask != 0 ? modFactor (z.modMask) : 1.0f;
    float modCur = z.modMask != 0 ? v.modGain : 1.0f;
    const float modStep = numFrames > 0 ? (modTarget - modCur) / static_cast<float> (numFrames) : 0.0f;

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
                    if (outLeft < 2.0) { endVoice (v); return; }
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
        if (reverse ? v.pos < static_cast<double> (startF) && ! loop : v.pos >= endLimit) { endVoice (v); return; } // fin de la région jouée (one-shot)

        const std::int64_t i0 = static_cast<std::int64_t> (std::floor (v.pos));
        const float t = static_cast<float> (v.pos - static_cast<double> (i0));
        const bool wrapNow = wrapping();
        auto wrapK = [&] (std::int64_t k) noexcept
        {
            if (wrapNow && k >= le) k = ls + (k - ls) % (le - ls);
            return k;
        };
        const std::int64_t a = wrapK (i0 - 1), b = wrapK (i0), c = wrapK (i0 + 1), d = wrapK (i0 + 2);
        float l = hermite (tap (0, a), tap (0, b), tap (0, c), tap (0, d), t);
        float r = stereo ? hermite (tap (1, a), tap (1, b), tap (1, c), tap (1, d), t) : l;
        if (wrapNow && ! reverse && cxl > 0 && v.pos >= static_cast<double> (le - cxl) && v.pos < static_cast<double> (le))
        {
            // fondu enchaîné de boucle : le son qui précède le début de boucle (une longueur de boucle plus tôt) est mélangé en puissance constante
            const double xfade = static_cast<double> (cxl), loopLen = static_cast<double> (le - ls);
            const double w = (v.pos - (static_cast<double> (le) - xfade)) / xfade;
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
        if (v.env.finished() && envValue <= 0.0f) { endVoice (v); return; } // enveloppe terminée (silence)
        float e = v.ampVel * z.gain * v.mods.gain * v.layerGain * mg * modCur * envValue;
        modCur += modStep;
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
            if (v.fade <= 0.0f) { endVoice (v); return; }
            e *= v.fade;
        }
        if (trackLevels) v.tracker.process (levelCoefs_, l * e * z.panL, r * e * z.panR);
        if (right != nullptr)
        {
            left[i] += l * e * z.panL;
            right[i] += r * e * z.panR;
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
        if (loop && ! reverse && v.pos >= static_cast<double> (le))
        {
            if (v.repsLeft == 0 && v.loopIdx < z.moreLoopCount)
            {
                // les retours de cette boucle sont épuisés : la lecture continue vers la boucle suivante
                ++v.loopIdx;
                useLoop (v.loopIdx);
                const int reps = v.loopIdx < z.moreLoopCount ? z.moreLoops[static_cast<std::size_t> (v.loopIdx - 1)].repeats : 0;
                v.repsLeft = reps > 0 ? reps : -1;
            }
            else
            {
                const double len = static_cast<double> (le - ls);
                v.pos = static_cast<double> (ls) + std::fmod (v.pos - static_cast<double> (ls), len);
                if (v.repsLeft > 0) --v.repsLeft;
            }
        }
        else if (loop && reverse && v.pos < static_cast<double> (z.loopStart))
        {
            const double len = static_cast<double> (z.loopEnd - z.loopStart);
            v.pos = static_cast<double> (z.loopEnd) - std::fmod (static_cast<double> (z.loopEnd) - v.pos, len);
        }
    }
    if (z.modMask != 0) v.modGain = modTarget;
    v.baseRatio = ratio; // le glissando du legato se poursuit au bloc suivant
    v.level = v.ampVel * z.gain * v.mods.gain * v.layerGain * lastEnv; // pour le vol de la voix la plus faible
}

// Valeurs effectives des modulateurs : les valeurs réglées, avec le tremblant (volume qui oscille) pour ceux qui en ont un et dont l'interrupteur est actif.
// Pour un tremblant, la valeur réglée (0..100 %) est sa PROFONDEUR : elle ne coupe jamais le son (0 = pas d'oscillation). Son modulateur de vitesse (s'il en a un)
// multiplie la cadence : 50 % = celle de l'orgue, de 4 fois plus lent (0) à 4 fois plus vite (100 %).
void SamplerVoices::updateModulators (int numFrames) noexcept
{
    const double seconds = static_cast<double> (numFrames) / std::max (sampleRate_, 1.0);
    for (int i = 0; i < kModulators; ++i)
    {
        float v = modValues_[static_cast<std::size_t> (i)];
        if (instrument_ != nullptr)
        {
            const auto& r = instrument_->modulators[static_cast<std::size_t> (i)];
            if (r.continuous && r.tremPeriodMs > 0.0)
            {
                const float depth = std::clamp (v, 0.0f, 1.0f);
                v = 1.0f;
                if (r.tremGate < 0 || modValues_[static_cast<std::size_t> (r.tremGate)] >= 0.5f)
                {
                    const double speed = r.tremSpeed >= 0 ? std::exp2 ((static_cast<double> (modValues_[static_cast<std::size_t> (r.tremSpeed)]) * 100.0 - 50.0) / 25.0) : 1.0;
                    auto& phase = tremPhase_[static_cast<std::size_t> (i)];
                    phase = std::fmod (phase + seconds * speed / (r.tremPeriodMs * 0.001), 1.0);
                    v *= 1.0f - depth * 0.5f * static_cast<float> (1.0 - std::cos (6.283185307179586 * phase));
                }
            }
        }
        modEff_[static_cast<std::size_t> (i)] = v;
    }
}

// Sons continus : une zone « Continuous » démarre dès que ses modulateurs la laissent sonner (ou tout de suite, sans modulateur) et se relâche quand ils se
// coupent. Une boucle qui a disparu (voix volée, tout coupé) repart au bloc suivant ; un one-shot ne repart qu'au rallumage.
void SamplerVoices::updateDrones() noexcept
{
    if (instrument_ == nullptr) return;
    const auto& drones = instrument_->droneZones();
    for (std::size_t k = 0; k < drones.size(); ++k)
    {
        const int zi = drones[k];
        const Zone& z = instrument_->zones[static_cast<std::size_t> (zi)];
        const bool allowed = modAllows (z.modMask);
        bool live = false, any = false;
        for (auto& v : voices_)
        {
            if (! v.active || v.channel != kDroneChannel || v.zoneIndex != zi) continue;
            any = true;
            if (v.stolen) continue;
            if (! allowed && ! v.releasing && z.mode == PlayMode::Loop)
            {
                v.releasing = true;
                releaseEnvelopes (v);
            }
            if (! v.releasing) live = true;
        }
        if (allowed && ! live && (z.mode == PlayMode::Loop ? true : ! droneOn_[k] && ! any))
            startVoice (kDroneChannel, z.rootNote, 1.0f, z, 1.0f);
        droneOn_[k] = allowed;
    }
}

void SamplerVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, double masterTuneCents) noexcept
{
    if (left == nullptr || numFrames <= 0) return;
    const double pitchFactor = masterTuneCents == 0.0 ? 1.0 : std::exp2 (masterTuneCents / 1200.0);
    clockFrames_ += static_cast<std::uint64_t> (numFrames);
    updateModulators (numFrames);
    updateDrones();
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
        if (! v.active || v.releasing || v.stolen || v.zone == nullptr || v.channel == kPreviewChannel || v.channel == kDroneChannel) continue;
        if (v.zone->mode == PlayMode::Loop)
        {
            v.releasing = true;
            releaseEnvelopes (v);
        }
    }
    numHeld_ = 0;
    numDown_ = 0;
}

void SamplerVoices::previewZone (int sourceIndex, const PreviewSpec& spec) noexcept
{
    if (instrument_ == nullptr) return;
    stopPreview(); // une écoute à la fois
    for (const auto& z : instrument_->zones)
    {
        if (z.sourceIndex != sourceIndex || z.sample == nullptr) continue;
        // vélocité : le milieu de la plage de la zone (on entend ce que la zone joue à sa vélocité « typique »)
        const float velocity = static_cast<float> (z.velLow + z.velHigh) / 254.0f;
        startVoice (kPreviewChannel, z.rootNote, std::clamp (velocity, 0.0f, 1.0f), z, 1.0f);
        using Source = PreviewSpec::Source;
        for (auto& v : voices_)
        {
            if (! v.active || v.channel != kPreviewChannel || v.order != counter_) continue;
            const std::int64_t frames = z.sample->frames;
            const std::int64_t zoneEnd = z.endFrame > z.startFrame ? z.endFrame : frames;
            const int loops = 1 + z.moreLoopCount;
            const int li = std::clamp (spec.loopIndex, 0, loops - 1);
            auto loopRange = [&] (int idx, std::int64_t& ls, std::int64_t& le, int& reps)
            {
                if (idx <= 0) { ls = z.loopStart; le = z.loopEnd; reps = z.moreLoopCount > 0 ? z.loopRepeats : 0; }
                else { const auto& m = z.moreLoops[static_cast<std::size_t> (idx - 1)]; ls = m.start; le = m.end; reps = m.repeats; }
            };
            auto place = [&] (std::int64_t from, std::int64_t to, bool loop)
            {
                v.ovr = { true, loop, from, to };
                v.pos = static_cast<double> (z.reverse ? std::max (from, to - 1) : from);
            };
            switch (spec.source)
            {
                case Source::Zone:
                    if (spec.frame >= 0)
                    {
                        const std::int64_t at = std::clamp<std::int64_t> (spec.frame, z.startFrame, std::max<std::int64_t> (z.startFrame, zoneEnd - 1));
                        v.pos = static_cast<double> (at);
                        // zone à plusieurs boucles : on reprend dans la boucle qui contient le point (ou la suivante)
                        if (z.mode == PlayMode::Loop && z.moreLoopCount > 0 && ! z.reverse)
                        {
                            int idx = 0;
                            std::int64_t le = z.loopEnd;
                            while (idx < z.moreLoopCount && at >= le) { le = z.moreLoops[static_cast<std::size_t> (idx)].end; ++idx; }
                            v.loopIdx = idx;
                            const int reps = idx == 0 ? z.loopRepeats : (idx < z.moreLoopCount ? z.moreLoops[static_cast<std::size_t> (idx - 1)].repeats : 0);
                            v.repsLeft = idx >= z.moreLoopCount ? -1 : (reps > 0 ? reps : -1);
                        }
                    }
                    break;
                case Source::Raw: place (0, frames, false); break;
                case Source::Region: place (z.startFrame, zoneEnd, false); break;
                case Source::FromFrame:
                {
                    const std::int64_t at = std::clamp<std::int64_t> (spec.frame < 0 ? z.startFrame : spec.frame, z.startFrame, std::max<std::int64_t> (z.startFrame, zoneEnd - 1));
                    v.ovr = { true, false, z.startFrame, zoneEnd };
                    v.pos = static_cast<double> (at);
                    break;
                }
                case Source::LoopOnce:
                {
                    std::int64_t ls = 0, le = 0; int reps = 0;
                    loopRange (li, ls, le, reps);
                    place (ls, std::min (le, frames), false);
                    break;
                }
                case Source::LoopHeard:
                {
                    std::int64_t ls = 0, le = 0; int reps = 0;
                    loopRange (z.reverse ? 0 : li, ls, le, reps);
                    v.ovr = { true, true, z.startFrame, zoneEnd };
                    v.pos = static_cast<double> (z.reverse ? le - 1 : ls);
                    v.loopIdx = z.reverse ? 0 : li;
                    v.repsLeft = (li >= z.moreLoopCount || reps <= 0 || z.reverse) ? -1 : reps;
                    break;
                }
            }
            break;
        }
        return;
    }
}

void SamplerVoices::previewObject (const PreviewSample& p) noexcept
{
    stopPreview();
    startVoice (kPreviewChannel, p.zone.rootNote, 0.8f, p.zone, 1.0f);
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
