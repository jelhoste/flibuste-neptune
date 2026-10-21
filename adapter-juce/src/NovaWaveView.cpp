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
        const float amp = laneH * 0.46f;
        for (float x = p.getX(); x < p.getRight(); x += 1.0f)
        {
            const double f0 = frameAt (x), f1 = frameAt (x + 1.0f);
            if (f1 < static_cast<double> (shown_.from) || f0 > static_cast<double> (shown_.to)) continue;
            const int b0 = std::clamp (static_cast<int> ((f0 - static_cast<double> (shown_.from)) / span * shown_.bins), 0, shown_.bins - 1);
            const int b1 = std::clamp (static_cast<int> ((f1 - static_cast<double> (shown_.from)) / span * shown_.bins), b0, shown_.bins - 1);
            float lo = 1.0f, hi = -1.0f;
            for (int k = b0; k <= b1; ++k) { lo = std::min (lo, shown_.low (c, k)); hi = std::max (hi, shown_.high (c, k)); }
            if (lo > hi) continue;
            const float y0 = cy - hi * amp, y1 = cy - lo * amp;
            g.fillRect (x, y0, 1.0f, std::max (1.0f, y1 - y0));
        }
    }
    if (! shown_.ok || shownPath_ != path_)
    {
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f));
        g.drawText ("...", p, juce::Justification::centred);
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
        const auto text = juce::String (static_cast<long long> (f)) + "  -  " + timeText (f / sampleRate_, 0.00001);
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
} // namespace nova
