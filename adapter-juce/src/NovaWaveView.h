#pragma once
// Forme d'onde d'un sample au style NOVA, avec les marqueurs de la zone : début et fin de lecture, début et fin de boucle, fondu enchaîné de boucle.
// Zoom à la molette (centré sur la souris), défilement Maj + molette ou glisser dans le vide, double-clic = tout voir. L'enveloppe (min / max par colonne) est calculée
// sur le disque par un fil d'arrière-plan pour la région visible et la largeur actuelle (computePeaks) : zoomer affine l'affichage sans tout garder en mémoire.
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "NovaKit.h"
#include "neptune/core/VolumeFootprint.h"
#include "neptune/core/WaveformPeaks.h"

namespace nova
{
class WaveView : public juce::Component, private juce::Timer
{
public:
    enum class Marker { Start, End, LoopStart, LoopEnd, Crossfade };
    struct Markers
    {
        std::int64_t start = 0, end = -1;          // région jouée (end < 0 : fin du fichier)
        std::int64_t loopStart = 0, loopEnd = -1;  // boucle (loopEnd < 0 : fin du fichier)
        std::int64_t crossfade = 0;                // longueur du fondu enchaîné avant la fin de boucle
        bool loop = false, reverse = false;
        // visualisation des réglages de lecture (en échantillons du fichier, à la hauteur d'origine du sample)
        std::int64_t fadeInFrames = 0, fadeOutFrames = 0, delayFrames = 0;
        double gainDb = 0.0, tuneCents = 0.0;
        double delayMs = 0.0, fadeInMs = 0.0, fadeOutMs = 0.0;
    };

    explicit WaveView (const Skin&);
    ~WaveView() override;

    // Fichier affiché (chemin UTF-8) ; chemin vide = rien. `frames` et `sampleRate` : longueur et fréquence du fichier.
    void setSample (const std::string& path, std::int64_t frames, double sampleRate);
    void setMarkers (const Markers&);           // sans notification ; ignoré pendant un glissé
    void showAll();

    // Empreintes de volume superposées (spec §13.5 / §23) : `mask` = bits 0 Pic, 1 RMS, 2 LUFS. Dégradé de couleur selon le niveau, jusqu'au violet au maximum de la courbe.
    void setFootprint (std::shared_ptr<const neptune::core::VolumeFootprint>, int mask);
    // Filtre de plage de puissance : illumine les zones dont le niveau de la courbe `kind` est entre lo et hi (dB).
    void setRange (bool on, double lo, double hi, int kind);
    // Repère de navigation (frame < 0 : aucun) ; `reveal` recentre la vue si le repère en sort.
    void setCursor (std::int64_t frame, bool reveal);
    void setAutoLabel (juce::String noSample) { noSample_ = std::move (noSample); repaint(); }

    std::function<void (Marker, std::int64_t frame)> onMarker;   // pendant un glissé (valeur brute : l'éditeur borne)
    std::function<void()> onGestureBegin, onGestureEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Request { std::string path; std::int64_t from = 0, to = 0; int bins = 0; std::uint64_t id = 0; };
    void timerCallback() override;
    void worker();
    void requestPeaks();
    double frameAt (float x) const noexcept;
    float xOf (double frame) const noexcept;
    juce::Rectangle<float> plot() const;
    struct Hit { bool valid = false; Marker marker = Marker::Start; };
    Hit hitTest (juce::Point<float>) const;
    std::int64_t effectiveEnd() const noexcept { return markers_.end < 0 ? frames_ : std::min (markers_.end, frames_); }
    std::int64_t effectiveLoopEnd() const noexcept { return markers_.loopEnd < 0 ? frames_ : std::min (markers_.loopEnd, frames_); }
    void zoomBy (double factor, double anchorFrame);
    void clampView();

    const Skin& skin_;
    juce::String noSample_;
    std::string path_;
    std::int64_t frames_ = 0;
    double sampleRate_ = 48000.0;
    Markers markers_;
    double viewFrom_ = 0.0, viewTo_ = 1.0;

    // fil d'arrière-plan : une demande à la fois (la plus récente)
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool quit_ = false;
    Request pending_;
    bool havePending_ = false;
    std::atomic<bool> cancel_ { false };
    std::uint64_t nextId_ = 1, shownId_ = 0, requestedId_ = 0;
    neptune::core::WaveformPeaks result_, shown_;
    std::uint64_t resultId_ = 0;
    std::string shownPath_;
    double lastFrom_ = -1.0, lastTo_ = -1.0;
    int lastBins_ = 0;

    bool dragging_ = false, panning_ = false;
    Hit active_;
    float panStartX_ = 0.0f;
    double panStartFrom_ = 0.0, panStartTo_ = 0.0;
    std::shared_ptr<const neptune::core::VolumeFootprint> footprint_;
    int curveMask_ = 0;
    float curveMin_[3] = { -60.0f, -60.0f, -60.0f }, curveMax_[3] = { 0.0f, 0.0f, 0.0f };
    bool rangeOn_ = false;
    double rangeLo_ = -30.0, rangeHi_ = 0.0;
    int rangeKind_ = 0;
    std::int64_t cursorFrame_ = -1;
    juce::Point<float> hoverPos_;
    bool hovering_ = false;
};

// Raccord de boucle (spec §13.5, vue superposée) : à gauche ce qu'on entend au passage de la boucle (la fin de boucle suivie du début), à droite la fin de boucle
// et ce qui précède le début de boucle superposés. Des courbes qui se confondent = boucle sans coupure audible. Les données (mélange mono) viennent de l'éditeur.
class LoopSeamView : public juce::Component
{
public:
    explicit LoopSeamView (const Skin& s) : skin_ (s) { setInterceptsMouseClicks (false, false); }
    // `beforeEnd` : les échantillons qui précèdent la fin de boucle ; `afterStart` : ceux qui suivent le début ; `beforeStart` : ceux qui le précèdent. Vides = rien à montrer.
    void setData (std::vector<float> beforeEnd, std::vector<float> afterStart, std::vector<float> beforeStart);
    void paint (juce::Graphics&) override;

private:
    const Skin& skin_;
    std::vector<float> beforeEnd_, afterStart_, beforeStart_;
};
} // namespace nova
