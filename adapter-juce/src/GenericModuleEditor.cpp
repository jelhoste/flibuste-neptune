#include "GenericModuleEditor.h"

#include <algorithm>
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kRowH = 30, kGroupH = 28;
}

// ------------------------------------------------------------------------------------------------ une ligne par paramètre
class GenericModuleEditor::Row : public juce::Component
{
public:
    Row (ParamHost& host, ParamId id, const theme::Palette& palette) : host_ (host), id_ (id), palette_ (palette)
    {
        const auto* spec = host.parameters().find (id);
        name_ = spec != nullptr ? juce::String::fromUTF8 (spec->name.c_str()) : juce::String (static_cast<int> (id));
        kind_ = spec != nullptr ? spec->kind : Kind::Continuous;
        if (kind_ == Kind::Choice && spec != nullptr)
        {
            for (std::size_t i = 0; i < spec->choices.size(); ++i) combo_.addItem (juce::String::fromUTF8 (spec->choices[i].c_str()), static_cast<int> (i) + 1);
            combo_.onChange = [this]
            {
                const int index = combo_.getSelectedId() - 1;
                if (updating_ || index < 0) return;
                const auto* s = host_.parameters().find (id_);
                if (s == nullptr) return;
                host_.uiBeginEdit (id_);
                host_.uiSetNormalised (id_, s->toNormalized (index));
                host_.uiEndEdit (id_);
            };
            addAndMakeVisible (combo_);
        }
        else if (kind_ == Kind::Toggle)
        {
            toggle_.onClick = [this]
            {
                if (updating_) return;
                const auto* s = host_.parameters().find (id_);
                if (s == nullptr) return;
                host_.uiBeginEdit (id_);
                host_.uiSetNormalised (id_, s->toNormalized (toggle_.getToggleState() ? 1.0 : 0.0));
                host_.uiEndEdit (id_);
            };
            addAndMakeVisible (toggle_);
        }
        else
        {
            slider_.setSliderStyle (juce::Slider::LinearBar);
            slider_.setRange (0.0, 1.0, 0.0);
            slider_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider_.setColour (juce::Slider::trackColourId, colour (Token::Accent).withAlpha (0.55f));
            slider_.setColour (juce::Slider::backgroundColourId, colour (Token::Grid));
            slider_.setColour (juce::Slider::textBoxTextColourId, colour (Token::Text));
            slider_.textFromValueFunction = [this] (double n)
            {
                const auto* s = host_.parameters().find (id_);
                return s != nullptr ? juce::String::fromUTF8 (host_.parameters().toText (*s, s->fromNormalized (n)).c_str()) : juce::String();
            };
            slider_.onDragStart = [this] { dragging_ = true; host_.uiBeginEdit (id_); };
            slider_.onValueChange = [this] { if (! updating_) host_.uiSetNormalised (id_, slider_.getValue()); };
            slider_.onDragEnd = [this] { dragging_ = false; host_.uiEndEdit (id_); };
            slider_.setDoubleClickReturnValue (true, 0.0);
            slider_.addMouseListener (this, false);
            addAndMakeVisible (slider_);
        }
        refresh (true);
    }

    ParamId id() const { return id_; }

    void refresh (bool force)
    {
        const auto* s = host_.parameters().find (id_);
        if (s == nullptr) return;
        const auto index = host_.parameters().indexOf (id_);
        const auto version = host_.parameters().versionAt (index);
        if (! force && version == seen_ && ! dragging_) return;
        seen_ = version;
        updating_ = true;
        if (kind_ == Kind::Choice) combo_.setSelectedId (static_cast<int> (std::lround (host_.parameters().get (id_))) + 1, juce::dontSendNotification);
        else if (kind_ == Kind::Toggle) toggle_.setToggleState (host_.parameters().get (id_) >= 0.5, juce::dontSendNotification);
        else if (! dragging_) { slider_.setValue (host_.parameters().getNormalized (id_), juce::dontSendNotification); slider_.updateText(); }
        updating_ = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { host_.uiResetToDefault (id_); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 3);
        labelArea_ = r.removeFromLeft (getWidth() / 2 - 10);
        if (kind_ == Kind::Choice) combo_.setBounds (r);
        else if (kind_ == Kind::Toggle) toggle_.setBounds (r);
        else slider_.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour (Token::Text));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (name_, labelArea_, juce::Justification::centredLeft, true);
        // valeur écrite sur la barre des curseurs
        if (kind_ == Kind::Continuous || kind_ == Kind::Stepped)
        {
            g.setColour (colour (Token::Text));
            g.drawText (slider_.getTextFromValue (slider_.getValue()), slider_.getBounds(), juce::Justification::centred);
        }
    }

private:
    juce::Colour colour (Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    ParamHost& host_;
    ParamId id_;
    theme::Palette palette_;
    juce::String name_;
    Kind kind_ = Kind::Continuous;
    juce::Slider slider_;
    juce::ComboBox combo_;
    juce::ToggleButton toggle_;
    juce::Rectangle<int> labelArea_;
    std::uint32_t seen_ = 0xFFFFFFFFu;
    bool dragging_ = false, updating_ = false;
};

// ------------------------------------------------------------------------------------------------ contenu défilant : titres de sections + lignes
class GenericModuleEditor::Content : public juce::Component
{
public:
    struct Section
    {
        juce::String title;
        bool open = true;
        std::vector<std::unique_ptr<Row>> rows;
        juce::Rectangle<int> titleArea;
    };
    std::vector<Section> sections;
    std::function<void()> onToggle;
    const theme::Palette* palette = nullptr;

    void layoutRows (int width)
    {
        int y = 0;
        for (auto& s : sections)
        {
            s.titleArea = { 0, y, width, kGroupH };
            y += kGroupH;
            for (auto& r : s.rows)
            {
                r->setVisible (s.open);
                if (s.open) { r->setBounds (0, y, width, kRowH); y += kRowH; }
            }
            y += 6;
        }
        setSize (width, y);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& s : sections)
            if (s.titleArea.contains (e.getPosition())) { s.open = ! s.open; if (onToggle) onToggle(); return; }
    }
    void paint (juce::Graphics& g) override
    {
        if (palette == nullptr) return;
        auto col = [this] (Token t) { const auto& c = palette->color (t); return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a)); };
        for (auto& s : sections)
        {
            g.setColour (col (Token::PanelBg));
            g.fillRoundedRectangle (s.titleArea.toFloat().reduced (2.0f), 4.0f);
            g.setColour (col (Token::Accent));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText ((s.open ? juce::String::fromUTF8 ("\xE2\x96\xBE  ") : juce::String::fromUTF8 ("\xE2\x96\xB8  ")) + s.title, s.titleArea.reduced (10, 0), juce::Justification::centredLeft);
        }
    }
};

GenericModuleEditor::GenericModuleEditor (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, Options options)
    : host_ (host), palette_ (palette), tr_ (tr), options_ (std::move (options))
{
    setWantsKeyboardFocus (true);
    content_ = std::make_unique<Content>();
    content_->palette = &palette_;
    content_->onToggle = [this] { content_->layoutRows (viewport_.getMaximumVisibleWidth()); };
    for (const auto& g : options_.groups)
    {
        Content::Section sec;
        sec.title = g.title;
        sec.open = g.open;
        const auto& set = host_.parameters();
        for (std::size_t i = 0; i < set.size(); ++i)
        {
            const auto id = set.spec (i).id;
            if (id < g.first || id > g.last) continue;
            if (std::find (options_.hidden.begin(), options_.hidden.end(), id) != options_.hidden.end()) continue;
            auto row = std::make_unique<Row> (host_, id, palette_);
            content_->addAndMakeVisible (*row);
            sec.rows.push_back (std::move (row));
        }
        content_->sections.push_back (std::move (sec));
    }
    viewport_.setViewedComponent (content_.get(), false);
    viewport_.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport_);
    startTimerHz (30);
}

GenericModuleEditor::~GenericModuleEditor() { stopTimer(); }

juce::Colour GenericModuleEditor::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void GenericModuleEditor::timerCallback()
{
    for (auto& s : content_->sections)
        if (s.open) for (auto& r : s.rows) r->refresh (false);
    if (options_.paintHeader) repaint (0, 0, getWidth(), options_.headerHeight);
}

bool GenericModuleEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z') { if (m.isShiftDown()) host_.uiRedo(); else host_.uiUndo(); return true; }
    if (code == 'Y' || code == 'y') { host_.uiRedo(); return true; }
    return false;
}

void GenericModuleEditor::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (options_.headerHeight);
    viewport_.setBounds (r);
    content_->layoutRows (viewport_.getMaximumVisibleWidth());
}

void GenericModuleEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgApp));
    if (options_.paintHeader && options_.headerHeight > 0) options_.paintHeader (g, { 0, 0, getWidth(), options_.headerHeight });
}
