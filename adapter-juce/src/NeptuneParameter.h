#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "neptune/core/Parameter.h"

// Vue d'un paramètre du cœur pour l'hôte. La source de vérité reste neptune::core::ParameterSet :
// cette classe ne fait que traduire (valeur normalisée, texte, métadonnées).
class NeptuneParameter : public juce::HostedAudioProcessorParameter
{
public:
    NeptuneParameter (neptune::core::ParameterSet& set, std::size_t index)
        : set_ (set), index_ (index), spec_ (set.spec (index)) {}

    // Identifiant stable pour l'automation : l'id du cœur, en texte (clap-juce-extensions en tire le clap_id par hachage).
    juce::String getParameterID() const override { return juce::String (static_cast<juce::int64> (spec_.id)); }

    float getValue() const override { return static_cast<float> (set_.getNormalizedAt (index_)); }
    void setValue (float newValue) override { set_.setNormalizedAt (index_, static_cast<double> (newValue)); }
    float getDefaultValue() const override { return static_cast<float> (spec_.toNormalized (spec_.defaultValue)); }

    juce::String getName (int maximumStringLength) const override
    {
        const auto s = juce::String::fromUTF8 (spec_.name.c_str());
        return maximumStringLength > 0 ? s.substring (0, maximumStringLength) : s;
    }
    juce::String getLabel() const override { return {}; } // l'unité est incluse dans le texte

    int getNumSteps() const override
    {
        using neptune::core::Kind;
        if (spec_.kind == Kind::Continuous) return AudioProcessorParameter::getNumSteps();
        return static_cast<int> (spec_.max - spec_.min) + 1;
    }
    bool isDiscrete() const override { return spec_.kind != neptune::core::Kind::Continuous; }
    bool isBoolean() const override { return spec_.kind == neptune::core::Kind::Toggle; }
    bool isAutomatable() const override { return spec_.automatable; }

    juce::String getText (float normalised, int maximumStringLength) const override
    {
        const auto s = juce::String::fromUTF8 (set_.toText (spec_, spec_.fromNormalized (normalised)).c_str());
        return maximumStringLength > 0 ? s.substring (0, maximumStringLength) : s;
    }
    float getValueForText (const juce::String& text) const override
    {
        double plain = 0.0;
        if (set_.fromText (spec_.id, text.toStdString(), plain))
            return static_cast<float> (spec_.toNormalized (plain));
        return getValue();
    }

private:
    neptune::core::ParameterSet& set_;
    std::size_t index_;
    const neptune::core::ParamSpec& spec_;
};
