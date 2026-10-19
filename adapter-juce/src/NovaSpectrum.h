#pragma once
// Moteur d'analyse de spectre de l'égaliseur côté interface : un fil de basse priorité (30 Hz) tire les échantillons de l'anneau d'analyse du module
// (AnalyzerRing, entrée et sortie), calcule les spectres (SpectrumAnalyzer, d'après ZL Equalizer) et publie la dernière image ; le fil d'affichage ne fait que
// construire les tracés. Réglages lus des paramètres cachés du module (spOn, spQuality, ...).
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "ParamHost.h"
#include "neptune/core/EqualizerModule.h"
#include "neptune/core/SpectrumAnalyzer.h"

namespace nova
{
class SpectrumEngine : private juce::Thread
{
public:
    struct Frame
    {
        std::vector<float> freqs;
        std::array<std::vector<float>, 2> db;   // [0] entrée, [1] sortie ; vide si la source n'est pas affichée
        bool highQuality = false;
        std::uint32_t version = 0;
    };

    SpectrumEngine (ParamHost&, const neptune::core::EqualizerModule*);
    ~SpectrumEngine() override;

    void setRunning (bool on);
    bool pullFrame (Frame& out);   // vrai s'il y a une image plus récente que la dernière copiée
    void stepOnce();               // une itération de l'analyse (le fil la répète) ; publique pour le rendu hors-ligne
    static void setThreading (bool enabled);   // faux : pas de fil (rendu hors-ligne, tests)

private:
    void run() override;

    ParamHost& host_;
    const neptune::core::EqualizerModule* module_;
    neptune::core::SpectrumAnalyzer analyzer_;
    std::array<std::uint64_t, 2> readPos_ {};
    double preparedRate_ = 0.0;
    bool preparedHq_ = false, prepared_ = false;
    std::atomic<bool> running_ { false };
    juce::CriticalSection lock_;
    Frame frame_;
    std::uint32_t copiedVersion_ = 0;
};

// Tracé fermé d'un spectre (dB par fréquence) : `xOfHz` et `yOfDb` donnent les coordonnées dans le composant, `bottomY` le bas de la zone, `rightX` le bord droit.
// En qualité Normal, les 32 premières cases sont lissées par Makima (comme ZL), car elles sont très espacées en échelle logarithmique.
juce::Path makeSpectrumPath (const SpectrumEngine::Frame&, int source, const std::function<float (float)>& xOfHz, const std::function<float (float)>& yOfDb,
                             float bottomY, float rightX);
} // namespace nova
