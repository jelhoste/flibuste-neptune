#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <memory>
#include <functional>
#include <set>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/ParamGroups.h"
#include "neptune/core/Theme.h"

// Filtres des listes de choix de paramètres : un menu de portée (toutes les pistes, une piste, Master, pistes parallèles, bus, global) et une
// rangée de pastilles de familles (bande, envois, inserts, synthé…). Se combine avec la recherche par mots de la liste.
class ParamFilterBar : public juce::Component
{
public:
    using Group = neptune::core::paramgroups::Group;
    using Scope = neptune::core::paramgroups::Scope;
    using Family = neptune::core::paramgroups::Family;

    // `groups` : le classement de chaque entrée de la liste (sert à ne proposer que les pistes et familles qui existent).
    ParamFilterBar (const std::vector<Group>& groups, const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr,
                    int defaultTrack, std::function<void()> onChange)
        : palette_ (palette), tr_ (tr), onChange_ (std::move (onChange))
    {
        std::set<int> tracks;
        std::array<bool, neptune::core::paramgroups::numFamilies> present {};
        bool hasParallel = false, hasBus = false, hasMaster = false, hasGlobal = false;
        for (const auto& g : groups)
        {
            if (g.family != Family::Other) present[static_cast<std::size_t> (g.family)] = true;
            switch (g.scope)
            {
                case Scope::Track: tracks.insert (g.index); break;
                case Scope::Parallel: hasParallel = true; break;
                case Scope::Bus: hasBus = true; break;
                case Scope::Master: hasMaster = true; break;
                case Scope::Global: hasGlobal = true; break;
            }
        }
        int id = 1;
        scope_.addItem (text ("pfilter.all_scopes"), id++);
        choices_.push_back ({ false, Scope::Global, 0 });
        int defaultId = 1;
        for (const int t : tracks)
        {
            if (t == defaultTrack) defaultId = id;
            scope_.addItem (text ("pfilter.track") + " " + juce::String (t + 1), id++);
            choices_.push_back ({ true, Scope::Track, t });
        }
        auto addKind = [&] (bool present, Scope s, const char* key)
        {
            if (! present) return;
            scope_.addItem (text (key), id++);
            choices_.push_back ({ true, s, -1 });
        };
        addKind (hasMaster, Scope::Master, "pfilter.master");
        addKind (hasParallel, Scope::Parallel, "pfilter.parallels");
        addKind (hasBus, Scope::Bus, "pfilter.buses");
        addKind (hasGlobal, Scope::Global, "pfilter.global");
        scope_.setSelectedId (defaultId, juce::dontSendNotification);
        scope_.onChange = [this] { if (onChange_) onChange_(); };
        addAndMakeVisible (scope_);

        addChip (text ("pfilter.all_families"), -1);
        for (int f = 0; f < neptune::core::paramgroups::numFamilies - 1; ++f)
            if (present[static_cast<std::size_t> (f)])
                addChip (text (neptune::core::paramgroups::familyKey (static_cast<Family> (f))), f);
        chips_.front()->setToggleState (true, juce::dontSendNotification);
    }

    // Vrai si l'entrée passe les deux filtres.
    bool accepts (const Group& g) const
    {
        const int idx = scope_.getSelectedId() - 1;
        if (idx > 0 && idx < static_cast<int> (choices_.size()))
        {
            const auto& c = choices_[static_cast<std::size_t> (idx)];
            if (g.scope != c.scope) return false;
            if (c.index >= 0 && g.index != c.index) return false;
        }
        return family_ < 0 || static_cast<int> (g.family) == family_;
    }

    int preferredHeight (int width) const
    {
        return 30 + 6 + rowsFor (width) * 26;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        scope_.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        int x = 0, y = r.getY();
        for (auto& c : chips_)
        {
            const int w = chipWidth (*c);
            if (x > 0 && x + w > r.getWidth()) { x = 0; y += 26; }
            c->setBounds (x, y, w, 24);
            x += w + 4;
        }
    }

private:
    struct Choice { bool filtered; Scope scope; int index; };

    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    static int chipWidth (const juce::TextButton& b) { return juce::jmax (48, juce::roundToInt (std::ceil (juce::Font (juce::FontOptions (14.0f)).getStringWidthFloat (b.getButtonText()))) + 20); }

    int rowsFor (int width) const
    {
        int rows = 1, x = 0;
        for (const auto& c : chips_)
        {
            const int w = chipWidth (*c);
            if (x > 0 && x + w > width) { ++rows; x = 0; }
            x += w + 4;
        }
        return rows;
    }

    void addChip (const juce::String& label, int family)
    {
        auto b = std::make_unique<juce::TextButton> (label);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setColour (juce::TextButton::buttonOnColourId, colour (neptune::core::theme::Token::Accent));
        b->onClick = [this, family, raw = b.get()]
        {
            if (! raw->getToggleState()) return;
            family_ = family;
            if (onChange_) onChange_();
        };
        addAndMakeVisible (*b);
        chips_.push_back (std::move (b));
    }

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::function<void()> onChange_;
    juce::ComboBox scope_;
    std::vector<Choice> choices_;
    std::vector<std::unique_ptr<juce::TextButton>> chips_;
    int family_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamFilterBar)
};
