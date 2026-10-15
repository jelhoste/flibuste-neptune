#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// Racine mise à l'échelle d'un éditeur : tout le contenu vit dans cette composante, dont la taille logique est au moins
// (designWidth x designHeight). Quand la fenêtre est plus petite, le contenu est réduit d'un même facteur au lieu de se superposer :
// l'interface reste utilisable sur un écran modeste. Au-dessus de cette taille, rien n'est agrandi : la disposition s'étire.
class ScaledRoot : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> onPaint;

    // Déplace dans la racine tous les enfants actuels de `editor`, puis ajoute la racine à `editor`.
    void adopt (juce::Component& editor)
    {
        std::vector<juce::Component*> kids;
        for (auto* c : editor.getChildren())
            if (c != this) kids.push_back (c);
        for (auto* c : kids) addChildComponent (*c, -1); // addChild garde la visibilité (addAndMakeVisible la forcerait)
        for (auto* c : kids) c->setVisible (c->isVisible());
        editor.addAndMakeVisible (*this);
    }

    // Calcule le facteur d'échelle pour une fenêtre de `windowW` x `windowH` ; retourne la taille logique de la racine.
    juce::Rectangle<int> fit (int windowW, int windowH, int designWidth, int designHeight, float minScale = 0.3f)
    {
        const float s = juce::jmax (minScale, juce::jmin (1.0f, static_cast<float> (windowW) / static_cast<float> (designWidth),
                                                              static_cast<float> (windowH) / static_cast<float> (designHeight)));
        scale_ = s;
        setTransform (juce::AffineTransform::scale (s));
        const juce::Rectangle<int> logical (0, 0, juce::roundToInt (static_cast<float> (windowW) / s),
                                            juce::roundToInt (static_cast<float> (windowH) / s));
        setBounds (logical);
        return logical;
    }

    float scale() const noexcept { return scale_; }
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }

private:
    float scale_ = 1.0f;
};
