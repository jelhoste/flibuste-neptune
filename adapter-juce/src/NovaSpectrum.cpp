#include "NovaSpectrum.h"

#include <algorithm>
#include <cmath>

using namespace neptune::core;
namespace eq = neptune::core::equalizer;

namespace nova
{
namespace
{
std::atomic<bool> gThreading { true };
constexpr float kTopDb[7] = { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f };
constexpr float kRangeDb[3] = { -60.0f, -72.0f, -96.0f };
} // namespace

SpectrumEngine::SpectrumEngine (ParamHost& h, const EqualizerModule* m) : juce::Thread ("spectrum"), host_ (h), module_ (m)
{
    if (gThreading.load() && module_ != nullptr) startThread (juce::Thread::Priority::low);
}

SpectrumEngine::~SpectrumEngine()
{
    if (isThreadRunning()) stopThread (1000);
    if (module_ != nullptr) module_->analyzer().setActive (false);
}

void SpectrumEngine::setThreading (bool enabled) { gThreading.store (enabled); }

void SpectrumEngine::setRunning (bool on)
{
    if (module_ == nullptr || running_.load() == on) return;
    running_.store (on);
    module_->analyzer().setActive (on);
}

bool SpectrumEngine::pullFrame (Frame& out)
{
    const juce::ScopedLock sl (lock_);
    if (frame_.version == copiedVersion_) return false;
    out = frame_;
    copiedVersion_ = frame_.version;
    return true;
}

void SpectrumEngine::run()
{
    while (! threadShouldExit())
    {
        if (running_.load()) stepOnce();
        wait (33);
    }
}

void SpectrumEngine::stepOnce()
{
    if (module_ == nullptr) return;
    auto& ring = module_->analyzer();
    const auto& p = host_.parameters();
    auto idx = [&p] (ParamId id, int hi) { return juce::jlimit (0, hi, static_cast<int> (std::lround (p.get (id)))); };
    const double sr = ring.sampleRate();
    const bool hq = idx (eq::spQuality, 1) == 1;
    if (! prepared_ || std::fabs (sr - preparedRate_) > 0.1 || hq != preparedHq_)
    {
        analyzer_.prepare (sr, hq);
        preparedRate_ = sr;
        preparedHq_ = hq;
        prepared_ = true;
        readPos_[0] = readPos_[1] = ring.writePos();
    }
    SpectrumSettings s;
    s.highQuality = hq;
    s.speedIdx = idx (eq::spSpeed, 4);
    s.tiltIdx = idx (eq::spTilt, 4);
    s.smoothType = idx (eq::spSmoothType, 1);
    s.smoothOct = idx (eq::spSmoothOct, 4);
    s.smoothErb = idx (eq::spSmoothErb, 4);
    s.topDb = kTopDb[idx (eq::spTop, 6)];
    s.rangeDb = kRangeDb[idx (eq::spMin, 2)];
    s.channels = static_cast<SpectrumChannels> (idx (eq::spChannels, 4));
    analyzer_.setSettings (s);
    const bool show[2] = { p.get (eq::spShowIn) >= 0.5, p.get (eq::spShowOut) >= 0.5 };
    for (int src = 0; src < 2; ++src)
        if (show[src])
        {
            analyzer_.pull (src, ring, src == 0 ? eq::kSpIn : eq::kSpOut, readPos_[static_cast<std::size_t> (src)]);
            analyzer_.analyze (src, 30.0);
        }
        else readPos_[static_cast<std::size_t> (src)] = ring.writePos();
    const juce::ScopedLock sl (lock_);
    if (frame_.freqs.size() != analyzer_.frequencies().size() || frame_.highQuality != hq) frame_.freqs = analyzer_.frequencies();
    frame_.highQuality = hq;
    for (int src = 0; src < 2; ++src) frame_.db[static_cast<std::size_t> (src)] = show[src] ? analyzer_.spectrumDb (src) : std::vector<float>();
    ++frame_.version;
}

juce::Path makeSpectrumPath (const SpectrumEngine::Frame& f, int source, const std::function<float (float)>& xOfHz, const std::function<float (float)>& yOfDb,
                             float bottomY, float rightX)
{
    juce::Path path;
    const auto& db = f.db[static_cast<std::size_t> (source)];
    const std::size_t m = f.freqs.size();
    if (db.size() != m || m < 4) return path;
    std::vector<float> xs, ys;
    xs.reserve (m);
    ys.reserve (m);
    for (std::size_t i = 0; i < m; ++i)
    {
        const float hz = i == 0 ? f.freqs[1] * 0.5f : f.freqs[i];
        xs.push_back (xOfHz (hz));
        ys.push_back (yOfDb (db[i]));
        if (xs.back() > rightX) break;
    }
    const std::size_t num = xs.size();
    if (num < 3) return path;
    path.startNewSubPath (xs[0] - 0.1f, bottomY);
    std::size_t first = 0;
    constexpr std::size_t kInter = 64;
    if (! f.highQuality && num >= kInter / 2 + 2)
    {
        Makima mk;
        mk.prepare (xs.data(), ys.data(), kInter / 2 + 2, 0.0f, 0.0f);
        std::array<float, kInter + 2> ix {}, iy {};
        ix[0] = xs[0];
        ix[kInter] = xs[kInter / 2];
        ix[kInter + 1] = xs[kInter / 2 + 1];
        const float dx = (xs[kInter / 2 - 1] - xs[0]) / static_cast<float> (kInter - 1);
        for (std::size_t i = 1; i < kInter; ++i) ix[i] = ix[i - 1] + dx;
        mk.eval (ix.data(), iy.data(), ix.size());
        path.lineTo (ix[0], iy[0]);
        for (std::size_t j = 1; j < kInter; j += 2) path.quadraticTo (ix[j], iy[j], ix[j + 1], iy[j + 1]);
        first = kInter / 2;
    }
    path.lineTo (xs[first], ys[first]);
    for (std::size_t j = first + 1; j < num; ++j) path.lineTo (xs[j], ys[j]);
    path.lineTo (xs[num - 1] + 0.1f, bottomY);
    path.closeSubPath();
    return path;
}
} // namespace nova
