#include "NovaCompressorEditor.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/CompressorPresets.h"
#include "neptune/core/Oversampler.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace cm = neptune::core::compressor;

namespace
{
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // barres
    {
        for (const float x : { 4.0f, 12.0f, 20.0f })
        {
            p.startNewSubPath (x, 20.0f);
            p.lineTo (x, x == 4.0f ? 10.0f : x == 12.0f ? 4.0f : 13.0f);
        }
    }
    else if (kind == 1)   // chemin de la clé
    {
        p.startNewSubPath (6.0f, 3.0f);
        p.lineTo (6.0f, 9.0f);
        p.quadraticTo (6.0f, 12.0f, 9.0f, 12.0f);
        p.lineTo (15.0f, 12.0f);
        p.quadraticTo (18.0f, 12.0f, 18.0f, 15.0f);
        p.lineTo (18.0f, 21.0f);
        p.addEllipse (4.4f, 1.4f, 3.2f, 3.2f);
        p.addEllipse (16.4f, 19.4f, 3.2f, 3.2f);
    }
    else   // réglages
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    return p;
}
} // namespace

// ------------------------------------------------------------------------------------------------ courbe de transfert
class NovaCompressorEditor::CurveView : public juce::Component
{
public:
    CurveView (const nova::Skin& s, ParamHost& h, const CompressorModule* m) : skin_ (s), host_ (h), module_ (m) {}

    static constexpr double kLo = -60.0, kHi = 12.0;

    // Appelé à 30 Hz : ne redessine que si la courbe ou le point de fonctionnement ont changé
    void update (float inDb, float grDb)
    {
        std::uint32_t key = 0;
        for (const auto id : { cm::direction, cm::threshold, cm::ratio, cm::kneeWidth, cm::curve, cm::floorDb, cm::style }) key = key * 31u + host_.parameters().version (id);
        const float qIn = std::round (inDb * 2.0f) * 0.5f, qGr = std::round (grDb * 2.0f) * 0.5f;
        if (key == key_ && qIn == in_ && qGr == gr_) return;
        key_ = key; in_ = qIn; gr_ = qGr;
        pathValid_ = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
        const auto plot = plotArea();
        // grille tous les 12 dB
        g.setFont (skin_.font (9.0f));
        for (int d = -60; d <= 12; d += 12)
        {
            g.setColour (skin_.colour (Token::Grid, 0.55f));
            g.drawVerticalLine (juce::roundToInt (xOf (d)), plot.getY(), plot.getBottom());
            g.drawHorizontalLine (juce::roundToInt (yOf (d)), plot.getX(), plot.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (d), juce::Rectangle<float> (xOf (d) - skin_.px (14.0f), plot.getBottom() + skin_.px (3.0f), skin_.px (28.0f), skin_.px (12.0f)), juce::Justification::centred);
            g.drawText (juce::String (d), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), plot.getX() - b.getX() - skin_.px (6.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        g.setColour (skin_.colour (Token::TextDim2));
        g.drawText (tx_in_, juce::Rectangle<float> (plot.getRight() - skin_.px (60.0f), plot.getBottom() - skin_.px (14.0f), skin_.px (58.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        g.drawText (tx_out_, juce::Rectangle<float> (plot.getX() + skin_.px (4.0f), plot.getY() + skin_.px (2.0f), skin_.px (60.0f), skin_.px (12.0f)), juce::Justification::centredLeft);

        // diagonale : aucun traitement
        {
            juce::Path unity;
            unity.startNewSubPath (xOf (kLo), yOf (kLo));
            unity.lineTo (xOf (kHi), yOf (kHi));
            juce::Path dashed;
            const float dash[] = { skin_.px (4.0f), skin_.px (4.0f) };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dash, 2);
            g.setColour (skin_.colour (Token::TextDim2, 0.6f));
            g.fillPath (dashed);
        }

        if (module_ == nullptr) return;
        const double thr = host_.parameters().get (cm::threshold);
        // zone au-delà du seuil
        const float tx = xOf (juce::jlimit (kLo, kHi, thr));
        g.setColour (skin_.colour (Token::Accent, 0.06f));
        g.fillRect (juce::Rectangle<float> (tx, plot.getY(), plot.getRight() - tx, plot.getHeight()));
        {
            juce::Path line;
            line.startNewSubPath (tx, plot.getY());
            line.lineTo (tx, plot.getBottom());
            juce::Path dashed;
            const float dash[] = { skin_.px (3.0f), skin_.px (4.0f) };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dash, 2);
            g.setColour (skin_.colour (Token::Accent, 0.55f));
            g.fillPath (dashed);
        }
        if (! pathValid_) rebuild();
        g.setColour (skin_.colour (Token::Accent));
        g.strokePath (path_, juce::PathStrokeType (skin_.px (2.5f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // point de fonctionnement en direct
        if (in_ > static_cast<float> (kLo))
        {
            const float outDb = in_ + gr_;
            const float cx = xOf (juce::jlimit (kLo, kHi, static_cast<double> (in_))), cy = yOf (juce::jlimit (kLo, kHi, static_cast<double> (outDb)));
            const float r = skin_.px (4.0f);
            g.setColour (skin_.colour (Token::Pink, 0.35f));
            g.fillEllipse (cx - r * 2, cy - r * 2, r * 4, r * 4);
            g.setColour (skin_.colour (Token::Pink2));
            g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
        }
        // poignée du seuil
        const auto h = handle();
        const float hr = skin_.px (6.0f);
        g.setColour (skin_.colour (Token::Hring));
        g.fillEllipse (h.x - hr - 2.0f, h.y - hr - 2.0f, hr * 2 + 4.0f, hr * 2 + 4.0f);
        g.setColour (skin_.colour (Token::Handle));
        g.fillEllipse (h.x - hr, h.y - hr, hr * 2, hr * 2);
        g.setColour (skin_.colour (Token::Accent));
        g.drawEllipse (h.x - hr, h.y - hr, hr * 2, hr * 2, skin_.px (1.5f));
    }

    void resized() override { pathValid_ = false; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (nearHandle (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! nearHandle (e.position)) return;
        dragging_ = true;
        host_.uiBeginEdit (cm::threshold);
        apply (e.position.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging_) apply (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging_) host_.uiEndEdit (cm::threshold);
        dragging_ = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (nearHandle (e.position)) host_.uiResetToDefault (cm::threshold); }

    void setAxisLabels (juce::String in, juce::String out) { tx_in_ = std::move (in); tx_out_ = std::move (out); }

private:
    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (34.0f), b.getY() + skin_.px (10.0f), b.getWidth() - skin_.px (34.0f + 14.0f), b.getHeight() - skin_.px (10.0f + 22.0f) };
    }
    float xOf (double db) const { const auto p = plotArea(); return p.getX() + static_cast<float> ((db - kLo) / (kHi - kLo)) * p.getWidth(); }
    float yOf (double db) const { const auto p = plotArea(); return p.getBottom() - static_cast<float> ((db - kLo) / (kHi - kLo)) * p.getHeight(); }
    double dbOfX (float x) const { const auto p = plotArea(); return kLo + static_cast<double> ((x - p.getX()) / p.getWidth()) * (kHi - kLo); }
    juce::Point<float> handle() const
    {
        const double thr = juce::jlimit (kLo, kHi, host_.parameters().get (cm::threshold));
        const double out = module_ != nullptr ? module_->curveOutputDb (host_.parameters(), thr) : thr;
        return { xOf (thr), yOf (juce::jlimit (kLo, kHi, out)) };
    }
    bool nearHandle (juce::Point<float> p) const { return handle().getDistanceFrom (p) < skin_.px (14.0f); }
    void apply (float x)
    {
        const auto* spec = host_.parameters().find (cm::threshold);
        if (spec == nullptr) return;
        host_.uiSetNormalised (cm::threshold, spec->toNormalized (juce::jlimit (kLo, 0.0, dbOfX (x))));
        repaint();
    }
    void rebuild()
    {
        path_.clear();
        for (int i = 0; i <= 144; ++i)
        {
            const double in = kLo + (kHi - kLo) * i / 144.0;
            const double out = juce::jlimit (kLo - 6.0, kHi + 6.0, module_->curveOutputDb (host_.parameters(), in));
            const float x = xOf (in), y = yOf (out);
            if (i == 0) path_.startNewSubPath (x, y); else path_.lineTo (x, y);
        }
        pathValid_ = true;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    const CompressorModule* module_;
    juce::Path path_;
    bool pathValid_ = false, dragging_ = false;
    std::uint32_t key_ = 0xFFFFFFFFu;
    float in_ = -999.0f, gr_ = 0.0f;
    juce::String tx_in_ = "IN dB", tx_out_ = "OUT dB";
};

// ------------------------------------------------------------------------------------------------ réponse des filtres de la clé
class NovaCompressorEditor::SideEqView : public juce::Component
{
public:
    SideEqView (const nova::Skin& s, ParamHost& h, std::function<double()> rate) : skin_ (s), host_ (h), rate_ (std::move (rate)) {}

    void update()
    {
        std::uint32_t key = 0;
        for (int b = 0; b < cm::kBands; ++b)
            for (int k = 0; k < 6; ++k) key = key * 31u + host_.parameters().version (cm::bandParam (b, k));
        if (key == key_) return;
        key_ = key;
        valid_ = false;
        repaint();
    }

    void resized() override { valid_ = false; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
        const auto plot = plotArea();
        g.setFont (skin_.font (9.0f));
        for (const double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            g.setColour (skin_.colour (Token::Grid, 0.5f));
            g.drawVerticalLine (juce::roundToInt (xOf (f)), plot.getY(), plot.getBottom());
            if (f == 100.0 || f == 1000.0 || f == 10000.0 || f == 20.0 || f == 20000.0)
            {
                g.setColour (skin_.colour (Token::TextDim2));
                g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String (f, 0),
                            juce::Rectangle<float> (xOf (f) - skin_.px (18.0f), plot.getBottom() + skin_.px (3.0f), skin_.px (36.0f), skin_.px (12.0f)), juce::Justification::centred);
            }
        }
        for (int d = -24; d <= 24; d += 12)
        {
            g.setColour (skin_.colour (Token::Grid, d == 0 ? 0.9f : 0.5f));
            g.drawHorizontalLine (juce::roundToInt (yOf (d)), plot.getX(), plot.getRight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (d), juce::Rectangle<float> (b.getX() + skin_.px (2.0f), yOf (d) - skin_.px (6.0f), plot.getX() - b.getX() - skin_.px (6.0f), skin_.px (12.0f)), juce::Justification::centredRight);
        }
        if (! valid_) rebuild();
        if (anyOn_)
        {
            juce::Path filled (total_);
            filled.lineTo (plot.getRight(), yOf (0.0));
            filled.lineTo (plot.getX(), yOf (0.0));
            filled.closeSubPath();
            g.setColour (skin_.colour (Token::Accent, 0.12f));
            g.fillPath (filled);
            g.setColour (skin_.colour (Token::Accent));
            g.strokePath (total_, juce::PathStrokeType (skin_.px (2.2f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // poignées des filtres en service
        for (int i = 0; i < cm::kBands; ++i)
        {
            if (host_.parameters().get (cm::bandParam (i, cm::kStatus)) < 1.5) continue;
            const double f = host_.parameters().get (cm::bandParam (i, cm::kFreq));
            const float cx = xOf (f), cy = yOf (juce::jlimit (-24.0, 24.0, CompressorModule::sideResponseDb (host_.parameters(), f, rate_())));
            const float r = skin_.px (5.0f);
            g.setColour (skin_.colour (colourFor (i)));
            g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
            g.setColour (skin_.colour (Token::StageBg));
            g.drawEllipse (cx - r, cy - r, r * 2, r * 2, skin_.px (1.2f));
        }
    }

private:
    static Token colourFor (int band) { return band == 1 ? Token::Band1 : band == 2 ? Token::Band2 : band == 3 ? Token::Band3 : Token::Accent; }
    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (34.0f), b.getY() + skin_.px (10.0f), b.getWidth() - skin_.px (34.0f + 14.0f), b.getHeight() - skin_.px (10.0f + 22.0f) };
    }
    float xOf (double f) const { const auto p = plotArea(); return p.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * p.getWidth(); }
    float yOf (double db) const { const auto p = plotArea(); return p.getCentreY() - static_cast<float> (db / 24.0) * p.getHeight() * 0.5f; }
    void rebuild()
    {
        total_.clear();
        anyOn_ = false;
        for (int i = 0; i < cm::kBands; ++i) anyOn_ = anyOn_ || host_.parameters().get (cm::bandParam (i, cm::kStatus)) > 1.5;
        const double fs = rate_();
        const auto plot = plotArea();
        const int n = juce::jmax (2, static_cast<int> (plot.getWidth() / 3.0f));
        for (int i = 0; i <= n; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, static_cast<double> (i) / n);
            const double db = juce::jlimit (-24.0, 24.0, CompressorModule::sideResponseDb (host_.parameters(), f, fs));
            if (i == 0) total_.startNewSubPath (xOf (f), yOf (db)); else total_.lineTo (xOf (f), yOf (db));
        }
        valid_ = true;
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    std::function<double()> rate_;
    juce::Path total_;
    bool valid_ = false, anyOn_ = false;
    std::uint32_t key_ = 0xFFFFFFFFu;
};

// ------------------------------------------------------------------------------------------------ zone de filtres de la clé
struct NovaCompressorEditor::Zone
{
    int band = 0;                                   // indice du filtre de la clé (0..7)
    int fixedType = cm::kPeak;                      // type imposé quand la zone est activée
    nova::Panel* card = nullptr;
    nova::Switch* sw = nullptr;
    std::vector<nova::KnobCard*> knobs;
    nova::Pill* slope = nullptr;
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaCompressorEditor::NovaCompressorEditor (ParamHost& host, const CompressorModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                            const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.compressor")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("nv.page_compressor"), iconPath (0));
    addPage (tx ("nv.page_sidechain"), iconPath (1));
    addPage (tx ("nv.page_advanced"), iconPath (2));
    buildCompressorPage();
    buildSidechainPage();
    buildAdvancedPage();
    buildBottomBar();
    if (const auto names = presetNames(); ! names.empty()) presetPill()->setText (names[1]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaCompressorEditor::~NovaCompressorEditor() = default;

std::vector<juce::String> NovaCompressorEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : cm::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}

std::vector<std::pair<ParamId, double>> NovaCompressorEditor::presetValues (int i) const
{
    return cm::factoryPresets()[static_cast<std::size_t> (i)].values;
}

std::vector<ParamId> NovaCompressorEditor::presetParamIds() const { return cm::presetParams(); }

void NovaCompressorEditor::buildCompressorPage()
{
    metersPanel_ = add<nova::Panel> (-1, skin());
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    curvePanel_ = add<nova::Panel> (0, skin());
    curvePanel_->setTitle (tx ("nv.curve_title"));
    curve_ = add<CurveView> (0, skin(), host(), module_);
    curve_->setAxisLabels (tx ("nv.in_db"), tx ("nv.out_db"));
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    grMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.gr"), nova::VMeter::Kind::Reduction, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, grMeter_, outMeter_ }) m->setModeVisible (false);

    struct Spec { ParamId id; const char* title; bool pink; ParamId sub; const char* subTitle; };
    const Spec specs[6] = { { cm::threshold, "nv.threshold", false, cm::kneeWidth, "nv.knee" }, { cm::ratio, "nv.ratio", false, cm::curve, "nv.curve" },
                            { cm::attack, "nv.attack", false, cm::pump, "nv.pump" }, { cm::release, "nv.release", false, cm::smooth, "nv.smooth" },
                            { cm::makeup, "nv.makeup", true, cm::clipperDrive, "nv.clip" }, { cm::wet, "nv.mix", true, 0, nullptr } };
    for (const auto& s : specs)
    {
        Column c {};
        c.card = add<nova::Panel> (0, skin());
        c.knob = add<nova::KnobCard> (0, skin(), host(), s.id, tx (s.title), modalOpener(), s.pink);
        c.knob->setPlain (true);
        if (const auto* spec = host().parameters().find (s.id))
            c.knob->setRangeLabels (juce::String::fromUTF8 (host().parameters().toText (*spec, spec->min).c_str()),
                                    juce::String::fromUTF8 (host().parameters().toText (*spec, spec->max).c_str()));
        if (s.sub != 0)
        {
            c.sub = add<nova::SubSlider> (0, skin(), host(), s.sub, tx (s.subTitle), s.pink);
            const char* key = s.sub == cm::kneeWidth ? "hlp.knee" : s.sub == cm::curve ? "hlp.curve" : s.sub == cm::pump ? "hlp.pump" : s.sub == cm::smooth ? "hlp.smooth" : "hlp.clip";
            addInfo (0, c.sub, key, true);
        }
        else addInfo (0, c.card, "hlp.mix", true);
        columns_.push_back (c);
    }
}

void NovaCompressorEditor::buildSidechainPage()
{
    scPanel_ = add<nova::Panel> (1, skin());
    scPanel_->setTitle (tx ("nv.sc_title"));
    sourcePill_ = add<nova::Pill> (1, skin());
    sourcePill_->bindChoice (host(), cm::externalSide, tx ("nv.source"), { tx ("nv.src_internal"), tx ("nv.src_external") });
    listenPill_ = add<nova::Pill> (1, skin());
    listenPill_->bindToggle (host(), cm::sideOut, tx ("nv.listen"));
    eqPill_ = add<nova::Pill> (1, skin());
    eqPill_->bindChoice (host(), cm::sideEqBypass, {}, { tx ("nv.eq_on"), tx ("nv.eq_off") });
    rmsPill_ = add<nova::Pill> (1, skin());
    rmsPill_->bindToggle (host(), cm::rmsOn, tx ("nv.rms_on"));
    keyGain_ = add<nova::SubSlider> (1, skin(), host(), cm::sideGain, tx ("nv.side_gain"));
    rmsLength_ = add<nova::SubSlider> (1, skin(), host(), cm::rmsLength, tx ("nv.rms_length"));
    addInfo (1, sourcePill_, "hlp.source");
    addInfo (1, listenPill_, "hlp.listen");
    addInfo (1, eqPill_, "hlp.eqswitch");
    addInfo (1, rmsPill_, "hlp.rmsswitch");
    addInfo (1, keyGain_, "hlp.keygain", true);
    addInfo (1, rmsLength_, "hlp.rmslength", true);
    sideEq_ = add<SideEqView> (1, skin(), host(), [this] { const double r = options().sampleRate ? options().sampleRate() : 48000.0; return r > 0.0 ? r : 48000.0; });

    // cinq zones : passe-haut (filtre 1), trois cloches (filtres 2 à 4), passe-bas (filtre 5) ; les trois autres filtres restent accessibles à l'hôte
    struct ZSpec { int band; int type; const char* title; int knobs; };   // knobs : 0 freq seul (+ pente), 1 gain / freq / Q
    const ZSpec zs[5] = { { 0, cm::kHighPass, "nv.zone_hp", 0 }, { 1, cm::kPeak, "nv.zone_band", 1 }, { 2, cm::kPeak, "nv.zone_band", 1 },
                          { 3, cm::kPeak, "nv.zone_band", 1 }, { 4, cm::kLowPass, "nv.zone_lp", 0 } };
    for (const auto& z : zs)
    {
        auto zone = std::make_unique<Zone>();
        zone->band = z.band;
        zone->fixedType = z.type;
        zone->card = add<nova::Panel> (1, skin());
        zone->card->setTitle (tx (z.title) + (z.knobs == 1 ? " " + juce::String (z.band) : juce::String()));
        zone->sw = add<nova::Switch> (1, skin());
        const int band = z.band, type = z.type;
        zone->sw->onChange = [this, band, type] (bool on)
        {
            if (on)
            {
                if (choiceValue (cm::bandParam (band, cm::kType)) != type) nova::setParam (host(), cm::bandParam (band, cm::kType), type);
                nova::setParam (host(), cm::bandParam (band, cm::kStatus), cm::kOn);
            }
            else nova::setParam (host(), cm::bandParam (band, cm::kStatus), cm::kOff);
        };
        auto knobFor = [&] (int k, const char* title)
        {
            auto* kc = add<nova::KnobCard> (1, skin(), host(), cm::bandParam (z.band, k), tx (title), modalOpener(), false);
            kc->setPlain (true);
            kc->setCompact (true);
            zone->knobs.push_back (kc);
        };
        if (z.knobs == 1) { knobFor (cm::kGain, "nv.gain"); knobFor (cm::kFreq, "nv.freq"); knobFor (cm::kQ, "nv.q"); }
        else
        {
            knobFor (cm::kFreq, "nv.freq");
            zone->slope = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
            zone->slope->bindChoice (host(), cm::bandParam (z.band, cm::kOrder), tx ("nv.slope"),
                                     { tx ("nv.slope_6"), tx ("nv.slope_12"), tx ("nv.slope_24"), tx ("nv.slope_48"), tx ("nv.slope_72"), tx ("nv.slope_96") });
        }
        zones_.push_back (std::move (zone));
    }
}

void NovaCompressorEditor::buildAdvancedPage()
{
    advPanel_ = add<nova::Panel> (2, skin());
    advPanel_->setTitle (tx ("nv.adv_title"));
    struct P { ParamId id; const char* key; };
    for (const auto& p : { P { cm::stereoSwap, "nv.swap" }, P { cm::rangeInf, "nv.range_inf" }, P { cm::rmsOn, "nv.rms_on" }, P { cm::delta, "nv.delta" } })
    {
        auto* pill = add<nova::Pill> (2, skin());
        pill->bindToggle (host(), p.id, tx (p.key));
        advPills_.push_back (pill);
        addInfo (2, pill, p.id == cm::stereoSwap ? "hlp.swap" : p.id == cm::rangeInf ? "hlp.rangeinf" : p.id == cm::rmsOn ? "hlp.rmsswitch" : "hlp.delta");
    }
    for (const auto& p : { P { cm::floorDb, "nv.floor" }, P { cm::range, "nv.range" }, P { cm::hold, "nv.hold" }, P { cm::stereoLink, "nv.link" }, P { cm::wet1, "nv.wet_a" },
                           P { cm::wet2, "nv.wet_b" }, P { cm::rmsLength, "nv.rms_length" }, P { cm::rmsSpeed, "nv.rms_speed" }, P { cm::rmsMix, "nv.rms_mix" } })
    {
        auto* kc = add<nova::KnobCard> (2, skin(), host(), p.id, tx (p.key), modalOpener(), false);
        advCards_.push_back (kc);
        const char* hk = p.id == cm::floorDb ? "hlp.floor" : p.id == cm::range ? "hlp.range" : p.id == cm::hold ? "hlp.hold" : p.id == cm::stereoLink ? "hlp.link"
                       : p.id == cm::wet1 || p.id == cm::wet2 ? "hlp.wet" : p.id == cm::rmsLength ? "hlp.rmslength" : p.id == cm::rmsSpeed ? "hlp.rmsspeed" : "hlp.rmsmix";
        addInfo (2, kc, hk, true);
    }
}

void NovaCompressorEditor::buildBottomBar()
{
    stereoPill_ = add<nova::Pill> (-1, skin());
    stereoPill_->bindChoice (host(), cm::stereoMode, tx ("nv.stereo_mode"), { tx ("nv.stereo_ms"), tx ("nv.stereo_lr"), tx ("nv.stereo_msmax"), tx ("nv.stereo_lrmax") });
    stylePill_ = add<nova::Pill> (-1, skin());
    stylePill_->bindChoice (host(), cm::style, tx ("nv.style"), { tx ("nv.style_clean"), tx ("nv.style_classic"), tx ("nv.style_optical"), tx ("nv.style_vocal") });
    dirPill_ = add<nova::Pill> (-1, skin());
    dirPill_->bindChoice (host(), cm::direction, tx ("nv.direction"), { tx ("nv.dir_compress"), tx ("nv.dir_inflate"), tx ("nv.dir_expand"), tx ("nv.dir_shape") });
    deltaPill_ = add<nova::Pill> (-1, skin());
    deltaPill_->bindToggle (host(), cm::delta, tx ("nv.delta"));
    osPill_ = add<nova::Pill> (-1, skin());
    osPill_->bindChoice (host(), cm::oversample, tx ("nv.oversampling"), { tx ("nv.os_off"), tx ("nv.os_2x"), tx ("nv.os_4x") });
    lookPill_ = add<nova::Pill> (-1, skin());
    lookPill_->onClick = [this] { openModal (cm::lookahead, uistate::Terminal::Pointer); };
    addInfo (-1, stereoPill_, "hlp.stereo");
    addInfo (-1, stylePill_, "hlp.style");
    addInfo (-1, dirPill_, "hlp.mode");
    addInfo (-1, deltaPill_, "hlp.delta");
    addInfo (-1, osPill_, "hlp.oversampling");
    addInfo (-1, lookPill_, "hlp.lookahead");
}

void NovaCompressorEditor::layout()
{
    // ---- page COMPRESSOR
    metersPanel_->setBounds (R (14, 72, 176, 410));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 3;
        inMeter_->setBounds (in.withWidth (w));
        grMeter_->setBounds (in.withX (in.getX() + w).withWidth (w));
        outMeter_->setBounds (in.withX (in.getX() + 2 * w).withWidth (w));
    }
    navPanel_->setBounds (R (14, 496, 176, 214));
    for (int i = 0; i < numPages(); ++i) nav (i)->setBounds (R (14 + 14, 496 + 21 + 50.0f * static_cast<float> (i), 148, 44));
    curvePanel_->setBounds (R (204, 72, 942, 410));
    curve_->setBounds (R (220, 100, 910, 366));
    const float colW = (942.0f - 12.0f * 5.0f) / 6.0f;
    for (std::size_t i = 0; i < columns_.size(); ++i)
    {
        const float x = 204.0f + static_cast<float> (i) * (colW + 12.0f);
        columns_[i].card->setBounds (R (x, 496, colW, 214));
        columns_[i].knob->setBounds (R (x, 496, colW, 132));
        if (columns_[i].sub != nullptr) columns_[i].sub->setBounds (R (x + 10, 496 + 140, colW - 20, 40));
    }

    // ---- page SIDECHAIN
    scPanel_->setBounds (R (204, 72, 942, 410));
    sourcePill_->setBounds (R (220, 108, 150, 30));
    listenPill_->setBounds (R (398, 108, 90, 30));
    eqPill_->setBounds (R (516, 108, 84, 30));
    rmsPill_->setBounds (R (628, 108, 60, 30));
    keyGain_->setBounds (R (724, 102, 190, 40));
    rmsLength_->setBounds (R (930, 102, 200, 40));
    sideEq_->setBounds (R (220, 152, 910, 314));
    const float unit = (942.0f - 12.0f * 4.0f) / 13.0f;
    float zx = 204.0f;
    for (auto& z : zones_)
    {
        const float zw = unit * static_cast<float> (z->knobs.size() == 3 ? 3 : 2);
        z->card->setBounds (R (zx, 496, zw, 214));
        z->sw->setBounds (R (zx + zw - 8 - 34, 496 + 10, 34, 19));
        const float inner = zw - 12.0f;
        if (z->knobs.size() == 3)
            for (std::size_t k = 0; k < 3; ++k) z->knobs[k]->setBounds (R (zx + 6 + inner / 3.0f * static_cast<float> (k), 496 + 36, inner / 3.0f, 112));
        else
        {
            z->knobs[0]->setBounds (R (zx + 6, 496 + 36, inner, 112));
            z->slope->setBounds (R (zx + 14, 496 + 160, zw - 28, 26));
        }
        zx += zw + 12.0f;
    }

    // ---- page ADVANCED
    advPanel_->setBounds (R (204, 72, 942, 638));
    for (std::size_t i = 0; i < advPills_.size(); ++i) advPills_[i]->setBounds (R (220 + 150.0f * static_cast<float> (i), 108, 120, 30));
    const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
    for (std::size_t i = 0; i < advCards_.size(); ++i)
    {
        const float x = 220.0f + static_cast<float> (i % 5) * (cw + 12.0f), y = 160.0f + static_cast<float> (i / 5) * 160.0f;
        advCards_[i]->setBounds (R (x, y, cw, 144));
    }

    // ---- barre du bas
    const float by = 724.0f + 12.0f;
    abPill()->setBounds (R (14, 724 + 12, 36, 32));
    float x = 58.0f;
    for (auto* p : { stereoPill_, stylePill_, dirPill_, deltaPill_, osPill_, lookPill_ })
    {
        const float w = p == stylePill_ ? 124.0f : p == dirPill_ ? 134.0f : p == deltaPill_ ? 64.0f : p == osPill_ ? 154.0f : p == lookPill_ ? 150.0f : 112.0f;
        p->setBounds (R (x, by, w, 32));
        x += w + 26.0f;   // place du bouton « ? »
    }

    // ---- en-tête
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaCompressorEditor::tick()
{
    // vu-mètres, courbe, légende
    float inDb = -120.0f, grDb = 0.0f;
    if (module_ != nullptr)
    {
        const float il = dbOf (module_->meter (cm::kInPeakL)), ir = dbOf (module_->meter (cm::kInPeakR));
        const float ol = dbOf (module_->meter (cm::kOutPeakL)), orr = dbOf (module_->meter (cm::kOutPeakR));
        const float gl = std::max (0.0f, -module_->meter (cm::kReductionL)), gr = std::max (0.0f, -module_->meter (cm::kReductionR));
        inMeter_->update (il, ir);
        outMeter_->update (ol, orr);
        grMeter_->update (gl, gr);
        inDb = std::max (il, ir);
        grDb = -std::max (gl, gr);
    }
    curve_->update (inDb, grDb);
    const auto meta = tx ("nv.in") + " " + (inDb < -100.0f ? juce::String ("-inf") : juce::String (inDb, 1)) + " dB   " + tx ("nv.gr") + " " + juce::String (-grDb, 1) + " dB";
    if (meta != metaText_) { metaText_ = meta; curvePanel_->setMeta (meta); }
    sideEq_->update();

    // zones de la clé : interrupteur = filtre en service ; potards estompés quand la zone est inactive
    for (auto& z : zones_)
    {
        const bool on = choiceValue (cm::bandParam (z->band, cm::kStatus)) == cm::kOn;
        z->sw->setOn (on);
        for (auto* k : z->knobs) k->setAlpha (on ? 1.0f : 0.4f);
        if (z->slope != nullptr) z->slope->setAlpha (on ? 1.0f : 0.4f);
    }
    const bool ext = value (cm::externalSide) >= 0.5;
    listenPill_->setAlpha (1.0f);
    sourcePill_->setTintToken (ext ? Token::Pink : Token::Accent);
    (void) ext;

    // anticipation et latence
    const double la = value (cm::lookahead);
    const auto look = tx ("nv.lookahead") + "  " + (la > 0.0 ? "+" : "") + juce::String (la, 1) + " ms";
    lookPill_->setText (look);
    const double rate = options().sampleRate ? options().sampleRate() : 48000.0;
    const int os = choiceValue (cm::oversample);
    const int latency = (la > 0.0 ? static_cast<int> (std::lround (la * 0.001 * rate)) : 0) + Oversampler::latencyFor (os >= 2 ? 4 : os == 1 ? 2 : 1);
    const auto lat = juce::String::fromUTF8 (translator().format ("nv.latency", { std::to_string (latency) }).c_str());
    if (lat != latencyText_) { latencyText_ = lat; repaint (R (920, 724, 226, 56)); }
}

void NovaCompressorEditor::paintBackground (juce::Graphics& g)
{
    g.setColour (skin().colour (Token::TextDim2));
    g.setFont (skin().font (11.0f, true));
    g.drawText (latencyText_, R (930, 724 + 12, 216, 32).toFloat(), juce::Justification::centredRight);
}
