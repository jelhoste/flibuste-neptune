#pragma once
// Graphe WAVE de La Flèche : historique défilant du signal (entrée en aire, sortie), des enveloppes de détection (rapide et lente) et du gain appliqué.
// Même méthode que l'historique du compresseur (voir NovaHistoryView.h, d'après ZL Compressor de zsliu98, AGPLv3) : l'anneau d'analyse du DSP est lu à
// intervalles réguliers (40 à 15 points par seconde), un point est calculé par flux, les points défilent sur l'horloge murale (Sync, Slow, Roll).
// Réécrit pour ce projet. Copyright (C) 2025-2026 zsliu98 pour la méthode d'origine ; adaptation pour ce projet.
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "NovaKit.h"
#include "neptune/core/ShaperModule.h"

namespace nova
{
class ShaperWaveView : public juce::Component
{
public:
    ShaperWaveView (const Skin&, ParamHost&, const neptune::core::ShaperModule*);
    ~ShaperWaveView() override;

    // Un pas de l'historique à l'instant `nowSeconds` (horloge murale). Appelé à la cadence de l'écran ; public pour le rendu hors-ligne.
    void step (double nowSeconds);
    void paint (juce::Graphics&) override;
    void resized() override { dirty_ = true; }

    // Fenêtre des réglages de l'analyseur (durée, défilement), ancrée sur `anchor`
    static void showSettings (juce::Component* anchor, const Skin&, ParamHost&, const std::function<juce::String (const char*)>& tx);

private:
    void readSettings();
    void configure (double sampleRate, int maxBlock);
    void computePoint (std::uint64_t start, int n);
    void pushPoint (bool zero);
    void updateXs (double now);
    void rebuildPaths();
    juce::Rectangle<float> plotArea() const;

    const Skin& skin_;
    ParamHost& host_;
    const neptune::core::ShaperModule* module_;
    juce::VBlankAttachment vblank_;

    int timeIdx_ = 3, moveType_ = 0;
    float range_ = 15.0f;   // dB, borne du gain affiché (paramètre RANGE)

    // un point = une valeur par flux (amplitude linéaire 0..1, gain en dB)
    std::vector<float> xs_, inV_, outV_, fastV_, slowV_, gainV_;
    juce::Path inPath_, outPath_, fastPath_, slowPath_, gainPath_;
    float inNow_ = 0.0f, outNow_ = 0.0f, fastNow_ = 0.0f, slowNow_ = 0.0f, gainNow_ = 0.0f;
    std::uint64_t readPos_ = 0;
    double startTime_ = 0.0, motionStart_ = 0.0, secondPerPoint_ = 0.0, timeLength_ = 18.0, sampleRate_ = 0.0;
    int samplesPerPoint_ = 0, pointsPerSecond_ = 15, missing_ = 0, maxBlock_ = 0, tooMuch_ = 0;
    std::size_t numPoints_ = 0, rollNext_ = 0;
    bool first_ = true, dirty_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperWaveView)
};
} // namespace nova
