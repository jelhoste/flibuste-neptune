#pragma once
// Couche Paramètre générique (palier 0).
// Une seule définition de « paramètre » sert de base à l'automation hôte, au MIDI/OSC, à l'undo/redo et à la modulation.
// Aucune dépendance à JUCE ni à CLAP : la traduction se fait dans les adaptateurs.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neptune::core
{
using ParamId = std::uint32_t; // identifiant stable : ne change jamais une fois publié (automation de l'hôte)

enum class Unit { None, Decibel, Hertz, Milliseconds, Seconds, Percent, Cents, Semitones, Octaves, Degrees, Samples, Ratio };

enum class Kind
{
    Continuous, // valeur réelle dans [min, max]
    Stepped,    // entiers de min à max
    Toggle,     // 0 ou 1
    Choice      // index dans `choices`
};

// Relation entre la position normalisée n ∈ [0,1] (potard, MIDI, automation) et la valeur réelle.
enum class Curve
{
    Linear,      // v = min + (max-min)·n
    Exponential, // v = min·(max/min)^n   (fréquences ; exige min > 0)
    Power        // v = min + (max-min)·n^p   (zone de précision fine sur le début de course ; exige p > 0)
};

struct ParamSpec
{
    ParamId id = 0;
    std::string name;
    Unit unit = Unit::None;
    Kind kind = Kind::Continuous;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    Curve curve = Curve::Linear;
    double curveExponent = 1.0;       // utilisé par Curve::Power
    std::vector<std::string> choices; // utilisé par Kind::Choice
    std::string minText;              // affiché quand la valeur vaut min (ex. "-inf", "OFF")
    int decimals = -1;                // -1 : automatique selon l'unité
    bool automatable = true;
    bool modulatable = true;
    bool hidden = false;

    static ParamSpec continuous (ParamId id, std::string name, Unit unit, double min, double max, double def,
                                 Curve curve = Curve::Linear, double exponent = 1.0);
    static ParamSpec stepped (ParamId id, std::string name, Unit unit, int min, int max, int def);
    static ParamSpec toggle (ParamId id, std::string name, bool def = false);
    static ParamSpec choice (ParamId id, std::string name, std::vector<std::string> choices, int def = 0);

    // Chaîne vide si la définition est valide, sinon le motif du refus.
    std::string validate() const;

    double clamp (double plain) const noexcept;        // borne + arrondit selon `kind`
    double toNormalized (double plain) const noexcept; // valeur réelle -> [0,1]
    double fromNormalized (double n) const noexcept;   // [0,1] -> valeur réelle (déjà bornée/arrondie)
};

// Ensemble de paramètres d'une instance.
// - Déclaration (add) : hors temps réel, avant que l'audio ne tourne. Les adresses restent stables ensuite.
// - Lecture/écriture des valeurs : sans verrou, utilisable depuis le thread audio, l'UI ou l'hôte.
class ParameterSet
{
public:
    static constexpr std::size_t npos = static_cast<std::size_t> (-1);

    // Retourne false (et ne fait rien) si la définition est invalide ou si l'id existe déjà.
    bool add (ParamSpec spec, std::string* error = nullptr);

    std::size_t size() const noexcept { return slots_.size(); }
    std::size_t indexOf (ParamId id) const noexcept; // npos si inconnu
    const ParamSpec& spec (std::size_t index) const { return slots_[index].spec; }
    const ParamSpec* find (ParamId id) const noexcept;

    // Accès par index : chemin chaud (pas de recherche).
    double getAt (std::size_t index) const noexcept { return slots_[index].value.load (std::memory_order_relaxed); }
    double getNormalizedAt (std::size_t index) const noexcept;
    bool setAt (std::size_t index, double plain) noexcept;           // true si la valeur a changé
    bool setNormalizedAt (std::size_t index, double n) noexcept;
    std::uint32_t versionAt (std::size_t index) const noexcept { return slots_[index].version.load (std::memory_order_relaxed); }

    // Accès par id (id inconnu : get -> 0.0, set -> false).
    double get (ParamId id) const noexcept;
    double getNormalized (ParamId id) const noexcept;
    bool set (ParamId id, double plain) noexcept;
    bool setNormalized (ParamId id, double n) noexcept;
    bool reset (ParamId id) noexcept;
    void resetAll() noexcept;
    std::uint32_t version (ParamId id) const noexcept; // incrémenté à chaque changement (l'UI/feedback compare)

    // Texte affiché / saisi (double-clic -> champ texte). Allocation : ne pas appeler depuis le thread audio.
    std::string toText (ParamId id) const;
    std::string toText (const ParamSpec& spec, double plain) const;
    bool fromText (ParamId id, std::string_view text, double& plainOut) const;

private:
    struct Slot
    {
        explicit Slot (ParamSpec s) : spec (std::move (s)), value (spec.defaultValue) {}
        ParamSpec spec;
        std::atomic<double> value;
        std::atomic<std::uint32_t> version { 0 };
    };

    static_assert (std::atomic<double>::is_always_lock_free, "std::atomic<double> doit être sans verrou");

    std::deque<Slot> slots_; // deque : les éléments ne bougent jamais (atomiques non déplaçables)
    std::vector<std::pair<ParamId, std::size_t>> index_; // trié par id
};
} // namespace neptune::core
