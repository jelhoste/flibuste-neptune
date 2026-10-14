#pragma once
// Catalogue des formes d'onde du synthé (spec-modules §2) : explorable par catégorie, noms génériques (aucune marque déposée).
//  - Les formes « analytiques » (sinus, triangle, dent de scie, carré, impulsion) sont générées à bande limitée par PolyBLEP /
//    PolyBLAMP dans SynthVoices (meilleure qualité, largeur d'impulsion réglable).
//  - Toutes les autres sont des tables d'onde à bande limitée : une table par octave (mip), construites une fois à partir de leur
//    spectre. À la lecture, on choisit la table dont le nombre d'harmoniques tient sous Nyquist : aucun repliement.
// Les identifiants sont STABLES (enregistrés par leur clé texte dans les projets) : ne jamais renuméroter, seulement ajouter.
#include <string_view>
#include <vector>

namespace neptune::core
{
enum class WaveCategory { Digital = 0, Analog = 1, Complex = 2 };
constexpr int numWaveCategories = 3;

enum WaveId : int
{
    // numérique classique
    kWaveSine = 0, kWaveTriangle, kWaveSaw, kWaveSquare, kWavePulse, kWaveHalfSine, kWaveFullRect, kWaveStaircase, kWaveParabola, kWaveTrapezoid,
    // style analogique
    kWaveLadderSaw, kWaveSoftSquare, kWaveDoubleSaw, kWaveReverseSaw, kWaveWarmSaw, kWaveNarrowPulse, kWaveTriSaw, kWaveSineStack,
    // complexe
    kWaveOrgan8, kWaveOrganFull, kWaveVoxA, kWaveVoxE, kWaveVoxO, kWaveReed, kWaveBrass, kWaveStrings, kWaveGlass, kWaveHollow,
    kWaveDigitalNoise, kWaveOctaves, kWaveFifths, kWaveComb,
    kNumWaves
};

struct WaveInfo
{
    int id;
    std::string_view key;       // "ladder_saw" : enregistré dans les projets
    std::string_view nameKey;   // clé i18n : "wave.ladder_saw"
    WaveCategory category;
    bool analytic;              // générée par PolyBLEP dans SynthVoices (pas de table de lecture)
};

const WaveInfo& waveInfo (int id) noexcept;               // id borné
int waveIdFromKey (std::string_view key) noexcept;        // -1 si inconnue
bool isAnalyticWave (int id) noexcept;

class WaveTables
{
public:
    static constexpr int kSize = 2048;   // échantillons par table
    static constexpr int kMips = 10;     // table m : harmoniques 1..(512 >> m)
    static constexpr int kMaxHarmonics = 512;

    // Construit (une seule fois, thread-safe) puis rend l'instance. À appeler HORS temps réel avant le premier rendu.
    static const WaveTables& instance();

    const float* table (int wave, int mip) const noexcept { return data_.data() + (static_cast<std::size_t> (wave) * kMips + static_cast<std::size_t> (mip)) * (kSize + 1); }
    // Table à utiliser pour un incrément de phase `dt` (cycles par échantillon) : la plus riche sans repliement.
    static int mipFor (double dt) noexcept;

private:
    WaveTables();
    std::vector<float> data_; // (kSize + 1) échantillons par table : le dernier recopie le premier (interpolation sans test)
};

// Un cycle de la forme, à bande limitée (64 harmoniques), pour l'affichage : `out` reçoit n échantillons dans [-1, 1].
void renderWaveCycle (int wave, float* out, int n, double pulseWidth = 0.5);
} // namespace neptune::core
