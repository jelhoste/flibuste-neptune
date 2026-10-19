#pragma once
// Panneau « SIGNAL / TRANSFER CURVE » du compresseur : historique d'amplitude défilant (entrée en aire, sortie, réduction, clé) en fond et courbe de
// transfert par-dessus, sur la même échelle verticale en dB. Méthode reprise de ZL Compressor de zsliu98 (https://github.com/ZL-Audio/ZLCompressor,
// panel/curve_panel/mag_analyzer_panel, AGPLv3 comme ce projet) : l'anneau d'analyse du DSP (AnalyzerRing) est lu par intervalles réguliers (40 à 15 points
// par seconde), un niveau par flux est calculé pour chaque point, la réduction est le niveau compressé moins le niveau d'entrée, les points défilent sur
// l'horloge murale. Réécrit pour ce projet : pas de fil dédié (la construction des chemins coûte moins d'une demi-milliseconde), pas d'OpenGL.
// Copyright (C) 2025-2026 zsliu98 pour la méthode d'origine ; adaptation pour ce projet.
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "NovaKit.h"
#include "neptune/core/CompressorModule.h"

namespace nova
{
class CompressorHistoryView : public juce::Component
{
public:
    CompressorHistoryView (const Skin&, ParamHost&, const neptune::core::CompressorModule*);
    ~CompressorHistoryView() override;

    // Un pas de l'historique à l'instant `nowSeconds` (horloge murale). Appelé à la cadence de l'écran ; public pour le rendu hors-ligne.
    void step (double nowSeconds);
    // Dernière réduction de gain mesurée (dB, <= 0) et niveau d'entrée (dB) : pour la légende du panneau
    float lastReductionDb() const noexcept { return reductionDb_; }
    float lastInputDb() const noexcept { return preDb_; }

    void paint (juce::Graphics&) override;
    void resized() override { invalidate(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Fenêtre des réglages de l'analyseur (durée, défilement, plage, crête / RMS, canaux, courbes visibles), ancrée sur `anchor`
    static void showSettings (juce::Component* anchor, const Skin&, ParamHost&, const std::function<juce::String (const char*)>& tx);

private:
    struct Range { float maxDb, rangeDb; };   // rangeDb < 0 : l'échelle descend du haut vers le bas

    void invalidate() { curveValid_ = false; }
    void readSettings();
    void resetHistory();
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> squareArea() const;
    float yOf (float db) const noexcept;
    float dbOfY (float y) const noexcept;
    float xOfIn (float db) const;
    float dbOfX (float x) const;
    juce::Point<float> handle() const;
    bool nearHandle (juce::Point<float>) const;
    void rebuildCurve();
    void renderBackground (juce::Graphics&) const;
    void renderOverlay (juce::Graphics&);
    void rebuildPaths();
    void updateXs (double now);
    void updateYMapping();

    const Skin& skin_;
    ParamHost& host_;
    const neptune::core::CompressorModule* module_;
    juce::VBlankAttachment vblank_;

    // réglages lus des paramètres cachés
    int timeIdx_ = 3, moveType_ = 0;
    float maxDb_ = 0.0f, minDb_ = -54.0f;
    neptune::core::MagType mag_ = neptune::core::MagType::Peak;
    neptune::core::StereoType stereo_ = neptune::core::StereoType::MidSide;
    bool showIn_ = true, showOut_ = true, showRed_ = true, showKey_ = false, centreReduction_ = false;

    // historique
    std::vector<float> xs_, preYs_, outYs_, keyYs_, redYs_;
    juce::Path inPath_, outPath_, keyPath_, redPath_;
    std::uint64_t readPos_ = 0;
    double startTime_ = 0.0, motionStart_ = 0.0, secondPerPoint_ = 0.0, timeLength_ = 9.0, sampleRate_ = 0.0;
    int samplesPerPoint_ = 0, pointsPerSecond_ = 40, missing_ = 0, maxBlock_ = 0;
    std::size_t numPoints_ = 0, rollNext_ = 0;
    bool first_ = true, mappingInit_ = false;
    float scale_ = 0.0f, bias_ = 0.0f, redBias_ = 0.0f;
    float preDb_ = -240.0f, outDb_ = -240.0f, keyDb_ = -240.0f, reductionDb_ = 0.0f;

    // courbe de transfert
    juce::Path curve_;
    bool curveValid_ = false, curveBuilt_ = false;
    juce::Image bgImage_, overlayImage_;   // couches fixes en cache, à la taille réelle en pixels
    std::uint32_t curveKey_ = 0xFFFFFFFFu;
    bool dragging_ = false;
    int tooMuch_ = 0, activeFrames_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorHistoryView)
};
} // namespace nova
