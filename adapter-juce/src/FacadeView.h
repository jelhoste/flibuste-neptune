#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include "PluginProcessor.h"
#include "NeptuneKnob.h"
#include "neptune/core/Facade.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Vue Façade (spec §15) : l'interface personnalisée de la piste sélectionnée. Une image de fond et des widgets (potard, bouton,
// étiquette) liés à des paramètres existants. Deux modes : Jeu (les widgets agissent sur leur paramètre) et Édition (on ajoute,
// déplace, redimensionne, lie, supprime). Chaque modification de la mise en page est un seul geste annulable (réglages de bande
// de la piste) ; le glissement travaille sur une copie locale et ne s'enregistre qu'au relâchement.
class FacadeView : public juce::Component, private juce::Timer
{
public:
    FacadeView (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                neptune::core::theme::KnobStyle, NeptuneKnob::OpenModal);
    ~FacadeView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    // « Ajouter à la Façade » (fenêtre d'un potard) : pose un widget déjà lié au paramètre, sur la Façade de la piste sélectionnée.
    void addLinked (neptune::core::facade::WidgetType, neptune::core::ParamId);

private:
    class Canvas;
    class Picker;
    void timerCallback() override;
    void refresh();                              // relit la Façade de la piste sélectionnée si elle a changé
    void commit (neptune::core::facade::Facade, const char* undoName);
    void addWidget (neptune::core::facade::WidgetType);
    void chooseImage();
    void showFitMenu();
    void openPicker (bool capture = false);
    void showAlignMenu();
    int single() const noexcept { return selection_.size() == 1 ? *selection_.begin() : 0; }   // le widget sélectionné s'il n'y en a qu'un
    void editText();
    void duplicateSelected();
    void deleteSelected();
    void updateButtons();
    void pruneSelection();
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token) const;

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    NeptuneKnob::OpenModal openModal_;

    int track_ = -1;
    std::uint32_t revision_ = 0xFFFFFFFFu;
    neptune::core::facade::Facade facade_;       // copie de travail (celle de la piste, ou celle d'un glissement en cours)
    std::set<int> selection_;                    // widgets sélectionnés (mode Édition)
    bool editMode_ = false;

    juce::TextButton editButton_, knobButton_, buttonButton_, labelButton_, menuButton_, actionButton_, curveButton_, imageButton_, fitButton_, clearImageButton_;
    juce::TextButton linkButton_, textButton_, duplicateButton_, deleteButton_, captureButton_, alignButton_;
    std::unique_ptr<Canvas> canvas_;
    std::unique_ptr<Picker> picker_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FacadeView)
};
