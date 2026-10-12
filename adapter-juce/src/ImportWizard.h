#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/MemoryBudget.h"
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
        // Import léger : ce que l'utilisateur a choisi de charger en mémoire (avec les zones, c'est ce qui est estimé).
        bool mainRoundRobinOnly = false;
        bool loadReleaseSamples = true;
        neptune::core::import::Template templ;
    };

    // `existing` : l'instrument actuel (pour « Ajouter » et pour la mémoire déjà occupée). `fileInfo` : cache partagé des
    // en-têtes de fichiers. `availableBytes` : RAM disponible (0 = inconnue).
    ImportWizard (std::vector<std::string> paths, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                  neptune::core::import::Template initial, neptune::core::InstrumentDescription existing,
                  neptune::core::memory::FileInfoCache& fileInfo, std::uint64_t availableBytes,
                  std::function<void (const Result&)> onImport, std::function<void()> onCancel);

    ~ImportWizard() override; // défini dans le .cpp : ExampleStrip et RuleRow y sont complets

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // le voile intercepte les clics

private:
    class ExampleStrip;
    class RuleRow;
    class KeywordRow;
    struct Panel : juce::Component { void paint (juce::Graphics&) override; ImportWizard* owner = nullptr; };

    // TableListBoxModel (aperçu)
    int getNumRows() override { return static_cast<int> (shown_.size()); }
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override; // montre ce nom dans l'exemple

    void recompute();
    void updateMemory(); // estimation de la mémoire et boutons, sans refaire le plan
    void rebuildRules();
    void rebuildKeywords();
    void addKeyword (neptune::core::import::KeywordRule);
    void addRuleFromSelection (int from, int to); // caractères 0-based, inclus
    void finish (bool replace);
    juce::String t (const char* key) const;
    juce::Colour colour (neptune::core::theme::Token tk) const;

    std::vector<std::string> paths_;
    neptune::core::InstrumentDescription existing_;
    neptune::core::memory::FileInfoCache& fileInfo_;
    std::uint64_t availableBytes_ = 0;
    int includedCount_ = 0;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::import::Template template_;
    neptune::core::import::PlanOptions options_;
    neptune::core::import::Plan plan_;
    std::vector<int> shown_;                       // indices dans plan_.zones affichés dans l'aperçu
    std::map<std::string, std::string> statusOf_;  // nom (sans extension) -> message
    bool replaceBlocked_ = false, addBlocked_ = false;
    std::function<void (const Result&)> onImport_;
    std::function<void()> onCancel_;

    Panel panel_;
    juce::Label keywordsLabel_;
    juce::TextButton addKeywordButton_;
    juce::OwnedArray<KeywordRow> keywordRows_;
    juce::Viewport keywordsViewport_;
    juce::Component keywordsHolder_;
    juce::Label titleLabel_, exampleLabel_, rulesLabel_, summaryLabel_, ramLabel_, delimiterLabel_, octaveLabel_;
    juce::TextEditor delimiterEdit_, belowKeys_, aboveKeys_, belowVel_, aboveVel_, layersEdit_;
    juce::ComboBox octaveBox_, keyStrategy_, velStrategy_, rrMode_;
    juce::Label keysLabel_, velLabel_, layersLabel_, rrLabel_;
    std::unique_ptr<ExampleStrip> strip_;
    juce::OwnedArray<RuleRow> ruleRows_;
    juce::Viewport rulesViewport_;
    juce::Component rulesHolder_;
    juce::TableListBox table_ { "preview", this };
    juce::ToggleButton alternatesToggle_, releaseToggle_;
    juce::TextButton replaceButton_, addButton_, cancelButton_;
    std::string exampleStem_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportWizard)
};
