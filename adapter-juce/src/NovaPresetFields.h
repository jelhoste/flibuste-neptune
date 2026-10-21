#pragma once
// Fiche du preset courant (page MATRICE) : un champ texte par information (morceau, atelier, élève, événement) et autant de champs libres que voulu, enregistrés
// avec le preset (champs personnalisés de la bibliothèque). Seuls les presets utilisateur sont modifiables ; un preset d'usine doit d'abord être enregistré sous.
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <vector>
#include "NovaKit.h"
#include "PresetHost.h"
#include "neptune/core/I18n.h"

namespace nova
{
class PresetFields : public juce::Component
{
public:
    PresetFields (const Skin&, PresetHost&, const neptune::core::i18n::Translator&);
    ~PresetFields() override;

    void update();                         // à chaque rafraîchissement : relit la fiche quand le preset courant change (sauf pendant une saisie)
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        std::string key;                   // clé enregistrée
        juce::String label;
        std::unique_ptr<juce::TextEditor> editor;
        bool standard = false;
    };
    void rebuild();
    void commit();
    void addField();
    juce::String tx (const char* key) const;

    const Skin& skin_;
    PresetHost& host_;
    const neptune::core::i18n::Translator& tr_;
    std::vector<Row> rows_;
    std::unique_ptr<Pill> addPill_;
    std::string shownId_;
    std::uint32_t shownRevision_ = 0xFFFFFFFFu;
    bool editable_ = false;
    std::vector<std::string> pendingKeys_;   // champs ajoutés mais encore vides
};
} // namespace nova
