#pragma once
// Filtres par voie du Sampler (palier 1, jalon 8) : deux filtres par voix (série ou parallèle), chacun avec son enveloppe
// AHDSHR, son suivi du clavier et son effet de la vélocité. Réglage par zone avec héritage du réglage d'instrument.
//
// Modèles :
//  - SVF : filtre à variables d'état « zero delay feedback » (TPT, Zavalishin) : stable même quand la coupure bouge vite ;
//    passe-bas / passe-haut / passe-bande / notch, 12 dB (un étage) ou 24 dB (deux étages en cascade, le second sans résonance).
//  - Ladder : passe-bas 4 pôles « échelle » à rétroaction résolue exactement (sans retard d'une échantillon), saturation en
//    entrée réglable (drive) ; 12 dB (sortie du 2e pôle) ou 24 dB (4e pôle). Le mode est toujours passe-bas.
// Tout est borné : aucune valeur du réglage ne peut produire de NaN ni d'instabilité.
#include <array>
#include <cmath>
#include <cstdint>

#include "neptune/core/Envelope.h"

namespace neptune::core
{
enum class FilterModel { SVF, Ladder };
enum class FilterMode { LowPass, HighPass, BandPass, Notch };

struct FilterSettings
{
    bool on = false;
    FilterModel model = FilterModel::SVF;
    FilterMode mode = FilterMode::LowPass; // Ladder : toujours passe-bas
    int slopeDb = 12;                      // 12 ou 24
    double cutoffHz = 20000.0;             // 20 .. 20000 ; pour les filtres hérités de l'instrument, c'est le paramètre automatisable
    double resonance = 0.0;                // 0 .. 1
    double keyTrack = 0.0;                 // -1 .. 2 : octaves de coupure par octave de clavier, par rapport à la note 60
    double velocityOctaves = 0.0;          // -6 .. 6 : de -v (vélocité 0) à +v (vélocité max), 0 à mi-vélocité
    double envDepthOctaves = 0.0;          // -8 .. 8 : décalage de coupure à pleine enveloppe
    double drive = 0.0;                    // 0 .. 1 : saturation du modèle Ladder
    EnvelopeDescription env;               // enveloppe de filtre (même moteur AHDSHR que le volume)

    bool operator== (const FilterSettings& o) const noexcept
    {
        return on == o.on && model == o.model && mode == o.mode && slopeDb == o.slopeDb && cutoffHz == o.cutoffHz
            && resonance == o.resonance && keyTrack == o.keyTrack && velocityOctaves == o.velocityOctaves
            && envDepthOctaves == o.envDepthOctaves && drive == o.drive && env == o.env;
    }
};

struct FilterBlock
{
    std::array<FilterSettings, 2> filter;
    bool parallel = false;   // faux : filtre 1 puis filtre 2 ; vrai : les deux en parallèle, mélangés par `balance`
    double balance = 0.5;    // 0 : filtre 1 seul ; 1 : filtre 2 seul (mode parallèle)

    bool anyOn() const noexcept { return filter[0].on || filter[1].on; }
    bool operator== (const FilterBlock& o) const noexcept
    {
        return filter[0] == o.filter[0] && filter[1] == o.filter[1] && parallel == o.parallel && balance == o.balance;
    }
};

constexpr double filterMinHz = 20.0, filterMaxHz = 20000.0;

FilterSettings sanitised (FilterSettings f);
FilterBlock sanitised (FilterBlock b);

// Coupure effective (Hz) d'un filtre pour une note, une vélocité (0..1) et la valeur courante de son enveloppe (0..1) ;
// bornée à [20 Hz, min(20 kHz, 0,45 x fréquence d'échantillonnage)].
double modulatedCutoffHz (const FilterSettings&, double baseCutoffHz, int key, float velocity, float envValue, double sampleRate) noexcept;

// ---- Traitement (temps réel : aucune allocation) ----

struct FilterCoefs
{
    FilterModel model = FilterModel::SVF;
    FilterMode mode = FilterMode::LowPass;
    int stages = 1;          // SVF : 1 (12 dB) ou 2 (24 dB) ; Ladder : tap du 2e (1) ou du 4e (2) pôle
    // SVF (TPT)
    float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f, a1b = 0, a2b = 0, a3b = 0, kb = 1.4142f;
    float m0 = 0, m1 = 0, m2 = 1, m0b = 0, m1b = 0, m2b = 1; // sortie = m0.x + m1.v1 + m2.v2 (selon le mode, sans branche)
    // Ladder
    float G = 0, kFeedback = 0, drive = 0, g4 = 0, invDen = 1.0f; // invDen = 1 / (1 + k.G^4)
};

// Calcule les coefficients pour une coupure (Hz) et une résonance (0..1).
FilterCoefs makeFilterCoefs (const FilterSettings&, double cutoffHz, double resonance, double sampleRate) noexcept;

// État d'un filtre pour UN canal.
struct FilterState
{
    float z[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // SVF : ic1eq/ic2eq par étage ; Ladder : un état par pôle
    void reset() noexcept { z[0] = z[1] = z[2] = z[3] = 0.0f; }
};

// Un échantillon.
inline float processFilter (const FilterCoefs& c, FilterState& s, float x) noexcept
{
    if (c.model == FilterModel::SVF)
    {
        // étage 1
        float v3 = x - s.z[1];
        float v1 = c.a1 * s.z[0] + c.a2 * v3;
        float v2 = s.z[1] + c.a2 * s.z[0] + c.a3 * v3;
        s.z[0] = 2.0f * v1 - s.z[0];
        s.z[1] = 2.0f * v2 - s.z[1];
        const float y = c.m0 * x + c.m1 * v1 + c.m2 * v2;
        if (c.stages < 2) return y;
        // étage 2 (sans résonance : Butterworth)
        v3 = y - s.z[3];
        v1 = c.a1b * s.z[2] + c.a2b * v3;
        v2 = s.z[3] + c.a2b * s.z[2] + c.a3b * v3;
        s.z[2] = 2.0f * v1 - s.z[2];
        s.z[3] = 2.0f * v2 - s.z[3];
        return c.m0b * y + c.m1b * v1 + c.m2b * v2;
    }

    // Ladder : 4 pôles TPT, rétroaction résolue ; u = x - k.y4
    const float G = c.G;
    const float oneMinusG = 1.0f - G;
    const float S1 = oneMinusG * s.z[0], S2 = oneMinusG * s.z[1], S3 = oneMinusG * s.z[2], S4 = oneMinusG * s.z[3];
    const float sigma = G * G * G * S1 + G * G * S2 + G * S3 + S4;
    const float y4 = (c.g4 * x + sigma) * c.invDen;
    float u = x - c.kFeedback * y4;
    if (c.drive > 0.0f) // saturation douce de l'entrée des pôles : cubique bornée (sans division), normalisée à gain 1 près de 0
    {
        const float a = 1.0f + 3.0f * c.drive;
        float t = u * a * (1.0f / 1.5f);
        t = t > 1.0f ? 1.0f : (t < -1.0f ? -1.0f : t);
        u = 1.5f * (t - t * t * t * (1.0f / 3.0f)) * (1.0f / a);
    }
    const float y1 = G * u + S1;
    const float y2 = G * y1 + S2;
    const float y3 = G * y2 + S3;
    const float y4n = G * y3 + S4;
    s.z[0] = 2.0f * y1 - s.z[0]; // trapèze : z_nouveau = y + v = 2y - z
    s.z[1] = 2.0f * y2 - s.z[1];
    s.z[2] = 2.0f * y3 - s.z[2];
    s.z[3] = 2.0f * y4n - s.z[3];
    return c.stages < 2 ? y2 : y4n;
}
} // namespace neptune::core
