#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include "EnvelopeEditor.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Synth.h"
#include "neptune/core/Theme.h"

// Clavier à l'écran (souris et tactile, plusieurs doigts) : joue la piste sélectionnée. « Maintien » (ou Maj+clic) : un clic
// sur une touche la laisse sonner, un second clic l'arrête. Sert à tester sans clavier MIDI, et à tenir des notes pendant qu'on
// règle le mixeur.
class SynthKeyboard : public juce::Component
{
public:
    explicit SynthKeyboard (const neptune::core::theme::Palette&);

    std::function<void (int key, float velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;

    void setHold (bool on) { hold_ = on; }
    void shiftOctave (int octaves);
    int firstKey() const noexcept { return firstKey_; }
    void releaseAll(); // relâche toutes les touches tenues (bouton « tout arrêter », changement de piste)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int numKeys = 36; // 3 octaves à partir d'un do

private:
    int keyAt (juce::Point<int>) const;
    juce::Rectangle<float> keyRect (int key) const;
    bool isBlack (int key) const noexcept;
    void press (int source, int key, bool latch);
    void release (int source);
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    int firstKey_ = 48;
    bool hold_ = false;
    std::map<int, int> sourceKey_;   // doigt / souris -> touche pressée
    std::set<int> latched_;          // touches maintenues
    std::set<int> down_;             // touches enfoncées (affichage)
};

// Vue du synthé soustractif : forme d'onde, octave, polyphonie, enveloppe de volume, filtre et son enveloppe, clavier.
class SynthView : public juce::Component
{
public:
    SynthView (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    ~SynthView() override;

    std::function<void (const neptune::core::SynthDescription&)> onChange; // à chaque modification par l'utilisateur
    std::function<void()> onGestureBegin, onGestureEnd;                    // entourent un glissé : une seule entrée d'annulation
    std::function<void (int key, float velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;

    void setDescription (const neptune::core::SynthDescription&); // sans notification ; ignoré pendant un geste
    void releaseKeys() { keyboard_.releaseAll(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    void discrete();        // lit les commandes à choix, notifie en un geste
    void continuous();      // lit les curseurs (le geste est déjà ouvert par le glissé)
    void refreshControls();
    void emit();

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::SynthDescription d_;
    bool updating_ = false, dragging_ = false;

    juce::Label waveLabel_, octaveLabel_, modeLabel_, voicesLabel_, glideLabel_, stealLabel_;
    juce::TextButton waveButtons_[neptune::core::numWaveforms];
    juce::Slider octave_ { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox mode_, steal_;
    juce::Slider voices_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider glide_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label ampTitle_, filterTitle_, filterEnvTitle_, hint_;
    EnvelopeEditor ampEnv_, filterEnv_;
    juce::ToggleButton filterOn_;
    juce::ComboBox model_, filterMode_, slope_;
    juce::Label keyLabel_, velLabel_, depthLabel_, driveLabel_;
    juce::Slider keyTrack_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider velocity_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider depth_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider drive_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    SynthKeyboard keyboard_;
    juce::TextButton octDown_, octUp_;
    juce::ToggleButton hold_;
    juce::Label keysHint_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthView)
};
