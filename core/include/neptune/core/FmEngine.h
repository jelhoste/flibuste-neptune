#pragma once
// Moteur FM de type DX7 : fine enveloppe autour de msfa (Apache 2.0, third_party/msfa). Aucun symbole de msfa n'apparaît ici.
//  - initTables : tables globales de msfa pour une fréquence d'échantillonnage (état partagé du processus) ;
//  - Note : une note de 6 opérateurs, rendue par blocs de 64 échantillons (sortie Q24 entière de msfa, ajoutée au tampon) ;
//  - Lfo : le LFO global d'un patch (vitesse, délai, forme, sync) ;
//  - algorithmGraph : topologie des 32 algorithmes (porteuses, modulations, feedback) pour l'interface et les macros.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "neptune/core/FmPatch.h"

namespace neptune::core::fm
{
constexpr int kBlock = 64;                       // la taille de bloc interne de msfa (N)
constexpr float kOutputScale = 1.0f / 268435456.0f; // sortie Q24 de msfa -> flottant : 2^28 = pleine échelle

// Initialise les tables globales de msfa (sinus, exponentielle, fréquences, LFO, enveloppes). Hors temps réel. Idempotent pour une
// même fréquence d'échantillonnage. Attention : l'état de msfa est global au processus, deux instances à des fréquences
// d'échantillonnage différentes se partagent la dernière valeur initialisée (limite connue).
void initTables (double sampleRate);

struct Controls
{
    int pitchBend = 8192;          // 0..16383, 8192 = repos
    int pitchBendRange = 2;        // demi-tons, 0..12
    int wheelPitchDepth = 0;       // 0..255 : vibrato ajouté par la molette
    int wheelAmpDepth = 0;         // 0..99 : trémolo ajouté par la molette
};

class Lfo
{
public:
    Lfo();
    ~Lfo();
    void reset (const Patch&);     // vitesse, délai, forme, sync du patch
    void keyDown();                // redémarre le délai (et la phase si « sync » est actif)
    std::int32_t sample();         // 0..1 en Q24, à appeler une fois par bloc de 64
    std::int32_t delay();          // 0..1 en Q24 (montée du délai)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Note
{
public:
    Note();
    ~Note();
    Note (const Note&) = delete;
    Note& operator= (const Note&) = delete;

    // Démarre la note : `patch` (déjà modifié par les macros), touche MIDI après transposition, vélocité 0..127.
    void start (const Patch& patch, int midiNote, int velocity);
    void keyUp();
    // Ajoute le rendu d'un bloc de kBlock échantillons dans `buf` (Q24 entiers).
    void compute (std::int32_t* buf, std::int32_t lfoValue, std::int32_t lfoDelay, const Controls&);
    // Décalages de hauteur Q24 par octave : `detune` pour tous les opérateurs, `glide` pour ceux en mode rapport.
    void setDetune (std::int32_t q24Octaves);
    void setGlide (std::int32_t q24Octaves);
    void setLevelOffset (int op, std::int32_t q24Doublings); // niveau d'un opérateur en doublements Q24 (1 << 24 = +6 dB)
    void setFeedback (int feedback);                         // 0..7, en direct

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Edge
{
    int from = 0, to = 0;          // indices d'opérateur du patch (0 = OP6 ... 5 = OP1) : `from` module `to`
};

struct AlgorithmGraph
{
    std::uint8_t carrierMask = 0;  // bit k : l'opérateur k est une porteuse (sortie audio)
    std::vector<Edge> edges;       // modulations
    int feedbackFrom = -1, feedbackTo = -1; // boucle de feedback (from == to : l'opérateur se module lui-même) ; -1 : aucune
};

const AlgorithmGraph& algorithmGraph (int algorithm);

// Décalage de niveau de sortie dû à la mise à l'échelle du clavier d'un opérateur (point de rupture, profondeurs et courbes gauche /
// droite) pour la touche `midiNote`, dans l'unité interne du DX7 (environ 0,75 dB par pas de niveau de sortie). Sert à l'affichage.
int keyboardLevelScaling (const Patch&, int opIndex, int midiNote);
} // namespace neptune::core::fm
