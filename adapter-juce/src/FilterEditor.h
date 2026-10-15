#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NeptuneKnob.h"
#include <functional>
#include <memory>
#include "EnvelopeEditor.h"
#include "neptune/core/Filter.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Éditeur des filtres par voie (superposé à la fenêtre, comme l'assistant d'import) : deux filtres, leur routage série ou
// parallèle, et pour chacun modèle, mode, pente, coupure, résonance, suivi du clavier, vélocité, profondeur d'enveloppe,
// drive et enveloppe AHDSHR.
//
// Cible : « Instrument » (le réglage par défaut, hérité par les zones) ou « Zones sélectionnées » (une surcharge par zone).
// Sur l'instrument, la coupure et la résonance sont les PARAMÈTRES automatisables (potards du bas) : les curseurs d'ici les
// pilotent. Pour des zones qui n'ont pas de surcharge, les réglages affichés sont ceux de l'instrument, en lecture seule ;
// « Personnaliser » en fait une copie propre à la zone.
class FilterEditor : public juce::Component
{
public:
    struct DefaultParams
    {
        double cutoffHz[2] = { 20000.0, 20000.0 };
        double resonance[2] = { 0.0, 0.0 }; // 0..1
    };

    FilterEditor (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                  neptune::core::theme::KnobStyle = neptune::core::theme::KnobStyle::Led);
    ~FilterEditor() override; // défini dans le .cpp : Column y est complet

    // État affiché (rappelé quand l'instrument change : annulation, édition ailleurs) ; ignoré pendant un geste de l'utilisateur.
    void setState (const neptune::core::FilterBlock& defaultBlock, const DefaultParams& params, int selectedZones,
                   bool zonesOverride, const neptune::core::FilterBlock& zoneBlock);

    std::function<void (const neptune::core::FilterBlock&)> onDefaultChanged;      // réglages d'instrument (hors coupure/résonance)
    std::function<void (int index, double plainValue, int phase)> onParam;         // index 0..3 (c1, r1, c2, r2) ; phase 0 début, 1 valeur, 2 fin
    std::function<void (bool overrideOn, const neptune::core::FilterBlock&)> onZonesChanged;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void()> onClose, onUndo, onRedo;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // le voile intercepte les clics
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Column;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    const neptune::core::FilterBlock& current() const { return target_ == 0 ? defaultBlock_ : zoneBlock_; }
    bool editable() const { return target_ == 0 || zoneOverride_; }
    void refresh();
    void emit (const neptune::core::FilterBlock&);

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::unique_ptr<NeptuneSliderLook> look_; // avant columns_ : détruit après les curseurs
    neptune::core::FilterBlock defaultBlock_, zoneBlock_;
    DefaultParams params_;
    int selectedZones_ = 0;
    bool zoneOverride_ = false;
    int target_ = 0; // 0 instrument, 1 zones sélectionnées
    bool updating_ = false;
    bool dragging_ = false;

    juce::Label titleLabel_, targetLabel_, routingLabel_, balanceLabel_, noteLabel_;
    juce::ComboBox targetBox_, routingBox_;
    juce::Slider balance_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton overrideToggle_;
    juce::TextButton closeButton_;
    std::unique_ptr<Column> columns_[2];
    juce::Rectangle<int> panelBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterEditor)
};
