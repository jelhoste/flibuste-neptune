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

// Intégrale de la correction ci-dessus (PolyBLAMP) pour un changement de pente de 2 unités de phase : g(x) du coin à t = 0.
// Un coin de changement de pente Δs corrige la sortie de Δs . dt / 2 . g.
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
} // namespace

void SynthVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

void SynthVoices::setPatch (const SynthPatch* patch) noexcept
{
    patch_ = patch;
    if (patch == nullptr) allSoundOff();
    numHeld_ = 0;
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

void SynthVoices::releaseAll() noexcept
{
    if (patch_ != nullptr)
        for (auto& v : voices_)
        {
            if (! v.active || v.releasing || v.stolen) continue;
            v.releasing = true;
            v.env.noteOff (patch_->ampEnv, sampleRate_);
            v.fenv.noteOff (patch_->filterEnv, sampleRate_);
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
    const double glideBlocks = patch_ != nullptr ? patch_->desc.glideMs * 0.001 * sampleRate_ / kControlFrames : 0.0;
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
    v->env.begin (patch_->ampEnv, sampleRate_);
    v->fenv.begin (patch_->filterEnv, sampleRate_);
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
        v.releasing = true;
        v.env.noteOff (patch_->ampEnv, sampleRate_);
        v.fenv.noteOff (patch_->filterEnv, sampleRate_);
    }
}

void SynthVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const Params& params) noexcept
{
    if (patch_ == nullptr || left == nullptr || numFrames <= 0) return;
    for (auto& v : voices_)
        if (v.active) renderVoice (v, left, right, numFrames, gainStart, gainEnd, params);
}

void SynthVoices::renderVoice (Voice& v, float* left, float* right, int numFrames, float g0, float g1, const Params& p) noexcept
{
    const auto& patch = *patch_;
    const auto& d = patch.desc;
    const bool filterOn = d.filter.on;
    const bool envModulated = filterOn && d.filter.envDepthOctaves != 0.0;
    const double octaves = static_cast<double> (d.octave) * 12.0;
    const float gStep = (g1 - g0) / static_cast<float> (numFrames);
    float mg = g0;
    bool coefsReady = false;
    const double resonance = p.resonance;

    for (int offset = 0; offset < numFrames;)
    {
        const int m = std::min (kControlFrames, numFrames - offset);

        // --- contrôle : glissando, hauteur, enveloppe et coefficients du filtre
        bool gliding = false;
        if (v.note != v.targetNote)
        {
            const double delta = v.targetNote - v.note;
            v.note = std::abs (delta) <= v.glideStep || v.glideStep <= 0.0 ? v.targetNote : v.note + (delta > 0.0 ? v.glideStep : -v.glideStep);
            gliding = v.note != v.targetNote;
        }
        const double semis = v.note - 69.0 + octaves + p.tuneCents / 100.0;
        const double dt = std::clamp (440.0 * std::exp2 (semis / 12.0) / sampleRate_, 1e-6, 0.45);
        if (envModulated)
        {
            v.fenv.advance (patch.filterEnv, sampleRate_, static_cast<double>(m));
            v.fenvValue = v.fenv.value();
        }
        if (filterOn && (! coefsReady || envModulated || gliding))
        {
            const double cutoff = modulatedCutoffHz (d.filter, p.cutoffHz, static_cast<int> (std::lround (v.note)), v.velocity, v.fenvValue, sampleRate_);
            v.fcoef = makeFilterCoefs (d.filter, cutoff, resonance, sampleRate_);
            coefsReady = true;
        }

        // --- audio
        for (int i = 0; i < m; ++i)
        {
            const double t = v.phase;
            double s;
            switch (d.waveform)
            {
                case Waveform::Sine: s = std::sin (kTwoPi * t); break;
                case Waveform::Triangle:
                {
                    s = 1.0 - 4.0 * std::abs (t - 0.5);
                    s += 4.0 * dt * (polyBlampShape (t, dt) - polyBlampShape (wrapped (t + 0.5), dt));
                    break;
                }
                case Waveform::Square:
                    s = (t < 0.5 ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (wrapped (t + 0.5), dt);
                    break;
                default: // Saw
                    s = 2.0 * t - 1.0 - polyBlep (t, dt);
                    break;
            }
            v.phase += dt;
            if (v.phase >= 1.0) v.phase -= 1.0;

            float x = static_cast<float> (s);
            if (filterOn) x = processFilter (v.fcoef, v.fstate, x + 1e-15f); // 1e-15 : pas de dénormaux quand le signal tombe à zéro

            const float envValue = v.env.next (patch.ampEnv, sampleRate_);
            if (v.env.finished() && envValue <= 0.0f) { v.active = false; return; }
            float e = v.velocity * static_cast<float> (SynthVoices::kVoiceHeadroom) * mg * envValue;
            v.level = v.velocity * envValue;
            if (v.stolen)
            {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f) { v.active = false; return; }
                e *= v.fade;
            }
            const float out = x * e;
            left[offset + i] += out;
            if (right != nullptr) right[offset + i] += out;
            mg += gStep;
        }
        offset += m;
    }
}
} // namespace neptune::core
