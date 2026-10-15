#include "neptune/core/SynthVoices.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr int kControlFrames = 16; // cadence de mise à jour de la hauteur et du filtre (0,33 ms à 48 kHz)

// Correction de la discontinuité d'une dent de scie / d'un carré (PolyBLEP) ; t : phase, dt : incrément de phase.
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// Intégrale de la correction ci-dessus (PolyBLAMP) pour un coin du triangle ; g(x) du coin à t = 0.
inline double polyBlampShape (double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = 1.0 - t / dt;
        return x * x * x / 3.0;
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt + 1.0;
        return x * x * x / 3.0;
    }
    return 0.0;
}

inline double wrapped (double t) noexcept { return t >= 1.0 ? t - 1.0 : t; }

// Valeur « naïve » (sans correction) des formes à marche, pour la synchro dure.
inline double stepValue (int wave, double ph, double pw) noexcept
{
    switch (wave)
    {
        case kWaveSaw: return 2.0 * ph - 1.0;
        case kWaveSquare: return ph < 0.5 ? 1.0 : -1.0;
        default: return ph < pw ? 1.0 : -1.0; // kWavePulse
    }
}

inline bool isStepWave (int wave) noexcept { return wave == kWaveSaw || wave == kWaveSquare || wave == kWavePulse; }

// Une forme d'onde, à bande limitée, pour la phase `ph` et l'incrément `dt`.
inline float waveSample (int wave, double ph, double dt, double pw, const float* table) noexcept
{
    switch (wave)
    {
        case kWaveSine: return static_cast<float> (std::sin (kTwoPi * ph));
        case kWaveTriangle:
            return static_cast<float> (1.0 - 4.0 * std::abs (ph - 0.5) + 4.0 * dt * (polyBlampShape (ph, dt) - polyBlampShape (wrapped (ph + 0.5), dt)));
        case kWaveSaw: return static_cast<float> (2.0 * ph - 1.0 - polyBlep (ph, dt));
        case kWaveSquare: return static_cast<float> ((ph < 0.5 ? 1.0 : -1.0) + polyBlep (ph, dt) - polyBlep (wrapped (ph + 0.5), dt));
        case kWavePulse:
            return static_cast<float> ((ph < pw ? 1.0 : -1.0) + polyBlep (ph, dt) - polyBlep (wrapped (ph + 1.0 - pw), dt));
        default:
        {
            const double pos = ph * WaveTables::kSize;
            const auto i = static_cast<int> (pos);
            const auto f = static_cast<float> (pos - i);
            return table[i] + f * (table[i + 1] - table[i]); // la table a un échantillon de garde
        }
    }
}

struct SyncIn
{
    const double* masterPhase; // phase de l'oscillateur maître à chaque échantillon du bloc
    double masterDt;
    double prev;               // phase du maître au dernier échantillon du bloc précédent
};

// Rend `m` échantillons d'UNE voix d'unisson. `sync` : synchro dure sur le maître ; `record` : enregistre la phase de chaque
// échantillon (pour les oscillateurs qui se synchronisent sur celui-ci).
void renderUnison (int wave, double& phase, double dt, double pw, int m, float* out, const float* table, const SyncIn* sync, double* record) noexcept
{
    const bool step = isStepWave (wave);
    for (int i = 0; i < m; ++i)
    {
        double ph = phase;
        double afterCorrection = 0.0;
        if (sync != nullptr)
        {
            const double mp = sync->masterPhase[i];
            const double prev = i > 0 ? sync->masterPhase[i - 1] : sync->prev;
            if (mp < prev) // le maître a fait un tour : l'esclave repart de 0 (à l'instant exact du tour, en fraction d'échantillon)
            {
                const double frac = std::clamp (mp / sync->masterDt, 0.0, 1.0);
                if (step)
                {
                    double pre = ph - frac * dt;
                    if (pre < 0.0) pre += 1.0;
                    const double vPre = stepValue (wave, pre, pw);
                    const double dActual = stepValue (wave, 0.0, pw) - vPre;
                    const double dNatural = stepValue (wave, 0.0, pw) - stepValue (wave, 1.0 - 1e-12, pw);
                    if (i > 0) out[i - 1] += static_cast<float> (0.5 * dActual * frac * frac);
                    afterCorrection = 0.5 * (dActual - dNatural) * (2.0 * frac - frac * frac - 1.0);
                }
                ph = frac * dt;
            }
        }
        if (record != nullptr) record[i] = ph;
        out[i] = waveSample (wave, ph, dt, pw, table) + static_cast<float> (afterCorrection);
        phase = ph + dt;
        if (phase >= 1.0) phase -= 1.0;
    }
}

inline float xorshiftNoise (std::uint32_t& s) noexcept
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float> (static_cast<std::int32_t> (s)) * (1.0f / 2147483648.0f);
}

// Position d'unisson -> décalage de détune relatif (-1..1)
inline double detuneOffset (DetuneCurve c, double pos) noexcept
{
    if (c == DetuneCurve::Linear) return pos;
    const double a = std::pow (std::abs (pos), 1.65); // « supersaw » : voix centrales serrées, voix extrêmes très écartées
    return pos < 0.0 ? -a : a;
}
} // namespace

void SynthVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

void SynthVoices::setPatch (const SynthPatch* patch) noexcept
{
    // changement de preset : les notes en cours gardent l'ancien patch et leurs valeurs (capture par voix)
    if (patch != nullptr && patch_ != nullptr && patch != patch_ && patch->freezeRunningVoices)
        for (auto& v : voices_)
        {
            if (! v.active || v.frozen) continue;
            v.frozen = true;
            v.patch = patch_;
            v.frozenParams = lastParams_;
            v.frozenGain = lastGainEnd_;
        }
    patch_ = patch;
    if (patch == nullptr) allSoundOff();
    numHeld_ = 0;
    publishGenerations();
}

void SynthVoices::publishGenerations() noexcept
{
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (const auto& v : voices_)
        if (v.active && v.frozen && v.patch != nullptr) oldest = std::min (oldest, v.patch->generation);
    // ordre : d'abord les voix figées, ensuite le patch adopté (le publieur lit dans l'ordre inverse)
    oldestFrozen_.store (oldest, std::memory_order_release);
    adopted_.store (patch_ != nullptr ? patch_->generation : 0, std::memory_order_release);
}

int SynthVoices::frozenVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active && v.frozen; }));
}

void SynthVoices::allSoundOff() noexcept
{
    for (auto& v : voices_) v = Voice {};
    numHeld_ = 0;
}

SynthVoices::VoiceInfo SynthVoices::describe (const Voice& v) const noexcept
{
    VoiceInfo i;
    i.key = v.key;
    i.channel = v.channel;
    i.velocity = std::clamp (static_cast<int> (std::lround (v.velocity * 127.0f)), 1, 127);
    i.releasing = v.releasing;
    i.fading = v.stolen;
    i.level = v.level;
    i.order = v.order;
    return i;
}

int SynthVoices::activeVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active; }));
}

int SynthVoices::heldVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(),
                                            [] (const Voice& v) { return v.active && ! v.releasing && ! v.stolen; }));
}

int SynthVoices::soundingVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active && ! v.stolen; }));
}

namespace
{
bool sacrificedBefore (bool relA, float levA, std::uint64_t ordA, bool relB, float levB, std::uint64_t ordB) noexcept
{
    if (relA != relB) return relA;
    if (levA != levB) return levA < levB;
    return ordA < ordB;
}
} // namespace

bool SynthVoices::weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept
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

void SynthVoices::stealWeakest() noexcept
{
    Voice* best = nullptr;
    for (auto& v : voices_)
    {
        if (! v.active || v.stolen) continue;
        if (best == nullptr || sacrificedBefore (v.releasing, v.level, v.order, best->releasing, best->level, best->order)) best = &v;
    }
    if (best != nullptr) steal (*best);
}

void SynthVoices::steal (Voice& v) noexcept
{
    v.stolen = true;
    v.fadeStep = static_cast<float> (1.0 / std::max (1.0, kStealFadeSeconds * sampleRate_));
}

void SynthVoices::cutAll() noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen) steal (v);
}

void SynthVoices::cutKey (int channel, int key) noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen && v.channel == channel && v.key == key) steal (v);
}

void SynthVoices::panic() noexcept
{
    cutAll();
    numHeld_ = 0;
}

EnvelopeTiming SynthVoices::timingOf (const EnvelopeRuntime& e, const Params::EnvParams& p) noexcept
{
    auto t = EnvelopeTiming::of (e);
    if (p.attackMs >= 0.0) t.attackSec = p.attackMs * 0.001;
    if (p.decayMs >= 0.0) t.decaySec = p.decayMs * 0.001;
    if (p.sustain >= 0.0) t.sustain = static_cast<float> (std::min (p.sustain, 1.0));
    if (p.releaseMs >= 0.0) t.releaseSec = p.releaseMs * 0.001;
    return t;
}

// Note Off d'une voix : le relâchement est celui des paramètres du moment (ceux d'avant le figement pour une voix figée).
void SynthVoices::noteOffVoice (Voice& v) noexcept
{
    v.releasing = true;
    const auto& pt = patchOf (v);
    const Params& p = v.frozen ? v.frozenParams : lastParams_;
    const auto amp = timingOf (pt.ampEnv, p.ampEnv);
    v.env.noteOff (pt.ampEnv, sampleRate_, &amp);
    v.penv.noteOff (pt.pitchEnv, sampleRate_);
    for (std::size_t f = 0; f < 2; ++f)
    {
        const auto ft = timingOf (pt.filterEnv[f], p.filterEnv[f]);
        v.fenv[f].noteOff (pt.filterEnv[f], sampleRate_, &ft);
    }
}

void SynthVoices::releaseAll() noexcept
{
    if (patch_ != nullptr)
        for (auto& v : voices_)
        {
            if (! v.active || v.releasing || v.stolen) continue;
            noteOffVoice (v);
        }
    numHeld_ = 0;
}

SynthVoices::Voice* SynthVoices::allocate() noexcept
{
    for (auto& v : voices_)
        if (! v.active) return &v;
    // Plus de place du tout (rafale de vols) : la plus ancienne voix en fondu, sinon la plus ancienne.
    Voice* best = nullptr;
    for (auto& v : voices_)
        if (v.stolen && (best == nullptr || v.order < best->order)) best = &v;
    if (best == nullptr)
        for (auto& v : voices_)
            if (best == nullptr || v.order < best->order) best = &v;
    return best;
}

SynthVoices::Voice* SynthVoices::chooseVictim (int newKey, bool& dropNew) noexcept
{
    dropNew = false;
    const auto mode = patch_ != nullptr ? patch_->desc.stealMode : VoiceStealMode::Oldest;
    auto better = [mode] (const Voice& a, const Voice* b)
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
    Voice* victim = nullptr;
    for (auto& v : voices_)
        if (v.active && ! v.releasing && ! v.stolen && better (v, victim)) victim = &v;
    if (victim != nullptr)
    {
        if (mode == VoiceStealMode::KeepLow && newKey >= victim->key) dropNew = true;
        if (mode == VoiceStealMode::KeepHigh && newKey <= victim->key) dropNew = true;
    }
    return victim;
}

void SynthVoices::pushHeld (int channel, int key, float velocity) noexcept
{
    removeHeld (channel, key);
    if (numHeld_ >= kMaxHeldKeys)
    {
        for (int i = 1; i < numHeld_; ++i) held_[i - 1] = held_[i];
        --numHeld_;
    }
    held_[numHeld_++] = { channel, key, velocity };
}

void SynthVoices::removeHeld (int channel, int key) noexcept
{
    int w = 0;
    for (int i = 0; i < numHeld_; ++i)
        if (! (held_[i].channel == channel && held_[i].key == key)) held_[w++] = held_[i];
    numHeld_ = w;
}

// Legato : les voix tenues changent de hauteur (avec glissando si réglé) au lieu de repartir. Faux s'il n'y en a pas.
bool SynthVoices::retargetHeld (int newKey) noexcept
{
    bool any = false;
    const double glideMs = lastParams_.glideMs >= 0.0 ? lastParams_.glideMs : (patch_ != nullptr ? patch_->desc.glideMs : 0.0);
    const double glideBlocks = glideMs * 0.001 * sampleRate_ / kControlFrames;
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen) continue;
        v.key = newKey;
        v.targetNote = newKey;
        v.glideStep = glideBlocks >= 1.0 ? std::abs (v.targetNote - v.note) / glideBlocks : 0.0;
        if (v.glideStep <= 0.0) v.note = v.targetNote;
        any = true;
    }
    return any;
}

void SynthVoices::startNote (int channel, int key, float velocity) noexcept
{
    if (patch_ == nullptr) return;
    const auto& d = patch_->desc;
    // Limite de voix : on fait s'éteindre une voix tenue selon le mode de vol (fondu court, pas de clic). Mono et legato : aucune.
    const int limit = d.polyphony == PolyphonyMode::Poly ? std::clamp (d.maxVoices, 1, kMaxVoices) : kMaxVoices;
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
    *v = Voice {};
    v->active = true;
    v->channel = channel;
    v->key = key;
    v->order = ++counter_;
    v->note = v->targetNote = key;
    v->velocity = std::clamp (velocity, 0.0f, 1.0f);
    v->level = v->velocity;
    const auto amp = timingOf (patch_->ampEnv, lastParams_.ampEnv);
    v->env.begin (patch_->ampEnv, sampleRate_, &amp);
    v->penv.begin (patch_->pitchEnv, sampleRate_);
    for (std::size_t f = 0; f < 2; ++f)
    {
        const auto ft = timingOf (patch_->filterEnv[f], lastParams_.filterEnv[f]);
        v->fenv[f].begin (patch_->filterEnv[f], sampleRate_, &ft);
    }
    v->rng = static_cast<std::uint32_t> (0x9E3779B9u * (static_cast<std::uint32_t> (counter_) + 1u)) | 1u;
    initPhases (*v);
}

// Phases de départ : « retrigger » : la phase réglée (les voix d'unisson sont décalées pour ne pas s'additionner en phase) ;
// sinon phase libre (aléatoire).
void SynthVoices::initPhases (Voice& v) noexcept
{
    const auto& d = patch_->desc;
    for (int o = 0; o < numOscillators; ++o)
        for (int u = 0; u < maxUnison; ++u)
        {
            const auto& od = d.osc[static_cast<std::size_t> (o)];
            double ph;
            if (od.retrigger) ph = od.startPhase + (u == 0 ? 0.0 : 0.6180339887 * u);
            else
            {
                v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
                ph = static_cast<double> (v.rng & 0xFFFFFFu) / 16777216.0;
            }
            ph -= std::floor (ph);
            v.phase[static_cast<std::size_t> (o)][static_cast<std::size_t> (u)] = ph;
        }
    v.subPhase = 0.0;
}

void SynthVoices::noteOn (int channel, int key, float velocity) noexcept
{
    if (patch_ == nullptr) return;
    const float vel = std::clamp (velocity, 0.0f, 1.0f);
    if (patch_->desc.polyphony != PolyphonyMode::Poly)
    {
        // Mono : une seule note à la fois (la plus récente) ; les touches tenues sont mémorisées pour revenir à la précédente.
        const bool otherHeld = numHeld_ > 0;
        pushHeld (channel, key, vel);
        if (patch_->desc.polyphony == PolyphonyMode::Legato && otherHeld && retargetHeld (key)) return;
        cutAll();
        startNote (channel, key, vel);
        return;
    }
    cutKey (channel, key); // même touche rejouée : l'ancienne voix s'éteint (pas d'empilement : le synthé n'a pas d'échantillon à finir)
    startNote (channel, key, vel);
}

void SynthVoices::noteOff (int channel, int key) noexcept
{
    if (patch_ == nullptr) return;
    if (patch_->desc.polyphony != PolyphonyMode::Poly)
    {
        bool wasSounding = false;
        for (const auto& v : voices_)
            if (v.active && ! v.releasing && ! v.stolen && v.channel == channel && v.key == key) { wasSounding = true; break; }
        removeHeld (channel, key);
        if (wasSounding && numHeld_ > 0)
        {
            const HeldKey back = held_[numHeld_ - 1];
            if (patch_->desc.polyphony == PolyphonyMode::Legato && retargetHeld (back.key))
                return; // la voix continue à la hauteur de la touche précédente
            cutAll();
            startNote (back.channel, back.key, back.velocity);
            return;
        }
    }
    for (auto& v : voices_)
    {
        if (! v.active || v.releasing || v.stolen || v.channel != channel || v.key != key) continue;
        noteOffVoice (v);
    }
}

void SynthVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const Params& params) noexcept
{
    if (patch_ == nullptr || left == nullptr || numFrames <= 0) return;
    lastParams_ = params;
    lastGainEnd_ = gainEnd;
    bool anyFrozen = false;
    for (auto& v : voices_)
    {
        if (! v.active) continue;
        if (v.frozen)
        {
            anyFrozen = true;
            renderVoice (v, left, right, numFrames, v.frozenGain, v.frozenGain, v.frozenParams);
        }
        else renderVoice (v, left, right, numFrames, gainStart, gainEnd, params);
    }
    if (anyFrozen || oldestFrozen_.load (std::memory_order_relaxed) != std::numeric_limits<std::uint64_t>::max()) publishGenerations();
}

void SynthVoices::renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, const Params& p) noexcept
{
    const auto& patch = patchOf (v);
    const auto& d = patch.desc;
    const float gStep = (g1 - g0) / static_cast<float> (numFrames);
    float mg = g0;
    const WaveTables& tables = WaveTables::instance();

    // --- ce qui ne change pas pendant le bloc
    std::array<bool, 2> fOn { d.filters.filter[0].on, d.filters.filter[1].on };
    const bool parallel = d.filters.parallel;
    const auto balance = static_cast<float> (p.filterBalance >= 0.0 ? std::min (p.filterBalance, 1.0) : d.filters.balance);
    std::array<bool, 2> fEnvModulated { fOn[0] && d.filters.filter[0].envDepthOctaves != 0.0,
                                         fOn[1] && d.filters.filter[1].envDepthOctaves != 0.0 };
    const bool pitchEnvUsed = p.pitchEnvDepth != 0.0;
    std::array<float, numSources> level {};
    for (int s = 0; s < numSources; ++s)
        level[static_cast<std::size_t> (s)] = d.mix[static_cast<std::size_t> (s)].mute ? 0.0f : static_cast<float> (std::clamp (p.level[s], 0.0, 100.0) / 100.0);
    // synchro dure : l'oscillateur 1 tourne même s'il est muet quand un esclave audible en a besoin
    const bool needMaster = (d.osc[1].sync && level[1] > 0.0f) || (d.osc[2].sync && level[2] > 0.0f);
    // pan du mixeur (balance : le canal opposé est atténué)
    std::array<float, numSources> mixL {}, mixR {};
    for (int s = 0; s < numSources; ++s)
    {
        const auto pan = static_cast<float> (d.mix[static_cast<std::size_t> (s)].pan);
        mixL[static_cast<std::size_t> (s)] = pan > 0.0f ? 1.0f - pan : 1.0f;
        mixR[static_cast<std::size_t> (s)] = pan < 0.0f ? 1.0f + pan : 1.0f;
    }
    bool coefsReady = false;

    for (int offset = 0; offset < numFrames;)
    {
        const int m = std::min (kControlFrames, numFrames - offset);

        // --- contrôle : glissando, enveloppe de pitch, hauteur, filtres
        bool gliding = false;
        if (v.note != v.targetNote)
        {
            const double delta = v.targetNote - v.note;
            v.note = std::abs (delta) <= v.glideStep || v.glideStep <= 0.0 ? v.targetNote : v.note + (delta > 0.0 ? v.glideStep : -v.glideStep);
            gliding = v.note != v.targetNote;
        }
        float pitchEnvValue = 0.0f;
        if (pitchEnvUsed)
        {
            v.penv.advance (patch.pitchEnv, sampleRate_, static_cast<double> (m));
            pitchEnvValue = v.penv.value();
        }
        const double semis = v.note - 69.0 + p.tuneCents / 100.0 + p.pitchEnvDepth * static_cast<double> (pitchEnvValue);
        const double f0 = 440.0 * std::exp2 (semis / 12.0) / sampleRate_; // incrément de phase de la note

        bool updateCoefs = ! coefsReady || gliding;
        for (std::size_t f = 0; f < 2; ++f)
            if (fEnvModulated[f])
            {
                v.fenv[f].advance (patch.filterEnv[f], sampleRate_, static_cast<double> (m));
                v.fenvValue[f] = v.fenv[f].value();
                updateCoefs = true;
            }
        if (updateCoefs)
        {
            for (std::size_t f = 0; f < 2; ++f)
            {
                if (! fOn[f]) continue;
                const double cutoff = modulatedCutoffHz (d.filters.filter[f], p.cutoffHz[f], static_cast<int> (std::lround (v.note)),
                                                         v.velocity, v.fenvValue[f], sampleRate_);
                v.fcoef[f] = makeFilterCoefs (d.filters.filter[f], cutoff, p.resonance[f], sampleRate_);
            }
            coefsReady = true;
        }

        // --- sources : chaque oscillateur produit un signal stéréo (unisson réparti), sous-osc et bruit un signal mono
        float oscL[numOscillators][kControlFrames], oscR[numOscillators][kControlFrames];
        float tmp[kControlFrames];
        double masterPhase[maxUnison][kControlFrames];
        double masterDt[maxUnison] = {};
        bool oscRendered[numOscillators] = { false, false, false };
        int masterVoices = 1;
        for (int o = 0; o < numOscillators; ++o)
        {
            const auto& od = d.osc[static_cast<std::size_t> (o)];
            const bool audible = level[static_cast<std::size_t> (o)] > 0.0f;
            const bool asMaster = o == 0 && needMaster;
            if (! audible && ! asMaster) continue;
            oscRendered[o] = true;
            std::fill (oscL[o], oscL[o] + m, 0.0f);
            std::fill (oscR[o], oscR[o] + m, 0.0f);
            const double semisOsc = od.octave * 12.0 + od.semitone + p.fineCents[o] / 100.0;
            const double baseDt = std::clamp (f0 * std::exp2 (semisOsc / 12.0), 1e-6, 0.45);
            const double pw = std::clamp (p.pulse[o] / 100.0, 0.02, 0.98);
            const int n = od.unison;
            if (o == 0) masterVoices = n;
            const float voiceGain = 1.0f / std::sqrt (static_cast<float> (n));
            const bool useSync = o > 0 && od.sync && needMaster;
            for (int u = 0; u < n; ++u)
            {
                const double pos = n == 1 ? 0.0 : 2.0 * u / (n - 1) - 1.0;
                const double cents = detuneOffset (od.curve, pos) * p.unisonDetune[o];
                const double dt = std::clamp (baseDt * std::exp2 (cents / 1200.0), 1e-6, 0.45);
                const float* table = isAnalyticWave (od.wave) ? nullptr : tables.table (od.wave, WaveTables::mipFor (dt));
                SyncIn sync {};
                const SyncIn* syncPtr = nullptr;
                if (useSync)
                {
                    const int mu = std::min (u, masterVoices - 1);
                    sync = { masterPhase[mu], masterDt[mu], v.masterLast[static_cast<std::size_t> (mu)] };
                    syncPtr = &sync;
                }
                double* record = asMaster ? masterPhase[u] : nullptr;
                if (asMaster) masterDt[u] = dt;
                renderUnison (od.wave, v.phase[static_cast<std::size_t> (o)][static_cast<std::size_t> (u)], dt, pw, m, tmp, table, syncPtr, record);
                // répartition stéréo des voix d'unisson (puissance constante)
                const double a = (od.spread * pos + 1.0) * 0.78539816339;
                const float gl = static_cast<float> (std::cos (a)) * voiceGain * 1.41421356f, gr = static_cast<float> (std::sin (a)) * voiceGain * 1.41421356f;
                for (int i = 0; i < m; ++i)
                {
                    oscL[o][i] += tmp[i] * gl;
                    oscR[o][i] += tmp[i] * gr;
                }
            }
        }
        if (needMaster && oscRendered[0])
            for (int u = 0; u < masterVoices; ++u) v.masterLast[static_cast<std::size_t> (u)] = masterPhase[u][m - 1];

        float subBuf[kControlFrames];
        const bool subOn = level[kSourceSub] > 0.0f;
        if (subOn)
        {
            const double dt = std::clamp (f0 * std::exp2 (static_cast<double> (d.subOctave)), 1e-6, 0.45);
            const int w = d.subWave == SubWave::Triangle ? kWaveTriangle : d.subWave == SubWave::Square ? kWaveSquare : kWaveSine;
            renderUnison (w, v.subPhase, dt, 0.5, m, subBuf, nullptr, nullptr, nullptr);
        }
        float noiseBuf[kControlFrames];
        const bool noiseOn = level[kSourceNoise] > 0.0f;
        if (noiseOn)
        {
            if (d.noise == NoiseType::White)
                for (int i = 0; i < m; ++i) noiseBuf[i] = xorshiftNoise (v.rng);
            else // rose : filtre de Paul Kellet (3 pôles), normalisé
                for (int i = 0; i < m; ++i)
                {
                    const float white = xorshiftNoise (v.rng);
                    v.pink[0] = 0.99765f * v.pink[0] + white * 0.0990460f;
                    v.pink[1] = 0.96300f * v.pink[1] + white * 0.2965164f;
                    v.pink[2] = 0.57000f * v.pink[2] + white * 1.0526913f;
                    noiseBuf[i] = (v.pink[0] + v.pink[1] + v.pink[2] + white * 0.1848f) * 0.25f;
                }
        }

        // --- mixage, filtres, enveloppe, sortie
        for (int i = 0; i < m; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int o = 0; o < numOscillators; ++o)
            {
                if (! oscRendered[o] || level[static_cast<std::size_t> (o)] <= 0.0f) continue;
                const float lv = level[static_cast<std::size_t> (o)];
                l += oscL[o][i] * lv * mixL[static_cast<std::size_t> (o)];
                r += oscR[o][i] * lv * mixR[static_cast<std::size_t> (o)];
            }
            if (subOn)
            {
                l += subBuf[i] * level[kSourceSub] * mixL[kSourceSub];
                r += subBuf[i] * level[kSourceSub] * mixR[kSourceSub];
            }
            if (noiseOn)
            {
                l += noiseBuf[i] * level[kSourceNoise] * mixL[kSourceNoise];
                r += noiseBuf[i] * level[kSourceNoise] * mixR[kSourceNoise];
            }

            auto chain = [&] (float x, std::size_t ch) noexcept
            {
                x += 1e-15f; // pas de dénormaux quand le signal tombe à zéro
                if (! parallel)
                {
                    if (fOn[0]) x = processFilter (v.fcoef[0], v.fstate[0][ch], x);
                    if (fOn[1]) x = processFilter (v.fcoef[1], v.fstate[1][ch], x);
                    return x;
                }
                if (fOn[0] && fOn[1])
                {
                    const float y0 = processFilter (v.fcoef[0], v.fstate[0][ch], x);
                    const float y1 = processFilter (v.fcoef[1], v.fstate[1][ch], x);
                    return y0 + balance * (y1 - y0);
                }
                if (fOn[0]) return processFilter (v.fcoef[0], v.fstate[0][ch], x);
                if (fOn[1]) return processFilter (v.fcoef[1], v.fstate[1][ch], x);
                return x;
            };
            l = chain (l, 0);
            r = chain (r, 1);

            const float envValue = v.env.next (patch.ampEnv, sampleRate_);
            if (v.env.finished() && envValue <= 0.0f) { v.active = false; return; }
            float e = v.velocity * static_cast<float> (kVoiceHeadroom) * mg * envValue;
            v.level = v.velocity * envValue;
            if (v.stolen)
            {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f) { v.active = false; return; }
                e *= v.fade;
            }
            if (right != nullptr)
            {
                left[offset + i] += l * e;
                right[offset + i] += r * e;
            }
            else
                left[offset + i] += 0.5f * (l + r) * e;
            mg += gStep;
        }
        offset += m;
    }
}
} // namespace neptune::core
