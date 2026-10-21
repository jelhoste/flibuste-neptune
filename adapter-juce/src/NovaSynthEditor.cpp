#include "NovaSynthEditor.h"

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

// ------------------------------------------------------------------------------------------------ fabriques
void NovaSynthEditor::onUpdate (int page, std::function<void()> f)
{
    updaters_[static_cast<std::size_t> (page + 1)].push_back (std::move (f));
}

void NovaSynthEditor::gestureBegin() { dragging_ = true; if (link_.beginGesture) link_.beginGesture(); }
void NovaSynthEditor::gestureEnd() { if (link_.endGesture) link_.endGesture(); dragging_ = false; }

void NovaSynthEditor::edit (const std::function<void (Desc&)>& change)
{
    auto d = desc_;
    change (d);
    if (d == desc_) return;
    desc_ = d;
    link_.edit (d);
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
    auto* s = add<nova::ValueSlider> (page, skin(), std::move (title), lo, hi, step, std::move (suffix), [this, get] { return get (desc_); },
                                      [this, set] (double v) { edit ([&] (Desc& d) { set (d, v); }); }, def);
    s->gestureBegin = [this] { gestureBegin(); };
    s->gestureEnd = [this] { gestureEnd(); };
    onUpdate (page, [this, s, enabled] { s->update(); if (enabled) s->setActive (enabled (desc_)); });
    return s;
}

nova::NumberPill* NovaSynthEditor::number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                                           std::function<bool (const Desc&)> enabled)
{
    auto* n = add<nova::NumberPill> (page, skin(), std::move (label), lo, hi, [this, get] { return get (desc_); },
                                     [this, set] (int v) { edit ([&] (Desc& d) { set (d, v); }); });
    n->gestureBegin = [this] { gestureBegin(); };
    n->gestureEnd = [this] { gestureEnd(); };
    onUpdate (page, [this, n, enabled] { n->update(); if (enabled) n->setActive (enabled (desc_)); });
    return n;
}

nova::ChoicePill* NovaSynthEditor::choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                                           std::function<bool (const Desc&)> enabled)
{
    auto c = std::make_unique<nova::ChoicePill>();
    c->pill = add<nova::Pill> (page, skin(), nova::Pill::Look::Small);
    c->label = [label] { return label; };
    c->items = std::move (items);
    c->get = [this, get] { return get (desc_); };
    c->set = [this, set] (int v) { commit ([&] (Desc& d) { set (d, v); }); };
    c->attach (*this);
    auto* raw = c.get();
    choices_.push_back (std::move (c));
    onUpdate (page, [this, raw, enabled] { raw->refresh (! enabled || enabled (desc_)); });
    return raw;
}

nova::Pill* NovaSynthEditor::toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set, std::function<bool (const Desc&)> enabled)
{
    auto* p = add<nova::Pill> (page, skin(), nova::Pill::Look::Small);
    p->setText (text);
    p->onClick = [this, get, set] { const bool now = ! get (desc_); commit ([&] (Desc& d) { set (d, now); }); };
    onUpdate (page, [this, p, get, enabled] { p->setOn (get (desc_)); const bool en = ! enabled || enabled (desc_); p->setAlpha (en ? 1.0f : 0.4f); p->setInterceptsMouseClicks (en, en); });
    return p;
}

juce::String NovaSynthEditor::paramTitle (ParamId id) const
{
    const auto& set = hostRef_.parameters();
    const auto index = set.indexOf (id);
    return index == ParameterSet::npos ? juce::String() : juce::String::fromUTF8 (set.spec (index).name.c_str()).toUpperCase();
}

nova::KnobCard* NovaSynthEditor::knob (int page, ParamId id, juce::String title, bool compact)
{
    auto* k = add<nova::KnobCard> (page, skin(), host(), id, title.isEmpty() ? paramTitle (id) : title, modalOpener());
    k->setPlain (true);
    k->setCompact (compact);
    return k;
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
        if (dragging_) return;
        const auto env = get (desc_);
        if (! shown->first || ! (env == shown->second)) { shown->first = true; shown->second = env; e->setEnvelope (env); }
        if (enabled) e->setEnabled (enabled (desc_));
    });
    return e;
}

// ------------------------------------------------------------------------------------------------ construction
NovaSynthEditor::NovaSynthEditor (ParamHost& host, PresetHost& presets, SynthLink link, const theme::Palette& palette, theme::KnobStyle knobStyle, const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knobStyle, tr, std::move (options), juce::String::fromUTF8 ("ANALOGSYNTH")), hostRef_ (host), presets_ (presets), link_ (std::move (link))
{
    updaters_.resize (7);
    desc_ = link_.description();
    ui_ = std::make_unique<nova::PresetUi> (*this, presets_, palette, tr, [this] { desc_ = link_.description(); });
    addPage (tx ("synth.page_osc"), iconPath (0));
    addPage (tx ("synth.page_filter"), iconPath (1));
    addPage (tx ("synth.page_env"), iconPath (2));
    addPage (tx ("og.page_matrix"), iconPath (3));
    addPage (tx ("nv.page_settings"), iconPath (4));
    buildLeftColumn();
    buildOscPage();
    buildFilterPage();
    buildEnvPage();
    buildMatrixPage();
    buildKeyboard();
    buildSettingsPage (4);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaSynthEditor::~NovaSynthEditor() = default;

void NovaSynthEditor::buildLeftColumn()
{
    outPanel_ = add<nova::Panel> (-1, skin());
    outPanel_->setTitle (tx ("sy.output").toUpperCase());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    for (const auto id : link_.outputs) outKnobs_.push_back (knob (-1, id, {}, true));
    kbPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    kbPill_->onClick = [this] { keyboardShown_ = ! keyboardShown_; if (! keyboardShown_) keyboard_->releaseAll(); applyKeyboard(); resized(); };
    addInfo (-1, outPanel_, "hlp.sy_out", 1);
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
        addInfo (0, u.wave, "hlp.sy_wave", 1);
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
        addInfo (0, u.unison, "hlp.sy_unison", 1);
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
        addInfo (0, u.retrigger, "hlp.sy_retrig", 1);
        if (o > 0)
        {
            u.sync = toggle (0, tx ("synth.sync"), [idx] (const Desc& d) { return d.osc[idx].sync; }, [idx] (Desc& d, bool b) { d.osc[idx].sync = b; });
            addInfo (0, u.sync, "hlp.sy_sync", 1);
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

void NovaSynthEditor::buildMatrixPage()
{
    matrixPanel_ = add<nova::Panel> (3, skin());
    matrixPanel_->setTitle (tx ("og.matrix_title").toUpperCase());
    tiles_ = add<nova::PresetTiles> (3, skin(), presets_);
    tiles_->onLoaded = [this] { desc_ = link_.description(); };
    addInfo (3, matrixPanel_, "hlp.sy_matrix", true);
    bankPrev_ = add<nova::Pill> (3, skin(), nova::Pill::Look::Small);
    bankPrev_->setText ("<");
    bankPrev_->onClick = [this] { tiles_->bank = (tiles_->bank + preset::numBanks - 1) % preset::numBanks; };
    bankNext_ = add<nova::Pill> (3, skin(), nova::Pill::Look::Small);
    bankNext_->setText (">");
    bankNext_->onClick = [this] { tiles_->bank = (tiles_->bank + 1) % preset::numBanks; };
    bankPill_ = add<nova::Pill> (3, skin());
    bankPill_->setStatic (true);
    colsPill_ = add<nova::Pill> (3, skin());
    colsPill_->onClick = [this] { tiles_->cols = tiles_->cols % 8 + 1; };
    rowsPill_ = add<nova::Pill> (3, skin());
    rowsPill_->onClick = [this] { tiles_->rows = tiles_->rows % 8 + 1; };
    fieldPill_ = add<nova::Pill> (3, skin());
    fieldPill_->onClick = [this] { tiles_->field = (tiles_->field + 1) % 5; };
    addInfo (3, fieldPill_, "hlp.og_field");
}

void NovaSynthEditor::buildKeyboard()
{
    keyboardPanel_ = add<nova::Panel> (-2, skin());
    keyboardPanel_->setTitle (tx ("synth.keyboard").toUpperCase());
    addInfo (-2, keyboardPanel_, "hlp.sy_keyboard", 1);
    keyboard_ = add<SynthKeyboard> (-2, skin().palette());
    keyboard_->onNoteOn = [this] (int key, float v) { if (link_.noteOn) link_.noteOn (key, v); };
    keyboard_->onNoteOff = [this] (int key) { if (link_.noteOff) link_.noteOff (key); };
    octDown_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    octDown_->setText (tx ("synth.oct_down"));
    octDown_->onClick = [this] { keyboard_->shiftOctave (-1); };
    octUp_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    octUp_->setText (tx ("synth.oct_up"));
    octUp_->onClick = [this] { keyboard_->shiftOctave (1); };
    holdPill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    holdPill_->setText (tx ("synth.hold"));
    holdPill_->onClick = [this] { holdPill_->setOn (! holdPill_->isOn()); keyboard_->setHold (holdPill_->isOn()); };
    panicPill_ = add<nova::Pill> (-2, skin(), nova::Pill::Look::Small);
    panicPill_->setText (tx ("panic.button"));
    panicPill_->onClick = [this] { if (link_.panic) link_.panic(); keyboard_->releaseAll(); };
}

// ------------------------------------------------------------------------------------------------ presets
void NovaSynthEditor::showPresetMenu (juce::Component* anchor) { ui_->showMenu (anchor, tiles_ != nullptr ? tiles_->bank : 0); }

bool NovaSynthEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'S' || k.getKeyCode() == 's')) { ui_->quickSave(); return true; }
    return EditorBase::keyPressed (k);
}

// ------------------------------------------------------------------------------------------------ clavier masquable
void NovaSynthEditor::applyKeyboard()
{
    const bool shown = keyboardShown_ && currentPage() != numPages() - 1;
    for (juce::Component* c : std::initializer_list<juce::Component*> { keyboardPanel_, keyboard_, octDown_, octUp_, holdPill_, panicPill_ }) c->setVisible (shown);
    setInfoVisible (keyboardPanel_, shown);
}

void NovaSynthEditor::pageChanged() { if (keyboardPanel_ != nullptr) applyKeyboard(); }

// ------------------------------------------------------------------------------------------------ mise en page
void NovaSynthEditor::layout()
{
    // hauteur utile de la zone de pages (design) : 512 avec le clavier, 638 sans ; les rangées se répartissent en proportion
    const float H = keyboardShown_ ? 512.0f : 638.0f, K = H / 512.0f;
    auto Y = [K] (float rel) { return 72.0f + rel * K; };
    constexpr float X0 = 204.0f;

    // ---- colonne de gauche
    outPanel_->setBounds (R (14, 72, 176, 316));
    for (std::size_t i = 0; i < outKnobs_.size(); ++i)
        outKnobs_[i]->setBounds (R (22.0f + static_cast<float> (i % 2) * 84.0f, 106.0f + static_cast<float> (i / 2) * 92.0f, 80, 88));
    navPanel_->setBounds (R (14, 400, 176, 310));
    layoutNav (400, 250);
    kbPill_->setText (tx (keyboardShown_ ? "synth.kb_shown" : "synth.kb_hidden"));
    kbPill_->setBounds (R (24, 664, 156, 30));

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

    // ---- page ENVELOPPES / VOIX
    {
        const float ph = H - 110.0f;
        ampPanel_->setBounds (R (X0, 72, 465, ph));
        ampEnv_->setBounds (R (X0 + 12, 72 + 36, 441, ph - 48));
        pitchPanel_->setBounds (R (X0 + 477, 72, 465, ph));
        pitchDepth_->setBounds (R (X0 + 477 + 12, 72 + 34, 104, 116));
        pitchEnv_->setBounds (R (X0 + 477 + 12, 72 + 156, 441, ph - 168));
        const float vy = 72.0f + H - 100.0f;
        voicesPanel_->setBounds (R (X0, vy, 942, 100));
        polyMode_->pill->setBounds (R (X0 + 12, vy + 40, 270, 28));
        steal_->pill->setBounds (R (X0 + 292, vy + 40, 250, 28));
        voices_->setBounds (R (X0 + 560, vy + 28, 170, 46));
        glide_->setBounds (R (X0 + 750, vy + 28, 170, 46));
    }

    // ---- page MATRICE
    matrixPanel_->setBounds (R (X0, 72, 942, H));
    bankPrev_->setBounds (R (X0 + 16, 72 + 36, 36, 30));
    bankPill_->setBounds (R (X0 + 56, 72 + 36, 150, 30));
    bankNext_->setBounds (R (X0 + 210, 72 + 36, 36, 30));
    colsPill_->setBounds (R (X0 + 266, 72 + 36, 150, 30));
    rowsPill_->setBounds (R (X0 + 426, 72 + 36, 150, 30));
    fieldPill_->setBounds (R (X0 + 586, 72 + 36, 310, 30));
    tiles_->setBounds (R (X0 + 16, 72 + 80, 910, H - 92));

    // ---- clavier
    keyboardPanel_->setBounds (R (X0, 596, 942, 114));
    keyboard_->setBounds (R (X0 + 12, 626, 700, 78));
    octDown_->setBounds (R (X0 + 732, 628, 60, 26));
    octUp_->setBounds (R (X0 + 798, 628, 60, 26));
    holdPill_->setBounds (R (X0 + 864, 628, 66, 26));
    panicPill_->setBounds (R (X0 + 732, 664, 198, 30));
    applyKeyboard();

    // ---- barre du bas, en-tête, gestionnaire
    abPill()->setBounds (R (14, 724 + 12, 40, 32));
    presetPill()->setBounds (R (440, 11, 280, 36));
    ui_->resized();
}

void NovaSynthEditor::tick()
{
    if (! dragging_)
    {
        const auto d = link_.description();
        if (! (d == desc_)) desc_ = d;
    }
    presetPill()->setText (ui_->label());
    for (auto& f : updaters_[0]) f();
    const int page = currentPage();
    if (page >= 0 && page < 5) for (auto& f : updaters_[static_cast<std::size_t> (page + 1)]) f();
    if (keyboardShown_ && page != numPages() - 1 && link_.heldKeys) keyboard_->setHeldKeys (link_.heldKeys (0), link_.heldKeys (1));
    if (page == 3)
    {
        tiles_->update();
        bankPill_->setText (tx ("og.bank") + " " + juce::String::fromUTF8 (preset::defaultBankName (tiles_->bank).c_str()) + " (" + juce::String (tiles_->bank) + ")");
        colsPill_->setText (tx ("og.columns") + "  " + juce::String (tiles_->cols));
        rowsPill_->setText (tx ("og.rows") + "  " + juce::String (tiles_->rows));
        static const char* const fields[5] = { "og.f_preset", "og.f_song", "og.f_workshop", "og.f_student", "og.f_event" };
        fieldPill_->setText (tx ("og.main_field") + ": " + tx (fields[tiles_->field]));
    }
}

void NovaSynthEditor::paintBackground (juce::Graphics& g)
{
    g.setColour (skin().colour (Token::TextDim2));
    g.setFont (skin().font (11.0f, true));
    if (ui_->statusText.isNotEmpty()) g.drawText (ui_->statusText, R (730, 724 + 12, 416, 32).toFloat(), juce::Justification::centredRight);
}
