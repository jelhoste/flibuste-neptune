#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

class AnalogSynthProcessor;

// Gestionnaire de presets (modèle de données §1 à §11), superposé à la fenêtre du plugin comme les modales de potard :
//  - « Presets » : recherche à facettes (nom, type, caractéristiques, note, champs personnalisés), détails et métadonnées
//    modifiables, enregistrement, corbeille, affectation à une banque ;
//  - « Banques » : banques 0..127 (programmes 0..127), nom, passage à la banque suivante, export / import `.flibank`,
//    import d'un `.flipreset` ;
//  - « Grille » : grille concert (tuiles verrouillables, glisser-déposer par la poignée, vérification d'intégrité) ;
//  - « Corbeille » : restauration, vidage ;
//  - « Réglages » : capturé / libre par paramètre, rétention de la corbeille.
class PresetManager : public juce::Component
{
public:
    PresetManager (AnalogSynthProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    ~PresetManager() override;

    std::function<void()> onClose;
    std::function<void()> onChanged; // un preset a été chargé ou la bibliothèque a changé

    void refresh();                  // relit la bibliothèque dans tous les onglets
    void showSaveAs();               // ouvre l'onglet Presets, champ du nom actif (« Enregistrer sous »)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override; // un clic hors du panneau ferme
    bool keyPressed (const juce::KeyPress&) override;
    void parentSizeChanged() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
