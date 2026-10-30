#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Theme.h"

// Polyphonie de l'instrument (fenêtre flottante) : mode (polyphonique / mono / legato), limite et vol de voix, même touche,
// durée du fondu de coupure, glissando du legato. Les réglages non pertinents pour le mode choisi sont grisés.
class PolyphonyPanel : public juce::Component
{
public:
    struct Settings
    {
        neptune::core::PolyphonyMode mode = neptune::core::PolyphonyMode::Poly;
        int maxVoices = neptune::core::InstrumentDescription::defaultMaxVoices;
        neptune::core::VoiceStealMode steal = neptune::core::VoiceStealMode::Oldest;
        neptune::core::SameKeyMode sameKey = neptune::core::SameKeyMode::Stack;
        int cutFadeMs = 3;
        int glideMs = 0;
        int bendRange = neptune::core::InstrumentDescription::defaultBendRange;   // plage du pitch bend, demi-tons
        bool chokeSameZone = false;
        bool operator== (const Settings& o) const noexcept
        {
            return mode == o.mode && maxVoices == o.maxVoices && steal == o.steal && sameKey == o.sameKey
                && cutFadeMs == o.cutFadeMs && glideMs == o.glideMs && bendRange == o.bendRange && chokeSameZone == o.chokeSameZone;
        }
    };

    PolyphonyPanel (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, const Settings&);

    std::function<void (const Settings&)> onChange;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void()> onUndo, onRedo;

    void setValues (const Settings&); // après annuler/rétablir ; ignoré pendant un geste
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    void changed();
    void refreshControls();

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    Settings s_;
    juce::Label modeLabel_, limitLabel_, stealLabel_, sameKeyLabel_, fadeLabel_, glideLabel_, bendLabel_, hint_;
    juce::ComboBox mode_, steal_, sameKey_;
    juce::TextButton limitPreset_[3]; // 64, 128, 256
    juce::ToggleButton chokeSame_;
    juce::Slider limit_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider fade_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider glide_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider bend_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };   // plage du pitch bend
    bool updating_ = false;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolyphonyPanel)
};
