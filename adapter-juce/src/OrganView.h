#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "SynthKeyboard.h"
#include "neptune/core/I18n.h"
#include "neptune/core/OrganEngine.h"
#include "neptune/core/Theme.h"

// Console de l'orgue (3 claviers de tirettes, percussion, vibrato / chorus, Leslie, overdrive, réverb, canaux / transposition /
// split) : la même vue pour le type de piste Orgue de Neptune et pour le plugin Orgamon. Les paramètres sont ceux de la piste
// `track` (mixer::organParam) ; `extraKnobs` donne les potards d'instrument à ajouter (volume, gain...).
class OrganView : public juce::Component
{
public:
    using KnobIds = std::vector<std::function<neptune::core::ParamId (int track)>>;
    using OpenModal = std::function<void (neptune::core::ParamId, neptune::core::uistate::Terminal)>;

    OrganView (ParamHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
               neptune::core::theme::KnobStyle, KnobIds extraKnobs, OpenModal);
    ~OrganView() override;

    std::function<void (const neptune::core::organ::OrganDescription&)> onChange; // l'utilisateur a modifié les claviers
    std::function<void()> onGestureBegin, onGestureEnd;
    // Clavier à l'écran : joue le clavier supérieur (donc aussi les zones de split). `channel` : canal MIDI 0..15 de ce clavier.
    std::function<void (int key, int channel)> onNoteOn, onNoteOff;
    void releaseKeys() { keyboard_.releaseAll(); }
    void setHeldKeys (std::uint64_t lo, std::uint64_t hi) { keyboard_.setHeldKeys (lo, hi); }

    void setTrack (int track);                                                    // rebranche les commandes sur une autre piste
    void setDescription (const neptune::core::organ::OrganDescription&);          // sans notification
    void refreshFromParameters();                                                 // listes et tirettes suivent l'automation

    // Place la vue dans `area` en la réduisant si besoin (jamais agrandie).
    void placeScaled (juce::Rectangle<int> area);
    static constexpr int designWidth = 960, designHeight = 580;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Drawbar;
    class Switch;
    struct ComboLink;
    struct Section { juce::String title; juce::Rectangle<int> area; };

    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const;
    void bindTrack();

    ParamHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    KnobIds extraIds_;
    OpenModal openModal_;
    int track_ = 0;
    bool updating_ = false;
    neptune::core::organ::OrganDescription description_;

    juce::OwnedArray<Drawbar> drawbars_;
    juce::OwnedArray<Switch> switches_;
    juce::OwnedArray<NeptuneKnob> knobs_;      // 5 potards de l'orgue puis les potards d'instrument
    juce::OwnedArray<ComboLink> combos_;
    juce::ComboBox channelBox_[3], transposeBox_[3];
    juce::Slider splitLower_, splitUpper_;
    SynthKeyboard keyboard_;
    juce::TextButton octDown_, octUp_;
    juce::ToggleButton hold_;
    std::vector<Section> sections_;
    juce::Rectangle<int> keyboardArea_;
    juce::Rectangle<int> lastArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganView)
};
