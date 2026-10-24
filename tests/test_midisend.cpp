#include <catch2/catch_test_macros.hpp>

#include "neptune/core/MidiSend.h"

using namespace neptune::core::midisend;

TEST_CASE ("midi send: channel messages are encoded to MIDI bytes")
{
    Message m;
    m.kind = Kind::ControlChange; m.channel = 3; m.data1 = 7; m.data2 = 100;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xB2, 7, 100 }));
    m.kind = Kind::NoteOn; m.channel = 1; m.data1 = 60; m.data2 = 127;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0x90, 60, 127 }));
    m.kind = Kind::NoteOff; m.channel = 16;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0x8F, 60, 127 }));
    m.kind = Kind::ProgramChange; m.data1 = 5;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xCF, 5 }));
    m.kind = Kind::ChannelPressure; m.data1 = 64;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xDF, 64 }));
    m.kind = Kind::PolyPressure; m.channel = 2; m.data1 = 40; m.data2 = 90;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xA1, 40, 90 }));
    m.kind = Kind::PitchBend; m.channel = 1; m.data1 = 8192;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xE0, 0, 64 }));   // 8192 = 0x2000 -> LSB 0, MSB 0x40
    m.data1 = 16383;
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xE0, 127, 127 }));
}

TEST_CASE ("midi send: values are bounded")
{
    Message m;
    m.kind = Kind::ControlChange; m.channel = 99; m.data1 = -5; m.data2 = 500;
    const auto bytes = encode (m);
    CHECK (bytes == std::vector<std::uint8_t> ({ 0xBF, 0, 127 }));
    Message bad;
    bad.kind = static_cast<Kind> (42);
    CHECK (sanitised (bad).kind == Kind::ControlChange);
}

TEST_CASE ("midi send: hexadecimal text is read tolerantly and SysEx is completed")
{
    std::vector<std::uint8_t> out;
    REQUIRE (parseHex ("F0 7E,7f\n09 0x01 f7", out));
    CHECK (out == std::vector<std::uint8_t> ({ 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }));
    CHECK (toHex (out) == "F0 7E 7F 09 01 F7");
    std::string error;
    CHECK (! parseHex ("F0 7G", out, &error));
    CHECK (! error.empty());
    CHECK (! parseHex ("F0 123", out));

    Message m;
    m.kind = Kind::SysEx;
    m.sysex = "7E 7F 09 01";   // F0 et F7 manquent
    CHECK (encode (m) == std::vector<std::uint8_t> ({ 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }));
    m.sysex = "F0 7E 90 F7";    // octet de données > 7F
    std::string why;
    CHECK (encode (m, &why).empty());
    CHECK (! why.empty());
    m.sysex = "zz";
    CHECK (encode (m).empty());
    m.sysex = "";
    CHECK (encode (m).empty());
    m.sysex = std::string (4000, '1');
    CHECK (encode (m).empty());
}

TEST_CASE ("midi send: messages are described")
{
    Message m;
    m.kind = Kind::ControlChange; m.channel = 1; m.data1 = 7; m.data2 = 100;
    CHECK (describe (m) == "CC 7 = 100 (ch 1)");
    m.kind = Kind::SysEx; m.sysex = "7E 7F 09 01";
    CHECK (describe (m) == "SysEx 6 bytes");
}
