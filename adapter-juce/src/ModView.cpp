

#include <algorithm>

#include "neptune/core/Control.h"

using neptune::core::theme::Token;
namespace mod = neptune::core::modulation;

namespace
{
juce::String num (double v, int decimals) { return juce::String (v, decimals); }
}

// ------------------------------------------------------------------------------------------------ une route

class ModView::RouteRow : public juce::Component
{
public:
    RouteRow (ModView& owner, std::size_t index) : owner_ (owner), index_ (index)
    {
        const auto& r = owner.matrix_.routes[index];
        const char* kinds[6] = { "mod.src_cc", "mod.src_bend", "mod.src_aftertouch", "mod.src_macro", "mod.src_lfo", "mod.src_env" };
        for (int i = 0; i < 6; ++i) kind_.addItem (owner.text (kinds[i]), i + 1);
        kind_.onChange = [this] { kindChanged(); };
        addAndMakeVisible (kind_);
        indexBox_.onChange = [this] { if (! updating_) { edit().source.index = indexBox_.getSelectedId() - 1; owner_.commit(); } };
        addAndMakeVisible (indexBox_);
        channel_.addItem (owner.text ("mod.any_channel"), 1);
        for (int c = 1; c <= 16; ++c) channel_.addItem (juce::String (c), c + 1);
        channel_.onChange = [this] { if (! updating_) { edit().source.channel = channel_.getSelectedId() - 1; owner_.commit(); } };
        addAndMakeVisible (channel_);
        amount_.setSliderStyle (juce::Slider::LinearHorizontal);
        amount_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        amount_.setRange (-100.0, 100.0, 1.0);
        amount_.setTextValueSuffix (" %");
        amount_.setDoubleClickReturnValue (true, 0.0);
        amount_.onValueChange = [this] { if (! updating_) { edit().amount = amount_.getValue() / 100.0; owner_.commit(); } };
        addAndMakeVisible (amount_);
        centred_.setButtonText (owner.text ("mod.centred"));
        centred_.setTooltip (owner.text ("mod.tip_centred"));
        centred_.setColour (juce::ToggleButton::textColourId, owner.colour (Token::Text));
        centred_.onClick = [this] { if (! updating_) { edit().centred = centred_.getToggleState(); owner_.commit(); } };
        addAndMakeVisible (centred_);
        dest_.onClick = [this] { owner_.chooseDestination (index_); };
        addAndMakeVisible (dest_);
        remove_.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        remove_.setTooltip (owner.text ("mod.remove"));
        remove_.onClick = [this] { owner_.removeRoute (index_); };
        addAndMakeVisible (remove_);
        refresh (r);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        remove_.setBounds (r.removeFromRight (34));
        r.removeFromRight (6);
        dest_.setBounds (r.removeFromRight (300));
        r.removeFromRight (6);
        kind_.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (6);
        indexBox_.setBounds (r.removeFromLeft (90));
        r.removeFromLeft (6);
        channel_.setBounds (r.removeFromLeft (90));
        r.removeFromLeft (10);
        centred_.setBounds (r.removeFromRight (70));
        amount_.setBounds (r);
    }

    void refresh (const mod::Route& r)
    {
        updating_ = true;
        using SK = mod::SourceKind;
        int kindId = 5;   // LFO par défaut
        switch (r.source.kind)
        {
            case SK::ControlChange: kindId = 1; break;
            case SK::PitchBend: kindId = 2; break;
            case SK::Aftertouch: kindId = 3; break;
            case SK::Macro: kindId = 4; break;
            case SK::Lfo: kindId = 5; break;
            case SK::Envelope: kindId = 6; break;
            default: break;
        }
        kind_.setSelectedId (kindId, juce::dontSendNotification);
        fillIndex (r.source.kind);
        indexBox_.setSelectedId (r.source.index + 1, juce::dontSendNotification);
        channel_.setSelectedId (r.source.channel + 1, juce::dontSendNotification);
        amount_.setValue (r.amount * 100.0, juce::dontSendNotification);
        centred_.setToggleState (r.centred, juce::dontSendNotification);
        centred_.setEnabled (! mod::isBipolar (r.source.kind));
        const auto* spec = r.dest != 0 ? owner_.processor_.parameters().find (r.dest) : nullptr;
        dest_.setButtonText (spec != nullptr ? juce::String::fromUTF8 (spec->name.c_str()) : owner_.text ("mod.choose_dest"));
        updating_ = false;
    }

private:
    mod::Route& edit() { return owner_.matrix_.routes[index_]; }

    void fillIndex (mod::SourceKind kind)
    {
        indexBox_.clear (juce::dontSendNotification);
        using SK = mod::SourceKind;
        const bool hasIndex = kind == SK::ControlChange || kind == SK::Macro || kind == SK::Lfo || kind == SK::Envelope;
        const int count = kind == SK::ControlChange ? 128 : kind == SK::Macro ? neptune::core::control::kMacros : kind == SK::Lfo ? mod::kLfos : kind == SK::Envelope ? mod::kEnvelopes : 0;
        for (int i = 0; i < count; ++i) indexBox_.addItem (juce::String (kind == SK::ControlChange ? i : i + 1), i + 1);   // les CC se comptent à partir de 0
        indexBox_.setVisible (hasIndex);
        channel_.setVisible (kind == SK::ControlChange || kind == SK::PitchBend || kind == SK::Aftertouch);
    }

    void kindChanged()
    {
        if (updating_) return;
        using SK = mod::SourceKind;
        const SK kinds[6] = { SK::ControlChange, SK::PitchBend, SK::Aftertouch, SK::Macro, SK::Lfo, SK::Envelope };
        auto& r = edit();
        r.source.kind = kinds[std::clamp (kind_.getSelectedId() - 1, 0, 5)];
        r.source.index = 0;
        if (r.source.kind != SK::ControlChange && r.source.kind != SK::PitchBend && r.source.kind != SK::Aftertouch) r.source.channel = 0;
        refresh (r);
        owner_.commit();
    }

    ModView& owner_;
    std::size_t index_;
    bool updating_ = false;
    juce::ComboBox kind_, indexBox_, channel_;
    juce::Slider amount_;
    juce::ToggleButton centred_;
    juce::TextButton dest_, remove_;
};

// ------------------------------------------------------------------------------------------------ la vue

ModView::ModView (NeptuneProcessor& p, const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr)
    : processor_ (p), palette_ (palette), tr_ (tr)
{
    auto caption = [this] (juce::Label& l, const char* key)
    {
        l.setText (text (key), juce::dontSendNotification);
        l.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, colour (Token::Accent));
        addAndMakeVisible (l);
    };
    caption (lfoCaption_, "mod.lfos");
    caption (envCaption_, "mod.envelopes");
    caption (routesCaption_, "mod.routes");
    hint_.setText (text ("mod.matrix_hint"), juce::dontSendNotification);
    hint_.setFont (juce::FontOptions (13.0f));
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    addAndMakeVisible (hint_);

    auto field = [this] (juce::TextEditor& e, const char* tooltip)
    {
        e.setFont (juce::FontOptions (16.0f));
        e.setInputRestrictions (8, "-0123456789.");
        e.setTooltip (text (tooltip));
        addAndMakeVisible (e);
    };
    const char* shapes[mod::numLfoShapes] = { "mod.shape_sine", "mod.shape_triangle", "mod.shape_saw_up", "mod.shape_saw_down", "mod.shape_square", "mod.shape_sh" };
    for (int i = 0; i < mod::kLfos; ++i)
    {
        auto& row = lfoRows_[static_cast<std::size_t> (i)];
        row.label.setText ("LFO " + juce::String (i + 1), juce::dontSendNotification);
        row.label.setColour (juce::Label::textColourId, colour (Token::Text));
        addAndMakeVisible (row.label);
        for (int s = 0; s < mod::numLfoShapes; ++s) row.shape.addItem (text (shapes[s]), s + 1);
        row.shape.onChange = [this, i] { if (! updating_) { matrix_.lfos[static_cast<std::size_t> (i)].shape = static_cast<mod::LfoShape> (lfoRows_[static_cast<std::size_t> (i)].shape.getSelectedId() - 1); commit(); } };
        addAndMakeVisible (row.shape);
        field (row.rate, "mod.tip_rate");
        field (row.phase, "mod.tip_phase");
        row.rate.onReturnKey = row.rate.onFocusLost = [this, i] { if (! updating_) { matrix_.lfos[static_cast<std::size_t> (i)].rateHz = lfoRows_[static_cast<std::size_t> (i)].rate.getText().getDoubleValue(); commit(); } };
        row.phase.onReturnKey = row.phase.onFocusLost = [this, i] { if (! updating_) { matrix_.lfos[static_cast<std::size_t> (i)].phase = lfoRows_[static_cast<std::size_t> (i)].phase.getText().getDoubleValue() / 100.0; commit(); } };
    }
    for (int i = 0; i < mod::kEnvelopes; ++i)
    {
        auto& row = envRows_[static_cast<std::size_t> (i)];
        row.label.setText ("ENV " + juce::String (i + 1), juce::dontSendNotification);
        row.label.setColour (juce::Label::textColourId, colour (Token::Text));
        addAndMakeVisible (row.label);
        field (row.a, "mod.tip_attack");
        field (row.d, "mod.tip_decay");
        field (row.s, "mod.tip_sustain");
        field (row.r, "mod.tip_release");
        auto bind = [this, i] (juce::TextEditor& e, double mod::EnvDesc::* member, double scale)
        {
            e.onReturnKey = e.onFocusLost = [this, i, &e, member, scale] { if (! updating_) { matrix_.envelopes[static_cast<std::size_t> (i)].*member = e.getText().getDoubleValue() / scale; commit(); } };
        };
        bind (row.a, &mod::EnvDesc::attackMs, 1.0);
        bind (row.d, &mod::EnvDesc::decayMs, 1.0);
        bind (row.s, &mod::EnvDesc::sustain, 100.0);
        bind (row.r, &mod::EnvDesc::releaseMs, 1.0);
    }
    addButton_.setButtonText (text ("mod.add_route"));
    addButton_.onClick = [this] { addRoute(); };
    addAndMakeVisible (addButton_);
    addAndMakeVisible (view_);
    view_.setViewedComponent (&routesHolder_, false);
    view_.setScrollBarsShown (true, false);
    reload();
    startTimerHz (8);
}

ModView::~ModView() { stopTimer(); }

juce::Colour ModView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void ModView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (6 + 26 + 4 * 34 + 6);
    top.removeFromTop (6);
    auto left = top.removeFromLeft (top.getWidth() / 2 - 10);
    top.removeFromLeft (20);
    lfoCaption_.setBounds (left.removeFromTop (26));
    envCaption_.setBounds (top.removeFromTop (26));
    for (int i = 0; i < mod::kLfos; ++i)
    {
        auto row = left.removeFromTop (34);
        auto& l = lfoRows_[static_cast<std::size_t> (i)];
        l.label.setBounds (row.removeFromLeft (60));
        l.shape.setBounds (row.removeFromLeft (150).reduced (0, 2));
        row.removeFromLeft (8);
        l.rate.setBounds (row.removeFromLeft (80).reduced (0, 2));
        row.removeFromLeft (8);
        l.phase.setBounds (row.removeFromLeft (70).reduced (0, 2));
    }
    for (int i = 0; i < mod::kEnvelopes; ++i)
    {
        auto row = top.removeFromTop (34);
        auto& e = envRows_[static_cast<std::size_t> (i)];
        e.label.setBounds (row.removeFromLeft (60));
        for (auto* f : { &e.a, &e.d, &e.s, &e.r }) { f->setBounds (row.removeFromLeft (80).reduced (0, 2)); row.removeFromLeft (8); }
    }
    r.removeFromTop (6);
    auto head = r.removeFromTop (30);
    routesCaption_.setBounds (head.removeFromLeft (200));
    addButton_.setBounds (head.removeFromRight (160));
    hint_.setBounds (head);
    r.removeFromTop (4);
    view_.setBounds (r);
    layoutRoutes();
    if (search_ != nullptr) search_->setBounds (getLocalBounds());
}

void ModView::layoutRoutes()
{
    const int width = std::max (400, view_.getMaximumVisibleWidth());
    int y = 0;
    for (auto& row : rows_) { row->setBounds (0, y, width, 36); y += 38; }
    routesHolder_.setSize (width, std::max (y, 1));
}

void ModView::reload()
{
    track_ = processor_.selectedTrack();
    committed_ = processor_.trackSettings (track_).modulation;
    matrix_ = committed_;
    updating_ = true;
    for (int i = 0; i < mod::kLfos; ++i)
    {
        const auto& d = matrix_.lfos[static_cast<std::size_t> (i)];
        auto& row = lfoRows_[static_cast<std::size_t> (i)];
        row.shape.setSelectedId (static_cast<int> (d.shape) + 1, juce::dontSendNotification);
        row.rate.setText (num (d.rateHz, 2), false);
        row.phase.setText (num (d.phase * 100.0, 0), false);
    }
    for (int i = 0; i < mod::kEnvelopes; ++i)
    {
        const auto& d = matrix_.envelopes[static_cast<std::size_t> (i)];
        auto& row = envRows_[static_cast<std::size_t> (i)];
        row.a.setText (num (d.attackMs, 0), false);
        row.d.setText (num (d.decayMs, 0), false);
        row.s.setText (num (d.sustain * 100.0, 0), false);
        row.r.setText (num (d.releaseMs, 0), false);
    }
    updating_ = false;
    rebuildRoutes();
}

void ModView::rebuildRoutes()
{
    rows_.clear();
    for (std::size_t i = 0; i < matrix_.routes.size(); ++i)
    {
        rows_.push_back (std::make_unique<RouteRow> (*this, i));
        routesHolder_.addAndMakeVisible (*rows_.back());
    }
    addButton_.setEnabled (matrix_.routes.size() < mod::kMaxRoutes);
    layoutRoutes();
}

void ModView::commit()
{
    committed_ = mod::sanitised (matrix_);
    auto strip = processor_.trackSettings (track_);
    strip.modulation = committed_;
    processor_.uiEditTrack (track_, std::move (strip), "Modulation", "modulation");
}

void ModView::timerCallback()
{
    if (! isShowing()) return;
    if (processor_.selectedTrack() != track_) { reload(); return; }
    if (processor_.trackSettings (track_).modulation != committed_) reload();   // annulation, chargement... : la matrice a changé ailleurs
}

void ModView::addRoute()
{
    if (matrix_.routes.size() >= mod::kMaxRoutes) return;
    mod::Route r;
    r.source = { mod::SourceKind::Lfo, 0, 0 };
    matrix_.routes.push_back (r);
    rebuildRoutes();
    chooseDestination (matrix_.routes.size() - 1);
}

void ModView::removeRoute (std::size_t index)
{
    if (index >= matrix_.routes.size()) return;
    matrix_.routes.erase (matrix_.routes.begin() + static_cast<std::ptrdiff_t> (index));
    commit();
    rebuildRoutes();
}

void ModView::chooseDestination (std::size_t index)
{
    if (search_ != nullptr || index >= matrix_.routes.size()) return;
    search_ = std::make_unique<ParamSearchPanel> (processor_.parameters(), palette_, tr_, track_, true,
        [safe = juce::Component::SafePointer<ModView> (this), index] (neptune::core::ParamId id)
        {
            if (safe == nullptr || index >= safe->matrix_.routes.size()) return;
            safe->matrix_.routes[index].dest = id;
            safe->commit();
            safe->rebuildRoutes();
        },
        [safe = juce::Component::SafePointer<ModView> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->search_.reset(); });
        });
    addAndMakeVisible (*search_);
    search_->setBounds (getLocalBounds());
    search_->toFront (true);
}
