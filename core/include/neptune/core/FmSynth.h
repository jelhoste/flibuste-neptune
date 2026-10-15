#pragma once
// Synthé FM (module FM de type DX7) en plugin indépendant : paramètres, moteur (voix, description, file de notes de l'interface,
// volume et pan propres), état. Même cœur que le type de piste FM de Neptune ; les identifiants de paramètres sont ceux de la
// piste 0 de Neptune (la vue et les presets sont donc les mêmes).
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "neptune/core/FmVoices.h"
#include "neptune/core/HandoffSlot.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::fm
{
constexpr ParamId synthVolume = probe::trackParam (0, probe::kVolume); // dB, -inf..+6
constexpr ParamId synthPan = probe::trackParam (0, probe::kPan);       // -100..+100

// Déclare volume, pan, gain, accordage et les 11 macros FM. Hors temps réel.
bool addSynthParams (ParameterSet& set, std::string* error = nullptr);

// État du plugin (getStateInformation) : JSON versionné.
std::string stateToJson (const ParameterSet&, const FmDescription&, const std::string& currentPresetId);
bool stateFromJson (std::string_view text, ParameterSet&, FmDescription&, std::string& currentPresetId);

class FmSynthEngine
{
public:
    FmSynthEngine();

    ParameterSet& params() noexcept { return params_; }
    const ParameterSet& params() const noexcept { return params_; }
    bool ok() const noexcept { return ok_; }

    void prepare (double sampleRate, int maxBlockSize);

    // --- thread de message ---
    void setDescription (const FmDescription&);
    const FmDescription& description() const noexcept { return description_; }
    void collect() { slot_.collect(); }
    void postNoteOn (int key, float velocity) noexcept { post (Event::NoteOn, key, velocity); }
    void postNoteOff (int key) noexcept { post (Event::NoteOff, key, 0.0f); }
    void postPanic() noexcept { post (Event::Panic, 0, 0.0f); }
    void postReleaseAll() noexcept { post (Event::ReleaseAll, 0, 0.0f); }

    // --- thread audio ---
    void noteOn (int channel, int key, float velocity) noexcept;
    void noteOff (int channel, int key) noexcept;
    void releaseAll() noexcept { clearHeld(); voices_.releaseAll(); }
    void panic() noexcept { clearHeld(); voices_.panic(); }
    void setSustain (bool on) noexcept { voices_.setSustain (on); }
    void setModWheel (int value) noexcept { modWheel_ = value; }
    void setPitchBend (int value) noexcept { pitchBend_ = value; }
    // Écrase `left` et `right` avec le rendu (volume et pan appliqués). Aucune allocation.
    void process (float* left, float* right, int numFrames) noexcept;

    int voiceCount() const noexcept { return voiceCount_.load (std::memory_order_relaxed); }
    int activeVoices() const noexcept { return voices_.activeVoices(); }
    std::uint64_t heldKeyWord (int word) const noexcept { return held_[word & 1].load (std::memory_order_relaxed); }

private:
    struct Event { enum Type : int { NoteOn, NoteOff, Panic, ReleaseAll } type = NoteOn; int key = 0; float velocity = 0.0f; };
    void post (Event::Type t, int key, float velocity) noexcept;
    void drain() noexcept;
    void adopt() noexcept { if (slot_.adoptPending()) voices_.setDescription (slot_.current()); }
    void clearHeld() noexcept { held_[0].store (0, std::memory_order_relaxed); held_[1].store (0, std::memory_order_relaxed); }

    ParameterSet params_;
    bool ok_ = true;
    std::array<std::size_t, mixer::kFmKinds> fmIdx_ {};
    std::size_t idxVolume_ = 0, idxPan_ = 0, idxGain_ = 0, idxSemi_ = 0, idxCents_ = 0;

    FmDescription description_;
    HandoffSlot<FmDescription> slot_;
    FmVoices voices_;
    float lastGain_ = 1.0f, lastFader_ = 1.0f, lastPanL_ = 1.0f, lastPanR_ = 1.0f;
    int modWheel_ = 0, pitchBend_ = 8192;
    std::atomic<int> voiceCount_ { 0 };
    std::atomic<std::uint64_t> held_[2] {};

    static constexpr std::size_t kQueue = 256;
    std::array<Event, kQueue> queue_ {};
    std::atomic<std::size_t> head_ { 0 }, tail_ { 0 };
};
} // namespace neptune::core::fm
