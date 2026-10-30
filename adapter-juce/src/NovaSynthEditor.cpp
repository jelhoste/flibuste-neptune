#include "NovaSynthEditor.h"
#include "ModView.h"

#include <algorithm>
#include <cmath>

#include "WaveExplorer.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mx = neptune::core::mixer;

namespace
{
juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // oscillateurs : une onde
    {
        p.startNewSubPath (2.0f, 12.0f);
        p.cubicTo (6.0f, 2.0f, 9.0f, 2.0f, 12.0f, 12.0f);
        p.cubicTo (15.0f, 22.0f, 18.0f, 22.0f, 22.0f, 12.0f);
    }
    else if (kind == 1)   // filtres : courbe de coupure
    {
        p.startNewSubPath (2.0f, 8.0f);
        p.lineTo (12.0f, 8.0f);
        p.cubicTo (16.0f, 8.0f, 16.0f, 4.0f, 18.0f, 14.0f);
        p.lineTo (22.0f, 20.0f);
    }
    else if (kind == 2)   // enveloppes
    {
        p.startNewSubPath (2.0f, 20.0f);
        p.lineTo (7.0f, 4.0f);
        p.lineTo (12.0f, 11.0f);
        p.lineTo (18.0f, 11.0f);
        p.lineTo (22.0f, 20.0f);
    }
    else if (kind == 5)   // modulation : deux ondes lentes
    {
        p.startNewSubPath (2.0f, 8.0f);
        p.cubicTo (5.0f, 1.0f, 9.0f, 1.0f, 12.0f, 8.0f);
        p.cubicTo (15.0f, 15.0f, 19.0f, 15.0f, 22.0f, 8.0f);
        p.startNewSubPath (2.0f, 18.0f);
        p.lineTo (8.0f, 18.0f);
        p.lineTo (11.0f, 13.0f);
        p.lineTo (22.0f, 13.0f);
    }
    else if (kind == 3)   // matrice
    {
        for (const float x : { 4.0f, 14.0f }) for (const float y : { 4.0f, 14.0f }) p.addRectangle (x, y, 6.0f, 6.0f);
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
} // namespace

// ------------------------------------------------------------------------------------------------ forme d'onde (un clic ouvre l'explorateur)
class NovaSynthEditor::WaveTile : public juce::Component
{
public:
    WaveTile (const nova::Skin& s, const i18n::Translator& tr, std::function<int()> get, std::function<void (int)> pick)
        : skin_ (s), tr_ (tr), get_ (std::move (get)), pick_ (std::move (pick)) { preview_.resize (96); }

    void update()
    {
        const int w = juce::jlimit (0, kNumWaves - 1, get_());
        if (w == wave_ && haveData_) return;
        wave_ = w;
        haveData_ = true;
        renderWaveCycle (wave_, preview_.data(), static_cast<int> (preview_.size()), 0.25);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        skin_.fill (g, hover_ ? Token::Nav1 : Token::StageBg, r, skin_.px (8.0f));
        g.setColour (skin_.colour (Token::Grid));
        g.drawRoundedRectangle (r, skin_.px (8.0f), skin_.px (1.0f));
        const auto plot = r.reduced (skin_.px (10.0f), skin_.px (6.0f)).withTrimmedBottom (skin_.px (16.0f));
        juce::Path p;
        for (std::size_t i = 0; i < preview_.size(); ++i)
        {
            const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (preview_.size() - 1);
            const float y = plot.getCentreY() - preview_[i] * plot.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (skin_.px (1.8f)));
        g.setColour (skin_.colour (Token::Text));
        g.setFont (skin_.font (11.0f, true));
        g.drawText (juce::String::fromUTF8 (std::string (tr_.tr (waveInfo (wave_).nameKey)).c_str()), r.withTrimmedTop (r.getHeight() - skin_.px (18.0f)), juce::Justification::centred, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover_ = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() > 6) return;
        auto explorer = std::make_unique<WaveExplorer> (skin_.palette(), tr_, wave_, pick_);
        juce::CallOutBox::launchAsynchronously (std::move (explorer), getScreenBounds(), nullptr);
    }

private:
    const nova::Skin& skin_;
    const i18n::Translator& tr_;
    std::function<int()> get_;
    std::function<void (int)> pick_;
    std::vector<float> preview_;
    int wave_ = kWaveSaw;
    bool haveData_ = false, hover_ = false;
};

// ------------------------------------------------------------------------------------------------ fabriques typées sur la description
void NovaSynthEditor::edit (const std::function<void (Desc&)>& change)
{
    auto d = desc_;
    change (d);
    if (d == desc_) return;
    desc_ = d;
    apply_ (d);
}

void NovaSynthEditor::commit (const std::function<void (Desc&)>& change)
{
    gestureBegin();
    edit (change);
    gestureEnd();
}

nova::ValueSlider* NovaSynthEditor::slider (int page, juce::String title, double lo, double hi, double step, juce::String suffix, std::function<double (const Desc&)> get,
                                            std::function<void (Desc&, double)> set, double def, std::function<bool (const Desc&)> enabled)
{
    return sliderFn (page, std::move (title), lo, hi, step, std::move (suffix), [this, get] { return get (desc_); },
                     [this, set] (double v) { edit ([&] (Desc& d) { set (d, v); }); }, def, enabled ? std::function<bool()> ([this, enabled] { return enabled (desc_); }) : std::function<bool()> {});
}

nova::NumberPill* NovaSynthEditor::number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                                           std::function<bool (const Desc&)> enabled)
{
    return numberFn (page, std::move (label), lo, hi, [this, get] { return get (desc_); }, [this, set] (int v) { edit ([&] (Desc& d) { set (d, v); }); },
                     enabled ? std::function<bool()> ([this, enabled] { return enabled (desc_); }) : std::function<bool()> {});
}

nova::ChoicePill* NovaSynthEditor::choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                                           std::function<bool (const Desc&)> enabled)
{
    return choiceFn (page, std::move (label), std::move (items), [this, get] { return get (desc_); }, [this, set] (int v) { edit ([&] (Desc& d) { set (d, v); }); },
                     enabled ? std::function<bool()> ([this, enabled] { return enabled (desc_); }) : std::function<bool()> {});
}

nova::Pill* NovaSynthEditor::toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set, std::function<bool (const Desc&)> enabled)
{
    return toggleFn (page, std::move (text), [this, get] { return get (desc_); }, [this, set] (bool b) { edit ([&] (Desc& d) { set (d, b); }); },
                     enabled ? std::function<bool()> ([this, enabled] { return enabled (desc_); }) : std::function<bool()> {});
}

EnvelopeEditor* NovaSynthEditor::envelope (int page, std::function<EnvelopeDescription (const Desc&)> get, std::function<void (Desc&, const EnvelopeDescription&)> set,
                                           std::function<bool (const Desc&)> enabled)
{
    auto* e = add<EnvelopeEditor> (page, skin().palette(), translator());
    e->onGestureBegin = [this] { gestureBegin(); };
    e->onGestureEnd = [this] { gestureEnd(); };
    e->onChange = [this, set] (const EnvelopeDescription& env) { edit ([&] (Desc& d) { set (d, env); }); };
    auto shown = std::make_shared<std::pair<bool, EnvelopeDescription>> (false, EnvelopeDescription {});
    onUpdate (page, [this, e, get, enabled, shown]
    {
        if (dragging()) return;
        const auto env = get (desc_);
        if (! shown->first || ! (env == shown->second)) { shown->first = true; shown->second = env; e->setEnvelope (env); }
        if (enabled) e->setEnabled (enabled (desc_));
    });
    return e;
}

// ------------------------------------------------------------------------------------------------ construction
NovaSynthEditor::NovaSynthEditor (ParamHost& host, PresetHost& presets, SynthLink link, const theme::Palette& palette, theme::KnobStyle knobStyle, const i18n::Translator& tr, Options options)
    : NovaInstrumentBase (host, presets, link, palette, knobStyle, tr, std::move (options), juce::String::fromUTF8 ("ANALOGSYNTH")), describe_ (link.description), apply_ (link.edit)
{
    desc_ = describe_();
    modulation_ = link.modulation;
    addPage (tx ("synth.page_osc"), iconPath (0));
    addPage (tx ("synth.page_filter"), iconPath (1));
    addPage (tx ("synth.page_env"), iconPath (2));
    if (modulation_ != nullptr) addPage (tx ("synth.page_mod"), iconPath (5));   // plugin seul
    buildCommon ("hlp.sy_out");   // MATRICE, SETTINGS, colonne de gauche, clavier
    buildOscPage();
    buildFilterPage();
    buildEnvPage();
    if (modulation_ != nullptr) buildModPage();
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaSynthEditor::~NovaSynthEditor() = default;

void NovaSynthEditor::buildModPage()
{
    // l'onglet MOD de Neptune, tel quel, sur la matrice du plugin (page 3 : après OSC, FILTRES, ENVELOPPES)
    modView_ = add<ModView> (3, *modulation_, skin().palette(), translator());
}

void NovaSynthEditor::buildOscPage()
{
    for (int o = 0; o < numOscillators; ++o)
    {
        auto& u = osc_[o];
        const auto idx = static_cast<std::size_t> (o);
        u.panel = add<nova::Panel> (0, skin());
        u.panel->setTitle (juce::String::fromUTF8 (translator().format ("synth.osc", { std::to_string (o + 1) }).c_str()));
        addInfo (0, u.panel, "hlp.sy_osc", 1);
        u.wave = add<WaveTile> (0, skin(), translator(), [this, idx] { return desc_.osc[idx].wave; },
                                [this, idx] (int w) { commit ([&] (Desc& d) { d.osc[idx].wave = w; }); });
        onUpdate (0, [w = u.wave] { w->update(); });
        u.octave = number (0, tx ("synth.octave").toUpperCase(), SynthDescription::minOctave, SynthDescription::maxOctave, [idx] (const Desc& d) { return d.osc[idx].octave; },
                           [idx] (Desc& d, int v) { d.osc[idx].octave = v; }, {});
        u.semitone = number (0, tx ("synth.semitone").toUpperCase(), -12, 12, [idx] (const Desc& d) { return d.osc[idx].semitone; },
                             [idx] (Desc& d, int v) { d.osc[idx].semitone = v; }, {});
        const auto level = static_cast<mx::SynthKind> (mx::kLevelOsc1 + o), fine = static_cast<mx::SynthKind> (mx::kFineOsc1 + o);
        const auto pulse = static_cast<mx::SynthKind> (mx::kPulseOsc1 + o), detune = static_cast<mx::SynthKind> (mx::kDetuneOsc1 + o);
        u.knobs.push_back (knob (0, mx::synthParam (link_.track, level), tx ("synth.level"), true));
        u.knobs.push_back (knob (0, mx::synthParam (link_.track, fine), tx ("synth.fine"), true));
        u.knobs.push_back (knob (0, mx::synthParam (link_.track, pulse), tx ("synth.pulse"), true));
        u.knobs.push_back (knob (0, mx::synthParam (link_.track, detune), tx ("synth.detune"), true));
        onUpdate (0, [this, idx, o]
        {
            const bool pulseOn = desc_.osc[idx].wave == kWavePulse, uni = desc_.osc[idx].unison > 1;
            osc_[o].knobs[2]->setAlpha (pulseOn ? 1.0f : 0.35f);
            osc_[o].knobs[2]->setInterceptsMouseClicks (pulseOn, pulseOn);
            osc_[o].knobs[3]->setAlpha (uni ? 1.0f : 0.35f);
            osc_[o].knobs[3]->setInterceptsMouseClicks (uni, uni);
        });
        u.unison = number (0, tx ("synth.unison").toUpperCase(), 1, maxUnison, [idx] (const Desc& d) { return d.osc[idx].unison; }, [idx] (Desc& d, int v) { d.osc[idx].unison = v; }, {});
        u.curve = choice (0, tx ("synth.curve").toUpperCase(), { tx ("synth.curve_linear"), tx ("synth.curve_supersaw") },
                          [idx] (const Desc& d) { return d.osc[idx].curve == DetuneCurve::Supersaw ? 1 : 0; },
                          [idx] (Desc& d, int v) { d.osc[idx].curve = v == 1 ? DetuneCurve::Supersaw : DetuneCurve::Linear; },
                          [idx] (const Desc& d) { return d.osc[idx].unison > 1; });
        u.spread = slider (0, tx ("synth.spread").toUpperCase(), 0.0, 100.0, 1.0, " %", [idx] (const Desc& d) { return d.osc[idx].spread * 100.0; },
                           [idx] (Desc& d, double v) { d.osc[idx].spread = v / 100.0; }, 50.0, [idx] (const Desc& d) { return d.osc[idx].unison > 1; });
        addInfo (0, u.spread, "hlp.sy_spread", 1);
        u.phase = slider (0, tx ("synth.phase").toUpperCase(), 0.0, 360.0, 1.0, " deg", [idx] (const Desc& d) { return d.osc[idx].startPhase * 360.0; },
                          [idx] (Desc& d, double v) { d.osc[idx].startPhase = v / 360.0; }, 0.0, [idx] (const Desc& d) { return d.osc[idx].retrigger; });
        addInfo (0, u.phase, "hlp.sy_phase", 1);
        u.pan = slider (0, tx ("synth.mix_pan").toUpperCase(), -100.0, 100.0, 1.0, "", [o] (const Desc& d) { return d.mix[static_cast<std::size_t> (o)].pan * 100.0; },
                        [o] (Desc& d, double v) { d.mix[static_cast<std::size_t> (o)].pan = v / 100.0; }, 0.0);
        u.retrigger = toggle (0, tx ("synth.retrigger"), [idx] (const Desc& d) { return d.osc[idx].retrigger; }, [idx] (Desc& d, bool b) { d.osc[idx].retrigger = b; });
        if (o > 0)
        {
            u.sync = toggle (0, tx ("synth.sync"), [idx] (const Desc& d) { return d.osc[idx].sync; }, [idx] (Desc& d, bool b) { d.osc[idx].sync = b; });
        }
        u.mute = toggle (0, tx ("synth.mute"), [o] (const Desc& d) { return d.mix[static_cast<std::size_t> (o)].mute; }, [o] (Desc& d, bool b) { d.mix[static_cast<std::size_t> (o)].mute = b; });
    }

    // sous-oscillateur et bruit
    sub_.panel = add<nova::Panel> (0, skin());
    sub_.panel->setTitle (tx ("synth.sub"));
    addInfo (0, sub_.panel, "hlp.sy_sub", 1);
    sub_.level = knob (0, mx::synthParam (link_.track, mx::kLevelSub), tx ("synth.level"), true);
    sub_.wave = choice (0, {}, { tx ("synth.sub_sine"), tx ("synth.sub_triangle"), tx ("synth.sub_square") },
                        [] (const Desc& d) { return d.subWave == SubWave::Triangle ? 1 : d.subWave == SubWave::Square ? 2 : 0; },
                        [] (Desc& d, int v) { d.subWave = v == 1 ? SubWave::Triangle : v == 2 ? SubWave::Square : SubWave::Sine; });
    sub_.octave = choice (0, tx ("synth.sub_octave").toUpperCase(), { "-1", "-2" }, [] (const Desc& d) { return d.subOctave <= -2 ? 1 : 0; },
                          [] (Desc& d, int v) { d.subOctave = v == 1 ? -2 : -1; });
    sub_.pan = slider (0, tx ("synth.mix_pan").toUpperCase(), -100.0, 100.0, 1.0, "", [] (const Desc& d) { return d.mix[kSourceSub].pan * 100.0; },
                       [] (Desc& d, double v) { d.mix[kSourceSub].pan = v / 100.0; }, 0.0);
    sub_.mute = toggle (0, tx ("synth.mute"), [] (const Desc& d) { return d.mix[kSourceSub].mute; }, [] (Desc& d, bool b) { d.mix[kSourceSub].mute = b; });
    noise_.panel = add<nova::Panel> (0, skin());
    noise_.panel->setTitle (tx ("synth.noise"));
    addInfo (0, noise_.panel, "hlp.sy_noise", 1);
    noise_.level = knob (0, mx::synthParam (link_.track, mx::kLevelNoise), tx ("synth.level"), true);
    noise_.wave = choice (0, {}, { tx ("synth.noise_white"), tx ("synth.noise_pink") }, [] (const Desc& d) { return d.noise == NoiseType::Pink ? 1 : 0; },
                          [] (Desc& d, int v) { d.noise = v == 1 ? NoiseType::Pink : NoiseType::White; });
    noise_.pan = slider (0, tx ("synth.mix_pan").toUpperCase(), -100.0, 100.0, 1.0, "", [] (const Desc& d) { return d.mix[kSourceNoise].pan * 100.0; },
                         [] (Desc& d, double v) { d.mix[kSourceNoise].pan = v / 100.0; }, 0.0);
    noise_.mute = toggle (0, tx ("synth.mute"), [] (const Desc& d) { return d.mix[kSourceNoise].mute; }, [] (Desc& d, bool b) { d.mix[kSourceNoise].mute = b; });
}

void NovaSynthEditor::buildFilterPage()
{
    routePanel_ = add<nova::Panel> (1, skin());
    routePanel_->setTitle (tx ("filt.routing").toUpperCase());
    addInfo (1, routePanel_, "hlp.sy_route", 1);
    routing_ = choice (1, {}, { tx ("filt.series"), tx ("filt.parallel") }, [] (const Desc& d) { return d.filters.parallel ? 1 : 0; },
                       [] (Desc& d, int v) { d.filters.parallel = v == 1; });
    balance_ = slider (1, tx ("filt.balance").toUpperCase(), 0.0, 100.0, 1.0, " %", [] (const Desc& d) { return d.filters.balance * 100.0; },
                       [] (Desc& d, double v) { d.filters.balance = v / 100.0; }, 50.0, [] (const Desc& d) { return d.filters.parallel; });
    addInfo (1, balance_, "hlp.sy_balance", 1);
    for (int f = 0; f < 2; ++f)
    {
        auto& u = filter_[f];
        const auto fi = static_cast<std::size_t> (f);
        auto on = [fi] (const Desc& d) { return d.filters.filter[fi].on; };
        u.panel = add<nova::Panel> (1, skin());
        u.panel->setTitle (tx (f == 0 ? "filt.filter1" : "filt.filter2").toUpperCase());
        addInfo (1, u.panel, "hlp.sy_filter", 1);
        u.on = toggle (1, tx ("filt.on"), [on] (const Desc& d) { return on (d); }, [fi] (Desc& d, bool b) { d.filters.filter[fi].on = b; });
        u.model = choice (1, {}, { tx ("filt.model_svf"), tx ("filt.model_ladder") }, [fi] (const Desc& d) { return d.filters.filter[fi].model == FilterModel::Ladder ? 1 : 0; },
                          [fi] (Desc& d, int v) { d.filters.filter[fi].model = v == 1 ? FilterModel::Ladder : FilterModel::SVF; }, on);
        u.mode = choice (1, {}, { tx ("filt.mode_lp"), tx ("filt.mode_hp"), tx ("filt.mode_bp"), tx ("filt.mode_notch") },
                         [fi] (const Desc& d) { const auto m = d.filters.filter[fi].mode; return m == FilterMode::HighPass ? 1 : m == FilterMode::BandPass ? 2 : m == FilterMode::Notch ? 3 : 0; },
                         [fi] (Desc& d, int v) { d.filters.filter[fi].mode = v == 1 ? FilterMode::HighPass : v == 2 ? FilterMode::BandPass : v == 3 ? FilterMode::Notch : FilterMode::LowPass; },
                         [fi] (const Desc& d) { return d.filters.filter[fi].on && d.filters.filter[fi].model == FilterModel::SVF; });
        u.slope = choice (1, {}, { "12 dB", "24 dB" }, [fi] (const Desc& d) { return d.filters.filter[fi].slopeDb >= 24 ? 1 : 0; },
                          [fi] (Desc& d, int v) { d.filters.filter[fi].slopeDb = v == 1 ? 24 : 12; }, on);
        u.cutoff = knob (1, mx::samplerParam (link_.track, f == 0 ? mx::kFilter1Cutoff : mx::kFilter2Cutoff), tx ("synth.cutoff"), false);
        u.resonance = knob (1, mx::samplerParam (link_.track, f == 0 ? mx::kFilter1Resonance : mx::kFilter2Resonance), tx ("synth.resonance"), false);
        u.key = slider (1, tx ("filt.key_track").toUpperCase(), -100.0, 200.0, 1.0, " %", [fi] (const Desc& d) { return d.filters.filter[fi].keyTrack * 100.0; },
                        [fi] (Desc& d, double v) { d.filters.filter[fi].keyTrack = v / 100.0; }, 0.0, on);
        u.velocity = slider (1, tx ("filt.velocity").toUpperCase(), -6.0, 6.0, 0.1, " oct", [fi] (const Desc& d) { return d.filters.filter[fi].velocityOctaves; },
                             [fi] (Desc& d, double v) { d.filters.filter[fi].velocityOctaves = v; }, 0.0, on);
        u.depth = slider (1, tx ("filt.env_depth").toUpperCase(), -8.0, 8.0, 0.1, " oct", [fi] (const Desc& d) { return d.filters.filter[fi].envDepthOctaves; },
                          [fi] (Desc& d, double v) { d.filters.filter[fi].envDepthOctaves = v; }, 0.0, on);
        u.drive = slider (1, tx ("filt.drive").toUpperCase(), 0.0, 100.0, 1.0, " %", [fi] (const Desc& d) { return d.filters.filter[fi].drive * 100.0; },
                          [fi] (Desc& d, double v) { d.filters.filter[fi].drive = v / 100.0; }, 0.0,
                          [fi] (const Desc& d) { return d.filters.filter[fi].on && d.filters.filter[fi].model == FilterModel::Ladder; });
        addInfo (1, u.key, "hlp.sy_keytrack", 1);
        addInfo (1, u.velocity, "hlp.sy_veltrack", 1);
        addInfo (1, u.depth, "hlp.sy_envdepth", 1);
        addInfo (1, u.drive, "hlp.sy_drive", 1);
        u.env = envelope (1, [fi] (const Desc& d) { return d.filters.filter[fi].env; }, [fi] (Desc& d, const EnvelopeDescription& e) { d.filters.filter[fi].env = e; }, on);
    }
}

void NovaSynthEditor::buildEnvPage()
{
    ampPanel_ = add<nova::Panel> (2, skin());
    ampPanel_->setTitle (tx ("synth.amp_env").toUpperCase());
    addInfo (2, ampPanel_, "hlp.sy_amp", 1);
    pitchPanel_ = add<nova::Panel> (2, skin());
    pitchPanel_->setTitle (tx ("synth.pitch_env").toUpperCase());
    addInfo (2, pitchPanel_, "hlp.sy_pitch", 1);
    ampEnv_ = envelope (2, [] (const Desc& d) { return d.ampEnv; }, [] (Desc& d, const EnvelopeDescription& e) { d.ampEnv = e; });
    pitchEnv_ = envelope (2, [] (const Desc& d) { return d.pitchEnv; }, [] (Desc& d, const EnvelopeDescription& e) { d.pitchEnv = e; });
    pitchDepth_ = knob (2, mx::synthParam (link_.track, mx::kPitchEnvDepth), tx ("synth.pitch_depth"), false);
    if (modulation_ != nullptr)
        bendRange_ = number (2, tx ("synth.bend_range").toUpperCase(), 0, SynthDescription::maxBendRange, [] (const Desc& d) { return d.bendRange; },
                             [] (Desc& d, int v) { d.bendRange = v; }, {});
    voicesPanel_ = add<nova::Panel> (2, skin());
    voicesPanel_->setTitle (tx ("sy.voices").toUpperCase());
    addInfo (2, voicesPanel_, "hlp.sy_voices", 1);
    polyMode_ = choice (2, tx ("poly.mode").toUpperCase(), { tx ("poly.mode_poly"), tx ("poly.mode_mono"), tx ("poly.mode_legato") },
                        [] (const Desc& d) { return static_cast<int> (d.polyphony); }, [] (Desc& d, int v) { d.polyphony = static_cast<PolyphonyMode> (v); });
    steal_ = choice (2, {}, { tx ("poly.steal_oldest"), tx ("poly.steal_quietest"), tx ("poly.steal_keep_low"), tx ("poly.steal_keep_high") },
                     [] (const Desc& d) { return static_cast<int> (d.stealMode); }, [] (Desc& d, int v) { d.stealMode = static_cast<VoiceStealMode> (v); },
                     [] (const Desc& d) { return d.polyphony == PolyphonyMode::Poly; });
    addInfo (2, steal_->pill, "hlp.sy_steal", 1);
    voices_ = slider (2, tx ("poly.limit").toUpperCase(), 1, SynthDescription::maxVoicesLimit, 1, "", [] (const Desc& d) { return static_cast<double> (d.maxVoices); },
                      [] (Desc& d, double v) { d.maxVoices = static_cast<int> (v); }, SynthDescription::defaultMaxVoices, [] (const Desc& d) { return d.polyphony == PolyphonyMode::Poly; });
    addInfo (2, voices_, "hlp.sy_limit", 1);
    glide_ = slider (2, tx ("poly.glide").toUpperCase(), 0, SynthDescription::maxGlideMs, 10, " ms", [] (const Desc& d) { return static_cast<double> (d.glideMs); },
                     [] (Desc& d, double v) { d.glideMs = static_cast<int> (v); }, 0.0, [] (const Desc& d) { return d.polyphony == PolyphonyMode::Legato; });
    addInfo (2, glide_, "hlp.sy_glide", 1);
}

// ------------------------------------------------------------------------------------------------ mise en page
void NovaSynthEditor::layout()
{
    // hauteur utile de la zone de pages (maquette) : 512 avec le clavier, 638 sans ; les rangées se répartissent en proportion (Y, stretch)
    const float H = contentH(), K = stretch();
    layoutCommon();

    // ---- page OSCILLATEURS
    for (int o = 0; o < numOscillators; ++o)
    {
        auto& u = osc_[o];
        const float px = X0 + static_cast<float> (o) * 318.0f;
        u.panel->setBounds (R (px, Y (0), 306, 366.0f * K));
        u.wave->setBounds (R (px + 12, Y (34), 140, 56));
        u.octave->setBounds (R (px + 160, Y (34), 134, 26));
        u.semitone->setBounds (R (px + 160, Y (64), 134, 26));
        for (std::size_t k = 0; k < u.knobs.size(); ++k) u.knobs[k]->setBounds (R (px + 8.0f + static_cast<float> (k) * 74.0f, Y (98), 72, 90));
        u.unison->setBounds (R (px + 12, Y (196), 100, 28));
        u.curve->pill->setBounds (R (px + 120, Y (196), 174, 28));
        u.spread->setBounds (R (px + 12, Y (234), 134, 46));
        u.phase->setBounds (R (px + 160, Y (234), 134, 46));
        u.pan->setBounds (R (px + 12, Y (288), 134, 46));
        u.retrigger->setBounds (R (px + 12, Y (334), 94, 26));
        if (u.sync != nullptr) u.sync->setBounds (R (px + 112, Y (334), 80, 26));
        u.mute->setBounds (R (px + 198, Y (334), 96, 26));
    }
    for (int s = 0; s < 2; ++s)
    {
        auto& u = s == 0 ? sub_ : noise_;
        const float px = X0 + static_cast<float> (s) * 477.0f;
        u.panel->setBounds (R (px, Y (376), 465, 136.0f * K));
        u.level->setBounds (R (px + 8, Y (376 + 34), 80, 90));
        u.wave->pill->setBounds (R (px + 100, Y (376 + 40), 160, 28));
        if (u.octave != nullptr) u.octave->pill->setBounds (R (px + 100, Y (376 + 76), 160, 28));
        u.pan->setBounds (R (px + 276, Y (376 + 32), 176, 46));
        u.mute->setBounds (R (px + 276, Y (376 + 90), 96, 26));
    }

    // ---- page FILTRES
    routePanel_->setBounds (R (X0, 72, 942, 64));
    routing_->pill->setBounds (R (X0 + 12, 72 + 30, 260, 28));
    balance_->setBounds (R (X0 + 300, 72 + 14, 280, 46));
    for (int f = 0; f < 2; ++f)
    {
        auto& u = filter_[f];
        const float px = X0 + static_cast<float> (f) * 477.0f, py = 72.0f + 74.0f, ph = H - 74.0f;
        u.panel->setBounds (R (px, py, 465, ph));
        u.on->setBounds (R (px + 465 - 120, py + 5, 70, 24));
        u.model->pill->setBounds (R (px + 12, py + 36, 144, 28));
        u.mode->pill->setBounds (R (px + 162, py + 36, 144, 28));
        u.slope->pill->setBounds (R (px + 312, py + 36, 141, 28));
        u.cutoff->setBounds (R (px + 12, py + 76, 104, 116));
        u.resonance->setBounds (R (px + 122, py + 76, 104, 116));
        u.key->setBounds (R (px + 240, py + 80, 106, 46));
        u.velocity->setBounds (R (px + 352, py + 80, 100, 46));
        u.depth->setBounds (R (px + 240, py + 134, 106, 46));
        u.drive->setBounds (R (px + 352, py + 134, 100, 46));
        u.env->setBounds (R (px + 12, py + 200, 441, ph - 212));
    }

    // ---- page MOD (plugin seul)
    if (modView_ != nullptr) modView_->setBounds (R (X0, 72, 942, H));

    // ---- page ENVELOPPES / VOIX
    {
        const float ph = H - 110.0f;
        ampPanel_->setBounds (R (X0, 72, 465, ph));
        ampEnv_->setBounds (R (X0 + 12, 72 + 36, 441, ph - 48));
        pitchPanel_->setBounds (R (X0 + 477, 72, 465, ph));
        pitchDepth_->setBounds (R (X0 + 477 + 12, 72 + 34, 104, 116));
        if (bendRange_ != nullptr) bendRange_->setBounds (R (X0 + 477 + 140, 72 + 64, 200, 28));
        pitchEnv_->setBounds (R (X0 + 477 + 12, 72 + 156, 441, ph - 168));
        const float vy = 72.0f + H - 100.0f;
        voicesPanel_->setBounds (R (X0, vy, 942, 100));
        polyMode_->pill->setBounds (R (X0 + 12, vy + 40, 270, 28));
        steal_->pill->setBounds (R (X0 + 292, vy + 40, 250, 28));
        voices_->setBounds (R (X0 + 560, vy + 28, 170, 46));
        glide_->setBounds (R (X0 + 750, vy + 28, 170, 46));
    }
}

void NovaSynthEditor::tick()
{
    if (! dragging())
    {
        const auto d = describe_();
        if (! (d == desc_)) desc_ = d;
    }
    tickCommon();
}
