#pragma once
// Banques SoundFont (`.sf2`, `.sf3`) : lecture du fichier, liste des presets, conversion d'un preset en instrument du Sampler.
//
// Le fichier contient tous les échantillons d'un coup. Plutôt que de les extraire en fichiers, une zone désigne « l'échantillon n de cette banque » par un chemin
// virtuel : `<banque.sf2>#12` (mono) ou `<banque.sf2>#12+13` (paire gauche/droite fusionnée en un échantillon stéréo). Le chargeur d'échantillons (SampleLoader)
// reconnaît ces chemins et décode l'échantillon à la demande ; la session ne retient que le chemin de la banque et le numéro de preset, pas les sons.
//   - SF2 : PCM 16 bits lu tel quel.
//   - SF3 : échantillons compressés en Ogg Vorbis (chacun dans son flux), décodés en mémoire (stb_vorbis).
// Convertis : étendue de notes et de vélocités, note racine, accordage (grossier, fin, correction de l'échantillon), volume (atténuation initiale), panoramique,
// boucles (modes 1 et 3), décalages de début/fin/boucle, enveloppe de volume (délai, attaque, maintien, déclin, sustain, relâchement), suivi de la touche du maintien et
// du déclin, filtre passe-bas avec son enveloppe de modulation, note et vélocité imposées, loi de vélocité par défaut (volume = vélocité au carré),
// classe d'exclusion (coupure mutuelle). Les réglages du preset s'ajoutent à ceux de l'instrument, comme dans la spécification 2.04.
// Non convertis, signalés dans `warnings` : LFO, enveloppe de modulation vers la hauteur (LFO vers le filtre ou le volume compris), envois de réverbération/chorus,
// échelle d'accord, modulateurs (la loi de vélocité par défaut est appliquée même si la banque en redéfinit un). Cœur portable, sans JUCE.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "neptune/core/Instrument.h"

namespace neptune::core::soundfont
{
struct PresetInfo
{
    std::string name;
    int bank = 0;
    int program = 0;
    int zones = 0;       // zones du preset (une zone peut en donner plusieurs une fois l'instrument développé)
};

struct BankInfo
{
    bool ok = false;
    std::string error;   // en anglais
    std::string name;    // nom du fichier (champ INAM, sinon nom du fichier)
    int versionMajor = 2;
    bool compressed = false;   // SF3
    int samples = 0;
    std::vector<PresetInfo> presets;   // dans l'ordre du fichier : l'indice d'un preset est sa position ici
};

// Lit l'index de la banque (sans toucher aux échantillons). Garde le résultat en cache tant que le fichier ne change pas.
BankInfo readBank (const std::filesystem::path& file);

struct Result
{
    bool ok = false;
    std::string error;
    std::string name;                  // nom du preset
    InstrumentDescription instrument;
    int zones = 0;
    std::vector<std::string> warnings;
};

Result importPreset (const std::filesystem::path& file, int presetIndex);

// Chemins virtuels d'échantillons.
struct SampleRef
{
    std::filesystem::path bank;
    int left = -1;
    int right = -1;       // -1 : mono
};
std::string samplePath (const std::filesystem::path& bank, int left, int right = -1);
bool parseSamplePath (const std::string& utf8Path, SampleRef& out);   // faux : ce n'est pas un chemin de banque (le fichier n'est pas vérifié)

struct SampleProbe
{
    bool ok = false;
    std::string error;
    int channels = 0;
    std::int64_t frames = 0;
    double sampleRate = 0.0;
};
SampleProbe probeSample (const SampleRef&);

struct DecodedSample
{
    bool ok = false;
    std::string error;
    int channels = 0;
    std::int64_t frames = 0;
    double sampleRate = 0.0;
    std::vector<std::int16_t> interleaved;   // frames * channels
};
DecodedSample decodeSample (const SampleRef&);
} // namespace neptune::core::soundfont
