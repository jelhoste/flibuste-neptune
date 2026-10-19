#include "neptune/core/SpectrumAnalyzer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace neptune::core
{
// ------------------------------------------------------------------------------------------------ FFT
Fft::Fft (int order) : n_ (static_cast<std::size_t> (1) << order), rev_ (n_), cosT_ (n_ / 2), sinT_ (n_ / 2)
{
    for (std::size_t i = 0; i < n_; ++i)
    {
        std::uint32_t r = 0;
        for (int b = 0; b < order; ++b) if (i & (static_cast<std::size_t> (1) << b)) r |= 1u << (order - 1 - b);
        rev_[i] = r;
    }
    for (std::size_t k = 0; k < n_ / 2; ++k)
    {
        const double a = 2.0 * 3.14159265358979323846 * static_cast<double> (k) / static_cast<double> (n_);
        cosT_[k] = static_cast<float> (std::cos (a));
        sinT_[k] = static_cast<float> (-std::sin (a));
    }
}

void Fft::transform (float* re, float* im) const noexcept
{
    for (std::size_t i = 0; i < n_; ++i)
    {
        const std::size_t j = rev_[i];
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (std::size_t len = 2; len <= n_; len <<= 1)
    {
        const std::size_t half = len / 2, step = n_ / len;
        for (std::size_t i = 0; i < n_; i += len)
            for (std::size_t j = 0; j < half; ++j)
            {
                const float wr = cosT_[j * step], wi = sinT_[j * step];
                const std::size_t a = i + j, b = a + half;
                const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
    }
}

// ------------------------------------------------------------------------------------------------ Makima
namespace
{
float makimaD (float d0, float d1, float d2, float d3)
{
    const float w1 = std::fabs (d3 - d2) + std::fabs (d3 + d2) * 0.5f, w2 = std::fabs (d1 - d0) + std::fabs (d1 + d0) * 0.5f;
    const float sum = w1 + w2;
    if (sum <= std::numeric_limits<float>::epsilon()) return (d1 + d2) * 0.5f;
    const float w = w1 / sum;
    return w * d1 + (1.0f - w) * d2;
}
} // namespace

void Makima::prepare (const float* x, const float* y, std::size_t n, float leftSlope, float rightSlope)
{
    x_ = x; y_ = y; n_ = n;
    d_.assign (n, 0.0f);
    delta_.assign (n > 0 ? n - 1 : 0, 0.0f);
    if (n == 0) return;
    d_[0] = leftSlope;
    if (n == 1) return;
    for (std::size_t i = 0; i + 1 < n; ++i) delta_[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
    d_[n - 1] = rightSlope;
    if (n == 2) return;
    const float leftDelta = 2.0f * delta_[0] - delta_[1], rightDelta = 2.0f * delta_[delta_.size() - 1] - delta_[delta_.size() - 2];
    if (n == 3) { d_[1] = makimaD (leftDelta, delta_[0], delta_[1], rightDelta); return; }
    d_[1] = makimaD (leftDelta, delta_[0], delta_[1], delta_[2]);
    for (std::size_t i = 2; i + 2 < n; ++i) d_[i] = makimaD (delta_[i - 2], delta_[i - 1], delta_[i], delta_[i + 1]);
    d_[n - 2] = makimaD (delta_[n - 4], delta_[n - 3], delta_[n - 2], rightDelta);
}

void Makima::eval (const float* xo, float* yo, std::size_t m) const
{
    if (m == 0 || n_ == 0) return;
    if (n_ == 1) { for (std::size_t i = 0; i < m; ++i) yo[i] = y_[0]; return; }
    std::size_t cur = 0, start = 0, end = m;
    while (start < m && xo[start] <= x_[0]) yo[start++] = y_[0];
    while (end > start && xo[end - 1] >= x_[n_ - 1]) yo[--end] = y_[n_ - 1];
    for (std::size_t i = start; i < end; ++i)
    {
        while (cur + 2 < n_ && xo[i] >= x_[cur + 1]) ++cur;
        const float h = x_[cur + 1] - x_[cur], t = (xo[i] - x_[cur]) / h;
        const float h00 = (1.0f + 2.0f * t) * (1.0f - t) * (1.0f - t), h10 = t * (1.0f - t) * (1.0f - t), h01 = t * t * (3.0f - 2.0f * t), h11 = t * t * (t - 1.0f);
        yo[i] = h00 * y_[cur] + h10 * h * d_[cur] + h01 * y_[cur + 1] + h11 * h * d_[cur + 1];
    }
}

// ------------------------------------------------------------------------------------------------ analyseur
int SpectrumAnalyzer::scaledOrder (double sr)
{
    if (sr <= 12500.0) return 10;
    if (sr <= 25000.0) return 11;
    if (sr <= 50000.0) return 12;
    if (sr <= 100000.0) return 13;
    return 14;
}

void SpectrumAnalyzer::prepare (double sampleRate, bool highQuality)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    highQuality_ = highQuality;
    // FFT moyenne bornée à 8192 points : l'anneau d'analyse garde au plus 32768 échantillons par canal (la FFT grave en a 4 fois plus que la moyenne)
    const int mid = std::clamp (scaledOrder (sampleRate_), 10, 13);
    const int orders[3] = { mid + 2, mid, mid - 2 };
    const std::size_t nMid = static_cast<std::size_t> (1) << mid;
    for (int i = 0; i < 3; ++i)
    {
        auto& r = res_[static_cast<std::size_t> (i)];
        if (! highQuality_ && i != 1) { r = Resolution{}; continue; }
        r.fft = std::make_unique<Fft> (orders[i]);
        const std::size_t n = r.fft->size();
        r.window.resize (n);
        for (std::size_t k = 0; k < n; ++k)
            r.window[k] = static_cast<float> ((0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * static_cast<double> (k) / static_cast<double> (n))) * 4.0 / static_cast<double> (n));
        r.noiseScale = static_cast<float> (n) / static_cast<float> (nMid);
        r.power.assign (n / 2 + 1, 0.0f);
    }
    history_ = highQuality_ ? res_[0].fft->size() : res_[1].fft->size();
    // grille de fréquences
    frequencies_.clear();
    if (! highQuality_)
    {
        const std::size_t n = res_[1].fft->size();
        const double df = sampleRate_ / static_cast<double>(n);
        for (std::size_t i = 0; i <= n / 2; ++i) frequencies_.push_back (static_cast<float> (static_cast<double> (i) * df));
    }
    else
    {
        auto append = [this] (std::size_t n, float lowerExcl, float upperIncl)
        {
            const double df = sampleRate_ / static_cast<double>(n);
            for (std::size_t b = 0; b <= n / 2; ++b)
            {
                const float f = static_cast<float> (static_cast<double> (b) * df);
                if (f > lowerExcl && f <= upperIncl) frequencies_.push_back (f);
            }
        };
        append (res_[0].fft->size(), -1.0f, 400.0f);
        append (res_[1].fft->size(), 400.0f, 4000.0f);
        append (res_[2].fft->size(), 4000.0f, std::numeric_limits<float>::infinity());
    }
    const std::size_t np = frequencies_.size();
    for (auto& v : spectrumDb_) v.assign (np, -240.0f);
    for (auto& v : decayState_) v.assign (np, -240.0f);
    for (auto& s : hist_) { s.assign (2, std::vector<float> (history_, 0.0f)); }
    for (auto& a : hpX_) a = { 0.0f, 0.0f };
    for (auto& a : hpY_) a = { 0.0f, 0.0f };
    const std::size_t maxN = highQuality_ ? res_[0].fft->size() : nMid;
    re_.assign (maxN, 0.0f);
    im_.assign (maxN, 0.0f);
    blendLow_.assign (highQuality_ ? res_[0].power.size() : 0, 0.0f);
    blendMid_.assign (res_[1].power.size(), 0.0f);
    blendHigh_.assign (highQuality_ ? res_[2].power.size() : 0, 0.0f);
    cum_.assign (maxN / 2 + 2, 0.0);
    prepared_ = true;
    tiltShift_.assign (np, 0.0f);
    configureSmoothing();
    setSettings (settings_);
}

void SpectrumAnalyzer::configureSmoothing()
{
    for (auto& r : res_)
    {
        if (! r.fft) continue;
        const std::size_t bins = r.fft->size() / 2 + 1;
        r.lowIdx.assign (bins, 0);
        r.highIdx.assign (bins, 0);
        r.countReq.assign (bins, 1.0f);
        const double fftSize = static_cast<double> (r.fft->size());
        if (settings_.smoothType == 0)
        {
            const double factor = std::pow (2.0, kSpectrumSmoothOct[static_cast<std::size_t> (std::clamp (settings_.smoothOct, 0, 4))] * 0.5), rep = 1.0 / factor;
            for (std::size_t i = 0; i < bins; ++i)
            {
                r.lowIdx[i] = static_cast<std::size_t> (std::round (static_cast<double> (i) * rep));
                r.highIdx[i] = std::min (bins, static_cast<std::size_t> (std::round (static_cast<double> (i) * factor) + 1.0));
                r.countReq[i] = 1.0f / static_cast<float> (std::max<std::size_t> (1, r.highIdx[i] - r.lowIdx[i]));
            }
        }
        else
        {
            const double erb = kSpectrumSmoothErb[static_cast<std::size_t> (std::clamp (settings_.smoothErb, 0, 4))] * 0.5;
            const double deltaF = sampleRate_ / fftSize, constTerm = 24.7 / deltaF, maxIdx = static_cast<double> (bins - 1);
            for (std::size_t i = 0; i < bins; ++i)
            {
                const double half = (0.107939 * static_cast<double> (i) + constTerm) * erb;
                r.lowIdx[i] = static_cast<std::size_t> (std::round (std::max (0.0, static_cast<double> (i) - half)));
                r.highIdx[i] = std::min (bins, static_cast<std::size_t> (std::round (std::min (maxIdx, static_cast<double> (i) + half)) + 1.0));
                r.countReq[i] = 1.0f / static_cast<float> (std::max<std::size_t> (1, r.highIdx[i] - r.lowIdx[i]));
            }
        }
    }
}

void SpectrumAnalyzer::setSettings (const SpectrumSettings& s)
{
    const bool smoothChanged = s.smoothType != settings_.smoothType || s.smoothOct != settings_.smoothOct || s.smoothErb != settings_.smoothErb;
    settings_ = s;
    if (! prepared_) return;
    if (smoothChanged) configureSmoothing();
    const double slope = kSpectrumTilts[static_cast<std::size_t> (std::clamp (settings_.tiltIdx, 0, 4))];
    for (std::size_t i = 1; i < tiltShift_.size(); ++i) tiltShift_[i] = static_cast<float> (std::log2 (static_cast<double> (frequencies_[i]) / 1000.0) * slope);
    if (tiltShift_.size() > 1) tiltShift_[0] = tiltShift_[1];
    decayRefresh_ = 0.0;   // recalculé à la prochaine analyse
}

void SpectrumAnalyzer::reset()
{
    for (auto& s : hist_) for (auto& c : s) std::fill (c.begin(), c.end(), 0.0f);
    for (auto& a : hpX_) a = { 0.0f, 0.0f };
    for (auto& a : hpY_) a = { 0.0f, 0.0f };
    for (auto& v : decayState_) std::fill (v.begin(), v.end(), -240.0f);
    for (auto& v : spectrumDb_) std::fill (v.begin(), v.end(), -240.0f);
}

void SpectrumAnalyzer::pull (int source, const AnalyzerRing& ring, int ringStream, std::uint64_t& readPos)
{
    if (! prepared_) return;
    const std::uint64_t w = ring.writePos();
    if (readPos > w) readPos = w;
    std::size_t n = static_cast<std::size_t> (w - readPos);
    if (n > history_) { readPos = w - history_; n = history_; }
    if (n == 0) return;
    const auto src = static_cast<std::size_t> (source);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& h = hist_[src][static_cast<std::size_t> (ch)];
        std::memmove (h.data(), h.data() + n, (history_ - n) * sizeof (float));
        float x = hpX_[src][static_cast<std::size_t> (ch)], y = hpY_[src][static_cast<std::size_t> (ch)];
        for (std::size_t i = 0; i < n; ++i)
        {
            const float in = ring.sample (ringStream, ch, readPos + i);
            y = in - x + 0.9999f * y;   // supprime le continu
            h[history_ - n + i] = y;
            x = in;
        }
        hpX_[src][static_cast<std::size_t> (ch)] = x;
        hpY_[src][static_cast<std::size_t> (ch)] = y;
    }
    readPos = w;
}

// puissance (N/2 + 1 valeurs) de la source pour la résolution donnée, selon les canaux choisis
void SpectrumAnalyzer::forward (int source, Resolution& r)
{
    const std::size_t n = r.fft->size(), off = history_ - n;
    const auto& L = hist_[static_cast<std::size_t> (source)][0];
    const auto& R = hist_[static_cast<std::size_t> (source)][1];
    float* re = re_.data();
    float* im = im_.data();
    const float k = 0.70710678f;
    switch (settings_.channels)
    {
        case SpectrumChannels::Stereo:
            for (std::size_t i = 0; i < n; ++i) { re[i] = L[off + i] * r.window[i]; im[i] = R[off + i] * r.window[i]; }
            r.fft->transform (re, im);
            for (std::size_t b = 0; b <= n / 2; ++b)
            {
                const std::size_t nb = (n - b) & (n - 1);
                const float a = 0.5f * (re[b] + re[nb]), bq = 0.5f * (im[b] - im[nb]);     // gauche
                const float c = 0.5f * (im[b] + im[nb]), d = 0.5f * (re[b] - re[nb]);      // droite
                r.power[b] = a * a + bq * bq + c * c + d * d;
            }
            return;
        case SpectrumChannels::Left: for (std::size_t i = 0; i < n; ++i) re[i] = L[off + i] * r.window[i]; break;
        case SpectrumChannels::Right: for (std::size_t i = 0; i < n; ++i) re[i] = R[off + i] * r.window[i]; break;
        case SpectrumChannels::Mid: for (std::size_t i = 0; i < n; ++i) re[i] = (L[off + i] + R[off + i]) * k * r.window[i]; break;
        case SpectrumChannels::Side: for (std::size_t i = 0; i < n; ++i) re[i] = (L[off + i] - R[off + i]) * k * r.window[i]; break;
    }
    std::fill (im, im + n, 0.0f);
    r.fft->transform (re, im);
    for (std::size_t b = 0; b <= n / 2; ++b) r.power[b] = re[b] * re[b] + im[b] * im[b];
}

// moyenne glissante appliquée deux fois, sur la puissance
void SpectrumAnalyzer::smooth (Resolution& r, std::vector<double>& cum)
{
    const std::size_t bins = r.power.size();
    for (int pass = 0; pass < 2; ++pass)
    {
        cum[0] = 0.0;
        for (std::size_t i = 0; i < bins; ++i) cum[i + 1] = cum[i] + static_cast<double> (r.power[i]);
        for (std::size_t i = 0; i < bins; ++i) r.power[i] = static_cast<float> (cum[r.highIdx[i]] - cum[r.lowIdx[i]]) * r.countReq[i];
    }
}

void SpectrumAnalyzer::analyze (int source, double refreshRate)
{
    if (! prepared_ || frequencies_.size() < 2) return;
    auto& out = spectrumDb_[static_cast<std::size_t> (source)];
    if (! highQuality_)
    {
        auto& r = res_[1];
        forward (source, r);
        smooth (r, cum_);
        std::copy (r.power.begin(), r.power.end(), out.begin());
    }
    else
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            auto& r = res_[i];
            forward (source, r);
            for (auto& v : r.power) v *= r.noiseScale;   // même densité de puissance du bruit blanc dans les trois résolutions
            smooth (r, cum_);
        }
        auto sampleAt = [this] (const std::vector<float>& sp, float f)
        {
            const double fftSize = static_cast<double> ((sp.size() - 1) * 2);
            const double bin = std::clamp (static_cast<double> (f) * fftSize / sampleRate_, 0.0, static_cast<double> (sp.size() - 1));
            const auto lower = static_cast<std::size_t> (bin);
            if (lower + 1 >= sp.size()) return sp.back();
            const float mix = static_cast<float> (bin - static_cast<double> (lower));
            return sp[lower] + (sp[lower + 1] - sp[lower]) * mix;
        };
        const auto& low = res_[0].power; const auto& mid = res_[1].power; const auto& high = res_[2].power;
        for (std::size_t i = 0; i < frequencies_.size(); ++i)
        {
            const float f = frequencies_[i];
            if (f <= 200.0f) out[i] = sampleAt (low, f);
            else if (f < 400.0f) { const float m = (f - 200.0f) / 200.0f; const float a = sampleAt (low, f), b = sampleAt (mid, f); out[i] = a + (b - a) * m; }
            else if (f <= 2000.0f) out[i] = sampleAt (mid, f);
            else if (f < 4000.0f) { const float m = (f - 2000.0f) / 2000.0f; const float a = sampleAt (mid, f), b = sampleAt (high, f); out[i] = a + (b - a) * m; }
            else out[i] = sampleAt (high, f);
        }
    }
    // dB, pente de compensation, retombée
    if (decayRefresh_ != refreshRate)
    {
        decayRefresh_ = refreshRate;
        constexpr float floorDb = -120.0f, startDb = 0.0f;
        const float minDb = settings_.topDb + settings_.rangeDb;
        const double speed = kSpectrumSpeeds[static_cast<std::size_t> (std::clamp (settings_.speedIdx, 0, 4))] * 1.1;   // 1,1 : réglage de vitesse supplémentaire par défaut de ZL (1 x 1 + 0,1)
        const float decaySecond = static_cast<float> (0.15 / speed);
        if (decaySecond <= 0.0f || minDb <= floorDb) decayP_ = 1.0f;
        else
        {
            const float frames = static_cast<float> (refreshRate) * decaySecond, ratio = (minDb - floorDb) / (startDb - floorDb);
            decayP_ = std::clamp (1.0f - std::pow (ratio, 1.0f / frames), 0.0f, 1.0f);
        }
    }
    auto& state = decayState_[static_cast<std::size_t> (source)];
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const float db = 4.3429448190325182765f * std::log (std::max (out[i], 1.0e-24f)) + tiltShift_[i];
        const float smoothed = state[i] + decayP_ * (db - state[i]);
        const float v = std::max (db, smoothed);
        state[i] = v;
        out[i] = v;
    }
}
} // namespace neptune::core
