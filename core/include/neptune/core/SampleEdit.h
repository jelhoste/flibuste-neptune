#pragma once
// Éditeur de sample (spec §13.5), partie calcul : opérations non destructives sur une copie de travail (le fichier source n'est jamais modifié),
// analyses (silence, transitoires, points de boucle) et aimantation au zéro. Hors temps réel, sans dépendance JUCE.
#include <cstdint>
#include <filesystem>
#include <string>
#include <memory>
#include <vector>

#include "neptune/core/SampleData.h"

namespace neptune::core::sampleedit
{
// Audio flottant entrelacé, en mémoire.
struct Buffer
{
    int channels = 0;
    double sampleRate = 44100.0;
    std::vector<float> data;
    std::int64_t frames() const noexcept { return channels > 0 ? static_cast<std::int64_t> (data.size()) / channels : 0; }
    float at (std::int64_t frame, int channel) const noexcept { return data[static_cast<std::size_t> (frame * channels + channel)]; }
};

// Tampon en mémoire -> SampleData (float32 planaire) jouable par le moteur (écoutes libres).
std::shared_ptr<const SampleData> toSampleData (const Buffer&, const std::string& name);

// ---- fichiers
bool readFile (const std::filesystem::path&, Buffer& out, std::string* error = nullptr);   // tout le fichier
bool writeWav (const std::filesystem::path&, const Buffer&, std::string* error = nullptr); // WAV 32 bits flottant ; crée les dossiers
// Dossier des copies de travail et nom d'un fichier dérivé (source + opération + empreinte du contenu) : même entrée, même nom.
std::filesystem::path derivedPath (const std::filesystem::path& dir, const std::string& sourcePath, const std::string& tag, const Buffer&);

// ---- mesures (région [from, to), to < 0 : jusqu'à la fin)
double peakAmplitude (const Buffer&, std::int64_t from = 0, std::int64_t to = -1) noexcept;     // 0..1+
double rmsAmplitude (const Buffer&, std::int64_t from = 0, std::int64_t to = -1) noexcept;      // sur tous les canaux
double toDb (double amplitude) noexcept;                                                         // -120 pour le silence

// ---- opérations
Buffer trimmed (const Buffer&, std::int64_t from, std::int64_t to);          // garde [from, to)
void applyGainDb (Buffer&, double db) noexcept;
enum class NormalizeTarget { Peak, Rms };
// Gain (dB) qui amène la région à `targetDb` (crête ou RMS). En RMS, le gain est borné pour que la crête de la région ne dépasse pas 0 dBFS (`limited` le dit).
double normalizeGainDb (const Buffer&, std::int64_t from, std::int64_t to, NormalizeTarget, double targetDb, bool* limited = nullptr) noexcept;
// Filtre de DC de l'Utilitaire (passe-haut 1 pôle à 5 Hz), sur tout le buffer.
void removeDc (Buffer&) noexcept;
// Décalage moyen par canal (pour affichage / décision), sur la région.
double dcOffset (const Buffer&, std::int64_t from = 0, std::int64_t to = -1) noexcept;

// ---- analyses
struct Silence { bool found = false; std::int64_t start = 0, end = 0; };       // [start, end) : le son utile
Silence detectSilence (const Buffer&, double thresholdDb) noexcept;

// Débuts de coups (frames), triés. sensitivity 0..1 (plus haut = plus de coups). minGapMs : écart minimal entre deux coups.
std::vector<std::int64_t> detectTransients (const Buffer&, double sensitivity, double minGapMs);

struct LoopCandidate { std::int64_t start = 0, end = 0; double score = 0.0; };    // score 0..1 (1 = raccord parfait)
// Réglages de la recherche de boucle.
struct LoopSearchOptions
{
    bool period = true;        // longueur de boucle = nombre entier de périodes du son (hauteur détectée par autocorrélation)
    bool stableOnly = true;    // ne chercher que dans la zone stable (niveau et brillance qui varient peu), hors attaque et fin
    bool spectral = true;      // le score compare aussi le spectre et le niveau avant le saut, pas seulement la forme d'onde
    bool aroundCursor = false; // le début de boucle doit tomber à moins de `radiusMs` du curseur
    std::int64_t cursor = -1;
    double radiusMs = 500.0;
};
// Candidats de boucle dans [from, to) : au plus `maxCount`, meilleurs d'abord, longueur au moins `minLengthMs`.
std::vector<LoopCandidate> findLoopCandidates (const Buffer&, std::int64_t from, std::int64_t to, int maxCount, double minLengthMs,
                                               const LoopSearchOptions& = {});
// Briques de la recherche (publiques pour les tests) :
struct StableRegion { bool found = false; std::int64_t start = 0, end = 0; };
StableRegion findStableRegion (const Buffer&, std::int64_t from, std::int64_t to, double minLengthMs);   // plus longue plage où niveau et brillance sont stables
double detectPeriod (const Buffer&, std::int64_t from, std::int64_t to) noexcept;                       // période en échantillons (0 : son non périodique)

// Passage par zéro (changement de signe du mélange mono) le plus proche de `frame`, dans ±radius ; `frame` si aucun.
std::int64_t snapToZero (const Buffer&, std::int64_t frame, std::int64_t radius) noexcept;
} // namespace neptune::core::sampleedit
