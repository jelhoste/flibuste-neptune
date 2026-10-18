#pragma once
// Suréchantillonnage 2x / 4x à phase linéaire (demi-bandes FIR en cascade), réutilisable par tous les modules stock (décision NOVA du
// 2026-10-18 : latence déclarée à l'hôte, courte). Un canal par instance. Aucune allocation hors prepare().
//
// Utilisation : `float* hi = os.up (in, n);` donne n x facteur échantillons à traiter en place, puis `os.down (out, n);` les ramène.
// Latence totale (aller-retour), en échantillons de la cadence de base : 0 (facteur 1), 31 (2x), 39 (4x).
#include <vector>

namespace neptune::core
{
// Un étage demi-bande : filtre passe-bas FIR de longueur impaire à phase linéaire, utilisé en interpolateur (x2) et en décimateur (/2).
class HalfbandStage
{
public:
    explicit HalfbandStage (int taps);   // impair
    void prepare (int maxInput);        // taille maximale du bloc d'entrée de up() / de sortie de down()
    void reset() noexcept;
    int taps() const noexcept { return taps_; }
    int groupDelay() const noexcept { return (taps_ - 1) / 2; }   // à la cadence haute
    void up (const float* in, int n, float* out) noexcept;        // n entrées -> 2n sorties
    void down (const float* in, int n, float* out) noexcept;      // 2n entrées -> n sorties
private:
    int taps_;
    std::vector<float> h_;
    std::vector<float> histUp_;     // dernières entrées (cadence basse), taps_ / 2 + 1
    std::vector<float> histDown_;   // dernières entrées (cadence haute), taps_ - 1
    std::vector<float> work_;
};

class Oversampler
{
public:
    static constexpr int kLatency2x = 31, kLatency4x = 39;
    static int latencyFor (int factor) noexcept { return factor >= 4 ? kLatency4x : factor >= 2 ? kLatency2x : 0; }
    // Retard, à la cadence haute, entre l'entrée de up() et l'instant où l'échantillon correspondant se trouve dans le tampon de up() :
    // c'est le décalage à appliquer à un gain calculé à la cadence de base avant de l'appliquer sur le tampon haute cadence.
    static int upDelayHigh (int factor) noexcept { return factor >= 4 ? 78 : factor >= 2 ? 31 : 0; }

    void prepare (int maxBlock);
    void reset() noexcept;
    void setFactor (int factor) noexcept;   // 1, 2 ou 4
    int factor() const noexcept { return factor_; }
    float* up (const float* in, int n) noexcept;     // retourne n x facteur échantillons (tampon interne)
    void down (float* out, int n) noexcept;          // ramène le tampon interne à la cadence de base
private:
    int factor_ = 1;
    HalfbandStage s1_ { 63 }, s2_ { 33 };
    std::vector<float> b1_, b2_, b4_;
    int lastN_ = 0;
};
} // namespace neptune::core
