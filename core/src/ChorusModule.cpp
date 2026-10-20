#include "neptune/core/ChorusModule.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/DelayRead.h"

namespace neptune::core
{
using namespace chorus;

namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double dbToLin (double db) noexcept { return std::exp (db * 0.11512925464970229); }
inline double coef (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * fs)); }
// Triangle de -1 à +1 sur un cycle (phase en cycles) : -1 en 0, +1 en 0,5.
inline double triangle (double phase) noexcept
{
    phase -= std::floor (phase);
    return 1.0 - 4.0 * std::fabs (phase - 0.5);
}
constexpr double kTalCutoffHz = 2183.0;   // passe-bas à un pôle du mode TAL (un pôle de 0,75 à 48 kHz dans le chorus d'origine), ramené en Hz
constexpr double kTalDcPole = 0.995;
constexpr double kTalWetGain = 1.4;
} // namespace

ChorusModule::ChorusModule()
{
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

bool ChorusModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto fixed = [] (P s) { s.automatable = false; return s; };
    auto pct = [] (ParamId id, const char* n, double lo, double hi, double def) { return P::continuous (id, n, Unit::Percent, lo, hi, def); };
    const std::vector<std::string> slopes { "6 dB/oct", "12 dB/oct", "24 dB/oct" };

    add (fixed (P::choice (mode, "Mode", { "Standard", "TAL" }, 0)));
    add (P::continuous (rate, "Rate", Unit::Hertz, 0.05, 10.0, 0.5, Curve::Exponential));
    add (P::toggle (sync, "Sync"));
    add (fixed (P::choice (syncDiv, "Sync Division", std::vector<std::string> (divisionNames, divisionNames + kDivisions), 1)));
    add (pct (depth, "Depth", 0.0, 100.0, 50.0));
    add (P::continuous (delayMs, "Delay", Unit::Milliseconds, 0.5, 30.0, 7.0, Curve::Exponential));
    add (fixed (P::choice (lfoShape, "LFO Shape", { "Triangle", "Sine" }, 0)));
    add (fixed (P::choice (voices, "Voices", { "1", "2", "3", "4" }, 0)));
    add (pct (width, "Width", 0.0, 100.0, 100.0));
    add (pct (feedback, "Feedback", -90.0, 90.0, 0.0));
    add (P::continuous (lowCut, "Low Cut", Unit::Hertz, 20.0, 1000.0, 20.0, Curve::Exponential));
    add (fixed (P::choice (lowCutSlope, "Low Cut Slope", slopes, 1)));
    add (P::toggle (lowCutOn, "Low Cut On"));
    add (P::continuous (highCut, "High Cut", Unit::Hertz, 1000.0, 20000.0, 20000.0, Curve::Exponential));
    add (fixed (P::choice (highCutSlope, "High Cut Slope", slopes, 1)));
    add (P::toggle (highCutOn, "High Cut On"));
    add (pct (bbd, "BBD", 0.0, 100.0, 0.0));
    add (P::toggle (bbdOn, "BBD On", true));
    add (fixed (P::choice (interp, "Interpolation", { "Linear", "Cubic" }, 1)));
    add (fixed (P::choice (lfoStart, "LFO Start", { "Free", "Each Play" }, 0)));
    add (P::toggle (talI, "TAL Chorus I"));
    add (P::toggle (talII, "TAL Chorus II"));
    add (P::continuous (talRate1, "TAL Rate I", Unit::Hertz, 0.1, 10.0, 0.5, Curve::Exponential));
    add (P::continuous (talRate2, "TAL Rate II", Unit::Hertz, 0.1, 10.0, 0.83, Curve::Exponential));
    return ok;
}

void ChorusModule::bind (const ParameterSet& set)
{
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
    idxIn_ = set.indexOf (stock::inputTrim);
    idxOut_ = set.indexOf (stock::outputTrim);
    idxMix_ = set.indexOf (stock::mix);
}

void ChorusModule::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    bufSize_ = static_cast<std::size_t> (std::ceil (0.07 * sampleRate_)) + 16;   // 2 x 30 ms au plus, plus une marge
    for (auto& ch : voices_)
        for (auto& v : ch) v.buf.assign (bufSize_, 0.0f);
    for (auto& u : tal_)
        for (auto& l : u) l.buf.assign (bufSize_, 0.0f);
    reset();
}

void ChorusModule::resetPhases() noexcept
{
    phase_ = 0.0;
    talPhase_[0] = talPhase_[1] = 0.0;
}

void ChorusModule::reset() noexcept
{
    for (auto& ch : voices_)
        for (auto& v : ch)
        {
            std::fill (v.buf.begin(), v.buf.end(), 0.0f);
            v.pos = 0;
            v.bbd.reset();
            v.hp.reset();
            v.lp.reset();
            v.lastOut = 0.0;
        }
    for (auto& u : tal_)
        for (auto& l : u)
        {
            std::fill (l.buf.begin(), l.buf.end(), 0.0f);
            l.pos = 0;
            l.lp = l.dcIn = l.dcOut = 0.0;
        }
    resetPhases();
    for (auto& d : designed_) d = -1.0;
    tal1On_ = tal2On_ = 0.0;
    rateSm_ = depthSm_ = delaySm_ = widthSm_ = fbSm_ = bbdSm_ = 0.0;
    smoothInit_ = false;
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
    for (auto& p : peakHold_) p = 0.0f;
}

// Coefficients des filtres de la voie traitée (recalculés seulement quand un réglage a bougé).
void ChorusModule::designFilters (double fs) noexcept
{
    const bool lowOn = param (lowCutOn) >= 0.5 && param (lowCut) > 20.5, highOn = param (highCutOn) >= 0.5 && param (highCut) < 19990.0;
    const double lowF = lowOn ? param (lowCut) : 0.0, highF = highOn ? param (highCut) : 0.0;
    const int lowSlope = 6 << std::clamp (static_cast<int> (std::lround (param (lowCutSlope))), 0, 2), highSlope = 6 << std::clamp (static_cast<int> (std::lround (param (highCutSlope))), 0, 2);
    const double bbdAmount = param (bbdOn) >= 0.5 ? param (bbd) * 0.01 : 0.0;
    const double bbdF = bbdAmount > 0.0 ? std::min (20000.0 * std::pow (0.3, bbdAmount), 0.45 * fs) : 0.0;
    const double now[6] = { lowF, static_cast<double> (lowSlope), highF, static_cast<double> (highSlope), bbdF, fs };
    bool same = true;
    for (int i = 0; i < 6; ++i) same = same && std::fabs (now[i] - designed_[i]) < 1.0e-9;
    if (same) return;
    for (int i = 0; i < 6; ++i) designed_[i] = now[i];
    for (auto& ch : voices_)
        for (auto& v : ch)
        {
            if (lowOn) v.hp.design (true, fs, lowF, lowSlope);
            if (highOn) v.lp.design (false, fs, std::min (highF, 0.45 * fs), highSlope);
            if (bbdF > 0.0) v.bbd.set (BiquadCoefs::lowpass (fs, bbdF, 0.70710678));
        }
}

void ChorusModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    if (n <= 0 || bufSize_ == 0) return;
    for (std::size_t i = 0; i < idx_.size(); ++i)
        if (idx_[i] != ParameterSet::npos) values_[i] = params.getAt (idx_[i]);
    const double fs = sampleRate_;
    double bpm = tempo_.load (std::memory_order_relaxed);
    if (! (bpm > 1.0)) bpm = 120.0;
    const bool playing = playing_.load (std::memory_order_relaxed);
    if (param (lfoStart) >= 0.5 && playing && ! wasPlaying_) resetPhases();
    wasPlaying_ = playing;

    const bool talMode = param (mode) >= 0.5;
    const bool cubic = param (interp) >= 0.5, triangleShape = param (lfoShape) < 0.5;
    const int nv = std::clamp (static_cast<int> (std::lround (param (voices))) + 1, 1, kMaxVoices);   // choix 0..3
    const double rateTarget = param (sync) >= 0.5 ? bpm / (60.0 * divisionBeats (static_cast<int> (std::lround (param (syncDiv))))) : param (rate);
    const double depthTarget = param (depth) * 0.01, delayTarget = param (delayMs), widthTarget = param (width) * 0.01, fbTarget = param (feedback) * 0.01;
    const double bbdTarget = param (bbdOn) >= 0.5 ? param (bbd) * 0.01 : 0.0;
    const bool lowActive = param (lowCutOn) >= 0.5 && param (lowCut) > 20.5, highActive = param (highCutOn) >= 0.5 && param (highCut) < 19990.0;
    designFilters (fs);
    if (! smoothInit_)
    {
        rateSm_ = rateTarget; depthSm_ = depthTarget; delaySm_ = delayTarget; widthSm_ = widthTarget; fbSm_ = fbTarget; bbdSm_ = bbdTarget;
        smoothInit_ = true;
    }
    const double kSm = coef (0.02, fs), kDelay = coef (0.05, fs), kOn = coef (0.01, fs);
    const double talRate[2] = { param (talRate1), param (talRate2) };
    const double talTarget[2] = { param (talI) >= 0.5 ? 1.0 : 0.0, param (talII) >= 0.5 ? 1.0 : 0.0 };
    const double talP = std::exp (-2.0 * kPi * kTalCutoffHz / fs);
    const double outGainMeter = dbToLin (idxOut_ == ParameterSet::npos ? 0.0 : params.getAt (idxOut_));
    const double inGainInv = 1.0 / std::max (1.0e-6, dbToLin (idxIn_ == ParameterSet::npos ? 0.0 : params.getAt (idxIn_)));
    const double mixMeter = idxMix_ == ParameterSet::npos ? 1.0 : std::clamp (params.getAt (idxMix_) * 0.01, 0.0, 1.0);
    const double maxD = static_cast<double> (bufSize_) - 8.0;
    float lfoShown = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const double in[2] = { left[i], right[i] };
        peakHold_[0] = std::max (peakHold_[0], static_cast<float> (std::fabs (in[0])));
        peakHold_[1] = std::max (peakHold_[1], static_cast<float> (std::fabs (in[1])));
        double out[2];
        if (! talMode)
        {
            rateSm_ += kSm * (rateTarget - rateSm_);
            depthSm_ += kSm * (depthTarget - depthSm_);
            delaySm_ += kDelay * (delayTarget - delaySm_);
            widthSm_ += kSm * (widthTarget - widthSm_);
            fbSm_ += kSm * (fbTarget - fbSm_);
            bbdSm_ += kSm * (bbdTarget - bbdSm_);
            phase_ += rateSm_ / fs;
            phase_ -= std::floor (phase_);
            const double satGain = 1.0 + 0.6 * bbdSm_, noiseAmp = 1.7783e-4 * bbdSm_;   // saturation douce et bruit (-75 dBFS à 100 %)
            for (int c = 0; c < 2; ++c)
            {
                double acc = 0.0;
                for (int v = 0; v < nv; ++v)
                {
                    auto& vo = voices_[static_cast<std::size_t> (c)][static_cast<std::size_t> (v)];
                    double ph = phase_ + (c == 1 ? widthSm_ * 0.5 : 0.0) + static_cast<double> (v) / nv;
                    ph -= std::floor (ph);
                    const double lfo = triangleShape ? triangle (ph) : std::sin (2.0 * kPi * ph);
                    if (c == 0 && v == 0) lfoShown = static_cast<float> (ph);
                    const double tauMs = std::max (0.05, delaySm_ * (1.0 + depthSm_ * lfo));
                    const double d = std::clamp (tauMs * 0.001 * fs, 3.0, maxD);
                    double y = cubic ? readHermite (vo.buf, vo.pos, d) : readLinear (vo.buf, vo.pos, d);
                    if (bbdSm_ > 1.0e-4)
                    {
                        y = vo.bbd.processD (y);
                        y = std::tanh (satGain * y) / satGain;
                        noise_ = noise_ * 1664525u + 1013904223u;
                        y += noiseAmp * (static_cast<double> (noise_ >> 8) / 8388608.0 - 1.0);
                    }
                    if (lowActive) y = vo.hp.process (static_cast<float> (y));
                    if (highActive) y = vo.lp.process (static_cast<float> (y));
                    vo.lastOut = y;
                    vo.buf[vo.pos] = static_cast<float> (in[c] + fbSm_ * y);
                    vo.pos = (vo.pos + 1) % bufSize_;
                    acc += y;
                }
                out[c] = acc / nv;
            }
        }
        else
        {
            // mode TAL : deux unités (I et II), chacune une ligne par canal ; LFO triangulaire, gauche et droite en opposition de phase
            double wet[2] = { 0.0, 0.0 };
            for (int u = 0; u < 2; ++u)
            {
                talPhase_[u] += talRate[u] / fs;
                talPhase_[u] -= std::floor (talPhase_[u]);
                double& gain = u == 0 ? tal1On_ : tal2On_;
                gain += kOn * (talTarget[u] - gain);
                for (int c = 0; c < 2; ++c)
                {
                    auto& line = tal_[static_cast<std::size_t> (u)][static_cast<std::size_t> (c)];
                    const double offset = (u == 0) == (c == 0) ? 0.5 : 0.0;   // I : gauche à +1, droite à -1 ; II : l'inverse
                    const double lfo = triangle (talPhase_[u] + offset);
                    const double d = std::clamp ((talCentreMs + talSwingMs * lfo) * 0.001 * fs, 3.0, maxD);
                    double y = cubic ? readHermite (line.buf, line.pos, d) : readLinear (line.buf, line.pos, d);
                    line.lp = (1.0 - talP) * y + talP * line.lp;
                    const double dc = line.lp - line.dcIn + kTalDcPole * line.dcOut;
                    line.dcIn = line.lp;
                    line.dcOut = dc;
                    line.buf[line.pos] = static_cast<float> (in[c]);
                    line.pos = (line.pos + 1) % bufSize_;
                    wet[c] += gain * dc;
                }
            }
            lfoShown = static_cast<float> (talPhase_[0] + 0.5 - std::floor (talPhase_[0] + 0.5));
            out[0] = in[0] + kTalWetGain * wet[0];
            out[1] = in[1] + kTalWetGain * wet[1];
        }
        left[i] = static_cast<float> (out[0]);
        right[i] = static_cast<float> (out[1]);
        // mesure de sortie : ce qui sortira du ModuleRunner (sec + traité, mix et gain de sortie compris)
        const double fl = (1.0 - mixMeter) * in[0] * inGainInv + mixMeter * out[0] * outGainMeter, fr = (1.0 - mixMeter) * in[1] * inGainInv + mixMeter * out[1] * outGainMeter;
        peakHold_[2] = std::max (peakHold_[2], static_cast<float> (std::fabs (fl)));
        peakHold_[3] = std::max (peakHold_[3], static_cast<float> (std::fabs (fr)));
    }
    for (int c = 0; c < 4; ++c) { meters_[static_cast<std::size_t> (c)].store (peakHold_[c], std::memory_order_relaxed); peakHold_[c] = 0.0f; }
    meters_[kLfoPhase].store (lfoShown, std::memory_order_relaxed);
}
} // namespace neptune::core
