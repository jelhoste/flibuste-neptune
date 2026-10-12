#include "neptune/core/Envelope.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
namespace
{
constexpr double kDbFloorLinear = 0.001; // -60 dB
constexpr double kDeclickSeconds = 0.002;

double clampd (double v, double lo, double hi) { return std::isfinite (v) ? std::clamp (v, lo, hi) : lo; }

EnvelopeSegment sanitisedSegment (EnvelopeSegment s, double minMs)
{
    s.ms = clampd (s.ms, minMs, envelopeMaxMs);
    s.curve.shape = clampd (s.curve.shape, -1.0, 1.0);
    return s;
}

// Fraction g(x) du chemin parcouru, x dans [0,1].
//  kind 0 : montée de 0 vers 1 ; kind 1 : descente de 1 vers `target` (0..1) ; kind 2 : descente vers 0 (relâchement).
double curveFraction (const CurveSpec& c, int kind, double target, double x)
{
    if (! c.db)
        return std::pow (x, std::exp2 (-3.0 * c.shape));

    if (kind == 0) // 0 -> 1, rampe en dB depuis le plancher, ramenée à 0 en x = 0
        return (std::pow (10.0, 3.0 * (x - 1.0)) - kDbFloorLinear) / (1.0 - kDbFloorLinear);
    if (kind == 1 && target >= kDbFloorLinear) // 1 -> target > 0 : dB exactement linéaire
        return target >= 1.0 ? x : (1.0 - std::pow (target, x)) / (1.0 - target);
    // vers 0 : de 0 dB au plancher, ramené exactement à 0 en bout de course
    return 1.0 - (std::pow (10.0, -3.0 * x) - kDbFloorLinear) / (1.0 - kDbFloorLinear);
}

void fillTable (std::array<float, EnvelopeRuntime::tableSize + 1>& t, const CurveSpec& c, int kind, double target)
{
    for (int i = 0; i <= EnvelopeRuntime::tableSize; ++i)
    {
        const double x = static_cast<double> (i) / EnvelopeRuntime::tableSize;
        t[static_cast<std::size_t> (i)] = static_cast<float> (std::clamp (curveFraction (c, kind, target, x), 0.0, 1.0));
    }
    t[0] = 0.0f;
    t[EnvelopeRuntime::tableSize] = 1.0f;
}

inline float lookup (const std::array<float, EnvelopeRuntime::tableSize + 1>& t, double x) noexcept
{
    const double p = std::clamp (x, 0.0, 1.0) * EnvelopeRuntime::tableSize;
    const int i = std::min (static_cast<int> (p), EnvelopeRuntime::tableSize - 1);
    const float f = static_cast<float> (p - i);
    return t[static_cast<std::size_t> (i)] + (t[static_cast<std::size_t> (i) + 1] - t[static_cast<std::size_t> (i)]) * f;
}
} // namespace

EnvelopeDescription sanitised (EnvelopeDescription e)
{
    e.delay = sanitisedSegment (e.delay, 0.0);
    e.attack = sanitisedSegment (e.attack, 0.0);
    e.hold = sanitisedSegment (e.hold, 0.0);
    e.decay = sanitisedSegment (e.decay, 0.0);
    e.release = sanitisedSegment (e.release, 1.0);
    e.sustain = clampd (e.sustain, 0.0, 1.0);
    return e;
}

EnvelopeDescription presetEnvelope (EnvelopePreset p)
{
    EnvelopeDescription e; // tous les segments actifs, durées nulles, sustain 1, relâchement 50 ms
    switch (p)
    {
        case EnvelopePreset::Sustained: // tous les segments : le réglage de départ
            break;
        case EnvelopePreset::PercussionAttack: // met en forme l'attaque seule ; le sample suit ensuite sa propre décroissance
            e.delay.on = e.hold.on = e.decay.on = e.release.on = false;
            e.attack = { true, 3.0, { 0.4, false } };
            break;
        case EnvelopePreset::PercussionAttackDecay: // attaque + hold + decay vers le silence : coupe la queue du son
            e.delay.on = e.release.on = false;
            e.attack = { true, 2.0, { 0.4, false } };
            e.hold = { true, 20.0, {} };
            e.decay = { true, 300.0, { 0.6, false } };
            e.sustain = 0.0;
            break;
    }
    return e;
}

const char* presetI18nKey (EnvelopePreset p) noexcept
{
    switch (p)
    {
        case EnvelopePreset::Sustained: return "envelope.preset_sustained";
        case EnvelopePreset::PercussionAttack: return "envelope.preset_perc_attack";
        case EnvelopePreset::PercussionAttackDecay: return "envelope.preset_perc_decay";
    }
    return "envelope.preset_sustained";
}

double envelopeTimeFromUnit (double u) noexcept
{
    u = std::clamp (u, 0.0, 1.0);
    return envelopeMaxMs * u * u * u;
}

double envelopeUnitFromTime (double ms) noexcept
{
    return std::cbrt (std::clamp (ms, 0.0, envelopeMaxMs) / envelopeMaxMs);
}

double envelopeCurveFraction (const CurveSpec& curve, int kind, double target, double x) noexcept
{
    return std::clamp (curveFraction (curve, kind, target, std::clamp (x, 0.0, 1.0)), 0.0, 1.0);
}

EnvelopeRuntime::EnvelopeRuntime() : EnvelopeRuntime (EnvelopeDescription {}) {}

EnvelopeRuntime::EnvelopeRuntime (const EnvelopeDescription& desc)
{
    const EnvelopeDescription d = sanitised (desc);
    delayOn = d.delay.on;     delaySec = d.delay.ms / 1000.0;
    attackOn = d.attack.on;   attackSec = d.attack.ms / 1000.0;
    holdOn = d.hold.on;       holdSec = d.hold.ms / 1000.0;
    decayOn = d.decay.on;     decaySec = d.decay.ms / 1000.0;
    releaseOn = d.release.on; releaseSec = d.release.ms / 1000.0;
    sustain = static_cast<float> (d.sustain);

    fillTable (attackCurve, d.attack.curve, 0, 1.0);
    fillTable (decayCurve, d.decay.curve, 1, d.sustain);
    fillTable (releaseCurve, d.release.curve, 2, 0.0);
}

void EnvelopeRunner::enterAfterHold (const EnvelopeRuntime& env) noexcept
{
    pos_ = 0.0;
    if (env.decayOn && env.decaySec > 0.0) { stage_ = Stage::Decay; value_ = 1.0f; return; }
    stage_ = Stage::Sustain;
    value_ = env.decayOn ? env.sustain : 1.0f; // decay à 0 ms : saut direct au niveau de sustain
    if (value_ <= 0.0f) stage_ = Stage::Done;
}

void EnvelopeRunner::enterFirstStage (const EnvelopeRuntime& env) noexcept
{
    pos_ = 0.0;
    if (env.delayOn && env.delaySec > 0.0) { stage_ = Stage::Delay; value_ = 0.0f; return; }
    if (env.attackOn && env.attackSec > 0.0) { stage_ = Stage::Attack; value_ = 0.0f; return; }
    if (env.holdOn && env.holdSec > 0.0) { stage_ = Stage::Hold; value_ = 1.0f; return; }
    enterAfterHold (env);
}

void EnvelopeRunner::begin (const EnvelopeRuntime& env, double) noexcept
{
    declick_ = false;
    enterFirstStage (env);
}

void EnvelopeRunner::noteOff (const EnvelopeRuntime& env, double sampleRate) noexcept
{
    if (stage_ == Stage::Done || stage_ == Stage::Release) return;
    releaseStart_ = value_;
    declick_ = ! (env.releaseOn && env.releaseSec > 0.0);
    releaseFrames_ = std::max (1.0, (declick_ ? kDeclickSeconds : env.releaseSec) * sampleRate);
    stage_ = Stage::Release;
    pos_ = 0.0;
}

float EnvelopeRunner::next (const EnvelopeRuntime& env, double sampleRate) noexcept
{
    const float out = value_;
    switch (stage_)
    {
        case Stage::Delay:
            pos_ += 1.0;
            if (pos_ >= env.delaySec * sampleRate)
            {
                pos_ = 0.0;
                if (env.attackOn && env.attackSec > 0.0) stage_ = Stage::Attack;
                else if (env.holdOn && env.holdSec > 0.0) { stage_ = Stage::Hold; value_ = 1.0f; }
                else enterAfterHold (env);
            }
            break;
        case Stage::Attack:
        {
            pos_ += 1.0;
            const double len = env.attackSec * sampleRate;
            if (pos_ >= len)
            {
                value_ = 1.0f;
                if (env.holdOn && env.holdSec > 0.0) { stage_ = Stage::Hold; pos_ = 0.0; }
                else enterAfterHold (env);
            }
            else
                value_ = lookup (env.attackCurve, pos_ / len);
            break;
        }
        case Stage::Hold:
            pos_ += 1.0;
            if (pos_ >= env.holdSec * sampleRate) enterAfterHold (env);
            break;
        case Stage::Decay:
        {
            pos_ += 1.0;
            const double len = env.decaySec * sampleRate;
            if (pos_ >= len)
            {
                value_ = env.sustain;
                stage_ = value_ <= 0.0f ? Stage::Done : Stage::Sustain;
            }
            else
                value_ = 1.0f + (env.sustain - 1.0f) * lookup (env.decayCurve, pos_ / len);
            break;
        }
        case Stage::Sustain:
            break;
        case Stage::Release:
        {
            pos_ += 1.0;
            if (pos_ >= releaseFrames_)
            {
                value_ = 0.0f;
                stage_ = Stage::Done;
            }
            else
            {
                const double x = pos_ / releaseFrames_;
                value_ = releaseStart_ * (1.0f - (declick_ ? static_cast<float> (x) : lookup (env.releaseCurve, x)));
            }
            break;
        }
        case Stage::Done:
            break;
    }
    return out;
}
} // namespace neptune::core
