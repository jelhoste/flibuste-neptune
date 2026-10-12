#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"
#include "neptune/core/ZoneImport.h"

// Assistant d'import (palier 1, jalon 3) : on dit comment lire les noms de fichiers à partir d'un exemple, on voit le
// résultat pour TOUS les fichiers avant de valider. Rien n'est deviné en silence : erreurs et doublons sont listés.
// Superposé à la fenêtre du plugin, comme la modale des potards.
class ImportWizard : public juce::Component, private juce::TableListBoxModel
{
public:
    struct Result
    {
        std::vector<neptune::core::ZoneDescription> zones;
        neptune::core::RoundRobinMode roundRobinMode = neptune::core::RoundRobinMode::Sequential;
        bool replace = true;
        neptune::core::import::Template templ;
    };

    ImportWizard (std::vector<std::string> paths, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                  neptune::core::import::Template initial, std::function<void (const Result&)> onImport, std::function<void()> onCancel);

    ~ImportWizard() override; // défini dans le .cpp : ExampleStrip et RuleRow y sont complets

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // le voile intercepte les clics

private:
    class ExampleStrip;
    class RuleRow;
    struct Panel : juce::Component { void paint (juce::Graphics&) override; ImportWizard* owner = nullptr; };

    // TableListBoxModel (aperçu)
    int getNumRows() override { return static_cast<int> (shown_.size()); }
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override;

    void recompute();
    void rebuildRules();
    void addRuleFromSelection (int from, int to); // caractères 0-based, inclus
    void finish (bool replace);
    juce::String t (const char* key) const;
    juce::Colour colour (neptune::core::theme::Token tk) const;

    std::vector<std::string> paths_;
    std::map<std::string, juce::int64> sizes_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::import::Template template_;
    neptune::core::import::PlanOptions options_;
    neptune::core::import::Plan plan_;
    std::vector<int> shown_;                       // indices dans plan_.zones affichés dans l'aperçu
    std::map<std::string, std::string> statusOf_;  // nom (sans extension) -> message
    double estimatedMegabytes_ = 0.0;
    bool blocked_ = false;
    std::function<void (const Result&)> onImport_;
    std::function<void()> onCancel_;

    Panel panel_;
    juce::Label titleLabel_, exampleLabel_, rulesLabel_, summaryLabel_, ramLabel_, delimiterLabel_, octaveLabel_;
    juce::TextEditor delimiterEdit_, belowKeys_, aboveKeys_, belowVel_, aboveVel_, layersEdit_;
    juce::ComboBox octaveBox_, keyStrategy_, velStrategy_, rrMode_;
    juce::Label keysLabel_, velLabel_, layersLabel_, rrLabel_;
    std::unique_ptr<ExampleStrip> strip_;
    juce::OwnedArray<RuleRow> ruleRows_;
    juce::Viewport rulesViewport_;
    juce::Component rulesHolder_;
    juce::TableListBox table_ { "preview", this };
    juce::TextButton replaceButton_, addButton_, cancelButton_;
    std::string exampleStem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportWizard)
};
