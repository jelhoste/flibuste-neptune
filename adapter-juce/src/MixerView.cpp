#include "MixerView.h"
#include <cmath>
#include "neptune/core/ProbePool.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kStripW = 58;
constexpr int kMinStripH = 300, kMaxStripH = 360;
constexpr int kInsertRowH = 16, kVisibleInserts = 4;
constexpr int kMasterW = 112;
constexpr double kFaderMinDb = -100.0, kFaderMaxDb = 6.0;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// Curseur lié à un paramètre : début / valeur / fin de geste passent par le processeur (annulation + automation de l'hôte).
struct BoundSlider : juce::Slider
{
    BoundSlider (NeptuneProcessor& p, juce::Slider::SliderStyle style) : juce::Slider (style, juce::Slider::NoTextBox), proc (p)
    {
        onDragStart = [this] { dragging = true; if (id != 0) proc.uiBeginEdit (id); };
        onDragEnd = [this] { dragging = false; if (id != 0) proc.uiEndEdit (id); };
        onValueChange = [this]
        {
            if (updating || id == 0) return;
            const auto* spec = proc.parameters().find (id);
            if (spec == nullptr) return;
            if (! dragging) proc.uiBeginEdit (id); // changement ponctuel (molette, clavier) : un geste complet
            proc.uiSetNormalised (id, spec->toNormalized (getValue()));
            if (! dragging) proc.uiEndEdit (id);
        };
    }
    void bind (ParamId pid, double resetValue)
    {
        id = pid;
        setDoubleClickReturnValue (true, resetValue);
    }
    void sync()
    {
        if (id == 0 || dragging || isMouseButtonDown()) return;
        updating = true;
        setValue (proc.parameters().get (id), juce::dontSendNotification);
        updating = false;
    }

    NeptuneProcessor& proc;
    ParamId id = 0;
    bool dragging = false, updating = false;
};

// Bouton à bascule lié à un paramètre 0/1 (mute, solo, mono, parallèle). Dessiné sans les marges du style par défaut : une lettre
// tient dans un tiers de bande (le style par défaut tronquait « M », « S » et « P » en « ... »).
struct BoundToggle : juce::TextButton
{
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        auto fill = findColour (on ? buttonOnColourId : buttonColourId);
        if (down) fill = fill.brighter (0.2f); else if (over) fill = fill.brighter (0.1f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (findColour (juce::ComboBox::outlineColourId).withAlpha (0.8f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (findColour (on ? textColourOnId : textColourOffId));
        g.setFont (juce::FontOptions (juce::jmin (12.0f, r.getHeight() * 0.6f), juce::Font::bold));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }

    BoundToggle (NeptuneProcessor& p, const juce::String& label) : proc (p)
    {
        setButtonText (label);
        setClickingTogglesState (false);
        onClick = [this] { if (onToggle) onToggle (juce::ModifierKeys::currentModifiers.isShiftDown()); };
    }
    void sync()
    {
        const bool on = id != 0 && proc.parameters().get (id) >= 0.5;
        if (on != getToggleState()) setToggleState (on, juce::dontSendNotification);
    }
    void setValueFromUi (bool on)
    {
        if (id == 0) return;
        const auto* spec = proc.parameters().find (id);
        if (spec == nullptr) return;
        proc.uiBeginEdit (id);
        proc.uiSetNormalised (id, spec->toNormalized (on ? 1.0 : 0.0));
        proc.uiEndEdit (id);
    }
    NeptuneProcessor& proc;
    ParamId id = 0;
    std::function<void (bool shift)> onToggle;
};

float dbOf (float lin) { return lin <= 1e-5f ? -100.0f : 20.0f * std::log10 (lin); }
} // namespace

// ------------------------------------------------------------------------------------------------ fenêtre de routage

// Routage d'une bande : sortie principale, sortie directe, mode de pan, 8 envois (bus, point de captation, niveau, pan, mute).
class RoutingPanel : public juce::Component
{
public:
    // kind : 0 piste, 1 bus, 2 piste parallèle
    RoutingPanel (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr, int kind, int index)
        : proc_ (p), palette_ (palette), tr_ (tr), isBus_ (kind == 1), isPar_ (kind == 2), index_ (index)
    {
        const bool isBus = isBus_;
        if (isPar_) par_ = p.parallelSettings (index);
        s_ = isBus ? p.busSettings (index) : isPar_ ? par_.strip : p.trackSettings (index);
        auto text = [this] (const char* k) { return u8 (std::string (tr_.tr (k))); };
        auto label = [this] (juce::Label& l, const juce::String& t)
        {
            l.setText (t, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, colour (Token::Text));
            addAndMakeVisible (l);
        };
        label (mainLabel_, text (isPar_ ? "mix.sum_parent_title" : "mix.main_out"));
        label (directLabel_, text ("mix.direct_out"));
        label (panLabel_, text ("mix.pan_mode"));
        mainBox_.addItem ("Master", 1);
        for (int b = 1; b <= mixer::numBuses; ++b)
        {
            mainBox_.addItem ("Bus " + juce::String (b), b + 1);
            if (isBus_ && b <= index_ + 1) mainBox_.setItemEnabled (b + 1, false); // un bus ne route que vers un bus supérieur
        }
        mainBox_.setSelectedId (s_.routing.mainDest + 1, juce::dontSendNotification);
        mainBox_.onChange = [this] { s_.routing.mainDest = mainBox_.getSelectedId() - 1; push(); };
        addAndMakeVisible (mainBox_);
        mainBox_.setVisible (! isPar_);
        mainOn_.setButtonText (text (isPar_ ? "mix.sum_parent" : "mix.main_on"));
        mainOn_.setToggleState (s_.routing.mainEnabled, juce::dontSendNotification);
        mainOn_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        mainOn_.onClick = [this] { s_.routing.mainEnabled = mainOn_.getToggleState(); push(); };
        addAndMakeVisible (mainOn_);
        directBox_.addItem (text ("mix.none"), 1);
        for (int o = 1; o <= mixer::numOutputs; ++o) directBox_.addItem (text ("mix.output") + " " + juce::String (o), o + 1);
        directBox_.setSelectedId (s_.routing.directOut + 1, juce::dontSendNotification);
        directBox_.onChange = [this] { s_.routing.directOut = directBox_.getSelectedId() - 1; push(); };
        addAndMakeVisible (directBox_);
        panBox_.addItem (text ("mix.pan_balance"), 1);
        panBox_.addItem (text ("mix.pan_true"), 2);
        panBox_.setSelectedId (s_.panMode == mixer::PanMode::True ? 2 : 1, juce::dontSendNotification);
        panBox_.onChange = [this] { s_.panMode = panBox_.getSelectedId() == 2 ? mixer::PanMode::True : mixer::PanMode::Balance; push(); };
        addAndMakeVisible (panBox_);
        if (isPar_)
        {
            follow_.setButtonText (text ("mix.follow_parent"));
            follow_.setToggleState (par_.followParentVolume, juce::dontSendNotification);
            follow_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
            follow_.onClick = [this]
            {
                par_.strip = s_;
                par_.followParentVolume = follow_.getToggleState();
                proc_.uiEditParallel (index_, par_, "Parallel follow volume", "follow");
            };
            addAndMakeVisible (follow_);
        }

        for (int i = 0; i < mixer::numSends; ++i)
        {
            auto& r = rows_[static_cast<std::size_t> (i)];
            r.title.setText (text ("mix.send") + " " + juce::String (i + 1), juce::dontSendNotification);
            r.title.setColour (juce::Label::textColourId, colour (Token::TextDim));
            addAndMakeVisible (r.title);
            r.dest.addItem (text ("mix.none"), 1);
            for (int b = 1; b <= mixer::numBuses; ++b)
            {
                r.dest.addItem ("Bus " + juce::String (b), b + 1);
                if (isBus_ && b <= index_ + 1) r.dest.setItemEnabled (b + 1, false);
            }
            r.dest.setSelectedId (s_.sends[static_cast<std::size_t> (i)].destination + 1, juce::dontSendNotification);
            r.dest.onChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].destination = rows_[static_cast<std::size_t> (i)].dest.getSelectedId() - 1; push(); };
            addAndMakeVisible (r.dest);
            r.tap.addItem (text ("mix.tap_pre_insert"), 1);
            r.tap.addItem (text ("mix.tap_pre_fader"), 2);
            r.tap.addItem (text ("mix.tap_post_fader"), 3);
            r.tap.addItem (text ("mix.tap_post_pan"), 4);
            r.tap.setSelectedId (static_cast<int> (s_.sends[static_cast<std::size_t> (i)].tap) + 1, juce::dontSendNotification);
            r.tap.onChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].tap = static_cast<mixer::TapPoint> (rows_[static_cast<std::size_t> (i)].tap.getSelectedId() - 1); push(); };
            addAndMakeVisible (r.tap);
            const ParamId lid = isBus_ ? mixer::busParam (index_, mixer::kBusSendLevel + static_cast<ParamId> (i))
                              : isPar_ ? mixer::parallelParam (index_, mixer::kParSendLevel + static_cast<ParamId> (i))
                                       : probe::trackParam (index_, probe::kSendLevel + static_cast<ParamId> (i));
            r.level = std::make_unique<BoundSlider> (proc_, juce::Slider::LinearHorizontal);
            r.level->setRange (kFaderMinDb, 6.0, 0.1);
            r.level->setSkewFactorFromMidPoint (-24.0);
            r.level->bind (lid, -100.0);
            r.level->sync();
            addAndMakeVisible (*r.level);
            r.pan.setSliderStyle (juce::Slider::LinearHorizontal);
            r.pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            r.pan.setRange (-1.0, 1.0, 0.01);
            r.pan.setDoubleClickReturnValue (true, 0.0);
            r.pan.setValue (s_.sends[static_cast<std::size_t> (i)].pan, juce::dontSendNotification);
            r.pan.onValueChange = [this, i] { s_.sends[static_cast<std::size_t> (i)].pan = rows_[static_cast<std::size_t> (i)].pan.getValue(); push(); };
            addAndMakeVisible (r.pan);
            r.mute.setButtonText ("M");
            r.mute.setClickingTogglesState (true);
            r.mute.setToggleState (s_.sends[static_cast<std::size_t> (i)].mute, juce::dontSendNotification);
            r.mute.onClick = [this, i] { s_.sends[static_cast<std::size_t> (i)].mute = rows_[static_cast<std::size_t> (i)].mute.getToggleState(); push(); };
            addAndMakeVisible (r.mute);
        }
        setSize (620, isPar_ ? 432 : 400);
    }

    void paint (juce::Graphics& g) override { g.fillAll (colour (Token::BgCard)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto row = [&r] (juce::Label& l, juce::Component& c, int lw = 130)
        {
            auto line = r.removeFromTop (30);
            l.setBounds (line.removeFromLeft (lw));
            c.setBounds (line.removeFromLeft (200).reduced (2));
            return line;
        };
        auto extra = row (mainLabel_, mainBox_);
        mainOn_.setBounds (extra.reduced (2));
        row (directLabel_, directBox_);
        row (panLabel_, panBox_);
        if (isPar_) follow_.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        for (auto& row2 : rows_)
        {
            auto line = r.removeFromTop (30);
            row2.title.setBounds (line.removeFromLeft (60));
            row2.dest.setBounds (line.removeFromLeft (100).reduced (1));
            row2.tap.setBounds (line.removeFromLeft (130).reduced (1));
            row2.level->setBounds (line.removeFromLeft (150).reduced (1));
            row2.pan.setBounds (line.removeFromLeft (90).reduced (1));
            row2.mute.setBounds (line.removeFromLeft (30).reduced (1));
        }
    }

    void refreshLevels() { for (auto& r : rows_) r.level->sync(); }

private:
    juce::Colour colour (Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    void push()
    {
        if (isPar_)
        {
            par_ = proc_.parallelSettings (index_); // garde la captation et le suivi tels qu'ils sont ailleurs
            par_.strip = s_;
            proc_.uiEditParallel (index_, par_, "Parallel routing", "routing");
        }
        else if (isBus_) proc_.uiEditBus (index_, s_, "Bus routing", "routing");
        else proc_.uiEditTrack (index_, s_, "Track routing", "routing");
    }

    struct Row
    {
        juce::Label title;
        juce::ComboBox dest, tap;
        std::unique_ptr<BoundSlider> level;
        juce::Slider pan;
        juce::TextButton mute;
    };

    NeptuneProcessor& proc_;
    theme::Palette palette_;
    const i18n::Translator& tr_;
    bool isBus_;
    bool isPar_ = false;
    int index_;
    mixer::StripSettings s_;
    mixer::ParallelSettings par_;
    juce::ToggleButton follow_;
    juce::Label mainLabel_, directLabel_, panLabel_;
    juce::ComboBox mainBox_, directBox_, panBox_;
    juce::ToggleButton mainOn_;
    std::array<Row, mixer::numSends> rows_;
};

// ------------------------------------------------------------------------------------------------ inserts

// Les 8 emplacements d'inserts d'une bande : 4 lignes visibles, les 4 suivantes par la molette ou la flèche. Vide : « + ».
class MixerView::InsertList : public juce::Component
{
public:
    InsertList (MixerView& owner, NeptuneProcessor::StripKind kind, int index) : owner_ (owner), kind_ (kind), index_ (index) {}

    static int heightNeeded() { return kInsertRowH * kVisibleInserts + 2; }

    void refresh()
    {
        bool changed = false;
        for (int s = 0; s < mixer::numInserts; ++s)
        {
            const auto type = owner_.processor_.insertType (kind_, index_, s);
            const bool by = ! type.empty() && owner_.processor_.insertBypassed (kind_, index_, s);
            if (type != types_[static_cast<std::size_t> (s)] || by != bypassed_[static_cast<std::size_t> (s)]) changed = true;
            types_[static_cast<std::size_t> (s)] = type;
            bypassed_[static_cast<std::size_t> (s)] = by;
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (10.0f));
        for (int row = 0; row < kVisibleInserts; ++row)
        {
            const int slot = page_ * kVisibleInserts + row;
            const auto r = rowRect (row);
            const bool used = ! types_[static_cast<std::size_t> (slot)].empty();
            g.setColour (owner_.colour (used ? Token::BgApp : Token::BgCard));
            g.fillRect (r);
            g.setColour (owner_.colour (Token::Border));
            g.drawRect (r);
            g.setColour (owner_.colour (used ? (bypassed_[static_cast<std::size_t> (slot)] ? Token::TextDim2 : Token::Accent) : Token::TextDim));
            g.drawText (used ? name (types_[static_cast<std::size_t> (slot)]) : juce::String ("+"), r.reduced (2, 0), juce::Justification::centred);
        }
        // pagination : 1-4 / 5-8
        g.setColour (owner_.colour (Token::TextDim));
        const auto a = arrowRect();
        juce::Path tri;
        if (page_ == 0) tri.addTriangle ((float) a.getX() + 1, (float) a.getY() + 4, (float) a.getRight() - 1, (float) a.getY() + 4, a.getCentreX(), (float) a.getBottom() - 4);
        else tri.addTriangle ((float) a.getX() + 1, (float) a.getBottom() - 4, (float) a.getRight() - 1, (float) a.getBottom() - 4, a.getCentreX(), (float) a.getY() + 4);
        g.fillPath (tri);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (arrowRect().contains (e.getPosition())) { page_ = 1 - page_; repaint(); return; }
        const int row = e.y / kInsertRowH;
        if (row < 0 || row >= kVisibleInserts) return;
        const int slot = page_ * kVisibleInserts + row;
        const bool used = ! types_[static_cast<std::size_t> (slot)].empty();
        if (e.mods.isPopupMenu() && used) { showMenu (slot); return; }
        if (used) { if (owner_.onOpenInsert) owner_.onOpenInsert (kind_, index_, slot); }
        else showMenu (slot);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int next = w.deltaY < 0 ? 1 : 0;
        if (w.deltaY != 0.0f && next != page_) { page_ = next; repaint(); }
    }

private:
    juce::Rectangle<int> rowRect (int row) const { return { 0, row * kInsertRowH, getWidth() - 12, kInsertRowH }; }
    juce::Rectangle<int> arrowRect() const { return { getWidth() - 12, 0, 12, kInsertRowH * kVisibleInserts }; }

    juce::String name (const std::string& type) const
    {
        for (const auto& m : availableModules())
            if (m.typeId == type) return u8 (std::string (owner_.tr_.tr (m.nameKey)));
        return u8 (type);
    }

    void showMenu (int slot)
    {
        const bool used = ! types_[static_cast<std::size_t> (slot)].empty();
        juce::PopupMenu menu;
        const auto& modules = availableModules();
        if (! used)
        {
            for (std::size_t i = 0; i < modules.size(); ++i)
                menu.addItem (static_cast<int> (i) + 1, u8 (std::string (owner_.tr_.tr (modules[i].nameKey))));
        }
        else
        {
            menu.addItem (1, u8 (std::string (owner_.tr_.tr ("insert.open"))));
            menu.addItem (2, u8 (std::string (owner_.tr_.tr ("insert.bypass"))), true, bypassed_[static_cast<std::size_t> (slot)]);
            menu.addItem (3, u8 (std::string (owner_.tr_.tr ("insert.remove"))));
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<InsertList> (this), slot, used] (int choice)
                            {
                                if (safe == nullptr || choice == 0) return;
                                auto& proc = safe->owner_.processor_;
                                const auto kind = safe->kind_;
                                const int index = safe->index_;
                                if (! used)
                                {
                                    const auto& mods = availableModules();
                                    if (choice < 1 || choice > static_cast<int> (mods.size())) return;
                                    proc.uiSetInsert (kind, index, slot, std::string (mods[static_cast<std::size_t> (choice - 1)].typeId));
                                    safe->refresh();
                                    if (safe->owner_.onOpenInsert) safe->owner_.onOpenInsert (kind, index, slot);
                                }
                                else if (choice == 1) { if (safe->owner_.onOpenInsert) safe->owner_.onOpenInsert (kind, index, slot); }
                                else if (choice == 2)
                                {
                                    const auto id = proc.insertBypassParam (kind, index, slot);
                                    const auto* spec = proc.parameters().find (id);
                                    if (spec == nullptr) return;
                                    proc.uiBeginEdit (id);
                                    proc.uiSetNormalised (id, spec->toNormalized (proc.parameters().get (id) < 0.5 ? 1.0 : 0.0));
                                    proc.uiEndEdit (id);
                                }
                                else if (choice == 3)
                                {
                                    proc.uiSetInsert (kind, index, slot, std::string());
                                    safe->refresh();
                                }
                            });
    }

    MixerView& owner_;
    NeptuneProcessor::StripKind kind_;
    int index_;
    int page_ = 0;
    std::array<std::string, mixer::numInserts> types_;
    std::array<bool, mixer::numInserts> bypassed_ {};
};

// ------------------------------------------------------------------------------------------------ une bande

class MixerView::Strip : public juce::Component
{
public:
    // kind : 0 piste, 1 bus, 2 piste parallèle
    Strip (MixerView& owner, int kind, int index)
        : owner_ (owner), isBus_ (kind == 1), isPar_ (kind == 2), index_ (index),
          pan_ (owner.processor_, juce::Slider::LinearHorizontal), fader_ (owner.processor_, juce::Slider::LinearVertical),
          mute_ (owner.processor_, "M"), solo_ (owner.processor_, "S"), parallel_ (owner.processor_, "P"),
          inserts_ (owner, kind == 1 ? NeptuneProcessor::StripKind::Bus : kind == 2 ? NeptuneProcessor::StripKind::Parallel : NeptuneProcessor::StripKind::Track, index)
    {
        const bool isBus = isBus_;
        auto& proc = owner.processor_;
        addAndMakeVisible (inserts_);
        name_.setJustificationType (juce::Justification::centred);
        name_.setFont (juce::FontOptions (11.0f));
        name_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        name_.setEditable (false, true, false);
        name_.onTextChange = [this]
        {
            auto s = settings();
            s.name = name_.getText().toStdString();
            commit (s);
        };
        name_.addMouseListener (this, false);
        addAndMakeVisible (name_);

        if (isPar_)
        {
            // en-tête : la parenté, le repliement des petites-filles et le retrait ; en dessous, le point de captation
            typeButton_.setEnabled (false);
            addAndMakeVisible (typeButton_);
            removeButton_.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
            removeButton_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.remove_parallel_tip"))));
            removeButton_.onClick = [this] { owner_.requestRemoveParallel (index_); };
            addAndMakeVisible (removeButton_);
            foldButton_.onClick = [this] { owner_.toggleFold (index_); };
            addAndMakeVisible (foldButton_);
            const char* taps[4] = { "mix.tap_pre_insert", "mix.tap_pre_fader", "mix.tap_post_fader", "mix.tap_post_pan" };
            for (int i = 0; i < 4; ++i) tapBox_.addItem (u8 (std::string (owner.tr_.tr (taps[i]))), i + 1);
            tapBox_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.capture_tip"))));
            tapBox_.onChange = [this]
            {
                auto ps = owner_.processor_.parallelSettings (index_);
                ps.tap = static_cast<mixer::TapPoint> (tapBox_.getSelectedId() - 1);
                owner_.processor_.uiEditParallel (index_, ps, "Parallel capture", "capture");
            };
            addAndMakeVisible (tapBox_);
        }
        if (! isBus && ! isPar_)
        {
            typeButton_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.type_tip"))));
            typeButton_.onClick = [this] { showTypeMenu(); };
            addAndMakeVisible (typeButton_);
            removeButton_.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
            removeButton_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.remove_tip"))));
            removeButton_.onClick = [this] { owner_.requestRemove (index_); };
            addAndMakeVisible (removeButton_);
            for (const auto* item : { "All" }) midi_.addItem (item, 1);
            for (int c = 1; c <= 16; ++c) midi_.addItem (juce::String (c), c + 1);
            midi_.onChange = [this]
            {
                auto s = settings();
                s.midiChannel = midi_.getSelectedId() - 1;
                commit (s);
            };
            addAndMakeVisible (midi_);
        }
        routing_.setButtonText (juce::String::fromUTF8 (std::string (owner.tr_.tr ("mix.routing_short")).c_str()));
        routing_.onClick = [this]
        {
            auto panel = std::make_unique<RoutingPanel> (owner_.processor_, owner_.palette_, owner_.tr_, isBus_ ? 1 : isPar_ ? 2 : 0, index_);
            panelRef_ = panel.get();
            juce::CallOutBox::launchAsynchronously (std::move (panel), routing_.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (routing_);

        pan_.setRange (-100.0, 100.0, 1.0);
        fader_.setRange (kFaderMinDb, kFaderMaxDb, 0.1);
        fader_.setSkewFactorFromMidPoint (-24.0);
        const ParamId pv = isBus ? mixer::busParam (index, mixer::kBusVolume) : isPar_ ? mixer::parallelParam (index, mixer::kParVolume) : probe::trackParam (index, probe::kVolume);
        const ParamId pp = isBus ? mixer::busParam (index, mixer::kBusPan) : isPar_ ? mixer::parallelParam (index, mixer::kParPan) : probe::trackParam (index, probe::kPan);
        const ParamId pm = isBus ? mixer::busParam (index, mixer::kBusMute) : isPar_ ? mixer::parallelParam (index, mixer::kParMute) : probe::trackParam (index, probe::kMute);
        const ParamId ps = isBus ? mixer::busParam (index, mixer::kBusSolo) : isPar_ ? mixer::parallelParam (index, mixer::kParSolo) : probe::trackParam (index, probe::kSolo);
        pan_.bind (pp, 0.0);
        fader_.bind (pv, 0.0);
        addAndMakeVisible (pan_);
        addAndMakeVisible (fader_);
        mute_.id = pm;
        solo_.id = ps;
        mute_.onToggle = [this, &proc] (bool)
        {
            mute_.setValueFromUi (proc.parameters().get (mute_.id) < 0.5);
        };
        solo_.onToggle = [this, &proc] (bool shift)
        {
            const bool turnOn = proc.parameters().get (solo_.id) < 0.5;
            if (shift && turnOn) owner_.exclusiveSolo (isBus_ ? 1 : isPar_ ? 2 : 0, index_); // solo exclusif : annule les autres solos
            solo_.setValueFromUi (turnOn);
        };
        mute_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::KnobHot));
        solo_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::Accent));
        addAndMakeVisible (mute_);
        addAndMakeVisible (solo_);
        if (! isBus_)
        {
            // « P » : crée une piste parallèle à droite de cette bande, dans sa chaîne
            parallel_.setTooltip (u8 (std::string (owner.tr_.tr ("mix.add_parallel_tip"))));
            parallel_.onToggle = [this] (bool) { owner_.requestAddParallel (isPar_ ? mixer::numTracks + index_ : index_); };
            addAndMakeVisible (parallel_);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isBus_ && ! isPar_ && owner_.onSelectTrack && e.eventComponent != &fader_ && e.eventComponent != &pan_) owner_.onSelectTrack (index_);
    }

    void refresh (bool selected)
    {
        selected_ = selected;
        if (isPar_)
        {
            const auto ps = owner_.processor_.parallelSettings (index_);
            const juce::String parent = ps.parent < mixer::numTracks ? juce::String (ps.parent + 1) : "P" + juce::String (ps.parent - mixer::numTracks + 1);
            typeButton_.setButtonText ("P" + juce::String (index_ + 1) + " <" + parent);
            tapBox_.setSelectedId (static_cast<int> (ps.tap) + 1, juce::dontSendNotification);
            int kids = 0;
            for (const int c : owner_.processor_.parallelChildren (mixer::numTracks + index_)) kids += 1 + static_cast<int> (owner_.processor_.parallelChildren (mixer::numTracks + c).size());
            hasFold_ = owner_.processor_.parallelDepth (index_) >= 1 && ! owner_.processor_.parallelChildren (mixer::numTracks + index_).empty();
            foldButton_.setButtonText ((owner_.isExpanded (index_) ? juce::String::fromUTF8 ("\xe2\x96\xbe") : juce::String::fromUTF8 ("\xe2\x96\xb8")) + juce::String (kids));
            foldButton_.setVisible (hasFold_);
            depth_ = owner_.processor_.parallelDepth (index_);
        }
        if (! isBus_ && ! isPar_)
        {
            const auto type = owner_.processor_.trackType (index_);
            const char* key = type == TrackType::Fm ? "mix.type_short_fm" : type == TrackType::Synth ? "mix.type_short_synth" : "mix.type_short_sampler";
            typeButton_.setButtonText (u8 (std::string (owner_.tr_.tr (key))));
        }
        const auto s = settings();
        const juce::String fallback = (isBus_ ? "Bus " : isPar_ ? "P" : "") + juce::String (index_ + 1);
        name_.setText (s.name.empty() ? fallback : u8 (s.name), juce::dontSendNotification);
        if (! isBus_ && ! isPar_) midi_.setSelectedId (s.midiChannel + 1, juce::dontSendNotification);
        colourIndex_ = s.color;
        repaint();
    }

    void syncValues()
    {
        pan_.sync();
        fader_.sync();
        mute_.sync();
        solo_.sync();
        inserts_.refresh();
        if (panelRef_ != nullptr) panelRef_->refreshLevels();
    }

    void updateMeter (const Engine::Meter& m)
    {
        auto fall = [] (float current, float incoming) { return incoming > current ? incoming : current * 0.88f; };
        peakL_ = fall (peakL_, m.peakL);
        peakR_ = fall (peakR_, m.peakR);
        repaint (meterArea());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        auto head = r.removeFromTop (24);
        if (! isBus_)
        {
            removeButton_.setBounds (head.removeFromRight (20).reduced (0, 1));
            if (isPar_ && hasFold_) foldButton_.setBounds (head.removeFromRight (26).reduced (0, 1));
            typeButton_.setBounds (head.reduced (0, 1));
        }
        name_.setBounds (r.removeFromTop (18));
        if (isPar_) tapBox_.setBounds (r.removeFromTop (22).reduced (0, 1));
        else if (! isBus_) midi_.setBounds (r.removeFromTop (22).reduced (0, 1));
        routing_.setBounds (r.removeFromTop (22).reduced (0, 1));
        inserts_.setBounds (r.removeFromTop (InsertList::heightNeeded() + 2).reduced (0, 1));
        pan_.setBounds (r.removeFromTop (20));
        auto bottom = r.removeFromBottom (24);
        if (isBus_)
        {
            mute_.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1));
            solo_.setBounds (bottom.reduced (1));
        }
        else
        {
            const int w = bottom.getWidth() / 3;
            mute_.setBounds (bottom.removeFromLeft (w).reduced (1));
            solo_.setBounds (bottom.removeFromLeft (w).reduced (1));
            parallel_.setBounds (bottom.reduced (1));
        }
        r.removeFromBottom (16); // valeur en dB
        faderArea_ = r;
        fader_.setBounds (r.removeFromLeft (r.getWidth() - 14));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (owner_.colour (selected_ ? Token::Nav1 : (isPar_ ? Token::BgApp : Token::BgCard)));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (owner_.colour (selected_ ? Token::Accent : Token::Border));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, selected_ ? 2.0f : 1.0f);
        if (isPar_) // une piste parallèle : bord d'accent à gauche, plus marqué en profondeur
        {
            g.setColour (owner_.colour (Token::Accent).withAlpha (depth_ >= 2 ? 0.55f : 1.0f));
            g.fillRect (0, 6, 3, getHeight() - 12);
        }
        if (isBus_) // le haut d'une bande de bus : l'étiquette que les pistes remplacent par leur générateur
        {
            g.setColour (owner_.colour (Token::TextDim));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("BUS", 2, 6, getWidth() - 4, 14, juce::Justification::centred);
        }
        // pastille de couleur
        static const juce::uint32 palette[16] = { 0xff5c6bc0, 0xff26a69a, 0xffffa726, 0xffef5350, 0xff8d6e63, 0xff66bb6a, 0xffab47bc, 0xff42a5f5,
                                                 0xffffee58, 0xffec407a, 0xff78909c, 0xff26c6da, 0xffd4e157, 0xffff7043, 0xff7e57c2, 0xffbdbdbd };
        g.setColour (juce::Colour (palette[colourIndex_ & 15]));
        g.fillRect (juce::Rectangle<int> (4, 3, getWidth() - 8, 3));

        // vu-mètre stéréo
        const auto m = meterArea();
        auto bar = [&] (float lin, int x)
        {
            const float db = dbOf (lin);
            const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            g.setColour (owner_.colour (Token::Border));
            g.fillRect (x, m.getY(), 5, m.getHeight());
            g.setColour (db > 0.0f ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent2));
            g.fillRect (x, m.getBottom() - static_cast<int> (h), 5, static_cast<int> (h));
        };
        bar (peakL_, m.getX());
        bar (peakR_, m.getX() + 6);
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (fader_.getValue() <= kFaderMinDb ? juce::String::fromUTF8 ("-inf") : juce::String (fader_.getValue(), 1)),
                    2, getHeight() - 42, getWidth() - 4, 14, juce::Justification::centred);
    }

private:
    void showTypeMenu()
    {
        const auto current = owner_.processor_.trackType (index_);
        juce::PopupMenu menu;
        menu.addItem (1, u8 (std::string (owner_.tr_.tr ("mix.type_sampler"))), true, current == TrackType::Sampler);
        menu.addItem (2, u8 (std::string (owner_.tr_.tr ("mix.type_synth"))), true, current == TrackType::Synth);
        menu.addSeparator();
        menu.addItem (3, u8 (std::string (owner_.tr_.tr ("mix.type_fm"))), true, current == TrackType::Fm);
        menu.addItem (4, u8 (std::string (owner_.tr_.tr ("mix.type_audio"))), false);       // plus tard
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typeButton_).withMinimumWidth (160),
                            [safe = juce::Component::SafePointer<Strip> (this)] (int choice)
                            {
                                if (safe == nullptr || choice == 0) return;
                                safe->owner_.requestTypeChange (safe->index_, choice == 3 ? TrackType::Fm : choice == 2 ? TrackType::Synth : TrackType::Sampler);
                            });
    }

    juce::Rectangle<int> meterArea() const { return { getWidth() - 16, faderArea_.getY(), 12, faderArea_.getHeight() }; }
    mixer::StripSettings settings() const
    {
        if (isPar_) return owner_.processor_.parallelSettings (index_).strip;
        return isBus_ ? owner_.processor_.busSettings (index_) : owner_.processor_.trackSettings (index_);
    }
    void commit (mixer::StripSettings s)
    {
        if (isPar_)
        {
            auto ps = owner_.processor_.parallelSettings (index_);
            ps.strip = std::move (s);
            owner_.processor_.uiEditParallel (index_, std::move (ps), "Parallel settings", "strip");
        }
        else if (isBus_) owner_.processor_.uiEditBus (index_, std::move (s), "Bus settings", "strip");
        else owner_.processor_.uiEditTrack (index_, std::move (s), "Track settings", "strip");
    }
    static juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    MixerView& owner_;
    bool isBus_;
    bool isPar_ = false;
    int index_;
    bool selected_ = false;
    bool hasFold_ = false;
    int depth_ = 0;
    int colourIndex_ = 0;
    float peakL_ = 0.0f, peakR_ = 0.0f;
    juce::Label name_;
    juce::ComboBox midi_;
    juce::TextButton typeButton_, removeButton_, foldButton_;
    juce::ComboBox tapBox_;
    juce::TextButton routing_;
    BoundSlider pan_, fader_;
    BoundToggle mute_, solo_, parallel_;
    juce::Rectangle<int> faderArea_;
    juce::Component::SafePointer<RoutingPanel> panelRef_;
    InsertList inserts_;

    friend class MixerView;
};

// ------------------------------------------------------------------------------------------------ master

class MixerView::MasterStrip : public juce::Component
{
public:
    explicit MasterStrip (MixerView& owner)
        : owner_ (owner), pan_ (owner.processor_, juce::Slider::LinearHorizontal), trim_ (owner.processor_, juce::Slider::LinearHorizontal),
          fader_ (owner.processor_, juce::Slider::LinearVertical), mute_ (owner.processor_, "M"), mono_ (owner.processor_, "Mono"),
          inserts_ (owner, NeptuneProcessor::StripKind::Master, 0)
    {
        addAndMakeVisible (inserts_);
        title_.setText ("Master", juce::dontSendNotification);
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        addAndMakeVisible (title_);
        pan_.setRange (-100.0, 100.0, 1.0);
        pan_.bind (probe::masterPan, 0.0);
        trim_.setRange (-12.0, 12.0, 0.1);
        trim_.bind (mixer::masterTrim, 0.0);
        fader_.setRange (kFaderMinDb, kFaderMaxDb, 0.1);
        fader_.setSkewFactorFromMidPoint (-24.0);
        fader_.bind (probe::masterVolume, 0.0);
        mute_.id = probe::masterMute;
        mono_.id = mixer::masterMono;
        mute_.onToggle = [this] (bool) { mute_.setValueFromUi (owner_.processor_.parameters().get (mute_.id) < 0.5); };
        mono_.onToggle = [this] (bool) { mono_.setValueFromUi (owner_.processor_.parameters().get (mono_.id) < 0.5); };
        mute_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::KnobHot));
        mono_.setColour (juce::TextButton::buttonOnColourId, owner.colour (Token::Accent));
        for (auto* c : std::initializer_list<juce::Component*> { &pan_, &trim_, &fader_, &mute_, &mono_ }) addAndMakeVisible (c);
        trimLabel_.setText ("Trim", juce::dontSendNotification);
        panLabel_.setText ("Pan", juce::dontSendNotification);
        for (auto* l : { &trimLabel_, &panLabel_ })
        {
            l->setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
            l->setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (*l);
        }
        for (int o = 1; o <= mixer::numOutputs; ++o) output_.addItem ("Out " + juce::String (o), o);
        output_.onChange = [this]
        {
            auto m = owner_.processor_.masterSettings();
            m.output = output_.getSelectedId();
            owner_.processor_.uiEditMaster (m, "Master output", "master");
        };
        addAndMakeVisible (output_);
        panMode_.addItem ("Balance", 1);
        panMode_.addItem ("True pan", 2);
        panMode_.onChange = [this]
        {
            auto m = owner_.processor_.masterSettings();
            m.panMode = panMode_.getSelectedId() == 2 ? mixer::PanMode::True : mixer::PanMode::Balance;
            owner_.processor_.uiEditMaster (m, "Master pan mode", "master");
        };
        addAndMakeVisible (panMode_);
        lufsReset_.setButtonText ("LUFS 0");
        lufsReset_.setTooltip ("Reset integrated loudness");
        lufsReset_.onClick = [this] { owner_.processor_.resetLoudness(); };
        addAndMakeVisible (lufsReset_);
    }

    void refresh()
    {
        const auto m = owner_.processor_.masterSettings();
        output_.setSelectedId (m.output, juce::dontSendNotification);
        panMode_.setSelectedId (m.panMode == mixer::PanMode::True ? 2 : 1, juce::dontSendNotification);
    }

    void syncValues()
    {
        pan_.sync(); trim_.sync(); fader_.sync(); mute_.sync(); mono_.sync();
        inserts_.refresh();
    }

    void updateLoudness (const neptune::core::LoudnessMeter::Reading& r)
    {
        if (r.momentary != lufs_.momentary || r.shortTerm != lufs_.shortTerm || r.integrated != lufs_.integrated || r.truePeakDb != lufs_.truePeakDb)
        {
            lufs_ = r;
            repaint (lufsArea_);
        }
    }

    void updateMeter (const Engine::Meter& m)
    {
        auto fall = [] (float current, float incoming) { return incoming > current ? incoming : current * 0.88f; };
        peakL_ = fall (peakL_, m.peakL);
        peakR_ = fall (peakR_, m.peakR);
        rmsL_ = 0.8f * rmsL_ + 0.2f * m.rmsL;
        rmsR_ = 0.8f * rmsR_ + 0.2f * m.rmsR;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (3);
        title_.setBounds (r.removeFromTop (20));
        output_.setBounds (r.removeFromTop (24).reduced (0, 1));
        panMode_.setBounds (r.removeFromTop (24).reduced (0, 1));
        inserts_.setBounds (r.removeFromTop (InsertList::heightNeeded() + 2).reduced (0, 1));
        // sonie : M (momentanée), S (court terme), I (intégrée), TP (crête vraie) puis le bouton de remise à zéro
        lufsArea_ = r.removeFromTop (4 * 14 + 4);
        lufsReset_.setBounds (r.removeFromTop (20).reduced (0, 1));
        auto line = r.removeFromTop (18);
        trimLabel_.setBounds (line);
        trim_.setBounds (r.removeFromTop (18));
        line = r.removeFromTop (18);
        panLabel_.setBounds (line);
        pan_.setBounds (r.removeFromTop (18));
        auto bottom = r.removeFromBottom (26);
        mute_.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1));
        mono_.setBounds (bottom.reduced (1));
        r.removeFromBottom (16);
        faderArea_ = r;
        fader_.setBounds (r.removeFromLeft (r.getWidth() - 40));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (owner_.colour (Token::BgCard));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (owner_.colour (Token::Accent));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
        const juce::Rectangle<int> m (getWidth() - 38, faderArea_.getY(), 34, faderArea_.getHeight());
        auto bar = [&] (float lin, float rms, int x)
        {
            const float db = dbOf (lin), rdb = dbOf (rms);
            const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            const float rh = juce::jlimit (0.0f, 1.0f, (rdb + 60.0f) / 66.0f) * static_cast<float> (m.getHeight());
            g.setColour (owner_.colour (Token::Border));
            g.fillRect (x, m.getY(), 12, m.getHeight());
            g.setColour (db > 0.0f ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent2));
            g.fillRect (x, m.getBottom() - static_cast<int> (h), 12, static_cast<int> (h));
            g.setColour (owner_.colour (Token::Text).withAlpha (0.6f)); // RMS
            g.fillRect (x + 3, m.getBottom() - static_cast<int> (rh), 6, static_cast<int> (rh));
        };
        bar (peakL_, rmsL_, m.getX());
        bar (peakR_, rmsR_, m.getX() + 15);
        {
            // LUFS : valeurs à une décimale, « -inf » en dessous de -100 ; la crête vraie passe en rouge au-dessus de -1 dBTP
            auto value = [] (float v) { return v <= -100.0f ? juce::String ("-inf") : juce::String (v, 1); };
            const char* const names[4] = { "M", "S", "I", "TP" };
            const float values[4] = { lufs_.momentary, lufs_.shortTerm, lufs_.integrated, lufs_.truePeakDb };
            g.setFont (juce::FontOptions (11.0f));
            for (int i = 0; i < 4; ++i)
            {
                auto row = juce::Rectangle<int> (lufsArea_.getX() + 2, lufsArea_.getY() + 2 + i * 14, lufsArea_.getWidth() - 4, 14);
                g.setColour (owner_.colour (Token::TextDim));
                g.drawText (names[i], row.removeFromLeft (22), juce::Justification::centredLeft);
                g.setColour (i == 3 && values[i] > -1.0f ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent));
                g.drawText (value (values[i]) + (i == 3 ? " dB" : " LUFS"), row, juce::Justification::centredRight);
            }
        }
        g.setColour (owner_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (fader_.getValue() <= kFaderMinDb ? juce::String::fromUTF8 ("-inf") : juce::String (fader_.getValue(), 1),
                    2, getHeight() - 42, getWidth() - 4, 14, juce::Justification::centred);
    }

private:
    MixerView& owner_;
    juce::Label title_, trimLabel_, panLabel_;
    juce::ComboBox output_, panMode_;
    BoundSlider pan_, trim_, fader_;
    BoundToggle mute_, mono_;
    juce::Rectangle<int> faderArea_, lufsArea_;
    juce::TextButton lufsReset_;
    neptune::core::LoudnessMeter::Reading lufs_;
    float peakL_ = 0.0f, peakR_ = 0.0f, rmsL_ = 0.0f, rmsR_ = 0.0f;
    InsertList inserts_;
};

// ------------------------------------------------------------------------------------------------ la vue

MixerView::MixerView (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr)
    : processor_ (p), palette_ (palette), tr_ (tr)
{
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        tracks_[static_cast<std::size_t> (t)] = std::make_unique<Strip> (*this, 0, t);
        trackRow_.addAndMakeVisible (*tracks_[static_cast<std::size_t> (t)]);
    }
    for (int q = 0; q < mixer::numParallels; ++q)
    {
        parallels_[static_cast<std::size_t> (q)] = std::make_unique<Strip> (*this, 2, q);
        trackRow_.addChildComponent (*parallels_[static_cast<std::size_t> (q)]);
    }
    trackViewport_.setViewedComponent (&trackRow_, false);
    trackViewport_.setScrollBarsShown (false, true);
    trackViewport_.setScrollBarThickness (12);
    addAndMakeVisible (trackViewport_);
    for (int b = 0; b < mixer::numBuses; ++b)
    {
        buses_[static_cast<std::size_t> (b)] = std::make_unique<Strip> (*this, 1, b);
        addAndMakeVisible (*buses_[static_cast<std::size_t> (b)]);
    }
    master_ = std::make_unique<MasterStrip> (*this);
    addAndMakeVisible (*master_);
    addTrack_.setButtonText (juce::String::fromUTF8 (std::string (tr.tr ("mix.add_track")).c_str()));
    addTrack_.onClick = [this]
    {
        if (processor_.uiAddTrack() >= 0) refresh();
    };
    trackRow_.addAndMakeVisible (addTrack_);
    tracksTitle_.setText (juce::String::fromUTF8 (std::string (tr.tr ("mix.tracks")).c_str()), juce::dontSendNotification);
    busesTitle_.setText (juce::String::fromUTF8 (std::string (tr.tr ("mix.buses")).c_str()), juce::dontSendNotification);
    for (auto* l : { &tracksTitle_, &busesTitle_ })
    {
        l->setColour (juce::Label::textColourId, colour (Token::TextDim));
        addAndMakeVisible (*l);
    }
    startTimerHz (30);
    refresh();
}

MixerView::~MixerView() { stopTimer(); }

juce::Colour MixerView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void MixerView::placeScaled (juce::Rectangle<int> area)
{
    const float s = juce::jmin (1.0f, static_cast<float> (area.getWidth()) / designWidth, static_cast<float> (area.getHeight()) / designHeight);
    const float scale = juce::jmax (0.4f, s);
    setTransform (juce::AffineTransform::scale (scale));
    // la transformation s'applique aux limites dans le repère du parent (autour de son origine) : position = zone / échelle
    setBounds (juce::roundToInt (static_cast<float> (area.getX()) / scale), juce::roundToInt (static_cast<float> (area.getY()) / scale),
               juce::roundToInt (static_cast<float> (area.getWidth()) / scale), juce::roundToInt (static_cast<float> (area.getHeight()) / scale));
}

void MixerView::resized()
{
    layoutTracks();
}

// Parallèles visibles sous un parent : les filles d'une parallèle repliée sont masquées (elles restent actives dans le moteur).
void MixerView::appendParallels (int parentId, std::vector<int>& out) const
{
    for (const int p : processor_.parallelChildren (parentId))
    {
        out.push_back (p);
        if (expanded_[static_cast<std::size_t> (p)]) appendParallels (mixer::numTracks + p, out);
    }
}

void MixerView::toggleFold (int p)
{
    expanded_[static_cast<std::size_t> (p)] = ! expanded_[static_cast<std::size_t> (p)];
    refresh();
}

// Chaque piste suivie de ses pistes parallèles (à sa droite), le bouton « + Piste » à la fin ; bus et master à leur place.
// La rangée des pistes défile horizontalement si elle dépasse.
void MixerView::layoutTracks()
{
    auto r = getLocalBounds();
    const int scrollbar = 12;
    stripHeight_ = juce::jlimit (kMinStripH, kMaxStripH, (getHeight() - 40 - scrollbar) / 2); // deux rangées de bandes + leurs titres
    auto masterArea = r.removeFromRight (kMasterW + 6);
    master_->setBounds (masterArea.withTrimmedLeft (6).withHeight (stripHeight_ * 2 + 40 + scrollbar));
    const int gap = 1;
    tracksTitle_.setBounds (r.removeFromTop (20));
    {
        auto area = r.removeFromTop (stripHeight_ + scrollbar);
        trackViewport_.setBounds (area);
        for (auto& s : parallels_) s->setVisible (false);
        int x = 0;
        for (int t = 0; t < mixer::numTracks; ++t)
        {
            auto& s = *tracks_[static_cast<std::size_t> (t)];
            if (! shownTracks_[static_cast<std::size_t> (t)]) continue;
            s.setBounds (x, 0, kStripW, stripHeight_);
            x += kStripW + gap;
            std::vector<int> kids;
            appendParallels (t, kids);
            for (const int p : kids)
            {
                auto& ps = *parallels_[static_cast<std::size_t> (p)];
                ps.setBounds (x, 0, kStripW, stripHeight_);
                ps.setVisible (true);
                x += kStripW + gap;
            }
        }
        addTrack_.setVisible (processor_.activeTrackCount() < mixer::numTracks);
        addTrack_.setBounds (x + 4, 4, kStripW + 20, 36);
        const int contentW = x + kStripW + 30;
        trackRow_.setSize (juce::jmax (contentW, area.getWidth() - 2), stripHeight_);
    }
    busesTitle_.setBounds (r.removeFromTop (20));
    {
        auto area = r.removeFromTop (stripHeight_);
        int x = area.getX();
        for (auto& s : buses_)
        {
            s->setBounds (x, area.getY(), kStripW, stripHeight_);
            x += kStripW + gap;
        }
    }
}

void MixerView::paint (juce::Graphics&) {}

void MixerView::refresh()
{
    const int selected = processor_.selectedTrack();
    bool layoutChanged = false;
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        const bool active = processor_.trackActive (t);
        auto& s = *tracks_[static_cast<std::size_t> (t)];
        if (active != shownTracks_[static_cast<std::size_t> (t)]) { shownTracks_[static_cast<std::size_t> (t)] = active; layoutChanged = true; }
        s.setVisible (active);
        if (active) s.refresh (t == selected);
    }
    for (int q = 0; q < mixer::numParallels; ++q)
    {
        const bool active = processor_.parallelActive (q);
        if (! active) expanded_[static_cast<std::size_t> (q)] = false;
        if (active != shownParallels_[static_cast<std::size_t> (q)]) { shownParallels_[static_cast<std::size_t> (q)] = active; layoutChanged = true; }
        if (active) parallels_[static_cast<std::size_t> (q)]->refresh (false);
    }
    // un parent ou un repliement a pu changer : on recalcule la disposition à chaque rafraîchissement (peu coûteux)
    for (int b = 0; b < mixer::numBuses; ++b) buses_[static_cast<std::size_t> (b)]->refresh (false);
    master_->refresh();
    layoutTracks();
    (void) layoutChanged;
}

void MixerView::timerCallback()
{
    if (! isShowing()) return;
    const auto rev = processor_.instrumentRevision();
    if (rev != lastRevision_)
    {
        lastRevision_ = rev;
        refresh();
    }
    for (int t = 0; t < mixer::numTracks; ++t)
    {
        if (! shownTracks_[static_cast<std::size_t> (t)]) continue;
        auto& s = *tracks_[static_cast<std::size_t> (t)];
        s.syncValues();
        s.updateMeter (processor_.trackMeter (t));
    }
    for (int q = 0; q < mixer::numParallels; ++q)
    {
        auto& s = *parallels_[static_cast<std::size_t> (q)];
        if (! s.isVisible()) continue;
        s.syncValues();
        s.updateMeter (processor_.parallelMeter (q));
    }
    for (int b = 0; b < mixer::numBuses; ++b)
    {
        auto& s = *buses_[static_cast<std::size_t> (b)];
        s.syncValues();
        s.updateMeter (processor_.busMeter (b));
    }
    master_->syncValues();
    master_->updateMeter (processor_.masterMeter());
    master_->updateLoudness (processor_.masterLoudness());
}

// Changement de générateur : la configuration d'instrument est perdue (spec §1), donc une confirmation si la piste en a une.
void MixerView::requestTypeChange (int track, TrackType type)
{
    if (processor_.trackType (track) == type) return;
    auto apply = [safe = juce::Component::SafePointer<MixerView> (this), track, type]
    {
        if (safe == nullptr) return;
        safe->processor_.uiSetTrackType (track, type);
        safe->refresh();
    };
    if (! processor_.trackHasContent (track)) { apply(); return; }
    auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("mix.confirm_title"),
                                                                                text ("mix.confirm_type"), text ("mix.continue"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); }); // 1 : premier bouton
}

void MixerView::requestRemove (int track)
{
    if (processor_.activeTrackCount() <= 1) return; // le mixeur garde toujours une piste
    auto apply = [safe = juce::Component::SafePointer<MixerView> (this), track]
    {
        if (safe == nullptr) return;
        safe->processor_.uiRemoveTrack (track);
        safe->refresh();
    };
    if (! processor_.trackHasContent (track)) { apply(); return; }
    auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    const auto message = juce::String::fromUTF8 (tr_.format ("mix.confirm_remove", { std::to_string (track + 1) }).c_str());
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("mix.confirm_title"),
                                                                                message, text ("mix.continue"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); });
}

// Ajout / retrait de pistes parallèles
void MixerView::requestAddParallel (int parentId)
{
    const int p = processor_.uiAddParallel (parentId);
    if (p >= 0)
    {
        if (parentId >= mixer::numTracks) expanded_[static_cast<std::size_t> (parentId - mixer::numTracks)] = true; // la nouvelle fille se voit
        refresh();
    }
}

void MixerView::requestRemoveParallel (int p)
{
    auto apply = [safe = juce::Component::SafePointer<MixerView> (this), p]
    {
        if (safe == nullptr) return;
        safe->processor_.uiRemoveParallel (p);
        safe->refresh();
    };
    const auto s = processor_.parallelSettings (p);
    bool hasInserts = false;
    for (int slot = 0; slot < mixer::numInserts; ++slot) hasInserts = hasInserts || ! processor_.insertType (NeptuneProcessor::StripKind::Parallel, p, slot).empty();
    if (processor_.parallelChildren (mixer::numTracks + p).empty() && ! hasInserts && s.strip.name.empty()) { apply(); return; }
    auto text = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    const auto message = juce::String::fromUTF8 (tr_.format ("mix.confirm_remove_parallel", { std::to_string (p + 1) }).c_str());
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, text ("mix.confirm_title"),
                                                                                message, text ("mix.continue"), text ("mix.cancel"), this),
                                  [apply] (int result) { if (result == 1) apply(); });
}

// Solo exclusif (Maj+clic) : coupe les solos et les mutes des autres bandes du même domaine (pistes + parallèles, ou bus).
void MixerView::exclusiveSolo (int kind, int index)
{
    auto setParam = [this] (ParamId id, double v)
    {
        const auto* spec = processor_.parameters().find (id);
        if (spec == nullptr || processor_.parameters().get (id) == v) return;
        processor_.uiBeginEdit (id);
        processor_.uiSetNormalised (id, spec->toNormalized (v));
        processor_.uiEndEdit (id);
    };
    if (kind != 1)
    {
        for (int t = 0; t < mixer::numTracks; ++t)
            if (! (kind == 0 && t == index)) setParam (probe::trackParam (t, probe::kSolo), 0.0);
        for (int q = 0; q < mixer::numParallels; ++q)
            if (! (kind == 2 && q == index)) setParam (mixer::parallelParam (q, mixer::kParSolo), 0.0);
    }
    else
        for (int b = 0; b < mixer::numBuses; ++b)
        {
            if (b == index) continue;
            setParam (mixer::busParam (b, mixer::kBusSolo), 0.0);
        }
}
