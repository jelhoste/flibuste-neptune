#pragma once
// Messages MIDI à envoyer à la main (widget « Message MIDI » de la Façade, fenêtre d'envoi libre) : modèle, encodage en octets MIDI,
// lecture/écriture d'un SysEx en hexadécimal. Cœur portable, sans JUCE ; l'envoi lui-même est fait par l'adaptateur.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::midisend
{
constexpr std::size_t maxSysexBytes = 1024;

enum class Kind { NoteOn = 0, NoteOff = 1, ControlChange = 2, ProgramChange = 3, PitchBend = 4, ChannelPressure = 5, PolyPressure = 6, SysEx = 7 };
constexpr int numKinds = 8;

// Valeurs selon le type :
//  NoteOn / NoteOff : data1 = note 0..127, data2 = vélocité 0..127          ControlChange : data1 = contrôleur, data2 = valeur
//  ProgramChange : data1 = programme 0..127                                 ChannelPressure : data1 = pression 0..127
//  PitchBend : data1 = valeur sur 14 bits 0..16383 (8192 = repos)           PolyPressure : data1 = note, data2 = pression
//  SysEx : `sysex` = octets en hexadécimal (« F0 7E 7F 09 01 F7 »), le reste est ignoré
struct Message
{
    Kind kind = Kind::ControlChange;
    int channel = 1;   // 1..16
    int data1 = 0;
    int data2 = 0;
    std::string sysex;
    bool operator== (const Message& o) const noexcept { return kind == o.kind && channel == o.channel && data1 == o.data1 && data2 == o.data2 && sysex == o.sysex; }
};

Message sanitised (Message);   // bornes de chaque valeur (le SysEx est conservé tel quel, vérifié à l'encodage)

// Lit des octets en hexadécimal : séparés par des espaces, virgules ou retours à la ligne, avec ou sans « 0x ». Faux avec `error` sinon.
bool parseHex (std::string_view text, std::vector<std::uint8_t>& out, std::string* error = nullptr);
std::string toHex (const std::vector<std::uint8_t>&);   // « F0 7E 7F »

// Les octets MIDI du message (statut + données ; un SysEx est complété par F0 et F7 s'ils manquent). Vide si le message est invalide
// (SysEx illisible, octet de données > 0x7F, trop long).
std::vector<std::uint8_t> encode (const Message&, std::string* error = nullptr);

// Texte lisible : « CC 7 = 100 (canal 1) », « SysEx F0 7E ... F7 (6 octets) ».
std::string describe (const Message&);
} // namespace neptune::core::midisend
