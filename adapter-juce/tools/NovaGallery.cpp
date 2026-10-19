// Galerie des composants NOVA (NovaKit) rendue hors-ligne en PNG, pour comparer avec les maquettes HTML et vérifier l'échelle.
// Usage : NovaGallery <dossier de sortie>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/NovaKit.h"
#include "../src/NovaCompressorEditor.h"
#include "../src/NovaEqualizerEditor.h"
#include "../src/NovaSpectrum.h"
#include "neptune/core/EqualizerModule.h"
#include "neptune/core/CompressorModule.h"
#include "neptune/core/StockModule.h"
#include <cmath>
#include <memory>

using namespace neptune::core;
namespace th = neptune::core::theme;

namespace
{
struct FakeHost : ParamHost
{
    ParameterSet set;
    ParameterSet& parameters() override { return set; }
    void uiBeginEdit (ParamId) override {}
    void uiSetNormalised (ParamId id, double n) override { set.setNormalized (id, n); }
    void uiEndEdit (ParamId) override {}
    void uiResetToDefault (ParamId id) override { set.reset (id); }
};

struct RunnerHost : ParamHost
{
    ModuleRunner& runner;
    explicit RunnerHost (ModuleRunner& r) : runner (r) {}
    ParameterSet& parameters() override { return runner.params(); }
    void uiBeginEdit (ParamId) override {}
    void uiSetNormalised (ParamId id, double n) override { runner.params().setNormalized (id, n); }
    void uiEndEdit (ParamId) override {}
    void uiResetToDefault (ParamId id) override { runner.params().reset (id); }
};

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0) p.addRectangle (4.0f, 12.0f, 3.0f, 8.0f), p.addRectangle (10.5f, 5.0f, 3.0f, 15.0f), p.addRectangle (17.0f, 9.0f, 3.0f, 11.0f);
    else if (kind == 1) p.addEllipse (4.0f, 2.0f, 4.0f, 4.0f), p.addEllipse (16.0f, 18.0f, 4.0f, 4.0f), p.startNewSubPath (6.0f, 6.0f), p.lineTo (6.0f, 10.0f), p.lineTo (18.0f, 14.0f), p.lineTo (18.0f, 18.0f);
    else p.addEllipse (8.0f, 8.0f, 8.0f, 8.0f), p.addEllipse (2.0f, 2.0f, 20.0f, 20.0f);
    return p;
}

class Scene : public juce::Component
{
public:
    static constexpr int dw = 1160, dh = 780;
    Scene (const th::Palette& pal, th::KnobStyle ks, th::ThemeId id, float scale, FakeHost& host)
        : skin_ (pal, ks, id), look_ (skin_)
    {
        skin_.setScale (scale);
        setLookAndFeel (&look_);
        const char* names[6] = { "THRESHOLD", "RATIO", "ATTACK", "RELEASE", "KNEE", "MAKEUP" };
        for (int i = 0; i < 6; ++i)
        {
            host.set.set (static_cast<ParamId> (i + 1), host.set.find (static_cast<ParamId> (i + 1))->min
                          + (host.set.find (static_cast<ParamId> (i + 1))->max - host.set.find (static_cast<ParamId> (i + 1))->min) * (0.15 + 0.13 * i));
            auto* k = cards_.add (new nova::KnobCard (skin_, host, static_cast<ParamId> (i + 1), names[i], nullptr, i == 5));
            k->setRangeLabels ("-60", "0");
            addAndMakeVisible (k);
            auto* s = subs_.add (new nova::SubSlider (skin_, host, static_cast<ParamId> (i + 1), "SUB"));
            addAndMakeVisible (s);
        }
        panel_ = std::make_unique<nova::Panel> (skin_);
        panel_->setTitle ("TRANSFER CURVE");
        panel_->setMeta ("IN -18.2 dB  GR 4.1 dB");
        addAndMakeVisible (*panel_);
        meters_ = std::make_unique<nova::Panel> (skin_);
        addAndMakeVisible (*meters_);
        const char* mt[3] = { "IN", "GR", "OUT" };
        for (int i = 0; i < 3; ++i)
        {
            auto* m = vm_.add (new nova::VMeter (skin_, mt[i], i == 1 ? nova::VMeter::Kind::Reduction : nova::VMeter::Kind::Level, i == 2));
            m->update (i == 1 ? 7.0f : -14.0f + 3.0f * static_cast<float> (i), i == 1 ? 6.0f : -17.0f);
            addAndMakeVisible (m);
        }
        const char* nav[3] = { "COMPRESSOR", "SIDECHAIN", "SETTINGS" };
        for (int i = 0; i < 3; ++i)
        {
            auto* n = navs_.add (new nova::NavItem (skin_, nav[i], iconPath (i)));
            n->setActive (i == 0);
            addAndMakeVisible (n);
        }
        for (int i = 0; i < 4; ++i)
        {
            auto* p = pills_.add (new nova::Pill (skin_, i == 3 ? nova::Pill::Look::Icon : nova::Pill::Look::Pill));
            p->setText (i == 0 ? "STEREO" : i == 1 ? "4X" : "LOOKAHEAD 2 ms");
            p->setOn (i == 1);
            if (i == 3) p->setIcon (iconPath (2));
            addAndMakeVisible (p);
        }
        sw_ = std::make_unique<nova::Switch> (skin_);
        sw_->setOn (true);
        addAndMakeVisible (*sw_);
        sw2_ = std::make_unique<nova::Switch> (skin_);
        addAndMakeVisible (*sw2_);
        setSize (juce::roundToInt (dw * scale), juce::roundToInt (dh * scale));
    }
    ~Scene() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (skin_.colour (th::Token::BgApp));
        nova::paintHeaderBar (g, skin_, header_.toFloat(), "COMPRESSOR", "NOVA SERIES");
        nova::paintBottomBar (g, skin_, bottom_.toFloat());
    }

    void resized() override
    {
        const auto S = [this] (float v) { return skin_.ipx (v); };
        auto r = getLocalBounds();
        header_ = r.removeFromTop (S (58.0f));
        bottom_ = r.removeFromBottom (S (56.0f));
        auto body = r;
        auto side = body.removeFromLeft (S (190.0f)).reduced (S (14.0f), S (14.0f));
        meters_->setBounds (side.removeFromTop (S (396.0f)));
        auto mm = meters_->getBounds().reduced (S (8.0f), S (12.0f));
        const int mw = mm.getWidth() / 3;
        for (int i = 0; i < 3; ++i) vm_[i]->setBounds (mm.removeFromLeft (mw));
        auto navArea = side.withTrimmedTop (S (14.0f));
        for (auto* n : navs_) n->setBounds (navArea.removeFromTop (S (40.0f)).withTrimmedBottom (S (4.0f)));
        auto content = body.reduced (S (14.0f), S (14.0f));
        panel_->setBounds (content.removeFromTop (S (396.0f)));
        content.removeFromTop (S (14.0f));
        const int cw = (content.getWidth() - S (12.0f) * 5) / 6;
        for (int i = 0; i < 6; ++i)
        {
            auto c = content.removeFromLeft (cw);
            content.removeFromLeft (S (12.0f));
            auto card = c.removeFromTop (S (150.0f));
            cards_[i]->setBounds (card);
            subs_[i]->setBounds (c.withTrimmedTop (S (8.0f)).withHeight (S (34.0f)));
        }
        auto b = bottom_.reduced (S (14.0f), S (12.0f));
        for (int i = 0; i < 3; ++i) pills_[i]->setBounds (b.removeFromLeft (S (i == 2 ? 150.0f : 100.0f)).withHeight (S (32.0f))), b.removeFromLeft (S (12.0f));
        sw_->setBounds (b.removeFromLeft (S (34.0f)).withSizeKeepingCentre (S (34.0f), S (19.0f))), b.removeFromLeft (S (12.0f));
        sw2_->setBounds (b.removeFromLeft (S (34.0f)).withSizeKeepingCentre (S (34.0f), S (19.0f)));
        pills_[3]->setBounds (bottom_.getRight() - S (14.0f + 36.0f), bottom_.getCentreY() - S (18.0f), S (36.0f), S (36.0f));
        for (auto* k : cards_) k->resized();
    }

private:
    nova::Skin skin_;
    nova::LookAndFeel look_;
    juce::OwnedArray<nova::KnobCard> cards_;
    juce::OwnedArray<nova::SubSlider> subs_;
    juce::OwnedArray<nova::VMeter> vm_;
    juce::OwnedArray<nova::NavItem> navs_;
    juce::OwnedArray<nova::Pill> pills_;
    std::unique_ptr<nova::Panel> panel_, meters_;
    std::unique_ptr<nova::Switch> sw_, sw2_;
    juce::Rectangle<int> header_, bottom_;
};
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "nova-gallery");
    out.createDirectory();

    FakeHost host;
    for (int i = 0; i < 6; ++i)
        host.set.add (ParamSpec::continuous (static_cast<ParamId> (i + 1), "p" + std::to_string (i), i % 2 ? Unit::Milliseconds : Unit::Decibel, i % 2 ? 1.0 : -60.0, i % 2 ? 500.0 : 0.0, i % 2 ? 10.0 : -12.0));

    struct Case { const char* name; th::ThemeId id; float scale; };
    const Case cases[] = { { "dark-100", th::ThemeId::Dark, 1.0f }, { "light-100", th::ThemeId::Light, 1.0f },
                           { "hc-100", th::ThemeId::HighContrast, 1.0f }, { "hammond-100", th::ThemeId::Hammond, 1.0f },
                           { "dark-070", th::ThemeId::Dark, 0.7f }, { "dark-140", th::ThemeId::Dark, 1.4f } };
    for (const auto& c : cases)
    {
        std::fprintf (stderr, "case %s\n", c.name);
        const auto& def = th::definition (c.id);
        Scene scene (th::withContrast (def, 0), def.knob, c.id, c.scale, host);
        const auto img = scene.createComponentSnapshot (scene.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile (juce::String (c.name) + ".png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (img, os); }
        std::printf ("%s %dx%d\n", c.name, img.getWidth(), img.getHeight());
    }
    // ---- l'éditeur réel du compresseur : trois pages, quatre thèmes, plusieurs échelles
    ModuleRunner runner (std::make_unique<CompressorModule>());
    runner.prepare (48000.0, 512);
    RunnerHost rhost (runner);
    auto& ps = runner.params();
    ps.set (compressor::threshold, -24.0);
    ps.set (compressor::ratio, 4.0);
    ps.set (compressor::attack, 10.0);
    ps.set (compressor::release, 150.0);
    ps.set (compressor::makeup, 3.2);
    ps.set (compressor::sideEqBypass, 0.0);
    ps.set (compressor::bandParam (0, compressor::kStatus), compressor::kOn);
    ps.set (compressor::bandParam (0, compressor::kType), compressor::kHighPass);
    ps.set (compressor::bandParam (0, compressor::kFreq), 120.0);
    ps.set (compressor::bandParam (1, compressor::kStatus), compressor::kOn);
    ps.set (compressor::bandParam (1, compressor::kFreq), 2500.0);
    ps.set (compressor::bandParam (1, compressor::kGain), 6.0);
    ps.set (compressor::bandParam (1, compressor::kQ), 1.2);
    ps.set (compressor::bandParam (2, compressor::kStatus), compressor::kOn);
    ps.set (compressor::bandParam (2, compressor::kFreq), 400.0);
    ps.set (compressor::bandParam (2, compressor::kGain), -5.0);
    ps.set (compressor::bandParam (4, compressor::kStatus), compressor::kOn);
    ps.set (compressor::bandParam (4, compressor::kType), compressor::kLowPass);
    ps.set (compressor::bandParam (4, compressor::kFreq), 9000.0);
    const i18n::Translator tr ("en");
    struct EdCase { const char* name; th::ThemeId id; float scale; int page; };
    const EdCase eds[] = { { "comp-dark-p0", th::ThemeId::Dark, 1.0f, 0 }, { "comp-dark-p3", th::ThemeId::Dark, 1.0f, 3 }, { "comp-dark-p1", th::ThemeId::Dark, 1.0f, 1 }, { "comp-dark-p2", th::ThemeId::Dark, 1.0f, 2 },
                           { "comp-light-p0", th::ThemeId::Light, 1.0f, 0 }, { "comp-hc-p0", th::ThemeId::HighContrast, 1.0f, 0 },
                           { "comp-hammond-p1", th::ThemeId::Hammond, 1.0f, 1 }, { "comp-dark-p0-070", th::ThemeId::Dark, 0.7f, 0 } };
    for (const auto& c : eds)
    {
        std::fprintf (stderr, "editor %s\n", c.name);
        const auto& def = th::definition (c.id);
        NovaCompressorEditor::Options opt;
        opt.brand = true;
        NovaCompressorEditor ed (rhost, static_cast<const CompressorModule*> (&runner.module()), th::withContrast (def, 0), def.knob, tr, opt);
        ed.setSize (juce::roundToInt (1160 * c.scale), juce::roundToInt (780 * c.scale));
        ed.setPageForTest (c.page);
        // signal simulé : percussions toutes les 0,6 s, jouées à 30 images par seconde pendant 10 s ; l'historique défile sur l'horloge simulée
        {
            std::vector<float> l (512), r (512);
            long sampleIndex = 0;
            double simTime = 100.0;
            for (int frame = 0; frame < 300; ++frame)
            {
                int remaining = 1600;
                while (remaining > 0)
                {
                    const int n = std::min (512, remaining);
                    for (int i = 0; i < n; ++i, ++sampleIndex)
                    {
                        const double t = static_cast<double> (sampleIndex) / 48000.0;
                        const double since = std::fmod (t, 0.6);
                        const double amp = 0.08 + 0.75 * std::exp (-since * 7.0) * (std::fmod (t, 2.4) < 0.6 ? 0.55 : 1.0);
                        const float v = static_cast<float> (amp * std::sin (6.2831853 * 110.0 * t));
                        l[static_cast<std::size_t> (i)] = v;
                        r[static_cast<std::size_t> (i)] = v * 0.9f;
                    }
                    runner.process (l.data(), r.data(), n, false);
                    remaining -= n;
                }
                simTime += 1.0 / 30.0;
                ed.refreshNow();
                ed.stepHistoryForTest (simTime);
            }
        }
        const auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile (juce::String (c.name) + ".png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (img, os); }
        std::printf ("%s %dx%d\n", c.name, img.getWidth(), img.getHeight());
    }

    // ---- l'éditeur réel de l'égaliseur
    {
        nova::SpectrumEngine::setThreading (false);
        ModuleRunner eqRunner (std::make_unique<EqualizerModule>());
        eqRunner.prepare (48000.0, 512);
        RunnerHost eh (eqRunner);
        auto& ep = eqRunner.params();
        namespace e = equalizer;
        auto band = [&] (int b, int type, double f, double g, double q, int dyn = 0)
        {
            ep.set (e::bandParam (b, e::kStatus), e::kOn);
            ep.set (e::bandParam (b, e::kType), type);
            ep.set (e::bandParam (b, e::kFreq), f);
            ep.set (e::bandParam (b, e::kGain), g);
            ep.set (e::bandParam (b, e::kQ), q);
            ep.set (e::bandParam (b, e::kDynOn), dyn);
            ep.set (e::bandParam (b, e::kTarget), -6.0);
            ep.set (e::bandParam (b, e::kThreshold), -30.0);
        };
        band (0, e::kHighPass, 80, 0, 0.707);
        band (1, e::kPeak, 250, -3.5, 1.2);
        band (2, e::kPeak, 3200, 4.0, 0.9, 1);
        band (3, e::kHighShelf, 9000, 2.5, 0.7);
        band (4, e::kNotch, 1800, 0, 4.0);
        ep.set (e::bandParam (2, e::kSideType), e::kSideBandPass);
        {
            std::vector<float> l (512), r (512);
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = 0.4f * std::sin (0.3f * static_cast<float> (b * 512 + i));
                eqRunner.process (l.data(), r.data(), 512, false);
            }
        }
        struct EqCase { const char* name; th::ThemeId id; float scale; int page; int sel; };
        const EqCase ecs[] = { { "eq-dark-p0", th::ThemeId::Dark, 1.0f, 0, 2 }, { "eq-dark-p2", th::ThemeId::Dark, 1.0f, 2, 2 }, { "eq-dark-p1", th::ThemeId::Dark, 1.0f, 1, 2 }, { "eq-light-p0", th::ThemeId::Light, 1.0f, 0, 0 },
                               { "eq-hc-p0", th::ThemeId::HighContrast, 1.0f, 0, 1 }, { "eq-hammond-p0", th::ThemeId::Hammond, 1.0f, 0, 3 }, { "eq-dark-p0-070", th::ThemeId::Dark, 0.7f, 0, 2 } };
        for (const auto& c : ecs)
        {
            std::fprintf (stderr, "editor %s\n", c.name);
            const auto& def = th::definition (c.id);
            NovaEqualizerEditor::Options opt;
            opt.brand = true;
            NovaEqualizerEditor ed (eh, static_cast<const EqualizerModule*> (&eqRunner.module()), th::withContrast (def, 0), def.knob, tr, opt);
            ed.setSize (juce::roundToInt (1160 * c.scale), juce::roundToInt (780 * c.scale));
            ed.selectBandForTest (c.sel);
            ed.setPageForTest (c.page);
            ed.refreshNow();   // active l'anneau d'analyse
            // signal simulé (bruit passé dans un filtre d'un pôle, deux sinus) joué à 30 images par seconde ; le spectre est calculé sans fil
            {
                std::vector<float> l (512), r (512);
                float lp = 0.0f;
                std::uint32_t seed = 12345u;
                long sampleIndex = 0;
                for (int frame = 0; frame < 150; ++frame)
                {
                    int remaining = 1600;
                    while (remaining > 0)
                    {
                        const int n = std::min (512, remaining);
                        for (int i = 0; i < n; ++i, ++sampleIndex)
                        {
                            seed = seed * 1664525u + 1013904223u;
                            const float w = static_cast<float> (seed >> 8) / 8388608.0f - 1.0f;
                            lp += 0.18f * (w - lp);
                            const double t = static_cast<double> (sampleIndex) / 48000.0;
                            const float v = 0.5f * lp + 0.12f * static_cast<float> (std::sin (6.2831853 * 220.0 * t)) + 0.05f * static_cast<float> (std::sin (6.2831853 * 3000.0 * t));
                            l[static_cast<std::size_t> (i)] = v;
                            r[static_cast<std::size_t> (i)] = 0.9f * v;
                        }
                        eqRunner.process (l.data(), r.data(), n, false);
                        remaining -= n;
                    }
                    ed.stepSpectrumForTest();
                    ed.refreshNow();
                }
            }
            const auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream os (out.getChildFile (juce::String (c.name) + ".png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (img, os); }
            std::printf ("%s %dx%d\n", c.name, img.getWidth(), img.getHeight());
        }
    }
    return 0;
}
