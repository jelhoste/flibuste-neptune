#include "neptune/core/MidiSend.h"

#include <algorithm>
#include <cctype>

namespace neptune::core::midisend
{
namespace
{
int clampInt (int v, int lo, int hi) { return std::clamp (v, lo, hi); }
const char* kindName (Kind k)
{
    switch (k)
    {
        case Kind::NoteOn: return "Note on";
        case Kind::NoteOff: return "Note off";
        case Kind::ControlChange: return "CC";
        case Kind::ProgramChange: return "Program";
        case Kind::PitchBend: return "Pitch bend";
        case Kind::ChannelPressure: return "Channel pressure";
        case Kind::PolyPressure: return "Poly pressure";
        case Kind::SysEx: return "SysEx";
    }
    return "?";
}
} // namespace

Message sanitised (Message m)
{
    const int kind = static_cast<int> (m.kind);
    if (kind < 0 || kind >= numKinds) m.kind = Kind::ControlChange;
    m.channel = clampInt (m.channel, 1, 16);
    if (m.kind == Kind::PitchBend) m.data1 = clampInt (m.data1, 0, 16383);
    else m.data1 = clampInt (m.data1, 0, 127);
    m.data2 = clampInt (m.data2, 0, 127);
    if (m.sysex.size() > maxSysexBytes * 4) m.sysex.resize (maxSysexBytes * 4);
    return m;
}

bool parseHex (std::string_view text, std::vector<std::uint8_t>& out, std::string* error)
{
    out.clear();
    std::string token;
    auto flush = [&] () -> bool
    {
        if (token.empty()) return true;
        std::string t = token;
        token.clear();
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr (2);
        if (t.empty() || t.size() > 2 || ! std::all_of (t.begin(), t.end(), [] (unsigned char c) { return std::isxdigit (c) != 0; }))
        {
            if (error != nullptr) *error = "Not a hexadecimal byte: " + t;
            return false;
        }
        out.push_back (static_cast<std::uint8_t> (std::stoi (t, nullptr, 16)));
        return true;
    };
    for (const char c : text)
    {
        if (c == ' ' || c == ',' || c == ';' || c == '\n' || c == '\r' || c == '\t') { if (! flush()) return false; }
        else token.push_back (c);
    }
    return flush();
}

std::string toHex (const std::vector<std::uint8_t>& bytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i != 0) out.push_back (' ');
        out.push_back (digits[bytes[i] >> 4]);
        out.push_back (digits[bytes[i] & 15]);
    }
    return out;
}

std::vector<std::uint8_t> encode (const Message& in, std::string* error)
{
    const Message m = sanitised (in);
    const auto ch = static_cast<std::uint8_t> (m.channel - 1);
    auto byte = [] (int v) { return static_cast<std::uint8_t> (v & 0x7F); };
    switch (m.kind)
    {
        case Kind::NoteOn:          return { static_cast<std::uint8_t> (0x90 | ch), byte (m.data1), byte (m.data2) };
        case Kind::NoteOff:         return { static_cast<std::uint8_t> (0x80 | ch), byte (m.data1), byte (m.data2) };
        case Kind::ControlChange:   return { static_cast<std::uint8_t> (0xB0 | ch), byte (m.data1), byte (m.data2) };
        case Kind::PolyPressure:    return { static_cast<std::uint8_t> (0xA0 | ch), byte (m.data1), byte (m.data2) };
        case Kind::ProgramChange:   return { static_cast<std::uint8_t> (0xC0 | ch), byte (m.data1) };
        case Kind::ChannelPressure: return { static_cast<std::uint8_t> (0xD0 | ch), byte (m.data1) };
        case Kind::PitchBend:       return { static_cast<std::uint8_t> (0xE0 | ch), byte (m.data1), byte (m.data1 >> 7) };
        case Kind::SysEx:
        {
            std::vector<std::uint8_t> bytes;
            if (! parseHex (m.sysex, bytes, error)) return {};
            if (bytes.empty()) { if (error != nullptr) *error = "The SysEx message is empty."; return {}; }
            if (bytes.front() != 0xF0) bytes.insert (bytes.begin(), 0xF0);
            if (bytes.back() != 0xF7) bytes.push_back (0xF7);
            if (bytes.size() > maxSysexBytes) { if (error != nullptr) *error = "The SysEx message is too long."; return {}; }
            for (std::size_t i = 1; i + 1 < bytes.size(); ++i)
                if (bytes[i] > 0x7F) { if (error != nullptr) *error = "A SysEx data byte is above 7F."; return {}; }
            return bytes;
        }
    }
    return {};
}

std::string describe (const Message& in)
{
    const Message m = sanitised (in);
    const std::string where = " (ch " + std::to_string (m.channel) + ")";
    switch (m.kind)
    {
        case Kind::NoteOn: case Kind::NoteOff: return std::string (kindName (m.kind)) + " " + std::to_string (m.data1) + " vel " + std::to_string (m.data2) + where;
        case Kind::ControlChange: return "CC " + std::to_string (m.data1) + " = " + std::to_string (m.data2) + where;
        case Kind::PolyPressure: return "Poly pressure " + std::to_string (m.data1) + " = " + std::to_string (m.data2) + where;
        case Kind::ProgramChange: case Kind::ChannelPressure: return std::string (kindName (m.kind)) + " " + std::to_string (m.data1) + where;
        case Kind::PitchBend: return "Pitch bend " + std::to_string (m.data1) + where;
        case Kind::SysEx:
        {
            const auto bytes = encode (m);
            if (bytes.empty()) return "SysEx (invalid)";
            return "SysEx " + std::to_string (bytes.size()) + " bytes";
        }
    }
    return {};
}
} // namespace neptune::core::midisend
