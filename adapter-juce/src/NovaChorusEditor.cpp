#include "NovaChorusEditor.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "neptune/core/Biquad.h"
#include "neptune/core/ChorusPresets.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace ch = neptune::core::chorus;

namespace
{
constexpr double kPi = 3.14159265358979323846;
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }
double triangle (double phase)
{
    phase -= std::floor (phase);
    return 1.0 - 4.0 * std::fabs (phase - 0.5);
}

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // ondes
    {
        p.startNewSubPath (2.0f, 12.0f);
        p.cubicTo (6.0f, 2.0f, 9.0f, 2.0f, 12.0f, 12.0f);
        p.cubicTo (15.0f, 22.0f, 18.0f, 22.0f, 22.0f, 12.0f);
    }
    else if (kind == 1)   // réponse en fréquence
    {
        p.startNewSubPath (3.0f, 18.0f);
        p.lineTo (9.0f, 18.0f);
        p.lineTo (14.0f, 6.0f);
        p.lineTo (21.0f, 6.0f);
    }
    else   // réglages
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (14.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (6.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (12.0f, 16.0f, 4.0f, 4.0f);
    }
    return p;
}

void setPlain (ParamHost& host, ParamId id, double v)
{
    if (const auto* spec = host.parameters().find (id)) host.uiSetNormalised (id, spec->toNormalized (v));
}
} // namespace

// ------------------------------------------------------------------------------------------------ vue de la modulation : retards ou réponse du peigne
class NovaChorusEditor::ModView : public juce::Component
{
public:
    ModView (const nova::Skin& s, ParamHost& h, const ChorusModule* m) : skin_ (s), host_ (h), module_ (m) {}

    bool response = false;

    void update()
    {
        // le point animé suit la phase du LFO : on redessine à chaque pas (30 Hz)
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        if (response) paintResponse (g); else paintDelay (g);
    }

private:
    double get (ParamId id) const { return host_.parameters().get (id); }
    bool tal() const { return get (ch::mode) >= 0.5; }

    // retard (ms) à la phase donnée pour un canal (0 gauche, 1 droite) et une voix, mode standard
    double standardDelayMs (double phase, int channel, int voice, int nv) const
    {
        double ph = phase + (channel == 1 ? get (ch::width) * 0.005 : 0.0) + static_cast<double> (voice) / nv;
        ph -= std::floor (ph);
        const double lfo = get (ch::lfoShape) < 0.5 ? triangle (ph) : std::sin (2.0 * kPi * ph);
        return std::max (0.05, get (ch::delayMs) * (1.0 + get (ch::depth) * 0.01 * lfo));
    }
    static double talDelayMs (double phase, int unit, int channel)
    {
        const double offset = (unit == 0) == (channel == 0) ? 0.5 : 0.0;
        return ch::talCentreMs + ch::talSwingMs * triangle (phase + offset);
    }
    int voiceCount() const { return std::clamp (static_cast<int> (std::lround (get (ch::voices))) + 1, 1, ch::kMaxVoices); }

    void paintDelay (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const auto plot = juce::Rectangle<float> (b.getX() + skin_.px (44.0f), b.getY() + skin_.px (14.0f), b.getWidth() - skin_.px (60.0f), b.getHeight() - skin_.px (40.0f));
        const bool isTal = tal();
        const int nv = voiceCount();
        // axe horizontal : un cycle de LFO (standard) ou deux cycles de l'unité la plus lente (TAL)
        double spanSec = 1.0;
        if (isTal)
        {
            double slow = 100.0;
            if (get (ch::talI) >= 0.5) slow = std::min (slow, get (ch::talRate1));
            if (get (ch::talII) >= 0.5) slow = std::min (slow, get (ch::talRate2));
            if (slow > 99.0) slow = std::min (get (ch::talRate1), get (ch::talRate2));
            spanSec = std::clamp (2.0 / slow, 0.4, 8.0);
        }
        double maxMs = 1.0;
        if (isTal) maxMs = ch::talCentreMs + ch::talSwingMs;
        else maxMs = std::max (1.0, get (ch::delayMs) * (1.0 + get (ch::depth) * 0.01));
        maxMs *= 1.15;
        auto xOf = [&] (double frac) { return plot.getX() + static_cast<float> (frac) * plot.getWidth(); };
        auto yOf = [&] (double ms) { return plot.getBottom() - static_cast<float> (ms / maxMs) * plot.getHeight(); };
        g.setFont (skin_.font (9.0f));
        double step = 1.0;
        for (const double s : { 0.5, 1.0, 2.0, 5.0, 10.0, 20.0 }) { step = s; if (maxMs / s <= 6.0) break; }
        for (double ms = 0.0; ms <= maxMs; ms += step)
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (plot.getX(), yOf (ms) - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (ms, step < 1.0 ? 1 : 0) + " ms", juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (ms) - skin_.px (6.0f), skin_.px (38.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        for (int t = 0; t <= 4; ++t)
        {
            const float x = xOf (static_cast<double> (t) / 4.0);
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (x - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (isTal ? juce::String (spanSec * t / 4.0, 1) + " s" : juce::String (t * 25) + " %", juce::Rectangle<float> (x - skin_.px (24.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (48.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        auto curve = [&] (auto delayAt, juce::Colour colour, float thickness)
        {
            juce::Path p;
            for (int i = 0; i <= 240; ++i)
            {
                const double frac = i / 240.0;
                const float x = xOf (frac), y = yOf (delayAt (frac));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (skin_.px (thickness), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        if (! isTal)
        {
            for (int v = nv - 1; v >= 0; --v)
                for (int c = 1; c >= 0; --c)
                    curve ([&, v, c] (double frac) { return standardDelayMs (frac, c, v, nv); }, skin_.colour (c == 0 ? Token::Accent : Token::Pink, v == 0 ? 1.0f : 0.45f), v == 0 ? 2.2f : 1.4f);
            // points animés sur la voix 1 : phase du LFO gauche (la droite s'en déduit avec la largeur)
            const double phase = module_ != nullptr ? static_cast<double> (module_->meter (ch::kLfoPhase)) : 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const float x = xOf (phase), y = yOf (standardDelayMs (phase, c, 0, nv));
                g.setColour (skin_.colour (Token::Handle));
                g.fillEllipse (x - skin_.px (5.0f), y - skin_.px (5.0f), skin_.px (10.0f), skin_.px (10.0f));
                g.setColour (skin_.colour (c == 0 ? Token::Accent : Token::Pink));
                g.drawEllipse (x - skin_.px (5.0f), y - skin_.px (5.0f), skin_.px (10.0f), skin_.px (10.0f), skin_.px (1.6f));
            }
        }
        else
        {
            const double rates[2] = { get (ch::talRate1), get (ch::talRate2) };
            for (int u = 1; u >= 0; --u)
            {
                const bool on = get (u == 0 ? ch::talI : ch::talII) >= 0.5;
                for (int c = 1; c >= 0; --c)
                    curve ([&, u, c] (double frac) { return talDelayMs (frac * spanSec * rates[u], u, c); },
                           skin_.colour (c == 0 ? (u == 0 ? Token::Accent : Token::Accent2) : (u == 0 ? Token::Pink : Token::Pink2), on ? 1.0f : 0.25f), on ? 2.0f : 1.2f);
            }
        }
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (11.0f, true));
        g.drawText (tx (isTal ? "I / II" : "L / R"), juce::Rectangle<float> (plot.getRight() - skin_.px (90.0f), plot.getY(), skin_.px (86.0f), skin_.px (16.0f)), juce::Justification::topRight);
    }

    static juce::String tx (const char* s) { return juce::String::fromUTF8 (s); }

    void paintResponse (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        const auto plot = juce::Rectangle<float> (b.getX() + skin_.px (44.0f), b.getY() + skin_.px (14.0f), b.getWidth() - skin_.px (60.0f), b.getHeight() - skin_.px (40.0f));
        auto xOf = [&] (double f) { return plot.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        auto yOf = [&] (double db) { return plot.getY() + static_cast<float> ((12.0 - db) / 48.0) * plot.getHeight(); };
        g.setFont (skin_.font (9.0f));
        for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawVerticalLine (static_cast<int> (xOf (f)), plot.getY(), plot.getBottom());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0), juce::Rectangle<float> (xOf (f) - skin_.px (16.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (32.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        for (const double d : { 12.0, 0.0, -12.0, -24.0, -36.0 })
        {
            g.setColour (skin_.colour (Token::Grid, d == 0.0 ? 1.0f : 0.6f));
            g.drawHorizontalLine (static_cast<int> (yOf (d)), plot.getX(), plot.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (static_cast<int> (d)), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), skin_.px (38.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        const bool isTal = tal();
        const double mix = host_.parameters().get (stock::mix) * 0.01, fb = isTal ? 0.0 : get (ch::feedback) * 0.01, wetGain = isTal ? 1.4 : 1.0;
        auto magDb = [&] (double f, double tauMs)
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * kPi * f * tauMs * 0.001);
            const std::complex<double> wet = wetGain * z / (1.0 - fb * z);
            const std::complex<double> h = isTal ? (1.0 - mix) * 1.0 + mix * (1.0 + wet) : (1.0 - mix) + mix * wet;
            return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-4));
        };
        // retard extrêmes sur un cycle de LFO
        double tMin = 1.0e9, tMax = 0.0;
        const int nv = voiceCount();
        for (int i = 0; i < 48; ++i)
        {
            const double ph = i / 48.0;
            const double t = isTal ? talDelayMs (ph, 0, 0) : standardDelayMs (ph, 0, 0, nv);
            tMin = std::min (tMin, t);
            tMax = std::max (tMax, t);
        }
        juce::Path band;
        std::vector<float> hi, lo;
        const int points = 200;
        for (int i = 0; i <= points; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / static_cast<double> (points));
            double dmin = 1.0e9, dmax = -1.0e9;
            for (int k = 0; k <= 24; ++k)
            {
                const double d = magDb (f, tMin + (tMax - tMin) * k / 24.0);
                dmin = std::min (dmin, d);
                dmax = std::max (dmax, d);
            }
            hi.push_back (yOf (std::clamp (dmax, -36.0, 12.0)));
            lo.push_back (yOf (std::clamp (dmin, -36.0, 12.0)));
        }
        for (int i = 0; i <= points; ++i)
        {
            const float x = xOf (20.0 * std::pow (1000.0, i / static_cast<double> (points)));
            if (i == 0) band.startNewSubPath (x, hi[0]); else band.lineTo (x, hi[static_cast<std::size_t> (i)]);
        }
        for (int i = points; i >= 0; --i) band.lineTo (xOf (20.0 * std::pow (1000.0, i / static_cast<double> (points))), lo[static_cast<std::size_t> (i)]);
        band.closeSubPath();
        g.setColour (skin_.colour (Token::Accent, 0.22f));
        g.fillPath (band);
        // trait du retard instantané
        const double phase = module_ != nullptr ? static_cast<double> (module_->meter (ch::kLfoPhase)) : 0.0;
        const double tNow = isTal ? talDelayMs (phase, 0, 0) : standardDelayMs (phase, 0, 0, nv);
        juce::Path now;
        for (int i = 0; i <= points; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / static_cast<double> (points));
            const float x = xOf (f), y = yOf (std::clamp (magDb (f, tNow), -36.0, 12.0));
            if (i == 0) now.startNewSubPath (x, y); else now.lineTo (x, y);
        }
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (now, juce::PathStrokeType (skin_.px (2.2f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    const ChorusModule* module_;
};

// ------------------------------------------------------------------------------------------------ réponse de la voie traitée (page TONE)
class NovaChorusEditor::ToneView : public juce::Component
{
public:
    ToneView (const nova::Skin& s, ParamHost& h, std::function<double()> rate) : skin_ (s), host_ (h), rate_ (std::move (rate)) {}

    void update()
    {
        std::uint32_t key = 0;
        for (const auto id : { ch::lowCut, ch::lowCutSlope, ch::lowCutOn, ch::highCut, ch::highCutSlope, ch::highCutOn, ch::bbd, ch::bbdOn }) key = key * 31u + host_.parameters().version (id);
        if (key == key_) return;
        key_ = key;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
        plot_ = juce::Rectangle<float> (b.getX() + skin_.px (44.0f), b.getY() + skin_.px (14.0f), b.getWidth() - skin_.px (60.0f), b.getHeight() - skin_.px (40.0f));
        g.setFont (skin_.font (9.0f));
        for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.drawVerticalLine (static_cast<int> (xOf (f)), plot_.getY(), plot_.getBottom());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0), juce::Rectangle<float> (xOf (f) - skin_.px (16.0f), plot_.getBottom() + skin_.px (4.0f), skin_.px (32.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        for (const double d : { 6.0, 0.0, -12.0, -24.0, -36.0 })
        {
            g.setColour (skin_.colour (Token::Grid, d == 0.0 ? 1.0f : 0.6f));
            g.drawHorizontalLine (static_cast<int> (yOf (d)), plot_.getX(), plot_.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (static_cast<int> (d)), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), skin_.px (38.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        const double fs = rate_ ? rate_() : 48000.0;
        const bool lowOn = host_.parameters().get (ch::lowCutOn) >= 0.5, highOn = host_.parameters().get (ch::highCutOn) >= 0.5, bbdOn = host_.parameters().get (ch::bbdOn) >= 0.5;
        const double lowF = host_.parameters().get (ch::lowCut), highF = host_.parameters().get (ch::highCut), bbdAmount = host_.parameters().get (ch::bbd) * 0.01;
        ButterworthCascade hp, lp;
        hp.design (true, fs, lowF, 6 << std::clamp (static_cast<int> (std::lround (host_.parameters().get (ch::lowCutSlope))), 0, 2));
        lp.design (false, fs, std::min (highF, 0.45 * fs), 6 << std::clamp (static_cast<int> (std::lround (host_.parameters().get (ch::highCutSlope))), 0, 2));
        const double bbdF = std::min (20000.0 * std::pow (0.3, bbdAmount), 0.45 * fs);
        const auto bbd = BiquadCoefs::lowpass (fs, bbdF, 0.70710678);
        juce::Path p;
        for (int i = 0; i <= 300; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 300.0);
            double m = 1.0;
            if (lowOn) m *= hp.magnitude (fs, f);
            if (highOn) m *= lp.magnitude (fs, f);
            if (bbdOn && bbdAmount > 0.0) m *= bbd.magnitude (fs, f);
            const float x = xOf (f), y = yOf (std::clamp (20.0 * std::log10 (std::max (m, 1.0e-4)), -36.0, 6.0));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (skin_.px (2.4f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int h = 0; h < 2; ++h)
        {
            const bool on = h == 0 ? lowOn : highOn;
            const auto pt = handle (h);
            const float r = skin_.px (h == active_ ? 7.0f : 6.0f);
            g.setColour (skin_.colour (h == 0 ? Token::Band1 : Token::Band2, on ? 1.0f : 0.35f));
            g.fillEllipse (pt.x - r, pt.y - r, r * 2, r * 2);
            g.setColour (skin_.colour (Token::BgApp));
            g.drawEllipse (pt.x - r, pt.y - r, r * 2, r * 2, skin_.px (1.2f));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (hit (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        active_ = e.mods.isPopupMenu() ? -1 : hit (e.position);
        if (active_ < 0) return;
        host_.uiBeginEdit (freqId (active_));
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (active_ < 0) return;
        const double f = 20.0 * std::pow (1000.0, (e.position.x - plot_.getX()) / plot_.getWidth());
        setPlain (host_, freqId (active_), active_ == 0 ? std::clamp (f, 20.0, 1000.0) : std::clamp (f, 1000.0, 20000.0));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (active_ >= 0) host_.uiEndEdit (freqId (active_));
        active_ = -1;
        repaint();
    }

private:
    static ParamId freqId (int h) { return h == 0 ? ch::lowCut : ch::highCut; }
    float xOf (double f) const { return plot_.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * plot_.getWidth(); }
    float yOf (double db) const { return plot_.getY() + static_cast<float> ((6.0 - db) / 42.0) * plot_.getHeight(); }
    juce::Point<float> handle (int h) const { return { xOf (host_.parameters().get (freqId (h))), yOf (0.0) }; }
    int hit (juce::Point<float> p) const
    {
        for (int h = 0; h < 2; ++h) if (handle (h).getDistanceFrom (p) < skin_.px (14.0f)) return h;
        return -1;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    std::function<double()> rate_;
    juce::Rectangle<float> plot_;
    std::uint32_t key_ = 0xFFFFFFFFu;
    int active_ = -1;
};

// ------------------------------------------------------------------------------------------------ zone de la page TONE
struct NovaChorusEditor::Zone
{
    ParamId onId = 0;
    nova::Panel* card = nullptr;
    nova::Switch* sw = nullptr;
    nova::KnobCard* knob = nullptr;
    nova::Pill* slope = nullptr;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaChorusEditor::NovaChorusEditor (ParamHost& host, const ChorusModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                    const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.chorus")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("ch.page_chorus"), iconPath (0));
    addPage (tx ("ch.page_tone"), iconPath (1));
    addPage (tx ("nv.page_settings"), iconPath (2));
    buildChorusPage();
    buildTonePage();
    buildSettingsPage (2);
    buildSettingsExtras();
    if (const auto names = presetNames(); ! names.empty()) presetPill()->setText (names[0]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaChorusEditor::~NovaChorusEditor() = default;

std::vector<juce::String> NovaChorusEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : ch::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}

std::vector<std::pair<ParamId, double>> NovaChorusEditor::presetValues (int i) const { return ch::factoryPresets()[static_cast<std::size_t> (i)].values; }

std::vector<ParamId> NovaChorusEditor::presetParamIds() const { return ch::presetParams(); }

void NovaChorusEditor::buildChorusPage()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    viewPanel_ = add<nova::Panel> (0, skin());
    viewPanel_->setTitle (tx ("ch.mod_title"));
    view_ = add<ModView> (0, skin(), host(), module_);
    viewPill_ = add<nova::Pill> (0, skin());
    viewPill_->onClick = [this] { showResponse_ = ! showResponse_; };
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, outMeter_ }) m->setModeVisible (false);
    inTrimPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    inTrimPill_->bindValue (host(), stock::inputTrim, {});
    inTrimPill_->onClick = [this] { openModal (stock::inputTrim, uistate::Terminal::Pointer); };

    // mini-contrôles sous le graphique
    modePill_ = add<nova::Pill> (0, skin());
    modePill_->bindChoice (host(), ch::mode, tx ("ch.mode"), { tx ("ch.mode_std"), tx ("ch.mode_tal") });
    syncPill_ = add<nova::Pill> (0, skin());
    syncPill_->bindToggle (host(), ch::sync, tx ("ch.sync"));
    divPill_ = add<nova::Pill> (0, skin());
    divPill_->bindChoice (host(), ch::syncDiv, tx ("ch.division"), std::vector<juce::String> (ch::divisionNames, ch::divisionNames + ch::kDivisions));
    shapePill_ = add<nova::Pill> (0, skin());
    shapePill_->bindChoice (host(), ch::lfoShape, tx ("ch.shape"), { tx ("ch.tri"), tx ("ch.sin") });
    voicesPill_ = add<nova::Pill> (0, skin());
    voicesPill_->bindChoice (host(), ch::voices, tx ("ch.voices"), { "1", "2", "3", "4" });
    unitIPill_ = add<nova::Pill> (0, skin());
    unitIPill_->bindToggle (host(), ch::talI, tx ("ch.unit_i"));
    unitIIPill_ = add<nova::Pill> (0, skin());
    unitIIPill_->bindToggle (host(), ch::talII, tx ("ch.unit_ii"));
    addInfo (0, modePill_, "hlp.ch_mode");
    addInfo (0, syncPill_, "hlp.ch_sync");
    addInfo (0, divPill_, "hlp.ch_division");
    addInfo (0, shapePill_, "hlp.ch_shape");
    addInfo (0, voicesPill_, "hlp.ch_voices");
    addInfo (0, unitIPill_, "hlp.ch_unit");

    struct Spec { ParamId id; const char* title; bool pink; const char* help; };
    const Spec specs[7] = { { ch::rate, "ch.rate", false, "hlp.ch_rate" }, { ch::depth, "ch.depth", false, "hlp.ch_depth" }, { ch::delayMs, "ch.delay", false, "hlp.ch_delay" },
                            { ch::width, "ch.width", false, "hlp.ch_width" }, { ch::feedback, "ch.feedback", false, "hlp.ch_feedback" },
                            { stock::mix, "ch.mix", true, "hlp.ch_mix" }, { stock::outputTrim, "ch.output", true, "hlp.ch_output" } };
    for (const auto& s : specs)
    {
        Column c {};
        c.card = add<nova::Panel> (0, skin());
        c.knob = add<nova::KnobCard> (0, skin(), host(), s.id, tx (s.title), modalOpener(), s.pink);
        c.knob->setPlain (true);
        addInfo (0, c.card, s.help, 1);
        columns_.push_back (c);
    }
}

void NovaChorusEditor::buildTonePage()
{
    tonePanel_ = add<nova::Panel> (1, skin());
    tonePanel_->setTitle (tx ("ch.tone_title"));
    toneView_ = add<ToneView> (1, skin(), host(), [this] { const double r = options().sampleRate ? options().sampleRate() : 48000.0; return r > 0.0 ? r : 48000.0; });
    struct ZSpec { ParamId on, value; const char* title; const char* help; ParamId slope; };
    const ZSpec zs[3] = { { ch::lowCutOn, ch::lowCut, "ch.zone_lowcut", "hlp.ch_lowcut", ch::lowCutSlope }, { ch::highCutOn, ch::highCut, "ch.zone_highcut", "hlp.ch_highcut", ch::highCutSlope },
                          { ch::bbdOn, ch::bbd, "ch.zone_bbd", "hlp.ch_bbd", 0 } };
    for (const auto& z : zs)
    {
        auto zone = std::make_unique<Zone>();
        zone->onId = z.on;
        zone->card = add<nova::Panel> (1, skin());
        zone->card->setTitle (tx (z.title));
        zone->sw = add<nova::Switch> (1, skin());
        const ParamId on = z.on;
        zone->sw->onChange = [this, on] (bool v) { nova::setParam (host(), on, v ? 1.0 : 0.0); };
        zone->knob = add<nova::KnobCard> (1, skin(), host(), z.value, tx (z.title), modalOpener(), false);
        zone->knob->setPlain (true);
        if (z.slope != 0)
        {
            zone->slope = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
            zone->slope->bindChoice (host(), z.slope, tx ("nv.slope"), { tx ("nv.slope_6"), tx ("nv.slope_12"), tx ("nv.slope_24") });
        }
        addInfo (1, zone->card, z.help, 1);
        zones_.push_back (std::move (zone));
    }
}

void NovaChorusEditor::buildSettingsExtras()
{
    modSection_ = add<nova::Text> (2, skin(), Token::TextDim2, 11.0f, true);
    modSection_->setText (tx ("ch.set_section"));
    interpPill_ = add<nova::Pill> (2, skin());
    interpPill_->bindChoice (host(), ch::interp, tx ("ch.interp"), { tx ("ch.linear"), tx ("ch.cubic") });
    lfoStartPill_ = add<nova::Pill> (2, skin());
    lfoStartPill_->bindChoice (host(), ch::lfoStart, tx ("ch.lfo_start"), { tx ("ch.free"), tx ("ch.each_play") });
    addInfo (2, interpPill_, "hlp.ch_interp");
    addInfo (2, lfoStartPill_, "hlp.ch_lfostart");
}

void NovaChorusEditor::rebindForMode()
{
    const bool talMode = value (ch::mode) >= 0.5;
    columns_[0].knob->setParam (talMode ? ch::talRate1 : ch::rate, tx (talMode ? "ch.rate_i" : "ch.rate"));
    columns_[1].knob->setParam (talMode ? ch::talRate2 : ch::depth, tx (talMode ? "ch.rate_ii" : "ch.depth"));
    boundMode_ = talMode ? 1 : 0;
}

void NovaChorusEditor::layout()
{
    // ---- page CHORUS
    metersPanel_->setBounds (R (14, 72, 176, 410));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 2, trimH = skin().ipx (40.0f);
        const auto meters = in.withTrimmedBottom (trimH);
        inMeter_->setBounds (meters.withWidth (w));
        outMeter_->setBounds (meters.withX (meters.getX() + w).withWidth (w));
        inTrimPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), w - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 496, 176, 214));
    layoutNav (496, 214);
    viewPanel_->setBounds (R (204, 72, 942, 410));
    viewPanel_->setMetaRightInset (130.0f);
    view_->setBounds (R (220, 104, 910, 300));
    viewPill_->setBounds (R (1146 - 14 - 112, 76, 112, 24));
    modePill_->setBounds (R (220, 424, 150, 32));
    syncPill_->setBounds (R (400, 424, 100, 32));
    divPill_->setBounds (R (530, 424, 150, 32));
    shapePill_->setBounds (R (710, 424, 150, 32));
    voicesPill_->setBounds (R (890, 424, 140, 32));
    unitIPill_->setBounds (R (400, 424, 140, 32));
    unitIIPill_->setBounds (R (575, 424, 140, 32));
    const float colW = (942.0f - 12.0f * 6.0f) / 7.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const float x = 204.0f + static_cast<float> (i) * (colW + 12.0f);
        columns_[i].card->setBounds (R (x, 496, colW, 214));
        columns_[i].knob->setBounds (R (x, 496, colW, 132));
    }

    // ---- page TONE
    tonePanel_->setBounds (R (204, 72, 942, 410));
    toneView_->setBounds (R (220, 104, 910, 362));
    const float zw = (942.0f - 12.0f * 2.0f) / 3.0f;
    for (std::size_t i = 0; i < zones_.size(); ++i)
    {
        auto& z = *zones_[i];
        const float x = 204.0f + static_cast<float> (i) * (zw + 12.0f);
        z.card->setBounds (R (x, 496, zw, 214));
        z.sw->setBounds (R (x + zw - 8 - 34, 496 + 10, 34, 19));
        z.knob->setBounds (R (x + 20, 496 + 36, zw - 40, 112));
        if (z.slope != nullptr) z.slope->setBounds (R (x + 40, 496 + 160, zw - 80, 28));
    }

    // ---- page SETTINGS (en plus des sections communes)
    modSection_->setBounds (R (220, 540, 400, 16));
    interpPill_->setBounds (R (220, 568, 300, 34));
    lfoStartPill_->setBounds (R (540, 568, 300, 34));

    // ---- barre du bas
    abPill()->setBounds (R (14, 724 + 12, 40, 32));

    // ---- en-tête
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaChorusEditor::tick()
{
    const int mode = value (ch::mode) >= 0.5 ? 1 : 0;
    if (mode != boundMode_) rebindForMode();
    if (module_ != nullptr)
    {
        inMeter_->update (dbOf (module_->meter (ch::kInPeakL)), dbOf (module_->meter (ch::kInPeakR)));
        outMeter_->update (dbOf (module_->meter (ch::kOutPeakL)), dbOf (module_->meter (ch::kOutPeakR)));
    }
    const bool talMode = mode == 1;
    if (currentPage() == 0)
    {
        view_->response = showResponse_;
        viewPill_->setText (showResponse_ ? tx ("ch.response") : tx ("ch.delay_view"));
        view_->update();
        const bool syncOn = value (ch::sync) >= 0.5;
        for (auto* p : { syncPill_, divPill_, shapePill_, voicesPill_ }) p->setVisible (! talMode);
        for (auto* p : { unitIPill_, unitIIPill_ }) p->setVisible (talMode);
        setInfoVisible (syncPill_, ! talMode);
        setInfoVisible (divPill_, ! talMode);
        setInfoVisible (shapePill_, ! talMode);
        setInfoVisible (voicesPill_, ! talMode);
        setInfoVisible (unitIPill_, talMode);
        syncPill_->setEnabled (options().hostTempo);
        syncPill_->setAlpha (options().hostTempo ? 1.0f : 0.4f);
        divPill_->setAlpha (syncOn ? 1.0f : 0.4f);
        // mode TAL : seules les deux vitesses, le mélange et la sortie agissent
        for (std::size_t i = 2; i < 5; ++i)
        {
            columns_[i].knob->setAlpha (talMode ? 0.3f : 1.0f);
            columns_[i].knob->setEnabled (! talMode);
        }
        columns_[0].knob->setAlpha (! talMode && syncOn ? 0.4f : 1.0f);
        modePill_->setTintToken (talMode ? Token::Pink : Token::Accent);
    }
    if (currentPage() == 1)
    {
        toneView_->update();
        for (auto& z : zones_)
        {
            const bool on = value (z->onId) >= 0.5;
            z->sw->setOn (on);
            z->knob->setAlpha (on ? 1.0f : 0.4f);
            if (z->slope != nullptr) z->slope->setAlpha (on ? 1.0f : 0.4f);
        }
    }
    const double bpm = module_ != nullptr && module_->tempo() > 1.0 ? module_->tempo() : 120.0;
    const auto tempo = (! talMode && value (ch::sync) >= 0.5) ? juce::String (bpm, 1) + " BPM" : juce::String();
    if (tempo != tempoText_) { tempoText_ = tempo; repaint (R (700, 724, 446, 56)); }
}

void NovaChorusEditor::paintBackground (juce::Graphics& g)
{
    g.setColour (skin().colour (Token::TextDim2));
    g.setFont (skin().font (11.0f, true));
    juce::String text = juce::String::fromUTF8 (translator().format ("nv.latency", { "0" }).c_str());
    if (tempoText_.isNotEmpty()) text = tx ("ch.tempo") + " " + tempoText_ + "     " + text;
    g.drawText (text, R (730, 724 + 12, 416, 32).toFloat(), juce::Justification::centredRight);
}
