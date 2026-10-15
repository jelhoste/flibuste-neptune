#include "neptune/core/Engine.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "neptune/core/SamplerParams.h"

namespace neptune::core
{
void Engine::AtomicStrip::load (const mixer::StripSettings& s) noexcept
{
    midiChannel.store (s.midiChannel, std::memory_order_relaxed);
    mainDest.store (s.routing.mainDest, std::memory_order_relaxed);
    mainEnabled.store (s.routing.mainEnabled ? 1 : 0, std::memory_order_relaxed);
    directOut.store (s.routing.directOut, std::memory_order_relaxed);
    panMode.store (static_cast<int> (s.panMode), std::memory_order_relaxed);
    for (std::size_t i = 0; i < sends.size(); ++i)
    {
        sends[i].destination.store (s.sends[i].destination, std::memory_order_relaxed);
        sends[i].tap.store (static_cast<int> (s.sends[i].tap), std::memory_order_relaxed);
        sends[i].mute.store (s.sends[i].mute ? 1 : 0, std::memory_order_relaxed);
        sends[i].pan.store (static_cast<float> (s.sends[i].pan), std::memory_order_relaxed);
    }
}

Engine::Engine() noexcept
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks_[static_cast<std::size_t> (t)];
        tr.voices.setStreamer (&streamer_);
        tr.config.midiChannel.store (t == 0 ? 0 : t + 1, std::memory_order_relaxed); // piste 1 : tous les canaux ; les autres : leur canal
    }
}

void Engine::prepare (double sampleRate, int maxBlockSize) noexcept
{
    fm::initTables (sampleRate); // tables globales de msfa (module FM)
    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max (1, maxBlockSize);
    activeNotes_ = 0;
    for (auto& t : tracks_)
    {
        t.voices.prepare (sampleRate);
        t.synth.prepare (sampleRate);
        t.fm.prepare (sampleRate);
        if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Organ)) t.organ.reserve (sampleRate); // hors lecture
        t.lastGain = 1.0f;
        t.lastFader = 1.0f;
        t.lastPanL = t.lastPanR = 1.0f;
    }
    for (auto& b : buses_) { b.lastFader = 1.0f; b.lastPanL = b.lastPanR = 1.0f; }
    lastMasterTrim_ = lastMasterFader_ = 1.0f;
    lastMasterPanL_ = lastMasterPanR_ = 1.0f;
    const auto n = static_cast<std::size_t> (maxBlockSize_);
    raw_.resize (n); inserted_.resize (n); postFader_.resize (n); final_.resize (n); sum0_.resize (n); master_.resize (n);
    loudL_.assign (n, 0.0f);
    loudR_.assign (n, 0.0f);
    loudness_.prepare (sampleRate);
    levels_.resize (kParallels);
    for (auto& l : levels_) l.resize (n);
    for (auto& p : parallels_) { p.lastFader = 1.0f; p.lastPanL = p.lastPanR = 1.0f; }
    for (auto& b : busIn_) b.resize (n);
    // les inserts déjà posés suivent la nouvelle configuration (prepare n'est jamais concurrent du traitement audio)
    adoptInserts();
    auto prepareInserts = [this] (Inserts& ins)
    {
        for (auto& s : ins)
            if (auto* inst = const_cast<InsertInstance*> (s.slot.current())) inst->runner.prepare (sampleRate_, maxBlockSize_);
    };
    for (auto& t : tracks_) prepareInserts (t.inserts);
    for (auto& b : buses_) prepareInserts (b.inserts);
    for (auto& p : parallels_) prepareInserts (p.inserts);
    prepareInserts (masterInserts_);
}

void Engine::publishInstrument (int track, std::shared_ptr<const Instrument> instrument)
{
    if (track < 0 || track >= kTracks) return;
    if (instrument != nullptr)
        for (const auto& z : instrument->zones)
            if (z.sample != nullptr && z.sample->isStreamed()) { streamer_.start(); break; }
    tracks_[static_cast<std::size_t> (track)].slot.publish (std::move (instrument));
}

void Engine::setTrackType (int track, TrackType type) noexcept
{
    if (track < 0 || track >= kTracks) return;
    auto& t = tracks_[static_cast<std::size_t> (track)];
    if (type == TrackType::Fm) t.fm.reserve(); // les notes sont allouées avant que l'audio ne voie le type FM
    if (type == TrackType::Organ) t.organ.reserve (sampleRate_); // idem : moteur d'orgue (tables d'ondes)
    t.type.store (static_cast<int> (type), std::memory_order_relaxed);
}

void Engine::publishSynth (int track, std::shared_ptr<const SynthPatch> patch)
{
    if (track < 0 || track >= kTracks) return;
    auto& t = tracks_[static_cast<std::size_t> (track)];
    t.synthKeeper.add (patch);
    t.synthSlot.publish (std::move (patch));
    pruneSynthKeeper (t);
}

void Engine::publishFm (int track, std::shared_ptr<const fm::FmDescription> description)
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].fmSlot.publish (std::move (description));
}

void Engine::publishOrgan (int track, std::shared_ptr<const organ::OrganDescription> description)
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].organSlot.publish (std::move (description));
}

void Engine::pruneSynthKeeper (Track& t)
{
    const auto adopted = t.synth.adoptedGeneration(); // l'audio écrit les voix figées avant le patch adopté : on lit dans l'ordre inverse
    const auto oldest = t.synth.oldestFrozenGeneration();
    t.synthKeeper.prune (oldest, adopted);
}

void Engine::setTrackActive (int track, bool active) noexcept
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].active.store (active ? 1 : 0, std::memory_order_relaxed);
}

Engine::Inserts* Engine::insertsOf (StripKind kind, int index) noexcept
{
    switch (kind)
    {
        case StripKind::Track: return index >= 0 && index < kTracks ? &tracks_[static_cast<std::size_t> (index)].inserts : nullptr;
        case StripKind::Bus: return index >= 0 && index < kBuses ? &buses_[static_cast<std::size_t> (index)].inserts : nullptr;
        case StripKind::Parallel: return index >= 0 && index < kParallels ? &parallels_[static_cast<std::size_t> (index)].inserts : nullptr;
        default: return &masterInserts_;
    }
}

void Engine::setInsert (StripKind kind, int index, int slot, std::shared_ptr<const InsertInstance> insert)
{
    auto* ins = insertsOf (kind, index);
    if (ins == nullptr || slot < 0 || slot >= kInserts) return;
    (*ins)[static_cast<std::size_t> (slot)].slot.publish (std::move (insert));
    insertsDirty_.store (true, std::memory_order_release);
}

void Engine::collectGarbage()
{
    for (auto& t : tracks_)
    {
        t.slot.collect();
        t.synthSlot.collect();
        t.fmSlot.collect();
        t.organSlot.collect();
        t.filterSlot.collect();
        pruneSynthKeeper (t);
        for (auto& s : t.inserts) s.slot.collect();
    }
    for (auto& b : buses_)
        for (auto& s : b.inserts) s.slot.collect();
    for (auto& p : parallels_)
        for (auto& s : p.inserts) s.slot.collect();
    for (auto& s : masterInserts_) s.slot.collect();
    insertsDirty_.store (true, std::memory_order_release); // une adoption bloquée par une file pleine est retentée
}

void Engine::adoptInserts() noexcept
{
    for (auto& t : tracks_)
        for (auto& s : t.inserts) s.slot.adoptPending();
    for (auto& b : buses_)
        for (auto& s : b.inserts) s.slot.adoptPending();
    for (auto& p : parallels_)
        for (auto& s : p.inserts) s.slot.adoptPending();
    for (auto& s : masterInserts_) s.slot.adoptPending();
}

void Engine::setTrackSettings (int track, const mixer::StripSettings& s)
{
    if (track < 0 || track >= kTracks) return;
    auto& t = tracks_[static_cast<std::size_t> (track)];
    const auto clean = mixer::sanitisedStrip (s, false, track);
    t.config.load (clean);
    if (clean.midiFilter != t.filterDescription)
    {
        t.filterDescription = clean.midiFilter;
        t.filterSlot.publish (std::make_shared<const midi::CompiledFilter> (clean.midiFilter));
    }
}

void Engine::setTrackMidiFilter (int track, std::shared_ptr<const midi::CompiledFilter> filter)
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].filterSlot.publish (std::move (filter));
}

void Engine::setBusSettings (int bus, const mixer::StripSettings& s) noexcept
{
    if (bus < 0 || bus >= kBuses) return;
    buses_[static_cast<std::size_t> (bus)].config.load (mixer::sanitisedStrip (s, true, bus));
}

void Engine::setParallel (int p, bool active, int parent, mixer::TapPoint tap, bool followParentVolume) noexcept
{
    if (p < 0 || p >= kParallels) return;
    auto& x = parallels_[static_cast<std::size_t> (p)];
    x.parent.store (std::clamp (parent, 0, mixer::numTracks + kParallels - 1), std::memory_order_relaxed);
    x.tap.store (std::clamp (static_cast<int> (tap), 0, 3), std::memory_order_relaxed);
    x.follow.store (followParentVolume ? 1 : 0, std::memory_order_relaxed);
    x.active.store (active ? 1 : 0, std::memory_order_relaxed);
}

void Engine::setParallelSettings (int p, const mixer::StripSettings& s) noexcept
{
    if (p < 0 || p >= kParallels) return;
    parallels_[static_cast<std::size_t> (p)].config.load (mixer::sanitisedStrip (s, false, 0));
}

void Engine::setMasterSettings (const mixer::MasterSettings& m) noexcept
{
    const auto s = mixer::sanitised (m);
    masterOutput_.store (s.output, std::memory_order_relaxed);
    masterPanMode_.store (static_cast<int> (s.panMode), std::memory_order_relaxed);
    globalMaxVoices_.store (s.maxVoices, std::memory_order_relaxed);
}

void Engine::attachParameters (const ParameterSet* parameters) noexcept
{
    params_ = parameters;
    if (parameters == nullptr) return;
    auto idx = [parameters] (ParamId id) { return parameters->indexOf (id); };
    for (int t = 0; t < kTracks; ++t)
    {
        auto& p = tracks_[static_cast<std::size_t> (t)].idx;
        p.volume = idx (probe::trackParam (t, probe::kVolume));
        p.pan = idx (probe::trackParam (t, probe::kPan));
        p.mute = idx (probe::trackParam (t, probe::kMute));
        p.solo = idx (probe::trackParam (t, probe::kSolo));
        for (int s = 0; s < mixer::numSends; ++s) p.sendLevel[s] = idx (probe::trackParam (t, probe::kSendLevel + static_cast<ParamId> (s)));
        for (int s = 0; s < kInserts; ++s)
            tracks_[static_cast<std::size_t> (t)].inserts[static_cast<std::size_t> (s)].bypass = idx (probe::trackParam (t, probe::kInsertBypass + static_cast<ParamId> (s)));
        for (int k = 0; k < mixer::kSamplerKinds; ++k) p.sampler[k] = idx (mixer::samplerParam (t, static_cast<mixer::SamplerKind> (k)));
        for (int k = 0; k < mixer::kSynthKinds; ++k) p.synth[k] = idx (mixer::synthParam (t, static_cast<mixer::SynthKind> (k)));
        for (int k = 0; k < mixer::kSynthEnvKinds; ++k) p.synthEnv[k] = idx (mixer::synthEnvParam (t, static_cast<mixer::SynthEnvKind> (k)));
        for (int k = 0; k < mixer::kFmKinds; ++k) p.fm[k] = idx (mixer::fmParam (t, static_cast<mixer::FmKind> (k)));
        for (int k = 0; k < mixer::kFmExtraKinds; ++k) p.fmExtra[k] = idx (mixer::fmExtraParam (t, static_cast<mixer::FmExtraKind> (k)));
        for (int k = 0; k < mixer::kOrganKinds; ++k) p.organ[k] = idx (mixer::organParam (t, k));
    }
    for (int b = 0; b < kBuses; ++b)
    {
        auto& p = buses_[static_cast<std::size_t> (b)].idx;
        p.volume = idx (mixer::busParam (b, mixer::kBusVolume));
        p.pan = idx (mixer::busParam (b, mixer::kBusPan));
        p.mute = idx (mixer::busParam (b, mixer::kBusMute));
        p.solo = idx (mixer::busParam (b, mixer::kBusSolo));
        for (int s = 0; s < mixer::numSends; ++s) p.sendLevel[s] = idx (mixer::busParam (b, mixer::kBusSendLevel + static_cast<ParamId> (s)));
        for (int s = 0; s < kInserts; ++s)
            buses_[static_cast<std::size_t> (b)].inserts[static_cast<std::size_t> (s)].bypass = idx (mixer::busParam (b, mixer::kBusInsertBypass + static_cast<ParamId> (s)));
    }
    for (int s = 0; s < kInserts; ++s) masterInserts_[static_cast<std::size_t> (s)].bypass = idx (mixer::masterInsertBypassId (s));
    for (int q = 0; q < kParallels; ++q)
    {
        auto& x = parallels_[static_cast<std::size_t> (q)];
        x.idx.volume = idx (mixer::parallelParam (q, mixer::kParVolume));
        x.idx.pan = idx (mixer::parallelParam (q, mixer::kParPan));
        x.idx.mute = idx (mixer::parallelParam (q, mixer::kParMute));
        x.idx.solo = idx (mixer::parallelParam (q, mixer::kParSolo));
        for (int s = 0; s < mixer::numSends; ++s) x.idx.sendLevel[s] = idx (mixer::parallelParam (q, mixer::kParSendLevel + static_cast<ParamId> (s)));
        for (int s = 0; s < kInserts; ++s) x.inserts[static_cast<std::size_t> (s)].bypass = idx (mixer::parallelParam (q, mixer::kParInsertBypass + static_cast<ParamId> (s)));
    }
    masterVolume_ = idx (probe::masterVolume);
    masterPan_ = idx (probe::masterPan);
    masterMute_ = idx (probe::masterMute);
    masterTrim_ = idx (mixer::masterTrim);
    masterMono_ = idx (mixer::masterMono);
}

// ---------------------------------------------------------------------------------------------------- notes

void Engine::pushTrace (int track, int key, float shaped) noexcept
{
    const auto head = traceHead_.load (std::memory_order_relaxed);
    if (head - traceTail_.load (std::memory_order_acquire) >= kTraceSize) return;
    const int v = std::clamp (static_cast<int> (std::lround (shaped * 127.0f)), 1, 127);
    trace_[head & (kTraceSize - 1)].store ((static_cast<std::uint32_t> (track) << 16) | (static_cast<std::uint32_t> (key) << 8)
                                               | static_cast<std::uint32_t> (v), std::memory_order_relaxed);
    traceHead_.store (head + 1, std::memory_order_release);
}

void Engine::noteOnTrack (int track, int channel, int key, float velocity) noexcept
{
    if (track < 0 || track >= kTracks) return;
    auto& t = tracks_[static_cast<std::size_t> (track)];
    if (t.active.load (std::memory_order_relaxed) == 0) return;
    enforceGlobalVoiceLimit();
    if (key >= 0 && key < 128) t.held[key >> 6].fetch_or (std::uint64_t (1) << (key & 63), std::memory_order_relaxed);
    ++activeNotes_;
    if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Organ))
    {
        t.organ.noteOn (channel, key); // la vélocité n'agit pas sur un orgue
        if (key >= 0 && key < 128) pushTrace (track, key, velocity);
        return;
    }
    if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Fm))
    {
        t.fm.noteOn (channel, key, velocity);
        if (key >= 0 && key < 128) pushTrace (track, key, velocity);
        return;
    }
    if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth))
    {
        t.synth.noteOn (channel, key, velocity);
        if (key >= 0 && key < 128) pushTrace (track, key, velocity);
        return;
    }
    t.voices.noteOn (channel, key, velocity);
    if (key >= 0 && key < 128) pushTrace (track, key, t.voices.lastShapedVelocity());
}

// Limite globale de voix (toutes pistes, Sampler et Synthé) : tant qu'elle est atteinte, on fait s'éteindre en fondu la voix
// la plus à sacrifier du plugin (les voix en relâchement d'abord, puis la plus faible). Les limites par piste restent appliquées.
void Engine::enforceGlobalVoiceLimit() noexcept
{
    const int limit = globalMaxVoices_.load (std::memory_order_relaxed);
    for (int guard = 0; guard < 32; ++guard)
    {
        int total = 0;
        for (const auto& t : tracks_) total += t.voices.soundingVoices() + t.synth.soundingVoices() + t.fm.soundingVoices();
        if (total < limit) return;

        int bestTrack = -1;
        int bestKind = 0; // 0 sampler, 1 synthé, 2 FM
        bool bestRel = false;
        float bestLevel = 0.0f;
        std::uint64_t bestOrder = 0;
        auto consider = [&] (int track, int kind, bool rel, float level, std::uint64_t order)
        {
            const bool better = bestTrack < 0 || (rel != bestRel ? rel : (level != bestLevel ? level < bestLevel : order < bestOrder));
            if (better) { bestTrack = track; bestKind = kind; bestRel = rel; bestLevel = level; bestOrder = order; }
        };
        for (int i = 0; i < kTracks; ++i)
        {
            const auto& t = tracks_[static_cast<std::size_t> (i)];
            bool rel; float level; std::uint64_t order;
            if (t.voices.weakestVoice (rel, level, order)) consider (i, 0, rel, level, order);
            if (t.synth.weakestVoice (rel, level, order)) consider (i, 1, rel, level, order);
            if (t.fm.weakestVoice (rel, level, order)) consider (i, 2, rel, level, order);
        }
        if (bestTrack < 0) return;
        auto& t = tracks_[static_cast<std::size_t> (bestTrack)];
        if (bestKind == 2) t.fm.stealWeakest(); else if (bestKind == 1) t.synth.stealWeakest(); else t.voices.stealWeakest();
    }
}

void Engine::noteOffTrack (int track, int channel, int key) noexcept
{
    if (track < 0 || track >= kTracks) return;
    activeNotes_ = std::max (0, activeNotes_ - 1);
    auto& t = tracks_[static_cast<std::size_t> (track)];
    if (key >= 0 && key < 128) t.held[key >> 6].fetch_and (~(std::uint64_t (1) << (key & 63)), std::memory_order_relaxed);
    const int type = t.type.load (std::memory_order_relaxed);
    if (type == static_cast<int> (TrackType::Organ)) t.organ.noteOff (channel, key);
    else if (type == static_cast<int> (TrackType::Fm)) t.fm.noteOff (channel, key);
    else if (type == static_cast<int> (TrackType::Synth)) t.synth.noteOff (channel, key);
    else t.voices.noteOff (channel, key);
}

// Une note entrante est distribuée aux pistes dont le canal convient ET dont le filtre MIDI avancé laisse passer le message.
// Un Note Off suit toujours le sort de son Note On (sinon une note resterait bloquée si la règle dépend de la vélocité).
void Engine::noteOn (int channel, int key, float velocity) noexcept
{
    midi::Message m;
    m.kind = midi::MsgKind::NoteOn;
    m.channel = channel & 15;
    m.data1 = key;
    m.data2 = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 0, 127);
    const auto bit = static_cast<std::size_t> (((channel & 15) << 7) | (key & 127));
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks_[static_cast<std::size_t> (t)];
        const int ch = tr.config.midiChannel.load (std::memory_order_relaxed);
        // un orgue écoute les canaux de ses propres claviers (description) : tous les canaux lui sont présentés
        const bool organ = tr.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Organ);
        if (! (organ || ch == 0 || ch == channel + 1)) continue;
        if (tr.filter != nullptr && ! tr.filter->passes (m)) continue;
        tr.acceptedNotes[bit >> 6] |= std::uint64_t (1) << (bit & 63);
        noteOnTrack (t, channel, key, velocity);
    }
}

void Engine::noteOff (int channel, int key) noexcept
{
    const auto bit = static_cast<std::size_t> (((channel & 15) << 7) | (key & 127));
    for (int t = 0; t < kTracks; ++t)
    {
        auto& tr = tracks_[static_cast<std::size_t> (t)];
        const int ch = tr.config.midiChannel.load (std::memory_order_relaxed);
        const bool organ = tr.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Organ);
        if (! (organ || ch == 0 || ch == channel + 1)) continue;
        const std::uint64_t mask = std::uint64_t (1) << (bit & 63);
        const bool accepted = (tr.acceptedNotes[bit >> 6] & mask) != 0;
        if (tr.filter != nullptr && tr.filter->enabled() && ! accepted) continue; // le Note On n'est jamais passé
        tr.acceptedNotes[bit >> 6] &= ~mask;
        noteOffTrack (t, channel, key);
    }
}

void Engine::panic() noexcept
{
    activeNotes_ = 0;
    for (auto& t : tracks_) { t.voices.panic(); t.synth.panic(); t.fm.panic(); t.organ.panic(); t.held[0].store (0, std::memory_order_relaxed); t.held[1].store (0, std::memory_order_relaxed); }
}

void Engine::releaseAll() noexcept
{
    activeNotes_ = 0;
    for (auto& t : tracks_) { t.voices.releaseAll(); t.synth.releaseAll(); t.fm.releaseAll(); t.organ.allNotesOff(); t.held[0].store (0, std::memory_order_relaxed); t.held[1].store (0, std::memory_order_relaxed); }
}

void Engine::setParameter (std::uint32_t, double) noexcept {}

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& t : tracks_) n += t.voices.activeVoices() + t.synth.activeVoices() + t.fm.activeVoices() + t.organ.heldKeys();
    return n;
}

bool Engine::popNoteTrace (NoteTrace& out) noexcept
{
    const auto tail = traceTail_.load (std::memory_order_relaxed);
    if (tail == traceHead_.load (std::memory_order_acquire)) return false;
    const auto packed = trace_[tail & (kTraceSize - 1)].load (std::memory_order_relaxed);
    out.track = static_cast<int> (packed >> 16);
    out.key = static_cast<int> ((packed >> 8) & 0xFF);
    out.velocity = static_cast<int> (packed & 0xFF);
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

void Engine::drainUiEvents() noexcept
{
    for (auto tail = uiTail_.load (std::memory_order_relaxed); tail != uiHead_.load (std::memory_order_acquire); ++tail)
    {
        const UiEvent& e = uiQueue_[tail & (kUiQueueSize - 1)];
        const int tr = std::clamp (e.track, 0, kTracks - 1);
        switch (e.type)
        {
            case UiEvent::Type::NoteOn: noteOnTrack (tr, e.channel, e.key, e.velocity); break;
            case UiEvent::Type::NoteOff: noteOffTrack (tr, e.channel, e.key); break;
            case UiEvent::Type::PreviewZone: if (tracks_[static_cast<std::size_t> (tr)].type.load() == 0) tracks_[static_cast<std::size_t> (tr)].voices.previewZone (e.key); break;
            case UiEvent::Type::PreviewRelease: tracks_[static_cast<std::size_t> (tr)].voices.releasePreview(); break;
            case UiEvent::Type::PreviewStop: tracks_[static_cast<std::size_t> (tr)].voices.stopPreview(); break;
            case UiEvent::Type::Panic: panic(); break;
            case UiEvent::Type::ReleaseAll: releaseAll(); break;
        }
        uiTail_.store (tail + 1, std::memory_order_release);
    }
}

// ---------------------------------------------------------------------------------------------------- audio

void Engine::process (float* const* out, int numChannels, int numFrames) noexcept
{
    for (int c = 0; c < numChannels; ++c)
        if (out[c] != nullptr) std::fill (out[c], out[c] + numFrames, 0.0f);

    for (auto& t : tracks_)
    {
        if (t.slot.adoptPending()) t.voices.setInstrument (t.slot.current());
        if (t.synthSlot.adoptPending()) t.synth.setPatch (t.synthSlot.current());
        if (t.fmSlot.adoptPending()) t.fm.setDescription (t.fmSlot.current());
        if (t.organSlot.adoptPending()) t.organ.setDescription (t.organSlot.current());
        if (t.filterSlot.adoptPending()) t.filter = t.filterSlot.current();
        // changement de type ou désactivation : les voix de la piste s'arrêtent net (rien ne doit sonner sous l'ancien type)
        const int type = t.type.load (std::memory_order_relaxed), active = t.active.load (std::memory_order_relaxed);
        if (type != t.appliedType || (active == 0 && t.appliedActive != 0))
        {
            t.voices.allSoundOff();
            t.synth.allSoundOff();
            t.fm.allSoundOff();
            t.organ.allNotesOff();
            t.held[0].store (0, std::memory_order_relaxed);
            t.held[1].store (0, std::memory_order_relaxed);
            t.appliedType = type;
        }
        t.appliedActive = active;
    }
    if (insertsDirty_.exchange (false, std::memory_order_acq_rel)) adoptInserts();
    drainUiEvents();
    if (numChannels < 1 || out[0] == nullptr || numFrames <= 0) return;

    if (maxBlockSize_ <= 0) prepare (sampleRate_ > 0.0 ? sampleRate_ : 48000.0, numFrames); // sécurité : prepare() manquant

    // Blocs plus longs que prévu par l'hôte : découpés
    std::array<float*, 128> shifted {};
    const int channels = std::min (numChannels, static_cast<int> (shifted.size()));
    for (int offset = 0; offset < numFrames; offset += maxBlockSize_)
    {
        const int frames = std::min (maxBlockSize_, numFrames - offset);
        for (int c = 0; c < channels; ++c) shifted[static_cast<std::size_t> (c)] = out[c] != nullptr ? out[c] + offset : nullptr;
        processChunk (shifted.data(), channels, frames);
    }
    totalVoices_.store (activeVoices(), std::memory_order_relaxed);
    publishVoices();
}

std::pair<float*, float*> Engine::channelPair (int output) const noexcept
{
    const int l = 2 * (output - 1), r = l + 1;
    if (output < 1 || r >= chunkChannels_) return { nullptr, nullptr };
    return { chunkOut_[l], chunkOut_[r] };
}

bool Engine::hasInserts (const Inserts& ins) noexcept
{
    for (const auto& s : ins)
        if (s.slot.current() != nullptr) return true;
    return false;
}

// Fait passer `buf` (en place) par les inserts d'un emplacement, dans l'ordre ; faux s'il n'y en a aucun.
bool Engine::runInserts (Inserts& ins, Stereo& buf) noexcept
{
    bool any = false;
    for (auto& s : ins)
    {
        auto* inst = const_cast<InsertInstance*> (s.slot.current()); // le thread audio est seul utilisateur du DSP après la remise
        if (inst == nullptr) continue;
        any = true;
        inst->runner.process (buf.l.data(), buf.r.data(), chunkN_, param (s.bypass, 0.0) >= 0.5);
    }
    return any;
}

// Fader puis pan : `postFader` (point 2) et `fin` (point 3, le signal propre de la bande).
void Engine::applyFaderPan (const Stereo& in, Stereo& postFader, Stereo& fin, const AtomicStrip& cfg, const ParamIndices& idx, float& lastFader,
                            float& lastPanL, float& lastPanR, bool muted) noexcept
{
    using namespace mixer;
    const int n = chunkN_;
    const float fader = muted ? 0.0f : dbToGain (param (idx.volume, 0.0));
    const float pan = static_cast<float> (param (idx.pan, 0.0) / 100.0);
    float pl = 1.0f, pr = 1.0f;
    panGains (static_cast<PanMode> (cfg.panMode.load (std::memory_order_relaxed)), pan, pl, pr);
    const float fStep = (fader - lastFader) / static_cast<float> (n);
    const float plStep = (pl - lastPanL) / static_cast<float> (n);
    const float prStep = (pr - lastPanR) / static_cast<float> (n);
    float g = lastFader, gl = lastPanL, gr = lastPanR;
    for (int i = 0; i < n; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const float l = in.l[k] * g, r = in.r[k] * g;
        postFader.l[k] = l;
        postFader.r[k] = r;
        fin.l[k] = l * gl;
        fin.r[k] = r * gr;
        g += fStep; gl += plStep; gr += prStep;
    }
    lastFader = fader; lastPanL = pl; lastPanR = pr;
}

// Vu-mètre : crête et RMS du signal qui sort de la bande.
void Engine::meterOf (const Stereo& out, std::atomic<float>& pkL, std::atomic<float>& pkR, std::atomic<float>& rmsL, std::atomic<float>& rmsR) noexcept
{
    const int n = chunkN_;
    float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = out.l[static_cast<std::size_t> (i)], r = out.r[static_cast<std::size_t> (i)];
        peakL = std::max (peakL, std::abs (l)); peakR = std::max (peakR, std::abs (r));
        sumL += l * l; sumR += r * r;
    }
    pkL.store (peakL, std::memory_order_relaxed);
    pkR.store (peakR, std::memory_order_relaxed);
    rmsL.store (std::sqrt (sumL / static_cast<float> (n)), std::memory_order_relaxed);
    rmsR.store (std::sqrt (sumR / static_cast<float> (n)), std::memory_order_relaxed);
}

// Envois (un point de captation par envoi), sortie principale (Master ou bus) et sortie directe d'une bande. `out` : ce qui sort de la
// bande (son signal propre + ses pistes parallèles). Pour une piste parallèle, `sumIntoParent` est vrai : la sortie principale est
// alors la somme dans le parent, faite par l'appelant.
void Engine::routeStrip (const AtomicStrip& cfg, const ParamIndices& idx, std::array<float, mixer::numSends>& lastSend, const Stereo& pre,
                         const Stereo& post, const Stereo& postFader, const Stereo& out, bool isBus, int busIndex, bool sumIntoParent) noexcept
{
    using namespace mixer;
    const int n = chunkN_;
    for (int s = 0; s < numSends; ++s)
    {
        const int dest = cfg.sends[static_cast<std::size_t> (s)].destination.load (std::memory_order_relaxed);
        const bool muted = cfg.sends[static_cast<std::size_t> (s)].mute.load (std::memory_order_relaxed) != 0;
        const double levelDb = param (idx.sendLevel[s], -100.0);
        const float target = (dest <= 0 || muted) ? 0.0f : dbToGain (levelDb);
        float& last = lastSend[static_cast<std::size_t> (s)];
        if (dest <= 0 || dest > kBuses || (isBus && dest - 1 <= busIndex) || (target == 0.0f && last == 0.0f)) { last = target; continue; }
        const int tap = cfg.sends[static_cast<std::size_t> (s)].tap.load (std::memory_order_relaxed);
        const Stereo& src = tap == 0 ? pre : (tap == 1 ? post : (tap == 2 ? postFader : out));
        float spl = 1.0f, spr = 1.0f;
        panGains (static_cast<PanMode> (cfg.panMode.load (std::memory_order_relaxed)), cfg.sends[static_cast<std::size_t> (s)].pan.load (std::memory_order_relaxed), spl, spr);
        Stereo& bus = busIn_[static_cast<std::size_t> (dest - 1)];
        const float step = (target - last) / static_cast<float> (n);
        float g = last;
        for (int i = 0; i < n; ++i)
        {
            bus.l[static_cast<std::size_t> (i)] += src.l[static_cast<std::size_t> (i)] * g * spl;
            bus.r[static_cast<std::size_t> (i)] += src.r[static_cast<std::size_t> (i)] * g * spr;
            g += step;
        }
        last = target;
        buses_[static_cast<std::size_t> (dest - 1)].hasInput = true;
    }
    if (! sumIntoParent && cfg.mainEnabled.load (std::memory_order_relaxed) != 0)
    {
        const int dest = cfg.mainDest.load (std::memory_order_relaxed);
        Stereo* target = &master_;
        if (dest > 0 && dest <= kBuses && ! (isBus && dest - 1 <= busIndex))
        {
            target = &busIn_[static_cast<std::size_t> (dest - 1)];
            buses_[static_cast<std::size_t> (dest - 1)].hasInput = true;
        }
        for (int i = 0; i < n; ++i)
        {
            target->l[static_cast<std::size_t> (i)] += out.l[static_cast<std::size_t> (i)];
            target->r[static_cast<std::size_t> (i)] += out.r[static_cast<std::size_t> (i)];
        }
    }
    const int direct = cfg.directOut.load (std::memory_order_relaxed);
    if (direct > 0)
    {
        const auto pair = channelPair (direct);
        if (pair.first != nullptr && pair.second != nullptr)
            for (int i = 0; i < n; ++i)
            {
                pair.first[i] += out.l[static_cast<std::size_t> (i)];
                pair.second[i] += out.r[static_cast<std::size_t> (i)];
            }
    }
}

// Une piste parallèle : prend le signal du point de captation du parent, a sa propre bande (inserts, fader, pan, envois, sortie directe),
// traite ses propres filles, puis se somme dans `parentSum` (fin de chaîne du parent).
void Engine::processParallel (int p, int depth, bool ancestorSolo, const Stereo& parentPre, const Stereo& parentPost, const Stereo& parentFader,
                              const Stereo& parentFin, Stereo& parentSum, float parentGainFrom, float parentGainTo) noexcept
{
    using namespace mixer;
    if (depth < 1 || depth > kParallels) return;
    auto& P = parallels_[static_cast<std::size_t> (p)];
    Level& L = levels_[static_cast<std::size_t> (depth - 1)];
    const int n = chunkN_;
    const int tap = P.tap.load (std::memory_order_relaxed);
    const Stereo& src = tap == 0 ? parentPre : (tap == 1 ? parentPost : (tap == 2 ? parentFader : parentFin));
    if (P.follow.load (std::memory_order_relaxed) != 0 && tap < 2)
    {
        // le fader du parent commande aussi cette piste : on applique son gain (fondu sur le bloc) au signal capté avant le fader
        const float step = (parentGainTo - parentGainFrom) / static_cast<float> (n);
        float g = parentGainFrom;
        for (int i = 0; i < n; ++i, g += step)
        {
            L.pre.l[static_cast<std::size_t> (i)] = src.l[static_cast<std::size_t> (i)] * g;
            L.pre.r[static_cast<std::size_t> (i)] = src.r[static_cast<std::size_t> (i)] * g;
        }
    }
    else
    {
        std::copy (src.l.begin(), src.l.begin() + n, L.pre.l.begin());
        std::copy (src.r.begin(), src.r.begin() + n, L.pre.r.begin());
    }

    Stereo* post = &L.pre;
    if (hasInserts (P.inserts))
    {
        std::copy (L.pre.l.begin(), L.pre.l.begin() + n, L.post.l.begin());
        std::copy (L.pre.r.begin(), L.pre.r.begin() + n, L.post.r.begin());
        runInserts (P.inserts, L.post);
        post = &L.post;
    }

    // solo « en place » : une bande qui n'est pas en solo mais qui alimente (ou contient) une piste en solo reste active, sans être entendue
    const int self = numTracks + p;
    const bool selfSolo = param (P.idx.solo, 0.0) >= 0.5;
    const bool audible = selfSolo || ancestorSolo;
    const bool feeding = subtreeSolo_[static_cast<std::size_t> (self)] != 0;
    const bool muted = param (P.idx.mute, 0.0) >= 0.5 || (anySolo_ && ! audible && ! feeding);
    const bool gateOwn = anySolo_ && ! audible && feeding;
    const float gainFrom = P.lastFader;
    applyFaderPan (*post, L.fader, L.fin, P.config, P.idx, P.lastFader, P.lastPanL, P.lastPanR, muted);
    const float gainTo = P.lastFader;

    Stereo* out = &L.fin;
    const bool hasChildren = firstChild_[static_cast<std::size_t> (self)] >= 0;
    if (gateOwn || hasChildren)
    {
        if (gateOwn) { std::fill (L.sum.l.begin(), L.sum.l.begin() + n, 0.0f); std::fill (L.sum.r.begin(), L.sum.r.begin() + n, 0.0f); }
        else { std::copy (L.fin.l.begin(), L.fin.l.begin() + n, L.sum.l.begin()); std::copy (L.fin.r.begin(), L.fin.r.begin() + n, L.sum.r.begin()); }
        for (int child = firstChild_[static_cast<std::size_t> (self)]; child >= 0; child = nextSibling_[static_cast<std::size_t> (child)])
            processParallel (child, depth + 1, audible, L.pre, *post, L.fader, L.fin, L.sum, gainFrom, gainTo);
        out = &L.sum;
    }
    meterOf (*out, P.peakL, P.peakR, P.rmsL, P.rmsR);

    const bool toParent = P.config.mainEnabled.load (std::memory_order_relaxed) != 0;
    routeStrip (P.config, P.idx, P.lastSend, L.pre, *post, L.fader, *out, false, -1, true);
    if (toParent)
        for (int i = 0; i < n; ++i)
        {
            parentSum.l[static_cast<std::size_t> (i)] += out->l[static_cast<std::size_t> (i)];
            parentSum.r[static_cast<std::size_t> (i)] += out->r[static_cast<std::size_t> (i)];
        }
}

void Engine::processChunk (float* const* out, int numChannels, int n) noexcept
{
    using namespace mixer;
    chunkOut_ = out;
    chunkChannels_ = numChannels;
    chunkN_ = n;
    auto zero = [n] (Stereo& s) { std::fill (s.l.begin(), s.l.begin() + n, 0.0f); std::fill (s.r.begin(), s.r.begin() + n, 0.0f); };
    zero (master_);
    for (auto& b : busIn_) zero (b);
    for (auto& b : buses_) b.hasInput = false;

    // --- arbre des pistes parallèles (reconstruit à chaque bloc : 32 entrées). Seules les pistes sont des racines : une boucle ou un
    // parent inactif rend une parallèle inatteignable, donc jamais traitée.
    firstChild_.fill (-1);
    subtreeSolo_.fill (0);
    subtreeBusy_.fill (0);
    for (int p = kParallels - 1; p >= 0; --p)
    {
        auto& x = parallels_[static_cast<std::size_t> (p)];
        if (x.active.load (std::memory_order_relaxed) == 0) { x.lastSend.fill (0.0f); continue; }
        const int parent = x.parent.load (std::memory_order_relaxed);
        if (parent == numTracks + p) continue;
        nextSibling_[static_cast<std::size_t> (p)] = firstChild_[static_cast<std::size_t> (parent)];
        firstChild_[static_cast<std::size_t> (parent)] = p;
        if (param (x.idx.solo, 0.0) >= 0.5) subtreeSolo_[static_cast<std::size_t> (numTracks + p)] = 1;
        if (x.peakL.load (std::memory_order_relaxed) != 0.0f || x.peakR.load (std::memory_order_relaxed) != 0.0f) subtreeBusy_[static_cast<std::size_t> (numTracks + p)] = 1;
    }
    for (int t = 0; t < kTracks; ++t)
        if (param (tracks_[static_cast<std::size_t> (t)].idx.solo, 0.0) >= 0.5) subtreeSolo_[static_cast<std::size_t> (t)] = 1;
    // remonte « solo » et « actif » vers les parents (au plus kParallels passes)
    for (int pass = 0; pass < kParallels; ++pass)
    {
        bool changed = false;
        for (int p = 0; p < kParallels; ++p)
        {
            auto& x = parallels_[static_cast<std::size_t> (p)];
            if (x.active.load (std::memory_order_relaxed) == 0) continue;
            const auto self = static_cast<std::size_t> (numTracks + p);
            const auto parent = static_cast<std::size_t> (x.parent.load (std::memory_order_relaxed));
            for (auto* flags : { &subtreeSolo_, &subtreeBusy_ })
                if ((*flags)[self] != 0 && (*flags)[parent] == 0) { (*flags)[parent] = 1; changed = true; }
        }
        if (! changed) break;
    }

    // Solo : deux domaines. Un solo de piste (ou de piste parallèle) coupe les pistes qui n'y mènent pas (pas les bus : le solo
    // doit rester audible) ; un solo de bus coupe les bus non solo.
    bool trackSolo = false, busSolo = false;
    for (const auto& t : tracks_) trackSolo = trackSolo || param (t.idx.solo, 0.0) >= 0.5;
    for (int p = 0; p < kParallels; ++p)
        if (parallels_[static_cast<std::size_t> (p)].active.load (std::memory_order_relaxed) != 0)
            trackSolo = trackSolo || param (parallels_[static_cast<std::size_t> (p)].idx.solo, 0.0) >= 0.5;
    for (const auto& b : buses_) busSolo = busSolo || param (b.idx.solo, 0.0) >= 0.5;
    anySolo_ = trackSolo;
    busSolo_ = busSolo;

    // ---- pistes
    for (int ti = 0; ti < kTracks; ++ti)
    {
        auto& t = tracks_[static_cast<std::size_t> (ti)];
        if (t.active.load (std::memory_order_relaxed) == 0) { t.lastSend.fill (0.0f); continue; }
        const bool synthTrack = t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth);
        const bool fmTrack = t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Fm);
        const bool organTrack = t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Organ);
        const bool wasSilent = t.peakL.load (std::memory_order_relaxed) == 0.0f && t.peakR.load (std::memory_order_relaxed) == 0.0f;
        // piste sans instrument, ou au repos et déjà silencieuse (elle et ses pistes parallèles) : rien à faire
        // l'orgue (Leslie, réverb) a une queue : au repos seulement quand aucune touche n'est tenue ET que la piste est déjà muette
        const bool idle = organTrack ? t.organ.heldKeys() == 0
                        : fmTrack ? t.fm.activeVoices() == 0
                        : synthTrack ? t.synth.activeVoices() == 0 : (t.slot.current() == nullptr || t.voices.activeVoices() == 0);
        if (wasSilent && idle && subtreeBusy_[static_cast<std::size_t> (ti)] == 0) { t.lastSend.fill (0.0f); continue; }

        std::fill (raw_.l.begin(), raw_.l.begin() + n, 0.0f);
        std::fill (raw_.r.begin(), raw_.r.begin() + n, 0.0f);
        // instrument : gain, accordage et filtres hérités (paramètres de CETTE piste)
        float gain = 1.0f;
        double cents = 0.0;
        SamplerVoices::FilterParams fp;
        if (params_ != nullptr)
        {
            const auto& s = t.idx.sampler;
            if (s[kGain] != ParameterSet::npos) gain = static_cast<float> (sampler::dbToGain (params_->getAt (s[kGain])));
            if (s[kTuneSemitones] != ParameterSet::npos) cents += params_->getAt (s[kTuneSemitones]) * 100.0;
            if (s[kTuneCents] != ParameterSet::npos) cents += params_->getAt (s[kTuneCents]);
            if (s[kFilter1Cutoff] != ParameterSet::npos) fp.cutoffHz[0] = params_->getAt (s[kFilter1Cutoff]);
            if (s[kFilter1Resonance] != ParameterSet::npos) fp.resonance[0] = params_->getAt (s[kFilter1Resonance]) / 100.0;
            if (s[kFilter2Cutoff] != ParameterSet::npos) fp.cutoffHz[1] = params_->getAt (s[kFilter2Cutoff]);
            if (s[kFilter2Resonance] != ParameterSet::npos) fp.resonance[1] = params_->getAt (s[kFilter2Resonance]) / 100.0;
        }
        if (organTrack)
        {
            double values[organ::kOrganValues];
            // valeurs par défaut de mixer::addOrganParams quand aucun jeu de paramètres n'est attaché
            static constexpr double kDefaults[organ::kOrganValues] = { 8, 8, 6, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0,
                                                                       0, 0, 1, 1,  0, 1, 0,  0, 100,  0, 50, 50, 50,  0 };
            for (int k = 0; k < organ::kOrganValues; ++k) values[k] = param (t.idx.organ[static_cast<std::size_t> (k)], kDefaults[k]);
            organ::OrganLive live = organ::liveFromValues (values);
            t.organ.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, live);
        }
        else if (fmTrack)
        {
            fm::FmLive live;
            live.tuneCents = static_cast<float> (cents);
            if (params_ != nullptr)
            {
                const auto& fx = t.idx.fm;
                auto get = [this, &fx] (mixer::FmKind k, double fallback) { return param (fx[static_cast<std::size_t> (k)], fallback); };
                live.brightness = static_cast<float> (get (mixer::kFmBrightness, 0.0) / 100.0);
                live.envSpeed = static_cast<float> (get (mixer::kFmEnvSpeed, 0.0) / 100.0);
                live.feedbackDrive = static_cast<float> (get (mixer::kFmFeedbackDrive, 0.0) / 100.0);
                for (int op = 0; op < fm::kOperators; ++op)
                    live.opLevel[static_cast<std::size_t> (op)] = static_cast<float> (get (static_cast<mixer::FmKind> (mixer::kFmLevelOp1 + op), 0.0));
                live.unisonDetuneCents = static_cast<float> (get (mixer::kFmUnisonDetune, 15.0));
                live.unisonSpread = static_cast<float> (get (mixer::kFmUnisonSpread, 60.0) / 100.0);
                const auto& fxe = t.idx.fmExtra;
                auto extra = [this, &fxe] (mixer::FmExtraKind k) { return static_cast<int> (std::lround (param (fxe[static_cast<std::size_t> (k)], -1.0))); };
                live.lfoSpeed = extra (mixer::kFmLfoSpeed);
                live.lfoPmd = extra (mixer::kFmLfoPmd);
                live.lfoAmd = extra (mixer::kFmLfoAmd);
                live.bendRange = extra (mixer::kFmBendRange);
                live.wheelPitchSens = extra (mixer::kFmWheelPitch);
                live.wheelAmpSens = extra (mixer::kFmWheelAmp);
                live.glideMs = extra (mixer::kFmGlide);
            }
            t.fm.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, live);
        }
        else if (synthTrack)
        {
            SynthVoices::Params sp;
            for (int f = 0; f < 2; ++f) { sp.cutoffHz[f] = fp.cutoffHz[f]; sp.resonance[f] = fp.resonance[f]; }
            sp.tuneCents = cents;
            if (params_ != nullptr)
            {
                const auto& sy = t.idx.synth;
                auto get = [this, &sy] (mixer::SynthKind k, double fallback) { return param (sy[static_cast<std::size_t> (k)], fallback); };
                for (int o = 0; o < numSources; ++o) sp.level[o] = get (static_cast<mixer::SynthKind> (mixer::kLevelOsc1 + o), sp.level[o]);
                for (int o = 0; o < numOscillators; ++o)
                {
                    sp.fineCents[o] = get (static_cast<mixer::SynthKind> (mixer::kFineOsc1 + o), 0.0);
                    sp.pulse[o] = get (static_cast<mixer::SynthKind> (mixer::kPulseOsc1 + o), 50.0);
                    sp.unisonDetune[o] = get (static_cast<mixer::SynthKind> (mixer::kDetuneOsc1 + o), 20.0);
                }
                sp.pitchEnvDepth = get (mixer::kPitchEnvDepth, 0.0);
                const auto& se = t.idx.synthEnv;
                auto env = [this, &se] (mixer::SynthEnvKind k, double fallback) { return param (se[static_cast<std::size_t> (k)], fallback); };
                auto fill = [&env] (SynthVoices::Params::EnvParams& e, int base)
                {
                    e.attackMs = env (static_cast<mixer::SynthEnvKind> (base), -1.0);
                    e.decayMs = env (static_cast<mixer::SynthEnvKind> (base + 1), -1.0);
                    const double sus = env (static_cast<mixer::SynthEnvKind> (base + 2), -100.0);
                    e.sustain = sus < 0.0 ? -1.0 : sus / 100.0;
                    e.releaseMs = env (static_cast<mixer::SynthEnvKind> (base + 3), -1.0);
                };
                fill (sp.ampEnv, 0);
                fill (sp.filterEnv[0], 4);
                fill (sp.filterEnv[1], 8);
                sp.glideMs = env (mixer::kSynthGlide, -1.0);
                const double bal = env (mixer::kFilterBalance, -100.0);
                sp.filterBalance = bal < 0.0 ? -1.0 : bal / 100.0;
            }
            t.synth.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, sp);
        }
        else
        {
            t.voices.setFilterParams (fp);
            t.voices.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, cents);
        }
        t.lastGain = gain;

        // solo « en place » : voir processParallel
        const bool selfSolo = param (t.idx.solo, 0.0) >= 0.5;
        const bool feeding = subtreeSolo_[static_cast<std::size_t> (ti)] != 0;
        const bool muted = param (t.idx.mute, 0.0) >= 0.5 || (anySolo_ && ! selfSolo && ! feeding);
        const bool gateOwn = anySolo_ && ! selfSolo && feeding;
        // inserts : sur une copie, le signal brut reste disponible pour le point de captation « pré-inserts »
        Stereo* afterInserts = &raw_;
        if (hasInserts (t.inserts))
        {
            std::copy (raw_.l.begin(), raw_.l.begin() + n, inserted_.l.begin());
            std::copy (raw_.r.begin(), raw_.r.begin() + n, inserted_.r.begin());
            runInserts (t.inserts, inserted_);
            afterInserts = &inserted_;
        }
        const float faderFrom = t.lastFader;
        applyFaderPan (*afterInserts, postFader_, final_, t.config, t.idx, t.lastFader, t.lastPanL, t.lastPanR, muted);
        const float faderTo = t.lastFader;

        // pistes parallèles : somme en fin de chaîne de la piste
        Stereo* trackOut = &final_;
        const bool hasChildren = firstChild_[static_cast<std::size_t> (ti)] >= 0;
        if (gateOwn || hasChildren)
        {
            if (gateOwn) { std::fill (sum0_.l.begin(), sum0_.l.begin() + n, 0.0f); std::fill (sum0_.r.begin(), sum0_.r.begin() + n, 0.0f); }
            else { std::copy (final_.l.begin(), final_.l.begin() + n, sum0_.l.begin()); std::copy (final_.r.begin(), final_.r.begin() + n, sum0_.r.begin()); }
            for (int child = firstChild_[static_cast<std::size_t> (ti)]; child >= 0; child = nextSibling_[static_cast<std::size_t> (child)])
                processParallel (child, 1, selfSolo, raw_, *afterInserts, postFader_, final_, sum0_, faderFrom, faderTo);
            trackOut = &sum0_;
        }
        meterOf (*trackOut, t.peakL, t.peakR, t.rmsL, t.rmsR);
        routeStrip (t.config, t.idx, t.lastSend, raw_, *afterInserts, postFader_, *trackOut, false, -1, false);
    }

    // ---- bus, dans l'ordre : un bus n'alimente que des bus d'indice supérieur
    for (int bi = 0; bi < kBuses; ++bi)
    {
        auto& b = buses_[static_cast<std::size_t> (bi)];
        const bool wasSilent = b.peakL.load (std::memory_order_relaxed) == 0.0f && b.peakR.load (std::memory_order_relaxed) == 0.0f;
        if (! b.hasInput && wasSilent) { b.lastSend.fill (0.0f); continue; }
        const bool muted = param (b.idx.mute, 0.0) >= 0.5 || (busSolo_ && param (b.idx.solo, 0.0) < 0.5);
        Stereo& in = busIn_[static_cast<std::size_t> (bi)];
        Stereo* afterInserts = &in;
        if (hasInserts (b.inserts))
        {
            std::copy (in.l.begin(), in.l.begin() + n, inserted_.l.begin());
            std::copy (in.r.begin(), in.r.begin() + n, inserted_.r.begin());
            runInserts (b.inserts, inserted_);
            afterInserts = &inserted_;
        }
        applyFaderPan (*afterInserts, postFader_, final_, b.config, b.idx, b.lastFader, b.lastPanL, b.lastPanR, muted);
        meterOf (final_, b.peakL, b.peakR, b.rmsL, b.rmsR);
        routeStrip (b.config, b.idx, b.lastSend, in, *afterInserts, postFader_, final_, true, bi, false);
    }

    // ---- master : trim, inserts, fader, pan, mute, réduction mono
    {
        const float trimTarget = dbToGain (std::clamp (param (masterTrim_, 0.0), -12.0, 12.0), -1000.0); // le trim ne coupe jamais
        const bool muted = param (masterMute_, 0.0) >= 0.5;
        const float faderTarget = muted ? 0.0f : dbToGain (param (masterVolume_, 0.0));
        float pl = 1.0f, pr = 1.0f;
        panGains (static_cast<PanMode> (masterPanMode_.load (std::memory_order_relaxed)), static_cast<float> (param (masterPan_, 0.0) / 100.0), pl, pr);
        const bool mono = param (masterMono_, 0.0) >= 0.5;
        const float invN = 1.0f / static_cast<float> (n);
        const float tStep = (trimTarget - lastMasterTrim_) * invN;
        const float fStep = (faderTarget - lastMasterFader_) * invN;
        const float plStep = (pl - lastMasterPanL_) * invN;
        const float prStep = (pr - lastMasterPanR_) * invN;
        float gt = lastMasterTrim_, gf = lastMasterFader_, gl = lastMasterPanL_, gr = lastMasterPanR_;
        // le trim d'entrée vient AVANT les inserts du Master
        const bool withInserts = hasInserts (masterInserts_);
        if (withInserts)
        {
            for (int i = 0; i < n; ++i, gt += tStep)
            {
                master_.l[static_cast<std::size_t> (i)] *= gt;
                master_.r[static_cast<std::size_t> (i)] *= gt;
            }
            gt = trimTarget;
            runInserts (masterInserts_, master_);
        }
        const auto pair = channelPair (masterOutput_.load (std::memory_order_relaxed));
        float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = (withInserts ? 1.0f : gt) * gf;
            float l = master_.l[static_cast<std::size_t> (i)] * g * gl;
            float r = master_.r[static_cast<std::size_t> (i)] * g * gr;
            if (mono) l = r = 0.5f * (l + r);
            peakL = std::max (peakL, std::abs (l)); peakR = std::max (peakR, std::abs (r));
            sumL += l * l; sumR += r * r;
            loudL_[static_cast<std::size_t> (i)] = l;
            loudR_[static_cast<std::size_t> (i)] = r;
            if (pair.first != nullptr) pair.first[i] += l;
            if (pair.second != nullptr) pair.second[i] += r;
            gt += tStep; gf += fStep; gl += plStep; gr += prStep;
        }
        lastMasterTrim_ = trimTarget; lastMasterFader_ = faderTarget; lastMasterPanL_ = pl; lastMasterPanR_ = pr;
        loudness_.push (loudL_.data(), loudR_.data(), n);
        masterPeakL_.store (peakL, std::memory_order_relaxed);
        masterPeakR_.store (peakR, std::memory_order_relaxed);
        masterRmsL_.store (std::sqrt (sumL / static_cast<float> (n)), std::memory_order_relaxed);
        masterRmsR_.store (std::sqrt (sumR / static_cast<float> (n)), std::memory_order_relaxed);
    }
}

Engine::Meter Engine::trackMeter (int t) const noexcept
{
    if (t < 0 || t >= kTracks) return {};
    const auto& x = tracks_[static_cast<std::size_t> (t)];
    return { x.peakL.load (std::memory_order_relaxed), x.peakR.load (std::memory_order_relaxed), x.rmsL.load (std::memory_order_relaxed), x.rmsR.load (std::memory_order_relaxed) };
}

Engine::Meter Engine::busMeter (int b) const noexcept
{
    if (b < 0 || b >= kBuses) return {};
    const auto& x = buses_[static_cast<std::size_t> (b)];
    return { x.peakL.load (std::memory_order_relaxed), x.peakR.load (std::memory_order_relaxed), x.rmsL.load (std::memory_order_relaxed), x.rmsR.load (std::memory_order_relaxed) };
}

Engine::Meter Engine::parallelMeter (int p) const noexcept
{
    if (p < 0 || p >= kParallels) return {};
    const auto& x = parallels_[static_cast<std::size_t> (p)];
    return { x.peakL.load (std::memory_order_relaxed), x.peakR.load (std::memory_order_relaxed), x.rmsL.load (std::memory_order_relaxed), x.rmsR.load (std::memory_order_relaxed) };
}

Engine::Meter Engine::masterMeter() const noexcept
{
    return { masterPeakL_.load (std::memory_order_relaxed), masterPeakR_.load (std::memory_order_relaxed),
             masterRmsL_.load (std::memory_order_relaxed), masterRmsR_.load (std::memory_order_relaxed) };
}

void Engine::publishVoices() noexcept
{
    if (! voiceMonitor_.load (std::memory_order_relaxed)) return; // moniteur éteint : aucun travail
    const auto seq = voiceSeq_.load (std::memory_order_relaxed);
    voiceSeq_.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    int n = 0;
    for (int ti = 0; ti < kTracks; ++ti)
    {
        auto pack = [this, &n, ti] (const VoiceInfo& v)
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
            w1 |= static_cast<std::uint64_t> (ti & 0xFF) << 56;
            voiceWords_[static_cast<std::size_t> (n) * 2].store (w0, std::memory_order_relaxed);
            voiceWords_[static_cast<std::size_t> (n) * 2 + 1].store (w1, std::memory_order_relaxed);
            ++n;
        };
        auto& tr = tracks_[static_cast<std::size_t> (ti)];
        if (tr.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Fm))
            tr.fm.forEachVoice ([&pack] (const fm::FmVoices::VoiceInfo& s)
            {
                VoiceInfo v;
                v.sourceIndex = -1; // pas de zone
                v.key = s.key; v.channel = s.channel; v.velocity = s.velocity;
                v.releasing = s.releasing; v.fading = s.fading; v.level = s.level; v.order = s.order;
                pack (v);
            });
        else if (tr.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth))
            tr.synth.forEachVoice ([&pack] (const SynthVoices::VoiceInfo& s)
            {
                VoiceInfo v;
                v.sourceIndex = -1; // le synthé n'a pas de zone
                v.key = s.key; v.channel = s.channel; v.velocity = s.velocity;
                v.releasing = s.releasing; v.fading = s.fading; v.level = s.level; v.order = s.order;
                pack (v);
            });
        else
            tr.voices.forEachVoice (pack);
    }
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
            v.track = static_cast<int> ((w1 >> 56) & 0xFF);
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (voiceSeq_.load (std::memory_order_relaxed) == before) return n;
    }
    return 0; // trop de collisions d'écriture (très improbable) : cette image est ignorée
}
} // namespace neptune::core
