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
#include "NovaKit.h"
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
    };

    explicit WaveView (const Skin&);
    ~WaveView() override;

    // Fichier affiché (chemin UTF-8) ; chemin vide = rien. `frames` et `sampleRate` : longueur et fréquence du fichier.
    void setSample (const std::string& path, std::int64_t frames, double sampleRate);
    void setMarkers (const Markers&);           // sans notification ; ignoré pendant un glissé
    void showAll();
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
    juce::Point<float> hoverPos_;
    bool hovering_ = false;
};
} // namespace nova
