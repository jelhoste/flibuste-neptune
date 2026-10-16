#include "SynthView.h"
#include <array>
#include <cctype>

using namespace neptune::core;
using neptune::core::theme::Token;

// ------------------------------------------------------------------------------------------------ forme d'onde

WaveButton::WaveButton (const theme::Palette& palette, const i18n::Translator& tr) : juce::Button ("wave"), palette_ (palette), tr_ (tr)
{
    preview_.resize (96);
    setWave (kWaveSaw);
}

void WaveButton::setWave (int wave)
{
    wave = juce::jlimit (0, kNumWaves - 1, wave);
    if (wave == wave_ && haveData_) return; // le calcul d'un aperçu passe par une FFT : pas à chaque rafraîchissement
    wave_ = wave;
    haveData_ = true;
    renderWaveCycle (wave_, preview_.data(), static_cast<int> (preview_.size()), 0.25);
    repaint();
}

void WaveButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (c (over ? Token::Nav1 : Token::BgApp));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (c (Token::Border));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    auto plot = r.reduced (6.0f, 4.0f).withTrimmedBottom (14.0f);
    juce::Path p;
    for (std::size_t i = 0; i < preview_.size(); ++i)
    {
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (preview_.size() - 1);
        const float y = plot.getCentreY() - preview_[i] * plot.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (c (Token::Accent));
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setColour (c (Token::Text));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 (std::string (tr_.tr (waveInfo (wave_).nameKey)).c_str()), r.withTrimmedTop (r.getHeight() - 16.0f).toNearestInt(),
                juce::Justification::centred);
}

// Explorateur : dalle de toutes les formes, filtrable par catégorie et par texte.
class WaveExplorer : public juce::Component
{
public:
    WaveExplorer (const theme::Palette& palette, const i18n::Translator& tr, int current, std::function<void (int)> onPick)
        : palette_ (palette), tr_ (tr), current_ (current), onPick_ (std::move (onPick))
    {
        auto text = [this] (const char* k) { return juce::String::fromUTF8 (std::string (tr_.tr (k)).c_str()); };
        const char* cats[4] = { "wave.cat_all", "wave.cat_digital", "wave.cat_analog", "wave.cat_complex" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = catButtons_.add (new juce::TextButton (text (cats[i])));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7001);
            b->setToggleState (i == 0, juce::dontSendNotification);
            b->onClick = [this, i] { category_ = i - 1; refilter(); };
            addAndMakeVisible (b);
        }
        search_.setTextToShowWhenEmpty (text ("wave.search"), colour (Token::TextDim));
        search_.onTextChange = [this] { refilter(); };
        addAndMakeVisible (search_);
        for (int w = 0; w < kNumWaves; ++w)
        {
            std::vector<float> data (80);
            renderWaveCycle (w, data.data(), 80, 0.25);
            previews_.push_back (std::move (data));
        }
        refilter();
        setSize (kCols * kTileW + 16, 32 + 6 + kRowsVisible * kTileH + 16);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (32);
        for (auto* b : catButtons_) b->setBounds (top.removeFromLeft (110).reduced (2));
        search_.setBounds (top.reduced (2));
        grid_ = r.withTrimmedTop (6);
        clampScroll();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour (Token::BgCard));
        const int cols = kCols, tileW = grid_.getWidth() / cols, tileH = kTileH;
        g.saveState();
        g.reduceClipRegion (grid_);
        for (std::size_t n = 0; n < shown_.size(); ++n)
        {
            const int w = shown_[n];
            const auto tile = juce::Rectangle<int> (grid_.getX() + static_cast<int> (n % cols) * tileW,
                                                    grid_.getY() + static_cast<int> (n / cols) * tileH - scroll_, tileW, tileH).reduced (3);
            g.setColour (colour (w == current_ ? Token::Nav1 : Token::BgApp));
            g.fillRoundedRectangle (tile.toFloat(), 5.0f);
            g.setColour (colour (w == current_ ? Token::Accent : Token::Border));
            g.drawRoundedRectangle (tile.toFloat(), 5.0f, w == current_ ? 2.0f : 1.0f);
            auto plot = tile.toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (18.0f);
            juce::Path p;
            const auto& data = previews_[static_cast<std::size_t> (w)];
            for (std::size_t i = 0; i < data.size(); ++i)
            {
                const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / static_cast<float> (data.size() - 1);
                const float y = plot.getCentreY() - data[i] * plot.getHeight() * 0.5f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (colour (Token::Accent));
            g.strokePath (p, juce::PathStrokeType (1.4f));
            g.setColour (colour (Token::Text));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String::fromUTF8 (std::string (tr_.tr (waveInfo (w).nameKey)).c_str()), tile.withTrimmedTop (tile.getHeight() - 18), juce::Justification::centred);
        }
        g.restoreState();
        // barre de défilement (visible seulement si tout ne tient pas)
        const int total = contentHeight();
        if (total > grid_.getHeight())
        {
            const float ratio = static_cast<float> (grid_.getHeight()) / static_cast<float> (total);
            const float barH = juce::jmax (24.0f, ratio * static_cast<float> (grid_.getHeight()));
            const float y = static_cast<float> (grid_.getY()) + (static_cast<float> (scroll_) / static_cast<float> (total - grid_.getHeight())) * (static_cast<float> (grid_.getHeight()) - barH);
            g.setColour (colour (Token::TextDim).withAlpha (0.6f));
            g.fillRoundedRectangle (static_cast<float> (grid_.getRight()) - 6.0f, y, 5.0f, barH, 2.5f);
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll_ -= static_cast<int> (w.deltaY * 220.0f);
        clampScroll();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int cols = kCols, tileW = grid_.getWidth() / cols, tileH = kTileH;
        if (! grid_.contains (e.getPosition())) return;
        const int col = (e.x - grid_.getX()) / tileW, row = (e.y - grid_.getY() + scroll_) / tileH;
        const auto n = static_cast<std::size_t> (row * cols + col);
        if (n < shown_.size() && onPick_) onPick_ (shown_[n]);
    }

private:
    static constexpr int kCols = 8, kTileW = 100, kTileH = 78, kRowsVisible = 4;

    juce::Colour colour (Token t) const { const auto& c = palette_.color (t); return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a)); }
    int contentHeight() const { return static_cast<int> ((shown_.size() + kCols - 1) / kCols) * kTileH; }
    void clampScroll() { scroll_ = juce::jlimit (0, juce::jmax (0, contentHeight() - grid_.getHeight()), scroll_); }

    void refilter()
    {
        scroll_ = 0;
        shown_.clear();
        const auto q = search_.getText().trim().toLowerCase();
        for (int w = 0; w < kNumWaves; ++w)
        {
            const auto& info = waveInfo (w);
            if (category_ >= 0 && static_cast<int> (info.category) != category_) continue;
            const auto name = juce::String::fromUTF8 (std::string (tr_.tr (info.nameKey)).c_str()).toLowerCase();
            if (q.isNotEmpty() && ! name.contains (q) && ! juce::String (std::string (info.key)).contains (q)) continue;
            shown_.push_back (w);
        }
        repaint();
    }

    theme::Palette palette_;
    const i18n::Translator& tr_;
    int current_;
    std::function<void (int)> onPick_;
    juce::OwnedArray<juce::TextButton> catButtons_;
    juce::TextEditor search_;
    std::vector<std::vector<float>> previews_;
    std::vector<int> shown_;
    int category_ = -1;
    int scroll_ = 0;
    juce::Rectangle<int> grid_;
};

// ------------------------------------------------------------------------------------------------ vue

struct SynthView::OscPanel
{
    OscPanel (const theme::Palette& p, const i18n::Translator& t) : wave (p, t) {}
    WaveButton wave;
    juce::Slider octave { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider semitone { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider unison { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider spread { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider phase { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider pan { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox curve;
    juce::ToggleButton retrigger, sync, mute;
    NeptuneKnob *level = nullptr, *fine = nullptr, *pulse = nullptr, *detune = nullptr;
    juce::Rectangle<int> area;
};

struct SynthView::Extras
{
    // sous-oscillateur et bruit
    juce::ComboBox subWave, subOctave, noiseType;
    juce::ToggleButton subMute, noiseMute;
    juce::Slider subPan { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider noisePan { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    NeptuneKnob *subLevel = nullptr, *noiseLevel = nullptr;
    juce::Rectangle<int> subArea, noiseArea;
    // filtres
    juce::ComboBox routing;
    juce::Slider balance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::array<juce::ToggleButton, 2> fOn;
    std::array<juce::ComboBox, 2> fModel, fMode, fSlope;
    std::array<juce::Slider, 2> fKey, fVel, fDepth, fDrive;
    std::array<std::unique_ptr<EnvelopeEditor>, 2> fEnv;
    std::array<juce::Rectangle<int>, 2> fArea;
    // enveloppes et voix
    std::unique_ptr<EnvelopeEditor> ampEnv, pitchEnv;
    juce::ComboBox polyMode, steal;
    juce::Slider voices { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider glide { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    NeptuneKnob* pitchDepth = nullptr;
    juce::Rectangle<int> ampArea, pitchArea, voicesArea;
    std::vector<std::pair<juce::Component*, juce::String>> captions;
    std::vector<std::pair<juce::Rectangle<int>*, juce::String>> titles; // zones titrées (page 0 : oscillateurs ; etc.)
};

SynthView::SynthView (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, theme::KnobStyle knobStyle)
    : host_ (host), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), x_ (std::make_unique<Extras>()), keyboard_ (palette)
{
    sliderLook_ = std::make_unique<NeptuneSliderLook> (palette_, knobStyle_);
    auto& x = *x_;
    auto reg = [this, &x] (int page, juce::Component& c, const char* captionKey = nullptr)
    {
        addAndMakeVisible (c);
        pageControls_.push_back ({ page, &c });
        if (captionKey != nullptr) x.captions.push_back ({ &c, text (captionKey) });
    };
    auto contSlider = [this] (juce::Slider& s, double lo, double hi, double step, const char* suffix, double reset, std::function<void (double)> apply)
    {
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setDoubleClickReturnValue (true, reset);
        s.onDragStart = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
        s.onDragEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
        s.onValueChange = [this, &s, apply] { if (updating_) return; apply (s.getValue()); emit(); };
    };
    auto stepSlider = [this] (juce::Slider& s, double lo, double hi, std::function<void (double)> apply)
    {
        s.setRange (lo, hi, 1.0);
        s.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
        s.onValueChange = [this, &s, apply] { if (updating_) return; gesture ([&s, apply] { apply (s.getValue()); }); };
    };
    auto choice = [this] (juce::ComboBox& c, std::initializer_list<const char*> keys, std::function<void (int)> apply)
    {
        int n = 1;
        for (const auto* k : keys) c.addItem (text (k), n++);
        c.onChange = [this, &c, apply] { if (updating_ || c.getSelectedId() < 1) return; gesture ([&c, apply] { apply (c.getSelectedId() - 1); }); };
    };
    auto toggle = [this] (juce::ToggleButton& t, const char* key, std::function<void (bool)> apply)
    {
        t.setButtonText (text (key));
        t.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        t.onClick = [this, &t, apply] { if (updating_) return; gesture ([&t, apply] { apply (t.getToggleState()); }); };
    };

    // ---- tabs
    const char* tabKeys[3] = { "synth.page_osc", "synth.page_filter", "synth.page_env" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = tabButtons_.add (new juce::TextButton (text (tabKeys[i])));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (7100);
        b->setColour (juce::TextButton::buttonOnColourId, colour (Token::Accent));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b->setToggleState (i == 0, juce::dontSendNotification);
        b->onClick = [this, i] { setPage (i); };
        addAndMakeVisible (b);
    }

    // ---- oscillateurs
    for (int o = 0; o < numOscillators; ++o)
    {
        panels_[o] = std::make_unique<OscPanel> (palette_, tr_);
        auto& p = *panels_[o];
        const auto idx = static_cast<std::size_t> (o);
        reg (0, p.wave);
        p.wave.onClick = [this, o] { openExplorer (o); };
        stepSlider (p.octave, SynthDescription::minOctave, SynthDescription::maxOctave, [this, idx] (double v) { d_.osc[idx].octave = static_cast<int> (v); });
        stepSlider (p.semitone, -12, 12, [this, idx] (double v) { d_.osc[idx].semitone = static_cast<int> (v); });
        stepSlider (p.unison, 1, maxUnison, [this, idx] (double v) { d_.osc[idx].unison = static_cast<int> (v); });
        contSlider (p.spread, 0.0, 100.0, 1.0, " %", 50.0, [this, idx] (double v) { d_.osc[idx].spread = v / 100.0; });
        contSlider (p.phase, 0.0, 360.0, 1.0, " deg", 0.0, [this, idx] (double v) { d_.osc[idx].startPhase = v / 360.0; });
        contSlider (p.pan, -100.0, 100.0, 1.0, "", 0.0, [this, o] (double v) { d_.mix[static_cast<std::size_t> (o)].pan = v / 100.0; });
        choice (p.curve, { "synth.curve_linear", "synth.curve_supersaw" }, [this, idx] (int i) { d_.osc[idx].curve = i == 1 ? DetuneCurve::Supersaw : DetuneCurve::Linear; });
        toggle (p.retrigger, "synth.retrigger", [this, idx] (bool b) { d_.osc[idx].retrigger = b; });
        toggle (p.sync, "synth.sync", [this, idx] (bool b) { d_.osc[idx].sync = b; });
        toggle (p.mute, "synth.mute", [this, o] (bool b) { d_.mix[static_cast<std::size_t> (o)].mute = b; });
        reg (0, p.octave, "synth.octave");
        reg (0, p.semitone, "synth.semitone");
        reg (0, p.unison, "synth.unison");
        reg (0, p.spread, "synth.spread");
        reg (0, p.phase, "synth.phase");
        reg (0, p.pan, "synth.mix_pan");
        reg (0, p.curve, "synth.curve");
        reg (0, p.retrigger);
        if (o > 0) reg (0, p.sync);
        reg (0, p.mute);
        p.level = addKnob (0, "synth.level", [o] (int t) { return mixer::synthParam (t, static_cast<mixer::SynthKind> (mixer::kLevelOsc1 + o)); });
        p.fine = addKnob (0, "synth.fine", [o] (int t) { return mixer::synthParam (t, static_cast<mixer::SynthKind> (mixer::kFineOsc1 + o)); });
        p.pulse = addKnob (0, "synth.pulse", [o] (int t) { return mixer::synthParam (t, static_cast<mixer::SynthKind> (mixer::kPulseOsc1 + o)); });
        p.detune = addKnob (0, "synth.detune", [o] (int t) { return mixer::synthParam (t, static_cast<mixer::SynthKind> (mixer::kDetuneOsc1 + o)); });
    }
    // ---- sous-oscillateur et bruit
    choice (x.subWave, { "synth.sub_sine", "synth.sub_triangle", "synth.sub_square" },
            [this] (int i) { d_.subWave = i == 1 ? SubWave::Triangle : i == 2 ? SubWave::Square : SubWave::Sine; });
    choice (x.subOctave, { "-1", "-2" }, [this] (int i) { d_.subOctave = i == 1 ? -2 : -1; });
    choice (x.noiseType, { "synth.noise_white", "synth.noise_pink" }, [this] (int i) { d_.noise = i == 1 ? NoiseType::Pink : NoiseType::White; });
    toggle (x.subMute, "synth.mute", [this] (bool b) { d_.mix[kSourceSub].mute = b; });
    toggle (x.noiseMute, "synth.mute", [this] (bool b) { d_.mix[kSourceNoise].mute = b; });
    contSlider (x.subPan, -100.0, 100.0, 1.0, "", 0.0, [this] (double v) { d_.mix[kSourceSub].pan = v / 100.0; });
    contSlider (x.noisePan, -100.0, 100.0, 1.0, "", 0.0, [this] (double v) { d_.mix[kSourceNoise].pan = v / 100.0; });
    for (auto* c : std::initializer_list<juce::Component*> { &x.subWave, &x.subOctave, &x.noiseType, &x.subMute, &x.noiseMute, &x.subPan, &x.noisePan }) reg (0, *c);
    x.captions.push_back ({ &x.subPan, text ("synth.mix_pan") });
    x.captions.push_back ({ &x.noisePan, text ("synth.mix_pan") });
    x.captions.push_back ({ &x.subOctave, text ("synth.sub_octave") });
    x.subLevel = addKnob (0, "synth.level", [] (int t) { return mixer::synthParam (t, mixer::kLevelSub); });
    x.noiseLevel = addKnob (0, "synth.level", [] (int t) { return mixer::synthParam (t, mixer::kLevelNoise); });

    // ---- filtres
    choice (x.routing, { "filt.series", "filt.parallel" }, [this] (int i) { d_.filters.parallel = i == 1; });
    contSlider (x.balance, 0.0, 100.0, 1.0, " %", 50.0, [this] (double v) { d_.filters.balance = v / 100.0; });
    reg (1, x.routing, "filt.routing");
    reg (1, x.balance, "filt.balance");
    for (int f = 0; f < 2; ++f)
    {
        const auto fi = static_cast<std::size_t> (f);
        toggle (x.fOn[fi], "filt.on", [this, fi] (bool b) { d_.filters.filter[fi].on = b; });
        choice (x.fModel[fi], { "filt.model_svf", "filt.model_ladder" },
                [this, fi] (int i) { d_.filters.filter[fi].model = i == 1 ? FilterModel::Ladder : FilterModel::SVF; });
        choice (x.fMode[fi], { "filt.mode_lp", "filt.mode_hp", "filt.mode_bp", "filt.mode_notch" }, [this, fi] (int i)
        {
            d_.filters.filter[fi].mode = i == 1 ? FilterMode::HighPass : i == 2 ? FilterMode::BandPass : i == 3 ? FilterMode::Notch : FilterMode::LowPass;
        });
        choice (x.fSlope[fi], { "12 dB", "24 dB" }, [this, fi] (int i) { d_.filters.filter[fi].slopeDb = i == 1 ? 24 : 12; });
        contSlider (x.fKey[fi], -100.0, 200.0, 1.0, " %", 0.0, [this, fi] (double v) { d_.filters.filter[fi].keyTrack = v / 100.0; });
        contSlider (x.fVel[fi], -6.0, 6.0, 0.1, " oct", 0.0, [this, fi] (double v) { d_.filters.filter[fi].velocityOctaves = v; });
        contSlider (x.fDepth[fi], -8.0, 8.0, 0.1, " oct", 0.0, [this, fi] (double v) { d_.filters.filter[fi].envDepthOctaves = v; });
        contSlider (x.fDrive[fi], 0.0, 100.0, 1.0, " %", 0.0, [this, fi] (double v) { d_.filters.filter[fi].drive = v / 100.0; });
        for (auto* dial : { &x.fKey[fi], &x.fVel[fi], &x.fDepth[fi], &x.fDrive[fi] }) // potards plutôt que curseurs
        {
            dial->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            dial->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            dial->setLookAndFeel (sliderLook_.get());
            dial->setColour (juce::Slider::textBoxTextColourId, colour (Token::Accent));
            dial->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
        reg (1, x.fOn[fi]);
        reg (1, x.fModel[fi], "filt.model");
        reg (1, x.fMode[fi], "filt.mode");
        reg (1, x.fSlope[fi], "filt.slope");
        reg (1, x.fKey[fi], "filt.key_track");
        reg (1, x.fVel[fi], "filt.velocity");
        reg (1, x.fDepth[fi], "filt.env_depth");
        reg (1, x.fDrive[fi], "filt.drive");
        x.fEnv[fi] = std::make_unique<EnvelopeEditor> (palette_, tr_);
        x.fEnv[fi]->onGestureBegin = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
        x.fEnv[fi]->onGestureEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
        x.fEnv[fi]->onChange = [this, fi] (const EnvelopeDescription& e) { if (updating_) return; d_.filters.filter[fi].env = e; emit(); };
        reg (1, *x.fEnv[fi]);
        addKnob (1, "synth.cutoff", [f] (int t) { return mixer::samplerParam (t, f == 0 ? mixer::kFilter1Cutoff : mixer::kFilter2Cutoff); });
        addKnob (1, "synth.resonance", [f] (int t) { return mixer::samplerParam (t, f == 0 ? mixer::kFilter1Resonance : mixer::kFilter2Resonance); });
    }

    // ---- enveloppes et voix
    x.ampEnv = std::make_unique<EnvelopeEditor> (palette_, tr_);
    x.pitchEnv = std::make_unique<EnvelopeEditor> (palette_, tr_);
    for (auto* env : { x.ampEnv.get(), x.pitchEnv.get() })
    {
        env->onGestureBegin = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
        env->onGestureEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
        reg (2, *env);
    }
    x.ampEnv->onChange = [this] (const EnvelopeDescription& e) { if (updating_) return; d_.ampEnv = e; emit(); };
    x.pitchEnv->onChange = [this] (const EnvelopeDescription& e) { if (updating_) return; d_.pitchEnv = e; emit(); };
    x.pitchDepth = addKnob (2, "synth.pitch_depth", [] (int t) { return mixer::synthParam (t, mixer::kPitchEnvDepth); });
    choice (x.polyMode, { "poly.mode_poly", "poly.mode_mono", "poly.mode_legato" }, [this] (int i) { d_.polyphony = static_cast<PolyphonyMode> (i); });
    choice (x.steal, { "poly.steal_oldest", "poly.steal_quietest", "poly.steal_keep_low", "poly.steal_keep_high" },
            [this] (int i) { d_.stealMode = static_cast<VoiceStealMode> (i); });
    contSlider (x.voices, 1, SynthDescription::maxVoicesLimit, 1, "", SynthDescription::defaultMaxVoices, [this] (double v) { d_.maxVoices = static_cast<int> (v); });
    contSlider (x.glide, 0, SynthDescription::maxGlideMs, 10, " ms", 0.0, [this] (double v) { d_.glideMs = static_cast<int> (v); });
    reg (2, x.polyMode, "poly.mode");
    reg (2, x.steal, "poly.steal");
    reg (2, x.voices, "poly.limit");
    reg (2, x.glide, "poly.glide");

    // ---- clavier
    keyboardButton_.onClick = [this]
    {
        keyboardMode_ = keyboardMode_ < 0 ? 0 : keyboardMode_ == 0 ? 1 : -1; // auto -> masqué -> affiché -> auto
        updateKeyboardButton();
        if (! lastArea_.isEmpty()) placeScaled (lastArea_);
    };
    updateKeyboardButton();
    addAndMakeVisible (keyboardButton_);
    keysHint_.setText (text ("synth.keys_hint"), juce::dontSendNotification);
    keysHint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    keysHint_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (keysHint_);
    keyboard_.onNoteOn = [this] (int key, float v) { if (onNoteOn) onNoteOn (key, v); };
    keyboard_.onNoteOff = [this] (int key) { if (onNoteOff) onNoteOff (key); };
    addAndMakeVisible (keyboard_);
    octDown_.setButtonText (text ("synth.oct_down"));
    octUp_.setButtonText (text ("synth.oct_up"));
    octDown_.onClick = [this] { keyboard_.shiftOctave (-1); };
    octUp_.onClick = [this] { keyboard_.shiftOctave (1); };
    hold_.setButtonText (text ("synth.hold"));
    hold_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    hold_.onClick = [this] { keyboard_.setHold (hold_.getToggleState()); };
    addAndMakeVisible (octDown_);
    addAndMakeVisible (octUp_);
    addAndMakeVisible (hold_);

    refreshControls();
    setPage (0);
    startTimerHz (20);
}

SynthView::~SynthView()
{
    stopTimer();
    for (auto& f : { std::ref (x_->fKey), std::ref (x_->fVel), std::ref (x_->fDepth), std::ref (x_->fDrive) })
        for (auto& d : f.get()) d.setLookAndFeel (nullptr);
}

void SynthView::placeScaled (juce::Rectangle<int> area)
{
    lastArea_ = area;
    // clavier : affiché si l'utilisateur le veut, ou (auto) si la hauteur suffit
    const bool shown = keyboardMode_ == 1 || (keyboardMode_ < 0 && area.getHeight() >= designHeight);
    if (keyboardShown_ && ! shown) keyboard_.releaseAll(); // pas de note restée tenue sous un clavier masqué
    keyboardShown_ = shown;
    const int dh = shown ? designHeight : designHeightNoKeyboard;
    const float s = juce::jmin (1.0f, static_cast<float> (area.getWidth()) / designWidth, static_cast<float> (area.getHeight()) / dh);
    const float scale = juce::jmax (0.4f, s);
    setTransform (juce::AffineTransform::scale (scale));
    // JUCE applique la transformation aux limites exprimées dans le repère du PARENT (autour de l'origine du parent, pas du coin de
    // la composante) : pour que la vue apparaisse à (area.x, area.y) il faut placer ses limites à area / échelle.
    const auto x = juce::roundToInt (static_cast<float> (area.getX()) / scale);
    const auto y = juce::roundToInt (static_cast<float> (area.getY()) / scale);
    const auto w = juce::roundToInt (static_cast<float> (area.getWidth()) / scale);
    const auto h = juce::roundToInt (static_cast<float> (area.getHeight()) / scale);
    if (getWidth() == w && getHeight() == h) resized(); // la visibilité du clavier a pu changer sans changement de taille
    setBounds (x, y, w, h);
}

void SynthView::updateKeyboardButton()
{
    const char* key = keyboardMode_ < 0 ? "synth.kb_auto" : keyboardMode_ == 0 ? "synth.kb_hidden" : "synth.kb_shown";
    keyboardButton_.setButtonText (text (key));
}

// Les valeurs affichées sous les potards sont dessinées par cette vue : elles doivent suivre l'automation et les presets.
void SynthView::timerCallback()
{
    if (! isVisible()) return;
    shownVersions_.resize (knobs_.size(), 0u);
    bool changed = false;
    for (std::size_t i = 0; i < knobs_.size(); ++i)
    {
        if (knobs_[i].page != page_) continue;
        const auto v = host_.parameters().version (knobs_[i].idFor (track_));
        if (v != shownVersions_[i]) { shownVersions_[i] = v; changed = true; }
    }
    if (changed) repaint();
}

juce::Colour SynthView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String SynthView::text (const char* key) const
{
    const std::string k (key);
    if (! k.empty() && (k[0] == '-' || std::isdigit (static_cast<unsigned char> (k[0])))) return juce::String (k); // valeur littérale
    return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str());
}

NeptuneKnob* SynthView::addKnob (int page, const char* labelKey, std::function<ParamId (int)> idFor)
{
    Knob k;
    k.idFor = std::move (idFor);
    k.labelKey = labelKey;
    k.page = page;
    k.knob = std::make_unique<NeptuneKnob> (host_, k.idFor (track_), palette_, knobStyle_,
                                            [this] (ParamId pid, uistate::Terminal t) { openModal (pid, t); });
    addAndMakeVisible (*k.knob);
    auto* raw = k.knob.get();
    knobs_.push_back (std::move (k));
    return raw;
}

void SynthView::setTrack (int track)
{
    if (track == track_) return;
    track_ = track;
    for (auto& k : knobs_) k.knob->setParamId (k.idFor (track_));
    repaint();
}

void SynthView::setPage (int page)
{
    page_ = juce::jlimit (0, 2, page);
    for (const auto& [p, c] : pageControls_) c->setVisible (p == page_);
    for (auto& k : knobs_) k.knob->setVisible (k.page == page_);
    for (int i = 0; i < tabButtons_.size(); ++i) tabButtons_[i]->setToggleState (i == page_, juce::dontSendNotification);
    resized();
    repaint();
}

void SynthView::emit()
{
    if (onChange) onChange (d_);
}

// Un changement discret (choix, bascule, pas) : un geste complet, puis les commandes se remettent à jour (grisages).
void SynthView::gesture (const std::function<void()>& change)
{
    if (onGestureBegin) onGestureBegin();
    change();
    emit();
    if (onGestureEnd) onGestureEnd();
    refreshControls();
}

void SynthView::setDescription (const SynthDescription& d)
{
    if (dragging_ || d == d_) return;
    d_ = d;
    refreshControls();
}

void SynthView::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (host_, id, terminal, palette_, tr_,
                                          [safe = juce::Component::SafePointer<SynthView> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    addAndMakeVisible (*modal_);
    modal_->setBounds (getLocalBounds());
    modal_->toFront (true);
}

void SynthView::closeModal()
{
    if (! modal_) return;
    host_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

void SynthView::openExplorer (int osc)
{
    auto explorer = std::make_unique<WaveExplorer> (palette_, tr_, d_.osc[static_cast<std::size_t> (osc)].wave,
        [safe = juce::Component::SafePointer<SynthView> (this), osc] (int wave)
        {
            if (safe == nullptr) return;
            safe->gesture ([safe, osc, wave] { safe->d_.osc[static_cast<std::size_t> (osc)].wave = wave; });
        });
    juce::CallOutBox::launchAsynchronously (std::move (explorer), panels_[osc]->wave.getScreenBounds(), nullptr);
}

void SynthView::refreshControls()
{
    updating_ = true;
    auto& x = *x_;
    for (int o = 0; o < numOscillators; ++o)
    {
        auto& p = *panels_[o];
        const auto& od = d_.osc[static_cast<std::size_t> (o)];
        p.wave.setWave (od.wave);
        p.octave.setValue (od.octave, juce::dontSendNotification);
        p.semitone.setValue (od.semitone, juce::dontSendNotification);
        p.unison.setValue (od.unison, juce::dontSendNotification);
        p.spread.setValue (od.spread * 100.0, juce::dontSendNotification);
        p.phase.setValue (od.startPhase * 360.0, juce::dontSendNotification);
        p.pan.setValue (d_.mix[static_cast<std::size_t> (o)].pan * 100.0, juce::dontSendNotification);
        p.curve.setSelectedId (od.curve == DetuneCurve::Supersaw ? 2 : 1, juce::dontSendNotification);
        p.retrigger.setToggleState (od.retrigger, juce::dontSendNotification);
        p.sync.setToggleState (od.sync, juce::dontSendNotification);
        p.mute.setToggleState (d_.mix[static_cast<std::size_t> (o)].mute, juce::dontSendNotification);
        // grisages
        const bool pulse = od.wave == kWavePulse;
        const bool uni = od.unison > 1;
        p.pulse->setEnabled (pulse);
        p.pulse->setAlpha (pulse ? 1.0f : 0.35f);
        p.detune->setEnabled (uni);
        p.detune->setAlpha (uni ? 1.0f : 0.35f);
        p.spread.setEnabled (uni);
        p.curve.setEnabled (uni);
        p.phase.setEnabled (od.retrigger);
    }
    x.subWave.setSelectedId (d_.subWave == SubWave::Triangle ? 2 : d_.subWave == SubWave::Square ? 3 : 1, juce::dontSendNotification);
    x.subOctave.setSelectedId (d_.subOctave <= -2 ? 2 : 1, juce::dontSendNotification);
    x.noiseType.setSelectedId (d_.noise == NoiseType::Pink ? 2 : 1, juce::dontSendNotification);
    x.subMute.setToggleState (d_.mix[kSourceSub].mute, juce::dontSendNotification);
    x.noiseMute.setToggleState (d_.mix[kSourceNoise].mute, juce::dontSendNotification);
    x.subPan.setValue (d_.mix[kSourceSub].pan * 100.0, juce::dontSendNotification);
    x.noisePan.setValue (d_.mix[kSourceNoise].pan * 100.0, juce::dontSendNotification);

    x.routing.setSelectedId (d_.filters.parallel ? 2 : 1, juce::dontSendNotification);
    x.balance.setValue (d_.filters.balance * 100.0, juce::dontSendNotification);
    x.balance.setEnabled (d_.filters.parallel);
    for (std::size_t f = 0; f < 2; ++f)
    {
        const auto& s = d_.filters.filter[f];
        x.fOn[f].setToggleState (s.on, juce::dontSendNotification);
        x.fModel[f].setSelectedId (s.model == FilterModel::Ladder ? 2 : 1, juce::dontSendNotification);
        x.fMode[f].setSelectedId (s.mode == FilterMode::HighPass ? 2 : s.mode == FilterMode::BandPass ? 3 : s.mode == FilterMode::Notch ? 4 : 1,
                                  juce::dontSendNotification);
        x.fSlope[f].setSelectedId (s.slopeDb >= 24 ? 2 : 1, juce::dontSendNotification);
        x.fKey[f].setValue (s.keyTrack * 100.0, juce::dontSendNotification);
        x.fVel[f].setValue (s.velocityOctaves, juce::dontSendNotification);
        x.fDepth[f].setValue (s.envDepthOctaves, juce::dontSendNotification);
        x.fDrive[f].setValue (s.drive * 100.0, juce::dontSendNotification);
        x.fEnv[f]->setEnvelope (s.env);
        for (auto* c : std::initializer_list<juce::Component*> { &x.fModel[f], &x.fSlope[f], &x.fKey[f], &x.fVel[f], &x.fDepth[f], x.fEnv[f].get() })
            c->setEnabled (s.on);
        x.fMode[f].setEnabled (s.on && s.model == FilterModel::SVF);
        x.fDrive[f].setEnabled (s.on && s.model == FilterModel::Ladder);
    }
    x.ampEnv->setEnvelope (d_.ampEnv);
    x.pitchEnv->setEnvelope (d_.pitchEnv);
    x.polyMode.setSelectedId (static_cast<int> (d_.polyphony) + 1, juce::dontSendNotification);
    x.steal.setSelectedId (static_cast<int> (d_.stealMode) + 1, juce::dontSendNotification);
    x.voices.setValue (d_.maxVoices, juce::dontSendNotification);
    x.glide.setValue (d_.glideMs, juce::dontSendNotification);
    const bool poly = d_.polyphony == PolyphonyMode::Poly;
    x.voices.setEnabled (poly);
    x.steal.setEnabled (poly);
    x.glide.setEnabled (d_.polyphony == PolyphonyMode::Legato);
    updating_ = false;
    repaint();
}

void SynthView::resized()
{
    auto& x = *x_;
    auto r = getLocalBounds();
    {
        auto tabs = r.removeFromTop (34);
        keyboardButton_.setBounds (tabs.removeFromRight (190).reduced (2));
        for (auto* b : tabButtons_) b->setBounds (tabs.removeFromLeft (230).reduced (2));
    }
    r.removeFromTop (4);
    for (auto* c : std::initializer_list<juce::Component*> { &keyboard_, &octDown_, &octUp_, &hold_, &keysHint_ })
        c->setVisible (keyboardShown_);
    auto bottom = r.removeFromBottom (keyboardShown_ ? 136 : 0); // clavier : 25 % plus bas qu'avant
    if (keyboardShown_)
    {
        auto line = bottom.removeFromTop (32);
        octDown_.setBounds (line.removeFromLeft (80).reduced (2));
        octUp_.setBounds (line.removeFromLeft (80).reduced (2));
        hold_.setBounds (line.removeFromLeft (120).reduced (2));
        keysHint_.setBounds (line);
        keyboard_.setBounds (bottom.reduced (0, 2));
    }
    const int knobSide = 50;
    auto placeKnob = [&] (NeptuneKnob* k, juce::Rectangle<int> cell)
    {
        if (k == nullptr) return;
        k->setBounds (cell.getCentreX() - knobSide / 2, cell.getY() + 16, knobSide, knobSide);
    };
    auto knobCaption = [&] (int page, const char* key, int index)
    {
        int n = 0;
        for (auto& k : knobs_)
            if (k.page == page && std::string (k.labelKey) == key && n++ == index) return k.knob.get();
        return static_cast<NeptuneKnob*> (nullptr);
    };
    (void) knobCaption;

    // ---- page 0 : trois oscillateurs, puis sous-oscillateur et bruit
    {
        auto page = r;
        const int subH = 112;
        auto oscRow = page.removeFromTop (page.getHeight() - subH - 6);
        const int w = oscRow.getWidth() / 3;
        for (int o = 0; o < numOscillators; ++o)
        {
            auto& p = *panels_[o];
            p.area = oscRow.withX (oscRow.getX() + o * w).withWidth (w).reduced (3);
            auto a = p.area.reduced (8, 6);
            auto head = a.removeFromTop (24);
            p.mute.setBounds (head.removeFromRight (70));
            a.removeFromTop (6);
            auto row = a.removeFromTop (54);
            p.wave.setBounds (row.removeFromLeft (row.getWidth() * 5 / 12).reduced (0, 2));
            row.removeFromLeft (6);
            auto oct = row.removeFromLeft (row.getWidth() / 2);
            p.octave.setBounds (oct.withTrimmedTop (18).reduced (2, 0));
            p.semitone.setBounds (row.withTrimmedTop (18).reduced (2, 0));
            a.removeFromTop (6);
            auto knobs = a.removeFromTop (88);
            const int kw = knobs.getWidth() / 4;
            placeKnob (p.level, knobs.removeFromLeft (kw));
            placeKnob (p.fine, knobs.removeFromLeft (kw));
            placeKnob (p.pulse, knobs.removeFromLeft (kw));
            placeKnob (p.detune, knobs.removeFromLeft (kw));
            auto u = a.removeFromTop (44);
            p.unison.setBounds (u.removeFromLeft (u.getWidth() / 3).withTrimmedTop (16).reduced (2, 0));
            p.spread.setBounds (u.removeFromLeft (u.getWidth() / 2).withTrimmedTop (16).reduced (2, 0));
            p.curve.setBounds (u.withTrimmedTop (16).reduced (2, 0));
            auto t = a.removeFromTop (44);
            p.retrigger.setBounds (t.removeFromLeft (t.getWidth() / 4).withTrimmedTop (14));
            p.phase.setBounds (t.removeFromLeft (t.getWidth() / 3).withTrimmedTop (16).reduced (2, 0));
            if (o > 0) p.sync.setBounds (t.removeFromLeft (t.getWidth() / 2).withTrimmedTop (14));
            p.pan.setBounds (t.withTrimmedTop (16).reduced (2, 0));
        }
        page.removeFromTop (6);
        x.subArea = page.removeFromLeft (page.getWidth() / 2).reduced (3);
        x.noiseArea = page.reduced (3);
        {
            auto a = x.subArea.reduced (8, 6);
            a.removeFromTop (22);
            placeKnob (x.subLevel, a.removeFromLeft (80));
            auto col = a.removeFromLeft (150);
            x.subWave.setBounds (col.removeFromTop (30).reduced (2));
            col.removeFromTop (4);
            x.subOctave.setBounds (col.removeFromTop (30).withTrimmedTop (0).reduced (2));
            x.subMute.setBounds (a.removeFromTop (28).removeFromLeft (80));
            x.subPan.setBounds (a.removeFromTop (44).withTrimmedTop (14).reduced (2, 0));
        }
        {
            auto a = x.noiseArea.reduced (8, 6);
            a.removeFromTop (22);
            placeKnob (x.noiseLevel, a.removeFromLeft (80));
            auto col = a.removeFromLeft (150);
            x.noiseType.setBounds (col.removeFromTop (30).reduced (2));
            x.noiseMute.setBounds (a.removeFromTop (28).removeFromLeft (80));
            x.noisePan.setBounds (a.removeFromTop (44).withTrimmedTop (14).reduced (2, 0));
        }
    }

    // ---- page 1 : routage et deux filtres
    {
        auto page = r;
        auto top = page.removeFromTop (44);
        x.routing.setBounds (top.removeFromLeft (220).withTrimmedTop (16).reduced (2, 0));
        top.removeFromLeft (16);
        x.balance.setBounds (top.removeFromLeft (320).withTrimmedTop (16).reduced (2, 0));
        const int w = page.getWidth() / 2;
        std::size_t knobIdx = 0;
        // les potards de la page 1 sont créés dans l'ordre : coupure 1, résonance 1, coupure 2, résonance 2
        std::vector<NeptuneKnob*> fk;
        for (auto& k : knobs_) if (k.page == 1) fk.push_back (k.knob.get());
        for (int f = 0; f < 2; ++f)
        {
            const auto fi = static_cast<std::size_t> (f);
            x.fArea[fi] = page.withX (page.getX() + f * w).withWidth (w).reduced (3);
            auto a = x.fArea[fi].reduced (8, 6);
            auto head = a.removeFromTop (28);
            head.removeFromLeft (110);
            x.fOn[fi].setBounds (head.removeFromLeft (80));
            auto combos = a.removeFromTop (46);
            const int cw = combos.getWidth() / 3;
            x.fModel[fi].setBounds (combos.removeFromLeft (cw).withTrimmedTop (16).reduced (2, 0));
            x.fMode[fi].setBounds (combos.removeFromLeft (cw).withTrimmedTop (16).reduced (2, 0));
            x.fSlope[fi].setBounds (combos.withTrimmedTop (16).reduced (2, 0));
            auto mid = a.removeFromTop (92);
            auto kcell = mid.removeFromLeft (160);
            if (knobIdx + 1 < fk.size() + 1)
            {
                placeKnob (fk[knobIdx++], kcell.removeFromLeft (80));
                placeKnob (fk[knobIdx++], kcell);
            }
            // suivi du clavier, vélocité, profondeur d'enveloppe, drive : quatre potards sur la rangée des coupures
            const int dw = mid.getWidth() / 4;
            for (auto* dial : { &x.fKey[fi], &x.fVel[fi], &x.fDepth[fi], &x.fDrive[fi] })
            {
                // même taille que les autres potards : le disque fait knobSide, la valeur s'écrit dessous
                const auto cell = mid.removeFromLeft (dw);
                dial->setBounds (cell.getCentreX() - 35, cell.getY() + 16, 70, knobSide + 16);
            }
            x.fEnv[fi]->setBounds (a.withTrimmedTop (4));
        }
    }

    // ---- page 2 : enveloppes de volume et de pitch, voix
    {
        auto page = r;
        x.voicesArea = page.removeFromBottom (64).reduced (3);
        const int w = page.getWidth() / 2;
        x.ampArea = page.withWidth (w).reduced (3);
        x.pitchArea = page.withX (page.getX() + w).withWidth (w).reduced (3);
        x.ampEnv->setBounds (x.ampArea.reduced (8, 6).withTrimmedTop (24));
        auto pa = x.pitchArea.reduced (8, 6);
        pa.removeFromTop (24);
        placeKnob (x.pitchDepth, pa.removeFromTop (84).removeFromLeft (90));
        x.pitchEnv->setBounds (pa);
        auto v = x.voicesArea.reduced (8, 4);
        const int vw = v.getWidth() / 4;
        x.polyMode.setBounds (v.removeFromLeft (vw).withTrimmedTop (18).reduced (2, 0));
        x.voices.setBounds (v.removeFromLeft (vw).withTrimmedTop (18).reduced (2, 0));
        x.steal.setBounds (v.removeFromLeft (vw).withTrimmedTop (18).reduced (2, 0));
        x.glide.setBounds (v.withTrimmedTop (18).reduced (2, 0));
    }
    if (modal_) modal_->setBounds (getLocalBounds());
}

void SynthView::paint (juce::Graphics& g)
{
    auto& x = *x_;
    auto card = [&] (juce::Rectangle<int> a)
    {
        g.setColour (colour (Token::BgCard));
        g.fillRoundedRectangle (a.toFloat(), 6.0f);
        g.setColour (colour (Token::Border));
        g.drawRoundedRectangle (a.toFloat(), 6.0f, 1.0f);
    };
    auto title = [&] (juce::Rectangle<int> a, const juce::String& t)
    {
        g.setColour (colour (Token::Text));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (t, a.getX() + 10, a.getY() + 4, a.getWidth() - 20, 22, juce::Justification::centredLeft);
    };
    if (page_ == 0)
    {
        for (int o = 0; o < numOscillators; ++o)
        {
            card (panels_[o]->area);
            title (panels_[o]->area, juce::String::fromUTF8 (tr_.format ("synth.osc", { std::to_string (o + 1) }).c_str()));
        }
        card (x.subArea);
        title (x.subArea, text ("synth.sub"));
        card (x.noiseArea);
        title (x.noiseArea, text ("synth.noise"));
    }
    else if (page_ == 1)
    {
        for (int f = 0; f < 2; ++f)
        {
            card (x.fArea[static_cast<std::size_t> (f)]);
            title (x.fArea[static_cast<std::size_t> (f)], text (f == 0 ? "filt.filter1" : "filt.filter2"));
        }
    }
    else
    {
        card (x.ampArea);
        title (x.ampArea, text ("synth.amp_env"));
        card (x.pitchArea);
        title (x.pitchArea, text ("synth.pitch_env"));
        card (x.voicesArea);
    }

    // légendes des sélecteurs et curseurs
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (colour (Token::TextDim));
    for (const auto& [c, caption] : x.captions)
    {
        if (! c->isVisible() || c->getParentComponent() != this) continue;
        const auto b = c->getBounds();
        g.setColour (colour (Token::TextDim).withMultipliedAlpha (c->isEnabled() ? 1.0f : 0.4f));
        g.drawText (caption, b.getX(), b.getY() - 14, b.getWidth(), 13, juce::Justification::centredLeft);
    }
    // légendes et valeurs des potards
    for (const auto& k : knobs_)
    {
        if (k.page != page_) continue;
        const auto b = k.knob->getBounds();
        const float a = k.knob->isEnabled() ? 1.0f : 0.35f;
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (colour (Token::TextDim).withMultipliedAlpha (a));
        g.drawText (text (k.labelKey), b.getX() - 14, b.getY() - 15, b.getWidth() + 28, 14, juce::Justification::centred);
        g.setColour (colour (Token::Accent).withMultipliedAlpha (a));
        g.drawText (juce::String::fromUTF8 (host_.parameters().toText (k.idFor (track_)).c_str()), b.getX() - 14, b.getBottom() + 1, b.getWidth() + 28, 14,
                    juce::Justification::centred);
    }
}
