#include "neptune/core/OverdriveModule.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
using namespace overdrive;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kSlopes[3] = { 12, 24, 48 };
constexpr int kFactors[3] = { 1, 2, 4 };

inline double dbToLin (double db) noexcept { return std::exp (db * 0.11512925464970229); }
inline double smoothCoef (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * fs)); }
inline int choiceIndex (double v, int count) noexcept { return std::clamp (static_cast<int> (v + 0.5), 0, count - 1); }

// ---- courbes : toutes de pente 1 à l'origine et bornées près de +-1
inline double softRat (double u) noexcept { return u / (1.0 + std::fabs (u)); }
inline double hardClip (double u) noexcept { return std::clamp (u, -1.0, 1.0); }
// Famille de coudes : 0 très doux (rationnelle), 0,5 tangente hyperbolique, 1 écrêtage dur.
inline double knee (double u, double c) noexcept
{
    if (c < 0.5)
    {
        const double t = c * 2.0;
        return (1.0 - t) * softRat (u) + t * std::tanh (u);
    }
    const double t = (c - 0.5) * 2.0;
    return (1.0 - t) * std::tanh (u) + t * hardClip (u);
}
// Étage de triode : le point de repos (bias) décale l'entrée ; on retire la valeur au repos pour que zéro reste zéro.
inline double tubeStage (double x, double b, double c) noexcept { return knee (x + b, c) - knee (b, c); }
} // namespace

namespace overdrive
{
const char* modelName (int m) noexcept
{
    switch (m)
    {
        case kTape: return "Tape";
        case kTransistor: return "Transistor";
        case kFuzz: return "Fuzz";
        default: return "Tube";
    }
}

double shape (double x, const ShapeParams& p) noexcept
{
    switch (p.model)
    {
        case kTape:
            return knee (x, p.character * 0.6);   // symétrique, jamais très dur
        case kTransistor:
        {
            // diodes : exponentielle de chaque côté, limites différentes selon l'asymétrie
            const double kp = std::exp (0.7 * p.bias), kn = std::exp (-0.7 * p.bias);
            const double y = x >= 0.0 ? (1.0 - std::exp (-x * kp)) / kp : -(1.0 - std::exp (x * kn)) / kn;
            return (1.0 - 0.7 * p.character) * y + 0.7 * p.character * hardClip (x);
        }
        case kFuzz:
        {
            // polarisation décalée, porte qui étouffe les petits signaux, écrêtage de plus en plus carré
            const double g = 0.12 * p.feel, ax = std::fabs (x);
            const double gate = (g > 1.0e-6 && ax < g) ? (ax / g) * (ax / g) : 1.0;
            const double u = 2.0 * (x + 0.5 * p.bias);
            return gate * ((1.0 - p.character) * std::tanh (u) + p.character * hardClip (u));
        }
        default:
        {
            // lampe : deux étages de triode en cascade, point de repos opposé et plus faible pour le second
            const double b = 0.6 * p.bias;
            const double y1 = tubeStage (x, b, p.character);
            return tubeStage (1.2 * y1, -0.4 * b, p.character);
        }
    }
}

double autoGainFor (double driveLin, const ShapeParams& p) noexcept
{
    constexpr int N = 512;
    constexpr double amp = 0.25;   // -12 dBFS
    double ys[N];
    double mean = 0.0;
    for (int i = 0; i < N; ++i)
    {
        ys[i] = shape (driveLin * amp * std::sin (2.0 * kPi * static_cast<double> (i) / N), p);
        mean += ys[i];
    }
    mean /= N;
    double sum = 0.0;
    for (int i = 0; i < N; ++i) sum += (ys[i] - mean) * (ys[i] - mean);
    const double rmsOut = std::sqrt (sum / N), rmsIn = amp / std::sqrt (2.0);
    if (rmsOut < 1.0e-9) return 1.0;
    return std::clamp (rmsIn / rmsOut, 0.05, 20.0);
}
} // namespace overdrive

// ---- module
void OverdriveModule::Chain::reset() noexcept
{
    os.reset();
    inHp.reset(); inLp.reset(); outHp.reset(); outLp.reset();
    pre.reset(); post1.reset(); post2.reset(); dc.reset();
    toneLp = sagEnv = 0.0;
    delay.fill (0.0f);
    delayPos = 0;
}

OverdriveModule::OverdriveModule()
{
    idx_.fill (ParameterSet::npos);
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
    hold_.fill (0.0f);
}

bool OverdriveModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto fixed = [] (P s) { s.automatable = false; return s; };
    auto pct = [] (ParamId id, const char* n, double lo, double hi, double def) { return P::continuous (id, n, Unit::Percent, lo, hi, def); };
    const std::vector<std::string> slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct" };

    add (P::choice (model, "Model", { "Tube", "Tape", "Transistor", "Fuzz" }, kTube));
    add (P::continuous (drive, "Drive", Unit::Decibel, 0.0, 60.0, 18.0));
    add (pct (bias, "Bias", -100.0, 100.0, 0.0));
    add (pct (character, "Character", 0.0, 100.0, 50.0));
    add (pct (feel, "Feel", 0.0, 100.0, 40.0));
    add (pct (tone, "Tone", -100.0, 100.0, 0.0));
    add (P::continuous (level, "Output Level", Unit::Decibel, -24.0, 12.0, 0.0));
    add (P::toggle (autoGain, "Auto Gain", true));
    add (fixed (P::choice (oversampling, "Oversampling", { "Off", "2x", "4x" }, 1)));
    add (fixed (P::choice (channels, "Channels", { "Stereo", "Mid/Side", "Mid only", "Side only" }, kStereo)));

    add (P::toggle (inHpOn, "Input HP On"));
    add (P::continuous (inHp, "Input HP", Unit::Hertz, 20.0, 1000.0, 80.0, Curve::Exponential));
    add (fixed (P::choice (inHpSlope, "Input HP Slope", slopes, 0)));
    add (P::toggle (inLpOn, "Input LP On"));
    add (P::continuous (inLp, "Input LP", Unit::Hertz, 1000.0, 20000.0, 8000.0, Curve::Exponential));
    add (fixed (P::choice (inLpSlope, "Input LP Slope", slopes, 0)));
    add (P::toggle (outHpOn, "Output HP On"));
    add (P::continuous (outHp, "Output HP", Unit::Hertz, 20.0, 1000.0, 40.0, Curve::Exponential));
    add (fixed (P::choice (outHpSlope, "Output HP Slope", slopes, 0)));
    add (P::toggle (outLpOn, "Output LP On"));
    add (P::continuous (outLp, "Output LP", Unit::Hertz, 1000.0, 20000.0, 12000.0, Curve::Exponential));
    add (fixed (P::choice (outLpSlope, "Output LP Slope", slopes, 0)));

    add (P::toggle (eqOn, "EQ On", true));
    const double defFreq[3] = { 100.0, 1000.0, 6000.0 };
    const int defType[3] = { 1, 0, 2 };   // plateau grave, cloche, plateau aigu
    for (int b = 0; b < 3; ++b)
    {
        const std::string n = "EQ " + std::to_string (b + 1);
        add (P::toggle (bandParam (b, kOn), n + " On"));
        add (P::choice (bandParam (b, kType), n + " Type", { "Bell", "Low Shelf", "High Shelf" }, defType[b]));
        add (P::continuous (bandParam (b, kFreq), n + " Freq", Unit::Hertz, 20.0, 20000.0, defFreq[b], Curve::Exponential));
        add (P::continuous (bandParam (b, kGain), n + " Gain", Unit::Decibel, -18.0, 18.0, 0.0));
        add (P::continuous (bandParam (b, kQ), n + " Q", Unit::None, 0.1, 10.0, 1.0, Curve::Exponential));
    }
    add (P::toggle (eqHpOn, "EQ HP On"));
    add (P::continuous (eqHp, "EQ HP", Unit::Hertz, 20.0, 1000.0, 80.0, Curve::Exponential));
    add (fixed (P::choice (eqHpSlope, "EQ HP Slope", slopes, 0)));
    add (P::toggle (eqLpOn, "EQ LP On"));
    add (P::continuous (eqLp, "EQ LP", Unit::Hertz, 1000.0, 20000.0, 12000.0, Curve::Exponential));
    add (fixed (P::choice (eqLpSlope, "EQ LP Slope", slopes, 0)));

    add (fixed (P::choice (keySource, "Key Source", { "Off", "Main", "Sidechain" }, kKeyOff)));
    add (pct (keyAmount, "Key Amount", -100.0, 100.0, 0.0));
    add (P::continuous (keyAttack, "Key Attack", Unit::Milliseconds, 1.0, 200.0, 5.0, Curve::Exponential));
    add (P::continuous (keyRelease, "Key Release", Unit::Milliseconds, 10.0, 1000.0, 120.0, Curve::Exponential));
    add (P::continuous (keyHp, "Key HP", Unit::Hertz, 20.0, 2000.0, 20.0, Curve::Exponential));
    return ok;
}

void OverdriveModule::bind (const ParameterSet& set)
{
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
}

void OverdriveModule::prepare (double sampleRate, int maxBlockSize)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max (1, maxBlockSize);
    for (auto& c : chain_) c.os.prepare (maxBlock_);
    for (auto* v : { &a_, &b_, &keepA_, &keepB_, &modDb_ }) v->assign (static_cast<std::size_t> (maxBlock_), 0.0f);
    eq_.prepare (fs_);
    sagAttack_ = smoothCoef (0.005, fs_);
    sagRelease_ = smoothCoef (0.150, fs_);
    gainSmooth_ = smoothCoef (0.005, fs_);
    levelSmooth_ = smoothCoef (0.010, fs_);
    toneCoef_ = 1.0 - std::exp (-2.0 * kPi * 800.0 / fs_);
    haveLast_ = false;
    reset();
}

void OverdriveModule::reset() noexcept
{
    for (auto& c : chain_) { c.reset(); c.gain = 1.0; }
    eq_.reset();
    keyHpFilter_.reset();
    keyEnv_ = 0.0;
    smoothInit_ = false;
    hold_.fill (0.0f);
}

int OverdriveModule::latencySamples (const ParameterSet& params) const noexcept
{
    const auto i = params.indexOf (oversampling);
    const int f = i == ParameterSet::npos ? 2 : kFactors[choiceIndex (params.getAt (i), 3)];
    return Oversampler::latencyFor (f);
}

void OverdriveModule::readParams (const ParameterSet& params) noexcept
{
    for (std::size_t i = 0; i < v_.size(); ++i) v_[i] = idx_[i] != ParameterSet::npos ? params.getAt (idx_[i]) : 0.0;
}

llc::EQParams OverdriveModule::makeEq() const noexcept
{
    llc::EQParams eq;
    for (int b = 0; b < 3; ++b)
    {
        auto& band = eq.band[b];
        band.enabled = param (bandParam (b, kOn)) >= 0.5;
        band.type = static_cast<llc::BandType> (choiceIndex (param (bandParam (b, kType)), 3));
        band.freq = param (bandParam (b, kFreq));
        band.gainDb = param (bandParam (b, kGain));
        band.q = param (bandParam (b, kQ));
    }
    eq.hp = { param (eqHpOn) >= 0.5, param (eqHp), kSlopes[choiceIndex (param (eqHpSlope), 3)] };
    eq.lp = { param (eqLpOn) >= 0.5, param (eqLp), kSlopes[choiceIndex (param (eqLpSlope), 3)] };
    return eq;
}

void OverdriveModule::updateDesigns() noexcept
{
    const bool snap = ! haveLast_;
    last_ = v_;
    haveLast_ = true;

    modelIndex_ = choiceIndex (param (model), kNumModels);
    factor_ = kFactors[choiceIndex (param (oversampling), 3)];
    channelMode_ = choiceIndex (param (channels), kNumChannelModes);
    keyMode_ = choiceIndex (param (keySource), kNumKeySources);
    latency_ = Oversampler::latencyFor (factor_);
    for (auto& c : chain_)
        if (c.os.factor() != factor_) c.os.setFactor (factor_);

    driveDb_ = param (drive);
    shapeTarget_ = { modelIndex_, param (bias) / 100.0, param (character) / 100.0, param (feel) / 100.0 };
    compTarget_ = param (autoGain) >= 0.5 ? autoGainFor (dbToLin (driveDb_), shapeTarget_) : 1.0;
    levelLin_ = dbToLin (param (level));
    const double t = param (tone) / 100.0;
    toneGainLow_ = dbToLin (-6.0 * t);
    toneGainHigh_ = dbToLin (6.0 * t);

    const double feel01 = param (feel) / 100.0;
    BiquadCoefs pre, post1, post2;   // identité par défaut
    if (modelIndex_ == kTape)
    {
        pre = BiquadCoefs::highShelf (fs_, 4000.0, 8.0 * feel01, 0.707);          // les aigus saturent davantage ...
        post1 = BiquadCoefs::highShelf (fs_, 4000.0, -8.0 * feel01, 0.707);       // ... puis retour à la balance d'origine
        post2 = BiquadCoefs::lowShelf (fs_, 80.0, 3.0 * feel01, 0.707);           // bosse de la tête de lecture
    }
    else if (modelIndex_ == kTransistor)
        pre = BiquadCoefs::peaking (fs_, 800.0, 8.0 * feel01, 0.8);               // bosse des médiums avant l'écrêtage
    const auto dcBlock = BiquadCoefs::highpass (fs_, 10.0, 0.7071);
    for (auto& c : chain_)
    {
        c.inHp.design (true, fs_, param (inHp), kSlopes[choiceIndex (param (inHpSlope), 3)]);
        c.inLp.design (false, fs_, param (inLp), kSlopes[choiceIndex (param (inLpSlope), 3)]);
        c.outHp.design (true, fs_, param (outHp), kSlopes[choiceIndex (param (outHpSlope), 3)]);
        c.outLp.design (false, fs_, param (outLp), kSlopes[choiceIndex (param (outLpSlope), 3)]);
        c.pre.set (pre);
        c.post1.set (post1);
        c.post2.set (post2);
        c.dc.set (dcBlock);
    }
    keyHpFilter_.set (BiquadCoefs::highpass (fs_, param (keyHp), 0.7071));
    keyAttack_ = smoothCoef (param (keyAttack) * 0.001, fs_);
    keyRelease_ = smoothCoef (param (keyRelease) * 0.001, fs_);
    eq_.setParams (makeEq(), snap);
    if (snap) { shape_ = shapeTarget_; smoothInit_ = false; }
}

void OverdriveModule::delayThrough (Chain& c, float* buf, int len, int samples) noexcept
{
    const std::size_t d = static_cast<std::size_t> (std::clamp (samples, 0, 127));
    for (int i = 0; i < len; ++i)
    {
        c.delay[c.delayPos] = buf[i];
        buf[i] = c.delay[(c.delayPos + 128 - d) & 127];
        c.delayPos = (c.delayPos + 1) & 127;
    }
}

void OverdriveModule::processChain (Chain& c, float* buf, int len) noexcept
{
    const bool inHpOnNow = param (inHpOn) >= 0.5, inLpOnNow = param (inLpOn) >= 0.5, outHpOnNow = param (outHpOn) >= 0.5, outLpOnNow = param (outLpOn) >= 0.5;
    const double sagAmount = modelIndex_ == kTube ? 1.5 * param (feel) / 100.0 : 0.0;
    const bool emphasis = modelIndex_ == kTape || modelIndex_ == kTransistor;

    // ---- cadence de base : filtres d'entrée, préaccentuation, drive
    for (int i = 0; i < len; ++i)
    {
        double x = buf[i];
        if (! std::isfinite (x)) x = 0.0;
        if (inHpOnNow) x = c.inHp.process (static_cast<float> (x));
        if (inLpOnNow) x = c.inLp.process (static_cast<float> (x));
        if (emphasis) x = c.pre.processD (x);
        const double target = dbToLin (driveDb_ + static_cast<double> (modDb_[static_cast<std::size_t> (i)]));
        if (! smoothInit_) c.gain = target;
        c.gain += (target - c.gain) * gainSmooth_;
        double g = c.gain;
        if (sagAmount > 0.0)
        {
            // affaissement : un signal fort tire l'alimentation vers le bas, le gain baisse un peu (enveloppe lente)
            const double level01 = std::min (std::fabs (x * g), 4.0) * 0.25;
            c.sagEnv += (level01 > c.sagEnv ? sagAttack_ : sagRelease_) * (level01 - c.sagEnv);
            g /= 1.0 + sagAmount * c.sagEnv;
        }
        buf[i] = static_cast<float> (x * g);
    }

    // ---- cadence haute : courbe de transfert (réglages interpolés sur le bloc)
    float* hi = c.os.up (buf, len);
    const int total = len * factor_;
    ShapeParams sp = shape_;
    const double inv = 1.0 / static_cast<double> (std::max (1, total));
    const double stepBias = (shapeTarget_.bias - sp.bias) * inv, stepChar = (shapeTarget_.character - sp.character) * inv, stepFeel = (shapeTarget_.feel - sp.feel) * inv;
    for (int k = 0; k < total; ++k)
    {
        sp.bias += stepBias;
        sp.character += stepChar;
        sp.feel += stepFeel;
        hi[k] = static_cast<float> (shape (static_cast<double> (hi[k]), sp));
    }
    c.os.down (buf, len);

    // ---- cadence de base : continu, tonalité, désaccentuation, filtres de sortie
    for (int i = 0; i < len; ++i)
    {
        double y = c.dc.processD (static_cast<double>(buf[i]));
        c.toneLp += toneCoef_ * (y - c.toneLp);
        y = c.toneLp * toneGainLow_ + (y - c.toneLp) * toneGainHigh_;
        if (modelIndex_ == kTape) y = c.post2.processD (c.post1.processD (y));
        float f = static_cast<float> (y);
        if (outHpOnNow) f = c.outHp.process (f);
        if (outLpOnNow) f = c.outLp.process (f);
        buf[i] = f;
    }
}

void OverdriveModule::processChunk (float* l, float* r, int len, const float* kl, const float* kr) noexcept
{
    float* a = a_.data();
    float* b = b_.data();
    auto hold = [this] (int m) -> float& { return hold_[static_cast<std::size_t> (m)]; };
    const bool firstBlock = ! smoothInit_;
    for (int i = 0; i < len; ++i)
    {
        hold (kInPeakL) = std::max (hold (kInPeakL), std::fabs (l[i]));
        hold (kInPeakR) = std::max (hold (kInPeakR), std::fabs (r[i]));
    }

    // ---- entrée latérale : enveloppe de la clé -> modulation du drive (en dB)
    if (keyMode_ != kKeyOff)
    {
        const double amount = param (keyAmount) / 100.0 * 30.0;
        for (int i = 0; i < len; ++i)
        {
            float k = 0.0f;
            if (keyMode_ == kKeyMain) k = 0.5f * (l[i] + r[i]);
            else if (kl != nullptr && kr != nullptr) k = 0.5f * (kl[i] + kr[i]);
            const double a2 = std::fabs (keyHpFilter_.processD (static_cast<double> (k)));
            keyEnv_ += (a2 > keyEnv_ ? keyAttack_ : keyRelease_) * (a2 - keyEnv_);
            const double db = 20.0 * std::log10 (std::max (keyEnv_, 1.0e-6));
            const double k01 = std::clamp ((db + 40.0) / 40.0, 0.0, 1.0);
            modDb_[static_cast<std::size_t> (i)] = static_cast<float> (amount * k01);
            hold (kKeyLevel) = std::max (hold (kKeyLevel), static_cast<float> (k01));
        }
    }
    else std::fill (modDb_.begin(), modDb_.begin() + len, 0.0f);

    // ---- voies : stéréo ou milieu / côtés
    const bool midSide = channelMode_ != kStereo;
    for (int i = 0; i < len; ++i)
    {
        if (midSide) { a[i] = 0.5f * (l[i] + r[i]); b[i] = 0.5f * (l[i] - r[i]); }
        else { a[i] = l[i]; b[i] = r[i]; }
    }
    const bool doA = channelMode_ != kSideOnly, doB = channelMode_ != kMidOnly;

    shape_.model = shapeTarget_.model;
    if (doA) processChain (chain_[0], a, len); else delayThrough (chain_[0], a, len, latency_);
    if (doB) processChain (chain_[1], b, len); else delayThrough (chain_[1], b, len, latency_);
    shape_ = shapeTarget_;

    // ---- égaliseur (la voie qui n'est pas traitée est rendue telle quelle)
    if (param (eqOn) >= 0.5)
    {
        if (! doA) std::copy (a, a + len, keepA_.begin());
        if (! doB) std::copy (b, b + len, keepB_.begin());
        eq_.process (a, b, len);
        if (! doA) std::copy (keepA_.begin(), keepA_.begin() + len, a);
        if (! doB) std::copy (keepB_.begin(), keepB_.begin() + len, b);
    }

    // ---- niveau de sortie (avec la compensation de gain), lissé ; seules les voies traitées le reçoivent
    const double gainTarget = levelLin_ * compTarget_;
    for (int i = 0; i < len; ++i)
    {
        if (firstBlock && i == 0) levelCur_ = gainTarget;
        levelCur_ += (gainTarget - levelCur_) * levelSmooth_;
        const float g = static_cast<float> (levelCur_);
        if (doA) a[i] *= g;
        if (doB) b[i] *= g;
        if (midSide) { l[i] = a[i] + b[i]; r[i] = a[i] - b[i]; }
        else { l[i] = a[i]; r[i] = b[i]; }
        hold (kOutPeakL) = std::max (hold (kOutPeakL), std::fabs (l[i]));
        hold (kOutPeakR) = std::max (hold (kOutPeakR), std::fabs (r[i]));
    }
    smoothInit_ = true;
}

void OverdriveModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    if (n <= 0) return;
    readParams (params);
    if (! haveLast_ || v_ != last_) updateDesigns();
    const float* kl = keyL_;
    const float* kr = keyR_;
    keyL_ = keyR_ = nullptr;
    for (int done = 0; done < n;)
    {
        const int len = std::min (n - done, maxBlock_);
        processChunk (left + done, right + done, len, kl != nullptr ? kl + done : nullptr, kr != nullptr ? kr + done : nullptr);
        done += len;
    }
    for (std::size_t i = 0; i < hold_.size(); ++i)
    {
        meters_[i].store (hold_[i], std::memory_order_relaxed);
        hold_[i] = 0.0f;
    }
}
} // namespace neptune::core
