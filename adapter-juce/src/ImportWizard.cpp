#include "ImportWizard.h"
#include <algorithm>
#include <filesystem>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
std::string megabytes (std::uint64_t bytes) { return std::to_string (static_cast<long long> (bytes / (1024ull * 1024ull))); }

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

std::string stemOf (const std::string& path) { return std::filesystem::u8path (path).stem().string(); }
}

// ---- Nom d'exemple en cases : on glisse sur des caractères pour leur donner un rôle ----

class ImportWizard::ExampleStrip : public juce::Component
{
public:
    explicit ExampleStrip (ImportWizard& o) : owner_ (o) {}
    void setStem (const std::string& s) { stem_ = u8 (s); sel_ = { -1, -1 }; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const int n = stem_.length();
        const float cw = cellWidth();
        g.setFont (juce::FontOptions (16.0f));
        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<float> r (static_cast<float> (i) * cw, 0.0f, cw, static_cast<float> (getHeight()));
            const bool selected = sel_.first >= 0 && i >= std::min (sel_.first, sel_.second) && i <= std::max (sel_.first, sel_.second);
            g.setColour (selected ? owner_.colour (Token::Accent).withAlpha (0.5f) : owner_.colour (Token::BgCard));
            g.fillRect (r.reduced (1.0f));
            g.setColour (owner_.colour (Token::Text));
            g.drawText (juce::String::charToString (stem_[i]), r.toNearestInt(), juce::Justification::centred);
            g.setColour (owner_.colour (Token::TextDim2));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (i + 1), r.toNearestInt().removeFromBottom (11), juce::Justification::centred);
            g.setFont (juce::FontOptions (16.0f));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { sel_.first = sel_.second = indexAt (e.position.x); repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override { sel_.second = indexAt (e.position.x); repaint(); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (sel_.first < 0) return;
        owner_.addRuleFromSelection (std::min (sel_.first, sel_.second), std::max (sel_.first, sel_.second));
        sel_ = { -1, -1 };
        repaint();
    }

private:
    float cellWidth() const { return std::min (28.0f, static_cast<float> (getWidth()) / static_cast<float> (std::max (1, stem_.length()))); }
    int indexAt (float x) const { return juce::jlimit (0, std::max (0, stem_.length() - 1), static_cast<int> (x / cellWidth())); }

    ImportWizard& owner_;
    juce::String stem_;
    std::pair<int, int> sel_ { -1, -1 };
};

// ---- Une règle : rôle + segment, début, longueur min / max ----

class ImportWizard::RuleRow : public juce::Component
{
public:
    RuleRow (ImportWizard& o, int index) : owner_ (o), index_ (index)
    {
        const auto& f = owner_.template_.fields[static_cast<std::size_t> (index)];
        label_.setText (owner_.t ((std::string ("role.") + import::roleKey (f.role)).c_str()), juce::dontSendNotification);
        label_.setColour (juce::Label::textColourId, owner_.colour (Token::Text));
        addAndMakeVisible (label_);
        auto setup = [this] (juce::TextEditor& e, int value, const char* key)
        {
            e.setText (juce::String (value), false);
            e.setInputRestrictions (4, "-0123456789");
            e.setTextToShowWhenEmpty (owner_.t (key), owner_.colour (Token::TextDim2));
            e.onTextChange = [this] { push(); };
            e.setColour (juce::TextEditor::backgroundColourId, owner_.colour (Token::BgCard));
            e.setColour (juce::TextEditor::textColourId, owner_.colour (Token::Text));
            addAndMakeVisible (e);
        };
        setup (segment_, f.segment, "import.segment");
        setup (start_, f.start, "import.start");
        setup (min_, f.minLength, "import.min");
        setup (max_, f.maxLength, "import.max");
        remove_.setButtonText ("x");
        remove_.onClick = [this] { owner_.template_.fields.erase (owner_.template_.fields.begin() + index_); owner_.rebuildRules(); owner_.recompute(); };
        addAndMakeVisible (remove_);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        remove_.setBounds (r.removeFromRight (36).reduced (2));
        max_.setBounds (r.removeFromRight (64).reduced (2));
        min_.setBounds (r.removeFromRight (64).reduced (2));
        start_.setBounds (r.removeFromRight (64).reduced (2));
        segment_.setBounds (r.removeFromRight (80).reduced (2));
        label_.setBounds (r);
    }

private:
    void push()
    {
        if (index_ >= static_cast<int> (owner_.template_.fields.size())) return;
        auto& f = owner_.template_.fields[static_cast<std::size_t> (index_)];
        f.segment = segment_.getText().getIntValue();
        f.start = start_.getText().getIntValue() == 0 ? 1 : start_.getText().getIntValue();
        f.minLength = std::max (1, min_.getText().getIntValue());
        f.maxLength = std::max (0, max_.getText().getIntValue());
        owner_.recompute();
    }

    ImportWizard& owner_;
    int index_;
    juce::Label label_;
    juce::TextEditor segment_, start_, min_, max_;
    juce::TextButton remove_;
};

// ---- Une expression : mot + portée + ce que les chiffres suivants signifient + déclenchement ----

class ImportWizard::KeywordRow : public juce::Component
{
public:
    KeywordRow (ImportWizard& o, int index) : owner_ (o), index_ (index)
    {
        const auto& k = owner_.template_.keywords[static_cast<std::size_t> (index)];
        auto style = [this] (juce::TextEditor& e, const juce::String& text, int maxLen, const juce::String& allowed)
        {
            e.setText (text, false);
            e.setInputRestrictions (maxLen, allowed);
            e.setColour (juce::TextEditor::backgroundColourId, owner_.colour (Token::BgCard));
            e.setColour (juce::TextEditor::textColourId, owner_.colour (Token::Text));
            e.onTextChange = [this] { push(); };
            addAndMakeVisible (e);
        };
        style (word_, u8 (k.keyword), 16, {});
        style (position_, juce::String (k.scope == import::KeywordScope::InSegment ? k.segment : k.start), 4, "-0123456789");
        style (digitsMin_, juce::String (k.digitsMin), 1, "123456789");
        style (digitsMax_, juce::String (k.digitsMax), 1, "123456789");
        style (group_, u8 (k.group), 32, {});
        group_.setTextToShowWhenEmpty (owner_.t ("import.col_group"), owner_.colour (Token::TextDim2));

        scope_.addItem (owner_.t ("import.scope_any"), 1);
        scope_.addItem (owner_.t ("import.scope_pos"), 2);
        scope_.addItem (owner_.t ("import.scope_seg"), 3);
        scope_.setSelectedId (k.scope == import::KeywordScope::AtPosition ? 2 : (k.scope == import::KeywordScope::InSegment ? 3 : 1),
                              juce::dontSendNotification);
        scope_.onChange = [this] { push(); };
        addAndMakeVisible (scope_);

        value_.addItem (owner_.t ("import.value_none"), 1);
        for (int i = 1; i < import::numRoles; ++i)
            value_.addItem (owner_.t ((std::string ("role.") + import::roleKey (static_cast<import::Role> (i))).c_str()), i + 1);
        value_.setSelectedId (static_cast<int> (k.valueRole) == 0 ? 1 : static_cast<int> (k.valueRole) + 1, juce::dontSendNotification);
        value_.onChange = [this] { push(); };
        addAndMakeVisible (value_);

        trigger_.addItem (owner_.t ("import.trigger_unchanged"), 1);
        trigger_.addItem (owner_.t ("import.trigger_attack"), 2);
        trigger_.addItem (owner_.t ("import.trigger_release"), 3);
        trigger_.setSelectedId (k.trigger == import::Trigger::Attack ? 2 : (k.trigger == import::Trigger::Release ? 3 : 1), juce::dontSendNotification);
        trigger_.onChange = [this] { push(); };
        addAndMakeVisible (trigger_);

        remove_.setButtonText ("x");
        remove_.onClick = [this] { owner_.template_.keywords.erase (owner_.template_.keywords.begin() + index_); owner_.rebuildKeywords(); owner_.recompute(); };
        addAndMakeVisible (remove_);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        remove_.setBounds (r.removeFromRight (36).reduced (2));
        group_.setBounds (r.removeFromRight (90).reduced (2));
        trigger_.setBounds (r.removeFromRight (150).reduced (2));
        value_.setBounds (r.removeFromRight (210).reduced (2));
        digitsMax_.setBounds (r.removeFromRight (34).reduced (2));
        digitsMin_.setBounds (r.removeFromRight (34).reduced (2));
        position_.setBounds (r.removeFromRight (56).reduced (2));
        scope_.setBounds (r.removeFromRight (150).reduced (2));
        word_.setBounds (r.reduced (2));
    }

private:
    void push()
    {
        if (index_ >= static_cast<int> (owner_.template_.keywords.size())) return;
        auto& k = owner_.template_.keywords[static_cast<std::size_t> (index_)];
        k.keyword = word_.getText().toStdString();
        const int sc = scope_.getSelectedId();
        k.scope = sc == 2 ? import::KeywordScope::AtPosition : (sc == 3 ? import::KeywordScope::InSegment : import::KeywordScope::Anywhere);
        const int pos = position_.getText().getIntValue();
        if (k.scope == import::KeywordScope::InSegment) { k.segment = pos == 0 ? 1 : pos; k.start = 0; }
        else { k.start = pos == 0 ? 1 : pos; k.segment = 0; }
        k.digitsMin = std::max (1, digitsMin_.getText().getIntValue());
        k.digitsMax = std::max (k.digitsMin, digitsMax_.getText().getIntValue());
        const int v = value_.getSelectedId();
        k.valueRole = v <= 1 ? import::Role::Ignore : static_cast<import::Role> (v - 1);
        const int tr = trigger_.getSelectedId();
        k.trigger = tr == 2 ? import::Trigger::Attack : (tr == 3 ? import::Trigger::Release : import::Trigger::Unchanged);
        k.group = group_.getText().toStdString();
        owner_.recompute();
    }

    ImportWizard& owner_;
    int index_;
    juce::TextEditor word_, position_, digitsMin_, digitsMax_, group_;
    juce::ComboBox scope_, value_, trigger_;
    juce::TextButton remove_;
};

// ---- Assistant ----

void ImportWizard::Panel::paint (juce::Graphics& g)
{
    g.setColour (owner->colour (Token::BgApp));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (owner->colour (Token::Border));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
}

juce::String ImportWizard::t (const char* key) const { return u8 (std::string (tr_.tr (key))); }

juce::Colour ImportWizard::colour (Token tk) const
{
    const auto& c = palette_.color (tk);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

ImportWizard::ImportWizard (std::vector<std::string> paths, const theme::Palette& palette, const i18n::Translator& tr,
                            import::Template initial, InstrumentDescription existing, memory::FileInfoCache& fileInfo,
                            std::uint64_t availableBytes, std::function<void (const Result&)> onImport, std::function<void()> onCancel)
    : paths_ (std::move (paths)), existing_ (std::move (existing)), fileInfo_ (fileInfo), availableBytes_ (availableBytes),
      palette_ (palette), tr_ (tr), template_ (std::move (initial)),
      onImport_ (std::move (onImport)), onCancel_ (std::move (onCancel))
{
    std::sort (paths_.begin(), paths_.end());
    exampleStem_ = paths_.empty() ? std::string() : stemOf (paths_.front());

    panel_.owner = this;
    addAndMakeVisible (panel_);
    auto label = [this] (juce::Label& l, const char* key, bool bold = false)
    {
        l.setText (t (key), juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colour (bold ? Token::Text : Token::TextDim));
        if (bold) l.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        panel_.addAndMakeVisible (l);
    };
    label (titleLabel_, "import.title", true);
    label (exampleLabel_, "import.example");
    label (rulesLabel_, "import.rules");
    label (delimiterLabel_, "import.delimiter");
    label (octaveLabel_, "import.octave");
    label (keysLabel_, "import.keys");
    label (velLabel_, "import.velocities");
    label (layersLabel_, "import.layers");
    label (rrLabel_, "import.rr_mode");
    label (keywordsLabel_, "import.keywords");
    addKeywordButton_.setButtonText (t ("import.add_keyword"));
    addKeywordButton_.onClick = [this] { import::KeywordRule k; k.keyword = "REL"; k.trigger = import::Trigger::Release; addKeyword (k); };
    panel_.addAndMakeVisible (addKeywordButton_);
    keywordsViewport_.setViewedComponent (&keywordsHolder_, false);
    panel_.addAndMakeVisible (keywordsViewport_);

    auto edit = [this] (juce::TextEditor& e, const juce::String& text, int maxLen, const juce::String& allowed)
    {
        e.setText (text, false);
        e.setInputRestrictions (maxLen, allowed);
        e.setColour (juce::TextEditor::backgroundColourId, colour (Token::BgCard));
        e.setColour (juce::TextEditor::textColourId, colour (Token::Text));
        e.onTextChange = [this] { recompute(); };
        panel_.addAndMakeVisible (e);
    };
    edit (delimiterEdit_, u8 (template_.delimiter), 8, {});
    delimiterEdit_.onTextChange = [this] { template_.delimiter = delimiterEdit_.getText().toStdString(); recompute(); };
    edit (belowKeys_, "0", 3, "0123456789");
    edit (aboveKeys_, "0", 3, "0123456789");
    edit (belowVel_, "0", 3, "0123456789");
    edit (aboveVel_, "0", 3, "0123456789");
    edit (layersEdit_, "0", 4, "0123456789");

    for (int oct : { 3, 4, 5 }) octaveBox_.addItem ("C" + juce::String (oct), oct);
    octaveBox_.setSelectedId (template_.notes.middleCOctave >= 3 && template_.notes.middleCOctave <= 5 ? template_.notes.middleCOctave : 4,
                              juce::dontSendNotification);
    octaveBox_.onChange = [this] { template_.notes.middleCOctave = octaveBox_.getSelectedId(); recompute(); };
    panel_.addAndMakeVisible (octaveBox_);

    for (auto* box : { &keyStrategy_, &velStrategy_ })
    {
        box->addItem (t ("import.strategy_exact"), 1);
        box->addItem (t ("import.strategy_midpoint"), 2);
        box->addItem (t ("import.strategy_fixed"), 3);
        box->setSelectedId (1, juce::dontSendNotification);
        box->onChange = [this] { recompute(); };
        panel_.addAndMakeVisible (*box);
    }
    rrMode_.addItem (t ("import.rr_sequential"), 1);
    rrMode_.addItem (t ("import.rr_random"), 2);
    rrMode_.addItem (t ("import.rr_no_repeat"), 3);
    rrMode_.setSelectedId (1, juce::dontSendNotification);
    rrMode_.onChange = [this] { recompute(); };
    panel_.addAndMakeVisible (rrMode_);

    strip_ = std::make_unique<ExampleStrip> (*this);
    strip_->setStem (exampleStem_);
    panel_.addAndMakeVisible (*strip_);

    rulesViewport_.setViewedComponent (&rulesHolder_, false);
    panel_.addAndMakeVisible (rulesViewport_);

    auto& h = table_.getHeader();
    h.addColumn (t ("import.col_file"), 1, 220, 80, 600, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_note"), 2, 70, 40, 120, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_keys"), 3, 80, 40, 140, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_velocity"), 4, 80, 40, 140, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_rr"), 5, 40, 24, 80, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_group"), 6, 90, 40, 200, juce::TableHeaderComponent::notSortable);
    h.addColumn (t ("import.col_status"), 7, 380, 100, 900, juce::TableHeaderComponent::notSortable);
    table_.setRowHeight (24);
    table_.setColour (juce::ListBox::backgroundColourId, colour (Token::BgCard));
    panel_.addAndMakeVisible (table_);

    summaryLabel_.setColour (juce::Label::textColourId, colour (Token::Accent));
    panel_.addAndMakeVisible (summaryLabel_);
    ramLabel_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    panel_.addAndMakeVisible (ramLabel_);

    replaceButton_.setButtonText (t ("import.replace"));
    replaceButton_.onClick = [this] { finish (true); };
    addButton_.setButtonText (t ("import.add"));
    addButton_.onClick = [this] { finish (false); };
    cancelButton_.setButtonText (t ("import.cancel"));
    cancelButton_.onClick = [this] { if (onCancel_) onCancel_(); };
    for (auto* b : { &replaceButton_, &addButton_, &cancelButton_ }) panel_.addAndMakeVisible (*b);

    // Import léger : décocher réduit la mémoire estimée (et ce qui sera chargé). On part de l'état de l'instrument actuel.
    alternatesToggle_.setButtonText (t ("import.light_alternates"));
    releaseToggle_.setButtonText (t ("import.light_release"));
    alternatesToggle_.setToggleState (! existing_.mainRoundRobinOnly, juce::dontSendNotification);
    releaseToggle_.setToggleState (existing_.loadReleaseSamples, juce::dontSendNotification);
    for (auto* b : { &alternatesToggle_, &releaseToggle_ })
    {
        b->setColour (juce::ToggleButton::textColourId, colour (Token::Text));
        b->onClick = [this] { updateMemory(); };
        panel_.addAndMakeVisible (*b);
    }

    rebuildRules();
    rebuildKeywords();
    recompute();
}

ImportWizard::~ImportWizard() = default;

void ImportWizard::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black.withAlpha (0.55f)); }

void ImportWizard::resized()
{
    panel_.setBounds (getLocalBounds().reduced (30, 24));
    auto r = panel_.getLocalBounds().reduced (16);
    titleLabel_.setBounds (r.removeFromTop (30));

    auto row = r.removeFromTop (34);
    delimiterLabel_.setBounds (row.removeFromLeft (90));
    delimiterEdit_.setBounds (row.removeFromLeft (70).reduced (2));
    row.removeFromLeft (24);
    octaveLabel_.setBounds (row.removeFromLeft (230));
    octaveBox_.setBounds (row.removeFromLeft (90).reduced (2));

    exampleLabel_.setBounds (r.removeFromTop (22));
    strip_->setBounds (r.removeFromTop (42));
    rulesLabel_.setBounds (r.removeFromTop (22));
    rulesViewport_.setBounds (r.removeFromTop (4 * 34)); // 4 règles visibles, défilement au-delà
    rulesHolder_.setSize (rulesViewport_.getMaximumVisibleWidth(), std::max (1, ruleRows_.size()) * 34);
    for (int i = 0; i < ruleRows_.size(); ++i) ruleRows_[i]->setBounds (0, i * 34, rulesHolder_.getWidth(), 34);

    auto kwHeader = r.removeFromTop (26);
    addKeywordButton_.setBounds (kwHeader.removeFromRight (140).reduced (2));
    keywordsLabel_.setBounds (kwHeader);
    keywordsViewport_.setBounds (r.removeFromTop (3 * 34));
    keywordsHolder_.setSize (keywordsViewport_.getMaximumVisibleWidth(), std::max (1, keywordRows_.size()) * 34);
    for (int i = 0; i < keywordRows_.size(); ++i) keywordRows_[i]->setBounds (0, i * 34, keywordsHolder_.getWidth(), 34);

    r.removeFromTop (6);
    auto opt = r.removeFromTop (34);
    keysLabel_.setBounds (opt.removeFromLeft (110));
    keyStrategy_.setBounds (opt.removeFromLeft (200).reduced (2));
    belowKeys_.setBounds (opt.removeFromLeft (46).reduced (2));
    aboveKeys_.setBounds (opt.removeFromLeft (46).reduced (2));
    opt.removeFromLeft (20);
    layersLabel_.setBounds (opt.removeFromLeft (150));
    layersEdit_.setBounds (opt.removeFromLeft (56).reduced (2));
    auto opt2 = r.removeFromTop (34);
    velLabel_.setBounds (opt2.removeFromLeft (110));
    velStrategy_.setBounds (opt2.removeFromLeft (200).reduced (2));
    belowVel_.setBounds (opt2.removeFromLeft (46).reduced (2));
    aboveVel_.setBounds (opt2.removeFromLeft (46).reduced (2));
    opt2.removeFromLeft (20);
    rrLabel_.setBounds (opt2.removeFromLeft (110));
    rrMode_.setBounds (opt2.removeFromLeft (200).reduced (2));

    auto bottom = r.removeFromBottom (44);
    {
        auto lightRow = r.removeFromBottom (28);
        alternatesToggle_.setBounds (lightRow.removeFromLeft (320));
        releaseToggle_.setBounds (lightRow.removeFromLeft (320));
    }
    cancelButton_.setBounds (bottom.removeFromRight (120).reduced (2));
    addButton_.setBounds (bottom.removeFromRight (200).reduced (2));
    replaceButton_.setBounds (bottom.removeFromRight (230).reduced (2));
    ramLabel_.setBounds (r.removeFromBottom (40));
    summaryLabel_.setBounds (r.removeFromBottom (26));
    table_.setBounds (r.reduced (0, 4));
}

void ImportWizard::addRuleFromSelection (int from, int to)
{
    if (exampleStem_.empty()) return;
    juce::PopupMenu menu;
    for (int i = 1; i < import::numRoles; ++i) // 0 = Ignore : inutile à créer
        menu.addItem (i, t ((std::string ("role.") + import::roleKey (static_cast<import::Role> (i))).c_str()));
    menu.addSeparator();
    menu.addItem (1000, t ("import.expr_from_selection"));
    const int len = to - from + 1;
    juce::Component::SafePointer<ImportWizard> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (strip_.get()), [safe, from, len] (int chosen)
    {
        if (safe == nullptr || chosen <= 0) return;
        if (chosen == 1000)
        {
            // Le texte sélectionné devient une expression cherchée à cette position (modifiable ensuite).
            import::KeywordRule k;
            k.keyword = safe->exampleStem_.substr (static_cast<std::size_t> (from), static_cast<std::size_t> (len));
            k.scope = import::KeywordScope::AtPosition;
            k.start = from + 1;
            safe->addKeyword (k);
            return;
        }
        import::FieldRule f;
        f.role = static_cast<import::Role> (chosen);
        f.segment = 0;
        f.start = from + 1;
        f.minLength = len;
        f.maxLength = len;
        // Avec un délimiteur, la règle est rattachée au segment qui contient la sélection (plus robuste si la
        // longueur des autres segments varie d'un fichier à l'autre).
        const auto& d = safe->template_.delimiter;
        if (! d.empty())
        {
            std::size_t segStart = 0;
            int segIndex = 1;
            const std::string& stem = safe->exampleStem_;
            while (true)
            {
                const auto next = stem.find (d, segStart);
                const std::size_t segEnd = next == std::string::npos ? stem.size() : next;
                if (static_cast<std::size_t> (from) >= segStart && static_cast<std::size_t> (from + len) <= segEnd)
                {
                    f.segment = segIndex;
                    f.start = static_cast<int> (static_cast<std::size_t> (from) - segStart) + 1;
                    break;
                }
                if (next == std::string::npos) break;
                segStart = next + d.size();
                ++segIndex;
            }
        }
        safe->template_.fields.push_back (f);
        safe->rebuildRules();
        safe->recompute();
    });
}

void ImportWizard::addKeyword (import::KeywordRule k)
{
    if (template_.keywords.size() >= 12) return;
    template_.keywords.push_back (std::move (k));
    rebuildKeywords();
    recompute();
}

void ImportWizard::rebuildKeywords()
{
    keywordRows_.clear();
    for (int i = 0; i < static_cast<int> (template_.keywords.size()); ++i)
    {
        auto* row = keywordRows_.add (new KeywordRow (*this, i));
        keywordsHolder_.addAndMakeVisible (row);
    }
    resized();
}

void ImportWizard::cellClicked (int row, int, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int> (shown_.size())) return;
    exampleStem_ = stemOf (plan_.zones[static_cast<std::size_t> (shown_[static_cast<std::size_t> (row)])].path);
    strip_->setStem (exampleStem_);
}

void ImportWizard::rebuildRules()
{
    ruleRows_.clear();
    for (int i = 0; i < static_cast<int> (template_.fields.size()); ++i)
    {
        auto* row = ruleRows_.add (new RuleRow (*this, i));
        rulesHolder_.addAndMakeVisible (row);
    }
    resized();
}

void ImportWizard::recompute()
{
    using import::RangeStrategy;
    auto strat = [] (int id) { return id == 2 ? RangeStrategy::Midpoint : (id == 3 ? RangeStrategy::Fixed : RangeStrategy::Exact); };
    options_.keys = { strat (keyStrategy_.getSelectedId()), belowKeys_.getText().getIntValue(), aboveKeys_.getText().getIntValue() };
    options_.velocities = { strat (velStrategy_.getSelectedId()), belowVel_.getText().getIntValue(), aboveVel_.getText().getIntValue() };
    options_.layerCountOverride = layersEdit_.getText().getIntValue();
    options_.roundRobinMode = rrMode_.getSelectedId() == 2 ? RoundRobinMode::Random
                            : (rrMode_.getSelectedId() == 3 ? RoundRobinMode::RandomNoRepeat : RoundRobinMode::Sequential);

    plan_ = import::planImport (paths_, template_, options_);

    // message le plus grave par fichier
    statusOf_.clear();
    std::map<std::string, int> worst;
    for (const auto& d : plan_.diagnostics)
    {
        if (d.file.empty()) continue;
        const int sev = static_cast<int> (d.severity);
        auto it = worst.find (d.file);
        if (it == worst.end() || sev > it->second) { worst[d.file] = sev; statusOf_[d.file] = d.message; }
    }

    shown_.clear();
    includedCount_ = 0;
    for (std::size_t i = 0; i < plan_.zones.size(); ++i)
    {
        shown_.push_back (static_cast<int> (i));
        if (plan_.zones[i].included) ++includedCount_;
    }
    // Problèmes d'abord : l'utilisateur voit tout de suite ce qui ne va pas
    std::stable_sort (shown_.begin(), shown_.end(), [this] (int a, int b)
    {
        return plan_.zones[static_cast<std::size_t> (a)].included < plan_.zones[static_cast<std::size_t> (b)].included;
    });

    summaryLabel_.setText (u8 (tr_.format ("import.summary", { std::to_string (paths_.size()), std::to_string (includedCount_),
                                                              std::to_string (plan_.errors()), std::to_string (plan_.warnings()),
                                                              std::to_string (plan_.detectedLayerCount) })),
                           juce::dontSendNotification);
    updateMemory();
    table_.updateContent();
    table_.repaint();
}

// Mémoire réelle (en-têtes des fichiers : frames x canaux x 2 ou 4 octets), pour chacun des deux résultats possibles.
void ImportWizard::updateMemory()
{
    const bool wantAlternates = alternatesToggle_.getToggleState();
    const bool wantRelease = releaseToggle_.getToggleState();
    const auto fresh = import::includedZones (plan_);

    InstrumentDescription replaceDesc = existing_;
    replaceDesc.zones = fresh;
    replaceDesc.mainRoundRobinOnly = ! wantAlternates;
    replaceDesc.loadReleaseSamples = wantRelease;

    InstrumentDescription addDesc = existing_;
    addDesc.zones.insert (addDesc.zones.end(), fresh.begin(), fresh.end());
    addDesc.mainRoundRobinOnly = existing_.mainRoundRobinOnly || ! wantAlternates;
    addDesc.loadReleaseSamples = existing_.loadReleaseSamples && wantRelease;

    int unreadable = 0;
    const auto replaceBytes = memory::estimateInstrumentBytes (replaceDesc, fileInfo_, &unreadable);
    const auto addBytes = existing_.zones.empty() ? replaceBytes : memory::estimateInstrumentBytes (addDesc, fileInfo_);
    const auto currentBytes = memory::estimateInstrumentBytes (existing_, fileInfo_);

    // La mémoire déjà prise par l'instrument actuel n'est plus « disponible » : elle compte dans la marge de l'instrument.
    const auto limits = memory::limitsFor (availableBytes_ == 0 ? 0 : availableBytes_ + currentBytes);
    const auto replaceVerdict = memory::judge (replaceBytes, limits);
    const auto addVerdict = memory::judge (addBytes, limits);
    replaceBlocked_ = replaceVerdict == memory::Verdict::Refuse;
    addBlocked_ = addVerdict == memory::Verdict::Refuse;

    std::string text = existing_.zones.empty()
        ? tr_.format ("import.ram", { megabytes (replaceBytes) })
        : tr_.format ("import.ram_both", { megabytes (replaceBytes), megabytes (addBytes) });
    text += " (" + (limits.fromAvailable
                        ? tr_.format ("import.avail", { megabytes (availableBytes_), megabytes (limits.refuseBytes) })
                        : tr_.format ("import.avail_unknown", { megabytes (limits.refuseBytes) })) + ")";
    if (unreadable > 0) text += " - " + tr_.format ("import.unreadable", { std::to_string (unreadable) });
    if (replaceBlocked_ || addBlocked_) text += " - " + std::string (tr_.tr ("import.ram_hint"));
    ramLabel_.setText (u8 (text), juce::dontSendNotification);

    const bool blockedAll = replaceBlocked_ && addBlocked_;
    const bool warn = replaceVerdict == memory::Verdict::Warn || addVerdict == memory::Verdict::Warn;
    ramLabel_.setColour (juce::Label::textColourId, replaceBlocked_ || addBlocked_ ? colour (blockedAll ? Token::KnobHot : Token::KnobHot2)
                                                                                  : (warn ? colour (Token::Accent) : colour (Token::TextDim)));

    const bool any = includedCount_ > 0;
    replaceButton_.setEnabled (any && ! replaceBlocked_);
    addButton_.setEnabled (any && ! addBlocked_);
}

void ImportWizard::finish (bool replace)
{
    Result r;
    r.zones = import::includedZones (plan_);
    r.roundRobinMode = options_.roundRobinMode;
    r.replace = replace;
    r.mainRoundRobinOnly = ! alternatesToggle_.getToggleState();
    r.loadReleaseSamples = releaseToggle_.getToggleState();
    r.templ = template_;
    if (onImport_) onImport_ (r);
}

void ImportWizard::paintRowBackground (juce::Graphics& g, int row, int, int, bool)
{
    if (row >= 0 && row < static_cast<int> (shown_.size()))
    {
        const auto& pz = plan_.zones[static_cast<std::size_t> (shown_[static_cast<std::size_t> (row)])];
        if (! pz.included) g.fillAll (colour (Token::KnobHot).withAlpha (0.18f));
    }
}

void ImportWizard::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (shown_.size())) return;
    const auto& pz = plan_.zones[static_cast<std::size_t> (shown_[static_cast<std::size_t> (row)])];
    const auto& z = pz.zone;
    const std::string stem = stemOf (pz.path);
    juce::String text;
    switch (column)
    {
        case 1: text = u8 (stem); break;
        case 2: text = pz.included ? juce::String (z.rootNote) + "  " + u8 (import::midiToNoteName (z.rootNote, template_.notes)) : juce::String(); break;
        case 3: text = pz.included ? (z.keyLow == z.keyHigh ? juce::String (z.keyLow) : juce::String (z.keyLow) + "-" + juce::String (z.keyHigh)) : juce::String(); break;
        case 4: text = pz.included ? (z.velLow == z.velHigh ? juce::String (z.velLow) : juce::String (z.velLow) + "-" + juce::String (z.velHigh)) : juce::String(); break;
        case 5: text = z.roundRobin > 0 ? juce::String (z.roundRobin) : juce::String(); break;
        case 6: text = u8 (z.group) + (z.onRelease ? " (rel)" : ""); break;
        case 7:
        {
            const auto it = statusOf_.find (stem);
            text = it != statusOf_.end() ? u8 (it->second) : t ("import.ok");
            break;
        }
        default: break;
    }
    g.setColour (pz.included ? colour (Token::Text) : colour (Token::KnobHot2));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
}
