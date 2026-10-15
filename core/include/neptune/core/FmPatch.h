#pragma once
// Patch FM de type DX7 (spec-modules §17) : modèle de données fidèle à la structure DX7, indépendant du moteur et de l'hôte.
//
// `data` est le patch « déplié » de 156 octets (le format des dumps sysex à une voix, plus un octet de masque d'opérateurs) :
//   opérateur k (k = 0 est l'OP6 du DX7, k = 5 l'OP1 ; comme dans le sysex) : 21 octets à partir de 21 * k
//     +0..3 vitesses R1..R4 | +4..7 niveaux L1..L4 | +8 point de rupture | +9 profondeur gauche | +10 profondeur droite
//     +11 courbe gauche | +12 courbe droite | +13 rate scaling | +14 sensibilité AMS | +15 sensibilité à la vélocité
//     +16 niveau de sortie | +17 mode (0 rapport, 1 fixe) | +18 coarse | +19 fine | +20 detune
//   126..129 enveloppe de pitch R1..R4 | 130..133 niveaux | 134 algorithme (0..31) | 135 feedback | 136 key sync oscillateurs
//   137 LFO vitesse | 138 délai | 139 PMD | 140 AMD | 141 sync | 142 forme | 143 PMS | 144 transposition (24 = do 3)
//   145..154 nom (10 caractères ASCII) | 155 masque des opérateurs actifs (bit k : opérateur k ; 0x3f = tous)
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::fm
{
constexpr int kOperators = 6;
constexpr int kPatchBytes = 156;
constexpr int kVoiceDataBytes = 155;      // sans le masque d'opérateurs
constexpr int kOperatorStride = 21;
constexpr int kNameOffset = 145, kNameLength = 10;
constexpr int kMaskOffset = 155;
constexpr int kAlgorithmOffset = 134, kFeedbackOffset = 135, kTransposeOffset = 144;
constexpr int kLfoOffset = 137;           // vitesse, délai, PMD, AMD, sync, forme, PMS = 137..143
constexpr int kPitchEgOffset = 126;       // R1..R4 puis L1..L4
constexpr int kNumAlgorithms = 32;

// Champs d'un opérateur (décalages dans les 21 octets).
enum OpField : int
{
    kRate1 = 0, kRate2, kRate3, kRate4, kLevel1, kLevel2, kLevel3, kLevel4, kBreakPoint, kLeftDepth, kRightDepth, kLeftCurve,
    kRightCurve, kRateScaling, kAmpModSens, kVelocitySens, kOutputLevel, kMode, kCoarse, kFine, kDetune, kNumOpFields
};

// Nombre maximal de chaque champ d'opérateur et de chaque octet global (le minimum est toujours 0).
int operatorFieldMax (int field) noexcept;
int globalByteMax (int index) noexcept; // index 126..154 ; -1 si l'octet n'est pas un réglage numérique (le nom)

struct Patch
{
    std::array<std::uint8_t, kPatchBytes> data {};

    // « INIT VOICE » du DX7 : un seul opérateur audible (OP1), sinus pur.
    static Patch initVoice();

    int op (int opIndex, int field) const noexcept { return data[static_cast<std::size_t> (opIndex * kOperatorStride + field)]; }
    void setOp (int opIndex, int field, int value) noexcept;
    int global (int index) const noexcept { return data[static_cast<std::size_t> (index)]; }
    void setGlobal (int index, int value) noexcept;

    int algorithm() const noexcept { return data[kAlgorithmOffset]; }       // 0..31
    int feedback() const noexcept { return data[kFeedbackOffset]; }         // 0..7
    bool operatorOn (int opIndex) const noexcept { return (data[kMaskOffset] >> opIndex) & 1; }
    void setOperatorOn (int opIndex, bool on) noexcept;

    std::string name() const;                                               // sans les espaces de fin
    void setName (std::string_view);                                        // ASCII imprimable, complété par des espaces

    // Ramène chaque octet dans ses bornes (un fichier sysex peut contenir n'importe quoi), le nom en ASCII imprimable.
    Patch& sanitise() noexcept;

    bool operator== (const Patch& o) const noexcept { return data == o.data; }
};

// ---- sysex DX7 (format technique de fait, spec §17) ----
// Une voix : F0 43 0n 00 01 1B <155 octets> <somme de contrôle> F7 (163 octets).
// Un lot de 32 voix : F0 43 0n 09 20 00 <4096 octets> <somme de contrôle> F7 (4104 octets).
struct SysexResult
{
    std::vector<Patch> patches; // 1 (voix) ou 32 (lot) ; vide si le message n'est pas reconnu
    bool checksumOk = true;
    std::string error;          // vide si tout va bien
};

// Lit un ou plusieurs messages à la suite dans `bytes` (un fichier .syx). Tolérant : une somme de contrôle fausse est signalée
// (`checksumOk`) mais le patch est quand même chargé.
SysexResult parseSysex (const std::uint8_t* bytes, std::size_t size);
std::vector<std::uint8_t> makeSingleVoiceSysex (const Patch&, int channel = 0);                     // 0..15
std::vector<std::uint8_t> makeBulkSysex (const std::vector<Patch>& voices32, int channel = 0);      // complété à 32 par des INIT VOICE

// Empaquetage d'une voix en 128 octets (format du lot de 32) et inverse.
std::array<std::uint8_t, 128> packVoice (const Patch&);
Patch unpackVoice (const std::uint8_t packed[128]);

// Somme de contrôle DX7 : complément à 2 de la somme des octets, sur 7 bits.
std::uint8_t checksum (const std::uint8_t* bytes, std::size_t size) noexcept;
} // namespace neptune::core::fm
