#include "EqualizerEditor.h"

#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace eq = neptune::core::equalizer;

namespace
{
juce::Colour tokenColour (const theme::Palette& p, Token t)
{
    const auto& c = p.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}
float dbOf (float lin) { return lin <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (lin); }
constexpr double kMinHz = 20.0, kMaxHz = 20000.0, kRangeDb = 24.0;
} // namespace

std::unique_ptr<GenericModuleEditor> makeEqualizerEditor (ParamHost& host, const EqualizerModule* module, const theme::Palette& palette, const i18n::Translator& tr)
{
    GenericModuleEditor::Options o;
    auto title = [&tr] (const char* key) { return juce::String::fromUTF8 (std::string (tr.tr (key)).c_str()); };
    o.groups.push_back ({ title ("eq.group_main"), eq::outputGain, eq::externalSide, true });
    for (int b = 0; b < eq::kBands; ++b)
        o.groups.push_back ({ title ("eq.group_band") + " " + juce::String (b + 1), eq::bandParam (b, 0), eq::bandParam (b, eq::kBandStride - 1), b == 0 });
    o.headerHeight = 280;
    o.paintHeader = [&host, module, palette] (juce::Graphics& g, juce::Rectangle<int> area)
    {
        if (module == nullptr) return;
        auto col = [&palette] (Token t) { return tokenColour (palette, t); };
        auto r = area.toFloat().reduced (10.0f, 8.0f);
        auto bars = r.removeFromRight (120.0f);
        r.removeFromRight (10.0f);
        g.setColour (col (Token::StageBg));
        g.fillRoundedRectangle (r, 6.0f);
        auto xOf = [&] (double hz) { return r.getX() + static_cast<float> (std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz)) * r.getWidth(); };
        auto yOf = [&] (double db) { return r.getCentreY() - static_cast<float> (std::clamp (db, -kRangeDb, kRangeDb) / kRangeDb) * r.getHeight() * 0.5f; };
        g.setColour (col (Token::Grid));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine (static_cast<int> (xOf (hz)), r.getY(), r.getBottom());
        for (int d = -24; d <= 24; d += 12) g.drawHorizontalLine (static_cast<int> (yOf (d)), r.getX(), r.getRight());
        g.setColour (col (Token::TextDim));
        g.setFont (juce::FontOptions (9.5f));
        for (double hz : { 100.0, 1000.0, 10000.0 })
            g.drawText (hz >= 1000.0 ? juce::String (static_cast<int> (hz / 1000)) + "k" : juce::String (static_cast<int> (hz)), static_cast<int> (xOf (hz)) + 2, static_cast<int> (r.getBottom()) - 13, 30, 12, juce::Justification::left);
        for (int d = -12; d <= 12; d += 12) g.drawText (juce::String (d), static_cast<int> (r.getX()) + 2, static_cast<int> (yOf (d)) - 12, 30, 12, juce::Justification::left);

        const auto& ps = host.parameters();
        // une courbe fine par bande active
        for (int b = 0; b < eq::kBands; ++b)
        {
            if (static_cast<int> (std::lround (ps.get (eq::bandParam (b, eq::kStatus)))) != eq::kOn) continue;
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const double hz = kMinHz * std::pow (kMaxHz / kMinHz, i / 120.0);
                const float x = xOf (hz), y = yOf (module->bandResponseDb (ps, b, hz, true));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (col (Token::Accent).withAlpha (0.35f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        juce::Path total;
        for (int i = 0; i <= 240; ++i)
        {
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, i / 240.0);
            const float x = xOf (hz), y = yOf (module->totalResponseDb (ps, hz, true));
            if (i == 0) total.startNewSubPath (x, y); else total.lineTo (x, y);
        }
        g.setColour (col (Token::Accent));
        g.strokePath (total, juce::PathStrokeType (2.0f));
        // points des bandes
        for (int b = 0; b < eq::kBands; ++b)
        {
            if (static_cast<int> (std::lround (ps.get (eq::bandParam (b, eq::kStatus)))) != eq::kOn) continue;
            const float x = xOf (ps.get (eq::bandParam (b, eq::kFreq))), y = yOf (module->meter (eq::kBandGain0 + b));
            const bool dyn = ps.get (eq::bandParam (b, eq::kDynOn)) >= 0.5;
            g.setColour (dyn ? col (Token::Pink) : col (Token::KnobHot));
            g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            g.setColour (col (Token::StageBg));
            g.setFont (juce::FontOptions (8.5f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x - 6.0f, y - 6.0f, 12.0f, 12.0f), juce::Justification::centred);
        }

        // vu-mètres : entrée et sortie (crêtes)
        const char* labels[4] = { "IN L", "IN R", "OUT L", "OUT R" };
        const float values[4] = { dbOf (module->meter (eq::kInPeakL)), dbOf (module->meter (eq::kInPeakR)), dbOf (module->meter (eq::kOutPeakL)), dbOf (module->meter (eq::kOutPeakR)) };
        const float bw = bars.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            auto b = juce::Rectangle<float> (bars.getX() + bw * static_cast<float> (i) + 5.0f, bars.getY() + 4.0f, bw - 10.0f, bars.getHeight() - 52.0f);
            g.setColour (col (Token::Grid));
            g.fillRoundedRectangle (b, 3.0f);
            const float f = juce::jlimit (0.0f, 1.0f, (values[i] + 60.0f) / 60.0f);
            g.setColour (col (Token::Accent));
            g.fillRoundedRectangle (b.withTrimmedTop (b.getHeight() * (1.0f - f)), 3.0f);
            g.setColour (col (Token::TextDim));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (labels[i], b.withY (b.getBottom() + 2.0f).withHeight (12.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
            g.drawText (juce::String (values[i], 1), b.withY (b.getBottom() + 14.0f).withHeight (12.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
        }
        g.setColour (col (Token::TextDim));
        g.drawText ("AUTO " + juce::String (module->meter (eq::kAutoGainDb), 1) + " dB", bars.withY (bars.getBottom() - 16.0f).withHeight (14.0f).toNearestInt(), juce::Justification::centred);
    };
    return std::make_unique<GenericModuleEditor> (host, palette, tr, std::move (o));
}
