#include "neptune/core/DelayModule.h"
#include "neptune/core/DelayRead.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace neptune::core
{
using namespace delay;

namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double dbToLin (double db) noexcept { return std::exp (db * 0.11512925464970229); }
inline double coef (double seconds, double fs) noexcept { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * fs)); }

// Saturation douce de la boucle de feedback : gain unité pour les petits signaux, compression des forts (d : 0..1).
inline double saturate (double x, double d) noexcept
{
    const double g = 1.0 + 6.0 * d;
    return std::tanh (g * x) / g;
}
// Limiteur de sécurité : transparent jusqu'à 0 dBFS (1,0), écrête doucement au-delà (jamais plus de 1,5).
inline double softLimit (double x) noexcept
{
    const double a = std::fabs (x);
    if (a <= 1.0) return x;
    return std::copysign (1.0 + 0.5 * std::tanh ((a - 1.0) / 0.5), x);
}
} // namespace

DelayModule::DelayModule()
{
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

bool DelayModule::declareParams (ParameterSet& set, std::string* error) const
{
    using P = ParamSpec;
    bool ok = true;
    auto add = [&] (P s) { ok = ok && set.add (std::move (s), error); };
    auto fixed = [] (P s) { s.automatable = false; return s; };
    auto pct = [] (ParamId id, const std::string& n, double lo, double hi, double def) { return P::continuous (id, n, Unit::Percent, lo, hi, def); };

    add (P::toggle (sync, "Sync"));
    add (fixed (P::choice (routing, "Routing", { "Parallel", "Cascade" }, 0)));
    add (P::toggle (freeze, "Freeze"));
    add (pct (duckAmount, "Duck Amount", 0.0, 100.0, 0.0));
    add (P::continuous (duckThreshold, "Duck Threshold", Unit::Decibel, -60.0, 0.0, -30.0));
    add (P::continuous (duckRelease, "Duck Release", Unit::Milliseconds, 10.0, 2000.0, 300.0, Curve::Exponential));
    add (pct (feedbackGlobal, "Feedback", -95.0, 95.0, 0.0));
    add (pct (pingPong, "Ping-Pong", 0.0, 100.0, 0.0));
    add (pct (drive, "Drive", 0.0, 100.0, 0.0));
    add (pct (width, "Width", 0.0, 200.0, 100.0));
    add (fixed (P::continuous (glideMs, "Glide Time", Unit::Milliseconds, 20.0, 500.0, 120.0)));
    add (fixed (P::toggle (safetyLimiter, "Feedback Safety Limiter", true)));
    add (fixed (P::choice (pitchQuality, "Pitch Quality", { "Standard", "High" }, 0)));
    add (fixed (P::choice (timeMode, "Time Change", { "Glide", "Crossfade" }, 0)));

    for (int n = 0; n < kLines; ++n)
    {
        const std::string name = "Line " + std::to_string (n + 1) + " ";
        auto id = [n] (int k) { return lineParam (n, k); };
        const int defaultDiv[kLines] = { 4, 1, 2, 0, 0, 0, 0, 0 };   // 1/4, 1/2, 1/2., 1/1 ... (1/4 x N quand c'est possible)
        add (P::toggle (id (kOn), name + "On", n == 0));
        add (P::toggle (id (kSolo), name + "Solo"));
        add (P::continuous (id (kTime), name + "Time", Unit::Milliseconds, 1.0, maxTimeMs, 500.0 * (n + 1), Curve::Exponential));
        add (fixed (P::choice (id (kDiv), name + "Division", std::vector<std::string> (divisionNames, divisionNames + kDivisions), defaultDiv[n])));
        add (P::continuous (id (kLevel), name + "Level", Unit::Decibel, -60.0, 6.0, -3.0 * n));
        add (pct (id (kPan), name + "Pan", -100.0, 100.0, 0.0));
        add (P::continuous (id (kOffset), name + "R Offset", Unit::Milliseconds, -maxOffsetMs, maxOffsetMs, 0.0));
        add (pct (id (kFeedback), name + "Feedback", -95.0, 95.0, 30.0));
        add (P::continuous (id (kPitch), name + "Pitch", Unit::None, -24.0, 24.0, 0.0));
        add (P::continuous (id (kLowCut), name + "Low Cut", Unit::Hertz, 20.0, 2000.0, 20.0, Curve::Exponential));
        add (P::continuous (id (kHighCut), name + "High Cut", Unit::Hertz, 500.0, 20000.0, 20000.0, Curve::Exponential));
        add (P::toggle (id (kInvert), name + "Invert"));
        add (pct (id (kModDepth), name + "Mod Depth", 0.0, 100.0, 0.0));
        add (P::continuous (id (kModRate), name + "Mod Rate", Unit::Hertz, 0.05, 10.0, 0.5, Curve::Exponential));
    }
    return ok;
}

void DelayModule::bind (const ParameterSet& set)
{
    for (std::size_t i = 0; i < idx_.size(); ++i) idx_[i] = set.indexOf (static_cast<ParamId> (100 + i));
    idxIn_ = set.indexOf (stock::inputTrim);
    idxOut_ = set.indexOf (stock::outputTrim);
    idxMix_ = set.indexOf (stock::mix);
}

void DelayModule::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    bufSize_ = static_cast<std::size_t> (std::ceil ((maxTimeMs + maxOffsetMs + maxModMs + 4.0) * 0.001 * sampleRate_)) + 8;
    for (auto& l : lines_)
        for (auto& b : l.buf) b.assign (bufSize_, 0.0f);
    shifterSize_ = static_cast<std::size_t> (std::ceil (0.08 * sampleRate_)) + 16;
    for (auto& l : lines_)
        for (auto& b : l.shifter) b.assign (shifterSize_, 0.0f);
    reset();
}

void DelayModule::reset() noexcept
{
    for (auto& l : lines_)
    {
        for (auto& b : l.buf) std::fill (b.begin(), b.end(), 0.0f);
        l.pos = 0;
        for (auto& f : l.hp) f.reset();
        for (auto& f : l.lp) f.reset();
        l.y = { 0.0, 0.0 };
        l.active = false;
        l.hpDesigned = l.lpDesigned = -1.0;
        for (auto& b : l.shifter) std::fill (b.begin(), b.end(), 0.0f);
        l.shPos = 0;
        l.pitchSm = l.pitchAct = l.shPhi = 0.0;
        l.shEngaged = false;
    }
    freezeSm_ = 0.0;
    fbGlobalSm_ = 0.0;
    duckEnv_ = 0.0;
    duckGain_ = 1.0;
    duckAmountSm_ = 0.0;
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
    for (auto& p : peakHold_) p = 0.0f;
}

void DelayModule::process (float* left, float* right, int n, const ParameterSet& params) noexcept
{
    if (n <= 0 || bufSize_ == 0) return;
    for (std::size_t i = 0; i < idx_.size(); ++i)
        if (idx_[i] != ParameterSet::npos) values_[i] = params.getAt (idx_[i]);
    const double fs = sampleRate_;
    double bpm = tempo_.load (std::memory_order_relaxed);
    if (! (bpm > 1.0)) bpm = 120.0;
    const bool syncOn = param (sync) >= 0.5, cascade = param (routing) >= 0.5, limiter = param (safetyLimiter) >= 0.5;
    const bool freezeOn = param (freeze) >= 0.5;
    const double pp = param (pingPong) * 0.01, driveAmt = param (drive) * 0.01, widthAmt = param (width) * 0.01;
    const double fbGlobalTarget = param (feedbackGlobal) * 0.01;
    const double duckAmountTarget = param (duckAmount) * 0.01, duckThr = param (duckThreshold);
    const double duckAtt = coef (0.005, fs), duckRel = coef (param (duckRelease) * 0.001, fs);
    const bool crossfade = param (timeMode) >= 0.5;
    const double xfStep = 1.0 / std::max (1.0, param (glideMs) * 0.001 * fs);
    const bool highQuality = param (pitchQuality) >= 0.5;
    const double grain = (highQuality ? 0.08 : 0.04) * fs;      // taille de grain (échantillons)
    const double kSm = coef (0.02, fs), kGlide = coef (param (glideMs) * 0.001, fs), kFreeze = coef (0.02, fs);
    const double outGainMeter = dbToLin (idxOut_ == ParameterSet::npos ? 0.0 : params.getAt (idxOut_));
    const double inGainInv = 1.0 / std::max (1.0e-6, dbToLin (idxIn_ == ParameterSet::npos ? 0.0 : params.getAt (idxIn_)));
    const double mixMeter = idxMix_ == ParameterSet::npos ? 1.0 : std::clamp (params.getAt (idxMix_) * 0.01, 0.0, 1.0);

    // ---- cibles de chaque ligne
    bool anySolo = false;
    for (int n8 = 0; n8 < kLines; ++n8)
        if (lineValue (n8, kOn) >= 0.5 && lineValue (n8, kSolo) >= 0.5) anySolo = true;
    struct Target { bool active; double time, offset, level, panL, panR, fb, low, high, depth, rate, pitch; bool invert; };
    std::array<Target, kLines> tg {};
    for (int k = 0; k < kLines; ++k)
    {
        auto& t = tg[static_cast<std::size_t> (k)];
        t.active = lineValue (k, kOn) >= 0.5 && (! anySolo || lineValue (k, kSolo) >= 0.5);
        const double ms = syncOn ? divisionBeats (static_cast<int> (std::lround (lineValue (k, kDiv)))) * 60000.0 / bpm : lineValue (k, kTime);
        t.time = std::clamp (ms, 1.0, maxTimeMs) * 0.001 * fs;
        t.offset = lineValue (k, kOffset) * 0.001 * fs;
        t.level = dbToLin (lineValue (k, kLevel));
        const double pan = lineValue (k, kPan) * 0.01;
        t.panL = pan > 0.0 ? 1.0 - pan : 1.0;
        t.panR = pan < 0.0 ? 1.0 + pan : 1.0;
        t.fb = lineValue (k, kFeedback) * 0.01;
        t.low = lineValue (k, kLowCut);
        t.high = lineValue (k, kHighCut);
        t.depth = lineValue (k, kModDepth) * 0.01;
        t.rate = lineValue (k, kModRate);
        t.pitch = lineValue (k, kPitch);
        t.invert = lineValue (k, kInvert) >= 0.5;
        auto& l = lines_[static_cast<std::size_t> (k)];
        if (t.active && ! l.active)   // la ligne vient d'être activée : contenu vide, valeurs lissées directement à leur cible
        {
            for (auto& b : l.buf) std::fill (b.begin(), b.end(), 0.0f);
            l.pos = 0;
            l.y = { 0.0, 0.0 };
            for (auto& f : l.hp) f.reset();
            for (auto& f : l.lp) f.reset();
            l.timeSmp = l.timeOld = t.time; l.xf = 1.0; l.offsetSmp = t.offset; l.level = t.level; l.panL = t.panL; l.panR = t.panR; l.fb = t.fb;
            l.lowHz = t.low; l.highHz = t.high; l.hpDesigned = l.lpDesigned = -1.0;
            for (auto& b : l.shifter) std::fill (b.begin(), b.end(), 0.0f);
            l.shPos = 0; l.pitchSm = t.pitch; l.pitchAct = 0.0; l.shPhi = 0.0; l.shEngaged = false;
        }
        l.active = t.active;
        l.modDepth = t.depth;
        l.modRate = t.rate;
    }

    // ---- boucle d'échantillons
    for (int i = 0; i < n; ++i)
    {
        const double inL = left[i], inR = right[i];
        peakHold_[0] = std::max (peakHold_[0], static_cast<float> (std::fabs (inL)));
        peakHold_[1] = std::max (peakHold_[1], static_cast<float> (std::fabs (inR)));

        // ducking : suiveur du signal d'entrée (attaque 5 ms), atténuation progressive sur 6 dB au-dessus du seuil
        const double mag = std::max (std::fabs (inL), std::fabs (inR));
        duckEnv_ += (mag > duckEnv_ ? duckAtt : duckRel) * (mag - duckEnv_);
        duckAmountSm_ += kSm * (duckAmountTarget - duckAmountSm_);
        const double envDb = 20.0 * std::log10 (std::max (duckEnv_, 1.0e-9));
        const double duckW = std::clamp ((envDb - duckThr) / 6.0, 0.0, 1.0);
        duckGain_ = 1.0 - duckAmountSm_ * duckW;

        freezeSm_ += kFreeze * ((freezeOn ? 1.0 : 0.0) - freezeSm_);
        fbGlobalSm_ += kSm * (fbGlobalTarget - fbGlobalSm_);
        const double feed = 1.0 - freezeSm_;                         // FREEZE coupe l'entrée des lignes
        const double x[2] = { inL * feed, inR * feed };

        // passe 1 : lecture de toutes les lignes (la lecture ne dépend jamais de l'écriture de cet échantillon : retard >= 3), filtres, niveau, panoramique
        double sum[2] = { 0.0, 0.0 };
        double chain[2] = { x[0], x[1] };                            // cascade : sortie de la dernière ligne active
        for (int k = 0; k < kLines; ++k)
        {
            auto& l = lines_[static_cast<std::size_t> (k)];
            if (! l.active) continue;
            const auto& t = tg[static_cast<std::size_t> (k)];
            l.src = { cascade ? chain[0] : x[0], cascade ? chain[1] : x[1] };
            if (crossfade)
            {
                if (l.xf >= 1.0)
                {
                    if (std::fabs (t.time - l.timeSmp) > 2.0) { l.timeOld = l.timeSmp; l.timeSmp = t.time; l.xf = 0.0; }   // nouveau temps : fondu depuis l'ancien
                }
                else l.xf = std::min (1.0, l.xf + xfStep);
            }
            else
            {
                l.timeSmp += kGlide * (t.time - l.timeSmp);
                l.timeOld = l.timeSmp;
                l.xf = 1.0;
            }
            l.offsetSmp += kSm * (t.offset - l.offsetSmp);
            l.level += kSm * (t.level - l.level);
            l.panL += kSm * (t.panL - l.panL);
            l.panR += kSm * (t.panR - l.panR);
            l.fb += kSm * (t.fb - l.fb);
            l.lowHz += kSm * (t.low - l.lowHz);
            l.highHz += kSm * (t.high - l.highHz);
            // lecture : gauche à T, droite à T + décalage (jamais avant le signal direct), modulation sinusoïdale commune
            const double excursion = l.modDepth * std::min (maxModMs, l.timeSmp / fs * 1000.0) * 0.001 * fs;
            const double lfo = l.modDepth > 0.0 ? std::sin (l.phase) : 0.0;
            l.phase += 2.0 * kPi * l.modRate / fs;
            if (l.phase > 2.0 * kPi) l.phase -= 2.0 * kPi;
            const double maxD = static_cast<double> (bufSize_) - 8.0;
            auto delayFor = [&] (double timeSmp, int c)
            {
                const double base = c == 0 ? timeSmp : std::max (0.0, timeSmp + l.offsetSmp);
                return std::clamp (base + excursion * lfo, 3.0, maxD);
            };
            // lecture de la ligne (retranche `comp` échantillons) ; pendant un fondu, mélange de l'ancien et du nouveau temps
            auto readLine = [&] (int c, double comp)
            {
                const auto& b = l.buf[static_cast<std::size_t> (c)];
                const double now = readHermite (b, l.pos, std::max (3.0, delayFor (l.timeSmp, c) - comp));
                if (l.xf >= 1.0) return now;
                const double g = l.xf * l.xf * (3.0 - 2.0 * l.xf);
                return (1.0 - g) * readHermite (b, l.pos, std::max (3.0, delayFor (l.timeOld, c) - comp)) + g * now;
            };
            double yr[2] = { readLine (0, 0.0), readLine (1, 0.0) };
            // décalage de hauteur : deux têtes de lecture à fenêtres de Hann sur un court tampon, lues sur le signal de la ligne avancé de la demi-longueur
            // d'un grain (le retard moyen du décaleur est ainsi absorbé dans le temps de la ligne) ; fondu de 20 ms avec le chemin direct
            l.pitchSm += kSm * (t.pitch - l.pitchSm);
            const bool wantShift = std::fabs (t.pitch) > 0.001 || std::fabs (l.pitchSm) > 0.001;
            if (wantShift && ! l.shEngaged)
            {
                for (auto& b : l.shifter) std::fill (b.begin(), b.end(), 0.0f);
                l.shPos = 0;
                l.shPhi = 0.0;
                l.shEngaged = true;
            }
            if (l.shEngaged)
            {
                l.pitchAct += kSm * ((wantShift ? 1.0 : 0.0) - l.pitchAct);
                const double comp = 0.5 * grain + 3.0;
                const double ratio = std::exp2 (l.pitchSm / 12.0);
                const double bIn[2] = { readLine (0, comp), readLine (1, comp) };
                for (int c = 0; c < 2; ++c) l.shifter[static_cast<std::size_t> (c)][l.shPos] = static_cast<float> (bIn[c]);
                l.shPos = (l.shPos + 1) % shifterSize_;
                l.shPhi += (1.0 - ratio) / grain;
                l.shPhi -= std::floor (l.shPhi);
                const double phi2 = l.shPhi + 0.5 - std::floor (l.shPhi + 0.5);
                const double w1 = 0.5 - 0.5 * std::cos (2.0 * kPi * l.shPhi), w2 = 0.5 - 0.5 * std::cos (2.0 * kPi * phi2);
                const double d1 = 3.0 + l.shPhi * grain, d2 = 3.0 + phi2 * grain;
                for (int c = 0; c < 2; ++c)
                {
                    const auto& sb = l.shifter[static_cast<std::size_t> (c)];
                    const double shifted = w1 * readHermite (sb, l.shPos, d1) + w2 * readHermite (sb, l.shPos, d2);
                    yr[c] = (1.0 - l.pitchAct) * yr[c] + l.pitchAct * shifted;
                }
                if (! wantShift && l.pitchAct < 0.0005) l.shEngaged = false;   // retour au chemin direct : plus de calcul
            }
            // filtres : coefficients recalculés seulement quand la coupure a bougé
            if (l.lowHz > 20.5)
            {
                if (std::fabs (l.lowHz - l.hpDesigned) > 0.002 * l.lowHz) { const auto c = BiquadCoefs::highpass (fs, l.lowHz, 0.70710678); l.hp[0].set (c); l.hp[1].set (c); l.hpDesigned = l.lowHz; }
                yr[0] = l.hp[0].processD (yr[0]);
                yr[1] = l.hp[1].processD (yr[1]);
            }
            else l.hpDesigned = -1.0;
            if (l.highHz < 19990.0)
            {
                if (std::fabs (l.highHz - l.lpDesigned) > 0.002 * l.highHz) { const auto c = BiquadCoefs::lowpass (fs, std::min (l.highHz, 0.45 * fs), 0.70710678); l.lp[0].set (c); l.lp[1].set (c); l.lpDesigned = l.highHz; }
                yr[0] = l.lp[0].processD (yr[0]);
                yr[1] = l.lp[1].processD (yr[1]);
            }
            else l.lpDesigned = -1.0;
            l.y = { yr[0], yr[1] };
            const double sign = t.invert ? -1.0 : 1.0;
            const double o[2] = { yr[0] * sign * l.level, yr[1] * sign * l.level };
            chain[0] = o[0];
            chain[1] = o[1];
            sum[0] += o[0] * l.panL;
            sum[1] += o[1] * l.panR;
        }
        // passe 2 : écriture (entrée + feedback de la ligne + feedback global, ping-pong, saturation, limiteur) ; la boucle dure donc exactement T
        const double globalFb[2] = { (1.0 - pp) * sum[0] + pp * sum[1], (1.0 - pp) * sum[1] + pp * sum[0] };
        for (int k = 0; k < kLines; ++k)
        {
            auto& l = lines_[static_cast<std::size_t> (k)];
            if (! l.active) continue;
            const double fbEff = l.fb + freezeSm_ * (1.0 - l.fb);   // FREEZE pousse le feedback de la ligne à son maximum
            const double own[2] = { (1.0 - pp) * l.y[0] + pp * l.y[1], (1.0 - pp) * l.y[1] + pp * l.y[0] };
            for (int c = 0; c < 2; ++c)
            {
                double fbSum = fbEff * own[c] + fbGlobalSm_ * globalFb[c];
                if (driveAmt > 0.0) fbSum = saturate (fbSum, driveAmt);
                double w = l.src[static_cast<std::size_t> (c)] + fbSum;
                if (limiter) w = softLimit (w);
                l.buf[static_cast<std::size_t> (c)][l.pos] = static_cast<float> (w);
            }
            l.pos = (l.pos + 1) % bufSize_;
        }

        // sortie : ducking puis largeur (M-S du signal retardé)
        double wl = sum[0] * duckGain_, wr = sum[1] * duckGain_;
        const double mid = 0.5 * (wl + wr), side = 0.5 * (wl - wr) * widthAmt;
        wl = mid + side;
        wr = mid - side;
        left[i] = static_cast<float> (wl);
        right[i] = static_cast<float> (wr);
        // mesure de sortie : ce qui sortira du ModuleRunner (sec + retardé, mix et gain de sortie compris)
        const double fl = (1.0 - mixMeter) * inL * inGainInv + mixMeter * wl * outGainMeter, fr = (1.0 - mixMeter) * inR * inGainInv + mixMeter * wr * outGainMeter;
        peakHold_[2] = std::max (peakHold_[2], static_cast<float> (std::fabs (fl)));
        peakHold_[3] = std::max (peakHold_[3], static_cast<float> (std::fabs (fr)));
    }
    for (int c = 0; c < 4; ++c) { meters_[static_cast<std::size_t> (c)].store (peakHold_[c], std::memory_order_relaxed); peakHold_[c] = 0.0f; }
    meters_[kDuckDb].store (static_cast<float> (20.0 * std::log10 (std::max (duckGain_, 0.0631))), std::memory_order_relaxed);   // plancher -24 dB
}
} // namespace neptune::core
