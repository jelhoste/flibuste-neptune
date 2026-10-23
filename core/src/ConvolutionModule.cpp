#include "neptune/core/ConvolutionModule.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "neptune/core/conv/processor.hpp"
#include "neptune/core/conv/wav_loader.hpp"

namespace neptune::core
{
using namespace convolution;

namespace
{
constexpr int kSlopes[3] = { 12, 24, 48 };
}

ConvolutionModule::ConvolutionModule()
{
    idx_.fill (ParameterSet::npos);
    last_.fill (0.0);
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
    worker_ = std::thread ([this] { workerLoop(); });
}

ConvolutionModule::~ConvolutionModule()
{
    quit_.store (true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}

bool ConvolutionModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto fixed = [] (P s) { s.automatable = false; return s; };
    auto toggle = [] (ParamId id, const char* n, bool def) { return P::toggle (id, n, def); };
    const std::vector<std::string> types { "Bell", "Low shelf", "High shelf" }, slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct" };

    add (P::continuous (width, "Width", Unit::Percent, 0.0, 200.0, 100.0));
    add (P::continuous (preDelay, "Pre-delay", Unit::Milliseconds, 0.0, 2000.0, 0.0, Curve::Power, 3.0));
    add (toggle (eqOn, "EQ On", true));
    const double freqs[3] = { 200.0, 1000.0, 4000.0 };
    for (int b = 0; b < 3; ++b)
    {
        const std::string n = "EQ" + std::to_string (b + 1) + " ";
        add (toggle (bandParam (b, kOn), (n + "On").c_str(), false));
        add (P::choice (bandParam (b, kType), n + "Type", types, 0));
        add (P::continuous (bandParam (b, kFreq), n + "Freq", Unit::Hertz, 20.0, 20000.0, freqs[b], Curve::Exponential));
        add (P::continuous (bandParam (b, kGain), n + "Gain", Unit::Decibel, -18.0, 18.0, 0.0));
        add (P::continuous (bandParam (b, kQ), n + "Q", Unit::None, 0.1, 10.0, 1.0, Curve::Exponential));
    }
    add (toggle (hpOn, "HP On", false));
    add (P::continuous (hpFreq, "HP Freq", Unit::Hertz, 20.0, 1000.0, 80.0, Curve::Exponential));
    add (P::choice (hpSlope, "HP Slope", slopes, 1));
    add (toggle (lpOn, "LP On", false));
    add (P::continuous (lpFreq, "LP Freq", Unit::Hertz, 1000.0, 20000.0, 6000.0, Curve::Exponential));
    add (P::choice (lpSlope, "LP Slope", slopes, 1));

    add (fixed (P::choice (inputMode, "Input Mode", { "Stereo", "Sum (L+R)", "Left", "Right" }, 1)));
    add (fixed (P::continuous (stretch, "IR Stretch", Unit::Percent, 50.0, 200.0, 100.0)));
    add (fixed (toggle (autoTrim, "IR Auto-trim", true)));
    add (fixed (P::continuous (begin, "IR Begin", Unit::Milliseconds, 0.0, 500.0, 0.0)));
    add (fixed (P::continuous (end, "IR End", Unit::Milliseconds, 0.0, 10000.0, 0.0)));
    add (fixed (toggle (autoLen, "IR Auto-length", false)));
    add (fixed (P::continuous (attack, "IR Attack", Unit::Milliseconds, 0.0, 500.0, 0.0)));
    add (fixed (P::continuous (decay, "IR Decay", Unit::Decibel, 0.0, 60.0, 0.0)));
    add (fixed (toggle (reverse, "IR Reverse", false)));
    add (fixed (P::choice (norm, "IR Normalise", { "Off", "Peak", "Energy" }, 2)));
    add (fixed (P::continuous (meterRmsMs, "Meter RMS Window", Unit::Milliseconds, 10.0, 3000.0, 300.0)));
    add (fixed (P::continuous (meterPeakDecay, "Meter Peak Decay", Unit::None, 5.0, 300.0, 24.0)));
    return ok;
}

void ConvolutionModule::bind (const ParameterSet& set)
{
    params_ = &set;
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
    irStamp_.fetch_add (1, std::memory_order_relaxed);   // les valeurs par défaut (ou restaurées ensuite) fixent l'IR à construire
}

void ConvolutionModule::prepare (double sampleRate, int)
{
    sr_.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    proc_.prepare (sr_.load());
    haveLast_ = false;
    irStamp_.fetch_add (1, std::memory_order_relaxed);   // l'IR doit être refaite à la nouvelle cadence
}

void ConvolutionModule::reset() noexcept
{
    proc_.reset();
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

void ConvolutionModule::pushProcessorParams (const std::array<double, numParams>& v, bool snap) noexcept
{
    auto at = [&v] (ParamId id) { return v[static_cast<std::size_t> (id - 100)]; };
    llc::ProcessorParams q;
    q.inputGainDb = 0.0;
    q.outputGainDb = 0.0;
    q.dryDb = -120.0;   // le sec est celui du ModuleRunner (mélange commun) : ici, la voie traitée seule
    q.wetDb = 0.0;
    q.preDelayMs = at (preDelay);
    q.width = at (width) / 100.0;
    q.eqEnabled = at (eqOn) >= 0.5;
    for (int b = 0; b < 3; ++b)
    {
        auto& band = q.eq.band[b];
        band.enabled = at (bandParam (b, kOn)) >= 0.5;
        band.type = static_cast<llc::BandType> (std::clamp (static_cast<int> (at (bandParam (b, kType)) + 0.5), 0, 2));
        band.freq = at (bandParam (b, kFreq));
        band.gainDb = at (bandParam (b, kGain));
        band.q = at (bandParam (b, kQ));
    }
    q.eq.hp = { at (hpOn) >= 0.5, at (hpFreq), kSlopes[std::clamp (static_cast<int> (at (hpSlope) + 0.5), 0, 2)] };
    q.eq.lp = { at (lpOn) >= 0.5, at (lpFreq), kSlopes[std::clamp (static_cast<int> (at (lpSlope) + 0.5), 0, 2)] };
    proc_.setParams (q, snap);
}

void ConvolutionModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    if (n <= 0) return;
    std::array<double, numParams> v;
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = idx_[i] != ParameterSet::npos ? params.getAt (idx_[i]) : 0.0;
    if (! haveLast_ || v != last_)
    {
        const bool snap = ! haveLast_;
        pushProcessorParams (v, snap);
        auto changed = [&] (ParamId id) { return snap || v[static_cast<std::size_t> (id - 100)] != last_[static_cast<std::size_t> (id - 100)]; };
        if (changed (meterRmsMs)) proc_.setMeterRmsMs (v[meterRmsMs - 100]);
        if (changed (meterPeakDecay)) proc_.setMeterPeakDecay (v[meterPeakDecay - 100]);
        bool irChanged = false;
        for (ParamId id = inputMode; id <= norm; ++id) irChanged = irChanged || (! snap && changed (id));
        if (irChanged) irStamp_.fetch_add (1, std::memory_order_relaxed);
        last_ = v;
        haveLast_ = true;
    }
    if (resetPeak_.exchange (false, std::memory_order_acq_rel)) proc_.resetMeterPeakMax();
    proc_.process (left, right, left, right, n);
    const auto f = proc_.takeMeterFrame();
    for (int i = 0; i < 4; ++i)
    {
        meters_[static_cast<std::size_t> (kPeakInL + i)].store (f.peak[i], std::memory_order_relaxed);
        meters_[static_cast<std::size_t> (kPeakMaxInL + i)].store (f.peakMax[i], std::memory_order_relaxed);
        meters_[static_cast<std::size_t> (kRmsInL + i)].store (f.rms[i], std::memory_order_relaxed);
    }
    meters_[kLufsMIn].store (f.lufsM[0], std::memory_order_relaxed);
    meters_[kLufsMOut].store (f.lufsM[1], std::memory_order_relaxed);
    meters_[kLufsSIn].store (f.lufsS[0], std::memory_order_relaxed);
    meters_[kLufsSOut].store (f.lufsS[1], std::memory_order_relaxed);
}

void ConvolutionModule::setIrPath (const std::string& path)
{
    {
        const std::lock_guard<std::mutex> lock (pathMutex_);
        if (irPath_ == path) return;
        irPath_ = path;
    }
    irStamp_.fetch_add (1, std::memory_order_relaxed);
}

std::string ConvolutionModule::irPath() const
{
    const std::lock_guard<std::mutex> lock (pathMutex_);
    return irPath_;
}

std::shared_ptr<const llc::IRBuffer> ConvolutionModule::rawIr() const
{
    const std::lock_guard<std::mutex> lock (rawMutex_);
    return raw_;
}

llc::IROptions ConvolutionModule::irOptionsFrom (const std::array<double, numParams>& v) const
{
    auto at = [&v] (ParamId id) { return v[static_cast<std::size_t> (id - 100)]; };
    llc::IROptions o;
    o.targetSampleRate = sr_.load();
    o.stretch = at (stretch) / 100.0;
    o.autoTrimStart = at (autoTrim) >= 0.5;
    o.beginMs = at (begin);
    o.endMs = at (end);
    o.autoLength = at (autoLen) >= 0.5;
    o.attackMs = at (attack);
    o.decayDb = at (decay);
    o.reverse = at (reverse) >= 0.5;
    o.norm = static_cast<llc::NormMode> (std::clamp (static_cast<int> (at (norm) + 0.5), 0, 2));
    return o;
}

llc::IROptions ConvolutionModule::irOptions() const
{
    std::array<double, numParams> v;
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = value (static_cast<ParamId> (100 + i));
    return irOptionsFrom (v);
}

llc::InputMode ConvolutionModule::inputModeValue() const
{
    return static_cast<llc::InputMode> (std::clamp (static_cast<int> (value (inputMode) + 0.5), 0, 3));
}

// Thread de chargement : débounce, lecture du WAV, préparation de l'IR, construction du moteur. Si une nouvelle demande arrive pendant un chargement, le résultat
// périmé est abandonné (jamais installé) ; en cas d'erreur l'IR est déchargée (le module repasse en transparent) pour rester cohérent avec le chemin sauvegardé.
void ConvolutionModule::workerLoop()
{
    using namespace std::chrono_literals;
    std::uint32_t built = 0xFFFFFFFFu;
    llc::IRBuffer raw;
    std::string rawPath;
    auto stale = [this] (std::uint32_t stamp) { return irStamp_.load() != stamp; };
    while (! quit_.load (std::memory_order_acquire))
    {
        std::this_thread::sleep_for (20ms);
        proc_.collectRetired();
        const std::uint32_t stamp = irStamp_.load();
        if (stamp == built || params_ == nullptr) continue;
        std::this_thread::sleep_for (60ms);   // débounce des réglages
        if (stale (stamp)) continue;
        built = stamp;

        const auto path = irPath();
        if (path.empty())
        {
            rawPath.clear();
            proc_.unloadIR();
            { const std::lock_guard<std::mutex> lock (rawMutex_); raw_.reset(); }
            setStatus (kNoIr, 0.0);
            irVersion_.fetch_add (1, std::memory_order_relaxed);
            continue;
        }
        setStatus (kLoading, 0.0);
        if (path != rawPath)
        {
            const bool ok = llc::loadWavFile (path, raw) == llc::WavStatus::Ok;
            if (stale (stamp)) continue;
            if (! ok)
            {
                rawPath.clear();
                proc_.unloadIR();
                { const std::lock_guard<std::mutex> lock (rawMutex_); raw_.reset(); }
                setStatus (kError, 0.0);
                irVersion_.fetch_add (1, std::memory_order_relaxed);
                continue;
            }
            rawPath = path;
            { const std::lock_guard<std::mutex> lock (rawMutex_); raw_ = std::make_shared<const llc::IRBuffer> (raw); }
        }
        std::array<double, numParams> v;
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = value (static_cast<ParamId> (100 + i));
        const auto options = irOptionsFrom (v);
        llc::IRBuffer ir;
        if (! llc::processIR (raw, options, ir))
        {
            if (! stale (stamp)) { proc_.unloadIR(); setStatus (kError, 0.0); }
            irVersion_.fetch_add (1, std::memory_order_relaxed);
            continue;
        }
        auto engine = llc::StereoEngine::build (ir, static_cast<llc::InputMode> (std::clamp (static_cast<int> (v[inputMode - 100] + 0.5), 0, 3)));
        if (stale (stamp)) continue;   // périmé : on ne l'installe pas
        if (! engine)
        {
            proc_.unloadIR();
            setStatus (kError, 0.0);
            irVersion_.fetch_add (1, std::memory_order_relaxed);
            continue;
        }
        const double ms = 1000.0 * static_cast<double> (ir.length()) / ir.sampleRate;
        proc_.loadEngine (std::move (engine));
        setStatus (kLoaded, ms);
        irVersion_.fetch_add (1, std::memory_order_relaxed);
    }
}
} // namespace neptune::core
