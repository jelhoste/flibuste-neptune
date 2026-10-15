#include "PresetManager.h"

#include <algorithm>
#include <set>

#include "PresetHost.h"
#include "neptune/core/AnalogSynth.h"
#include "neptune/core/FileUtil.h"

using neptune::core::theme::Token;
namespace preset = neptune::core::preset;
namespace analogsynth = neptune::core::analogsynth;

namespace
{
constexpr int kTabPresets = 0, kTabBanks = 1, kTabGrid = 2, kTabTrash = 3, kTabConfig = 4, kNumTabs = 5;
constexpr int kGridColumns = 8, kGridRows = 8, kGridTiles = kGridColumns * kGridRows;
constexpr juce::int64 kMaxImportBytes = 4 * 1024 * 1024;

juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
std::string S (const juce::String& s) { return s.toStdString(); }

// « Clé=Valeur ; Clé=Valeur » <-> champs personnalisés. Les valeurs multiples d'une même clé se répètent.
std::vector<preset::CustomField> parseFields (const juce::String& text, bool splitAlternatives = false)
{
    std::vector<preset::CustomField> out;
    for (const auto& part : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto eq = part.indexOfChar ('=');
        if (eq <= 0) continue;
        const auto key = part.substring (0, eq).trim();
        const auto value = part.substring (eq + 1).trim();
        if (key.isEmpty() || value.isEmpty()) continue;
        if (splitAlternatives)
        {
            for (const auto& alt : juce::StringArray::fromTokens (value, "|", ""))
                if (alt.trim().isNotEmpty()) out.push_back ({ S (key), S (alt.trim()) });
        }
        else out.push_back ({ S (key), S (value) });
    }
    return out;
}

juce::String formatFields (const std::vector<preset::CustomField>& f)
{
    juce::StringArray parts;
    for (const auto& c : f) parts.add (U (c.key) + "=" + U (c.value));
    return parts.joinIntoString ("; ");
}

std::vector<std::string> parseList (const juce::String& text)
{
    std::vector<std::string> out;
    for (const auto& t : juce::StringArray::fromTokens (text, ",;", ""))
        if (t.trim().isNotEmpty()) out.push_back (S (t.trim()));
    return out;
}

juce::String formatList (const std::vector<std::string>& v)
{
    juce::StringArray parts;
    for (const auto& s : v) parts.add (U (s));
    return parts.joinIntoString (", ");
}

std::string readTextFile (const juce::File& f)
{
    if (! f.existsAsFile() || f.getSize() > kMaxImportBytes) return {};
    return S (f.loadFileAsString());
}

// Modèle de liste générique : les onglets fournissent des fonctions.
struct FnListModel : juce::ListBoxModel
{
    std::function<int()> count;
    std::function<void (int, juce::Graphics&, int, int, bool)> paint;
    std::function<void (int)> selected;
    std::function<void (int)> doubleClicked;

    int getNumRows() override { return count ? count() : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override { if (paint) paint (row, g, w, h, sel); }
    void selectedRowsChanged (int last) override { if (selected) selected (last); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (doubleClicked) doubleClicked (row); }
};

// Bouton à appui long : un appui bref verrouille (si déverrouillé), un appui long déverrouille. Évite un déverrouillage accidentel.
class LongPressLock : public juce::Button, private juce::Timer
{
public:
    LongPressLock() : juce::Button ("lock") {}
    std::function<void (bool locked)> onToggled;
    void setLocked (bool l) { locked_ = l; repaint(); }
    bool locked() const noexcept { return locked_; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (locked_ ? juce::Colour (0xff7a3b2e) : juce::Colour (0xff2e6b4a));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (over || down ? 1.0f : 0.85f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (label_, getLocalBounds(), juce::Justification::centred);
        if (down && locked_ && ! fired_)
        {
            const float p = static_cast<float> (juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - pressedAt_) / kHoldMs));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (b.withWidth (b.getWidth() * p));
        }
    }
    void setLabels (const juce::String& locked, const juce::String& unlocked) { lockedText_ = locked; unlockedText_ = unlocked; updateLabel(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        fired_ = false;
        pressedAt_ = juce::Time::getMillisecondCounterHiRes();
        if (locked_) startTimerHz (30);
        juce::Button::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        const bool wasFired = fired_;
        fired_ = false;
        if (! wasFired && ! locked_ && getLocalBounds().contains (e.getPosition()))
        {
            locked_ = true;
            updateLabel();
            if (onToggled) onToggled (true);
        }
        juce::Button::mouseUp (e);
        repaint();
    }

private:
    static constexpr double kHoldMs = 600.0;
    void updateLabel() { label_ = locked_ ? lockedText_ : unlockedText_; }
    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounterHiRes() - pressedAt_ < kHoldMs) { repaint(); return; }
        stopTimer();
        fired_ = true;
        locked_ = false;
        updateLabel();
        if (onToggled) onToggled (false);
        repaint();
    }
    bool locked_ = true, fired_ = false;
    double pressedAt_ = 0.0;
    juce::String lockedText_ { "LOCKED" }, unlockedText_ { "UNLOCKED" }, label_ { "LOCKED" };
};

struct Ctx
{
    PresetHost& p;
    neptune::core::theme::Palette palette;
    const neptune::core::i18n::Translator& tr;
    std::string selected;       // preset sélectionné (partagé entre onglets)
    std::string gridId;         // grille active
    std::function<void (const juce::String&, std::function<void()>)> confirm;
    std::function<void (const juce::String&)> status;
    std::function<void()> changed;  // la bibliothèque a changé : tout se relit
    std::function<void()> loaded;   // le preset courant a changé

    juce::String text (const char* key) const { return U (std::string (tr.tr (key))); }
    juce::Colour colour (Token t) const
    {
        const auto& c = palette.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    void editor (juce::TextEditor& e, bool multiline = false) const
    {
        e.setMultiLine (multiline, multiline);
        e.setReturnKeyStartsNewLine (multiline);
        e.setColour (juce::TextEditor::backgroundColourId, colour (Token::BgCard));
        e.setColour (juce::TextEditor::textColourId, colour (Token::Text));
        e.setColour (juce::TextEditor::outlineColourId, colour (Token::CtlBd));
        e.setColour (juce::TextEditor::focusedOutlineColourId, colour (Token::Accent));
    }
    void label (juce::Label& l, const juce::String& t, bool dim = true) const
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colour (dim ? Token::TextDim : Token::Text));
        l.setFont (juce::FontOptions (12.0f));
    }
    void combo (juce::ComboBox& c) const
    {
        c.setColour (juce::ComboBox::backgroundColourId, colour (Token::BgCard));
        c.setColour (juce::ComboBox::textColourId, colour (Token::Text));
        c.setColour (juce::ComboBox::outlineColourId, colour (Token::CtlBd));
        c.setColour (juce::ComboBox::arrowColourId, colour (Token::Text));
    }
    void toggle (juce::ToggleButton& t, const juce::String& text) const
    {
        t.setButtonText (text);
        t.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        t.setColour (juce::ToggleButton::tickColourId, colour (Token::Accent));
    }
    void list (juce::ListBox& l) const
    {
        l.setColour (juce::ListBox::backgroundColourId, colour (Token::PanelBg));
        l.setColour (juce::ListBox::outlineColourId, colour (Token::CtlBd));
        l.setOutlineThickness (1);
        l.setRowHeight (26);
    }
    void slider (juce::Slider& s, double lo, double hi) const
    {
        s.setRange (lo, hi, 1.0);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        s.setColour (juce::Slider::trackColourId, colour (Token::Accent));
        s.setColour (juce::Slider::backgroundColourId, colour (Token::Grid));
        s.setColour (juce::Slider::thumbColourId, colour (Token::Handle));
        s.setColour (juce::Slider::textBoxTextColourId, colour (Token::Text));
        s.setColour (juce::Slider::textBoxOutlineColourId, colour (Token::CtlBd));
    }
    void rowBackground (juce::Graphics& g, int w, int h, bool sel) const
    {
        if (sel) { g.setColour (colour (Token::Accent).withAlpha (0.28f)); g.fillRect (0, 0, w, h); }
        g.setColour (colour (Token::Border).withAlpha (0.4f));
        g.drawHorizontalLine (h - 1, 0.0f, static_cast<float> (w));
    }
    void stars (juce::Graphics& g, int rating, juce::Rectangle<float> area) const
    {
        const float r = juce::jmin (area.getHeight() * 0.42f, area.getWidth() / 7.0f);
        for (int i = 0; i < preset::maxRating; ++i)
        {
            juce::Path s;
            s.addStar ({ area.getX() + r * 1.2f + static_cast<float> (i) * r * 2.4f, area.getCentreY() }, 5, r * 0.45f, r);
            g.setColour (i < rating ? colour (Token::Accent) : colour (Token::TextDim2).withAlpha (0.5f));
            g.fillPath (s);
        }
    }
};

// ---------------------------------------------------------------------------------------------------- onglet Presets
class PresetsTab : public juce::Component
{
public:
    explicit PresetsTab (Ctx& c) : c_ (c)
    {
        c.editor (search_);
        search_.setTextToShowWhenEmpty (c.text ("preset.search"), c.colour (Token::TextDim));
        search_.onTextChange = [this] { rebuildRows(); };
        c.editor (facets_);
        facets_.setTextToShowWhenEmpty (c.text ("preset.facets_hint"), c.colour (Token::TextDim));
        facets_.onTextChange = [this] { rebuildRows(); };
        for (auto* b : { &typeBox_, &charBox_, &ratingBox_ })
        {
            c.combo (*b);
            b->onChange = [this] { if (! refreshing_) rebuildRows(); };
            addAndMakeVisible (*b);
        }
        ratingBox_.addItem (c.text ("preset.rating_any"), 1);
        for (int i = 1; i <= preset::maxRating; ++i) ratingBox_.addItem (c.text ("preset.rating_min") + " " + juce::String (i), i + 1);
        ratingBox_.setSelectedId (1, juce::dontSendNotification);
        c.toggle (lockedOnly_, c.text ("preset.locked_only"));
        lockedOnly_.onClick = [this] { rebuildRows(); };
        addAndMakeVisible (search_);
        addAndMakeVisible (facets_);
        addAndMakeVisible (lockedOnly_);

        c.list (list_);
        model_.count = [this] { return static_cast<int> (rows_.size()); };
        model_.paint = [this] (int row, juce::Graphics& g, int w, int h, bool sel) { paintRow (row, g, w, h, sel); };
        model_.selected = [this] (int row) { onRowSelected (row); };
        model_.doubleClicked = [this] (int row)
        {
            if (row < 0 || row >= static_cast<int> (rows_.size())) return;
            if (c_.p.loadPreset (rows_[static_cast<std::size_t> (row)].id)) { c_.status (c_.text ("preset.loaded")); c_.loaded(); }
        };
        list_.setModel (&model_);
        addAndMakeVisible (list_);

        // détails
        for (auto* l : { &nameL_, &typeL_, &charsL_, &ratingL_, &fieldsL_, &originL_, &bankL_, &programL_, &tileL_ })
            addAndMakeVisible (*l);
        c.label (nameL_, c.text ("preset.name"));
        c.label (typeL_, c.text ("preset.type"));
        c.label (charsL_, c.text ("preset.characteristics"));
        c.label (ratingL_, c.text ("preset.rating"));
        c.label (fieldsL_, c.text ("preset.fields"));
        c.label (bankL_, c.text ("preset.bank"));
        c.label (programL_, c.text ("preset.program"));
        c.label (tileL_, c.text ("preset.grid_tile"));
        c.label (originL_, {}, true);
        for (auto* e : { &name_, &type_, &chars_, &fields_ })
        {
            c.editor (*e);
            addAndMakeVisible (*e);
        }
        c.combo (rating_);
        for (int i = 0; i <= preset::maxRating; ++i) rating_.addItem (i == 0 ? c.text ("preset.rating_none") : juce::String (i), i + 1);
        addAndMakeVisible (rating_);
        c.toggle (locked_, c.text ("preset.lock"));
        addAndMakeVisible (locked_);
        c.slider (bank_, 0, preset::numBanks - 1);
        c.slider (program_, 0, preset::programsPerBank - 1);
        c.slider (tile_, 1, kGridTiles);
        for (auto* s : { &bank_, &program_, &tile_ }) addAndMakeVisible (*s);

        auto button = [this] (juce::TextButton& b, const char* key, std::function<void()> fn)
        {
            b.setButtonText (c_.text (key));
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };
        button (load_, "preset.load", [this] { doLoad(); });
        button (overwrite_, "preset.overwrite", [this] { doOverwrite(); });
        button (saveNew_, "preset.save_new", [this] { doSaveNew(); });
        button (apply_, "preset.apply_meta", [this] { doApplyMeta(); });
        button (trash_, "preset.trash", [this] { doTrash(); });
        button (assignBank_, "preset.assign_bank", [this] { doAssignBank(); });
        button (assignTile_, "preset.assign_tile", [this] { doAssignTile(); });
        refresh();
    }

    void focusName() { name_.grabKeyboardFocus(); name_.selectAll(); }

    void refresh()
    {
        refreshing_ = true;
        const auto all = c_.p.library().allPresets();
        std::set<std::string> types, chars;
        for (const auto& p : all)
        {
            if (! p.type.empty()) types.insert (p.type);
            for (const auto& ch : p.characteristics) chars.insert (ch);
        }
        const auto typeSel = typeBox_.getText(), charSel = charBox_.getText();
        typeBox_.clear (juce::dontSendNotification);
        charBox_.clear (juce::dontSendNotification);
        typeBox_.addItem (c_.text ("preset.all_types"), 1);
        charBox_.addItem (c_.text ("preset.all_chars"), 1);
        int id = 2;
        for (const auto& t : types) typeBox_.addItem (U (t), id++);
        id = 2;
        for (const auto& t : chars) charBox_.addItem (U (t), id++);
        typeBox_.setText (typeSel.isEmpty() ? c_.text ("preset.all_types") : typeSel, juce::dontSendNotification);
        charBox_.setText (charSel.isEmpty() ? c_.text ("preset.all_chars") : charSel, juce::dontSendNotification);
        if (typeBox_.getSelectedId() == 0) typeBox_.setSelectedId (1, juce::dontSendNotification);
        if (charBox_.getSelectedId() == 0) charBox_.setSelectedId (1, juce::dontSendNotification);
        refreshing_ = false;
        rebuildRows();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (juce::jmax (300, r.getWidth() * 5 / 12));
        r.removeFromLeft (10);
        auto top = left.removeFromTop (28);
        search_.setBounds (top);
        left.removeFromTop (4);
        auto filters = left.removeFromTop (26);
        typeBox_.setBounds (filters.removeFromLeft (filters.getWidth() / 3).reduced (1));
        charBox_.setBounds (filters.removeFromLeft (filters.getWidth() / 2).reduced (1));
        ratingBox_.setBounds (filters.reduced (1));
        left.removeFromTop (4);
        auto filters2 = left.removeFromTop (26);
        lockedOnly_.setBounds (filters2.removeFromLeft (110));
        facets_.setBounds (filters2.reduced (1));
        left.removeFromTop (4);
        list_.setBounds (left);

        // panneau de détails
        const int labelW = 92;
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (5); return x; };
        auto rowOf = [&] (juce::Label& l, juce::Component& comp)
        {
            auto x = row();
            l.setBounds (x.removeFromLeft (labelW));
            comp.setBounds (x);
        };
        rowOf (nameL_, name_);
        rowOf (typeL_, type_);
        rowOf (charsL_, chars_);
        {
            auto x = row();
            ratingL_.setBounds (x.removeFromLeft (labelW));
            rating_.setBounds (x.removeFromLeft (110));
            x.removeFromLeft (10);
            locked_.setBounds (x);
        }
        rowOf (fieldsL_, fields_);
        originL_.setBounds (row (20));
        {
            auto x = row (30);
            load_.setBounds (x.removeFromLeft (x.getWidth() / 3).reduced (2, 0));
            overwrite_.setBounds (x.removeFromLeft (x.getWidth() / 2).reduced (2, 0));
            saveNew_.setBounds (x.reduced (2, 0));
        }
        {
            auto x = row (30);
            apply_.setBounds (x.removeFromLeft (x.getWidth() / 2).reduced (2, 0));
            trash_.setBounds (x.reduced (2, 0));
        }
        r.removeFromTop (8);
        {
            auto x = row();
            bankL_.setBounds (x.removeFromLeft (labelW));
            bank_.setBounds (x);
        }
        {
            auto x = row();
            programL_.setBounds (x.removeFromLeft (labelW));
            program_.setBounds (x);
        }
        assignBank_.setBounds (row (30).reduced (2, 0));
        r.removeFromTop (6);
        {
            auto x = row();
            tileL_.setBounds (x.removeFromLeft (labelW));
            tile_.setBounds (x);
        }
        assignTile_.setBounds (row (30).reduced (2, 0));
    }

private:
    struct Row { std::string id, name, type; int rating = 0; bool locked = false, factory = false; };

    preset::Query query() const
    {
        preset::Query q;
        q.text = S (search_.getText());
        if (typeBox_.getSelectedId() > 1) q.type = S (typeBox_.getText());
        if (charBox_.getSelectedId() > 1) q.characteristics = { S (charBox_.getText()) };
        q.minRating = ratingBox_.getSelectedId() > 1 ? ratingBox_.getSelectedId() - 1 : 0;
        q.lockedOnly = lockedOnly_.getToggleState();
        q.facets = parseFields (facets_.getText(), true);
        return q;
    }

    void rebuildRows()
    {
        const auto all = c_.p.library().allPresets();
        rows_.clear();
        for (const auto* p : preset::filter (all, query()))
            if (c_.p.acceptsPreset (*p))
                rows_.push_back ({ p->id, p->name, p->type, p->rating, p->locked, p->factory });
        list_.updateContent();
        int sel = -1;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == c_.selected) sel = static_cast<int> (i);
        if (sel >= 0) list_.selectRow (sel, false, false);
        else list_.deselectAllRows();
        list_.repaint();
        showDetails();
    }

    void paintRow (int row, juce::Graphics& g, int w, int h, bool sel)
    {
        if (row < 0 || row >= static_cast<int> (rows_.size())) return;
        const auto& r = rows_[static_cast<std::size_t> (row)];
        c_.rowBackground (g, w, h, sel);
        const bool current = r.id == c_.p.currentPresetId();
        g.setColour (c_.colour (current ? Token::Accent : Token::Text));
        g.setFont (juce::FontOptions (13.0f, current ? juce::Font::bold : juce::Font::plain));
        g.drawText (U (r.name), 8, 0, w - 230, h, juce::Justification::centredLeft, true);
        g.setColour (c_.colour (Token::TextDim));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (U (r.type), w - 220, 0, 80, h, juce::Justification::centredLeft, true);
        c_.stars (g, r.rating, { static_cast<float> (w - 138), 0.0f, 70.0f, static_cast<float> (h) });
        g.setColour (c_.colour (Token::TextDim));
        g.drawText (r.factory ? c_.text ("preset.tag_factory") : (r.locked ? c_.text ("preset.tag_locked") : juce::String()), w - 64, 0, 60, h,
                    juce::Justification::centredRight, true);
    }

    void onRowSelected (int row)
    {
        if (row < 0 || row >= static_cast<int> (rows_.size())) return;
        c_.selected = rows_[static_cast<std::size_t> (row)].id;
        showDetails();
    }

    const preset::Preset* selectedPreset() const { return c_.selected.empty() ? nullptr : c_.p.library().find (c_.selected); }

    void showDetails()
    {
        const auto* p = selectedPreset();
        const bool have = p != nullptr, user = have && ! p->factory;
        name_.setText (have ? U (p->name) : juce::String(), false);
        type_.setText (have ? U (p->type) : juce::String(), false);
        chars_.setText (have ? formatList (p->characteristics) : juce::String(), false);
        fields_.setText (have ? formatFields (p->customFields) : juce::String(), false);
        rating_.setSelectedId (have ? p->rating + 1 : 1, juce::dontSendNotification);
        locked_.setToggleState (have && p->locked, juce::dontSendNotification);
        juce::String origin;
        if (have && p->factory) origin = c_.text ("preset.origin_factory");
        else if (have && ! p->derivedFrom.empty())
        {
            const auto* src = c_.p.library().find (p->derivedFrom);
            origin = c_.text ("preset.derived") + " " + (src != nullptr ? U (src->name) : U (p->derivedFrom));
        }
        originL_.setText (origin, juce::dontSendNotification);
        for (auto* e : { &name_, &type_, &chars_, &fields_ }) e->setEnabled (user);
        rating_.setEnabled (user);
        locked_.setEnabled (user);
        apply_.setEnabled (user);
        trash_.setEnabled (user);
        load_.setEnabled (have);
        overwrite_.setEnabled (user && ! p->locked);
        assignBank_.setEnabled (have);
        assignTile_.setEnabled (have);
    }

    void doLoad()
    {
        if (c_.selected.empty()) return;
        if (c_.p.loadPreset (c_.selected)) { c_.status (c_.text ("preset.loaded")); c_.loaded(); }
        else c_.status (c_.text ("preset.load_failed"));
    }

    void doOverwrite()
    {
        std::string error;
        if (c_.p.overwritePreset (c_.selected, &error)) { c_.status (c_.text ("preset.saved")); c_.changed(); }
        else c_.status (c_.text ("preset.save_failed") + " " + U (error));
    }

    void doSaveNew()
    {
        auto name = S (name_.getText().trim());
        if (name.empty()) name = S (c_.text ("preset.default_name"));
        preset::Preset out;
        std::string error;
        if (c_.p.saveCurrentAs (name, S (type_.getText().trim()), &out, &error))
        {
            c_.selected = out.id;
            c_.status (c_.text ("preset.saved"));
            c_.changed();
            c_.loaded();
        }
        else c_.status (c_.text ("preset.save_failed") + " " + U (error));
    }

    void doApplyMeta()
    {
        const auto* p = selectedPreset();
        if (p == nullptr || p->factory) return;
        preset::Preset edited = *p;
        edited.name = S (name_.getText().trim());
        edited.type = S (type_.getText().trim());
        edited.characteristics = parseList (chars_.getText());
        edited.rating = rating_.getSelectedId() - 1;
        edited.locked = locked_.getToggleState();
        edited.customFields = parseFields (fields_.getText());
        std::string error;
        if (c_.p.library().updateMetadata (edited, &error)) { c_.status (c_.text ("preset.saved")); c_.changed(); }
        else c_.status (c_.text ("preset.save_failed") + " " + U (error));
    }

    void doTrash()
    {
        const auto* p = selectedPreset();
        if (p == nullptr || p->factory) return;
        const auto id = p->id;
        c_.confirm (c_.text ("preset.confirm_trash") + " « " + U (p->name) + " » ?", [this, id]
        {
            std::string error;
            if (c_.p.library().moveToTrash (id, &error))
            {
                if (c_.selected == id) c_.selected.clear();
                c_.status (c_.text ("preset.trashed"));
                c_.changed();
            }
            else c_.status (U (error));
        });
    }

    void doAssignBank()
    {
        if (c_.selected.empty()) return;
        const int b = static_cast<int> (bank_.getValue()), pr = static_cast<int> (program_.getValue());
        const auto place = [this, b, pr, id = c_.selected]
        {
            std::string error;
            if (c_.p.library().assign (b, pr, id, &error)) { c_.status (c_.text ("preset.assigned")); c_.changed(); }
            else c_.status (U (error));
        };
        const auto* bank = c_.p.library().banks().find (b);
        if (bank != nullptr && ! bank->presets[static_cast<std::size_t> (pr)].empty() && bank->presets[static_cast<std::size_t> (pr)] != c_.selected)
            c_.confirm (c_.text ("preset.confirm_overwrite_slot"), place);
        else place();
    }

    void doAssignTile()
    {
        if (c_.selected.empty()) return;
        if (c_.gridId.empty()) { c_.status (c_.text ("grid.none")); return; }
        const int t = static_cast<int> (tile_.getValue()) - 1;
        const auto place = [this, t, id = c_.selected]
        {
            for (const auto& g : c_.p.library().grids())
                if (g.gridId == c_.gridId)
                {
                    auto copy = g;
                    copy.assign (t, id);
                    std::string error;
                    if (c_.p.library().saveGrid (copy, &error)) { c_.status (c_.text ("preset.assigned")); c_.changed(); }
                    else c_.status (U (error));
                }
        };
        bool occupied = false;
        for (const auto& g : c_.p.library().grids())
            if (g.gridId == c_.gridId)
                for (const auto& tile : g.tiles)
                    if (tile.position == t && ! tile.presetRef.empty() && tile.presetRef != c_.selected) occupied = true;
        if (occupied) c_.confirm (c_.text ("grid.confirm_overwrite"), place);
        else place();
    }

    Ctx& c_;
    bool refreshing_ = false;
    std::vector<Row> rows_;
    juce::TextEditor search_, facets_, name_, type_, chars_, fields_;
    juce::ComboBox typeBox_, charBox_, ratingBox_, rating_;
    juce::ToggleButton lockedOnly_, locked_;
    FnListModel model_;
    juce::ListBox list_ { "presets", nullptr };
    juce::Label nameL_, typeL_, charsL_, ratingL_, fieldsL_, originL_, bankL_, programL_, tileL_;
    juce::Slider bank_, program_, tile_;
    juce::TextButton load_, overwrite_, saveNew_, apply_, trash_, assignBank_, assignTile_;
};

// ---------------------------------------------------------------------------------------------------- onglet Banques
class BanksTab : public juce::Component
{
public:
    explicit BanksTab (Ctx& c) : c_ (c)
    {
        c.list (bankList_);
        c.list (programList_);
        bankModel_.count = [] { return preset::numBanks; };
        bankModel_.paint = [this] (int row, juce::Graphics& g, int w, int h, bool sel)
        {
            c_.rowBackground (g, w, h, sel);
            const auto* b = c_.p.library().banks().find (row);
            int used = 0;
            if (b != nullptr)
                for (const auto& id : b->presets) used += id.empty() ? 0 : 1;
            g.setColour (c_.colour (b != nullptr ? Token::Text : Token::TextDim2));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (juce::String (row) + "   " + (b != nullptr ? U (preset::displayName (*b)) : U (preset::defaultBankName (row))), 8, 0, w - 60, h,
                        juce::Justification::centredLeft, true);
            if (used > 0)
            {
                g.setColour (c_.colour (Token::TextDim));
                g.drawText (juce::String (used), w - 50, 0, 42, h, juce::Justification::centredRight);
            }
        };
        bankModel_.selected = [this] (int row) { if (row >= 0) { bank_ = row; programList_.updateContent(); programList_.repaint(); showBank(); } };
        bankList_.setModel (&bankModel_);
        addAndMakeVisible (bankList_);

        programModel_.count = [] { return preset::programsPerBank; };
        programModel_.paint = [this] (int row, juce::Graphics& g, int w, int h, bool sel)
        {
            c_.rowBackground (g, w, h, sel);
            const auto* b = c_.p.library().banks().find (bank_);
            const std::string id = b != nullptr ? b->presets[static_cast<std::size_t> (row)] : std::string();
            const auto* p = id.empty() ? nullptr : c_.p.library().find (id);
            g.setColour (c_.colour (Token::TextDim));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (row).paddedLeft ('0', 3), 8, 0, 34, h, juce::Justification::centredLeft);
            const bool current = ! id.empty() && id == c_.p.currentPresetId();
            g.setColour (c_.colour (p != nullptr ? (current ? Token::Accent : Token::Text) : Token::TextDim2));
            g.setFont (juce::FontOptions (13.0f, current ? juce::Font::bold : juce::Font::plain));
            g.drawText (p != nullptr ? U (p->name) : (id.empty() ? juce::String ("—") : c_.text ("bank.missing")), 48, 0, w - 56, h,
                        juce::Justification::centredLeft, true);
        };
        programModel_.selected = [this] (int row) { program_ = row; };
        programModel_.doubleClicked = [this] (int row) { program_ = row; doLoadSlot(); };
        programList_.setModel (&programModel_);
        addAndMakeVisible (programList_);

        c.label (nameL_, c.text ("bank.name"));
        c.label (fieldsL_, c.text ("bank.fields"));
        c.editor (name_);
        c.editor (fields_);
        c.toggle (wrap_, c.text ("bank.wrap"));
        for (juce::Component* x : std::initializer_list<juce::Component*> { &nameL_, &fieldsL_, &name_, &fields_, &wrap_ }) addAndMakeVisible (x);

        auto button = [this] (juce::TextButton& b, const char* key, std::function<void()> fn)
        {
            b.setButtonText (c_.text (key));
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };
        button (applyBank_, "bank.apply", [this] { doApplyBank(); });
        button (loadSlot_, "bank.load_slot", [this] { doLoadSlot(); });
        button (assign_, "bank.assign_selected", [this] { doAssign(); });
        button (clear_, "bank.clear_slot", [this] { doClear(); });
        button (export_, "bank.export", [this] { doExport(); });
        button (importBundle_, "bank.import", [this] { doImportBundle(); });
        button (importPreset_, "bank.import_preset", [this] { doImportPreset(); });
        bankList_.selectRow (0);
        refresh();
    }

    void refresh()
    {
        bankList_.updateContent();
        programList_.updateContent();
        bankList_.repaint();
        programList_.repaint();
        showBank();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        bankList_.setBounds (r.removeFromLeft (r.getWidth() / 4));
        r.removeFromLeft (8);
        programList_.setBounds (r.removeFromLeft (r.getWidth() * 5 / 9));
        r.removeFromLeft (10);
        auto row = [&r] (int h = 26) { auto x = r.removeFromTop (h); r.removeFromTop (5); return x; };
        nameL_.setBounds (row (18));
        name_.setBounds (row());
        fieldsL_.setBounds (row (18));
        fields_.setBounds (row());
        wrap_.setBounds (row());
        applyBank_.setBounds (row (30).reduced (2, 0));
        r.removeFromTop (8);
        assign_.setBounds (row (30).reduced (2, 0));
        loadSlot_.setBounds (row (30).reduced (2, 0));
        clear_.setBounds (row (30).reduced (2, 0));
        r.removeFromTop (8);
        export_.setBounds (row (30).reduced (2, 0));
        importBundle_.setBounds (row (30).reduced (2, 0));
        importPreset_.setBounds (row (30).reduced (2, 0));
    }

private:
    void showBank()
    {
        const auto* b = c_.p.library().banks().find (bank_);
        name_.setText (b != nullptr ? U (b->name) : juce::String(), false);
        name_.setTextToShowWhenEmpty (U (preset::defaultBankName (bank_)), c_.colour (Token::TextDim));
        fields_.setText (b != nullptr ? formatFields (b->customFields) : juce::String(), false);
        wrap_.setToggleState (b != nullptr && b->wrapToNextBank, juce::dontSendNotification);
    }

    void doApplyBank()
    {
        preset::Bank b = c_.p.library().banks().find (bank_) != nullptr ? *c_.p.library().banks().find (bank_) : preset::Bank();
        b.index = bank_;
        b.name = S (name_.getText().trim());
        b.customFields = parseFields (fields_.getText());
        b.wrapToNextBank = wrap_.getToggleState();
        c_.p.library().setBank (b);
        std::string error;
        if (c_.p.library().saveBanks (&error)) { c_.status (c_.text ("preset.saved")); c_.changed(); }
        else c_.status (U (error));
    }

    void doLoadSlot()
    {
        const auto id = preset::Navigator::presetAt (c_.p.library().banks(), { bank_, program_ });
        if (id.empty()) return;
        if (c_.p.loadPreset (id)) { c_.status (c_.text ("preset.loaded")); c_.loaded(); }
        else c_.status (c_.text ("preset.load_failed"));
    }

    void doAssign()
    {
        if (c_.selected.empty()) { c_.status (c_.text ("bank.no_selection")); return; }
        std::string error;
        const auto place = [this, id = c_.selected, b = bank_, pr = program_]
        {
            std::string e;
            if (c_.p.library().assign (b, pr, id, &e)) { c_.status (c_.text ("preset.assigned")); c_.changed(); }
            else c_.status (U (e));
        };
        const auto current = preset::Navigator::presetAt (c_.p.library().banks(), { bank_, program_ });
        if (! current.empty() && current != c_.selected) c_.confirm (c_.text ("preset.confirm_overwrite_slot"), place);
        else place();
    }

    void doClear()
    {
        std::string error;
        if (c_.p.library().assign (bank_, program_, {}, &error)) c_.changed();
    }

    void doExport()
    {
        std::vector<preset::Preset> presets;
        const auto manifest = c_.p.library().exportManifest (bank_, &presets);
        if (presets.empty()) { c_.status (c_.text ("bank.empty")); return; }
        chooser_ = std::make_unique<juce::FileChooser> (c_.text ("bank.export"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                      .getChildFile (U (manifest.bankName) + preset::bundleExtension),
                                                        juce::String ("*") + preset::bundleExtension);
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this, manifest, presets] (const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file == juce::File()) return;
                                   juce::ZipFile::Builder builder;
                                   auto add = [&builder] (const juce::String& name, const std::string& text)
                                   {
                                       builder.addEntry (new juce::MemoryInputStream (text.data(), text.size(), true), 6, name, juce::Time::getCurrentTime());
                                   };
                                   add ("bundle-manifest.json", preset::toJson (manifest));
                                   for (const auto& p : presets) add ("presets/" + U (p.id) + preset::presetExtension, preset::toJson (p));
                                   juce::FileOutputStream out (file);
                                   if (out.openedOk())
                                   {
                                       out.setPosition (0);
                                       out.truncate();
                                       builder.writeToStream (out, nullptr);
                                       c_.status (c_.text ("bank.exported"));
                                   }
                                   else c_.status (c_.text ("bank.export_failed"));
                               });
    }

    void doImportBundle()
    {
        chooser_ = std::make_unique<juce::FileChooser> (c_.text ("bank.import"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        juce::String ("*") + preset::bundleExtension);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file == juce::File()) return;
                                   juce::ZipFile zip (file);
                                   if (zip.getNumEntries() <= 0 || zip.getNumEntries() > 300) { c_.status (c_.text ("bank.import_failed")); return; }
                                   auto readEntry = [&zip] (int index) -> std::string
                                   {
                                       const auto* e = zip.getEntry (index);
                                       if (e == nullptr || e->uncompressedSize > kMaxImportBytes) return {};
                                       std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (index));
                                       return in != nullptr ? S (in->readEntireStreamAsString()) : std::string();
                                   };
                                   const int m = zip.getIndexOfFileName ("bundle-manifest.json");
                                   if (m < 0) { c_.status (c_.text ("bank.import_failed")); return; }
                                   const auto manifest = preset::bundleFromJson (readEntry (m));
                                   std::vector<preset::Preset> presets;
                                   for (int i = 0; i < zip.getNumEntries(); ++i)
                                   {
                                       const auto* e = zip.getEntry (i);
                                       if (e == nullptr || ! e->filename.startsWith ("presets/") || ! e->filename.endsWithIgnoreCase (preset::presetExtension)) continue;
                                       bool ok = false;
                                       auto p = preset::presetFromJson (readEntry (i), &ok);
                                       if (ok && presets.size() < static_cast<std::size_t> (preset::programsPerBank)) presets.push_back (std::move (p));
                                   }
                                   std::string error;
                                   const int placed = c_.p.library().importBundle (manifest, std::move (presets), bank_, &error);
                                   c_.status (c_.text ("bank.imported") + " " + juce::String (placed));
                                   c_.changed();
                               });
    }

    void doImportPreset()
    {
        chooser_ = std::make_unique<juce::FileChooser> (c_.text ("bank.import_preset"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        juce::String ("*") + preset::presetExtension);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file == juce::File()) return;
                                   bool ok = false;
                                   auto p = preset::presetFromJson (readTextFile (file), &ok);
                                   preset::Preset stored;
                                   std::string error;
                                   if (ok && c_.p.library().importPreset (std::move (p), &stored, &error))
                                   {
                                       c_.selected = stored.id;
                                       c_.status (c_.text ("bank.imported") + " 1");
                                       c_.changed();
                                   }
                                   else c_.status (c_.text ("bank.import_failed"));
                               });
    }

    Ctx& c_;
    int bank_ = 0, program_ = 0;
    FnListModel bankModel_, programModel_;
    juce::ListBox bankList_ { "banks", nullptr }, programList_ { "programs", nullptr };
    juce::Label nameL_, fieldsL_;
    juce::TextEditor name_, fields_;
    juce::ToggleButton wrap_;
    juce::TextButton applyBank_, loadSlot_, assign_, clear_, export_, importBundle_, importPreset_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

// ---------------------------------------------------------------------------------------------------- onglet Grille
class TileGrid : public juce::Component
{
public:
    explicit TileGrid (Ctx& c) : c_ (c) {}

    std::function<void (int tile)> onTileClicked;
    std::function<void (int from, int to)> onSwap;
    void setGrid (const preset::Grid* g) { grid_ = g != nullptr ? *g : preset::Grid(); haveGrid_ = g != nullptr; repaint(); }
    bool locked() const noexcept { return ! haveGrid_ || grid_.locked; }

    void paint (juce::Graphics& g) override
    {
        for (int t = 0; t < kGridTiles; ++t)
        {
            const auto r = tileRect (t).reduced (2.0f);
            const std::string id = refAt (t);
            const auto* p = id.empty() ? nullptr : c_.p.library().find (id);
            const bool current = ! id.empty() && id == c_.p.currentPresetId();
            const bool missing = ! id.empty() && p == nullptr;
            g.setColour (c_.colour (current ? Token::Accent : Token::BgCard).withAlpha (current ? 0.55f : 1.0f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (c_.colour (missing ? Token::Pink : (dragTo_ == t ? Token::Accent : Token::CtlBd)));
            g.drawRoundedRectangle (r, 6.0f, dragTo_ == t ? 2.0f : 1.0f);
            g.setColour (c_.colour (Token::TextDim));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (t + 1), r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
            g.setColour (c_.colour (missing ? Token::Pink : Token::Text));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (p != nullptr ? U (p->name) : (missing ? c_.text ("bank.missing") : juce::String()), r.reduced (6.0f, 14.0f).toNearestInt(),
                              juce::Justification::centred, 2);
            if (! locked()) // poignée de glisser-déposer, dans un coin
            {
                const auto h = handleRect (t);
                g.setColour (c_.colour (Token::Handle));
                g.fillRoundedRectangle (h, 3.0f);
                g.setColour (c_.colour (Token::BgCard));
                for (int i = 0; i < 3; ++i) g.drawHorizontalLine (static_cast<int> (h.getY()) + 4 + i * 4, h.getX() + 4.0f, h.getRight() - 4.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int t = tileAt (e.position);
        if (t < 0) return;
        dragFrom_ = (! locked() && handleRect (t).contains (e.position)) ? t : -1;
        dragTo_ = -1;
        if (dragFrom_ < 0 && onTileClicked) onTileClicked (t);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom_ < 0) return;
        const int t = tileAt (e.position);
        if (t != dragTo_) { dragTo_ = t; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragFrom_ >= 0)
        {
            const int to = tileAt (e.position);
            if (to >= 0 && to != dragFrom_ && onSwap) onSwap (dragFrom_, to);
        }
        dragFrom_ = dragTo_ = -1;
        repaint();
    }

private:
    juce::Rectangle<float> tileRect (int t) const
    {
        const float w = static_cast<float> (getWidth()) / kGridColumns, h = static_cast<float> (getHeight()) / kGridRows;
        return { static_cast<float> (t % kGridColumns) * w, static_cast<float> (t / kGridColumns) * h, w, h };
    }
    juce::Rectangle<float> handleRect (int t) const
    {
        const auto r = tileRect (t).reduced (2.0f);
        return { r.getRight() - 24.0f, r.getY() + 3.0f, 20.0f, 20.0f };
    }
    int tileAt (juce::Point<float> p) const
    {
        if (! getLocalBounds().toFloat().contains (p)) return -1;
        const int col = static_cast<int> (p.x / (static_cast<float> (getWidth()) / kGridColumns));
        const int row = static_cast<int> (p.y / (static_cast<float> (getHeight()) / kGridRows));
        return juce::jlimit (0, kGridTiles - 1, row * kGridColumns + col);
    }
    std::string refAt (int t) const
    {
        for (const auto& tile : grid_.tiles)
            if (tile.position == t) return tile.presetRef;
        return {};
    }

    Ctx& c_;
    preset::Grid grid_;
    bool haveGrid_ = false;
    int dragFrom_ = -1, dragTo_ = -1;
};

class GridTab : public juce::Component
{
public:
    explicit GridTab (Ctx& c) : c_ (c), tiles_ (c)
    {
        c.combo (gridBox_);
        gridBox_.onChange = [this]
        {
            if (refreshing_) return;
            const int idx = gridBox_.getSelectedId() - 1;
            const auto& grids = c_.p.library().grids();
            c_.gridId = idx >= 0 && idx < static_cast<int> (grids.size()) ? grids[static_cast<std::size_t> (idx)].gridId : std::string();
            refresh();
        };
        addAndMakeVisible (gridBox_);
        c.editor (name_);
        name_.onReturnKey = [this] { renameGrid(); };
        name_.onFocusLost = [this] { renameGrid(); };
        addAndMakeVisible (name_);
        c.label (linkedL_, c.text ("grid.linked"));
        addAndMakeVisible (linkedL_);
        c.editor (linked_);
        linked_.onReturnKey = [this] { renameGrid(); };
        linked_.onFocusLost = [this] { renameGrid(); };
        addAndMakeVisible (linked_);
        lock_.setLabels (c.text ("grid.locked"), c.text ("grid.unlocked"));
        lock_.onToggled = [this] (bool locked)
        {
            if (auto* g = current())
            {
                auto copy = *g;
                copy.locked = locked;
                c_.p.library().saveGrid (copy);
                refresh();
            }
        };
        addAndMakeVisible (lock_);
        auto button = [this] (juce::TextButton& b, const char* key, std::function<void()> fn)
        {
            b.setButtonText (c_.text (key));
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };
        button (new_, "grid.new", [this] { newGrid(); });
        button (delete_, "grid.delete", [this] { deleteGrid(); });
        button (repair_, "grid.repair", [this] { repairGrid(); });
        c.label (problems_, {}, false);
        problems_.setColour (juce::Label::textColourId, c.colour (Token::Pink));
        addAndMakeVisible (problems_);
        addAndMakeVisible (tiles_);

        tiles_.onTileClicked = [this] (int t) { tileClicked (t); };
        tiles_.onSwap = [this] (int from, int to)
        {
            if (auto* g = current())
            {
                auto copy = *g;
                copy.swapTiles (from, to);
                c_.p.library().saveGrid (copy);
                refresh();
            }
        };
        refresh();
    }

    void refresh()
    {
        refreshing_ = true;
        const auto& grids = c_.p.library().grids();
        gridBox_.clear (juce::dontSendNotification);
        int sel = 0, id = 1;
        for (const auto& g : grids)
        {
            gridBox_.addItem (g.name.empty() ? c_.text ("grid.unnamed") : U (g.name), id);
            if (g.gridId == c_.gridId) sel = id;
            ++id;
        }
        if (sel == 0 && ! grids.empty()) { sel = 1; c_.gridId = grids.front().gridId; }
        gridBox_.setSelectedId (sel, juce::dontSendNotification);
        refreshing_ = false;
        const auto* g = current();
        tiles_.setGrid (g);
        name_.setText (g != nullptr ? U (g->name) : juce::String(), false);
        linked_.setText (g != nullptr ? U (g->linkedConcertField) : juce::String(), false);
        lock_.setLocked (g == nullptr || g->locked);
        for (auto* e : { &name_, &linked_ }) e->setEnabled (g != nullptr && ! g->locked);
        delete_.setEnabled (g != nullptr && ! g->locked);
        const auto missing = g != nullptr ? c_.p.library().gridProblems (*g) : std::vector<std::string>();
        problems_.setText (missing.empty() ? juce::String() : juce::String (static_cast<int> (missing.size())) + " " + c_.text ("grid.problems"),
                           juce::dontSendNotification);
        repair_.setVisible (! missing.empty());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30);
        gridBox_.setBounds (top.removeFromLeft (220).reduced (1));
        new_.setBounds (top.removeFromLeft (90).reduced (2, 0));
        delete_.setBounds (top.removeFromLeft (90).reduced (2, 0));
        top.removeFromLeft (10);
        lock_.setBounds (top.removeFromLeft (130).reduced (1));
        top.removeFromLeft (10);
        problems_.setBounds (top.removeFromLeft (170));
        repair_.setBounds (top.removeFromLeft (110).reduced (2, 0));
        r.removeFromTop (6);
        auto second = r.removeFromTop (26);
        name_.setBounds (second.removeFromLeft (second.getWidth() / 2).reduced (1));
        linkedL_.setBounds (second.removeFromLeft (110));
        linked_.setBounds (second.reduced (1));
        r.removeFromTop (8);
        tiles_.setBounds (r);
    }

private:
    const preset::Grid* current() const
    {
        for (const auto& g : c_.p.library().grids())
            if (g.gridId == c_.gridId) return &g;
        return nullptr;
    }

    void renameGrid()
    {
        auto* g = current();
        if (g == nullptr || g->locked) return;
        if (S (name_.getText()) == g->name && S (linked_.getText()) == g->linkedConcertField) return;
        auto copy = *g;
        copy.name = S (name_.getText().trim());
        copy.linkedConcertField = S (linked_.getText().trim());
        c_.p.library().saveGrid (copy);
        refresh();
    }

    void newGrid()
    {
        std::string concert;
        if (const auto* p = c_.selected.empty() ? nullptr : c_.p.library().find (c_.selected))
            for (const auto& f : p->customFields)
                if (f.key == "Concert") { concert = f.value; break; }
        auto& g = c_.p.library().newGrid (concert.empty() ? S (c_.text ("grid.new_name")) : concert);
        g.linkedConcertField = concert; // suggestion de départ, jamais recalculée ensuite
        g.locked = false;               // une grille neuve se remplit tout de suite
        const auto copy = g;
        c_.p.library().saveGrid (copy);
        c_.gridId = copy.gridId;
        refresh();
    }

    void deleteGrid()
    {
        const auto* g = current();
        if (g == nullptr) return;
        const auto id = g->gridId;
        c_.confirm (c_.text ("grid.confirm_delete") + " « " + U (g->name) + " » ?", [this, id]
        {
            c_.p.library().deleteGrid (id);
            c_.gridId.clear();
            c_.changed();
        });
    }

    void repairGrid()
    {
        const auto* g = current();
        if (g == nullptr || g->locked) { c_.status (c_.text ("grid.unlock_first")); return; }
        auto copy = *g;
        const auto missing = c_.p.library().gridProblems (copy);
        for (auto& t : copy.tiles)
            if (std::find (missing.begin(), missing.end(), t.presetRef) != missing.end()) t.presetRef.clear();
        c_.p.library().saveGrid (copy);
        refresh();
    }

    void tileClicked (int t)
    {
        const auto* g = current();
        if (g == nullptr) return;
        std::string present;
        for (const auto& tile : g->tiles)
            if (tile.position == t) present = tile.presetRef;
        if (g->locked)
        {
            if (present.empty()) { c_.status (c_.text ("grid.empty_tile")); return; } // le preset actuel reste chargé
            if (c_.p.loadPreset (present)) { c_.status (c_.text ("preset.loaded")); c_.loaded(); }
            else c_.status (c_.text ("preset.load_failed"));
            return;
        }
        if (c_.selected.empty()) { c_.status (c_.text ("bank.no_selection")); return; }
        const auto place = [this, t, id = c_.selected, gid = g->gridId]
        {
            for (const auto& grid : c_.p.library().grids())
                if (grid.gridId == gid)
                {
                    auto copy = grid;
                    copy.assign (t, id);
                    c_.p.library().saveGrid (copy);
                    refresh();
                    return;
                }
        };
        if (! present.empty() && present != c_.selected) c_.confirm (c_.text ("grid.confirm_overwrite"), place);
        else place();
    }

    Ctx& c_;
    bool refreshing_ = false;
    juce::ComboBox gridBox_;
    juce::TextEditor name_, linked_;
    juce::Label linkedL_, problems_;
    LongPressLock lock_;
    juce::TextButton new_, delete_, repair_;
    TileGrid tiles_;
};

// ---------------------------------------------------------------------------------------------------- onglet Corbeille
class TrashTab : public juce::Component
{
public:
    explicit TrashTab (Ctx& c) : c_ (c)
    {
        c.list (list_);
        model_.count = [this] { return static_cast<int> (entries_.size()); };
        model_.paint = [this] (int row, juce::Graphics& g, int w, int h, bool sel)
        {
            if (row < 0 || row >= static_cast<int> (entries_.size())) return;
            c_.rowBackground (g, w, h, sel);
            const auto& e = entries_[static_cast<std::size_t> (row)];
            g.setColour (c_.colour (Token::Text));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (U (e.name), 8, 0, w - 220, h, juce::Justification::centredLeft, true);
            g.setColour (c_.colour (Token::TextDim));
            g.drawText (juce::Time (e.deletedAt * 1000).formatted ("%Y-%m-%d %H:%M"), w - 200, 0, 190, h, juce::Justification::centredRight);
        };
        list_.setModel (&model_);
        addAndMakeVisible (list_);
        restore_.setButtonText (c.text ("trash.restore"));
        restore_.onClick = [this]
        {
            const int row = list_.getSelectedRow();
            if (row < 0 || row >= static_cast<int> (entries_.size())) return;
            std::string error;
            if (c_.p.library().restore (entries_[static_cast<std::size_t> (row)], &error)) { c_.status (c_.text ("trash.restored")); c_.changed(); }
            else c_.status (U (error));
        };
        addAndMakeVisible (restore_);
        empty_.setButtonText (c.text ("trash.empty"));
        empty_.onClick = [this]
        {
            if (entries_.empty()) return;
            c_.confirm (c_.text ("trash.confirm_empty"), [this] { c_.p.library().emptyTrash(); c_.changed(); });
        };
        addAndMakeVisible (empty_);
        c.label (hint_, c.text ("trash.hint"));
        addAndMakeVisible (hint_);
        refresh();
    }

    void refresh()
    {
        entries_ = c_.p.library().trash();
        list_.updateContent();
        list_.repaint();
        restore_.setEnabled (! entries_.empty());
        empty_.setEnabled (! entries_.empty());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (34);
        hint_.setBounds (bottom.removeFromLeft (bottom.getWidth() - 320));
        restore_.setBounds (bottom.removeFromLeft (160).reduced (2));
        empty_.setBounds (bottom.reduced (2));
        list_.setBounds (r);
    }

private:
    Ctx& c_;
    std::vector<preset::TrashEntry> entries_;
    FnListModel model_;
    juce::ListBox list_ { "trash", nullptr };
    juce::TextButton restore_, empty_;
    juce::Label hint_;
};

// ---------------------------------------------------------------------------------------------------- onglet Réglages
class ConfigTab : public juce::Component
{
public:
    explicit ConfigTab (Ctx& c) : c_ (c)
    {
        c.label (intro_, c.text ("config.intro"));
        addAndMakeVisible (intro_);
        for (const auto& key : c.p.presetKeys())
        {
            auto* t = toggles_.add (new juce::ToggleButton());
            c.toggle (*t, U (key));
            keys_.push_back (key);
            content_.addAndMakeVisible (t);
        }
        viewport_.setViewedComponent (&content_, false);
        viewport_.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport_);
        c.slider (retention_, 0, 365);
        c.slider (maxEntries_, 0, 500);
        c.label (retentionL_, c.text ("config.retention"));
        c.label (maxL_, c.text ("config.max_entries"));
        for (juce::Component* x : std::initializer_list<juce::Component*> { &retention_, &maxEntries_, &retentionL_, &maxL_ }) addAndMakeVisible (x);
        apply_.setButtonText (c.text ("config.apply"));
        apply_.onClick = [this] { apply (false); };
        reset_.setButtonText (c.text ("config.reset"));
        reset_.onClick = [this] { apply (true); };
        addAndMakeVisible (apply_);
        addAndMakeVisible (reset_);
        refresh();
    }

    void refresh()
    {
        const auto& cfg = c_.p.presetConfig();
        preset::Preset none;
        for (int i = 0; i < toggles_.size(); ++i)
            toggles_[i]->setToggleState (preset::captureOf (keys_[static_cast<std::size_t> (i)], none, cfg) == preset::Capture::Captured, juce::dontSendNotification);
        retention_.setValue (cfg.trashRetentionDays, juce::dontSendNotification);
        maxEntries_.setValue (cfg.trashMaxEntries, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        intro_.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto bottom = r.removeFromBottom (92);
        viewport_.setBounds (r);
        const int cols = 2, rowH = 26;
        const int rows = (toggles_.size() + cols - 1) / cols;
        content_.setSize (juce::jmax (100, viewport_.getMaximumVisibleWidth()), rows * rowH);
        for (int i = 0; i < toggles_.size(); ++i)
            toggles_[i]->setBounds ((i % cols) * (content_.getWidth() / cols), (i / cols) * rowH, content_.getWidth() / cols - 6, rowH);
        auto row1 = bottom.removeFromTop (30);
        retentionL_.setBounds (row1.removeFromLeft (190));
        retention_.setBounds (row1.removeFromLeft (320));
        auto row2 = bottom.removeFromTop (30);
        maxL_.setBounds (row2.removeFromLeft (190));
        maxEntries_.setBounds (row2.removeFromLeft (320));
        auto row3 = bottom.removeFromTop (30);
        apply_.setBounds (row3.removeFromLeft (150).reduced (2));
        reset_.setBounds (row3.removeFromLeft (220).reduced (2));
    }

private:
    void apply (bool defaults)
    {
        auto cfg = defaults ? preset::LibraryConfig {} : c_.p.presetConfig();
        if (defaults) for (const auto& key : keys_) if (key == "output.volume" || key == "output.pan" || key == analogsynth::glideKey) cfg.parameterCaptureDefaults[key] = preset::Capture::Free;
        if (! defaults)
        {
            cfg.parameterCaptureDefaults.clear();
            for (int i = 0; i < toggles_.size(); ++i)
                if (! toggles_[i]->getToggleState())
                    cfg.parameterCaptureDefaults[keys_[static_cast<std::size_t> (i)]] = preset::Capture::Free;
            cfg.trashRetentionDays = static_cast<int> (retention_.getValue());
            cfg.trashMaxEntries = static_cast<int> (maxEntries_.getValue());
        }
        if (c_.p.saveConfig (cfg)) c_.status (c_.text ("preset.saved"));
        refresh();
    }

    Ctx& c_;
    juce::Label intro_, retentionL_, maxL_;
    juce::OwnedArray<juce::ToggleButton> toggles_;
    std::vector<std::string> keys_;
    juce::Component content_;
    juce::Viewport viewport_;
    juce::Slider retention_, maxEntries_;
    juce::TextButton apply_, reset_;
};
} // namespace

// ---------------------------------------------------------------------------------------------------- PresetManager
struct PresetManager::Impl
{
    Impl (PresetManager& o, PresetHost& p, const neptune::core::theme::Palette& pal, const neptune::core::i18n::Translator& tr)
        : owner (o), ctx { p, pal, tr, {}, {}, {}, {}, {}, {} }, presets (ctx), banks (ctx), grid (ctx), trash (ctx), config (ctx)
    {
        ctx.selected = p.currentPresetId();
        ctx.confirm = [this] (const juce::String& message, std::function<void()> onYes)
        {
            pendingYes = std::move (onYes);
            confirmLabel.setText (message, juce::dontSendNotification);
            showConfirm (true);
        };
        ctx.status = [this] (const juce::String& s) { statusLabel.setText (s, juce::dontSendNotification); };
        ctx.changed = [this]
        {
            ctx.p.touchPresets();
            refreshAll();
            if (owner.onChanged) owner.onChanged();
        };
        ctx.loaded = [this]
        {
            refreshAll();
            if (owner.onChanged) owner.onChanged();
        };

        const char* tabKeys[kNumTabs] = { "preset.tab_presets", "preset.tab_banks", "preset.tab_grid", "preset.tab_trash", "preset.tab_config" };
        for (int i = 0; i < kNumTabs; ++i)
        {
            tabButtons[i].setButtonText (ctx.text (tabKeys[i]));
            tabButtons[i].setClickingTogglesState (false);
            tabButtons[i].onClick = [this, i] { setTab (i); };
            owner.addAndMakeVisible (tabButtons[i]);
        }
        tabComponents[kTabPresets] = &presets;
        tabComponents[kTabBanks] = &banks;
        tabComponents[kTabGrid] = &grid;
        tabComponents[kTabTrash] = &trash;
        tabComponents[kTabConfig] = &config;
        for (auto* t : tabComponents) owner.addChildComponent (*t);

        title.setText (ctx.text ("preset.manager_title"), juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, ctx.colour (Token::Text));
        title.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        owner.addAndMakeVisible (title);
        statusLabel.setColour (juce::Label::textColourId, ctx.colour (Token::Accent));
        statusLabel.setFont (juce::FontOptions (12.0f));
        owner.addAndMakeVisible (statusLabel);
        closeButton.setButtonText (ctx.text ("modal.close"));
        closeButton.onClick = [this] { if (owner.onClose) owner.onClose(); };
        owner.addAndMakeVisible (closeButton);

        confirmLabel.setColour (juce::Label::textColourId, ctx.colour (Token::Text));
        confirmLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        owner.addChildComponent (confirmLabel);
        yesButton.setButtonText (ctx.text ("preset.yes"));
        noButton.setButtonText (ctx.text ("preset.no"));
        yesButton.onClick = [this]
        {
            auto fn = std::move (pendingYes);
            pendingYes = nullptr;
            showConfirm (false);
            if (fn) fn();
        };
        noButton.onClick = [this] { pendingYes = nullptr; showConfirm (false); };
        owner.addChildComponent (yesButton);
        owner.addChildComponent (noButton);
        setTab (kTabPresets);
    }

    void setTab (int t)
    {
        tab = t;
        for (int i = 0; i < kNumTabs; ++i)
        {
            tabComponents[i]->setVisible (i == t);
            tabButtons[i].setToggleState (i == t, juce::dontSendNotification);
        }
        refreshAll();
    }

    void showConfirm (bool on)
    {
        confirmLabel.setVisible (on);
        yesButton.setVisible (on);
        noButton.setVisible (on);
        owner.resized();
    }

    void refreshAll()
    {
        presets.refresh();
        banks.refresh();
        grid.refresh();
        trash.refresh();
        config.refresh();
        owner.repaint();
    }

    PresetManager& owner;
    Ctx ctx;
    PresetsTab presets;
    BanksTab banks;
    GridTab grid;
    TrashTab trash;
    ConfigTab config;
    juce::Component* tabComponents[kNumTabs] {};
    juce::TextButton tabButtons[kNumTabs];
    juce::Label title, statusLabel, confirmLabel;
    juce::TextButton closeButton, yesButton, noButton;
    std::function<void()> pendingYes;
    int tab = 0;
    juce::Rectangle<int> panel;
};

PresetManager::PresetManager (PresetHost& p, const neptune::core::theme::Palette& pal, const neptune::core::i18n::Translator& tr)
{
    setWantsKeyboardFocus (true);
    impl_ = std::make_unique<Impl> (*this, p, pal, tr);
}

PresetManager::~PresetManager() = default;

void PresetManager::refresh() { impl_->refreshAll(); }

void PresetManager::showSaveAs()
{
    impl_->setTab (kTabPresets);
    impl_->presets.focusName();
}

void PresetManager::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto b = impl_->panel.toFloat();
    g.setColour (impl_->ctx.colour (Token::BgCard));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (impl_->ctx.colour (Token::Border));
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
}

void PresetManager::resized()
{
    auto& m = *impl_;
    m.panel = getLocalBounds().reduced (juce::jmax (16, getWidth() / 20), juce::jmax (12, getHeight() / 24));
    auto r = m.panel.reduced (16, 12);
    auto top = r.removeFromTop (32);
    m.closeButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    m.title.setBounds (top.removeFromLeft (200));
    for (int i = 0; i < kNumTabs; ++i) m.tabButtons[i].setBounds (top.removeFromLeft (juce::jmin (120, top.getWidth() / (kNumTabs - i))).reduced (2, 0));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (30);
    if (m.confirmLabel.isVisible())
    {
        m.noButton.setBounds (bottom.removeFromRight (80).reduced (2));
        m.yesButton.setBounds (bottom.removeFromRight (80).reduced (2));
        m.confirmLabel.setBounds (bottom);
        m.statusLabel.setBounds ({});
    }
    else
    {
        m.statusLabel.setBounds (bottom);
        m.confirmLabel.setBounds ({});
    }
    r.removeFromBottom (6);
    for (auto* t : m.tabComponents) t->setBounds (r);
}

void PresetManager::mouseDown (const juce::MouseEvent& e)
{
    if (! impl_->panel.contains (e.getPosition()) && onClose) onClose();
}

bool PresetManager::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (impl_->confirmLabel.isVisible()) impl_->noButton.onClick();
        else if (onClose) onClose();
        return true;
    }
    return false;
}

void PresetManager::parentSizeChanged()
{
    if (auto* parent = getParentComponent()) setBounds (parent->getLocalBounds());
}
