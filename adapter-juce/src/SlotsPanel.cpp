#include "SlotsPanel.h"
#include "neptune/core/Mixer.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kNone = 1, kGroupBase = 10, kSelectedZone = 1000, kZoneBase = 2000;
constexpr int kRowH = 28;
const char* const kParamKeys[kSlotParams] = { "slots.param_none", "slots.param_attack", "slots.param_decay", "slots.param_sustain",
                                              "slots.param_release", "slots.param_level", "slots.param_tune" };
} // namespace

struct SlotsPanel::Row
{
    juce::Label number;
    juce::ComboBox target, param;
    juce::Slider value { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    ParamId id = 0;
    bool dragging = false;
    SlotParam shownParam = SlotParam::None;
};

juce::Colour SlotsPanel::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String SlotsPanel::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

juce::String SlotsPanel::zoneLabel (const InstrumentDescription& d, int zone) const
{
    if (zone < 0 || zone >= static_cast<int> (d.zones.size())) return text ("slots.zone") + " " + juce::String (zone + 1);
    const auto name = juce::File (juce::String::fromUTF8 (d.zones[static_cast<std::size_t> (zone)].samplePath.c_str())).getFileName();
    return text ("slots.zone") + " " + juce::String (zone + 1) + " - " + name;
}

SlotsPanel::SlotsPanel (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, int track)
    : host_ (host), palette_ (palette), tr_ (tr), track_ (track)
{
    for (int s = 0; s < kZoneSlots; ++s)
    {
        auto* row = rows_.add (new Row);
        row->id = mixer::zoneSlotParam (track_, s);
        row->number.setText (juce::String (s + 1), juce::dontSendNotification);
        row->number.setColour (juce::Label::textColourId, colour (Token::Accent));
        row->number.setJustificationType (juce::Justification::centredRight);
        for (int p = 0; p < kSlotParams; ++p) row->param.addItem (text (kParamKeys[p]), p + 1);
        row->target.onChange = [this, s, row]
        {
            if (updating_) return;
            assign (s, row->target.getSelectedId(), row->param.getSelectedId());
        };
        row->param.onChange = [this, s, row]
        {
            if (updating_) return;
            assign (s, row->target.getSelectedId(), row->param.getSelectedId());
        };
        row->value.setRange (-100.0, 100.0, 1.0);
        row->value.setDoubleClickReturnValue (true, 0.0);
        row->value.setColour (juce::Slider::trackColourId, colour (Token::Accent));
        row->value.setColour (juce::Slider::backgroundColourId, colour (Token::Grid));
        row->value.setColour (juce::Slider::textBoxTextColourId, colour (Token::Text));
        row->value.setColour (juce::Slider::textBoxOutlineColourId, colour (Token::CtlBd));
        row->value.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 22);
        // texte : ce que la valeur fait à la zone visée
        row->value.textFromValueFunction = [this, row] (double v)
        {
            switch (row->shownParam)
            {
                case SlotParam::Attack: case SlotParam::Decay: case SlotParam::Release: return "x" + juce::String (std::pow (10.0, v / 100.0), 2);
                case SlotParam::Sustain: return "x" + juce::String (juce::jmax (0.0, 1.0 + v / 100.0), 2);
                case SlotParam::Level: return juce::String (v * 0.24, 1) + " dB";
                case SlotParam::Tune: return juce::String (v, 0) + " ct";
                default: return juce::String (v, 0) + " %";
            }
        };
        row->value.onDragStart = [this, row] { row->dragging = true; host_.uiBeginEdit (row->id); };
        row->value.onValueChange = [this, row]
        {
            if (updating_) return;
            host_.uiSetNormalised (row->id, (row->value.getValue() + 100.0) / 200.0);
        };
        row->value.onDragEnd = [this, row] { row->dragging = false; host_.uiEndEdit (row->id); };
        addAndMakeVisible (row->number);
        addAndMakeVisible (row->target);
        addAndMakeVisible (row->param);
        addAndMakeVisible (row->value);
    }
    hint_.setText (text ("slots.hint"), juce::dontSendNotification);
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setFont (juce::FontOptions (11.0f));
    hint_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint_);
    setSize (panelWidth, panelHeight);
    refresh();
    startTimerHz (6);
}

SlotsPanel::~SlotsPanel() { stopTimer(); }

void SlotsPanel::assign (int slot, int targetId, int paramId)
{
    if (! getDescription || ! onEdit) return;
    auto d = getDescription();
    auto& a = d.slots[static_cast<std::size_t> (slot)];
    SlotAssignment n;
    n.param = static_cast<SlotParam> (juce::jlimit (0, kSlotParams - 1, paramId - 1));
    if (targetId >= kGroupBase && targetId < kSelectedZone)
    {
        // les Groupes sont listés dans l'ordre alphabétique de leurs noms
        std::vector<std::string> names;
        for (const auto& z : d.zones)
            if (! z.group.empty() && std::find (names.begin(), names.end(), z.group) == names.end()) names.push_back (z.group);
        std::sort (names.begin(), names.end());
        const auto g = static_cast<std::size_t> (targetId - kGroupBase);
        if (g < names.size()) { n.isGroup = true; n.group = names[g]; }
    }
    else if (targetId == kSelectedZone)
    {
        const auto sel = getSelectedZones ? getSelectedZones() : std::vector<int> {};
        if (! sel.empty()) n.zone = sel.front();
    }
    else if (targetId >= kZoneBase) n.zone = targetId - kZoneBase;
    if (n.isGroup == false && n.zone < 0 && n.group.empty())
    {
        n = SlotAssignment {};   // « aucun », ou « zone sélectionnée » sans sélection : l'emplacement est libéré
    }
    else if (n.param == SlotParam::None)
        n.param = SlotParam::Release; // une cible sans paramètre : le plus courant
    if (n == a) { refresh(); return; }
    a = n;
    onEdit (std::move (d));
    refresh();
}

void SlotsPanel::refresh()
{
    if (! getDescription) return;
    const auto d = getDescription();
    std::vector<std::string> names;
    for (const auto& z : d.zones)
        if (! z.group.empty() && std::find (names.begin(), names.end(), z.group) == names.end()) names.push_back (z.group);
    std::sort (names.begin(), names.end());
    const auto selected = getSelectedZones ? getSelectedZones() : std::vector<int> {};

    updating_ = true;
    for (int s = 0; s < kZoneSlots; ++s)
    {
        auto* row = rows_[s];
        const auto& a = d.slots[static_cast<std::size_t> (s)];
        row->target.clear (juce::dontSendNotification);
        row->target.addItem (text ("slots.none"), kNone);
        for (std::size_t g = 0; g < names.size(); ++g)
            row->target.addItem (text ("slots.group") + " " + juce::String::fromUTF8 (names[g].c_str()), kGroupBase + static_cast<int> (g));
        if (! selected.empty())
            row->target.addItem (text ("slots.selected") + ": " + zoneLabel (d, selected.front()), kSelectedZone);
        int shown = kNone;
        if (a.assigned() && a.isGroup)
        {
            const auto it = std::find (names.begin(), names.end(), a.group);
            if (it != names.end()) shown = kGroupBase + static_cast<int> (it - names.begin());
        }
        else if (a.assigned())
        {
            row->target.addItem (zoneLabel (d, a.zone), kZoneBase + a.zone);
            shown = kZoneBase + a.zone;
        }
        row->target.setSelectedId (shown, juce::dontSendNotification);
        row->param.setSelectedId (static_cast<int> (a.assigned() ? a.param : SlotParam::None) + 1, juce::dontSendNotification);
        row->shownParam = a.assigned() ? a.param : SlotParam::None;
        row->value.updateText();
    }
    updating_ = false;
}

void SlotsPanel::timerCallback()
{
    // valeurs des emplacements (automation) et cibles (sélection de zones, annuler / rétablir)
    updating_ = true;
    for (auto* row : rows_)
        if (! row->dragging) row->value.setValue (host_.parameters().get (row->id), juce::dontSendNotification);
    updating_ = false;
    if (! getDescription) return;
    const auto d = getDescription();
    std::string sig;
    for (const auto& a : d.slots) sig += std::to_string (static_cast<int> (a.param)) + (a.isGroup ? "g" + a.group : "z" + std::to_string (a.zone)) + ";";
    sig += std::to_string (d.zones.size());
    if (getSelectedZones) { const auto sel = getSelectedZones(); sig += sel.empty() ? "-" : std::to_string (sel.front()); }
    if (sig != signature_) { signature_ = sig; refresh(); }
}

void SlotsPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (22);                       // titre
    auto head = r.removeFromTop (18); (void) head;
    r.removeFromTop (4);
    for (auto* row : rows_)
    {
        auto line = r.removeFromTop (kRowH);
        row->number.setBounds (line.removeFromLeft (26));
        line.removeFromLeft (6);
        row->target.setBounds (line.removeFromLeft (290).reduced (0, 2));
        line.removeFromLeft (6);
        row->param.setBounds (line.removeFromLeft (130).reduced (0, 2));
        line.removeFromLeft (8);
        row->value.setBounds (line.reduced (0, 2));
    }
    r.removeFromTop (6);
    hint_.setBounds (r);
}

void SlotsPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgCard));
    g.setColour (colour (Token::Accent));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (text ("slots.title"), 12, 8, getWidth() - 24, 22, juce::Justification::centredLeft);
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (text ("slots.target"), 44, 32, 290, 14, juce::Justification::centredLeft);
    g.drawText (text ("slots.parameter"), 340, 32, 130, 14, juce::Justification::centredLeft);
    g.drawText (text ("slots.value"), 478, 32, 200, 14, juce::Justification::centredLeft);
}
