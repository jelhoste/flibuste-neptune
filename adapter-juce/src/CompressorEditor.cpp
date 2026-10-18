#include "CompressorEditor.h"

#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace cm = neptune::core::compressor;

namespace
{
juce::Colour tokenColour (const theme::Palette& p, Token t)
{
    const auto& c = p.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }
} // namespace

std::unique_ptr<GenericModuleEditor> makeCompressorEditor (ParamHost& host, const CompressorModule* module, const theme::Palette& palette, const i18n::Translator& tr)
{
    GenericModuleEditor::Options o;
    auto title = [&tr] (const char* key) { return juce::String::fromUTF8 (std::string (tr.tr (key)).c_str()); };
    o.groups = {
        { title ("cmp.group_main"), cm::style, cm::wet, true },
        { title ("cmp.group_side"), cm::externalSide, cm::sideGain, true },
        { title ("cmp.group_stereo"), cm::stereoMode, cm::wet2, false },
        { title ("cmp.group_output"), cm::delta, cm::lookahead, false },
        { title ("cmp.group_rms"), cm::rmsOn, cm::rmsMix, false },
    };
    for (int b = 0; b < cm::kBands; ++b)
        o.groups.push_back ({ title ("cmp.group_filter") + " " + juce::String (b + 1), cm::bandParam (b, 0), cm::bandParam (b, 5), false });
    o.headerHeight = 230;
    o.paintHeader = [&host, module, palette] (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (module == nullptr) return;
        auto col = [&palette] (Token t) { return tokenColour (palette, t); };
        auto r = area.toFloat().reduced (10.0f, 8.0f);
        auto curve = r.removeFromLeft (r.getHeight()).reduced (4.0f);
        g.setColour (col (Token::StageBg));
        g.fillRoundedRectangle (curve, 6.0f);
        // grille tous les 20 dB, de -80 à 0
        auto xOf = [&] (double db) { return curve.getX() + static_cast<float> ((db + 80.0) / 80.0) * curve.getWidth(); };
        auto yOf = [&] (double db) { return curve.getBottom() - static_cast<float> ((std::clamp (db, -80.0, 0.0) + 80.0) / 80.0) * curve.getHeight(); };
        g.setColour (col (Token::Grid));
        for (int d = -80; d <= 0; d += 20)
        {
            g.drawVerticalLine (static_cast<int> (xOf (d)), curve.getY(), curve.getBottom());
            g.drawHorizontalLine (static_cast<int> (yOf (d)), curve.getX(), curve.getRight());
        }
        g.setColour (col (Token::TextDim2));
        g.drawLine (xOf (-80), yOf (-80), xOf (0), yOf (0), 1.0f);   // diagonale : pas de traitement
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const double in = -80.0 + 80.0 * i / 160.0;
            const double out = module->curveOutputDb (host.parameters(), in);
            const float x = xOf (in), y = yOf (out);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col (Token::Accent));
        g.strokePath (p, juce::PathStrokeType (2.0f));
        // seuil
        const double thr = host.parameters().get (cm::threshold);
        g.setColour (col (Token::KnobHot));
        g.drawVerticalLine (static_cast<int> (xOf (thr)), curve.getY(), curve.getBottom());
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (col (Token::TextDim));
        g.drawText ("IN dB", curve.toNearestInt().withTrimmedTop (static_cast<int> (curve.getHeight()) - 14), juce::Justification::centredBottom);
        g.drawText ("OUT dB", curve.toNearestInt().withHeight (14), juce::Justification::centredTop);

        // vu-mètres : entrée, réduction, sortie
        r.removeFromLeft (14.0f);
        auto bars = r.withWidth (std::min (r.getWidth(), 260.0f));
        const float bw = bars.getWidth() / 6.0f;
        const char* labels[6] = { "IN L", "IN R", "GR L", "GR R", "OUT L", "OUT R" };
        const float values[6] = { dbOf (module->meter (cm::kInPeakL)), dbOf (module->meter (cm::kInPeakR)), module->meter (cm::kReductionL), module->meter (cm::kReductionR),
                                  dbOf (module->meter (cm::kOutPeakL)), dbOf (module->meter (cm::kOutPeakR)) };
        for (int i = 0; i < 6; ++i)
        {
            auto b = juce::Rectangle<float> (bars.getX() + bw * static_cast<float> (i) + 6.0f, bars.getY() + 4.0f, bw - 12.0f, bars.getHeight() - 38.0f);
            g.setColour (col (Token::Grid));
            g.fillRoundedRectangle (b, 3.0f);
            const bool gr = i == 2 || i == 3;
            const float f = gr ? juce::jlimit (0.0f, 1.0f, -values[i] / 40.0f) : juce::jlimit (0.0f, 1.0f, (values[i] + 60.0f) / 60.0f);
            g.setColour (gr ? col (Token::Pink) : col (Token::Accent));
            if (gr) g.fillRoundedRectangle (b.withHeight (b.getHeight() * f), 3.0f);                    // la réduction descend du haut
            else g.fillRoundedRectangle (b.withTrimmedTop (b.getHeight() * (1.0f - f)), 3.0f);
            g.setColour (col (Token::TextDim));
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (labels[i], b.withY (b.getBottom() + 2.0f).withHeight (12.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
            g.drawText (juce::String (values[i], 1), b.withY (b.getBottom() + 14.0f).withHeight (12.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }
    };
    return std::make_unique<GenericModuleEditor> (host, palette, tr, std::move (o));
}
