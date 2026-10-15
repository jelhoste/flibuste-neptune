#pragma once
// Orgue à roues phoniques (type de piste « Orgue » et plugin séparé) : description, valeurs automatisables et voix. Le moteur
// sonore est celui de setBfree (third_party/setbfree, via OrganCore.h) ; ici : routage des notes (3 claviers, canaux, split,
// transposition), traduction des réglages en fonctions de contrôle de setBfree, gain de sortie.
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace neptune::core::organ
{
constexpr int kManuals = 3;            // 0 supérieur, 1 inférieur, 2 pédalier
constexpr int kDrawbars = 9;           // 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
constexpr int kVibratoModes = 7;       // −, V1, C1, V2, C2, V3, C3
constexpr int kLeslieChorale = 0, kLeslieTremolo = 1, kLeslieBrake = 2;

// Réglages de jeu : sauvegardés avec la piste, non automatisables.
struct OrganDescription
{
    std::array<int, kManuals> channel { 1, 2, 3 };   // canal MIDI (1..16) de chaque clavier ; 0 : ce clavier n'écoute aucun canal
    int splitLower = 0;                              // note MIDI : sur le canal du supérieur, en dessous : pédalier (0 : pas de split)
    int splitUpper = 0;                              // note MIDI : sur ce même canal, en dessous : inférieur (0 : pas de split)
    std::array<int, kManuals> transpose { 0, 0, 0 }; // demi-tons, -24..24
    int tuningCents = 0;                             // -100..100 (accord général)

    bool operator== (const OrganDescription& o) const noexcept
    {
        return channel == o.channel && splitLower == o.splitLower && splitUpper == o.splitUpper && transpose == o.transpose
            && tuningCents == o.tuningCents;
    }
    bool operator!= (const OrganDescription& o) const noexcept { return ! (*this == o); }
};

OrganDescription sanitised (OrganDescription);
std::string toJson (const OrganDescription&);
OrganDescription organFromJson (std::string_view); // lecture tolérante

// Valeurs lues à chaque bloc (paramètres de la piste).
struct OrganLive
{
    std::array<std::array<int, kDrawbars>, kManuals> drawbars {};  // 0..8 ; { 8, 8, 6, ... } au départ du supérieur
    bool percussionOn = false, percussionSoft = false, percussionFast = true, percussionThird = true;
    int vibratoMode = 0;                           // 0 : sans vibrato ; 1..6 : V1 C1 V2 C2 V3 C3
    bool vibratoUpper = true, vibratoLower = false;
    int leslieSpeed = kLeslieChorale;
    float leslieMix = 1.0f;                        // 0..1
    bool driveOn = false;
    float driveInput = 0.5f, driveCharacter = 0.5f, driveOutput = 0.5f; // 0..1
    float reverbMix = 0.0f;                        // 0..1
    float tuneCents = 0.0f;                        // réservé : accord de la piste
    OrganLive();
};

// Valeurs lues à chaque bloc à partir de `numKinds` valeurs de paramètres dans l'ordre de mixer::OrganKind (tirettes du supérieur,
// de l'inférieur, pédalier 16' et 8', percussion, vibrato, Leslie, overdrive, réverb) : même traduction pour le plugin et Neptune.
constexpr int kOrganValues = 34;
OrganLive liveFromValues (const double* values) noexcept;

class OrganVoices
{
public:
    OrganVoices();
    ~OrganVoices();
    OrganVoices (const OrganVoices&) = delete;
    OrganVoices& operator= (const OrganVoices&) = delete;

    // Thread de message : crée le moteur (tables d'ondes) pour cette fréquence. Idempotent pour une même fréquence ; une autre
    // fréquence recrée le moteur (appeler hors lecture).
    bool reserve (double sampleRate);
    bool reserved() const noexcept { return reserved_.load (std::memory_order_acquire); }
    // Description adoptée (nulle : celle par défaut).
    void setDescription (const OrganDescription* d) noexcept { desc_ = d; }

    void noteOn (int channel, int note) noexcept;    // canal 0..15 (hôte), note 0..127
    void noteOff (int channel, int note) noexcept;
    void allNotesOff() noexcept;                     // relâche tout (All Notes Off)
    void panic() noexcept;                           // relâche tout et coupe la cabine
    int heldKeys() const noexcept;

    // Ajoute (si `add`) ou écrase le rendu dans left / right ; gain interpolé de gainStart à gainEnd.
    void render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const OrganLive&, bool add = true) noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    const OrganDescription* desc_ = nullptr;
    std::atomic<bool> reserved_ { false };
};

// Clavier de setBfree visé par une note (-1 : hors étendue ou aucun clavier n'écoute ce canal). Exposé pour les tests.
int organKeyFor (const OrganDescription&, int channel, int note) noexcept;
} // namespace neptune::core::organ
