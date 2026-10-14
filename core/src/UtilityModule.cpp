#include "neptune/core/UtilityModule.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Mixer.h"

namespace neptune::core
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

bool UtilityModule::declareParams (ParameterSet& set, std::string* error) const
{
    using namespace utility;
    auto volume = [] (ParamId id, const char* name)
    {
        auto s = ParamSpec::continuous (id, name, Unit::Decibel, -100.0, 12.0, 0.0);
        s.minText = "-inf";
        return s;
    };
    auto pct = [] (ParamId id, const char* name, double lo, double hi, double def)
    {
        return ParamSpec::continuous (id, name, Unit::Percent, lo, hi, def);
    };
    return set.add (ParamSpec::choice (mode, "Channel Mode", { "Stereo", "Dual Mono" }), error)
        && set.add (ParamSpec::choice (io, "Stereo / M-S", { "Stereo > Stereo", "Stereo > M/S", "M/S > Stereo" }), error)
        && set.add (volume (volumeL, "Volume L"), error)
        && set.add (volume (volumeR, "Volume R"), error)
        && set.add (ParamSpec::toggle (phaseL, "Phase Invert L"), error)
        && set.add (ParamSpec::toggle (phaseR, "Phase Invert R"), error)
        && set.add (ParamSpec::toggle (swap, "Swap L/R"), error)
        && set.add (ParamSpec::toggle (mono, "Mono Sum"), error)
        && set.add (pct (width, "Width", 0.0, 200.0, 100.0), error)
        && set.add (ParamSpec::choice (panMode, "Pan Mode", { "Single", "Dual" }), error)
        && set.add (pct (pan, "Pan", -100.0, 100.0, 0.0), error)
        && set.add (pct (panL, "Pan L", -100.0, 100.0, -100.0), error)
        && set.add (pct (panR, "Pan R", -100.0, 100.0, 100.0), error)
        && set.add (ParamSpec::choice (panLaw, "Pan Law", { "Balance", "True Pan" }), error)
        && set.add (ParamSpec::continuous (delayL, "Delay L", Unit::Milliseconds, 0.0, maxDelayMs, 0.0), error)
        && set.add (ParamSpec::continuous (delayR, "Delay R", Unit::Milliseconds, 0.0, maxDelayMs, 0.0), error)
        && set.add (ParamSpec::toggle (dc, "DC Filter"), error);
}

void UtilityModule::bind (const ParameterSet& set)
{
    for (ParamId id = utility::mode; id <= utility::dc; ++id) idx_[id - utility::mode] = set.indexOf (id);
}

void UtilityModule::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto size = static_cast<std::size_t> (std::ceil (utility::maxDelayMs * 0.001 * sampleRate_)) + 4;
    chL_.line.assign (size, 0.0f);
    chR_.line.assign (size, 0.0f);
    scratch_.assign (static_cast<std::size_t> (std::max (1, maxBlockSize)), 0.0f);
    dcR_ = static_cast<float> (1.0 - 2.0 * kPi * 5.0 / sampleRate_);
    corrAlpha_ = static_cast<float> (std::exp (-1.0 / (0.3 * sampleRate_)));
    reset();
}

void UtilityModule::reset() noexcept
{
    for (auto* ch : { &chL_, &chR_ })
    {
        std::fill (ch->line.begin(), ch->line.end(), 0.0f);
        ch->pos = 0;
        ch->delay = 0.0f;
        ch->dcX = ch->dcY = 0.0f;
    }
    lastPre_ = lastPost_ = Matrix {};
    primed_ = false;
    corrLL_ = corrRR_ = corrLR_ = 0.0f;
    for (auto& m : meters_) m.store (0.0f, std::memory_order_relaxed);
}

UtilityModule::Matrix UtilityModule::mul (const Matrix& x, const Matrix& y) noexcept
{
    return { y.a * x.a + y.b * x.c, y.a * x.b + y.b * x.d, y.c * x.a + y.d * x.c, y.c * x.b + y.d * x.d };
}

// Retard (interpolé linéairement, glissant du retard précédent au nouveau sur le bloc) puis filtre DC, sur un canal.
void UtilityModule::channelPass (Channel& ch, float* x, int n, float delayTarget, bool dcOn) noexcept
{
    const std::size_t size = ch.line.size();
    if (size < 8) return;
    const float maxDelay = static_cast<float> (size - 3);
    delayTarget = std::clamp (delayTarget, 0.0f, maxDelay);
    const float step = (delayTarget - ch.delay) / static_cast<float> (n);
    float d = ch.delay;
    const bool delayed = d > 0.0f || delayTarget > 0.0f;
    std::size_t pos = ch.pos;
    float* line = ch.line.data();
    for (int i = 0; i < n; ++i)
    {
        line[pos] = x[i];
        if (delayed)
        {
            const auto di = static_cast<std::size_t> (d);
            const float fr = d - static_cast<float> (di);
            std::size_t r0 = pos >= di ? pos - di : pos + size - di;
            std::size_t r1 = r0 > 0 ? r0 - 1 : size - 1;
            x[i] = line[r0] * (1.0f - fr) + line[r1] * fr;
            d += step;
        }
        if (++pos >= size) pos = 0;
    }
    ch.pos = pos;
    ch.delay = delayTarget;
    if (dcOn)
    {
        float xp = ch.dcX, yp = ch.dcY;
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];
            const float out = in - xp + dcR_ * yp + 1e-15f;
            xp = in;
            yp = out;
            x[i] = out;
        }
        ch.dcX = xp;
        ch.dcY = yp;
    }
    else
        ch.dcX = ch.dcY = 0.0f;
}

void UtilityModule::process (float* l, float* r, int n, const ParameterSet& p) noexcept
{
    using namespace utility;
    if (n <= 0) return;
    auto v = [&] (ParamId id) { return p.getAt (idx_[id - mode]); };
    const bool dual = v (mode) >= 0.5;
    const int ioMode = dual ? 0 : static_cast<int> (v (io) + 0.5);
    const bool phL = v (phaseL) >= 0.5, phR = v (phaseR) >= 0.5;

    // --- matrices du traitement linéaire : avant le retard (décodage, phase, échange) et après (largeur, mono, pan, volume)
    Matrix pre;
    if (ioMode == 2) pre = { 1.0f, 1.0f, 1.0f, -1.0f }; // M/S -> L/R : L = M + S, R = M - S
    pre = mul (pre, { phL ? -1.0f : 1.0f, 0.0f, 0.0f, phR ? -1.0f : 1.0f });
    if (! dual && v (swap) >= 0.5) pre = mul (pre, { 0.0f, 1.0f, 1.0f, 0.0f });

    Matrix post;
    if (! dual)
    {
        if (v (mono) >= 0.5) post = mul (post, { 0.5f, 0.5f, 0.5f, 0.5f });
        else
        {
            const auto w = static_cast<float> (std::clamp (v (width), 0.0, 200.0) / 100.0);
            post = mul (post, { 0.5f * (1.0f + w), 0.5f * (1.0f - w), 0.5f * (1.0f - w), 0.5f * (1.0f + w) });
        }
        if (v (panMode) < 0.5)
        {
            float gl = 1.0f, gr = 1.0f;
            const auto law = v (panLaw) >= 0.5 ? mixer::PanMode::True : mixer::PanMode::Balance;
            mixer::panGains (law, static_cast<float> (v (pan) / 100.0), gl, gr);
            post = mul (post, { gl, 0.0f, 0.0f, gr });
        }
        else
        {
            // Pan double : chaque canal d'entrée est une source placée à sa position (puissance constante).
            const auto tl = static_cast<float> ((std::clamp (v (panL), -100.0, 100.0) / 100.0 + 1.0) * kPi / 4.0);
            const auto tr = static_cast<float> ((std::clamp (v (panR), -100.0, 100.0) / 100.0 + 1.0) * kPi / 4.0);
            post = mul (post, { std::cos (tl), std::cos (tr), std::sin (tl), std::sin (tr) });
        }
    }
    const float volL = mixer::dbToGain (std::clamp (v (volumeL), -100.0, 12.0));
    const float volR = dual ? mixer::dbToGain (std::clamp (v (volumeR), -100.0, 12.0)) : volL;
    post = mul (post, { volL, 0.0f, 0.0f, volR });
    const Matrix enc = ioMode == 1 ? Matrix { 0.5f, 0.5f, 0.5f, -0.5f } : Matrix {}; // L/R -> M/S
    if (! primed_) { lastPre_ = pre; lastPost_ = post; primed_ = true; }

    // --- mesures d'entrée
    float inPkL = 0, inPkR = 0, inSqL = 0, inSqR = 0;
    for (int i = 0; i < n; ++i)
    {
        inPkL = std::max (inPkL, std::abs (l[i])); inPkR = std::max (inPkR, std::abs (r[i]));
        inSqL += l[i] * l[i]; inSqR += r[i] * r[i];
    }

    // --- avant le retard
    {
        const float inv = 1.0f / static_cast<float> (n);
        Matrix m = lastPre_;
        const float sa = (pre.a - m.a) * inv, sb = (pre.b - m.b) * inv, sc = (pre.c - m.c) * inv, sd = (pre.d - m.d) * inv;
        for (int i = 0; i < n; ++i)
        {
            const float x = l[i], y = r[i];
            l[i] = m.a * x + m.b * y;
            r[i] = m.c * x + m.d * y;
            m.a += sa; m.b += sb; m.c += sc; m.d += sd;
        }
        lastPre_ = pre;
    }

    // --- retard et DC (réglages liés en mode Stéréo : le retard de gauche vaut pour les deux canaux)
    const double msToSamples = 0.001 * sampleRate_;
    const float dlL = static_cast<float> (std::clamp (v (delayL), 0.0, maxDelayMs) * msToSamples);
    const float dlR = dual ? static_cast<float> (std::clamp (v (delayR), 0.0, maxDelayMs) * msToSamples) : dlL;
    const bool dcOn = v (dc) >= 0.5;
    channelPass (chL_, l, n, dlL, dcOn);
    channelPass (chR_, r, n, dlR, dcOn);

    // --- après le retard, corrélation, puis encodage M/S
    {
        const float inv = 1.0f / static_cast<float> (n);
        Matrix m = lastPost_;
        const float sa = (post.a - m.a) * inv, sb = (post.b - m.b) * inv, sc = (post.c - m.c) * inv, sd = (post.d - m.d) * inv;
        const float al = corrAlpha_, be = 1.0f - al;
        for (int i = 0; i < n; ++i)
        {
            const float x = l[i], y = r[i];
            const float lo = m.a * x + m.b * y, ro = m.c * x + m.d * y;
            l[i] = lo;
            r[i] = ro;
            corrLL_ = al * corrLL_ + be * lo * lo;
            corrRR_ = al * corrRR_ + be * ro * ro;
            corrLR_ = al * corrLR_ + be * lo * ro;
            m.a += sa; m.b += sb; m.c += sc; m.d += sd;
        }
        lastPost_ = post;
    }
    if (ioMode == 1)
        for (int i = 0; i < n; ++i)
        {
            const float x = l[i], y = r[i];
            l[i] = enc.a * x + enc.b * y;
            r[i] = enc.c * x + enc.d * y;
        }

    // --- mesures de sortie
    float outPkL = 0, outPkR = 0, outSqL = 0, outSqR = 0;
    for (int i = 0; i < n; ++i)
    {
        outPkL = std::max (outPkL, std::abs (l[i])); outPkR = std::max (outPkR, std::abs (r[i]));
        outSqL += l[i] * l[i]; outSqR += r[i] * r[i];
    }
    const float inv = 1.0f / static_cast<float> (n);
    auto put = [this] (int i, float x) { meters_[static_cast<std::size_t> (i)].store (x, std::memory_order_relaxed); };
    put (kInPeakL, inPkL); put (kInPeakR, inPkR); put (kOutPeakL, outPkL); put (kOutPeakR, outPkR);
    put (kInRmsL, std::sqrt (inSqL * inv)); put (kInRmsR, std::sqrt (inSqR * inv));
    put (kOutRmsL, std::sqrt (outSqL * inv)); put (kOutRmsR, std::sqrt (outSqR * inv));
    const float denom = std::sqrt (corrLL_ * corrRR_);
    put (kCorrelation, denom > 1e-12f ? std::clamp (corrLR_ / denom, -1.0f, 1.0f) : 0.0f);
}
} // namespace neptune::core
