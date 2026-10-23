#include "NovaConvolutionEditor.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "neptune/core/ConvolutionPresets.h"
#include "neptune/core/conv/ir_display.hpp"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace cvl = neptune::core::convolution;

namespace
{
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // réponse impulsionnelle : une impulsion qui s'éteint
    {
        const float h[7] = { 9, -7, 5, -4, 3, -2, 1 };
        for (int i = 0; i < 7; ++i) { const float x = 4.0f + 2.8f * static_cast<float> (i); p.startNewSubPath (x, 12.0f); p.lineTo (x, 12.0f - h[i]); }
    }
    else if (kind == 1)   // curseurs
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (14.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (6.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (12.0f, 16.0f, 4.0f, 4.0f);
    }
    else if (kind == 2)   // niveaux
    {
        for (const float x : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (x, 20.0f); p.lineTo (x, x == 12.0f ? 5.0f : 11.0f); }
    }
    else
    {
        p.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        p.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
    }
    return p;
}

void setPlain (ParamHost& host, ParamId id, double v)
{
    if (const auto* spec = host.parameters().find (id)) host.uiSetNormalised (id, spec->toNormalized (v));
}

// Calcul de l'affichage de l'IR hors du thread de l'interface : la dernière demande gagne, un résultat périmé est abandonné.
class DisplayWorker
{
public:
    struct Job
    {
        std::shared_ptr<const llc::IRBuffer> raw;
        llc::IROptions options;
        llc::InputMode mode = llc::InputMode::Sum;
    };

    DisplayWorker() : thread_ ([this] { loop(); }) {}
    ~DisplayWorker()
    {
        { const std::lock_guard<std::mutex> lock (m_); quit_ = true; }
        cv_.notify_all();
        thread_.join();
    }
    DisplayWorker (const DisplayWorker&) = delete;
    DisplayWorker& operator= (const DisplayWorker&) = delete;

    void request (Job job, std::uint32_t id)
    {
        { const std::lock_guard<std::mutex> lock (m_); job_ = std::move (job); id_ = id; pending_ = true; }
        cv_.notify_all();
    }
    std::shared_ptr<const llc::IREditorDisplay> fetch (std::uint32_t& id) const
    {
        const std::lock_guard<std::mutex> lock (m_);
        id = resultId_;
        return result_;
    }

private:
    void loop()
    {
        for (;;)
        {
            Job job;
            std::uint32_t id = 0;
            {
                std::unique_lock<std::mutex> lock (m_);
                cv_.wait (lock, [this] { return pending_ || quit_; });
                if (quit_) return;
                job = std::move (job_);
                id = id_;
                pending_ = false;
            }
            auto d = std::make_shared<llc::IREditorDisplay>();
            if (job.raw != nullptr) *d = llc::computeIREditorDisplay (*job.raw, job.options, job.mode);
            {
                const std::lock_guard<std::mutex> lock (m_);
                if (pending_) continue;   // une demande plus récente est arrivée entre-temps : ce résultat est périmé
                result_ = std::move (d);
                resultId_ = id;
            }
        }
    }

    mutable std::mutex m_;
    std::condition_variable cv_;
    Job job_;
    std::uint32_t id_ = 0, resultId_ = 0;
    bool pending_ = false, quit_ = false;
    std::shared_ptr<const llc::IREditorDisplay> result_;
    std::thread thread_;
};
} // namespace

// ------------------------------------------------------------------------------------------------ forme d'onde et spectre de l'IR
class NovaConvolutionEditor::IrView : public juce::Component
{
public:
    IrView (const nova::Skin& s, ParamHost& h, ConvolutionModule& m) : skin_ (s), host_ (h), module_ (m) {}

    std::function<juce::String()> emptyText;

    void setSpectrum (bool on) { spectrum_ = on; repaint(); }

    void update()
    {
        std::uint32_t key = module_.irVersion() * 2654435761u;
        for (ParamId id = cvl::inputMode; id <= cvl::norm; ++id) key = key * 31u + host_.parameters().version (id);
        if (key != lastKey_)
        {
            lastKey_ = key;
            worker_.request ({ module_.rawIr(), module_.irOptions(), module_.inputModeValue() }, ++requestId_);
        }
        std::uint32_t id = 0;
        auto d = worker_.fetch (id);
        if (id != shownId_)
        {
            shownId_ = id;
            disp_ = d;
            repaint();
        }
        const auto status = module_.irStatus();
        if (status != lastStatus_) { lastStatus_ = status; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        if (disp_ == nullptr || ! disp_->valid || ! disp_->kept.valid)
        {
            g.setColour (skin_.colour (Token::TextDim2));
            g.setFont (skin_.font (13.0f));
            g.drawText (emptyText ? emptyText() : juce::String(), getLocalBounds(), juce::Justification::centred);
            return;
        }
        if (spectrum_) paintSpectrum (g);
        else paintWave (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (! spectrum_ && hit (e.position) > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag_ = e.mods.isPopupMenu() || spectrum_ ? 0 : hit (e.position);
        if (drag_ == 0) return;
        host_.uiBeginEdit (drag_ == 1 ? cvl::begin : cvl::end);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag_ == 0 || disp_ == nullptr) return;
        const auto plot = plotArea();
        const double ms = xToMs (e.position.x);
        if (drag_ == 1)
        {
            const double v = std::clamp (ms, 0.0, 500.0);
            setPlain (host_, cvl::begin, v);
            dragMs_ = v;
        }
        else
        {
            const bool atEdge = e.position.x >= plot.getRight() - skin_.px (6.0f);
            const double v = atEdge ? 0.0 : std::clamp (ms, 1.0, 10000.0);
            setPlain (host_, cvl::end, v);
            dragMs_ = v == 0.0 ? disp_->axisMs : v;
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag_ != 0) host_.uiEndEdit (drag_ == 1 ? cvl::begin : cvl::end);
        drag_ = 0;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (spectrum_) return;
        const int h = hit (e.position);
        if (h > 0) host_.uiResetToDefault (h == 1 ? cvl::begin : cvl::end);
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (36.0f), b.getY() + skin_.px (12.0f), b.getWidth() - skin_.px (36.0f + 14.0f), b.getHeight() - skin_.px (12.0f + 24.0f) };
    }
    float msToX (double ms) const
    {
        const auto p = plotArea();
        const double axis = disp_ != nullptr && disp_->axisMs > 0.0 ? disp_->axisMs : 1.0;
        return p.getX() + static_cast<float> (std::clamp (ms / axis, 0.0, 1.0)) * p.getWidth();
    }
    double xToMs (float x) const
    {
        const auto p = plotArea();
        const double axis = disp_ != nullptr ? disp_->axisMs : 1.0;
        return std::clamp (static_cast<double> ((x - p.getX()) / p.getWidth()), 0.0, 1.0) * axis;
    }
    double beginMs() const { return drag_ == 1 ? dragMs_ : disp_->keptStartMs; }
    double endMs() const { return drag_ == 2 ? dragMs_ : (disp_->endAtAxisEnd ? disp_->axisMs : disp_->keptEndMs); }
    // 1 : marqueur de début (B), 2 : marqueur de fin (E), 0 : aucun
    int hit (juce::Point<float> p) const
    {
        if (disp_ == nullptr || ! disp_->valid) return 0;
        const float db = std::fabs (p.x - msToX (beginMs())), de = std::fabs (p.x - msToX (endMs())), lim = skin_.px (9.0f);
        if (db <= lim && db <= de) return 1;
        return de <= lim ? 2 : 0;
    }

    void paintWave (juce::Graphics& g) const
    {
        const auto plot = plotArea();
        const double axis = disp_->axisMs;
        double step = 1.0;
        for (const double s : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 }) { step = s; if (axis / s <= 9.0) break; }
        g.setFont (skin_.font (9.0f));
        for (double t = 0.0; t <= axis + 1.0e-6; t += step)
        {
            const float x = msToX (t);
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (x - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (t, step < 5.0 ? 1 : 0) + " ms", juce::Rectangle<float> (x - skin_.px (28.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (56.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        const float laneH = plot.getHeight() * 0.5f;
        const float peak = std::max ({ disp_->kept.peak, disp_->base.peak, 1.0e-6f });
        const bool reversed = host_.parameters().get (cvl::reverse) >= 0.5;
        const float xb = msToX (beginMs()), xe = msToX (endMs());
        for (int t = 0; t < 2; ++t)
        {
            const float centre = plot.getY() + laneH * (static_cast<float> (t) + 0.5f), half = laneH * 0.5f * 0.92f;
            g.setColour (skin_.colour (Token::Grid));
            g.fillRect (plot.getX(), centre - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
            // l'IR entière, estompée
            const auto& bmin = disp_->base.waveMin[t];
            const auto& bmax = disp_->base.waveMax[t];
            const std::size_t nb = std::min (bmin.size(), bmax.size());
            g.setColour (skin_.colour (Token::TextDim2, 0.45f));
            for (std::size_t c = 0; c < nb; ++c)
            {
                const float x = plot.getX() + static_cast<float> (c) * plot.getWidth() / static_cast<float> (nb);
                const float top = centre - bmax[c] / peak * half, bottom = centre - bmin[c] / peak * half;
                g.fillRect (x, top, plot.getWidth() / static_cast<float> (nb) + 0.5f, std::max (1.0f, bottom - top));
            }
            // l'IR jouée, vive, entre les marqueurs (dans le sens de l'axe : une IR inversée se relit à l'envers)
            const auto& kmin = disp_->kept.waveMin[t];
            const auto& kmax = disp_->kept.waveMax[t];
            const std::size_t nk = std::min (kmin.size(), kmax.size());
            g.setColour (skin_.colour (t == 0 ? Token::Accent : Token::Pink));
            const float spanX = xe - xb;
            for (std::size_t c = 0; c < nk; ++c)
            {
                const std::size_t src = reversed ? nk - 1 - c : c;
                const float x = xb + static_cast<float> (c) * spanX / static_cast<float> (nk);
                const float top = centre - kmax[src] / peak * half, bottom = centre - kmin[src] / peak * half;
                g.fillRect (x, top, spanX / static_cast<float> (nk) + 0.5f, std::max (1.0f, bottom - top));
            }
        }
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        g.drawText ("L", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY(), skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        g.drawText ("R", juce::Rectangle<float> (plot.getX() - skin_.px (28.0f), plot.getY() + laneH, skin_.px (22.0f), skin_.px (16.0f)), juce::Justification::centredRight);
        // marqueurs de début (B) et de fin (E)
        const char* labels[2] = { "B", "E" };
        const float xs[2] = { xb, xe };
        for (int i = 0; i < 2; ++i)
        {
            g.setColour (skin_.colour (Token::Handle, 0.95f));
            g.fillRect (xs[i] - skin_.px (1.0f), plot.getY(), std::max (2.0f, skin_.px (2.0f)), plot.getHeight());
            juce::Path flag;
            const float w = skin_.px (14.0f), h = skin_.px (14.0f), x0 = i == 0 ? xs[i] : xs[i] - w;
            flag.addRectangle (x0, plot.getY(), w, h);
            g.fillPath (flag);
            g.setColour (skin_.colour (Token::LogoInk));
            g.setFont (skin_.font (9.0f, true));
            g.drawText (labels[i], juce::Rectangle<float> (x0, plot.getY(), w, h), juce::Justification::centred);
        }
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (10.0f));
        g.drawText (juce::String (disp_->kept.durationMs, 1) + " ms", juce::Rectangle<float> (plot.getRight() - skin_.px (120.0f), plot.getY() + skin_.px (16.0f), skin_.px (118.0f), skin_.px (14.0f)), juce::Justification::centredRight);
    }

    void paintSpectrum (juce::Graphics& g) const
    {
        const auto plot = plotArea();
        const auto& k = disp_->kept;
        float maxDb = -120.0f;
        for (int t = 0; t < 2; ++t) for (const float v : k.spectrumDb[t]) maxDb = std::max (maxDb, v);
        const float top = std::ceil (maxDb / 10.0f) * 10.0f, span = 90.0f;
        auto yOf = [&] (float db) { return plot.getY() + (top - std::clamp (db, top - span, top)) / span * plot.getHeight(); };
        g.setFont (skin_.font (9.0f));
        for (float db = top; db >= top - span; db -= 10.0f)
        {
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (plot.getX(), yOf (db) - 0.5f, plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
            if (std::fmod (std::fabs (db), 20.0f) < 0.5f)
            {
                g.setColour (skin_.colour (Token::TextDim2));
                g.drawText (juce::String (static_cast<int> (db)), juce::Rectangle<float> (plot.getX() - skin_.px (32.0f), yOf (db) - skin_.px (6.0f), skin_.px (28.0f), skin_.px (12.0f)), juce::Justification::centredRight);
            }
        }
        const double fMin = k.fMin, fMax = k.fMax;
        auto xOf = [&] (double f) { return plot.getX() + static_cast<float> (std::log (f / fMin) / std::log (fMax / fMin)) * plot.getWidth(); };
        for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (f < fMin || f > fMax) continue;
            g.setColour (skin_.colour (Token::Grid, 0.6f));
            g.fillRect (xOf (f) - 0.5f, plot.getY(), std::max (1.0f, skin_.px (1.0f)), plot.getHeight());
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : (f == 1000.0 || f == 2000.0 || f == 5000.0 ? 0 : 1)) + "k" : juce::String (static_cast<int> (f)),
                        juce::Rectangle<float> (xOf (f) - skin_.px (20.0f), plot.getBottom() + skin_.px (4.0f), skin_.px (40.0f), skin_.px (12.0f)), juce::Justification::centred);
        }
        for (int t = 1; t >= 0; --t)   // la voie gauche passe au-dessus
        {
            const auto& s = k.spectrumDb[t];
            if (s.size() < 2) continue;
            juce::Path p;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const float x = plot.getX() + static_cast<float> (i) / static_cast<float> (s.size() - 1) * plot.getWidth();
                if (i == 0) p.startNewSubPath (x, yOf (s[i])); else p.lineTo (x, yOf (s[i]));
            }
            g.setColour (skin_.colour (t == 0 ? Token::Accent : Token::Pink, 0.9f));
            g.strokePath (p, juce::PathStrokeType (skin_.px (1.6f)));
        }
    }

    const nova::Skin& skin_;
    ParamHost& host_;
    ConvolutionModule& module_;
    DisplayWorker worker_;
    std::shared_ptr<const llc::IREditorDisplay> disp_;
    std::uint32_t lastKey_ = 0xFFFFFFFFu, requestId_ = 0, shownId_ = 0;
    int lastStatus_ = -1, drag_ = 0;
    double dragMs_ = 0.0;
    bool spectrum_ = false;
};

// ------------------------------------------------------------------------------------------------ niveaux
class NovaConvolutionEditor::LevelsView : public juce::Component
{
public:
    LevelsView (const nova::Skin& s, ConvolutionModule& m, const i18n::Translator& tr) : skin_ (s), module_ (m), tr_ (tr) {}

    void update()
    {
        bool changed = false;
        for (int i = 0; i < cvl::kNumMeters; ++i)
        {
            const float v = module_.meter (i);
            if (std::fabs (v - shown_[static_cast<std::size_t> (i)]) > 0.05f) { shown_[static_cast<std::size_t> (i)] = v; changed = true; }
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        skin_.fill (g, Token::StageBg, getLocalBounds().toFloat(), skin_.px (6.0f));
        auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
        const auto b = getLocalBounds().toFloat().reduced (skin_.px (16.0f), skin_.px (12.0f));
        const float labelW = skin_.px (190.0f), colW = (b.getWidth() - labelW) / 4.0f, rowH = skin_.px (34.0f);
        const char* heads[4] = { "cv.in_l", "cv.in_r", "cv.out_l", "cv.out_r" };
        g.setFont (skin_.font (11.0f, true));
        for (int c = 0; c < 4; ++c)
        {
            g.setColour (skin_.colour (c < 2 ? Token::Accent : Token::Pink));
            g.drawText (text (heads[c]), juce::Rectangle<float> (b.getX() + labelW + colW * static_cast<float> (c), b.getY(), colW, rowH), juce::Justification::centred);
        }
        struct Row { const char* label; int first; const char* unit; };
        const Row rows[3] = { { "cv.peak", cvl::kPeakInL, " dB" }, { "cv.peak_max", cvl::kPeakMaxInL, " dB" }, { "cv.rms", cvl::kRmsInL, " dB" } };
        float y = b.getY() + rowH;
        for (const auto& r : rows)
        {
            g.setColour (skin_.colour (Token::TextDim));
            g.setFont (skin_.font (12.0f, true));
            g.drawText (text (r.label), juce::Rectangle<float> (b.getX(), y, labelW, rowH), juce::Justification::centredLeft);
            for (int c = 0; c < 4; ++c)
            {
                const float v = shown_[static_cast<std::size_t> (r.first + c)];
                g.setColour (skin_.colour (r.first == cvl::kPeakMaxInL && v > -0.1f ? Token::KnobHot : Token::Text));
                g.setFont (skin_.font (15.0f, true));
                g.drawText (v <= -119.0f ? juce::String ("-inf") : juce::String (v, 1) + r.unit, juce::Rectangle<float> (b.getX() + labelW + colW * static_cast<float> (c), y, colW, rowH), juce::Justification::centred);
            }
            y += rowH;
        }
        y += rowH * 0.5f;
        // LUFS : momentané (400 ms) et court terme (3 s), entrée / sortie
        const char* lufsHeads[2] = { "cv.lufs_m", "cv.lufs_s" };
        const int lufsFirst[2] = { cvl::kLufsMIn, cvl::kLufsSIn };
        for (int r = 0; r < 2; ++r)
        {
            g.setColour (skin_.colour (Token::TextDim));
            g.setFont (skin_.font (12.0f, true));
            g.drawText (text (lufsHeads[r]), juce::Rectangle<float> (b.getX(), y, labelW, rowH), juce::Justification::centredLeft);
            for (int c = 0; c < 2; ++c)
            {
                const float v = shown_[static_cast<std::size_t> (lufsFirst[r] + c)];
                g.setColour (skin_.colour (Token::Text));
                g.setFont (skin_.font (15.0f, true));
                g.drawText (v <= -119.0f ? juce::String ("-inf") : juce::String (v, 1) + " LUFS",
                            juce::Rectangle<float> (b.getX() + labelW + colW * static_cast<float> (c * 2), y, colW * 2.0f, rowH), juce::Justification::centred);
            }
            y += rowH;
        }
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.5f));
        g.drawText (text ("cv.levels_note"), juce::Rectangle<float> (b.getX(), y + rowH * 0.3f, b.getWidth(), skin_.px (30.0f)), juce::Justification::topLeft);
    }

private:
    const nova::Skin& skin_;
    ConvolutionModule& module_;
    const i18n::Translator& tr_;
    std::array<float, cvl::kNumMeters> shown_ { -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f, -999.0f };
};

// ------------------------------------------------------------------------------------------------ éditeur
NovaConvolutionEditor::NovaConvolutionEditor (ParamHost& host, ConvolutionModule* module, const theme::Palette& palette, theme::KnobStyle knob,
                                              const i18n::Translator& tr, Options options)
    : EditorBase (host, palette, knob, tr, std::move (options), juce::String::fromUTF8 (std::string (tr.tr ("module.convolution")).c_str()).toUpperCase()),
      module_ (module)
{
    addPage (tx ("cv.page_ir"), iconPath (0));
    addPage (tx ("cv.page_tone"), iconPath (1));
    addPage (tx ("cv.page_levels"), iconPath (2));
    addPage (tx ("nv.page_settings"), iconPath (3));
    // colonne de gauche, commune à toutes les pages
    metersPanel_ = add<nova::Panel> (-1, skin());
    addInfo (-1, metersPanel_, "hlp.cv_meters", true);
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();   // sous les entrées du menu, créées avant lui
    inMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.in"), nova::VMeter::Kind::Level, false);
    outMeter_ = add<nova::VMeter> (-1, skin(), tx ("nv.out"), nova::VMeter::Kind::Level, true);
    for (auto* m : { inMeter_, outMeter_ }) m->setModeVisible (false);
    inTrimPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    inTrimPill_->bindValue (host, stock::inputTrim, {});
    inTrimPill_->onClick = [this] { openModal (stock::inputTrim, uistate::Terminal::Pointer); };
    buildIrPage();
    buildTonePage();
    buildLevelsPage();
    buildSettingsPage (3);
    if (const auto names = presetNames(); ! names.empty()) presetPill()->setText (names[0]);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaConvolutionEditor::~NovaConvolutionEditor() = default;

std::vector<juce::String> NovaConvolutionEditor::presetNames() const
{
    std::vector<juce::String> out;
    for (const auto& p : cvl::factoryPresets()) out.push_back (juce::String::fromUTF8 (p.name));
    return out;
}

std::vector<std::pair<ParamId, double>> NovaConvolutionEditor::presetValues (int i) const { return cvl::factoryPresets()[static_cast<std::size_t> (i)].values; }

std::vector<ParamId> NovaConvolutionEditor::presetParamIds() const { return cvl::presetParams(); }

juce::String NovaConvolutionEditor::statusText() const
{
    switch (module_->irStatus())
    {
        case cvl::kLoading: return tx ("cv.st_loading");
        case cvl::kLoaded: return tx ("cv.st_loaded") + "  -  " + juce::String (module_->irLengthMs(), 1) + " ms";
        case cvl::kError: return tx ("cv.st_error");
        default: return tx ("cv.st_none");
    }
}

void NovaConvolutionEditor::chooseFile()
{
    const juce::File current (juce::String::fromUTF8 (module_->irPath().c_str()));
    const juce::File start = current.existsAsFile() ? current.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser> (tx ("cv.choose"), start, "*.wav;*.wave;*.w64");
    juce::Component::SafePointer<NovaConvolutionEditor> safe (this);
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f != juce::File()) safe->module_->setIrPath (f.getFullPathName().toStdString());
    });
}

void NovaConvolutionEditor::buildIrPage()
{
    irPanel_ = add<nova::Panel> (0, skin());
    irPanel_->setTitle (tx ("cv.ir_title"));
    addInfo (0, irPanel_, "hlp.cv_ir", 1);
    loadPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    loadPill_->setText (tx ("cv.load"));
    loadPill_->onClick = [this] { chooseFile(); };
    clearPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    clearPill_->setText (tx ("cv.clear"));
    clearPill_->onClick = [this] { module_->setIrPath ({}); };
    fileText_ = add<nova::Text> (0, skin(), Token::Text, 12.0f, true);
    statusText_ = add<nova::Text> (0, skin(), Token::TextDim2, 11.0f);
    wavePill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    wavePill_->setText (tx ("cv.wave"));
    wavePill_->setOn (true);
    spectrumPill_ = add<nova::Pill> (0, skin(), nova::Pill::Look::Small);
    spectrumPill_->setText (tx ("cv.spectrum"));
    irView_ = add<IrView> (0, skin(), host(), *module_);
    irView_->emptyText = [this] { return statusText(); };
    wavePill_->onClick = [this] { irView_->setSpectrum (false); wavePill_->setOn (true); spectrumPill_->setOn (false); };
    spectrumPill_->onClick = [this] { irView_->setSpectrum (true); wavePill_->setOn (false); spectrumPill_->setOn (true); };

    optionsPanel_ = add<nova::Panel> (0, skin());
    optionsPanel_->setTitle (tx ("cv.options_title"));
    struct K { ParamId id; const char* title; const char* help; };
    for (const K k : { K { cvl::stretch, "cv.stretch", "hlp.cv_stretch" }, K { cvl::begin, "cv.begin", "hlp.cv_begin" }, K { cvl::end, "cv.end", "hlp.cv_end" },
                       K { cvl::attack, "cv.attack", "hlp.cv_attack" }, K { cvl::decay, "cv.decay", "hlp.cv_decay" } })
    {
        auto* kc = add<nova::KnobCard> (0, skin(), host(), k.id, tx (k.title), modalOpener(), false);
        irKnobs_.push_back (kc);
        addInfo (0, kc, k.help, true);
    }
    modePill_ = add<nova::Pill> (0, skin());
    modePill_->bindChoice (host(), cvl::inputMode, tx ("cv.input_mode"), { tx ("cv.m_stereo"), tx ("cv.m_sum"), tx ("cv.m_left"), tx ("cv.m_right") });
    addInfo (0, modePill_, "hlp.cv_mode");
    trimPill_ = add<nova::Pill> (0, skin());
    trimPill_->bindToggle (host(), cvl::autoTrim, tx ("cv.autotrim"));
    addInfo (0, trimPill_, "hlp.cv_autotrim");
    lenPill_ = add<nova::Pill> (0, skin());
    lenPill_->bindToggle (host(), cvl::autoLen, tx ("cv.autolen"));
    addInfo (0, lenPill_, "hlp.cv_autolen");
    revPill_ = add<nova::Pill> (0, skin());
    revPill_->bindToggle (host(), cvl::reverse, tx ("cv.reverse"));
    normPill_ = add<nova::Pill> (0, skin());
    normPill_->bindChoice (host(), cvl::norm, tx ("cv.norm"), { tx ("cv.n_off"), tx ("cv.n_peak"), tx ("cv.n_energy") });
    addInfo (0, normPill_, "hlp.cv_norm");
}

void NovaConvolutionEditor::buildTonePage()
{
    voicePanel_ = add<nova::Panel> (1, skin());
    voicePanel_->setTitle (tx ("cv.voice_title"));
    struct K { ParamId id; const char* title; const char* help; bool pink; };
    for (const K k : { K { cvl::preDelay, "cv.predelay", "hlp.cv_predelay", false }, K { cvl::width, "cv.width", "hlp.cv_width", false },
                       K { stock::mix, "cv.mix", "hlp.cv_mix", true }, K { stock::outputTrim, "cv.output", "hlp.cv_output", true } })
    {
        auto* kc = add<nova::KnobCard> (1, skin(), host(), k.id, tx (k.title), modalOpener(), k.pink);
        voiceKnobs_.push_back (kc);
        addInfo (1, kc, k.help, true);
    }
    eqPanel_ = add<nova::Panel> (1, skin());
    eqPanel_->setTitle (tx ("cv.eq_title"));
    eqPill_ = add<nova::Pill> (1, skin());
    eqPill_->bindToggle (host(), cvl::eqOn, tx ("cv.eq_on"));
    addInfo (1, eqPill_, "hlp.cv_eq");
    const std::vector<juce::String> types { tx ("cv.t_bell"), tx ("cv.t_low"), tx ("cv.t_high") }, slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    auto knob = [this] (ParamId id, const char* title)
    {
        auto* kc = add<nova::KnobCard> (1, skin(), host(), id, tx (title), modalOpener(), false);
        kc->setCompact (true);
        return kc;
    };
    for (int b = 0; b < 3; ++b)
    {
        EqColumn c {};
        c.on = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        c.on->bindToggle (host(), cvl::bandParam (b, cvl::kOn), "EQ " + juce::String (b + 1));
        c.type = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        c.type->bindChoice (host(), cvl::bandParam (b, cvl::kType), {}, types);
        c.freq = knob (cvl::bandParam (b, cvl::kFreq), "cv.freq");
        c.gain = knob (cvl::bandParam (b, cvl::kGain), "cv.gain");
        c.q = knob (cvl::bandParam (b, cvl::kQ), "cv.q");
        eqColumns_.push_back (c);
    }
    for (int p = 0; p < 2; ++p)
    {
        EqColumn c {};
        c.on = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        c.on->bindToggle (host(), p == 0 ? cvl::hpOn : cvl::lpOn, p == 0 ? tx ("cv.hp") : tx ("cv.lp"));
        c.slope = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        c.slope->bindChoice (host(), p == 0 ? cvl::hpSlope : cvl::lpSlope, {}, slopes);
        c.freq = knob (p == 0 ? cvl::hpFreq : cvl::lpFreq, "cv.freq");
        eqColumns_.push_back (c);
    }
}

void NovaConvolutionEditor::buildLevelsPage()
{
    levelsPanel_ = add<nova::Panel> (2, skin());
    levelsPanel_->setTitle (tx ("cv.levels_title"));
    levelsView_ = add<LevelsView> (2, skin(), *module_, translator());
    resetPill_ = add<nova::Pill> (2, skin());
    resetPill_->setText (tx ("cv.reset_peaks"));
    resetPill_->onClick = [this] { module_->resetPeakMax(); inMeter_->resetPeak(); outMeter_->resetPeak(); };
    for (const auto& k : { std::pair<ParamId, const char*> { cvl::meterRmsMs, "cv.rms_window" }, std::pair<ParamId, const char*> { cvl::meterPeakDecay, "cv.peak_decay" } })
    {
        auto* kc = add<nova::KnobCard> (2, skin(), host(), k.first, tx (k.second), modalOpener(), false);
        levelKnobs_.push_back (kc);
    }
}

void NovaConvolutionEditor::layout()
{
    // ---- colonne de gauche (toutes les pages)
    metersPanel_->setBounds (R (14, 72, 176, 456));
    {
        const auto in = metersPanel_->getBounds().reduced (skin().ipx (8.0f), skin().ipx (12.0f));
        const int w = in.getWidth() / 2, trimH = skin().ipx (40.0f);
        const auto meters = in.withTrimmedBottom (trimH);
        inMeter_->setBounds (meters.withWidth (w));
        outMeter_->setBounds (meters.withX (meters.getX() + w).withWidth (w));
        inTrimPill_->setBounds (in.getX() + skin().ipx (1.0f), in.getBottom() - skin().ipx (30.0f), w - skin().ipx (2.0f), skin().ipx (24.0f));
    }
    navPanel_->setBounds (R (14, 542, 176, 168));
    layoutNav (542, 168);

    // ---- page IR
    irPanel_->setBounds (R (204, 72, 942, 332));
    loadPill_->setBounds (R (220, 104, 140, 28));
    clearPill_->setBounds (R (366, 104, 80, 28));
    fileText_->setBounds (R (460, 104, 470, 18));
    statusText_->setBounds (R (460, 122, 470, 14));
    wavePill_->setBounds (R (940, 104, 84, 28));
    spectrumPill_->setBounds (R (1030, 104, 100, 28));
    irView_->setBounds (R (220, 142, 910, 250));
    optionsPanel_->setBounds (R (204, 418, 942, 292));
    {
        const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
        for (std::size_t i = 0; i < irKnobs_.size(); ++i) irKnobs_[i]->setBounds (R (220.0f + static_cast<float> (i) * (cw + 12.0f), 452, cw, 142));
        float x = 220.0f;
        const std::pair<nova::Pill*, float> pills[5] = { { modePill_, 180.0f }, { trimPill_, 160.0f }, { lenPill_, 170.0f }, { revPill_, 140.0f }, { normPill_, 190.0f } };
        for (const auto& p : pills) { p.first->setBounds (R (x, 612, p.second, 34)); x += p.second + 12.0f; }
    }

    // ---- page TONE
    voicePanel_->setBounds (R (204, 72, 942, 200));
    {
        const float cw = (910.0f - 12.0f * 3.0f) / 4.0f;
        for (std::size_t i = 0; i < voiceKnobs_.size(); ++i) voiceKnobs_[i]->setBounds (R (220.0f + static_cast<float> (i) * (cw + 12.0f), 108, cw, 152));
    }
    eqPanel_->setBounds (R (204, 286, 942, 424));
    eqPill_->setBounds (R (220, 322, 150, 30));
    {
        const float cw = (910.0f - 12.0f * 4.0f) / 5.0f;
        for (std::size_t i = 0; i < eqColumns_.size(); ++i)
        {
            const auto& c = eqColumns_[i];
            const float x = 220.0f + static_cast<float> (i) * (cw + 12.0f);
            c.on->setBounds (R (x, 362, cw, 28));
            if (c.type != nullptr) c.type->setBounds (R (x, 396, cw, 28));
            if (c.slope != nullptr) c.slope->setBounds (R (x, 396, cw, 28));
            c.freq->setBounds (R (x, 434, cw, 84));
            if (c.gain != nullptr) c.gain->setBounds (R (x, 524, cw, 84));
            if (c.q != nullptr) c.q->setBounds (R (x, 614, cw, 84));
        }
    }

    // ---- page LEVELS
    levelsPanel_->setBounds (R (204, 72, 942, 638));
    levelsView_->setBounds (R (220, 108, 910, 320));
    resetPill_->setBounds (R (220, 444, 180, 34));
    {
        const float cw = 218.0f;
        for (std::size_t i = 0; i < levelKnobs_.size(); ++i) levelKnobs_[i]->setBounds (R (220.0f + static_cast<float> (i) * (cw + 12.0f), 500, cw, 160));
    }

    // ---- barre du bas et en-tête
    abPill()->setBounds (R (14, 724 + 12, 40, 32));
    presetPill()->setBounds (R (470, 11, 220, 36));
    if (bypassPill() != nullptr) bypassPill()->setBounds (R (1160 - 14 - 36, 11, 36, 36));
}

void NovaConvolutionEditor::pageChanged()
{
    // les colonnes du passe-haut et du passe-bas n'ont ni type ni gain ni Q : leurs composants n'existent pas
    for (const auto& c : eqColumns_)
    {
        const bool show = currentPage() == 1;
        if (c.type != nullptr) c.type->setVisible (show);
        if (c.slope != nullptr) c.slope->setVisible (show);
        if (c.gain != nullptr) c.gain->setVisible (show);
        if (c.q != nullptr) c.q->setVisible (show);
    }
}

void NovaConvolutionEditor::tick()
{
    if (module_ == nullptr) return;
    inMeter_->update (dbOf (module_->meter (cvl::kPeakInL)), dbOf (module_->meter (cvl::kPeakInR)));
    outMeter_->update (dbOf (module_->meter (cvl::kPeakOutL)), dbOf (module_->meter (cvl::kPeakOutR)));
    if (currentPage() == 0)
    {
        irView_->update();
        const auto path = module_->irPath();
        const auto name = path.empty() ? tx ("cv.no_file") : juce::File (juce::String::fromUTF8 (path.c_str())).getFileName();
        if (name != shownFile_) { shownFile_ = name; fileText_->setText (name); }
        const auto status = statusText();
        if (status != shownStatus_) { shownStatus_ = status; statusText_->setText (status); }
    }
    if (currentPage() == 2) levelsView_->update();
}
