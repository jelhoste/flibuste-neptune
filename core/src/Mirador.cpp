#include "neptune/core/Mirador.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace neptune::core::mirador
{
namespace
{
constexpr const char* kTargetNames[kNumTargets] = { "-23 LUFS (EBU R128)", "-24 LUFS (ATSC A/85)", "-16 LUFS (Podcast)", "-14 LUFS (Streaming)", "Custom" };
constexpr double kTargetValues[kNumTargets] = { -23.0, -24.0, -16.0, -14.0, -23.0 };
constexpr double kRmsSeconds[kNumRmsWindows] = { 0.3, 1.0, 3.0 };

float toDb (double linear) noexcept
{
    return linear > 1.0e-9 ? static_cast<float> (20.0 * std::log10 (linear)) : kNoValue;
}
float meanSquareToDb (double ms) noexcept
{
    return ms > 1.0e-18 ? static_cast<float> (10.0 * std::log10 (ms)) : kNoValue;
}
} // namespace

bool addParams (ParameterSet& set, std::string* error)
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };

    std::vector<std::string> targets;
    for (const char* n : kTargetNames) targets.emplace_back (n);
    add (P::choice (target, "Loudness Target", targets, kTargetEbu));
    add (P::continuous (customTarget, "Custom Target", Unit::None, -40.0, 0.0, -23.0));
    {
        auto t = P::continuous (tolerance, "Tolerance", Unit::None, 0.1, 3.0, 1.0);
        t.decimals = 1;
        add (std::move (t));
    }
    add (P::stepped (historyMinutes, "History Length", Unit::None, 1, 60, 1));
    add (P::continuous (peakHold, "Peak Hold", Unit::Seconds, 0.0, 10.0, 2.0));
    add (P::continuous (peakFall, "Peak Fall", Unit::None, 5.0, 60.0, 20.0));
    add (P::choice (rmsWindow, "RMS Window", { "300 ms", "1 s", "3 s" }, kRms300ms));
    add (P::toggle (autoReset, "Reset On Play", true));
    add (P::choice (meterMode, "Meter", { "Peak", "Momentary", "Short-term", "Integrated" }, kMeterShortTerm));
    return ok;
}

const char* targetName (int t) noexcept
{
    return t >= 0 && t < kNumTargets ? kTargetNames[t] : "";
}

double targetLufs (int t, double custom) noexcept
{
    if (t == kTargetCustom) return custom;
    return t >= 0 && t < kNumTargets ? kTargetValues[t] : kTargetValues[kTargetEbu];
}

float plr (const LoudnessMeter::Reading& r) noexcept
{
    if (r.truePeakDb <= -150.0f || r.integrated <= -150.0f) return kNoValue;
    return r.truePeakDb - r.integrated;
}

float psr (const LoudnessMeter::Reading& r) noexcept
{
    if (r.truePeakDb <= -150.0f || r.shortTerm <= -150.0f) return kNoValue;
    return r.truePeakDb - r.shortTerm;
}

TargetState targetState (float integratedLufs, double targetLufsValue, double toleranceLu) noexcept
{
    if (integratedLufs <= -150.0f) return TargetState::Unknown;
    const double d = static_cast<double> (integratedLufs) - targetLufsValue;
    if (d > toleranceLu) return TargetState::Over;
    if (d < -toleranceLu) return TargetState::Under;
    return TargetState::OnTarget;
}

// ---- LoudnessHistory
void LoudnessHistory::setCapacity (std::size_t capacity)
{
    capacity = std::max<std::size_t> (capacity, 1);
    if (capacity == data_.size()) return;
    std::vector<Point> next (capacity);
    const std::size_t keep = std::min (count_, capacity);
    for (std::size_t i = 0; i < keep; ++i) next[i] = at (count_ - keep + i);
    data_ = std::move (next);
    count_ = keep;
    head_ = keep % data_.size();
}

void LoudnessHistory::add (float momentary, float shortTerm) noexcept
{
    data_[head_] = Point { momentary, shortTerm };
    head_ = (head_ + 1) % data_.size();
    count_ = std::min (count_ + 1, data_.size());
}

LoudnessHistory::Point LoudnessHistory::at (std::size_t i) const noexcept
{
    if (i >= count_) return Point {};
    const std::size_t oldest = (head_ + data_.size() - count_) % data_.size();
    return data_[(oldest + i) % data_.size()];
}

// ---- MiradorEngine
MiradorEngine::MiradorEngine()
{
    std::string error;
    ok_ = addParams (params_, &error);
    if (ok_)
    {
        auto ix = [this] (ParamId id) { return params_.indexOf (id); };
        iPeakHold_ = ix (peakHold); iPeakFall_ = ix (peakFall);
        iRmsWindow_ = ix (rmsWindow); iAutoReset_ = ix (autoReset);
    }
}

void MiradorEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    ring_.setSampleRate (sr_);
    ring_.setMaxBlock (std::max (1, maxBlockSize));
    loudness_.prepare (sr_);
    reset();
}

void MiradorEngine::reset() noexcept
{
    msL_ = msR_ = sLR_ = sMid_ = sSide_ = 0.0;
    envL_ = envR_ = holdL_ = holdR_ = 0.0;
    holdLeftS_ = holdRightS_ = 0.0;
    winPeak_ = prevWinPeak_ = 0.0;
    winLeft_ = 0.0;
    wasPlaying_ = false;
    peakL_.store (kNoValue); peakR_.store (kNoValue); holdLPub_.store (kNoValue); holdRPub_.store (kNoValue);
    rmsLPub_.store (kNoValue); rmsRPub_.store (kNoValue);
    crest_.store (0.0f); correlation_.store (0.0f); balance_.store (0.0f); width_.store (0.0f);
    clipL_.store (false); clipR_.store (false);
}

void MiradorEngine::process (const float* left, const float* right, int n, bool playing) noexcept
{
    if (left == nullptr || n <= 0) return;
    if (right == nullptr) right = left;
    processed_.fetch_add (static_cast<std::uint64_t> (n), std::memory_order_relaxed);

    if (resetRequested_.exchange (false, std::memory_order_acq_rel)) reset();
    if (playing && ! wasPlaying_ && p (iAutoReset_) > 0.5) loudness_.reset();
    wasPlaying_ = playing;

    loudness_.push (left, right, n);
    if (ring_.active())
    {
        const float* in[2] = { left, right };
        ring_.push (in, n);
    }

    const double window = kRmsSeconds[std::clamp (static_cast<int> (p (iRmsWindow_) + 0.5), 0, kNumRmsWindows - 1)];
    const double coef = 1.0 - std::exp (-1.0 / (sr_ * window));
    const double fall = std::pow (10.0, -p (iPeakFall_) * n / sr_ / 20.0);   // retombée de la crête sur ce bloc
    const double holdSamples = p (iPeakHold_) * sr_;

    double peakBlockL = 0.0, peakBlockR = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double l = left[i], r = right[i];
        const double al = std::fabs (l), ar = std::fabs (r);
        peakBlockL = std::max (peakBlockL, al);
        peakBlockR = std::max (peakBlockR, ar);
        msL_ += coef * (l * l - msL_);
        msR_ += coef * (r * r - msR_);
        sLR_ += coef * (l * r - sLR_);
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
        sMid_ += coef * (mid * mid - sMid_);
        sSide_ += coef * (side * side - sSide_);
    }
    if (peakBlockL >= 1.0) clipL_.store (true, std::memory_order_relaxed);
    if (peakBlockR >= 1.0) clipR_.store (true, std::memory_order_relaxed);

    // crête avec retombée, puis maintien
    envL_ = std::max (peakBlockL, envL_ * fall);
    envR_ = std::max (peakBlockR, envR_ * fall);
    auto holdStep = [&] (double& hold, double& remain, double block, double env)
    {
        if (block >= hold) { hold = block; remain = holdSamples; }
        else if (remain > 0.0) remain -= n;
        else hold = env;
    };
    holdStep (holdL_, holdLeftS_, peakBlockL, envL_);
    holdStep (holdR_, holdRightS_, peakBlockR, envR_);

    // facteur de crête : crête la plus forte sur la fenêtre en cours et la précédente, moins le RMS des deux voies
    winPeak_ = std::max ({ winPeak_, peakBlockL, peakBlockR });
    winLeft_ -= n;
    if (winLeft_ <= 0.0)
    {
        prevWinPeak_ = winPeak_;
        winPeak_ = 0.0;
        winLeft_ = window * sr_;
    }

    peakL_.store (toDb (envL_), std::memory_order_relaxed);
    peakR_.store (toDb (envR_), std::memory_order_relaxed);
    holdLPub_.store (toDb (holdL_), std::memory_order_relaxed);
    holdRPub_.store (toDb (holdR_), std::memory_order_relaxed);
    rmsLPub_.store (meanSquareToDb (msL_), std::memory_order_relaxed);
    rmsRPub_.store (meanSquareToDb (msR_), std::memory_order_relaxed);

    const double both = 0.5 * (msL_ + msR_);
    const float peakDb = toDb (std::max (winPeak_, prevWinPeak_)), rmsDb = meanSquareToDb (both);
    crest_.store (peakDb > kNoValue && rmsDb > kNoValue ? peakDb - rmsDb : 0.0f, std::memory_order_relaxed);

    const double den = std::sqrt (msL_ * msR_);
    correlation_.store (den > 1.0e-18 ? static_cast<float> (std::clamp (sLR_ / den, -1.0, 1.0)) : 0.0f, std::memory_order_relaxed);
    const double sum = msL_ + msR_;
    balance_.store (sum > 1.0e-18 ? static_cast<float> ((msR_ - msL_) / sum) : 0.0f, std::memory_order_relaxed);
    const double ms = sMid_ + sSide_;
    width_.store (ms > 1.0e-18 ? static_cast<float> (std::sqrt (sSide_ / ms)) : 0.0f, std::memory_order_relaxed);
}

Levels MiradorEngine::levels() const noexcept
{
    Levels v;
    v.peakL = peakL_.load (std::memory_order_relaxed);
    v.peakR = peakR_.load (std::memory_order_relaxed);
    v.holdL = holdLPub_.load (std::memory_order_relaxed);
    v.holdR = holdRPub_.load (std::memory_order_relaxed);
    v.rmsL = rmsLPub_.load (std::memory_order_relaxed);
    v.rmsR = rmsRPub_.load (std::memory_order_relaxed);
    v.crest = crest_.load (std::memory_order_relaxed);
    v.correlation = correlation_.load (std::memory_order_relaxed);
    v.balance = balance_.load (std::memory_order_relaxed);
    v.width = width_.load (std::memory_order_relaxed);
    v.clipL = clipL_.load (std::memory_order_relaxed);
    v.clipR = clipR_.load (std::memory_order_relaxed);
    return v;
}
} // namespace neptune::core::mirador
