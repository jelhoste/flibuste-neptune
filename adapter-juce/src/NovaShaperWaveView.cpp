#include "NovaShaperWaveView.h"

#include <algorithm>
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace sh = neptune::core::shaper;

namespace nova
{
namespace
{
constexpr int kMaxPoints = 300, kPausedThreshold = 6, kTooMuchResetThreshold = 64;
constexpr double kDisplayDelayMs = 50.0;   // l'hôte livre l'audio par rafales : une petite réserve évite les trous (donc les saccades)
constexpr int kPointsPerSecond[4] = { 40, 30, 20, 15 };
constexpr double kTimeLength[4] = { 6.0, 9.0, 12.0, 18.0 };

// Nombre de points par seconde : le plus proche multiple de la valeur préférée qui divise la fréquence d'échantillonnage (comme ZL)
int choosePointsPerSecond (int sampleRate, int preferred, int timeLength)
{
    if (sampleRate <= 0 || preferred <= 0 || timeLength <= 0) return preferred;
    const int maxPps = kMaxPoints / timeLength;
    for (int candidate = preferred; candidate <= maxPps; ++candidate)
        if (sampleRate % candidate == 0) return candidate;
    return preferred;
}
} // namespace

ShaperWaveView::ShaperWaveView (const Skin& s, ParamHost& h, const ShaperModule* m)
    : skin_ (s), host_ (h), module_ (m), vblank_ (this, [this] { step (juce::Time::getMillisecondCounterHiRes() * 0.001); })
{
    if (module_ != nullptr) module_->analyzer().setActive (true);
    readSettings();
}

ShaperWaveView::~ShaperWaveView()
{
    if (module_ != nullptr) module_->analyzer().setActive (false);
}

void ShaperWaveView::showSettings (juce::Component* anchor, const Skin& skin, ParamHost& host, const std::function<juce::String (const char*)>& tx)
{
    std::vector<SettingItem> items = {
        { sh::anTime, tx ("nv.an_time"), { "6 s", "9 s", "12 s", "18 s" } },
        { sh::anMove, tx ("nv.an_scroll"), { "Sync", "Slow", "Roll" } } };
    showSettingsPopup (anchor, skin, host, tx ("nv.an_title"), std::move (items));
}

void ShaperWaveView::readSettings()
{
    const auto& p = host_.parameters();
    auto idx = [&p] (ParamId id, int hi) { return juce::jlimit (0, hi, static_cast<int> (std::lround (p.get (id)))); };
    const int oldTime = timeIdx_, oldMove = moveType_;
    timeIdx_ = idx (sh::anTime, 3);
    moveType_ = idx (sh::anMove, 2);
    const float oldRange = range_;
    range_ = static_cast<float> (p.get (sh::range));
    if (oldTime != timeIdx_ || oldMove != moveType_) sampleRate_ = 0.0;   // reconfiguration au prochain pas
    if (oldRange != range_) dirty_ = true;
}

juce::Rectangle<float> ShaperWaveView::plotArea() const
{
    const auto b = getLocalBounds().toFloat();
    return { b.getX() + skin_.px (40.0f), b.getY() + skin_.px (10.0f), b.getWidth() - skin_.px (40.0f + 10.0f), b.getHeight() - skin_.px (10.0f + 18.0f) };
}

void ShaperWaveView::configure (double sr, int maxBlock)
{
    sampleRate_ = sr;
    maxBlock_ = maxBlock;
    timeLength_ = kTimeLength[timeIdx_];
    const int rounded = static_cast<int> (std::lround (sr));
    pointsPerSecond_ = choosePointsPerSecond (rounded, kPointsPerSecond[timeIdx_], static_cast<int> (std::lround (timeLength_)));
    samplesPerPoint_ = std::max (1, rounded / pointsPerSecond_);
    numPoints_ = static_cast<std::size_t> (pointsPerSecond_) * static_cast<std::size_t> (timeLength_);
    secondPerPoint_ = timeLength_ / static_cast<double> (numPoints_);
    const std::size_t size = moveType_ == 2 ? numPoints_ : numPoints_ + 2;
    for (auto* v : { &xs_, &inV_, &outV_, &fastV_, &slowV_, &gainV_ }) v->assign (size, 0.0f);
    first_ = true;
    missing_ = 0;
    rollNext_ = 0;
    dirty_ = true;
}

// Un point : crête des deux canaux pour le signal et les enveloppes, gain le plus fort en valeur absolue.
void ShaperWaveView::computePoint (std::uint64_t start, int n)
{
    const auto& ring = module_->analyzer();
    float in = 0.0f, out = 0.0f, fast = 0.0f, slow = 0.0f, gain = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto p = start + static_cast<std::uint64_t> (i);
        for (int c = 0; c < 2; ++c)
        {
            in = std::max (in, std::fabs (ring.sample (sh::kAnIn, c, p)));
            out = std::max (out, std::fabs (ring.sample (sh::kAnOut, c, p)));
            fast = std::max (fast, ring.sample (sh::kAnFast, c, p));
            slow = std::max (slow, ring.sample (sh::kAnSlow, c, p));
            const float g = ring.sample (sh::kAnGain, c, p);
            if (std::fabs (g) > std::fabs (gain)) gain = g;
        }
    }
    inNow_ = std::min(1.0f, in);
    outNow_ = std::min (1.0f, out);
    fastNow_ = std::min (1.0f, fast);
    slowNow_ = std::min (1.0f, slow);
    gainNow_ = gain;
}

void ShaperWaveView::pushPoint (bool zero)
{
    auto push = [] (std::vector<float>& v, float y) { std::rotate (v.begin(), v.begin() + 1, v.end()); v.back() = y; };
    push (inV_, zero ? 0.0f : inNow_);
    push (outV_, zero ? 0.0f : outNow_);
    push (fastV_, zero ? 0.0f : fastNow_);
    push (slowV_, zero ? 0.0f : slowNow_);
    push (gainV_, zero ? 0.0f : gainNow_);
}

void ShaperWaveView::updateXs (double now)
{
    const auto bound = plotArea();
    const std::size_t n = xs_.size();
    if (moveType_ == 2)   // Roll : les points restent en place, la tête avance
    {
        const double dx = static_cast<double> (bound.getWidth()) / static_cast<double> (n - 1);
        for (std::size_t i = 0; i < n; ++i) xs_[i] = bound.getX() + static_cast<float> (static_cast<double> ((rollNext_ + i) % n) * dx);
        return;
    }
    const double scale = static_cast<double> (bound.getWidth()) / timeLength_;
    const double dx = secondPerPoint_ * scale;
    double x0 = static_cast<double> (bound.getX()) - (now - startTime_) * scale;
    if (moveType_ == 1)   // Slow : la tête avance lentement puis revient vite
    {
        constexpr double fastLength = 0.5, restart = 0.25, sweep = 1.0 - restart;
        const double slowLength = timeLength_ - fastLength;
        const double phase = std::fmod (std::max (0.0, now - motionStart_), timeLength_);
        const double head = phase < slowLength ? restart + sweep * phase / slowLength : 1.0 - sweep * (phase - slowLength) / fastLength;
        x0 += head * static_cast<double> (bound.getWidth()) - static_cast<double> (n - 1) * dx;
    }
    for (std::size_t i = 0; i < n; ++i, x0 += dx) xs_[i] = static_cast<float> (x0);
}

void ShaperWaveView::rebuildPaths()
{
    const auto plot = plotArea();
    const float topH = plot.getHeight() * 0.58f, mid = plot.getY() + topH * 0.5f, amp = topH * 0.5f;
    const float gTop = plot.getY() + topH + skin_.px (12.0f), gMid = gTop + (plot.getBottom() - gTop) * 0.5f, gHalf = (plot.getBottom() - gTop) * 0.5f;
    for (auto* p : { &inPath_, &outPath_, &fastPath_, &slowPath_, &gainPath_ }) p->clear();
    const std::size_t n = xs_.size();
    if (n < 2) return;
    // segments continus (en mode Roll la tête recommence à gauche : une coupure)
    std::size_t begin = 0;
    auto flush = [&] (std::size_t end)
    {
        if (end - begin < 2) { begin = end; return; }
        auto fill = [&] (juce::Path& path, const std::vector<float>& v)
        {
            path.startNewSubPath (xs_[begin], mid - v[begin] * amp);
            for (std::size_t i = begin + 1; i < end; ++i) path.lineTo (xs_[i], mid - v[i] * amp);
            for (std::size_t i = end; i-- > begin;) path.lineTo (xs_[i], mid + v[i] * amp);
            path.closeSubPath();
        };
        auto line = [&] (juce::Path& path, const std::vector<float>& v)
        {
            path.startNewSubPath (xs_[begin], mid - v[begin] * amp);
            for (std::size_t i = begin + 1; i < end; ++i) path.lineTo (xs_[i], mid - v[i] * amp);
        };
        fill (inPath_, inV_);
        fill (outPath_, outV_);
        line (fastPath_, fastV_);
        line (slowPath_, slowV_);
        gainPath_.startNewSubPath (xs_[begin], gMid - juce::jlimit (-range_, range_, gainV_[begin]) / range_ * gHalf);
        for (std::size_t i = begin + 1; i < end; ++i) gainPath_.lineTo (xs_[i], gMid - juce::jlimit (-range_, range_, gainV_[i]) / range_ * gHalf);
        begin = end;
    };
    for (std::size_t i = 1; i < n; ++i)
        if (xs_[i] < xs_[i - 1]) flush (i);
    flush (n);
}

void ShaperWaveView::step (double now)
{
    if (module_ == nullptr || ! isVisible()) return;
    readSettings();
    auto& ring = module_->analyzer();
    const double sr = ring.sampleRate();
    const int maxBlock = ring.maxBlock();
    if (sampleRate_ <= 0.0 || std::fabs (sampleRate_ - sr) > 0.1 || maxBlock_ != maxBlock)
    {
        configure (sr, maxBlock);
        readPos_ = ring.writePos();
    }
    const std::uint64_t w = ring.writePos();
    if (readPos_ > w) readPos_ = w;
    std::int64_t ready = static_cast<std::int64_t> (w - readPos_);
    if (ready > static_cast<std::int64_t> (ring.capacity() / 2))   // lecteur trop en retard : on saute pour ne jamais lire de données écrasées
    {
        readPos_ = w - static_cast<std::uint64_t> (samplesPerPoint_) * 2;
        ready = samplesPerPoint_ * 2;
    }
    if (! first_ && now - startTime_ > 2.0 * timeLength_)   // fenêtre cachée longtemps : on repart de zéro
    {
        first_ = true;
        readPos_ = w;
        ready = 0;
    }
    bool idle = false;
    if (! first_)
    {
        while (now - startTime_ > secondPerPoint_)
        {
            if (ready >= samplesPerPoint_)
            {
                computePoint (readPos_, samplesPerPoint_);
                readPos_ += static_cast<std::uint64_t> (samplesPerPoint_);
                ready -= samplesPerPoint_;
                missing_ = 0;
            }
            else if (missing_ < kPausedThreshold) ++missing_;
            const bool paused = missing_ >= kPausedThreshold;
            pushPoint (paused);
            if (moveType_ == 2) rollNext_ = (rollNext_ + 1) % xs_.size();
            startTime_ += secondPerPoint_;
        }
        const std::int64_t threshold = 2 * static_cast<std::int64_t> (std::max (maxBlock_, samplesPerPoint_)) + static_cast<std::int64_t> (sampleRate_ * kDisplayDelayMs * 0.001);
        if (ready > threshold)   // trop de données accumulées (hôte en rafales) : on en jette une partie, comme ZL
        {
            tooMuch_ += static_cast<int> ((ready - threshold) / samplesPerPoint_);
            if (tooMuch_ > kTooMuchResetThreshold) { readPos_ += static_cast<std::uint64_t> (ready - threshold); tooMuch_ = 0; }
        }
        else tooMuch_ = 0;
        updateXs (now);
        rebuildPaths();
        idle = missing_ > static_cast<int> (numPoints_) + 10 && ! dirty_;
    }
    else if (ready >= samplesPerPoint_ + static_cast<std::int64_t> (sampleRate_ * kDisplayDelayMs * 0.001))
    {
        first_ = false;
        startTime_ = now;
        motionStart_ = now;
        for (auto* v : { &inV_, &outV_, &fastV_, &slowV_, &gainV_ }) std::fill (v->begin(), v->end(), 0.0f);
    }
    else idle = ! dirty_;
    // pas de repeinture inutile quand tout est vide et immobile
    if (! idle) repaint();
    dirty_ = false;
}

void ShaperWaveView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
    const auto plot = plotArea();
    const float topH = plot.getHeight() * 0.58f, mid = plot.getY() + topH * 0.5f;
    const float gTop = plot.getY() + topH + skin_.px (12.0f), gMid = gTop + (plot.getBottom() - gTop) * 0.5f, gHalf = (plot.getBottom() - gTop) * 0.5f;
    const float line = std::max (1.0f, skin_.px (1.0f));
    g.setColour (skin_.colour (Token::Grid, 0.6f));
    g.fillRect (plot.getX(), mid - line * 0.5f, plot.getWidth(), line);
    for (const float db : { -range_, 0.0f, range_ }) g.fillRect (plot.getX(), gMid - db / range_ * gHalf - line * 0.5f, plot.getWidth(), line);
    g.setColour (skin_.colour (Token::TextDim2));
    g.setFont (skin_.font (9.0f));
    const float lw = skin_.px (34.0f), lh = skin_.px (12.0f);
    g.drawText ("+" + juce::String (range_, 0), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), gMid - gHalf - lh * 0.5f, lw, lh), juce::Justification::centredRight);
    g.drawText ("0 dB", juce::Rectangle<float> (b.getX() + skin_.px (2.0f), gMid - lh * 0.5f, lw, lh), juce::Justification::centredRight);
    g.drawText ("-" + juce::String (range_, 0), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), gMid + gHalf - lh * 0.5f, lw, lh), juce::Justification::centredRight);
    for (int t = 0; t <= 8; ++t)
    {
        const double sec = timeLength_ * (8 - t) / 8.0;
        g.drawText (t == 8 ? juce::String ("0 s") : "-" + juce::String (sec, sec >= 4.0 ? 0 : 1) + " s",
                    juce::Rectangle<float> (plot.getX() + plot.getWidth() * static_cast<float> (t) / 8.0f - skin_.px (20.0f), plot.getBottom() + skin_.px (3.0f), skin_.px (40.0f), lh),
                    juce::Justification::centred);
    }
    if (module_ == nullptr || first_) return;
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.expanded (0.0f, skin_.px (2.0f)).toNearestInt());
    g.setColour (skin_.colour (Token::TextDim2, 0.28f));
    g.fillPath (inPath_);
    g.setColour (skin_.colour (Token::Accent, 0.38f));
    g.fillPath (outPath_);
    const auto stroke = [this] (float w) { return juce::PathStrokeType (skin_.px (w), juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    g.setColour (skin_.colour (Token::Pink2, 0.9f));
    g.strokePath (slowPath_, stroke (1.4f));
    g.setColour (skin_.colour (Token::KnobHot2, 0.9f));
    g.strokePath (fastPath_, stroke (1.4f));
    g.setColour (skin_.colour (Token::Accent));
    g.strokePath (gainPath_, stroke (1.9f));
}
} // namespace nova
