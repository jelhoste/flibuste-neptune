#include "ShaperEditor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <set>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace sh = neptune::core::shaper;

namespace
{
constexpr float kPi = 3.14159265358979f;
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }

// FFT radix 2 en place (taille puissance de 2), pour l'analyseur de la clé
void fft (std::vector<std::complex<float>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * kPi / static_cast<float> (len);
        const std::complex<float> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

// ------------------------------------------------------------------------------------------------ surface (gabarit logique fixe)
class ShaperEditor::Surface : public juce::Component
{
public:
    explicit Surface (ShaperEditor& o) : owner_ (o) {}
    void paint (juce::Graphics& g) override { owner_.paint (g); }
private:
    ShaperEditor& owner_;
};

// ------------------------------------------------------------------------------------------------ contrôle compact (valeur sous le libellé)
class ShaperEditor::ParamBar : public juce::Component
{
public:
    ParamBar (ShaperEditor& o, ParamId id, juce::String label, bool horizontal, double step = 0.0)
        : owner_ (o), id_ (id), label_ (std::move (label)), horizontal_ (horizontal), step_ (step) {}

    ParamId id() const { return id_; }
    void setStep (double s) { step_ = s; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (owner_.colour (Token::PanelBg));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (owner_.colour (Token::Pb).withAlpha (0.8f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const auto& set = owner_.host_.parameters();
        const auto index = set.indexOf (id_);
        if (index == ParameterSet::npos) return;
        const float n = static_cast<float> (set.getNormalizedAt (index));
        // jauge fine en bas
        auto bar = r.reduced (8.0f, 0.0f).withTrimmedTop (r.getHeight() - 5.0f).withHeight (3.0f).translated (0.0f, -4.0f);
        g.setColour (owner_.colour (Token::Grid));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (owner_.colour (Token::Accent).withAlpha (isEnabled() ? 0.9f : 0.4f));
        g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * n)), 1.5f);
        g.setColour (owner_.colour (Token::TextDim2));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (label_, r.withTrimmedBottom (r.getHeight() * 0.55f).reduced (6.0f, 0.0f), juce::Justification::centred, true);
        g.setColour (owner_.colour (Token::Text));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 (set.toText (id_).c_str()), r.withTrimmedTop (r.getHeight() * 0.38f).withTrimmedBottom (8.0f).reduced (4.0f, 0.0f),
                    juce::Justification::centred, true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        owner_.host_.uiBeginEdit (id_);
        dragging_ = true;
        start_ = owner_.host_.parameters().getNormalized (id_);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging_) return;
        const float travel = horizontal_ ? static_cast<float> (getWidth()) * 1.5f : 160.0f;
        const float d = horizontal_ ? static_cast<float> (e.getDistanceFromDragStartX()) : -static_cast<float> (e.getDistanceFromDragStartY());
        apply (start_ + d / travel * (e.mods.isShiftDown() ? 0.1f : 1.0f));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && e.getDistanceFromDragStart() < 4) { owner_.openModal (id_, uistate::Terminal::Pointer); return; }
        if (dragging_) owner_.host_.uiEndEdit (id_);
        dragging_ = false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { owner_.host_.uiResetToDefault (id_); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        owner_.host_.uiBeginEdit (id_);
        apply (owner_.host_.parameters().getNormalized (id_) + (w.deltaY > 0.0f ? 0.01 : -0.01));
        owner_.host_.uiEndEdit (id_);
    }

private:
    void apply (double n)
    {
        n = std::clamp (n, 0.0, 1.0);
        if (step_ > 0.0)
        {
            const auto* spec = owner_.host_.parameters().find (id_);
            if (spec != nullptr) n = spec->toNormalized (std::round (spec->fromNormalized (n) / step_) * step_);
        }
        owner_.host_.uiSetNormalised (id_, n);
        repaint();
    }
    ShaperEditor& owner_;
    ParamId id_;
    juce::String label_;
    bool horizontal_, dragging_ = false;
    double step_;
    double start_ = 0.0;
};

// ------------------------------------------------------------------------------------------------ bouton d'état (bascule ou choix cyclique)
class ShaperEditor::StateButton : public juce::TextButton
{
public:
    StateButton (ShaperEditor& o, ParamId id, juce::String label, std::vector<juce::String> choices)
        : owner_ (o), id_ (id), label_ (std::move (label)), choices_ (std::move (choices))
    {
        setClickingTogglesState (false);
        setColour (juce::TextButton::buttonColourId, o.colour (Token::PanelBg));
        setColour (juce::TextButton::buttonOnColourId, o.colour (Token::Accent).withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, o.colour (Token::Text));
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
    ParamId id() const { return id_; }
    void refresh()
    {
        const int v = static_cast<int> (std::lround (owner_.value (id_)));
        if (choices_.empty())
        {
            setToggleState (v != 0, juce::dontSendNotification);
            setButtonText (label_);
        }
        else
        {
            setToggleState (false, juce::dontSendNotification);
            setButtonText ((label_.isEmpty() ? juce::String() : label_ + "  ") + choices_[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (choices_.size()) - 1, v))]);
        }
    }
    std::set<int> disabledChoices;
    void clicked() override
    {
        const auto* spec = owner_.host_.parameters().find (id_);
        if (spec == nullptr) return;
        int next;
        const int cur = static_cast<int> (std::lround (owner_.value (id_)));
        if (choices_.empty()) next = cur != 0 ? 0 : 1;
        else
        {
            const int n = static_cast<int> (choices_.size());
            next = (cur + 1) % n;
            for (int k = 0; k < n && disabledChoices.count (next) != 0; ++k) next = (next + 1) % n;
        }
        owner_.host_.uiBeginEdit (id_);
        owner_.host_.uiSetNormalised (id_, spec->toNormalized (next));
        owner_.host_.uiEndEdit (id_);
        refresh();
    }
private:
    ShaperEditor& owner_;
    ParamId id_;
    juce::String label_;
    std::vector<juce::String> choices_;
};

// ------------------------------------------------------------------------------------------------ vu-mètre (IN, OUT : stéréo ; GAIN : bipolaire)
class ShaperEditor::LevelMeter : public juce::Component
{
public:
    enum Kind { In, Out, Gain };
    LevelMeter (ShaperEditor& o, Kind k) : owner_ (o), kind_ (k) {}
    void update()
    {
        const auto* m = owner_.module_;
        if (m == nullptr) return;
        auto fall = [] (float& cur, float in) { cur = in > cur ? in : cur * 0.88f; };
        if (kind_ == Gain)
        {
            gain_[0] = m->meter (sh::kGainL);
            gain_[1] = m->meter (sh::kGainR);
        }
        else
        {
            const int peakBase = kind_ == In ? sh::kInPeakL : sh::kOutPeakL, rmsBase = kind_ == In ? sh::kInRmsL : sh::kOutRmsL;
            for (int c = 0; c < 2; ++c)
            {
                fall (peak_[c], m->meter (peakBase + c));
                rms_[c] = 0.7f * rms_[c] + 0.3f * m->meter (rmsBase + c);
                maxPeak_ = std::max (maxPeak_, m->meter (peakBase + c));
            }
        }
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (kind_ == Gain) return;
        if (e.y > getHeight() - 24) { rmsMode_ = ! rmsMode_; repaint(); }
        else maxPeak_ = 0.0f;   // clic sur la valeur : remise à zéro de la crête max
    }
    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const char* title = kind_ == In ? "IN" : kind_ == Out ? "OUT" : "GAIN";
        g.setColour (kind_ == Out ? owner_.colour (Token::Pink) : owner_.colour (Token::Accent));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (title, area.withHeight (16.0f), juce::Justification::centred);
        auto bars = area.withTrimmedTop (22.0f).withTrimmedBottom (kind_ == Gain ? 30.0f : 44.0f).reduced (area.getWidth() * 0.2f, 0.0f);
        const float bw = (bars.getWidth() - 4.0f) / 2.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto b = juce::Rectangle<float> (bars.getX() + c * (bw + 4.0f), bars.getY(), bw, bars.getHeight());
            g.setColour (owner_.colour (Token::Grid));
            g.fillRoundedRectangle (b, 2.0f);
            if (kind_ == Gain)
            {
                const float range = 24.0f, v = juce::jlimit (-range, range, gain_[c]);
                const float mid = b.getCentreY(), h = std::fabs (v) / range * b.getHeight() * 0.5f;
                g.setColour (owner_.colour (Token::Accent));
                g.fillRect (b.getX(), v >= 0.0f ? mid - h : mid, b.getWidth(), std::max (1.0f, h));
                g.setColour (owner_.colour (Token::TextDim2));
                g.fillRect (b.getX() - 3.0f, mid - 0.5f, b.getWidth() + 6.0f, 1.0f);
            }
            else
            {
                const float level = rmsMode_ ? rms_[c] : peak_[c];
                const float f = juce::jlimit (0.0f, 1.0f, (dbOf (level) + 60.0f) / 60.0f);
                g.setColour ((kind_ == Out ? owner_.colour (Token::Pink) : owner_.colour (Token::Accent)).withAlpha (0.9f));
                g.fillRoundedRectangle (b.withTrimmedTop (b.getHeight() * (1.0f - f)), 2.0f);
                const float fm = juce::jlimit (0.0f, 1.0f, (dbOf (maxPeak_) + 60.0f) / 60.0f);
                g.setColour (owner_.colour (Token::Text));
                g.fillRect (b.getX(), b.getY() + b.getHeight() * (1.0f - fm), b.getWidth(), 1.5f);
            }
        }
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (9.5f));
        if (kind_ == Gain)
            g.drawText (juce::String (gain_[0], 1) + " dB", area.withTrimmedTop (area.getHeight() - 28.0f).withHeight (14.0f), juce::Justification::centred);
        else
        {
            g.drawText (juce::String (dbOf (rmsMode_ ? std::max (rms_[0], rms_[1]) : std::max (peak_[0], peak_[1])), 1) + " dB",
                        area.withTrimmedTop (area.getHeight() - 42.0f).withHeight (14.0f), juce::Justification::centred);
            g.drawText ("MAX " + juce::String (dbOf (maxPeak_), 1), area.withTrimmedTop (area.getHeight() - 28.0f).withHeight (12.0f), juce::Justification::centred);
            g.setColour (owner_.colour (Token::Accent));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (rmsMode_ ? "RMS" : "PEAK", area.withTrimmedTop (area.getHeight() - 16.0f), juce::Justification::centred);
        }
    }
private:
    ShaperEditor& owner_;
    Kind kind_;
    float peak_[2] = { 0, 0 }, rms_[2] = { 0, 0 }, gain_[2] = { 0, 0 }, maxPeak_ = 0.0f;
    bool rmsMode_ = false;
};

// ------------------------------------------------------------------------------------------------ vue principale : WAVE / RESPONSE
class ShaperEditor::DetectionView : public juce::Component, private juce::Timer
{
public:
    explicit DetectionView (ShaperEditor& o) : owner_ (o), history_ (static_cast<std::size_t> (sh::kHistoryColumns)) { startTimerHz (60); }
    ~DetectionView() override { stopTimer(); }
    int view = 0;   // 0 WAVE, 1 RESPONSE

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (owner_.colour (Token::StageBg));
        g.fillRoundedRectangle (r, 6.0f);
        if (view == 0) paintWave (g, r.reduced (34.0f, 12.0f).withTrimmedBottom (6.0f));
        else paintResponse (g, r.reduced (40.0f, 16.0f));
    }

    void mouseDown (const juce::MouseEvent& e) override { grab (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (editing_ != 0) owner_.host_.uiEndEdit (editing_ == 1 ? sh::levelThreshold : sh::levelKnee);
        editing_ = 0;
    }

private:
    void timerCallback() override { if (isShowing() && view == 0) repaint(); }   // défilement fluide : 60 images par seconde
    // vue RESPONSE : abscisse = niveau du transitoire (-60..0 dB), ordonnée = poids w (0..1)
    juce::Rectangle<float> plot_;
    float xOf (double db) const { return plot_.getX() + static_cast<float> ((db + 60.0) / 60.0) * plot_.getWidth(); }
    double dbAt (float x) const { return (x - plot_.getX()) / plot_.getWidth() * 60.0 - 60.0; }

    void paintResponse (juce::Graphics& g, juce::Rectangle<float> area)
    {
        plot_ = area;
        const double level = owner_.value (sh::levelDependency) / 100.0, thr = owner_.value (sh::levelThreshold), knee = owner_.value (sh::levelKnee);
        g.setColour (owner_.colour (Token::Grid));
        for (int d = -60; d <= 0; d += 12) g.drawVerticalLine (static_cast<int> (xOf (d)), area.getY(), area.getBottom());
        for (int i = 0; i <= 4; ++i) g.drawHorizontalLine (static_cast<int> (area.getY() + area.getHeight() * i / 4.0f), area.getX(), area.getRight());
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (9.5f));
        for (int d = -60; d <= 0; d += 12) g.drawText (juce::String (d), static_cast<int> (xOf (d)) - 14, static_cast<int> (area.getBottom()) + 2, 28, 12, juce::Justification::centred);
        g.drawText (owner_.tx ("shp.response_hint"), area.withHeight (14.0f), juce::Justification::topLeft);
        juce::Path p;
        const double lv = std::fabs (level);
        for (int i = 0; i <= 240; ++i)
        {
            const double db = -60.0 + 60.0 * i / 240.0;
            double c = knee < 0.01 ? (db > thr ? 1.0 : 0.0) : std::clamp (0.5 + (db - thr) / knee, 0.0, 1.0);
            c = c * c * (3.0 - 2.0 * c);
            const double w = level >= 0.0 ? (1.0 - lv) + lv * c : (1.0 - lv) + lv * (1.0 - c);
            const float x = xOf (db), y = area.getBottom() - static_cast<float> (w) * area.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (owner_.colour (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (2.0f));
        // poignées : seuil (cercle), coude (carrés)
        const float xt = xOf (thr);
        g.setColour (owner_.colour (Token::KnobHot));
        g.fillEllipse (xt - 5.0f, area.getY() + area.getHeight() * 0.5f - 5.0f, 10.0f, 10.0f);
        if (knee >= 0.01)
            for (const double db : { thr - knee * 0.5, thr + knee * 0.5 })
                g.fillRect (xOf (db) - 4.0f, area.getY() + area.getHeight() * 0.5f + 12.0f, 8.0f, 8.0f);
    }
    void grab (const juce::MouseEvent& e)
    {
        if (view != 1) return;
        const double thr = owner_.value (sh::levelThreshold), knee = owner_.value (sh::levelKnee);
        const float cy = plot_.getY() + plot_.getHeight() * 0.5f;
        if (std::hypot (e.position.x - xOf (thr), e.position.y - cy) < 12.0f) editing_ = 1;
        else if (std::fabs (e.position.y - (cy + 16.0f)) < 10.0f && (std::fabs (e.position.x - xOf (thr - knee * 0.5)) < 10.0f || std::fabs (e.position.x - xOf (thr + knee * 0.5)) < 10.0f)) editing_ = 2;
        if (editing_ != 0) owner_.host_.uiBeginEdit (editing_ == 1 ? sh::levelThreshold : sh::levelKnee);
    }
    void drag (const juce::MouseEvent& e)
    {
        if (editing_ == 1) setPlain (sh::levelThreshold, std::clamp (dbAt (e.position.x), -60.0, 0.0));
        else if (editing_ == 2) setPlain (sh::levelKnee, std::clamp (2.0 * std::fabs (dbAt (e.position.x) - owner_.value (sh::levelThreshold)), 0.0, 24.0));
    }
    void setPlain (ParamId id, double v)
    {
        if (const auto* spec = owner_.host_.parameters().find (id)) owner_.host_.uiSetNormalised (id, spec->toNormalized (v));
    }

    // Moyenne glissante centrée (lissage de l'affichage : la précision d'une colonne n'est pas utile à l'oeil)
    static void smooth (std::vector<float>& v, int radius)
    {
        std::vector<float> out (v.size());
        const int n = static_cast<int> (v.size());
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            int cnt = 0;
            for (int k = std::max (0, i - radius); k <= std::min (n - 1, i + radius); ++k) { acc += v[static_cast<std::size_t> (k)]; ++cnt; }
            out[static_cast<std::size_t> (i)] = acc / static_cast<float> (cnt);
        }
        v.swap (out);
    }

    // vue WAVE : signal et enveloppes en haut, gain appliqué en bas (échelle +-RANGE). Défilement continu (la colonne en cours est
    // soustraite), contours remplis et lissés plutôt que des barres : l'image bouge sans saccade.
    void paintWave (juce::Graphics& g, juce::Rectangle<float> area)
    {
        if (owner_.module_ == nullptr) return;
        owner_.module_->copyHistory (history_.data());
        const float phase = owner_.module_->historyPhase();
        const int cols = sh::kHistoryColumns;
        const float cw = area.getWidth() / static_cast<float> (cols - 2);
        const float shift = phase * cw;
        const float topH = area.getHeight() * 0.58f;
        auto top = area.withHeight (topH), bottom = area.withTrimmedTop (topH + 12.0f);
        const float mid = top.getCentreY(), amp = topH * 0.5f;
        auto xAt = [&] (int i) { return area.getX() + cw * static_cast<float> (i) - shift; };
        g.setColour (owner_.colour (Token::Grid));
        g.drawHorizontalLine (static_cast<int> (mid), top.getX(), top.getRight());

        std::vector<float> inEnv (static_cast<std::size_t> (cols)), outEnv (inEnv.size()), fast (inEnv.size()), slow (inEnv.size()), gain (inEnv.size());
        for (int i = 0; i < cols; ++i)
        {
            const auto& c = history_[static_cast<std::size_t> (i)];
            inEnv[static_cast<std::size_t> (i)] = std::min (1.0f, c.inPeak);
            outEnv[static_cast<std::size_t> (i)] = std::min (1.0f, c.outPeak);
            fast[static_cast<std::size_t> (i)] = std::min (1.0f, std::pow (10.0f, c.fastDb / 20.0f));
            slow[static_cast<std::size_t> (i)] = std::min (1.0f, std::pow (10.0f, c.slowDb / 20.0f));
            gain[static_cast<std::size_t> (i)] = c.gainDb;
        }
        for (auto* v : { &inEnv, &outEnv }) smooth (*v, 2);
        for (auto* v : { &fast, &slow }) smooth (*v, 3);
        smooth (gain, 3);

        g.saveState();
        g.reduceClipRegion (area.expanded (2.0f, 0.0f).toNearestInt());
        auto fillEnvelope = [&] (const std::vector<float>& env, juce::Colour col)
        {
            juce::Path p;
            p.startNewSubPath (xAt (0), mid);
            for (int i = 0; i < cols; ++i) p.lineTo (xAt (i), mid - env[static_cast<std::size_t> (i)] * amp);
            for (int i = cols - 1; i >= 0; --i) p.lineTo (xAt (i), mid + env[static_cast<std::size_t> (i)] * amp);
            p.closeSubPath();
            g.setColour (col);
            g.fillPath (p);
        };
        fillEnvelope (inEnv, owner_.colour (Token::TextDim2).withAlpha (0.28f));
        fillEnvelope (outEnv, owner_.colour (Token::Accent).withAlpha (0.38f));
        auto line = [&] (const std::vector<float>& v, juce::Colour col, float thick)
        {
            juce::Path p;
            for (int i = 0; i < cols; ++i)
            {
                const float x = xAt (i), y = mid - v[static_cast<std::size_t> (i)] * amp;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        line (slow, owner_.colour (Token::Pink2).withAlpha (0.9f), 1.3f);
        line (fast, owner_.colour (Token::KnobHot2).withAlpha (0.9f), 1.3f);
        // gain
        const float range = static_cast<float> (owner_.value (sh::range)), gmid = bottom.getCentreY();
        g.setColour (owner_.colour (Token::Grid));
        for (const float dbv : { -range, 0.0f, range }) g.drawHorizontalLine (static_cast<int> (gmid - dbv / range * bottom.getHeight() * 0.5f), bottom.getX(), bottom.getRight());
        juce::Path gp;
        for (int i = 0; i < cols; ++i)
        {
            const float gdb = juce::jlimit (-range, range, gain[static_cast<std::size_t> (i)]);
            const float x = xAt (i), y = gmid - gdb / range * bottom.getHeight() * 0.5f;
            if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
        }
        g.setColour (owner_.colour (Token::Accent));
        g.strokePath (gp, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();

        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText ("+" + juce::String (range, 0), static_cast<int> (area.getX()) - 32, static_cast<int> (bottom.getY()) - 5, 28, 12, juce::Justification::centredRight);
        g.drawText ("0 dB", static_cast<int> (area.getX()) - 32, static_cast<int> (gmid) - 6, 28, 12, juce::Justification::centredRight);
        g.drawText ("-" + juce::String (range, 0), static_cast<int> (area.getX()) - 32, static_cast<int> (bottom.getBottom()) - 6, 28, 12, juce::Justification::centredRight);
        const double span = sh::kHistorySeconds * std::clamp (owner_.value (sh::displaySpan), 50.0, 800.0) / 100.0;
        for (int t = 0; t <= 8; ++t)
            g.drawText (juce::String (span * t / 8.0, span >= 4.0 ? 1 : 2) + " s", static_cast<int> (area.getX() + area.getWidth() * t / 8.0f) - 18, static_cast<int> (area.getBottom()) + 1, 36, 12, juce::Justification::centred);
    }

    ShaperEditor& owner_;
    std::vector<sh::Column> history_;
    int editing_ = 0;
};

// ------------------------------------------------------------------------------------------------ égaliseur de détection (page Sidechain)
class ShaperEditor::EqView : public juce::Component
{
public:
    explicit EqView (ShaperEditor& o) : owner_ (o), key_ (4096), spectrum_ (64, -120.0f) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (owner_.colour (Token::StageBg));
        g.fillRoundedRectangle (r, 6.0f);
        plot_ = r.reduced (36.0f, 14.0f);
        const bool eqOn = owner_.value (sh::scEqOn) >= 0.5;
        g.setColour (owner_.colour (Token::Grid));
        for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine (static_cast<int> (xOf (f)), plot_.getY(), plot_.getBottom());
        for (const double d : { -12.0, -6.0, 0.0, 6.0, 12.0 }) g.drawHorizontalLine (static_cast<int> (yOf (d)), plot_.getX(), plot_.getRight());
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (9.5f));
        for (const double f : { 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 })
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0), static_cast<int> (xOf (f)) - 14, static_cast<int> (plot_.getBottom()) + 2, 28, 12, juce::Justification::centred);
        for (const double d : { -12.0, 0.0, 12.0 }) g.drawText (juce::String (d, 0), 2, static_cast<int> (yOf (d)) - 6, 30, 12, juce::Justification::centredRight);
        paintSpectrum (g);
        juce::Path p;
        for (int i = 0; i <= 300; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 300.0);
            const double db = eqOn && owner_.module_ != nullptr ? owner_.module_->detectionResponseDb (owner_.host_.parameters(), f) : 0.0;
            const float x = xOf (f), y = yOf (std::clamp (db, -24.0, 18.0));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (owner_.colour (Token::Accent).withAlpha (eqOn ? 1.0f : 0.35f));
        g.strokePath (p, juce::PathStrokeType (2.0f));
        // poignées : HP et LP sur la ligne de 0 dB, bandes à (f, gain)
        for (int h = 0; h < 5; ++h)
        {
            const auto pt = handle (h);
            g.setColour ((h == 0 || h == 4 ? owner_.colour (Token::Band1) : owner_.colour (Token::Band2)).withAlpha (eqOn ? 1.0f : 0.35f));
            g.fillEllipse (pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f);
            g.setColour (owner_.colour (Token::BgApp));
            g.drawEllipse (pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f, 1.2f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        active_ = -1;
        for (int h = 0; h < 5; ++h)
            if (handle (h).getDistanceFrom (e.position) < 12.0f) active_ = h;
        if (active_ >= 0) { owner_.host_.uiBeginEdit (freqId (active_)); if (active_ >= 1 && active_ <= 3) owner_.host_.uiBeginEdit (freqId (active_) + 1); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (active_ < 0) return;
        const double f = std::clamp (fOf (e.position.x), 20.0, 20000.0);
        set (freqId (active_), f);
        if (active_ >= 1 && active_ <= 3) set (freqId (active_) + 1, std::clamp (dbAt (e.position.y), -18.0, 18.0));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (active_ >= 0) { owner_.host_.uiEndEdit (freqId (active_)); if (active_ >= 1 && active_ <= 3) owner_.host_.uiEndEdit (freqId (active_) + 1); }
        active_ = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        for (int h = 1; h <= 3; ++h)
            if (handle (h).getDistanceFrom (e.position) < 12.0f) owner_.host_.uiResetToDefault (freqId (h) + 1);
    }

private:
    static ParamId freqId (int h) { return h == 0 ? sh::scHpFreq : h == 4 ? sh::scLpFreq : sh::scBand1Freq + 3 * static_cast<ParamId> (h - 1); }
    float xOf (double f) const { return plot_.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * plot_.getWidth(); }
    double fOf (float x) const { return 20.0 * std::pow (1000.0, (x - plot_.getX()) / plot_.getWidth()); }
    float yOf (double db) const { return plot_.getY() + static_cast<float> ((18.0 - db) / 36.0) * plot_.getHeight(); }
    double dbAt (float y) const { return 18.0 - (y - plot_.getY()) / plot_.getHeight() * 36.0; }
    juce::Point<float> handle (int h) const
    {
        const double f = owner_.value (freqId (h));
        const double gain = h >= 1 && h <= 3 ? owner_.value (freqId (h) + 1) : 0.0;
        return { xOf (f), yOf (gain) };
    }
    void set (ParamId id, double v)
    {
        if (const auto* spec = owner_.host_.parameters().find (id)) owner_.host_.uiSetNormalised (id, spec->toNormalized (v));
    }
    void paintSpectrum (juce::Graphics& g)
    {
        if (owner_.module_ == nullptr || owner_.value (sh::scAnalyzerOn) < 0.5) return;
        const int n = 2048;
        owner_.module_->copyKey (key_.data(), n);
        std::vector<std::complex<float>> a (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i) a[static_cast<std::size_t> (i)] = key_[static_cast<std::size_t> (i)] * (0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float> (i) / static_cast<float> (n)));
        fft (a);
        const double fs = owner_.options_.sampleRate ? owner_.options_.sampleRate() : 48000.0;
        const int bands = static_cast<int> (spectrum_.size());
        juce::Path p;
        for (int b = 0; b < bands; ++b)
        {
            const double f0 = 20.0 * std::pow (1000.0, static_cast<double> (b) / bands), f1 = 20.0 * std::pow (1000.0, static_cast<double> (b + 1) / bands);
            const int k0 = std::max (1, static_cast<int> (f0 / fs * n)), k1 = std::max (k0 + 1, std::min (n / 2, static_cast<int> (f1 / fs * n)));
            float m = 0.0f;
            for (int k = k0; k < k1; ++k) m = std::max (m, std::abs (a[static_cast<std::size_t> (k)]));
            const float db = dbOf (m / (static_cast<float> (n) * 0.25f));
            auto& s = spectrum_[static_cast<std::size_t> (b)];
            s = db > s ? db : s * 0.92f + db * 0.08f;
            const float x = xOf (std::sqrt (f0 * f1)), y = plot_.getBottom() - juce::jlimit (0.0f, 1.0f, (s + 90.0f) / 90.0f) * plot_.getHeight();
            if (b == 0) { p.startNewSubPath (x, plot_.getBottom()); p.lineTo (x, y); } else p.lineTo (x, y);
        }
        p.lineTo (xOf (20000.0), plot_.getBottom());
        p.closeSubPath();
        g.setColour (owner_.colour (Token::Accent2).withAlpha (0.22f));
        g.fillPath (p);
    }

    ShaperEditor& owner_;
    juce::Rectangle<float> plot_;
    std::vector<float> key_;
    std::vector<float> spectrum_;
    int active_ = -1;
};

// ------------------------------------------------------------------------------------------------ éditeur
ShaperEditor::ShaperEditor (ParamHost& host, const ShaperModule* module, const theme::Palette& palette, const i18n::Translator& tr,
                            theme::KnobStyle knobStyle, Options options)
    : host_ (host), module_ (module), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), options_ (std::move (options))
{
    setWantsKeyboardFocus (true);
    surface_ = std::make_unique<Surface> (*this);
    surface_->setSize (designWidth, designHeight);
    addAndMakeVisible (*surface_);
    buildHeader();
    buildShaperPage();
    buildSidechainPage();
    buildSettingsPage();
    setPage (0);
    refreshStates();
    startTimerHz (30);
    setSize (designWidth, designHeight);
}

ShaperEditor::~ShaperEditor() { stopTimer(); }

juce::Colour ShaperEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String ShaperEditor::tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void ShaperEditor::setValue (ParamId id, double plain)
{
    const auto* spec = host_.parameters().find (id);
    if (spec == nullptr) return;
    host_.uiBeginEdit (id);
    host_.uiSetNormalised (id, spec->toNormalized (plain));
    host_.uiEndEdit (id);
}

void ShaperEditor::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (host_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<ShaperEditor> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    surface_->addAndMakeVisible (*modal_);
    modal_->setBounds (surface_->getLocalBounds());
    modal_->toFront (true);
}

void ShaperEditor::closeModal()
{
    if (! modal_) return;
    host_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

ShaperEditor::ParamBar* ShaperEditor::addBar (juce::Component& parent, std::vector<juce::Component*>& page, ParamId id, const char* labelKey, bool horizontal)
{
    auto b = std::make_unique<ParamBar> (*this, id, tx (labelKey), horizontal);
    auto* raw = b.get();
    parent.addAndMakeVisible (*raw);
    page.push_back (raw);
    bars_.push_back (raw);
    owned_.push_back (std::move (b));
    return raw;
}

ShaperEditor::StateButton* ShaperEditor::addState (juce::Component& parent, std::vector<juce::Component*>& page, ParamId id, const char* labelKey,
                                                   std::vector<const char*> choices)
{
    std::vector<juce::String> texts;
    for (const auto* c : choices) texts.push_back (tx (c));
    auto b = std::make_unique<StateButton> (*this, id, labelKey != nullptr ? tx (labelKey) : juce::String(), std::move (texts));
    auto* raw = b.get();
    parent.addAndMakeVisible (*raw);
    page.push_back (raw);
    states_.push_back (raw);
    owned_.push_back (std::move (b));
    return raw;
}

void ShaperEditor::buildHeader()
{
    auto& s = *surface_;
    presetButton_.setButtonText (juce::String::fromUTF8 (sh::factoryPresets()[0].name));
    presetButton_.onClick = [this] { showPresetMenu(); };
    s.addAndMakeVisible (presetButton_);
    abButton_.setButtonText ("A");
    abButton_.onClick = [this] { toggleAB(); };
    s.addAndMakeVisible (abButton_);
    if (options_.bypass)
    {
        bypassButton_.setButtonText (tx ("shp.bypass"));
        bypassButton_.setColour (juce::TextButton::buttonOnColourId, colour (Token::KnobHot));
        bypassButton_.onClick = [this] { setValue (stock::bypass, value (stock::bypass) >= 0.5 ? 0.0 : 1.0); };
        s.addAndMakeVisible (bypassButton_);
    }
    static const char* const pageKeys[3] = { "shp.page_shaper", "shp.page_sidechain", "shp.page_settings" };
    for (int i = 0; i < 3; ++i)
    {
        navButtons_[static_cast<std::size_t> (i)].setButtonText (tx (pageKeys[i]));
        navButtons_[static_cast<std::size_t> (i)].setColour (juce::TextButton::buttonOnColourId, colour (Token::Accent).withAlpha (0.35f));
        navButtons_[static_cast<std::size_t> (i)].onClick = [this, i] { setPage (i); };
        s.addAndMakeVisible (navButtons_[static_cast<std::size_t> (i)]);
    }
    latencyLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    latencyLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    latencyLabel_.setJustificationType (juce::Justification::centredRight);
    s.addAndMakeVisible (latencyLabel_);

    inMeter_ = std::make_unique<LevelMeter> (*this, LevelMeter::In);
    gainMeter_ = std::make_unique<LevelMeter> (*this, LevelMeter::Gain);
    outMeter_ = std::make_unique<LevelMeter> (*this, LevelMeter::Out);
    for (auto* m : { inMeter_.get(), gainMeter_.get(), outMeter_.get() }) s.addAndMakeVisible (*m);
    inMeter_->setBounds (24, 92, 44, 380);
    gainMeter_->setBounds (68, 92, 44, 380);
    outMeter_->setBounds (112, 92, 44, 380);
}

void ShaperEditor::buildShaperPage()
{
    auto& s = *surface_;
    auto& page = pages_[0];
    detection_ = std::make_unique<DetectionView> (*this);
    s.addAndMakeVisible (*detection_);
    page.push_back (detection_.get());
    viewButton_.setButtonText ("WAVE");
    viewButton_.onClick = [this]
    {
        detection_->view ^= 1;
        viewButton_.setButtonText (detection_->view != 0 ? "RESPONSE" : "WAVE");
        refreshStates();
    };
    s.addAndMakeVisible (viewButton_);
    page.push_back (&viewButton_);
    // rangée des temps et de l'amplitude, sous le graphique
    addBar (s, page, sh::attackTime, "shp.attack_time");
    addBar (s, page, sh::sustainTime, "shp.sustain_time");
    addBar (s, page, sh::shapeSmoothing, "shp.smoothing");
    addBar (s, page, sh::range, "shp.range");
    addBar (s, page, sh::levelKnee, "shp.knee");
    addBar (s, page, sh::stereoLink, "shp.link");
    // potards : ATTACK, SUSTAIN, LEVEL, THRESHOLD, OUTPUT, MIX (jeu 1 ; le jeu 2 s'ouvre en Dual Mono / M-S par les mêmes potards)
    struct K { ParamId id; const char* key; };
    for (const K k : { K { sh::attackGain1, "shp.attack" }, K { sh::sustainGain1, "shp.sustain" }, K { sh::levelDependency, "shp.level" },
                       K { sh::levelThreshold, "shp.threshold" }, K { stock::outputTrim, "shp.output" }, K { stock::mix, "shp.mix" } })
    {
        auto knob = std::make_unique<NeptuneKnob> (host_, k.id, palette_, knobStyle_, [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); });
        s.addAndMakeVisible (*knob);
        page.push_back (knob.get());
        knobs_.push_back ({ knob.get(), k.id });
        owned_.push_back (std::move (knob));
    }
    // jeu 2 (Dual Mono / M-S) : deux barres sous les potards ATTACK / SUSTAIN
    addBar (s, page, sh::attackGain2, "shp.attack_2");
    addBar (s, page, sh::sustainGain2, "shp.sustain_2");
    // barre du bas
    addState (s, page, sh::channelMode, nullptr, { "shp.ch_stereo", "shp.ch_dual", "shp.ch_ms" });
    addState (s, page, sh::delta, "shp.delta");
    addBar (s, page, sh::lookahead, "shp.lookahead", true);
    addState (s, page, sh::oversampling, "shp.oversampling", { "shp.os_none", "shp.os_2x", "shp.os_4x" });
}

void ShaperEditor::buildSidechainPage()
{
    auto& s = *surface_;
    auto& page = pages_[1];
    eq_ = std::make_unique<EqView> (*this);
    s.addAndMakeVisible (*eq_);
    page.push_back (eq_.get());
    addState (s, page, sh::scEnable, "shp.sc_enable");
    auto* src = addState (s, page, sh::scSource, nullptr, { "shp.sc_internal", "shp.sc_external", "shp.sc_mix" });
    if (! options_.externalKey) { src->disabledChoices.insert (1); src->disabledChoices.insert (2); }
    addBar (s, page, sh::scGain, "shp.sc_gain");
    addBar (s, page, sh::scIntVol, "shp.sc_int_vol");
    addBar (s, page, sh::scExtVol, "shp.sc_ext_vol");
    addState (s, page, sh::scPhaseInt, "shp.sc_phase_int");
    addState (s, page, sh::scPhaseExt, "shp.sc_phase_ext");
    addState (s, page, sh::scExtMs, "shp.sc_ext_ms");
    addState (s, page, sh::scListen, "shp.sc_listen");
    addState (s, page, sh::scEqOn, "shp.sc_eq");
    addState (s, page, sh::scAnalyzerOn, "shp.sc_spectrum");
    addBar (s, page, sh::scHpFreq, "shp.sc_hp_freq");
    addBar (s, page, sh::scHpSlope, "shp.sc_slope")->setStep (6.0);
    for (int b = 0; b < 3; ++b)
    {
        const ParamId base = sh::scBand1Freq + 3 * static_cast<ParamId> (b);
        addBar (s, page, base, b == 0 ? "shp.sc_b1_freq" : b == 1 ? "shp.sc_b2_freq" : "shp.sc_b3_freq");
        addBar (s, page, base + 1, "shp.sc_gain_band");
        addBar (s, page, base + 2, "shp.sc_q");
    }
    addBar (s, page, sh::scLpFreq, "shp.sc_lp_freq");
    addBar (s, page, sh::scLpSlope, "shp.sc_slope")->setStep (6.0);
}

void ShaperEditor::buildSettingsPage()
{
    auto& s = *surface_;
    auto& page = pages_[2];
    addState (s, page, sh::detMode, "shp.set_detection", { "shp.det_peak", "shp.det_rms" });
    addBar (s, page, sh::detFastRatio, "shp.set_ratio");
    addBar (s, page, sh::noiseFloor, "shp.set_floor");
    addBar (s, page, sh::meterRmsMs, "shp.set_rms_window");
    addBar (s, page, sh::meterPeakDecay, "shp.set_peak_decay");
    addBar (s, page, sh::displaySpan, "shp.set_display_span");
    settingsInfo_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    settingsInfo_.setFont (juce::FontOptions (12.0f));
    settingsInfo_.setJustificationType (juce::Justification::topLeft);
    s.addAndMakeVisible (settingsInfo_);
    page.push_back (&settingsInfo_);
}

void ShaperEditor::setPage (int page)
{
    page_ = juce::jlimit (0, 2, page);
    for (int p = 0; p < 3; ++p)
        for (auto* c : pages_[static_cast<std::size_t> (p)]) c->setVisible (p == page_);
    for (int i = 0; i < 3; ++i) navButtons_[static_cast<std::size_t> (i)].setToggleState (i == page_, juce::dontSendNotification);
    resized();
    refreshStates();
}

std::map<ParamId, double> ShaperEditor::snapshot() const
{
    std::map<ParamId, double> m;
    for (const auto id : sh::presetParams()) m[id] = value (id);
    return m;
}

void ShaperEditor::restore (const std::map<ParamId, double>& m)
{
    for (const auto& [id, v] : m) setValue (id, v);
}

void ShaperEditor::toggleAB()
{
    // A/B : deux jeux complets ; on bascule en gardant l'autre dans l'éditeur (non enregistré dans le projet)
    auto current = snapshot();
    if (otherSlot_.empty()) otherSlot_ = current;
    restore (otherSlot_);
    otherSlot_ = std::move (current);
    slotB_ = ! slotB_;
    abButton_.setButtonText (slotB_ ? "B" : "A");
}

void ShaperEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = sh::factoryPresets();
    for (std::size_t i = 0; i < list.size(); ++i) menu.addItem (static_cast<int> (i) + 1, juce::String::fromUTF8 (list[i].name), true, static_cast<int> (i) == presetIndex_);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton_), [safe = juce::Component::SafePointer<ShaperEditor> (this)] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        const auto& l = sh::factoryPresets();
        const auto& p = l[static_cast<std::size_t> (choice - 1)];
        safe->presetIndex_ = choice - 1;
        // un geste par paramètre modifié (annulable, visible de l'hôte)
        for (const auto id : sh::presetParams())
        {
            double v = safe->host_.parameters().find (id) != nullptr ? safe->host_.parameters().find (id)->defaultValue : 0.0;
            for (const auto& [pid, val] : p.values) if (pid == id) v = val;
            if (safe->value (id) != v) safe->setValue (id, v);
        }
        safe->presetButton_.setButtonText (juce::String::fromUTF8 (p.name));
    });
}

void ShaperEditor::refreshStates()
{
    const int mode = static_cast<int> (std::lround (value (sh::channelMode)));
    const bool levelOn = std::fabs (value (sh::levelDependency)) > 0.01;
    const int src = static_cast<int> (std::lround (value (sh::scSource)));
    const bool scOn = value (sh::scEnable) >= 0.5;
    auto enable = [this] (ParamId id, bool on)
    {
        for (auto* b : bars_) if (b->id() == id) { b->setEnabled (on); b->setAlpha (on ? 1.0f : 0.35f); }
        for (auto* b : states_) if (b->id() == id) { b->setEnabled (on); b->setAlpha (on ? 1.0f : 0.35f); }
        for (auto& [k, kid] : knobs_) if (kid == id) { k->setEnabled (on); k->setAlpha (on ? 1.0f : 0.35f); }
    };
    enable (sh::stereoLink, mode == 0);
    enable (sh::attackGain2, mode != 0);
    enable (sh::sustainGain2, mode != 0);
    enable (sh::levelThreshold, levelOn);
    enable (sh::levelKnee, levelOn);
    enable (sh::scSource, scOn);
    enable (sh::scGain, scOn && src != 2);
    enable (sh::scIntVol, scOn && src == 2);
    enable (sh::scExtVol, scOn && src == 2);
    enable (sh::scPhaseInt, scOn && src == 2);
    enable (sh::scPhaseExt, scOn && src == 2);
    enable (sh::scExtMs, scOn && src != 0);
    enable (sh::scListen, scOn);
    enable (sh::scEqOn, scOn);
    for (auto* b : states_) b->refresh();
    const double rate = options_.sampleRate ? options_.sampleRate() : 48000.0;
    const int latency = module_ != nullptr ? static_cast<int> (std::lround (value (sh::lookahead) * 0.001 * rate))
                                                   + Oversampler::latencyFor (static_cast<int> (std::lround (value (sh::oversampling))) >= 2 ? 4 : static_cast<int> (std::lround (value (sh::oversampling))) == 1 ? 2 : 1) : 0;
    latencyLabel_.setText (juce::String::fromUTF8 (tr_.format ("shp.latency", { std::to_string (latency) }).c_str()), juce::dontSendNotification);
    if (options_.bypass) bypassButton_.setToggleState (value (stock::bypass) >= 0.5, juce::dontSendNotification);
    settingsInfo_.setText (tx ("shp.set_info") + "\n\n" + tx ("shp.set_sample_rate") + ": " + juce::String (rate, 0) + " Hz\n" + tx ("shp.set_latency") + ": "
                               + juce::String (latency) + " smp (" + juce::String (latency * 1000.0 / rate, 2) + " ms)",
                           juce::dontSendNotification);
}

void ShaperEditor::timerCallback()
{
    refreshStates();
    inMeter_->update();
    gainMeter_->update();
    outMeter_->update();
    for (auto* b : bars_) if (b->isVisible()) b->repaint();
    if (page_ == 1) eq_->repaint();
    else if (page_ == 0 && detection_->view != 0) detection_->repaint();
}

bool ShaperEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z') { if (m.isShiftDown()) host_.uiRedo(); else host_.uiUndo(); return true; }
    if (code == 'Y' || code == 'y') { host_.uiRedo(); return true; }
    return false;
}

void ShaperEditor::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgApp)); }

void ShaperEditor::resized()
{
    const float s = std::min (static_cast<float> (getWidth()) / designWidth, static_cast<float> (getHeight()) / designHeight);
    surface_->setTransform (juce::AffineTransform::scale (s).translated ((static_cast<float> (getWidth()) - designWidth * s) * 0.5f,
                                                                           (static_cast<float> (getHeight()) - designHeight * s) * 0.5f));
    surface_->setBounds (0, 0, designWidth, designHeight);

    const juce::Rectangle<int> header (20, 14, designWidth - 40, 44);
    int x = header.getRight();
    if (options_.bypass) { x -= 90; bypassButton_.setBounds (x, header.getY() + 6, 84, 32); }
    presetButton_.setBounds (header.getCentreX() - 130, header.getY() + 6, 220, 32);
    abButton_.setBounds (header.getCentreX() + 100, header.getY() + 6, 36, 32);
    for (int i = 0; i < 3; ++i) navButtons_[static_cast<std::size_t> (i)].setBounds (24, 492 + i * 52, 132, 44);

    const juce::Rectangle<int> main (176, 80, 964, 392);
    const juce::Rectangle<int> bottom (176, 704, 964, 56);
    latencyLabel_.setBounds (bottom.getX() + 380, bottom.getY() + 12, 150, 32);

    auto place = [] (juce::Component* c, int x0, int y0, int w, int h) { c->setBounds (x0, y0, w, h); };
    auto find = [this] (ParamId id) -> juce::Component*
    {
        for (auto* b : bars_) if (b->id() == id) return b;
        for (auto* b : states_) if (b->id() == id) return b;
        for (auto& [k, kid] : knobs_) if (kid == id) return k;
        return nullptr;
    };
    // ---- page Shaper
    {
        detection_->setBounds (main.withTrimmedBottom (62));
        viewButton_.setBounds (main.getRight() - 120, main.getY() + 6, 110, 26);
        const int w = (main.getWidth() - 5 * 8) / 6;
        int x0 = main.getX();
        for (const auto id : { sh::attackTime, sh::sustainTime, sh::shapeSmoothing, sh::range, sh::levelKnee, sh::stereoLink })
        {
            place (find (id), x0, main.getBottom() - 54, w, 54);
            x0 += w + 8;
        }
        const int kw = (main.getWidth() - 5 * 10) / 6;
        x0 = main.getX();
        for (const auto id : { sh::attackGain1, sh::sustainGain1, sh::levelDependency, sh::levelThreshold, stock::outputTrim, stock::mix })
        {
            place (find (id), x0 + 20, 492, kw - 40, kw - 40 + 10);
            x0 += kw + 10;
        }
        place (find (sh::attackGain2), main.getX(), 650, kw, 40);
        place (find (sh::sustainGain2), main.getX() + kw + 10, 650, kw, 40);
        place (find (sh::channelMode), bottom.getX(), bottom.getY() + 12, 150, 32);
        place (find (sh::delta), bottom.getX() + 160, bottom.getY() + 12, 100, 32);
        place (find (sh::lookahead), bottom.getX() + 548, bottom.getY() + 6, 190, 44);
        place (find (sh::oversampling), bottom.getX() + 752, bottom.getY() + 12, 200, 32);
    }
    // ---- page Sidechain
    {
        auto left = juce::Rectangle<int> (main.getX(), main.getY(), 250, main.getHeight() + 220);
        int y = left.getY();
        for (const auto id : { sh::scEnable, sh::scSource }) { place (find (id), left.getX(), y, 240, 32); y += 40; }
        place (find (sh::scGain), left.getX(), y, 240, 50); y += 56;
        place (find (sh::scIntVol), left.getX(), y, 117, 50);
        place (find (sh::scExtVol), left.getX() + 123, y, 117, 50); y += 56;
        place (find (sh::scPhaseInt), left.getX(), y, 117, 30);
        place (find (sh::scPhaseExt), left.getX() + 123, y, 117, 30); y += 38;
        place (find (sh::scExtMs), left.getX(), y, 240, 30); y += 38;
        place (find (sh::scListen), left.getX(), y, 240, 34); y += 42;
        place (find (sh::scEqOn), left.getX(), y, 117, 30);
        place (find (sh::scAnalyzerOn), left.getX() + 123, y, 117, 30);
        eq_->setBounds (main.getX() + 262, main.getY(), main.getWidth() - 262, 300);
        // zones d'égaliseur : HP, 3 bandes, LP
        const int cw = (main.getWidth() - 262 - 4 * 8) / 5, top = main.getY() + 312;
        int cx = main.getX() + 262;
        auto column = [&] (std::initializer_list<ParamId> ids)
        {
            int cy = top;
            for (const auto id : ids) { place (find (id), cx, cy, cw, 50); cy += 56; }
            cx += cw + 8;
        };
        column ({ sh::scHpFreq, sh::scHpSlope });
        column ({ sh::scBand1Freq, sh::scBand1Freq + 1, sh::scBand1Freq + 2 });
        column ({ sh::scBand2Freq, sh::scBand2Freq + 1, sh::scBand2Freq + 2 });
        column ({ sh::scBand3Freq, sh::scBand3Freq + 1, sh::scBand3Freq + 2 });
        column ({ sh::scLpFreq, sh::scLpSlope });
    }
    // ---- page Settings
    {
        int y = main.getY();
        place (find (sh::detMode), main.getX(), y, 300, 36); y += 48;
        for (const auto id : { sh::detFastRatio, sh::noiseFloor, sh::meterRmsMs, sh::meterPeakDecay, sh::displaySpan }) { place (find (id), main.getX(), y, 300, 54); y += 62; }
        settingsInfo_.setBounds (main.getX() + 340, main.getY(), 560, 300);
    }
    if (modal_) modal_->setBounds (surface_->getLocalBounds());
}
