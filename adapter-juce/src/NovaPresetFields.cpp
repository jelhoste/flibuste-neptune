#include "NovaPresetFields.h"

#include <algorithm>

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return s;
}

struct Standard { const char* key; const char* labelKey; std::vector<std::string> aliases; };
const std::vector<Standard>& standards()
{
    static const std::vector<Standard> s = {
        { "song", "og.f_song", { "song", "morceau", "piece", "pi\xc3\xa8" "ce" } },
        { "workshop", "og.f_workshop", { "workshop", "atelier" } },
        { "student", "og.f_student", { "student", "\xc3\xa9" "l\xc3\xa8" "ve", "eleve" } },
        { "event", "og.f_event", { "event", "\xc3\xa9v\xc3\xa9nement", "evenement", "concert" } } };
    return s;
}
int standardOf (const std::string& key)
{
    const auto k = lower (key);
    const auto& s = standards();
    for (std::size_t i = 0; i < s.size(); ++i)
        for (const auto& a : s[i].aliases) if (a == k) return static_cast<int> (i);
    return -1;
}
} // namespace

namespace nova
{
PresetFields::PresetFields (const Skin& s, PresetHost& host, const i18n::Translator& tr) : skin_ (s), host_ (host), tr_ (tr)
{
    addPill_ = std::make_unique<Pill> (skin_, Pill::Look::Small);
    addPill_->setText (tx ("sy.add_field"));
    addPill_->onClick = [this] { addField(); };
    addAndMakeVisible (*addPill_);
}

PresetFields::~PresetFields() = default;

juce::String PresetFields::tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void PresetFields::update()
{
    for (const auto& r : rows_) if (r.editor->hasKeyboardFocus (true)) return;
    const auto id = host_.currentPresetId();
    const auto rev = host_.presetRevision();
    if (id == shownId_ && rev == shownRevision_) return;
    shownId_ = id;
    shownRevision_ = rev;
    pendingKeys_.clear();
    rebuild();
}

void PresetFields::rebuild()
{
    rows_.clear();
    const auto* p = host_.currentPreset();
    editable_ = p != nullptr && ! p->factory;
    auto make = [this] (std::string key, juce::String label, juce::String value, bool standard)
    {
        Row r;
        r.key = std::move (key);
        r.label = std::move (label);
        r.standard = standard;
        r.editor = std::make_unique<juce::TextEditor>();
        r.editor->setMultiLine (false);
        r.editor->setText (value, false);
        r.editor->setEnabled (editable_);
        r.editor->setColour (juce::TextEditor::backgroundColourId, skin_.colour (Token::StageBg));
        r.editor->setColour (juce::TextEditor::textColourId, skin_.colour (Token::Text));
        r.editor->setColour (juce::TextEditor::outlineColourId, skin_.colour (Token::Grid));
        r.editor->setColour (juce::TextEditor::focusedOutlineColourId, skin_.colour (Token::Accent));
        r.editor->setFont (juce::FontOptions (skin_.px (13.0f)));
        r.editor->onReturnKey = [this] { commit(); };
        r.editor->onFocusLost = [this] { commit(); };
        addAndMakeVisible (*r.editor);
        rows_.push_back (std::move (r));
    };
    std::vector<std::string> usedKeys;
    for (std::size_t i = 0; i < standards().size(); ++i)
    {
        std::string key = standards()[i].key;
        juce::String value;
        if (p != nullptr)
            for (const auto& cf : p->customFields)
                if (standardOf (cf.key) == static_cast<int> (i)) { key = cf.key; value = juce::String::fromUTF8 (cf.value.c_str()); break; }
        usedKeys.push_back (lower (key));
        make (key, tx (standards()[i].labelKey).toUpperCase(), value, true);
    }
    if (p != nullptr)
        for (const auto& cf : p->customFields)
        {
            if (standardOf (cf.key) >= 0 || std::find (usedKeys.begin(), usedKeys.end(), lower (cf.key)) != usedKeys.end()) continue;
            usedKeys.push_back (lower (cf.key));
            make (cf.key, juce::String::fromUTF8 (cf.key.c_str()).toUpperCase(), juce::String::fromUTF8 (cf.value.c_str()), false);
        }
    for (const auto& k : pendingKeys_)
        if (std::find (usedKeys.begin(), usedKeys.end(), lower (k)) == usedKeys.end())
        {
            usedKeys.push_back (lower (k));
            make (k, juce::String::fromUTF8 (k.c_str()).toUpperCase(), {}, false);
        }
    addPill_->setEnabled (editable_ && rows_.size() < 9);
    addPill_->setAlpha (editable_ && rows_.size() < 9 ? 1.0f : 0.4f);
    resized();
    repaint();
}

void PresetFields::commit()
{
    const auto* p = host_.currentPreset();
    if (p == nullptr || p->factory) return;
    auto edited = *p;
    edited.customFields.clear();
    for (const auto& r : rows_)
    {
        const auto v = r.editor->getText().trim();
        if (v.isNotEmpty()) edited.customFields.push_back ({ r.key, v.toStdString() });
    }
    if (edited.customFields == p->customFields) return;
    std::string error;
    if (host_.library().updateMetadata (edited, &error))
    {
        host_.touchPresets();
        shownRevision_ = host_.presetRevision();
    }
}

void PresetFields::addField()
{
    auto* w = new juce::AlertWindow (tx ("sy.add_field"), tx ("sy.field_name"), juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (juce::String::fromUTF8 (std::string (tr_.tr ("insert.close")).c_str()), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetFields> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result == 1 && safe != nullptr)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isNotEmpty()) { safe->pendingKeys_.push_back (name.toStdString()); safe->rebuild(); }
        }
    }), true);
}

void PresetFields::resized()
{
    const float rowH = 46.0f;
    float y = 0.0f;
    for (auto& r : rows_)
    {
        r.editor->setBounds (juce::Rectangle<float> (0.0f, skin_.px (y + 16.0f), static_cast<float> (getWidth()), skin_.px (26.0f)).toNearestInt());
        y += rowH;
    }
    addPill_->setBounds (juce::Rectangle<float> (0.0f, skin_.px (y + 4.0f), static_cast<float> (getWidth()), skin_.px (28.0f)).toNearestInt());
}

void PresetFields::paint (juce::Graphics& g)
{
    g.setColour (skin_.colour (Token::TextDim2));
    g.setFont (skin_.font (10.0f, true));
    float y = 0.0f;
    for (const auto& r : rows_)
    {
        g.drawText (r.label, juce::Rectangle<float> (0.0f, skin_.px (y), static_cast<float> (getWidth()), skin_.px (15.0f)), juce::Justification::centredLeft, true);
        y += 46.0f;
    }
    if (! editable_)
    {
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (11.0f));
        g.drawFittedText (tx (host_.currentPreset() == nullptr ? "sy.fields_none" : "sy.fields_locked"),
                          juce::Rectangle<float> (0.0f, skin_.px (y + 40.0f), static_cast<float> (getWidth()), skin_.px (80.0f)).toNearestInt(), juce::Justification::topLeft, 5);
    }
}
} // namespace nova
