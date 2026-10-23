#include "FacadeView.h"

#include <algorithm>
#include <cmath>

using neptune::core::theme::Token;
namespace fc = neptune::core::facade;

namespace
{
juce::Colour toColour (const neptune::core::theme::Palette& p, Token t)
{
    const auto& c = p.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

int snapped (int v) { return (v + 2) / 4 * 4; } // aimantation sur une grille de 4

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
} // namespace

// ============================== Toile : dessin des widgets et gestes ==============================

class FacadeView::Canvas : public juce::Component
{
public:
    explicit Canvas (FacadeView& owner) : owner_ (owner) { setWantsKeyboardFocus (true); }

    bool dragging() const noexcept { return mode_ != Mode::None; }

    void paint (juce::Graphics& g) override
    {
        auto& o = owner_;
        const auto area = canvasArea();
        g.setColour (o.colour (Token::PanelBg));
        g.fillRect (area);

        loadImageIfNeeded();
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (area.toNearestInt());
            if (image_.isValid())
            {
                const float iw = static_cast<float> (image_.getWidth()), ih = static_cast<float> (image_.getHeight());
                auto dest = area;
                if (o.facade_.fit == fc::ImageFit::Fit || o.facade_.fit == fc::ImageFit::Fill)
                {
                    const float k = o.facade_.fit == fc::ImageFit::Fit ? std::min (area.getWidth() / iw, area.getHeight() / ih)
                                                                       : std::max (area.getWidth() / iw, area.getHeight() / ih);
                    dest = juce::Rectangle<float> (iw * k, ih * k).withCentre (area.getCentre());
                }
                g.drawImage (image_, dest, juce::RectanglePlacement::stretchToFit);
            }

            g.addTransform (juce::AffineTransform::scale (scale()).translated (area.getX(), area.getY()));
            for (const auto& w : o.facade_.widgets) paintWidget (g, w);
            if (o.editMode_)
                for (const auto& w : o.facade_.widgets) paintEditOverlay (g, w);
        }
        g.setColour (o.colour (Token::Border));
        g.drawRect (area, 1.0f);

        if (o.facade_.widgets.empty())
        {
            g.setColour (o.colour (Token::TextDim));
            g.setFont (juce::FontOptions (16.0f));
            g.drawFittedText (o.text ("facade.empty"), area.toNearestInt().reduced (40), juce::Justification::centred, 4);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& o = owner_;
        const auto p = toCanvas (e.position);
        if (o.editMode_) { editDown (p); return; }

        const auto* w = widgetAt (p);
        if (w == nullptr || w->param == 0 || o.processor_.parameters().indexOf (w->param) == neptune::core::ParameterSet::npos) return;
        if (e.mods.isPopupMenu())
        {
            if (w->type == fc::WidgetType::Knob) o.openModal_ (w->param, neptune::core::uistate::Terminal::Pointer);
            return;
        }
        playId_ = w->param;
        if (w->type == fc::WidgetType::Button)
        {
            toggle (w->param);
            playId_ = 0;
        }
        else if (w->type == fc::WidgetType::Knob)
        {
            o.processor_.uiBeginEdit (w->param);
            mode_ = Mode::Turn;
            startNorm_ = o.processor_.parameters().getNormalized (w->param);
            startY_ = e.position.y;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& o = owner_;
        if (mode_ == Mode::Turn)
        {
            const double range = e.mods.isShiftDown() ? 1000.0 : 200.0;
            o.processor_.uiSetNormalised (playId_, juce::jlimit (0.0, 1.0, startNorm_ + (startY_ - e.position.y) / range));
            repaint();
            return;
        }
        if (mode_ != Mode::Move && mode_ != Mode::Resize) return;
        const auto p = toCanvas (e.position);
        auto* w = o.facade_.find (o.selected_);
        if (w == nullptr) return;
        const int dx = juce::roundToInt (p.x - dragStart_.x), dy = juce::roundToInt (p.y - dragStart_.y);
        if (mode_ == Mode::Move)
        {
            w->x = std::clamp (snapped (startRect_.getX() + dx), 0, fc::canvasWidth - w->w);
            w->y = std::clamp (snapped (startRect_.getY() + dy), 0, fc::canvasHeight - w->h);
        }
        else
        {
            w->w = std::clamp (snapped (startRect_.getWidth() + dx), fc::minWidgetSize, fc::canvasWidth - w->x);
            w->h = std::clamp (snapped (startRect_.getHeight() + dy), fc::minWidgetSize, fc::canvasHeight - w->y);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto& o = owner_;
        const auto mode = mode_;
        mode_ = Mode::None;
        if (mode == Mode::Turn)
        {
            o.processor_.uiEndEdit (playId_);
            playId_ = 0;
        }
        else if (mode == Mode::Move || mode == Mode::Resize)
        {
            if (o.facade_ != before_) o.commit (o.facade_, mode == Mode::Move ? "Facade: move widget" : "Facade: resize widget");
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto& o = owner_;
        const auto* w = widgetAt (toCanvas (e.position));
        if (w == nullptr) return;
        if (o.editMode_) { o.selected_ = w->id; o.updateButtons(); o.openPicker(); return; }
        if (w->type == fc::WidgetType::Knob && w->param != 0) o.processor_.uiResetToDefault (w->param);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        auto& o = owner_;
        if (o.editMode_) return;
        const auto* w = widgetAt (toCanvas (e.position));
        if (w == nullptr || w->type != fc::WidgetType::Knob || w->param == 0) return;
        auto& ps = o.processor_.parameters();
        if (ps.indexOf (w->param) == neptune::core::ParameterSet::npos) return;
        o.processor_.uiBeginEdit (w->param);
        o.processor_.uiSetNormalised (w->param, juce::jlimit (0.0, 1.0, ps.getNormalized (w->param) + wheel.deltaY * 0.1));
        o.processor_.uiEndEdit (w->param);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto& o = owner_;
        if (! o.editMode_ || o.selected_ == 0) return false;
        const int step = k.getModifiers().isShiftDown() ? 10 : 1;
        int dx = 0, dy = 0;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { o.deleteSelected(); return true; }
        if (k.getKeyCode() == juce::KeyPress::leftKey) dx = -step;
        else if (k.getKeyCode() == juce::KeyPress::rightKey) dx = step;
        else if (k.getKeyCode() == juce::KeyPress::upKey) dy = -step;
        else if (k.getKeyCode() == juce::KeyPress::downKey) dy = step;
        else return false;
        auto f = o.facade_;
        if (auto* w = f.find (o.selected_))
        {
            w->x += dx;
            w->y += dy;
            o.commit (std::move (f), "Facade: move widget");
        }
        return true;
    }

private:
    enum class Mode { None, Move, Resize, Turn };

    float scale() const
    {
        return std::min (static_cast<float> (getWidth()) / fc::canvasWidth, static_cast<float> (getHeight()) / fc::canvasHeight);
    }
    juce::Rectangle<float> canvasArea() const
    {
        const float s = scale();
        return juce::Rectangle<float> (fc::canvasWidth * s, fc::canvasHeight * s).withCentre (getLocalBounds().toFloat().getCentre());
    }
    juce::Point<float> toCanvas (juce::Point<float> p) const
    {
        const auto a = canvasArea();
        const float s = scale();
        return { (p.x - a.getX()) / s, (p.y - a.getY()) / s };
    }

    const fc::Widget* widgetAt (juce::Point<float> p) const
    {
        const auto& ws = owner_.facade_.widgets;
        for (auto it = ws.rbegin(); it != ws.rend(); ++it)
            if (juce::Rectangle<float> (static_cast<float> (it->x), static_cast<float> (it->y), static_cast<float> (it->w), static_cast<float> (it->h)).contains (p))
                return &*it;
        return nullptr;
    }

    void editDown (juce::Point<float> p)
    {
        auto& o = owner_;
        before_ = o.facade_;
        dragStart_ = p;
        if (const auto* sel = o.facade_.find (o.selected_))
        {
            const juce::Rectangle<float> handle (static_cast<float> (sel->x + sel->w) - 14.0f, static_cast<float> (sel->y + sel->h) - 14.0f, 14.0f, 14.0f);
            if (handle.contains (p))
            {
                mode_ = Mode::Resize;
                startRect_ = { sel->x, sel->y, sel->w, sel->h };
                return;
            }
        }
        const auto* w = widgetAt (p);
        o.selected_ = w != nullptr ? w->id : 0;
        o.updateButtons();
        if (w != nullptr)
        {
            mode_ = Mode::Move;
            startRect_ = { w->x, w->y, w->w, w->h };
        }
        repaint();
    }

    void toggle (neptune::core::ParamId id)
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        const auto* spec = ps.find (id);
        if (spec == nullptr) return;
        const double plain = ps.get (id);
        double next;
        if ((spec->kind == neptune::core::Kind::Choice || spec->kind == neptune::core::Kind::Stepped) && spec->max - spec->min > 1.0)
            next = plain + 1.0 > spec->max ? spec->min : plain + 1.0;   // plusieurs positions : on passe à la suivante
        else
            next = ps.getNormalized (id) >= 0.5 ? spec->min : spec->max;
        o.processor_.uiBeginEdit (id);
        o.processor_.uiSetNormalised (id, spec->toNormalized (next));
        o.processor_.uiEndEdit (id);
    }

    void loadImageIfNeeded()
    {
        const auto& path = owner_.facade_.image;
        if (path == imagePath_) return;
        imagePath_ = path;
        image_ = {};
        if (path.empty()) return;
        const juce::File file (utf8 (path));
        if (file.existsAsFile()) image_ = juce::ImageFileFormat::loadFrom (file);
    }

    void paintWidget (juce::Graphics& g, const fc::Widget& w) const
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        const auto* spec = w.param != 0 ? ps.find (w.param) : nullptr;
        const bool linked = spec != nullptr;
        const juce::Rectangle<float> b (static_cast<float> (w.x), static_cast<float> (w.y), static_cast<float> (w.w), static_cast<float> (w.h));
        const juce::String caption = w.text.empty() ? (linked ? utf8 (spec->name) : juce::String()) : utf8 (w.text);
        const float n = linked ? static_cast<float> (ps.getNormalized (w.param)) : 0.0f;

        switch (w.type)
        {
            case fc::WidgetType::Knob:
            {
                auto r = b;
                auto valueRow = r.removeFromBottom (15.0f);
                auto captionRow = r.removeFromBottom (17.0f);
                paintNeptuneKnob (g, r, n, o.palette_, o.knobStyle_);
                g.setColour (o.colour (Token::Text));
                g.setFont (juce::FontOptions (14.0f));
                g.drawFittedText (caption, captionRow.toNearestInt(), juce::Justification::centred, 1);
                g.setColour (o.colour (Token::Accent));
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText (linked ? utf8 (ps.toText (w.param)) : (o.editMode_ ? o.text ("facade.unlinked") : juce::String()), valueRow.toNearestInt(), juce::Justification::centred, 1);
                break;
            }
            case fc::WidgetType::Button:
            {
                const bool on = linked && n >= 0.5f;
                const auto r = b.reduced (1.0f);
                g.setColour (on ? o.colour (Token::Accent) : o.colour (Token::Pb));
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (o.colour (Token::Border));
                g.drawRoundedRectangle (r, 8.0f, 1.5f);
                g.setColour (on ? o.colour (Token::BgApp) : o.colour (Token::Text));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 24.0f, b.getHeight() * 0.42f), juce::Font::bold));
                g.drawFittedText (caption.isEmpty() && o.editMode_ ? o.text ("facade.unlinked") : caption, r.toNearestInt().reduced (6, 2), juce::Justification::centred, 2);
                break;
            }
            case fc::WidgetType::Label:
                g.setColour (o.colour (Token::Text));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 48.0f, b.getHeight() * 0.62f)));
                g.drawFittedText (utf8 (w.text), b.toNearestInt(), juce::Justification::centredLeft, 1);
                break;
        }
    }

    void paintEditOverlay (juce::Graphics& g, const fc::Widget& w) const
    {
        auto& o = owner_;
        const bool sel = w.id == o.selected_;
        const juce::Rectangle<float> b (static_cast<float> (w.x), static_cast<float> (w.y), static_cast<float> (w.w), static_cast<float> (w.h));
        g.setColour (sel ? o.colour (Token::Accent) : o.colour (Token::TextDim2).withAlpha (0.8f));
        g.drawRect (b, sel ? 2.0f : 1.0f);
        if (sel) g.fillRect (juce::Rectangle<float> (b.getRight() - 12.0f, b.getBottom() - 12.0f, 12.0f, 12.0f));
    }

    FacadeView& owner_;
    Mode mode_ = Mode::None;
    fc::Facade before_;
    juce::Point<float> dragStart_;
    juce::Rectangle<int> startRect_;
    neptune::core::ParamId playId_ = 0;
    double startNorm_ = 0.0;
    float startY_ = 0.0f;
    std::string imagePath_;
    juce::Image image_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};

// ============================== Choix du paramètre à lier ==============================

class FacadeView::Picker : public juce::Component, private juce::ListBoxModel
{
public:
    Picker (FacadeView& owner, int track, std::function<void (neptune::core::ParamId)> onChoose)
        : owner_ (owner), onChoose_ (std::move (onChoose))
    {
        auto& ps = owner.processor_.parameters();
        for (std::size_t i = 0; i < ps.size(); ++i)
        {
            const auto& s = ps.spec (i);
            if (! s.hidden) entries_.push_back ({ s.id, utf8 (s.name) });
        }
        title_.setText (owner.text ("facade.picker_title"), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        hint_.setText (owner.text ("facade.picker_hint"), juce::dontSendNotification);
        hint_.setFont (juce::FontOptions (13.0f));
        hint_.setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
        addAndMakeVisible (title_);
        addAndMakeVisible (hint_);
        filter_.setText ("Track " + juce::String (track + 1) + " ", false);
        filter_.setFont (juce::FontOptions (18.0f));
        filter_.onTextChange = [this] { refilter(); };
        filter_.onReturnKey = [this] { choose (list_.getSelectedRow()); };
        filter_.onEscapeKey = [this] { close(); };
        addAndMakeVisible (filter_);
        list_.setModel (this);
        list_.setRowHeight (26);
        list_.setColour (juce::ListBox::backgroundColourId, owner.colour (Token::BgApp));
        addAndMakeVisible (list_);
        cancel_.setButtonText (owner.text ("facade.picker_cancel"));
        cancel_.onClick = [this] { close(); };
        unlink_.setButtonText (owner.text ("facade.picker_unlink"));
        unlink_.onClick = [this] { if (onChoose_) onChoose_ (0); close(); };
        addAndMakeVisible (cancel_);
        addAndMakeVisible (unlink_);
        refilter();
        setWantsKeyboardFocus (true);
    }

    void visibilityChanged() override { if (isVisible()) filter_.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (owner_.colour (Token::PanelBg));
        g.fillRoundedRectangle (panel().toFloat(), 10.0f);
        g.setColour (owner_.colour (Token::Border));
        g.drawRoundedRectangle (panel().toFloat(), 10.0f, 1.5f);
    }

    void resized() override
    {
        auto r = panel().reduced (16);
        title_.setBounds (r.removeFromTop (30));
        hint_.setBounds (r.removeFromTop (36));
        filter_.setBounds (r.removeFromTop (32));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (32);
        r.removeFromBottom (8);
        list_.setBounds (r);
        cancel_.setBounds (bottom.removeFromRight (120));
        bottom.removeFromRight (8);
        unlink_.setBounds (bottom.removeFromRight (120));
    }

private:
    struct Entry { neptune::core::ParamId id; juce::String name; };

    juce::Rectangle<int> panel() const { return juce::Rectangle<int> (560, juce::jmin (620, getHeight() - 20)).withCentre (getLocalBounds().getCentre()); }

    void refilter()
    {
        shown_.clear();
        const auto words = juce::StringArray::fromTokens (filter_.getText().trim(), " ", "");
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            bool ok = true;
            for (const auto& w : words)
                if (w.isNotEmpty() && ! entries_[i].name.containsIgnoreCase (w)) { ok = false; break; }
            if (ok) shown_.push_back (i);
        }
        list_.updateContent();
        list_.selectRow (shown_.empty() ? -1 : 0);
        list_.repaint();
    }

    void choose (int row)
    {
        if (row < 0 || row >= static_cast<int> (shown_.size())) return;
        const auto id = entries_[shown_[static_cast<std::size_t> (row)]].id;
        if (onChoose_) onChoose_ (id);
        close();
    }

    void close()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FacadeView> (&owner_)]
        {
            if (safe != nullptr) { safe->picker_.reset(); if (safe->canvas_ != nullptr) safe->canvas_->grabKeyboardFocus(); }
        });
    }

    int getNumRows() override { return static_cast<int> (shown_.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (shown_.size())) return;
        if (selected) { g.setColour (owner_.colour (Token::Accent).withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
        g.setColour (owner_.colour (Token::Text));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (entries_[shown_[static_cast<std::size_t> (row)]].name, 8, 0, w - 16, h, juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }

    FacadeView& owner_;
    std::function<void (neptune::core::ParamId)> onChoose_;
    std::vector<Entry> entries_;
    std::vector<std::size_t> shown_;
    juce::Label title_, hint_;
    juce::TextEditor filter_;
    juce::ListBox list_;
    juce::TextButton cancel_, unlink_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Picker)
};

// ============================== Vue ==============================

FacadeView::FacadeView (NeptuneProcessor& p, const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr,
                        neptune::core::theme::KnobStyle style, NeptuneKnob::OpenModal openModal)
    : processor_ (p), palette_ (palette), tr_ (tr), knobStyle_ (style), openModal_ (std::move (openModal))
{
    canvas_ = std::make_unique<Canvas> (*this);
    addAndMakeVisible (*canvas_);

    editButton_.setButtonText (text ("facade.edit_layout"));
    editButton_.setClickingTogglesState (true);
    editButton_.onClick = [this]
    {
        editMode_ = editButton_.getToggleState();
        if (! editMode_) selected_ = 0;
        updateButtons();
        resized();
        canvas_->repaint();
    };
    knobButton_.setButtonText (text ("facade.add_knob"));
    knobButton_.onClick = [this] { addWidget (fc::WidgetType::Knob); };
    buttonButton_.setButtonText (text ("facade.add_button"));
    buttonButton_.onClick = [this] { addWidget (fc::WidgetType::Button); };
    labelButton_.setButtonText (text ("facade.add_label"));
    labelButton_.onClick = [this] { addWidget (fc::WidgetType::Label); };
    imageButton_.setButtonText (text ("facade.image"));
    imageButton_.onClick = [this] { chooseImage(); };
    fitButton_.setButtonText (text ("facade.fit_fit"));
    fitButton_.onClick = [this] { showFitMenu(); };
    clearImageButton_.setButtonText (text ("facade.image_clear"));
    clearImageButton_.onClick = [this]
    {
        auto f = facade_;
        f.image.clear();
        commit (std::move (f), "Facade: background");
    };
    linkButton_.setButtonText (text ("facade.link"));
    linkButton_.onClick = [this] { openPicker(); };
    textButton_.setButtonText (text ("facade.text"));
    textButton_.onClick = [this] { editText(); };
    duplicateButton_.setButtonText (text ("facade.duplicate"));
    duplicateButton_.onClick = [this] { duplicateSelected(); };
    deleteButton_.setButtonText (text ("facade.delete"));
    deleteButton_.onClick = [this] { deleteSelected(); };

    for (auto* b : { &editButton_, &knobButton_, &buttonButton_, &labelButton_, &imageButton_, &fitButton_, &clearImageButton_,
                     &linkButton_, &textButton_, &duplicateButton_, &deleteButton_ })
        addAndMakeVisible (*b);
    refresh();
    updateButtons();
    startTimerHz (15);
}

FacadeView::~FacadeView() { stopTimer(); }

juce::Colour FacadeView::colour (Token t) const { return toColour (palette_, t); }

void FacadeView::updateButtons()
{
    const bool selectedWidget = editMode_ && facade_.find (selected_) != nullptr;
    for (auto* b : { &knobButton_, &buttonButton_, &labelButton_, &imageButton_, &fitButton_, &clearImageButton_ }) b->setVisible (editMode_);
    for (auto* b : { &linkButton_, &textButton_, &duplicateButton_, &deleteButton_ }) b->setVisible (selectedWidget);
    if (const auto* w = facade_.find (selected_)) linkButton_.setEnabled (w->type != fc::WidgetType::Label);
    clearImageButton_.setEnabled (! facade_.image.empty());
    fitButton_.setButtonText (text (facade_.fit == fc::ImageFit::Fit ? "facade.fit_fit" : facade_.fit == fc::ImageFit::Stretch ? "facade.fit_stretch" : "facade.fit_fill"));
    editButton_.setToggleState (editMode_, juce::dontSendNotification);
}

void FacadeView::resized()
{
    static constexpr int rowH = 28, gap = 6, pad = 24;
    auto fit = [this] (const juce::TextButton& b)
    {
        return juce::roundToInt (std::ceil (getLookAndFeel().getTextButtonFont (const_cast<juce::TextButton&> (b), rowH).getStringWidthFloat (b.getButtonText()))) + pad;
    };
    auto area = getLocalBounds();
    auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<juce::TextButton*> buttons)
    {
        for (auto* b : buttons)
        {
            const int w = fit (*b);
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    };
    auto top = area.removeFromTop (rowH);
    layoutRow (top, { &editButton_, &knobButton_, &buttonButton_, &labelButton_, &imageButton_, &fitButton_, &clearImageButton_ });
    area.removeFromTop (gap);
    if (editMode_)
    {
        layoutRow (area.removeFromTop (rowH), { &linkButton_, &textButton_, &duplicateButton_, &deleteButton_ });
        area.removeFromTop (gap);
    }
    canvas_->setBounds (area);
    if (picker_ != nullptr) picker_->setBounds (getLocalBounds());
}

void FacadeView::paint (juce::Graphics&) {}

void FacadeView::timerCallback()
{
    if (! isShowing()) return;
    refresh();
    canvas_->repaint();   // les valeurs peuvent changer (automation, autre vue)
}

void FacadeView::refresh()
{
    const int t = processor_.selectedTrack();
    const auto rev = processor_.instrumentRevision();
    if (t == track_ && rev == revision_) return;
    if (canvas_->dragging()) return;
    if (t != track_) selected_ = 0;
    track_ = t;
    revision_ = rev;
    facade_ = processor_.trackSettings (t).facade;
    if (facade_.find (selected_) == nullptr) selected_ = 0;
    updateButtons();
    resized();
}

void FacadeView::commit (fc::Facade f, const char* undoName)
{
    if (track_ < 0) return;
    f = fc::sanitised (std::move (f));
    facade_ = f;
    auto s = processor_.trackSettings (track_);
    if (s.facade != f)
    {
        s.facade = f;
        processor_.uiEditTrack (track_, std::move (s), undoName);
        revision_ = processor_.instrumentRevision();
    }
    if (facade_.find (selected_) == nullptr) selected_ = 0;
    updateButtons();
    canvas_->repaint();
}

void FacadeView::addWidget (fc::WidgetType type)
{
    auto f = facade_;
    if (static_cast<int> (f.widgets.size()) >= fc::maxWidgets) return;
    // posé près du centre, décalé à chaque ajout pour ne pas empiler exactement
    const int n = static_cast<int> (f.widgets.size()) % 8;
    auto w = fc::defaultWidget (type, f.freeId(), fc::canvasWidth / 2 - 40 + 24 * n, fc::canvasHeight / 2 - 40 + 24 * n);
    if (type == fc::WidgetType::Label) w.text = "Label";
    selected_ = w.id;
    f.widgets.push_back (std::move (w));
    commit (std::move (f), "Facade: add widget");
}

void FacadeView::duplicateSelected()
{
    auto f = facade_;
    const auto* src = f.find (selected_);
    if (src == nullptr || static_cast<int> (f.widgets.size()) >= fc::maxWidgets) return;
    auto copy = *src;
    copy.id = f.freeId();
    copy.x += 24;
    copy.y += 24;
    selected_ = copy.id;
    f.widgets.push_back (std::move (copy));
    commit (std::move (f), "Facade: duplicate widget");
}

void FacadeView::deleteSelected()
{
    auto f = facade_;
    const auto it = std::find_if (f.widgets.begin(), f.widgets.end(), [this] (const fc::Widget& w) { return w.id == selected_; });
    if (it == f.widgets.end()) return;
    f.widgets.erase (it);
    selected_ = 0;
    commit (std::move (f), "Facade: delete widget");
}

void FacadeView::chooseImage()
{
    chooser_ = std::make_unique<juce::FileChooser> (text ("facade.choose_image"), juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<FacadeView> (this)] (const juce::FileChooser& c)
                           {
                               if (safe == nullptr) return;
                               const auto file = c.getResult();
                               if (! file.existsAsFile()) return;
                               auto f = safe->facade_;
                               f.image = file.getFullPathName().toStdString();
                               safe->commit (std::move (f), "Facade: background");
                           });
}

void FacadeView::showFitMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("facade.fit_fit"), true, facade_.fit == fc::ImageFit::Fit);
    menu.addItem (2, text ("facade.fit_stretch"), true, facade_.fit == fc::ImageFit::Stretch);
    menu.addItem (3, text ("facade.fit_fill"), true, facade_.fit == fc::ImageFit::Fill);
    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fitButton_), [safe = juce::Component::SafePointer<FacadeView> (this)] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        auto f = safe->facade_;
        f.fit = static_cast<fc::ImageFit> (choice - 1);
        safe->commit (std::move (f), "Facade: background fit");
    });
}

void FacadeView::openPicker()
{
    if (picker_ != nullptr || facade_.find (selected_) == nullptr) return;
    const int id = selected_;
    picker_ = std::make_unique<Picker> (*this, track_, [safe = juce::Component::SafePointer<FacadeView> (this), id] (neptune::core::ParamId param)
    {
        if (safe == nullptr) return;
        auto f = safe->facade_;
        if (auto* w = f.find (id)) { w->param = param; safe->commit (std::move (f), "Facade: link parameter"); }
    });
    addAndMakeVisible (*picker_);
    picker_->setBounds (getLocalBounds());
    picker_->toFront (true);
}

void FacadeView::editText()
{
    const auto* current = facade_.find (selected_);
    if (current == nullptr) return;
    const int id = selected_;
    auto* w = new juce::AlertWindow (text ("facade.text_title"), {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", utf8 (current->text), "");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<FacadeView> (this), w, id] (int result)
    {
        if (safe == nullptr || result != 1) return;
        auto f = safe->facade_;
        if (auto* widget = f.find (id)) { widget->text = w->getTextEditorContents ("t").toStdString(); safe->commit (std::move (f), "Facade: widget text"); }
    }), true);
}
