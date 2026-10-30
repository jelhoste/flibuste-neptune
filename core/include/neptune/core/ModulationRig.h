#pragma once
// Rack de modulation d'un module à réglages modulables : la matrice (LFO, enveloppes, routes vers des réglages), sa version compilée remise au thread audio
// sans verrou, l'état MIDI qui nourrit les sources (contrôleurs, pitch bend, pression), les touches tenues (porte des enveloppes) et la remise à zéro des
// décalages du bloc précédent. C'est ce que le moteur de Neptune fait pour chaque piste (Engine::processChunk), pour un module seul : un plugin comme
// AnalogSynth n'a qu'à lui passer ses événements MIDI et à l'évaluer une fois par bloc, avant de produire le son.
// Cœur portable, sans JUCE. Aucune allocation côté audio.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "neptune/core/HandoffSlot.h"
#include "neptune/core/MidiFilter.h"
#include "neptune/core/Modulation.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::modulation
{
class Rig
{
public:
    Rig() { touched_.reserve (kMaxRoutes + 64); }

    // ---- thread de message ----
    void prepare (double sampleRate) noexcept { modulator_.prepare (sampleRate); }

    // Enregistre la matrice (bornée par `sanitised`) et la compile pour le thread audio ; `set` donne les réglages que les routes visent.
    void setMatrix (Matrix matrix, const ParameterSet& set)
    {
        description_ = sanitised (std::move (matrix));
        slot_.publish (compile (description_, set));
        slot_.collect();
    }
    const Matrix& matrix() const noexcept { return description_; }   // thread de message seulement

    // ---- thread audio ----
    void midiEvent (const midi::Message& m) noexcept { state_.update (m); }
    void noteOn (int key) noexcept { if (key >= 0 && key < 128) held_[key >> 6] |= std::uint64_t (1) << (key & 63); }
    void noteOff (int key) noexcept { if (key >= 0 && key < 128) held_[key >> 6] &= ~(std::uint64_t (1) << (key & 63)); }
    void releaseAll() noexcept { held_[0] = held_[1] = 0; }

    // Une fois par bloc, avant le rendu : remet à zéro les décalages du bloc précédent, adopte une nouvelle matrice si elle est arrivée, puis évalue les
    // sources de `frames` échantillons et ajoute leurs décalages aux réglages (voir ParameterSet::modOffsetAt).
    void process (int frames, const ParameterSet& set) noexcept
    {
        clearOffsets (set);
        if (slot_.adoptPending()) modulator_.setCompiled (slot_.current());
        modulator_.process (frames, (held_[0] | held_[1]) != 0, state_, set, touched_);
    }
    // Remet les décalages à zéro (arrêt, rechargement d'un état...).
    void clearOffsets (const ParameterSet& set) noexcept
    {
        for (const auto i : touched_) set.setModOffsetAt (i, 0.0f);
        touched_.clear();
    }
    void reset (const ParameterSet& set) noexcept
    {
        clearOffsets (set);
        modulator_.reset();
        releaseAll();
    }

private:
    Matrix description_;
    HandoffSlot<Compiled> slot_;
    Modulator modulator_;
    midi::State state_;
    std::vector<std::size_t> touched_;
    std::uint64_t held_[2] {};
};
} // namespace neptune::core::modulation
