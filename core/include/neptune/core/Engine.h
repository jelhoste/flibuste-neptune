#pragma once
#include <cstdint>

#include "neptune/core/Profile.h"

namespace neptune::core
{
// Frontière unique entre le cœur et les adaptateurs (JUCE/CLAP, tests, futur web).
// Squelette du palier 0 : aucune synthèse, il produit du silence.
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void setParameter (std::uint32_t id, double value) noexcept;
    void process (float* const* out, int numChannels, int numFrames) noexcept;
    // Profil Live/Studio effectif de l'instance ; appelé par l'adaptateur avant chaque bloc (sans coût). Aucun effet audible pour l'instant.
    void setProfile (PerformanceProfile p) noexcept { profile_ = p; }
    PerformanceProfile profile() const noexcept { return profile_; }

    double sampleRate() const noexcept { return sampleRate_; }
    int activeNotes() const noexcept { return activeNotes_; }

private:
    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    int activeNotes_ = 0;
    PerformanceProfile profile_ = PerformanceProfile::Live;
};
} // namespace neptune::core
