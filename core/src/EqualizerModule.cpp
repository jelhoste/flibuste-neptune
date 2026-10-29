// Égaliseur : voir EqualizerModule.h (porté de ZL Equalizer, zsliu98, AGPLv3).
#include "neptune/core/EqualizerModule.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
using namespace equalizer;

namespace
{
constexpr double kPi = kBiquadPi;
double dbToLin (double db) noexcept { return std::pow (10.0, db / 20.0); }

// Q de la i-ème section d'un filtre d'ordre n (n pair) : pôles de Butterworth étalés selon le Q demandé (ZL, filter_design.hpp).
double sectionQ (int n, int i, double q0) noexcept
{
    const int number = n / 2;
    const double theta0 = kPi / number / 4.0, scale = std::pow (std::sqrt (2.0) * q0, 1.0 / number);
    const double rescaleBase = std::log10 (std::sqrt (2.0) * q0) / std::pow (static_cast<double> (n), 1.5) * 12.0;
    const double centered = i - number / 2.0 + 0.5;
    return 0.5 / std::cos (theta0 * (2 * i + 1)) * scale * std::pow (2.0, centered * rescaleBase);
}

int passSections (Type type, int n, double fs, double f, double q0, std::array<BiquadCoefs, kMaxSections>& out, int start) noexcept
{
    if (n <= 1)
    {
        out[static_cast<std::size_t> (start)] = type == kLowPass ? BiquadCoefs::lowpass1 (fs, f) : type == kHighPass ? BiquadCoefs::highpass1 (fs, f) : BiquadCoefs::allpass1 (fs, f);
        return 1;
    }
    const int number = n / 2;
    for (int i = 0; i < number; ++i)
    {
        const double q = sectionQ (n, i, q0);
        out[static_cast<std::size_t> (start + i)] = type == kLowPass ? BiquadCoefs::lowpass (fs, f, q) : type == kHighPass ? BiquadCoefs::highpass (fs, f, q) : BiquadCoefs::allpass (fs, f, q);
    }
    return number;
}

// Étagère (basse ou haute) d'ordre n : le gain est réparti sur les sections.
int shelfSections (bool low, int n, double fs, double f, double gainDb, double qm, std::array<BiquadCoefs, kMaxSections>& out, int start) noexcept
{
    if (n <= 1)
    {
        out[static_cast<std::size_t> (start)] = low ? BiquadCoefs::lowShelf1 (fs, f, gainDb) : BiquadCoefs::highShelf1 (fs, f, gainDb);
        return 1;
    }
    if (n == 2)
    {
        out[static_cast<std::size_t> (start)] = low ? BiquadCoefs::lowShelf (fs, f, gainDb, qm) : BiquadCoefs::highShelf (fs, f, gainDb, qm);
        return 1;
    }
    const int number = n / 2;
    for (int i = 0; i < number; ++i)
    {
        const double q = sectionQ (n, i, qm), g = gainDb / number;
        out[static_cast<std::size_t> (start + i)] = low ? BiquadCoefs::lowShelf (fs, f, g, q) : BiquadCoefs::highShelf (fs, f, g, q);
    }
    return number;
}

// Gain « sgc » : intégrale approchée de l'effet de la bande, coefficients ajustés par ZL.
double integrateFQ (double f1, double f2) noexcept
{
    const double w1 = 1.0000057078597646 + 1.3450513160225395e-8 * f1 * f1, w2 = 1.0000057078597646 + 1.3450513160225395e-8 * f2 * f2;
    return std::log ((w1 + 1.0) * (1.0 - w2) / (w2 + 1.0) / (1.0 - w1));
}
double estimation (double fq, double bw, double g, const std::array<double, 3>& x) noexcept { return (x[0] * fq + x[1] * bw) * g * x[2]; }
} // namespace

namespace equalizer
{
int designFilter (Type type, int order, double fs, double f, double gainDb, double q, std::array<BiquadCoefs, kMaxSections>& out, double& scale) noexcept
{
    scale = 1.0;
    const int n = std::max (1, order);
    q = std::clamp (q, 0.025, 25.0);
    f = std::clamp (f, 1.0, 0.49 * fs);
    const double qm = std::sqrt (q * std::sqrt (2.0)) / std::sqrt (2.0);
    switch (type)
    {
        case kPeak:
        {
            if (n < 2) return 0;
            if (n == 2) { out[0] = BiquadCoefs::peaking (fs, f, gainDb, q); return 1; }
            const double scaleBw = std::pow (2.0, std::asinh (0.5 / q) / std::log (2.0)), w1 = f / scaleBw, w2 = f * scaleBw;
            const bool has1 = w1 / fs > 1.0 / 48000.0, has2 = w2 / fs < 0.49;
            const double qs = std::sqrt (2.0) / 2.0;
            if (has1 && has2)
            {
                const int c1 = shelfSections (true, n, fs, w1, -gainDb, qs, out, 0);
                return c1 + shelfSections (true, n, fs, w2, gainDb, qs, out, c1);
            }
            if (has1) return shelfSections (false, n, fs, w1, gainDb, qs, out, 0);
            if (has2) return shelfSections (true, n, fs, w2, gainDb, qs, out, 0);
            scale = dbToLin (gainDb);
            return 0;
        }
        case kLowShelf: return shelfSections (true, n, fs, f, gainDb, qm, out, 0);
        case kHighShelf: return shelfSections (false, n, fs, f, gainDb, qm, out, 0);
        case kTiltShelf:
        {
            // positif : aigus relevés, graves abaissés, de gainDb/2 de part et d'autre
            const int c = shelfSections (true, n, fs, f, -gainDb, qm, out, 0);
            scale = dbToLin (gainDb * 0.5);
            return c;
        }
        case kLowPass: return passSections (kLowPass, n, fs, f, q, out, 0);
        case kHighPass: return passSections (kHighPass, n, fs, f, q, out, 0);
        case kAllPass: return passSections (kAllPass, n, fs, f, q, out, 0);
        case kNotch:
        case kBandPass:
        {
            if (n < 2) return 0;
            const double scaleBw = std::pow (2.0, std::asinh (0.5 / q) / std::log (2.0)), w = f / scaleBw, g = dbToLin (-6.0 / n);
            const double den = f * f - w * w;
            const double qq = type == kNotch ? g * w * f / std::sqrt (1.0 - g * g) / den : std::sqrt (1.0 - g * g) * w * f / g / den;
            const auto c = type == kNotch ? BiquadCoefs::notch (fs, f, qq) : BiquadCoefs::bandpass (fs, f, qq);
            for (int i = 0; i < n / 2; ++i) out[static_cast<std::size_t> (i)] = c;
            return n / 2;
        }
        case kFlatGain: scale = dbToLin (gainDb); return 0;
        default: return 0;
    }
}

double gainCompensationDb (Type type, double freq, double gain, double q) noexcept
{
    static constexpr std::array<double, 3> pps { 0.6797385437634612, 0.6501623179337382, 0.1661043031674446 }, pns { 1.0005839027125558, 0.2615438074138483, 0.0876180361048472 },
        lps { 0.5615303279130026, 1.0955796383939556, 0.0578375534446572 }, lns { 1.7666900390139590, -0.9879875452397923, 0.0466874416227134 },
        hps { -1.6271905034386083, 2.6722453328537070, 0.1780141475194901 }, hns { -0.0999799556355004, 1.0888973867418563, 0.0760070892708112 };
    switch (type)
    {
        case kPeak:
        {
            const double bw0 = std::asinh (0.5 / q) / std::log (2.0), sc = std::pow (2.0, bw0 / 2.0);
            const double f1 = std::clamp (freq / sc, 10.0, 20000.0), f2 = std::clamp (freq * sc, 10.0, 20000.0);
            const double bw = std::log2 (std::max (1e-9, f2 / f1)) * 2.0, fq = integrateFQ (f1, f2);
            return gain > 0 ? -std::max (0.0, estimation (fq, bw, gain, pps)) : -std::min (0.0, estimation (fq, bw, gain, pns));
        }
        case kLowShelf:
        {
            const double f = std::clamp (freq, 15.0, 5000.0), bw = std::log2 (f / 10.0), fq = integrateFQ (10.0, f);
            return gain > 0 ? -std::max (0.0, estimation (fq, bw, gain, lps)) : -std::min (0.0, estimation (fq, bw, gain, lns));
        }
        case kHighShelf:
        {
            const double f = std::clamp (freq, 200.0, 19999.0), bw = std::log2 (20000.0 / f), fq = integrateFQ (f, 20000.0);
            return gain > 0 ? -std::max (0.0, estimation (fq, bw, gain, hps)) : -std::min (0.0, estimation (fq, bw, gain, hns));
        }
        case kFlatGain: return -gain;
        default: return 0.0;
    }
}
} // namespace equalizer

EqualizerModule::EqualizerModule()
{
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

bool EqualizerModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto db = [] (ParamId id, const std::string& n, double lo, double hi, double def) { return P::continuous (id, n.c_str(), Unit::Decibel, lo, hi, def); };
    auto pct = [] (ParamId id, const std::string& n, double lo, double hi, double def) { return P::continuous (id, n.c_str(), Unit::Percent, lo, hi, def); };

    add (db (outputGain, "Output Gain", -30.0, 30.0, 0.0));
    add (P::toggle (autoGain, "Auto Gain"));
    add (pct (gainScale, "Gain Scale", -100.0, 200.0, 100.0));
    add (P::toggle (phaseFlip, "Phase Flip"));
    add (P::toggle (externalSide, "External Side"));
    {
        auto hid = [] (P p) { p.hidden = true; p.automatable = false; p.modulatable = false; return p; };
        add (hid (P::toggle (spOn, "Spectrum On", false)));   // éteint par défaut (coût de calcul) ; l'interrupteur SPECTRUM l'allume
        add (hid (P::choice (spQuality, "Spectrum Quality", { "Normal", "High" }, 0)));
        add (hid (P::choice (spSpeed, "Spectrum Speed", { "Very Fast", "Fast", "Medium", "Slow", "Very Slow" }, 2)));
        add (hid (P::choice (spTilt, "Spectrum Tilt", { "0 dB/oct", "1.5 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" }, 3)));
        add (hid (P::choice (spSmoothType, "Spectrum Smooth Type", { "OCT", "ERB" }, 1)));
        add (hid (P::choice (spSmoothOct, "Spectrum Smooth Octave", { "1/24", "1/12", "1/6", "1/3", "1" }, 1)));
        add (hid (P::choice (spSmoothErb, "Spectrum Smooth ERB", { "1/4", "1/2", "1", "2", "4" }, 1)));
        add (hid (P::choice (spTop, "Spectrum Top dB", { "6", "0", "-6", "-12", "-18", "-24", "-36" }, 1)));
        add (hid (P::choice (spMin, "Spectrum Range dB", { "-60", "-72", "-96" }, 0)));
        add (hid (P::choice (spChannels, "Spectrum Channels", { "Stereo", "Left", "Right", "Mid", "Side" }, 0)));
        add (hid (P::toggle (spShowIn, "Spectrum Show Input", true)));
        add (hid (P::toggle (spShowOut, "Spectrum Show Output", true)));
    }
    const std::vector<std::string> orders { "6 dB/oct", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    for (int b = 0; b < kBands; ++b)
    {
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        // Par défaut : un passe-haut à 20 Hz et trois cloches à gain nul (120 Hz, 1 kHz, 6 kHz) en service ; les autres bandes éteintes, réparties sur le spectre.
        const double freqs[kBands] = { 20, 120, 1000, 6000, 500, 800, 1200, 2000, 3000, 4500, 6500, 9000, 12000, 15000, 18000, 21000 };
        add (P::choice (bandParam (b, kStatus), n + "Status", { "Off", "Bypass", "On" }, b < 4 ? 2 : 0));
        add (P::choice (bandParam (b, kType), n + "Type", { "Peak", "Low Shelf", "Low Pass", "High Shelf", "High Pass", "Notch", "Band Pass", "Tilt Shelf", "All Pass", "Flat Gain" }, b == 0 ? 4 : 0));
        add (P::choice (bandParam (b, kOrder), n + "Order", orders, 1));
        add (P::choice (bandParam (b, kStereo), n + "Stereo", { "Stereo", "Left", "Right", "Mid", "Side" }, 0));
        add (P::continuous (bandParam (b, kFreq), (n + "Freq").c_str(), Unit::Hertz, 10.0, 30000.0, freqs[b], Curve::Exponential));
        add (db (bandParam (b, kGain), n + "Gain", -30.0, 30.0, 0.0));
        add (P::continuous (bandParam (b, kQ), (n + "Q").c_str(), Unit::None, 0.025, 25.0, 0.707, Curve::Exponential));
        add (P::toggle (bandParam (b, kDynOn), n + "Dynamic ON"));
        add (P::toggle (bandParam (b, kDynBypass), n + "Dynamic Bypass"));
        add (db (bandParam (b, kTarget), n + "Target Gain", -30.0, 30.0, 0.0));
        add (db (bandParam (b, kThreshold), n + "Threshold", -80.0, 0.0, -40.0));
        add (P::continuous (bandParam (b, kKnee), (n + "Knee Width").c_str(), Unit::Decibel, 0.0, 32.0, 8.0, Curve::Power, 2.0));
        add (P::continuous (bandParam (b, kAttack), (n + "Attack").c_str(), Unit::Milliseconds, 0.0, 1000.0, 100.0, Curve::Power, 2.0));
        add (P::continuous (bandParam (b, kRelease), (n + "Release").c_str(), Unit::Milliseconds, 0.0, 5000.0, 500.0, Curve::Power, 2.0));
        add (pct (bandParam (b, kSmooth), n + "Dynamic Smooth", 0.0, 100.0, 0.0));
        add (P::continuous (bandParam (b, kRmsLength), (n + "Dynamic RMS Length").c_str(), Unit::Milliseconds, 0.0, 40.0, 0.0));
        add (pct (bandParam (b, kRmsMix), n + "Dynamic RMS Mix", 0.0, 100.0, 50.0));
        add (P::choice (bandParam (b, kSideType), n + "Side Filter Type", { "BP", "LP", "HP", "AL" }, 0));
        add (P::choice (bandParam (b, kSideOrder), n + "Side Order", orders, 1));
        add (P::continuous (bandParam (b, kSideFreq), (n + "Side Freq").c_str(), Unit::Hertz, 10.0, 30000.0, 1000.0, Curve::Exponential));
        add (P::continuous (bandParam (b, kSideQ), (n + "Side Q").c_str(), Unit::None, 0.025, 25.0, 0.707, Curve::Exponential));
        add (P::toggle (bandParam (b, kSideLink), n + "Side Link"));
    }
    return ok;
}

void EqualizerModule::bind (const ParameterSet& set)
{
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
}

void EqualizerModule::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max (1, maxBlockSize);
    wl_.assign (static_cast<std::size_t> (maxBlock_), 0.0);
    wr_.assign (static_cast<std::size_t> (maxBlock_), 0.0);
    preL_.assign (static_cast<std::size_t> (maxBlock_), 0.0f);
    preR_.assign (static_cast<std::size_t> (maxBlock_), 0.0f);
    analyzer_.setSampleRate (sampleRate_);
    analyzer_.setMaxBlock (maxBlock_);
    const auto ringSize = static_cast<std::size_t> (std::ceil (0.04 * sampleRate_)) + 8;
    for (auto& b : bands_)
    {
        b.follower.prepare (sampleRate_);
        b.ring.assign (ringSize, 0.0);
    }
    reset();
}

void EqualizerModule::reset() noexcept
{
    for (auto& b : bands_)
    {
        for (auto& c : b.chain) c.reset();
        for (auto& c : b.sideChain) c.reset();
        b.follower.reset (0.0);
        std::fill (b.ring.begin(), b.ring.end(), 0.0);
        b.ringPos = b.ringFill = 0;
        b.ringSum = 0.0;
        b.haveCfg = false;
        b.appliedScaleGain = 1e9;
    }
    autoSmoothed_ = 0.0;
    lastGain_ = 1.0;
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

void EqualizerModule::designSide (Band& b) noexcept
{
    const Cfg& c = b.cfg;
    const int n = kOrders[static_cast<std::size_t> (std::clamp (c.sideOrder, 0, 6))];
    const double f = c.sideLink ? c.freq : c.sideFreq, q = c.sideLink ? c.q : c.sideQ;
    b.sideCount = 0;
    if (c.sideType == kSideAll) return;
    std::array<BiquadCoefs, kMaxSections> tmp {};
    double sc = 1.0;
    const Type t = c.sideType == kSideLowPass ? kLowPass : c.sideType == kSideHighPass ? kHighPass : kBandPass;
    const int count = std::min (8, designFilter (t, n, sampleRate_, f, 0.0, q, tmp, sc));
    for (int i = 0; i < count; ++i) b.sideCoefs[static_cast<std::size_t> (i)] = tmp[static_cast<std::size_t> (i)];
    b.sideCount = count;
}

void EqualizerModule::readSettings (const ParameterSet& params) noexcept
{
    for (std::size_t i = 0; i < idx_.size(); ++i)
        if (idx_[i] != ParameterSet::npos) values_[i] = params.getAt (idx_[i]);
    gainScale_ = param (equalizer::gainScale) * 0.01;
    for (int bi = 0; bi < kBands; ++bi)
    {
        auto p = [&] (int k) { return param (bandParam (bi, k)); };
        auto ip = [&] (int k) { return static_cast<int> (std::lround (p (k))); };
        Cfg c;
        c.status = std::clamp (ip (kStatus), 0, 2);
        c.type = std::clamp (ip (kType), 0, kNumTypes - 1);
        c.order = kOrders[static_cast<std::size_t> (std::clamp (ip (kOrder), 0, 6))];
        c.stereo = std::clamp (ip (kStereo), 0, 4);
        c.freq = p (kFreq); c.gain = p (kGain); c.q = p (kQ);
        c.dynOn = p (kDynOn) >= 0.5; c.dynBypass = p (kDynBypass) >= 0.5; c.target = p (kTarget);
        c.threshold = p (kThreshold); c.knee = std::max (0.01, p (kKnee)); c.attack = p (kAttack); c.release = p (kRelease);
        c.smooth = std::clamp (p (kSmooth) * 0.01, 0.0, 1.0);
        c.rmsLength = p (kRmsLength); c.rmsMix = std::clamp (p (kRmsMix) * 0.01, 0.0, 1.0);
        c.sideType = std::clamp (ip (kSideType), 0, 3); c.sideOrder = ip (kSideOrder);
        c.sideFreq = p (kSideFreq); c.sideQ = p (kSideQ); c.sideLink = p (kSideLink) >= 0.5;

        Band& b = bands_[static_cast<std::size_t> (bi)];
        const Cfg& o = b.cfg;
        const bool changedShape = !b.haveCfg || c.type != o.type || c.order != o.order || c.stereo != o.stereo || c.freq != o.freq || c.q != o.q || c.gain != o.gain
                                  || c.target != o.target || c.dynOn != o.dynOn || c.dynBypass != o.dynBypass;
        const bool changedSide = !b.haveCfg || c.sideType != o.sideType || c.sideOrder != o.sideOrder || c.sideFreq != o.sideFreq || c.sideQ != o.sideQ || c.sideLink != o.sideLink
                                 || (c.sideLink && (c.freq != o.freq || c.q != o.q));
        if (b.haveCfg && (c.type != o.type || c.order != o.order || c.stereo != o.stereo || c.status != o.status))
            for (auto& ch : b.chain) ch.reset();
        if (b.haveCfg && (c.sideType != o.sideType || c.sideOrder != o.sideOrder))
            for (auto& ch : b.sideChain) ch.reset();
        if (c.rmsLength != o.rmsLength) { std::fill (b.ring.begin(), b.ring.end(), 0.0); b.ringPos = b.ringFill = 0; b.ringSum = 0.0; }
        if (!b.haveCfg || c.attack != o.attack || c.release != o.release || c.smooth != o.smooth) b.follower.set (c.attack, c.release, 0.0, c.smooth);
        b.cfg = c;
        b.haveCfg = true;
        if (changedSide) designSide (b);
        if (changedShape) b.appliedScaleGain = 1e9;   // recalcul des coefficients à ce bloc
    }
}

void EqualizerModule::processBand (int index, int n) noexcept
{
    Band& b = bands_[static_cast<std::size_t> (index)];
    const Cfg& c = b.cfg;
    const bool dynamic = c.dynOn && !c.dynBypass;
    const double base = scaledGain (c.gain), target = scaledGain(c.target);
    const Type type = static_cast<Type> (c.type);
    double* const wl = wl_.data();
    double* const wr = wr_.data();

    auto redesign = [&] (double gainDb)
    {
        b.count = designFilter (type, c.order, sampleRate_, c.freq, gainDb, c.q, b.coefs, b.scale);
        b.appliedScaleGain = gainDb;
    };
    auto run = [] (const std::array<BiquadCoefs, kMaxSections>& cf, int count, Chain& ch, double x) noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& k = cf[static_cast<std::size_t> (i)];
            auto& z = ch.z[static_cast<std::size_t> (i)];
            const double y = k.b0 * x + z[0];
            z[0] = k.b1 * x - k.a1 * y + z[1];
            z[1] = k.b2 * x - k.a2 * y;
            x = y;
        }
        return x;
    };
    auto filterSample = [&] (int i) noexcept
    {
        double* l = wl + i; double* r = wr + i;
        switch (c.stereo)
        {
            case kStereoBoth: *l = run (b.coefs, b.count, b.chain[0], *l) * b.scale; *r = run (b.coefs, b.count, b.chain[1], *r) * b.scale; break;
            case kLeft: *l = run (b.coefs, b.count, b.chain[0], *l) * b.scale; break;
            case kRight: *r = run (b.coefs, b.count, b.chain[1], *r) * b.scale; break;
            case kMid:
            {
                const double m = (*l + *r) * 0.5, d = run (b.coefs, b.count, b.chain[0], m) * b.scale - m;
                *l += d; *r += d;
                break;
            }
            default:
            {
                const double s = (*l - *r) * 0.5, d = run (b.coefs, b.count, b.chain[1], s) * b.scale - s;
                *l += d; *r -= d;
                break;
            }
        }
    };

    if (!dynamic)
    {
        if (b.appliedScaleGain != base) redesign (base);
        b.currentGain = base;
        for (int i = 0; i < n; ++i) filterSample (i);
        return;
    }

    const bool useKey = param (externalSide) >= 0.5 && keyL_ != nullptr && keyR_ != nullptr;
    const double kneeLow = c.threshold - c.knee, slope = 0.5 / c.knee;
    const std::size_t ringLen = std::min (b.ring.size(), static_cast<std::size_t> (std::max (0.0, std::round (c.rmsLength * 0.001 * sampleRate_))));
    for (int c0 = 0; c0 < n; c0 += kChunk)
    {
        const int len = std::min (kChunk, n - c0);
        double ratio = 0.0;
        for (int j = 0; j < len; ++j)
        {
            const int i = c0 + j;
            const double l = useKey ? static_cast<double> (keyL_[i]) : wl[i], r = useKey ? static_cast<double> (keyR_[i]) : wr[i];
            double x0 = 0.0, x1 = 0.0;
            int channels = 1;
            switch (c.stereo)
            {
                case kStereoBoth: x0 = l; x1 = r; channels = 2; break;
                case kLeft: x0 = l; break;
                case kRight: x0 = r; break;
                case kMid: x0 = (l + r) * 0.5; break;
                default: x0 = (l - r) * 0.5; break;
            }
            if (b.sideCount > 0)
            {
                x0 = run (b.sideCoefs, b.sideCount, b.sideChain[0], x0);
                if (channels == 2) x1 = run (b.sideCoefs, b.sideCount, b.sideChain[1], x1);
            }
            double p = x0 * x0 + (channels == 2 ? x1 * x1 : 0.0);
            if (ringLen > 0)
            {
                if (b.ringFill >= ringLen) { b.ringSum -= b.ring[(b.ringPos + b.ring.size() - ringLen) % b.ring.size()]; --b.ringFill; }
                b.ring[b.ringPos] = p;
                b.ringPos = (b.ringPos + 1) % b.ring.size();
                b.ringSum = std::max (0.0, b.ringSum + p);
                ++b.ringFill;
                p = p * (1.0 - c.rmsMix) + b.ringSum / static_cast<double> (ringLen) * c.rmsMix;
            }
            const double dbv = 10.0 * std::log10 (std::max (p, 1e-24));
            const double v = std::clamp ((dbv - kneeLow) * slope, 0.0, 1.0);
            ratio = b.follower.process (v * v);
        }
        const double g = base + ratio * (target - base);
        if (std::abs (g - b.appliedScaleGain) > 0.002) redesign (g);
        b.currentGain = g;
        for (int j = 0; j < len; ++j) filterSample (c0 + j);
    }
}

void EqualizerModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    n = std::min (n, maxBlock_);
    readSettings (params);
    float inL = 0.0f, inR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        wl_[static_cast<std::size_t> (i)] = left[i];
        wr_[static_cast<std::size_t> (i)] = right[i];
        preL_[static_cast<std::size_t> (i)] = left[i];
        preR_[static_cast<std::size_t> (i)] = right[i];
        inL = std::max (inL, std::abs (left[i]));
        inR = std::max (inR, std::abs (right[i]));
    }
    double comp = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        Band& band = bands_[static_cast<std::size_t> (b)];
        if (band.cfg.status == kOn)
        {
            processBand (b, n);
            comp += gainCompensationDb (static_cast<Type> (band.cfg.type), band.cfg.freq, band.currentGain, band.cfg.q);
        }
        else band.currentGain = scaledGain (band.cfg.gain);
        meters_[static_cast<std::size_t> (kBandGain0 + b)].store (static_cast<float> (band.currentGain), std::memory_order_relaxed);
    }
    const bool autoOn = param (autoGain) >= 0.5;
    const double blockSeconds = n / sampleRate_;
    autoSmoothed_ += ((autoOn ? comp : 0.0) - autoSmoothed_) * (1.0 - std::exp (-blockSeconds / 0.05));
    const double total = dbToLin (param (outputGain) + autoSmoothed_) * (param (phaseFlip) >= 0.5 ? -1.0 : 1.0);
    float outL = 0.0f, outR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double gain = lastGain_ + (total - lastGain_) * (static_cast<double> (i + 1) / n);
        left[i] = static_cast<float> (wl_[static_cast<std::size_t> (i)] * gain);
        right[i] = static_cast<float> (wr_[static_cast<std::size_t> (i)] * gain);
        outL = std::max (outL, std::abs (left[i]));
        outR = std::max (outR, std::abs (right[i]));
    }
    lastGain_ = total;
    if (analyzer_.active())
    {
        const float* const streams[4] = { preL_.data(), preR_.data(), left, right };
        analyzer_.push (streams, n);
    }
    meters_[kInPeakL].store (inL, std::memory_order_relaxed);
    meters_[kInPeakR].store (inR, std::memory_order_relaxed);
    meters_[kOutPeakL].store (outL, std::memory_order_relaxed);
    meters_[kOutPeakR].store (outR, std::memory_order_relaxed);
    meters_[kAutoGainDb].store (static_cast<float> (autoSmoothed_), std::memory_order_relaxed);
}

double EqualizerModule::bandResponseDb (const ParameterSet& params, int band, double f, bool current) const noexcept
{
    auto at = [&] (int k) { const auto i = idx_[static_cast<std::size_t> (bandParam (band, k) - 100)]; return i == ParameterSet::npos ? 0.0 : params.getAt (i); };
    if (band < 0 || band >= kBands || std::lround (at (kStatus)) != kOn) return 0.0;
    const auto si = idx_[static_cast<std::size_t> (equalizer::gainScale - 100)];
    const double sc = si == ParameterSet::npos ? 1.0 : params.getAt (si) * 0.01;
    double g = at (kGain) * sc;
    // gain courant mesuré par le DSP : seulement pour une bande dynamique (son gain bouge avec le signal). Une bande statique suit toujours le réglage, même quand le module
    // n'est pas traité (insert de Neptune sans signal) : sinon la courbe resterait figée sur la dernière mesure.
    if (current && at (kDynOn) >= 0.5) g = static_cast<double> (meters_[static_cast<std::size_t> (kBandGain0 + band)].load (std::memory_order_relaxed));
    std::array<BiquadCoefs, kMaxSections> cf {};
    double scale = 1.0;
    const int count = designFilter (static_cast<Type> (std::clamp (static_cast<int> (std::lround (at (kType))), 0, kNumTypes - 1)),
                                    kOrders[static_cast<std::size_t> (std::clamp (static_cast<int> (std::lround (at (kOrder))), 0, 6))], sampleRate_, at (kFreq), g, at (kQ), cf, scale);
    double m = scale;
    for (int i = 0; i < count; ++i) m *= cf[static_cast<std::size_t> (i)].magnitude (sampleRate_, f);
    return 20.0 * std::log10 (std::max (m, 1e-9));
}

double EqualizerModule::totalResponseDb (const ParameterSet& params, double f, bool current) const noexcept
{
    double s = 0.0;
    for (int b = 0; b < kBands; ++b) s += bandResponseDb (params, b, f, current);
    return s;
}
} // namespace neptune::core
