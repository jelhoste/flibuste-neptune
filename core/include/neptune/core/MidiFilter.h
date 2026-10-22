#pragma once
// Filtre MIDI avancé d'une piste (spec-modules §1) : un arbre de règles combinées par la logique booléenne (ET, OU, OU exclusif,
// NON ET, NON OU, NON). Les feuilles testent un message MIDI : type, canal, plage de la première donnée (note, numéro de CC,
// programme) et de la seconde (vélocité, valeur de CC, pression, pitch bend 0..127). Un message n'est transmis à la piste que si
// l'arbre est vrai (en plus du canal de la piste). Aucune dépendance à JUCE ; l'évaluation ne fait aucune allocation.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::midi
{
enum class MsgKind { Note = 0, NoteOn, NoteOff, ControlChange, PitchBend, ChannelPressure, PolyPressure, ProgramChange };
constexpr int numMsgKinds = 8;
const char* toString (MsgKind) noexcept; // "note", "note_on", ...

// Un message MIDI de canal, tel que le moteur le voit.
struct Message
{
    MsgKind kind = MsgKind::NoteOn; // jamais MsgKind::Note (c'est un type de condition)
    int channel = 0;          // 0..15
    int data1 = 0;            // note, numéro de CC, programme ; note pour la pression polyphonique
    int data2 = 0;            // vélocité, valeur de CC, pression, pitch bend ramené à 0..127
};

// Dernier état connu des contrôleurs de chaque canal : de quoi juger une condition « au moment où une note se déclenche » (pédale de sustain enfoncée, pitch bend,
// pression...). Mis à jour par le thread audio, lu par lui : pas de verrou.
struct State
{
    std::array<std::array<std::uint8_t, 128>, 16> cc {}, poly {};
    std::array<std::uint8_t, 16> bend {}, pressure {}, program {};
    State() noexcept { bend.fill (64); }
    void update (const Message&) noexcept;
};

struct Condition
{
    MsgKind kind = MsgKind::Note;   // Note : note on ET note off
    int channel = 0;          // 0 : tous, 1..16
    int lo1 = 0, hi1 = 127;   // note / numéro de CC / programme
    int lo2 = 0, hi2 = 127;   // vélocité / valeur / pression / bend
    bool operator== (const Condition& o) const noexcept
    {
        return kind == o.kind && channel == o.channel && lo1 == o.lo1 && hi1 == o.hi1 && lo2 == o.lo2 && hi2 == o.hi2;
    }
    bool matches (const Message&) const noexcept;
    // Comme matches(), mais pour un contrôleur on regarde sa DERNIÈRE valeur connue : une note qui se déclenche (`trigger`) est jugée sur l'état courant.
    // Note / Note On : la note qui se déclenche elle-même. Note Off : jamais vrai au déclenchement.
    bool matchesState (const State&, const Message& trigger) const noexcept;
};

enum class Op { Leaf = 0, And, Or, Xor, Nand, Nor, Not };
constexpr int numOps = 7;
const char* toString (Op) noexcept;

struct Rule
{
    Op op = Op::Leaf;
    Condition condition;      // feuille
    std::vector<Rule> children; // groupes (Not : un seul enfant)
    bool operator== (const Rule& o) const noexcept { return op == o.op && condition == o.condition && children == o.children; }
};

constexpr int maxNodes = 32, maxDepth = 6, maxChildren = 8;

struct FilterDescription
{
    bool enabled = false;
    Rule root;                // groupe ET par défaut
    FilterDescription() { root.op = Op::And; }
    bool operator== (const FilterDescription& o) const noexcept { return enabled == o.enabled && root == o.root; }
    bool operator!= (const FilterDescription& o) const noexcept { return ! (*this == o); }
};

FilterDescription sanitised (FilterDescription);     // bornes : 32 noeuds, profondeur 6, 8 enfants, plages ordonnées
std::string toJson (const FilterDescription&);
FilterDescription filterFromJson (std::string_view);  // lecture tolérante
int countNodes (const Rule&) noexcept;
// Texte lisible d'une règle (aperçu dans l'interface) : « (note 36-60 ET vél 80-127) OU CC 64 »
std::string describe (const Rule&);

// Forme compilée, plate et de taille fixe : évaluée sur le thread audio sans allocation.
class CompiledFilter
{
public:
    CompiledFilter() = default;
    explicit CompiledFilter (const FilterDescription&);
    bool enabled() const noexcept { return enabled_ && count_ > 0; }
    // Vrai si le message est transmis à la piste. Filtre éteint : toujours vrai.
    bool passes (const Message&) const noexcept;
    // Même arbre, jugé sur l'état des contrôleurs au moment où la note `trigger` se déclenche (condition MIDI d'une zone). Éteint : toujours vrai.
    bool passesState (const State&, const Message& trigger) const noexcept;

private:
    struct Node
    {
        Op op = Op::Leaf;
        Condition cond;
        int childCount = 0;
        std::array<int, maxChildren> child {};
    };
    bool eval (int index, const Message&, int depth) const noexcept;
    bool evalState (int index, const State&, const Message& trigger, int depth) const noexcept;

    bool enabled_ = false;
    int count_ = 0;
    std::array<Node, maxNodes> nodes_ {};
};
} // namespace neptune::core::midi
