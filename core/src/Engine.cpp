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
    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max (1, maxBlockSize);
    activeNotes_ = 0;
    for (auto& t : tracks_)
    {
        t.voices.prepare (sampleRate);
        t.synth.prepare (sampleRate);
        t.lastGain = 1.0f;
        t.lastFader = 1.0f;
        t.lastPanL = t.lastPanR = 1.0f;
    }
    for (auto& b : buses_) { b.lastFader = 1.0f; b.lastPanL = b.lastPanR = 1.0f; }
    lastMasterGain_ = 1.0f;
    lastMasterPanL_ = lastMasterPanR_ = 1.0f;
    const auto n = static_cast<std::size_t> (maxBlockSize_);
    raw_.resize (n); postFader_.resize (n); final_.resize (n); master_.resize (n);
    for (auto& b : busIn_) b.resize (n);
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
    tracks_[static_cast<std::size_t> (track)].type.store (static_cast<int> (type), std::memory_order_relaxed);
}

void Engine::publishSynth (int track, std::shared_ptr<const SynthPatch> patch)
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].synthSlot.publish (std::move (patch));
}

void Engine::setTrackActive (int track, bool active) noexcept
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].active.store (active ? 1 : 0, std::memory_order_relaxed);
}

void Engine::setTrackSettings (int track, const mixer::StripSettings& s) noexcept
{
    if (track < 0 || track >= kTracks) return;
    tracks_[static_cast<std::size_t> (track)].config.load (mixer::sanitisedStrip (s, false, track));
}

void Engine::setBusSettings (int bus, const mixer::StripSettings& s) noexcept
{
    if (bus < 0 || bus >= kBuses) return;
    buses_[static_cast<std::size_t> (bus)].config.load (mixer::sanitisedStrip (s, true, bus));
}

void Engine::setMasterSettings (const mixer::MasterSettings& m) noexcept
{
    const auto s = mixer::sanitised (m);
    masterOutput_.store (s.output, std::memory_order_relaxed);
    masterPanMode_.store (static_cast<int> (s.panMode), std::memory_order_relaxed);
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
        for (int k = 0; k < mixer::kSamplerKinds; ++k) p.sampler[k] = idx (mixer::samplerParam (t, static_cast<mixer::SamplerKind> (k)));
    }
    for (int b = 0; b < kBuses; ++b)
    {
        auto& p = buses_[static_cast<std::size_t> (b)].idx;
        p.volume = idx (mixer::busParam (b, mixer::kBusVolume));
        p.pan = idx (mixer::busParam (b, mixer::kBusPan));
        p.mute = idx (mixer::busParam (b, mixer::kBusMute));
        p.solo = idx (mixer::busParam (b, mixer::kBusSolo));
        for (int s = 0; s < mixer::numSends; ++s) p.sendLevel[s] = idx (mixer::busParam (b, mixer::kBusSendLevel + static_cast<ParamId> (s)));
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
    ++activeNotes_;
    if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth))
    {
        t.synth.noteOn (channel, key, velocity);
        if (key >= 0 && key < 128) pushTrace (track, key, velocity);
        return;
    }
    t.voices.noteOn (channel, key, velocity);
    if (key >= 0 && key < 128) pushTrace (track, key, t.voices.lastShapedVelocity());
}

void Engine::noteOffTrack (int track, int channel, int key) noexcept
{
    if (track < 0 || track >= kTracks) return;
    activeNotes_ = std::max (0, activeNotes_ - 1);
    auto& t = tracks_[static_cast<std::size_t> (track)];
    if (t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth)) t.synth.noteOff (channel, key);
    else t.voices.noteOff (channel, key);
}

void Engine::noteOn (int channel, int key, float velocity) noexcept
{
    for (int t = 0; t < kTracks; ++t)
    {
        const int ch = tracks_[static_cast<std::size_t> (t)].config.midiChannel.load (std::memory_order_relaxed);
        if (ch == 0 || ch == channel + 1) noteOnTrack (t, channel, key, velocity);
    }
}

void Engine::noteOff (int channel, int key) noexcept
{
    for (int t = 0; t < kTracks; ++t)
    {
        const int ch = tracks_[static_cast<std::size_t> (t)].config.midiChannel.load (std::memory_order_relaxed);
        if (ch == 0 || ch == channel + 1) noteOffTrack (t, channel, key);
    }
}

void Engine::panic() noexcept
{
    activeNotes_ = 0;
    for (auto& t : tracks_) { t.voices.panic(); t.synth.panic(); }
}

void Engine::releaseAll() noexcept
{
    activeNotes_ = 0;
    for (auto& t : tracks_) { t.voices.releaseAll(); t.synth.releaseAll(); }
}

void Engine::setParameter (std::uint32_t, double) noexcept {}

int Engine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& t : tracks_) n += t.voices.activeVoices() + t.synth.activeVoices();
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
        // changement de type ou désactivation : les voix de la piste s'arrêtent net (rien ne doit sonner sous l'ancien type)
        const int type = t.type.load (std::memory_order_relaxed), active = t.active.load (std::memory_order_relaxed);
        if (type != t.appliedType || (active == 0 && t.appliedActive != 0))
        {
            t.voices.allSoundOff();
            t.synth.allSoundOff();
            t.appliedType = type;
        }
        t.appliedActive = active;
    }
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
    publishVoices();
}

void Engine::processChunk (float* const* out, int numChannels, int n) noexcept
{
    using namespace mixer;
    auto zero = [n] (Stereo& s) { std::fill (s.l.begin(), s.l.begin() + n, 0.0f); std::fill (s.r.begin(), s.r.begin() + n, 0.0f); };
    zero (master_);
    for (auto& b : busIn_) zero (b);
    for (auto& b : buses_) b.hasInput = false;

    // Solo : deux domaines. Un solo de piste coupe les pistes non solo (pas les bus : le solo doit rester audible) ; un solo
    // de bus coupe les bus non solo.
    bool trackSolo = false, busSolo = false;
    for (const auto& t : tracks_) trackSolo = trackSolo || param (t.idx.solo, 0.0) >= 0.5;
    for (const auto& b : buses_) busSolo = busSolo || param (b.idx.solo, 0.0) >= 0.5;

    auto channelPair = [&] (int output) -> std::pair<float*, float*>
    {
        const int l = 2 * (output - 1), r = l + 1;
        if (output < 1 || r >= numChannels) return { nullptr, nullptr };
        return { out[l], out[r] };
    };

    // Mixe une bande (piste ou bus) : fader, pan, sorties. `in` : signal brut ; rend le signal final dans final_.
    auto stripOutputs = [&] (Stereo& in, std::atomic<float>& pkL, std::atomic<float>& pkR, std::atomic<float>& rmsL, std::atomic<float>& rmsR,
                             const AtomicStrip& cfg, const ParamIndices& idx, float& lastFader, float& lastPanL, float& lastPanR,
                             bool muted) noexcept
    {
        const float fader = muted ? 0.0f : dbToGain (param (idx.volume, 0.0));
        const float pan = static_cast<float> (param (idx.pan, 0.0) / 100.0);
        float pl = 1.0f, pr = 1.0f;
        panGains (static_cast<PanMode> (cfg.panMode.load (std::memory_order_relaxed)), pan, pl, pr);

        // point 2 : post-fader / pré-pan ; point 3 : post-pan
        const float fStep = (fader - lastFader) / static_cast<float> (n);
        const float plStep = (pl - lastPanL) / static_cast<float> (n);
        const float prStep = (pr - lastPanR) / static_cast<float> (n);
        float g = lastFader, gl = lastPanL, gr = lastPanR;
        for (int i = 0; i < n; ++i)
        {
            const float l = in.l[static_cast<std::size_t> (i)] * g, r = in.r[static_cast<std::size_t> (i)] * g;
            postFader_.l[static_cast<std::size_t> (i)] = l;
            postFader_.r[static_cast<std::size_t> (i)] = r;
            final_.l[static_cast<std::size_t> (i)] = l * gl;
            final_.r[static_cast<std::size_t> (i)] = r * gr;
            g += fStep; gl += plStep; gr += prStep;
        }
        lastFader = fader; lastPanL = pl; lastPanR = pr;

        // vu-mètre : crête et RMS du signal final du bloc
        float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float l = final_.l[static_cast<std::size_t> (i)], r = final_.r[static_cast<std::size_t> (i)];
            peakL = std::max (peakL, std::abs (l)); peakR = std::max (peakR, std::abs (r));
            sumL += l * l; sumR += r * r;
        }
        pkL.store (peakL, std::memory_order_relaxed);
        pkR.store (peakR, std::memory_order_relaxed);
        rmsL.store (std::sqrt (sumL / static_cast<float> (n)), std::memory_order_relaxed);
        rmsR.store (std::sqrt (sumR / static_cast<float> (n)), std::memory_order_relaxed);
        return peakL > 0.0f || peakR > 0.0f;
    };

    auto routeStrip = [&] (const AtomicStrip& cfg, const ParamIndices& idx, std::array<float, numSends>& lastSend, Stereo& rawIn,
                           bool isBus, int busIndex) noexcept
    {
        // envois : un point de captation par envoi
        for (int s = 0; s < numSends; ++s)
        {
            const int dest = cfg.sends[static_cast<std::size_t> (s)].destination.load (std::memory_order_relaxed);
            const bool muted = cfg.sends[static_cast<std::size_t> (s)].mute.load (std::memory_order_relaxed) != 0;
            const double levelDb = param (idx.sendLevel[s], -100.0);
            const float target = (dest <= 0 || muted) ? 0.0f : dbToGain (levelDb);
            float& last = lastSend[static_cast<std::size_t> (s)];
            if (dest <= 0 || dest > kBuses || (isBus && dest - 1 <= busIndex) || (target == 0.0f && last == 0.0f)) { last = target; continue; }
            const int tap = cfg.sends[static_cast<std::size_t> (s)].tap.load (std::memory_order_relaxed);
            const Stereo& src = tap <= 1 ? rawIn : (tap == 2 ? postFader_ : final_);
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
        // sortie principale (Master ou bus) et sortie directe
        if (cfg.mainEnabled.load (std::memory_order_relaxed) != 0)
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
                target->l[static_cast<std::size_t> (i)] += final_.l[static_cast<std::size_t> (i)];
                target->r[static_cast<std::size_t> (i)] += final_.r[static_cast<std::size_t> (i)];
            }
        }
        const int direct = cfg.directOut.load (std::memory_order_relaxed);
        if (direct > 0)
        {
            const auto pair = channelPair (direct);
            if (pair.first != nullptr && pair.second != nullptr)
                for (int i = 0; i < n; ++i)
                {
                    pair.first[i] += final_.l[static_cast<std::size_t> (i)];
                    pair.second[i] += final_.r[static_cast<std::size_t> (i)];
                }
        }
    };

    // ---- pistes
    for (int ti = 0; ti < kTracks; ++ti)
    {
        auto& t = tracks_[static_cast<std::size_t> (ti)];
        if (t.active.load (std::memory_order_relaxed) == 0) { t.lastSend.fill (0.0f); continue; }
        const bool synthTrack = t.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth);
        const bool wasSilent = t.peakL.load (std::memory_order_relaxed) == 0.0f && t.peakR.load (std::memory_order_relaxed) == 0.0f;
        // piste sans instrument, ou au repos et déjà silencieuse : rien à faire (le cas de presque toutes les pistes)
        const bool idle = synthTrack ? t.synth.activeVoices() == 0 : (t.slot.current() == nullptr || t.voices.activeVoices() == 0);
        if (wasSilent && idle) { t.lastSend.fill (0.0f); continue; }

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
        if (synthTrack)
        {
            SynthVoices::Params sp;
            sp.cutoffHz = fp.cutoffHz[0];
            sp.resonance = fp.resonance[0];
            sp.tuneCents = cents;
            t.synth.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, sp);
        }
        else
        {
            t.voices.setFilterParams (fp);
            t.voices.render (raw_.l.data(), raw_.r.data(), n, t.lastGain, gain, cents);
        }
        t.lastGain = gain;

        const bool muted = param (t.idx.mute, 0.0) >= 0.5 || (trackSolo && param (t.idx.solo, 0.0) < 0.5);
        stripOutputs (raw_, t.peakL, t.peakR, t.rmsL, t.rmsR, t.config, t.idx, t.lastFader, t.lastPanL, t.lastPanR, muted);
        routeStrip (t.config, t.idx, t.lastSend, raw_, false, -1);
    }

    // ---- bus, dans l'ordre : un bus n'alimente que des bus d'indice supérieur
    for (int bi = 0; bi < kBuses; ++bi)
    {
        auto& b = buses_[static_cast<std::size_t> (bi)];
        const bool wasSilent = b.peakL.load (std::memory_order_relaxed) == 0.0f && b.peakR.load (std::memory_order_relaxed) == 0.0f;
        if (! b.hasInput && wasSilent) { b.lastSend.fill (0.0f); continue; }
        const bool muted = param (b.idx.mute, 0.0) >= 0.5 || (busSolo && param (b.idx.solo, 0.0) < 0.5);
        Stereo& in = busIn_[static_cast<std::size_t> (bi)];
        stripOutputs (in, b.peakL, b.peakR, b.rmsL, b.rmsR, b.config, b.idx, b.lastFader, b.lastPanL, b.lastPanR, muted);
        routeStrip (b.config, b.idx, b.lastSend, in, true, bi);
    }

    // ---- master : trim, fader, pan, mute, réduction mono
    {
        const float trimGain = dbToGain (std::clamp (param (masterTrim_, 0.0), -12.0, 12.0), -1000.0); // le trim ne coupe jamais
        const bool muted = param (masterMute_, 0.0) >= 0.5;
        const float fader = muted ? 0.0f : dbToGain (param (masterVolume_, 0.0));
        const float target = fader * trimGain;
        float pl = 1.0f, pr = 1.0f;
        panGains (static_cast<PanMode> (masterPanMode_.load (std::memory_order_relaxed)), static_cast<float> (param (masterPan_, 0.0) / 100.0), pl, pr);
        const bool mono = param (masterMono_, 0.0) >= 0.5;
        const float gStep = (target - lastMasterGain_) / static_cast<float> (n);
        const float plStep = (pl - lastMasterPanL_) / static_cast<float> (n);
        const float prStep = (pr - lastMasterPanR_) / static_cast<float> (n);
        float g = lastMasterGain_, gl = lastMasterPanL_, gr = lastMasterPanR_;
        const auto pair = channelPair (masterOutput_.load (std::memory_order_relaxed));
        float peakL = 0.0f, peakR = 0.0f, sumL = 0.0f, sumR = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float l = master_.l[static_cast<std::size_t> (i)] * g * gl;
            float r = master_.r[static_cast<std::size_t> (i)] * g * gr;
            if (mono) l = r = 0.5f * (l + r);
            peakL = std::max (peakL, std::abs (l)); peakR = std::max (peakR, std::abs (r));
            sumL += l * l; sumR += r * r;
            if (pair.first != nullptr) pair.first[i] += l;
            if (pair.second != nullptr) pair.second[i] += r;
            g += gStep; gl += plStep; gr += prStep;
        }
        lastMasterGain_ = target; lastMasterPanL_ = pl; lastMasterPanR_ = pr;
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
        if (tr.type.load (std::memory_order_relaxed) == static_cast<int> (TrackType::Synth))
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
