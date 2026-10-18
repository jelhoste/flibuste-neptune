#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "KnobModal.h"
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Theme.h"

// Éditeur générique d'un module stock : une ligne par paramètre (curseur, liste ou bascule), regroupés en sections repliées par plages
// d'identifiants, sous une zone d'en-tête dessinée par l'appelant (courbe, vu-mètres). Sert à écouter et régler un module avant que son interface
// soignée existe ; ne connaît que ParamHost, donc utilisable dans un insert de Neptune comme dans un plugin CLAP.
class GenericModuleEditor : public juce::Component, private juce::Timer
{
public:
    struct Group
    {
        juce::String title;
        neptune::core::ParamId first = 0, last = 0;        // plage d'identifiants (bornes comprises)
        bool open = true;
    };
    struct Options
    {
        std::vector<Group> groups;
        int headerHeight = 0;
        // Dessine l'en-tête (courbe, vu-mètres...) dans `area` ; appelé à chaque rafraîchissement (30 Hz).
        std::function<void (juce::Graphics&, juce::Rectangle<int> area)> paintHeader;
        std::vector<neptune::core::ParamId> hidden;       // paramètres à ne pas montrer
    };

    GenericModuleEditor (ParamHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, Options);
    ~GenericModuleEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Row;
    class Content;
    void timerCallback() override;
    void rebuild();
    juce::Colour colour (neptune::core::theme::Token t) const;

    ParamHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    Options options_;
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    int dirtyCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericModuleEditor)
};
