#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"
#include "neptune/core/WaveCatalog.h"

using neptune::core::theme::Token;
namespace theme = neptune::core::theme;
namespace i18n = neptune::core::i18n;
using neptune::core::kNumWaves;
using neptune::core::waveInfo;
using neptune::core::renderWaveCycle;

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
