#include "neptune/core/MicrodecaleModule.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/DelayRead.h"

namespace neptune::core
{
using namespace microdecale;

namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double dbToLin (double db) noexcept { return std::exp (db * 0.11512925464970229); }
inline double coef (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * fs)); }
} // namespace

MicrodecaleModule::MicrodecaleModule()
{
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

bool MicrodecaleModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto fixed = [] (P s) { s.automatable = false; return s; };
    auto pct = [] (ParamId id, const char* n, double lo, double hi, double def) { return P::continuous (id, n, Unit::Percent, lo, hi, def); };
    auto cents = [] (ParamId id, const char* n, double def) { auto s = P::continuous (id, n, Unit::Cents, -1200.0, 1200.0, def); s.decimals = 1; return s; };
    auto delay = [] (ParamId id, const char* n, double def) { auto s = P::continuous (id, n, Unit::Milliseconds, 0.0, maxDelayMs, def, Curve::Power, 3.0); s.decimals = 1; return s; };

    add (fixed (P::choice (mode, "Mode", { "Stereo", "Mono" }, 0)));
    add (cents (shiftL, "Left Shift", 9.0));
    add (cents (shiftR, "Right Shift", -9.0));
    add (delay (delayL, "Left Delay", 0.0));
    add (delay (delayR, "Right Delay", 25.0));
    add (pct (feedbackL, "Left Feedback", -90.0, 90.0, 0.0));
    add (pct (feedbackR, "Right Feedback", -90.0, 90.0, 0.0));
    add (fixed (P::continuous (splice, "Splice", Unit::Milliseconds, 5.0, 100.0, 30.0)));
    add (pct (levelShiftL, "Left Shift Level", -100.0, 100.0, 100.0));
    add (pct (levelShiftR, "Right Shift Level", -100.0, 100.0, 100.0));
    add (pct (levelDryL, "Left Dry Tap Level", -100.0, 100.0, 0.0));
    add (pct (levelDryR, "Right Dry Tap Level", -100.0, 100.0, 0.0));
    add (pct (panShiftL, "Left Shift Pan", -100.0, 100.0, -100.0));
    add (pct (panShiftR, "Right Shift Pan", -100.0, 100.0, 100.0));
    add (pct (panDryL, "Left Dry Tap Pan", -100.0, 100.0, -100.0));
    add (pct (panDryR, "Right Dry Tap Pan", -100.0, 100.0, 100.0));
    add (pct (mod, "Mod", 0.0, 100.0, 0.0));
    add (P::continuous (modRate, "Mod Rate", Unit::Hertz, 0.05, 10.0, 0.5, Curve::Exponential));
    add (fixed (P::choice (modWave, "Mod Wave", { "Sine", "Random" }, 0)));
    return ok;
}

void MicrodecaleModule::bind (const ParameterSet& set)
{
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
    idxIn_ = set.indexOf (stock::inputTrim);
    idxOut_ = set.indexOf (stock::outputTrim);
    idxMix_ = set.indexOf (stock::mix);
}

void MicrodecaleModule::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    bufSize_ = static_cast<std::size_t> (std::ceil ((maxDelayMs + 10.0) * 0.001 * sampleRate_)) + 16;
    for (auto& v : voices_)
    {
        v.shifter.prepare (sampleRate_);
        v.delayBuf.assign (bufSize_, 0.0f);
        v.dryBuf.assign (bufSize_, 0.0f);
    }
    reset();
}

void MicrodecaleModule::reset() noexcept
{
    for (std::size_t s = 0; s < voices_.size(); ++s)
    {
        auto& v = voices_[s];
        v.shifter.reset();
        std::fill (v.delayBuf.begin(), v.delayBuf.end(), 0.0f);
        std::fill (v.dryBuf.begin(), v.dryBuf.end(), 0.0f);
        v.pos = 0;
        v.fbState = 0.0;
        v.lfoPhase = 0.25 * static_cast<double> (s);
        v.randFrom = v.randTo = 0.0;
        v.randPos = 1.0;
    }
    smoothInit_ = false;
    lastSplice_ = -1.0;
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
    for (auto& p : peakHold_) p = 0.0f;
}

void MicrodecaleModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    if (n <= 0 || bufSize_ == 0) return;
    for (std::size_t i = 0; i < idx_.size(); ++i)
        if (idx_[i] != ParameterSet::npos) values_[i] = params.getAt (idx_[i]);
    const double fs = sampleRate_;
    const bool monoMode = param (mode) >= 0.5, randomWave = param (modWave) >= 0.5;
    if (std::fabs (param (splice) - lastSplice_) > 1.0e-9)
    {
        for (auto& v : voices_) v.shifter.setSplice (param (splice));
        lastSplice_ = param (splice);
    }
    const double shiftT[2] = { param (shiftL), param (shiftR) };
    const double delayT[2] = { param (delayL) * 0.001 * fs, param (delayR) * 0.001 * fs };
    const double fbT[2] = { param (feedbackL) * 0.01, param (feedbackR) * 0.01 };
    // niveaux (0..3 : décalé G, décalé D, sec G, sec D) et panoramiques (4..7)
    const double gainT[8] = { param (levelShiftL) * 0.01, param (levelShiftR) * 0.01, param (levelDryL) * 0.01, param (levelDryR) * 0.01,
                              param (panShiftL) * 0.01, param (panShiftR) * 0.01, param (panDryL) * 0.01, param (panDryR) * 0.01 };
    if (! smoothInit_)
    {
        for (std::size_t s = 0; s < 2; ++s)
        {
            voices_[s].shiftSm = shiftT[s];
            voices_[s].delaySm = delayT[s];
            voices_[s].feedbackSm = fbT[s];
        }
        for (std::size_t i = 0; i < 8; ++i) gainSm_[i] = gainT[i];
        smoothInit_ = true;
    }
    const double kSm = coef (0.02, fs), kDelay = coef (0.05, fs);
    const double modCents = param (mod) * 0.01 * maxModCents, modStep = param (modRate) / fs;
    const double outGainMeter = dbToLin (idxOut_ == ParameterSet::npos ? 0.0 : params.getAt (idxOut_));
    const double inGainInv = 1.0 / std::max (1.0e-6, dbToLin (idxIn_ == ParameterSet::npos ? 0.0 : params.getAt (idxIn_)));
    const double mixMeter = idxMix_ == ParameterSet::npos ? 1.0 : std::clamp (params.getAt (idxMix_) * 0.01, 0.0, 1.0);
    const double maxD = static_cast<double> (bufSize_) - 8.0;

    for (int i = 0; i < n; ++i)
    {
        const double inL = left[i], inR = right[i];
        peakHold_[0] = std::max (peakHold_[0], static_cast<float> (std::fabs (inL)));
        peakHold_[1] = std::max (peakHold_[1], static_cast<float> (std::fabs (inR)));
        const double x[2] = { monoMode ? 0.5 * (inL + inR) : inL, monoMode ? 0.5 * (inL + inR) : inR };
        for (std::size_t g = 0; g < 8; ++g) gainSm_[g] += kSm * (gainT[g] - gainSm_[g]);
        double shifted[2], dryTap[2];
        for (std::size_t s = 0; s < 2; ++s)
        {
            auto& v = voices_[s];
            v.shiftSm += kSm * (shiftT[s] - v.shiftSm);
            v.delaySm += kDelay * (delayT[s] - v.delaySm);
            v.feedbackSm += kSm * (fbT[s] - v.feedbackSm);
            // modulation de hauteur : sinus (voie droite en quadrature) ou aléatoire lissé
            double lfo = 0.0;
            if (modCents > 0.0)
            {
                v.lfoPhase += modStep;
                v.lfoPhase -= std::floor (v.lfoPhase);
                if (! randomWave) lfo = std::sin (2.0 * kPi * v.lfoPhase);
                else
                {
                    v.randPos += modStep;
                    if (v.randPos >= 1.0)
                    {
                        v.randPos -= 1.0;
                        v.randFrom = v.randTo;
                        noise_ = noise_ * 1664525u + 1013904223u;
                        v.randTo = static_cast<double> (noise_ >> 8) / 8388608.0 - 1.0;
                    }
                    lfo = v.randFrom + (v.randTo - v.randFrom) * (0.5 - 0.5 * std::cos (kPi * v.randPos));
                }
            }
            const double ratio = std::exp2 ((v.shiftSm + modCents * lfo) / 1200.0);
            const double vin = x[s] + v.feedbackSm * v.fbState;                      // le feedback repasse par le décaleur
            const float sh = v.shifter.process (static_cast<float> (vin), ratio);
            const double d = std::clamp (v.delaySm, 0.0, maxD);
            v.delayBuf[v.pos] = sh;
            v.dryBuf[v.pos] = static_cast<float> (x[s]);
            // tampon lu 3 échantillons plus loin que le retard réglé (0,06 ms) pour que l'interpolation de Hermite ne lise jamais l'avenir
            const double outS = readHermite (v.delayBuf, v.pos + 1, d + 4.0);
            const double outD = readHermite (v.dryBuf, v.pos + 1, d + 4.0);
            v.pos = (v.pos + 1) % bufSize_;
            v.fbState = outS;
            shifted[s] = outS;
            dryTap[s] = outD;
        }
        // niveaux et panoramiques (balance à puissance constante)
        double outL = 0.0, outR = 0.0;
        const double src[4] = { shifted[0], shifted[1], dryTap[0], dryTap[1] };
        for (std::size_t k = 0; k < 4; ++k)
        {
            const double a = 0.25 * kPi * (gainSm_[4 + k] + 1.0);
            outL += src[k] * gainSm_[k] * std::cos (a);
            outR += src[k] * gainSm_[k] * std::sin (a);
        }
        left[i] = static_cast<float> (outL);
        right[i] = static_cast<float> (outR);
        const double fl = (1.0 - mixMeter) * inL * inGainInv + mixMeter * outL * outGainMeter, fr = (1.0 - mixMeter) * inR * inGainInv + mixMeter * outR * outGainMeter;
        peakHold_[2] = std::max (peakHold_[2], static_cast<float> (std::fabs (fl)));
        peakHold_[3] = std::max (peakHold_[3], static_cast<float> (std::fabs (fr)));
    }
    for (int c = 0; c < 4; ++c) { meters_[static_cast<std::size_t> (c)].store (peakHold_[c], std::memory_order_relaxed); peakHold_[c] = 0.0f; }
}
} // namespace neptune::core
