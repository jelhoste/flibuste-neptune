#pragma once
// Orgue à roues phoniques en plugin indépendant : paramètres, moteur (voix, description, file de notes de l'interface, volume et pan
// propres), état. Même cœur que le type de piste Orgue de Neptune ; les identifiants de paramètres sont ceux de la piste 0 de
// Neptune (la vue et les presets sont donc les mêmes).
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "neptune/core/Control.h"
#include "neptune/core/HandoffSlot.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/OrganEngine.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::organ
{
constexpr ParamId synthVolume = probe::trackParam (0, probe::kVolume); // dB, -inf..+6
constexpr ParamId synthPan = probe::trackParam (0, probe::kPan);       // -100..+100

// Déclare volume, pan, gain et les paramètres de l'orgue. Hors temps réel.
bool addSynthParams (ParameterSet& set, std::string* error = nullptr);

// Valeurs lues à chaque bloc dans un jeu de paramètres, pour la piste `track` (0 pour le plugin).
OrganLive liveFromParams (const ParameterSet&, int track = 0);

// Contrôleurs MIDI (CC) : mêmes numéros que setBfree. Chaque clavier écoute sa propre voie : tirettes 70..78 (valeur inversée, 127 =
// rentrée) ; sur la voie du supérieur : 91 réverb, 66 / 80 percussion, 81 douce, 82 rapide, 83 2e harmonique, 92 type de vibrato,
// 95 routage, 30 vibrato inférieur, 31 vibrato supérieur, 65 overdrive, 93 caractère de l'overdrive ; sur toutes les voies : 1 vitesse
// du Leslie (0..42 chorale, 43..85 frein, 86.. tremolo), 64 pédale (bascule chorale / tremolo). Écrit dans les paramètres de la piste.
// Retourne faux si le message ne concerne pas l'orgue.
bool applyControlChange (const OrganDescription&, ParameterSet&, int channel, int cc, int value, int track = 0);

// Assignations de la couche de contrôle (Control.h) qui reproduisent les CC de setBfree pour la piste `track`, avec les voies des
// claviers de `d` : tirettes 70..78 (sur la voie du clavier concerné), réverb 91, percussion 66 / 80..83, vibrato 92 / 30 / 31,
// overdrive 65 / 93, Leslie sur la molette (1 : bas = chorale, haut = tremolo). Mêmes conventions que applyControlChange, sauf le routage
// du vibrato (95) et la pédale du Leslie (64), qui ont besoin d'une logique propre à setBfree et ne sont pas des assignations simples.
std::vector<control::Binding> organControlBindings (const OrganDescription&, int track = 0);
// Vrai si `id` est un paramètre d'orgue de la piste `track` (pour remplacer les assignations d'une piste sans toucher aux autres).
bool isOrganParam (ParamId id, int track) noexcept;

// État du plugin (getStateInformation) : JSON versionné.
// `ccEnabled` : réaction aux contrôleurs MIDI (désactivée par défaut ; absent de l'état = désactivé).
std::string stateToJson (const ParameterSet&, const OrganDescription&, const std::string& currentPresetId, bool ccEnabled = false);
bool stateFromJson (std::string_view text, ParameterSet&, OrganDescription&, std::string& currentPresetId, bool* ccEnabled = nullptr);

class OrganSynthEngine
{
public:
    OrganSynthEngine();

    ParameterSet& params() noexcept { return params_; }
    const ParameterSet& params() const noexcept { return params_; }
    bool ok() const noexcept { return ok_; }

    void prepare (double sampleRate, int maxBlockSize); // alloue le moteur : hors temps réel

    // --- thread de message ---
    void setDescription (const OrganDescription&);
    const OrganDescription& description() const noexcept { return description_; }
    void collect() { slot_.collect(); voices_.collect(); }
    void postNoteOn (int channel, int key) noexcept { post (Event::NoteOn, channel, key); }
    void postNoteOff (int channel, int key) noexcept { post (Event::NoteOff, channel, key); }
    void postPanic() noexcept { post (Event::Panic, 0, 0); }

    // --- thread audio ---
    void noteOn (int channel, int key) noexcept;
    void noteOff (int channel, int key) noexcept;
    void allNotesOff() noexcept { voices_.allNotesOff(); clearHeld(); }
    void panic() noexcept { voices_.panic(); clearHeld(); }
    void controlChange (int channel, int cc, int value) noexcept { if (const auto* d = slot_.current()) applyControlChange (*d, params_, channel, cc, value); }
    // Écrase `left` et `right` avec le rendu (volume et pan appliqués). Aucune allocation.
    void process (float* left, float* right, int numFrames) noexcept;

    int heldKeys() const noexcept { return voices_.heldKeys(); }
    std::uint64_t heldKeyWord (int word) const noexcept { return held_[word & 1].load (std::memory_order_relaxed); }

private:
    struct Event { enum Type : int { NoteOn, NoteOff, Panic } type = NoteOn; int channel = 0, key = 0; };
    void post (Event::Type t, int channel, int key) noexcept;
    void drain() noexcept;
    void adopt() noexcept { if (slot_.adoptPending()) voices_.setDescription (slot_.current()); }
    void clearHeld() noexcept { held_[0].store (0, std::memory_order_relaxed); held_[1].store (0, std::memory_order_relaxed); }

    ParameterSet params_;
    bool ok_ = true;
    std::size_t idxVolume_ = 0, idxPan_ = 0, idxGain_ = 0;

    OrganDescription description_;
    HandoffSlot<OrganDescription> slot_;
    OrganVoices voices_;
    float lastGain_ = 1.0f, lastFader_ = 1.0f, lastPanL_ = 1.0f, lastPanR_ = 1.0f;
    std::atomic<std::uint64_t> held_[2] {};

    static constexpr std::size_t kQueue = 256;
    std::array<Event, kQueue> queue_ {};
    std::atomic<std::size_t> head_ { 0 }, tail_ { 0 };
};
} // namespace neptune::core::organ
