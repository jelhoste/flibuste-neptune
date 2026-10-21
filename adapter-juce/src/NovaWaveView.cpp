#include "NovaWaveView.h"

#include <algorithm>
#include <cmath>

using neptune::core::theme::Token;

namespace nova
{
namespace
{
constexpr float kRulerH = 18.0f;
constexpr float kGrab = 8.0f;

juce::String timeText (double seconds, double step)
{
    if (step >= 1.0) return juce::String (seconds, step >= 10.0 ? 0 : 1) + " s";
    if (step >= 0.001) return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";
    return juce::String (seconds * 1000.0, 2) + " ms";
}
} // namespace

WaveView::WaveView (const Skin& s) : skin_ (s)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    thread_ = std::thread ([this] { worker(); });
    startTimerHz (30);
}

WaveView::~WaveView()
{
    stopTimer();
    {
        std::lock_guard<std::mutex> lock (mutex_);
        quit_ = true;
        cancel_.store (true);
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveView::worker()
{
    for (;;)
    {
        Request r;
        {
            std::unique_lock<std::mutex> lock (mutex_);
            cv_.wait (lock, [this] { return quit_ || havePending_; });
            if (quit_) return;
            r = pending_;
            havePending_ = false;
            cancel_.store (false);
        }
        auto peaks = neptune::core::computePeaks (std::filesystem::u8path (r.path), r.from, r.to, r.bins, &cancel_);
        std::lock_guard<std::mutex> lock (mutex_);
        if (peaks.ok) { result_ = std::move (peaks); resultId_ = r.id; }
    }
}

void WaveView::setSample (const std::string& path, std::int64_t frames, double sampleRate)
{
    if (path == path_ && frames == frames_) return;
    path_ = path;
    frames_ = frames;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    shown_ = {};
    shownPath_.clear();
    lastFrom_ = lastTo_ = -1.0;
    showAll();
}

void WaveView::setFootprint (std::shared_ptr<const neptune::core::VolumeFootprint> fp, int mask)
{
    if (fp == footprint_ && mask == curveMask_) return;
    if (fp != footprint_ && fp != nullptr && fp->ok)
        for (int k = 0; k < 3; ++k)
        {
            const auto& cv = fp->curve (k);
            if (cv.empty()) continue;
            const auto mm = std::minmax_element (cv.begin(), cv.end());
            curveMax_[k] = *mm.second;
            curveMin_[k] = std::max (*mm.first, curveMax_[k] - 60.0f);   // le dégradé couvre au plus 60 dB sous le maximum
            if (curveMax_[k] - curveMin_[k] < 6.0f) curveMin_[k] = curveMax_[k] - 6.0f;
        }
    footprint_ = std::move (fp);
    curveMask_ = mask;
    repaint();
}

void WaveView::setRange (bool on, double lo, double hi, int kind)
{
    if (on == rangeOn_ && lo == rangeLo_ && hi == rangeHi_ && kind == rangeKind_) return;
    rangeOn_ = on; rangeLo_ = lo; rangeHi_ = hi; rangeKind_ = kind;
    repaint();
}

void WaveView::setCursor (std::int64_t frame, bool reveal)
{
    cursorFrame_ = frame;
    if (reveal && frame >= 0 && frames_ > 0 && (static_cast<double> (frame) < viewFrom_ || static_cast<double> (frame) > viewTo_))
    {
        const double span = viewTo_ - viewFrom_;
        viewFrom_ = static_cast<double> (frame) - span / 2.0;
        viewTo_ = viewFrom_ + span;
        clampView();
    }
    repaint();
}

void WaveView::setMarkers (const Markers& m)
{
    if (dragging_) return;
    markers_ = m;
    repaint();
}

void WaveView::showAll()
{
    viewFrom_ = 0.0;
    viewTo_ = std::max<double> (1.0, static_cast<double> (frames_));
    lastFrom_ = -1.0;
    repaint();
}

void WaveView::resized() { lastFrom_ = -1.0; }

juce::Rectangle<float> WaveView::plot() const
{
    return getLocalBounds().toFloat().reduced (skin_.px (6.0f), 0.0f).withTrimmedTop (skin_.px (kRulerH)).withTrimmedBottom (skin_.px (6.0f));
}

double WaveView::frameAt (float x) const noexcept
{
    const auto p = plot();
    return viewFrom_ + static_cast<double> ((x - p.getX()) / std::max (1.0f, p.getWidth())) * (viewTo_ - viewFrom_);
}

float WaveView::xOf (double frame) const noexcept
{
    const auto p = plot();
    return p.getX() + static_cast<float> ((frame - viewFrom_) / std::max (1.0, viewTo_ - viewFrom_)) * p.getWidth();
}

void WaveView::clampView()
{
    const double total = std::max<double> (1.0, static_cast<double> (frames_));
    double span = std::clamp (viewTo_ - viewFrom_, std::min (32.0, total), total);
    viewFrom_ = std::clamp (viewFrom_, 0.0, total - span);
    viewTo_ = viewFrom_ + span;
}

void WaveView::zoomBy (double factor, double anchorFrame)
{
    const double span = (viewTo_ - viewFrom_) * factor;
    const double rel = (anchorFrame - viewFrom_) / std::max (1.0, viewTo_ - viewFrom_);
    viewFrom_ = anchorFrame - rel * span;
    viewTo_ = viewFrom_ + span;
    clampView();
    repaint();
}

void WaveView::requestPeaks()
{
    if (path_.empty() || frames_ <= 0) return;
    const int bins = std::max (16, juce::roundToInt (plot().getWidth()));
    if (std::abs (viewFrom_ - lastFrom_) < 0.5 && std::abs (viewTo_ - lastTo_) < 0.5 && bins == lastBins_) return;
    lastFrom_ = viewFrom_;
    lastTo_ = viewTo_;
    lastBins_ = bins;
    Request r;
    r.path = path_;
    // un peu de marge de chaque côté : le glissé reste fluide sans attendre le calcul suivant
    r.from = static_cast<std::int64_t> (std::floor (viewFrom_));
    r.to = std::min<std::int64_t> (frames_, static_cast<std::int64_t> (std::ceil (viewTo_)));
    r.bins = bins;
    r.id = nextId_++;
    {
        std::lock_guard<std::mutex> lock (mutex_);
        pending_ = r;
        havePending_ = true;
        cancel_.store (true);   // interrompt le calcul en cours : la demande la plus récente passe devant
    }
    cv_.notify_all();
    requestedId_ = r.id;
}

void WaveView::timerCallback()
{
    if (! isShowing()) return;
    requestPeaks();
    std::lock_guard<std::mutex> lock (mutex_);
    if (resultId_ != shownId_ && resultId_ != 0)
    {
        shown_ = result_;
        shownId_ = resultId_;
        shownPath_ = path_;
        repaint();
    }
}

WaveView::Hit WaveView::hitTest (juce::Point<float> pt) const
{
    Hit best;
    if (frames_ <= 0) return best;
    const float tol = skin_.px (kGrab);
    float bestDist = tol;
    auto test = [&] (Marker m, double frame)
    {
        const float d = std::abs (pt.x - xOf (frame));
        if (d <= bestDist) { bestDist = d; best = { true, m }; }
    };
    test (Marker::Start, static_cast<double> (markers_.start));
    test (Marker::End, static_cast<double> (effectiveEnd()));
    if (markers_.loop)
    {
        test (Marker::LoopStart, static_cast<double> (markers_.loopStart));
        test (Marker::LoopEnd, static_cast<double> (effectiveLoopEnd()));
        if (markers_.crossfade > 0 && ! markers_.reverse) test (Marker::Crossfade, static_cast<double> (effectiveLoopEnd() - markers_.crossfade));
    }
    return best;
}

void WaveView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    skin_.fill (g, Token::StageBg, b, skin_.px (8.0f));
    if (path_.empty() || frames_ <= 0)
    {
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (13.0f, true));
        g.drawText (noSample_, b, juce::Justification::centred);
        return;
    }
    const auto p = plot();
    const int channels = shown_.ok ? std::max (1, shown_.channels) : 1;
    const float laneH = p.getHeight() / static_cast<float> (channels);

    // ---- règle du temps
    {
        const double span = (viewTo_ - viewFrom_) / sampleRate_;
        const double target = span / std::max (2.0, static_cast<double> (p.getWidth() / skin_.px (90.0f)));
        double step = 1.0e-5;
        const double mags[3] = { 1.0, 2.0, 5.0 };
        for (int e = -6; e <= 4; ++e)
        {
            bool done = false;
            for (const double m : mags) { const double s = m * std::pow (10.0, e); if (s >= target) { step = s; done = true; break; } }
            if (done) break;
        }
        g.setFont (skin_.font (9.5f));
        const double t0 = viewFrom_ / sampleRate_, t1 = viewTo_ / sampleRate_;
        for (double t = std::ceil (t0 / step) * step; t <= t1; t += step)
        {
            const float x = xOf (t * sampleRate_);
            g.setColour (skin_.colour (Token::Grid));
            g.drawVerticalLine (juce::roundToInt (x), p.getY(), p.getBottom());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (timeText (t, step), juce::Rectangle<float> (x + skin_.px (3.0f), b.getY() + skin_.px (2.0f), skin_.px (80.0f), skin_.px (kRulerH - 2.0f)), juce::Justification::centredLeft, false);
        }
    }

    // ---- région de boucle et zone muette
    const float xs = xOf (static_cast<double> (markers_.start)), xe = xOf (static_cast<double> (effectiveEnd()));
    g.setColour (skin_.colour (Token::BgApp, 0.55f));
    if (xs > p.getX()) g.fillRect (juce::Rectangle<float> (p.getX(), p.getY(), std::min (xs, p.getRight()) - p.getX(), p.getHeight()));
    if (xe < p.getRight()) g.fillRect (juce::Rectangle<float> (std::max (xe, p.getX()), p.getY(), p.getRight() - std::max (xe, p.getX()), p.getHeight()));
    if (markers_.loop)
        for (const auto& o : markers_.otherLoops)
        {
            const float a = std::max (xOf (static_cast<double> (o.start)), p.getX()), b = std::min (xOf (static_cast<double> (std::min (o.end, frames_))), p.getRight());
            if (b <= a) continue;
            g.setColour (skin_.colour (Token::Pink, 0.06f));
            g.fillRect (juce::Rectangle<float> (a, p.getY(), b - a, p.getHeight()));
            g.setColour (skin_.colour (Token::TextDim2));
            g.setFont (skin_.font (9.0f, true));
            g.drawText ("LOOP " + juce::String (o.number), juce::Rectangle<float> (a + skin_.px (3.0f), p.getBottom() - skin_.px (28.0f), skin_.px (60.0f), skin_.px (12.0f)), juce::Justification::centredLeft);
        }
    if (markers_.loop)
    {
        const float ls = xOf (static_cast<double> (markers_.loopStart)), le = xOf (static_cast<double> (effectiveLoopEnd()));
        g.setColour (skin_.colour (Token::Pink, 0.12f));
        g.fillRect (juce::Rectangle<float> (std::max (ls, p.getX()), p.getY(), std::min (le, p.getRight()) - std::max (ls, p.getX()), p.getHeight()));
        if (markers_.crossfade > 0 && ! markers_.reverse)
        {
            const float cs = xOf (static_cast<double> (effectiveLoopEnd() - markers_.crossfade));
            g.setColour (skin_.colour (Token::Accent2, 0.25f));
            g.fillRect (juce::Rectangle<float> (std::max (cs, p.getX()), p.getY(), std::min (le, p.getRight()) - std::max (cs, p.getX()), p.getHeight()));
        }
    }

    // ---- forme d'onde
    for (int c = 0; c < channels; ++c)
    {
        const float cy = p.getY() + laneH * (static_cast<float> (c) + 0.5f);
        g.setColour (skin_.colour (Token::Grid));
        g.drawHorizontalLine (juce::roundToInt (cy), p.getX(), p.getRight());
        if (! shown_.ok || shownPath_ != path_ || shown_.bins < 1) continue;
        g.setColour (skin_.colour (Token::Accent));
        const double span = static_cast<double> (shown_.to - shown_.from);
        const float amp = laneH * 0.46f * static_cast<float> (std::pow (10.0, markers_.gainDb / 20.0));   // le gain de la zone se voit sur la forme d'onde
        for (float x = p.getX(); x < p.getRight(); x += 1.0f)
        {
            const double f0 = frameAt (x), f1 = frameAt (x + 1.0f);
            if (f1 < static_cast<double> (shown_.from) || f0 > static_cast<double> (shown_.to)) continue;
            const int b0 = std::clamp (static_cast<int> ((f0 - static_cast<double> (shown_.from)) / span * shown_.bins), 0, shown_.bins - 1);
            const int b1 = std::clamp (static_cast<int> ((f1 - static_cast<double> (shown_.from)) / span * shown_.bins), b0, shown_.bins - 1);
            float lo = 1.0f, hi = -1.0f;
            for (int k = b0; k <= b1; ++k) { lo = std::min (lo, shown_.low (c, k)); hi = std::max (hi, shown_.high (c, k)); }
            if (lo > hi) continue;
            const float lim = laneH * 0.5f;
            const float y0 = std::max (cy - lim, cy - hi * amp), y1 = std::min (cy + lim, cy - lo * amp);   // écrêté à la voie : le dépassement saute aux yeux
            g.fillRect (x, y0, 1.0f, std::max (1.0f, y1 - y0));
        }
    }
    if (! shown_.ok || shownPath_ != path_)
    {
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f));
        g.drawText ("...", p, juce::Justification::centred);
    }


    // ---- empreintes de volume : courbes de niveau en dB (plancher -60 dB en bas), colorées du bleu-vert au violet selon le niveau
    if (footprint_ != nullptr && footprint_->ok && footprint_->hopFrames > 0)
    {
        const auto& fp = *footprint_;
        const float floorDb = -60.0f;
        auto levelColour = [] (float t)
        {
            static const juce::Colour stops[5] = { juce::Colour (0xff1fb5a8), juce::Colour (0xff6ccf5a), juce::Colour (0xfff2d14b), juce::Colour (0xfff0603c), juce::Colour (0xffb04cff) };
            t = std::clamp (t, 0.0f, 1.0f) * 4.0f;
            const int i = std::min (3, static_cast<int> (t));
            return stops[i].interpolatedWith (stops[i + 1], t - static_cast<float> (i));
        };
        auto levelAt = [&] (const std::vector<float>& cv, float x, bool& have)
        {
            const double f0 = frameAt (x), f1 = frameAt (x + 1.0f);
            have = f1 >= 0.0 && f0 <= static_cast<double> (fp.totalFrames) && ! cv.empty();
            if (! have) return floorDb;
            const auto last = static_cast<std::int64_t> (cv.size()) - 1;
            const std::int64_t i0 = std::clamp<std::int64_t> (static_cast<std::int64_t> (f0) / fp.hopFrames, 0, last);
            const std::int64_t i1 = std::clamp<std::int64_t> (static_cast<std::int64_t> (f1) / fp.hopFrames, i0, last);
            float v = -200.0f;
            for (std::int64_t i = i0; i <= i1; ++i) v = std::max (v, cv[static_cast<std::size_t> (i)]);
            return v;
        };
        if (rangeOn_ && rangeKind_ >= 0 && rangeKind_ < 3 && ! fp.curve (rangeKind_).empty())
        {
            g.setColour (levelColour (1.0f).withAlpha (0.20f));
            for (float x = p.getX(); x < p.getRight(); x += 1.0f)
            {
                bool have = false;
                const float v = levelAt (fp.curve (rangeKind_), x, have);
                if (have && v >= static_cast<float> (rangeLo_) && v <= static_cast<float> (rangeHi_)) g.fillRect (x, p.getY(), 1.0f, p.getHeight());
            }
        }
        for (int kind = 0; kind < 3; ++kind)
        {
            if ((curveMask_ & (1 << kind)) == 0 || fp.curve (kind).empty()) continue;
            const auto& cv = fp.curve (kind);
            float px = 0.0f, py = 0.0f;
            bool prev = false;
            for (float x = p.getX(); x < p.getRight(); x += 1.0f)
            {
                bool have = false;
                const float v = levelAt (cv, x, have);
                if (! have) { prev = false; continue; }
                const float y = p.getBottom() - (std::clamp (v, floorDb, 0.0f) - floorDb) / (-floorDb) * p.getHeight();
                if (prev)
                {
                    g.setColour (levelColour ((v - curveMin_[kind]) / (curveMax_[kind] - curveMin_[kind])));
                    g.drawLine (px, py, x, y, skin_.px (kind == 0 ? 1.2f : 1.8f));
                }
                px = x; py = y; prev = true;
            }
        }
        if (curveMask_ != 0)
        {
            g.setColour (skin_.colour (Token::TextDim2));
            g.setFont (skin_.font (9.0f));
            g.drawText ("0 dB", juce::Rectangle<float> (p.getRight() - skin_.px (46.0f), p.getY() + skin_.px (1.0f), skin_.px (44.0f), skin_.px (12.0f)), juce::Justification::centredRight);
            g.drawText ("-60 dB", juce::Rectangle<float> (p.getRight() - skin_.px (46.0f), p.getBottom() - skin_.px (13.0f), skin_.px (44.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
    }
    if (cursorFrame_ >= 0)
    {
        const float cx = xOf (static_cast<double> (cursorFrame_));
        if (cx >= p.getX() && cx <= p.getRight())
        {
            g.setColour (skin_.colour (Token::Text, 0.9f));
            g.drawVerticalLine (juce::roundToInt (cx), p.getY(), p.getBottom());
            juce::Path tri;
            tri.addTriangle (cx - skin_.px (5.0f), p.getY(), cx + skin_.px (5.0f), p.getY(), cx, p.getY() + skin_.px (7.0f));
            g.fillPath (tri);
        }
    }

    // ---- fondus, délai, gain et accordage de la zone
    {
        const bool rev = markers_.reverse;
        const float top = p.getY(), bottom = p.getBottom();
        auto ramp = [&] (float xA, float xB, bool up, const juce::String& label)
        {
            // rampe de gain : de 0 (bas) à 1 (haut) si `up`, l'inverse sinon ; la partie atténuée est teintée
            juce::Path area;
            if (up) { area.startNewSubPath (xA, top); area.lineTo (xB, top); area.lineTo (xA, bottom); }
            else { area.startNewSubPath (xA, top); area.lineTo (xB, top); area.lineTo (xB, bottom); }
            area.closeSubPath();
            g.setColour (skin_.colour (Token::Accent2, 0.22f));
            g.fillPath (area);
            g.setColour (skin_.colour (Token::Accent2));
            if (up) g.drawLine (xA, bottom, xB, top, skin_.px (2.0f)); else g.drawLine (xA, top, xB, bottom, skin_.px (2.0f));
            g.setFont (skin_.font (9.0f, true));
            g.drawText (label, juce::Rectangle<float> (std::min (xA, xB), top + skin_.px (16.0f), skin_.px (90.0f), skin_.px (13.0f)), juce::Justification::centredLeft);
        };
        const auto msText = [] (double ms) { return juce::String (ms, ms < 10.0 ? 1 : 0) + " ms"; };
        if (markers_.fadeInFrames > 0)
        {
            const float len = std::max (skin_.px (3.0f), xOf (static_cast<double> (markers_.fadeInFrames)) - xOf (0.0));
            if (! rev) ramp (xs, xs + len, true, "FADE IN " + msText (markers_.fadeInMs));
            else ramp (xe, xe - len, true, "FADE IN " + msText (markers_.fadeInMs));
        }
        if (! markers_.loop && markers_.fadeOutFrames > 0)
        {
            const float len = std::max (skin_.px (3.0f), xOf (static_cast<double> (markers_.fadeOutFrames)) - xOf (0.0));
            if (! rev) ramp (xe - len, xe, false, "FADE OUT " + msText (markers_.fadeOutMs));   // se termine sur le point END
            else ramp (xs + len, xs, false, "FADE OUT " + msText (markers_.fadeOutMs));          // lecture inversée : se termine sur le point START
        }
        if (markers_.delayFrames > 0)
        {
            const float len = std::max (skin_.px (3.0f), xOf (static_cast<double> (markers_.delayFrames)) - xOf (0.0));
            const float x0 = rev ? xe : xs - len;
            g.setColour (skin_.colour (Token::Accent, 0.30f));
            g.fillRect (juce::Rectangle<float> (x0, bottom - skin_.px (30.0f), len, skin_.px (14.0f)));
            g.setColour (skin_.colour (Token::Accent));
            g.setFont (skin_.font (9.0f, true));
            g.drawText ("DELAY " + msText (markers_.delayMs), juce::Rectangle<float> (rev ? x0 + len + skin_.px (3.0f) : x0 - skin_.px (84.0f), bottom - skin_.px (30.0f), skin_.px (80.0f), skin_.px (14.0f)),
                        rev ? juce::Justification::centredLeft : juce::Justification::centredRight);
        }
        float by = top + skin_.px (4.0f);
        auto badge = [&] (const juce::String& text)
        {
            const float w = skin_.px (112.0f), h = skin_.px (16.0f);
            const juce::Rectangle<float> r (p.getRight() - w - skin_.px (4.0f), by, w, h);
            g.setColour (skin_.colour (Token::BgApp, 0.75f));
            g.fillRoundedRectangle (r, skin_.px (4.0f));
            g.setColour (skin_.colour (Token::Text));
            g.setFont (skin_.font (10.0f, true));
            g.drawText (text, r, juce::Justification::centred);
            by += h + skin_.px (3.0f);
        };
        if (std::abs (markers_.gainDb) > 0.049) badge ("GAIN " + juce::String (markers_.gainDb, 1) + " dB");
        if (std::abs (markers_.tuneCents) > 0.049) badge ("TUNE " + juce::String (markers_.tuneCents, 1) + " ct");
    }

    // ---- marqueurs
    auto flag = [&] (double frame, Token token, bool top, const juce::String& label, bool active)
    {
        const float x = xOf (frame);
        if (x < p.getX() - 2.0f || x > p.getRight() + 2.0f) return;
        g.setColour (skin_.colour (token, active ? 1.0f : 0.9f));
        g.drawLine (x, p.getY(), x, p.getBottom(), skin_.px (active ? 2.2f : 1.5f));
        const float fh = skin_.px (14.0f), fw = skin_.px (46.0f);
        const auto r = top ? juce::Rectangle<float> (x, p.getY(), fw, fh) : juce::Rectangle<float> (x - fw, p.getBottom() - fh, fw, fh);
        g.fillRoundedRectangle (r, skin_.px (3.0f));
        g.setColour (skin_.colour (Token::BgApp));
        g.setFont (skin_.font (9.0f, true));
        g.drawText (label, r, juce::Justification::centred);
    };
    flag (static_cast<double> (markers_.start), Token::Accent, true, "START", active_.valid && dragging_ && active_.marker == Marker::Start);
    flag (static_cast<double> (effectiveEnd()), Token::Accent, false, "END", active_.valid && dragging_ && active_.marker == Marker::End);
    if (markers_.loop)
    {
        flag (static_cast<double> (markers_.loopStart), Token::Pink, true, "LOOP", active_.valid && dragging_ && active_.marker == Marker::LoopStart);
        flag (static_cast<double> (effectiveLoopEnd()), Token::Pink, false, "LOOP", active_.valid && dragging_ && active_.marker == Marker::LoopEnd);
        if (markers_.crossfade > 0 && ! markers_.reverse)
            flag (static_cast<double> (effectiveLoopEnd() - markers_.crossfade), Token::Accent2, true, "XFADE", active_.valid && dragging_ && active_.marker == Marker::Crossfade);
    }
    if (markers_.reverse)
    {
        g.setColour (skin_.colour (Token::Accent));
        g.setFont (skin_.font (10.0f, true));
        g.drawText ("<< REVERSE", juce::Rectangle<float> (p.getRight() - skin_.px (110.0f), p.getBottom() - skin_.px (18.0f), skin_.px (104.0f), skin_.px (16.0f)), juce::Justification::centredRight);
    }

    // ---- info au survol : position et temps
    if (hovering_ && p.contains (hoverPos_))
    {
        const double f = frameAt (hoverPos_.x);
        auto text = juce::String (static_cast<long long> (f)) + "  -  " + timeText (f / sampleRate_, 0.00001);
        if (shown_.ok && shown_.bins > 0 && shown_.to > shown_.from && f >= static_cast<double> (shown_.from) && f < static_cast<double> (shown_.to))
        {
            const int bin = std::clamp (static_cast<int> ((f - static_cast<double> (shown_.from)) * shown_.bins / static_cast<double> (shown_.to - shown_.from)), 0, shown_.bins - 1);
            float a = 0.0f;
            for (int ch = 0; ch < shown_.channels; ++ch) a = std::max ({ a, std::fabs (shown_.low (ch, bin)), std::fabs (shown_.high (ch, bin)) });
            text += a > 1.0e-6f ? "  -  " + juce::String (20.0 * std::log10 (static_cast<double> (a)), 1) + " dB" : juce::String ("  -  -inf dB");
        }
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (10.0f));
        g.drawText (text, juce::Rectangle<float> (p.getX() + skin_.px (6.0f), p.getBottom() - skin_.px (18.0f), skin_.px (220.0f), skin_.px (16.0f)), juce::Justification::centredLeft);
    }
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    hoverPos_ = e.position;
    hovering_ = true;
    const auto h = hitTest (e.position);
    setMouseCursor (h.valid ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (frames_ <= 0) return;
    const auto h = hitTest (e.position);
    if (h.valid && ! e.mods.isPopupMenu())
    {
        active_ = h;
        dragging_ = true;
        if (onGestureBegin) onGestureBegin();
        mouseDrag (e);
        return;
    }
    panning_ = true;
    panStartX_ = e.position.x;
    panStartFrom_ = viewFrom_;
    panStartTo_ = viewTo_;
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    hoverPos_ = e.position;
    if (dragging_ && active_.valid)
    {
        const auto frame = static_cast<std::int64_t> (std::llround (std::clamp (frameAt (e.position.x), 0.0, static_cast<double> (frames_))));
        // mise à jour locale pour le retour visuel immédiat ; l'éditeur borne et renvoie la valeur définitive
        switch (active_.marker)
        {
            case Marker::Start: markers_.start = std::min (frame, effectiveEnd() - 1); break;
            case Marker::End: markers_.end = std::max (frame, markers_.start + 1); break;
            case Marker::LoopStart: markers_.loopStart = std::min (frame, effectiveLoopEnd() - 1); break;
            case Marker::LoopEnd: markers_.loopEnd = std::max (frame, markers_.loopStart + 1); break;
            case Marker::Crossfade: markers_.crossfade = std::max<std::int64_t> (0, effectiveLoopEnd() - frame); break;
        }
        if (onMarker)
        {
            switch (active_.marker)
            {
                case Marker::Start: onMarker (active_.marker, markers_.start); break;
                case Marker::End: onMarker (active_.marker, markers_.end); break;
                case Marker::LoopStart: onMarker (active_.marker, markers_.loopStart); break;
                case Marker::LoopEnd: onMarker (active_.marker, markers_.loopEnd); break;
                case Marker::Crossfade: onMarker (active_.marker, markers_.crossfade); break;
            }
        }
        repaint();
    }
    else if (panning_)
    {
        const double span = panStartTo_ - panStartFrom_;
        const double shift = static_cast<double> (panStartX_ - e.position.x) / std::max (1.0f, plot().getWidth()) * span;
        viewFrom_ = panStartFrom_ + shift;
        viewTo_ = panStartTo_ + shift;
        clampView();
        repaint();
    }
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (dragging_)
    {
        dragging_ = false;
        if (onGestureEnd) onGestureEnd();
    }
    panning_ = false;
    repaint();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! hitTest (e.position).valid) showAll();
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (frames_ <= 0) return;
    if (e.mods.isShiftDown())
    {
        const double span = viewTo_ - viewFrom_;
        const double shift = -static_cast<double> (w.deltaY + w.deltaX) * span * 0.5;
        viewFrom_ += shift;
        viewTo_ += shift;
        clampView();
        repaint();
        return;
    }
    const double delta = static_cast<double> (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX);
    if (delta == 0.0) return;
    zoomBy (std::pow (0.7, delta * 4.0), frameAt (e.position.x));
}

void LoopSeamView::setData (std::vector<float> beforeEnd, std::vector<float> afterStart, std::vector<float> beforeStart)
{
    beforeEnd_ = std::move (beforeEnd);
    afterStart_ = std::move (afterStart);
    beforeStart_ = std::move (beforeStart);
    repaint();
}

void LoopSeamView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
    if (beforeEnd_.empty() || afterStart_.empty() || beforeStart_.empty())
    {
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f));
        g.drawText ("LOOP", b, juce::Justification::centred);
        return;
    }
    const float gap = skin_.px (6.0f);
    const float w = (b.getWidth() - 3.0f * gap) / 2.0f;
    const auto left = juce::Rectangle<float> (b.getX() + gap, b.getY() + gap, w, b.getHeight() - 2.0f * gap);
    const auto right = juce::Rectangle<float> (left.getRight() + gap, left.getY(), w, left.getHeight());
    auto trace = [&] (const std::vector<float>& v, juce::Rectangle<float> area, float x0, float x1, juce::Colour c)
    {
        if (v.size() < 2) return;
        juce::Path p;
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            const float x = area.getX() + area.getWidth() * (x0 + (x1 - x0) * static_cast<float> (i) / static_cast<float> (v.size() - 1));
            const float y = area.getCentreY() - std::clamp (v[i], -1.0f, 1.0f) * area.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (skin_.px (1.4f)));
    };
    for (const auto& r : { left, right })
    {
        g.setColour (skin_.colour (Token::Grid));
        g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
    }
    // gauche : fin de boucle puis début de boucle, raccordés au milieu
    trace (beforeEnd_, left, 0.0f, 0.5f, skin_.colour (Token::Accent));
    trace (afterStart_, left, 0.5f, 1.0f, skin_.colour (Token::Pink));
    g.setColour (skin_.colour (Token::TextDim2, 0.8f));
    g.drawVerticalLine (juce::roundToInt (left.getCentreX()), left.getY(), left.getBottom());
    // droite : les deux avant-côtés superposés
    trace (beforeEnd_, right, 0.0f, 1.0f, skin_.colour (Token::Accent));
    trace (beforeStart_, right, 0.0f, 1.0f, skin_.colour (Token::Pink, 0.9f));
}
} // namespace nova
