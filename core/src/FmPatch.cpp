#include "neptune/core/FmPatch.h"

#include <algorithm>

namespace neptune::core::fm
{
namespace
{
// Maxima des champs d'un opérateur (DX7).
constexpr int kOpMax[kNumOpFields] = { 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14 };

// Maxima des octets globaux 126..154 (nom exclu).
int globalMax (int i) noexcept
{
    if (i >= 126 && i <= 133) return 99;       // enveloppe de pitch
    switch (i)
    {
        case 134: return 31;                   // algorithme
        case 135: return 7;                    // feedback
        case 136: return 1;                    // key sync
        case 137: case 138: case 139: case 140: return 99; // LFO
        case 141: return 1;                    // LFO sync
        case 142: return 5;                    // forme
        case 143: return 7;                    // PMS
        case 144: return 48;                   // transposition
        default: return -1;
    }
}
} // namespace

int operatorFieldMax (int field) noexcept { return field >= 0 && field < kNumOpFields ? kOpMax[field] : 0; }
int globalByteMax (int index) noexcept { return index >= 126 && index <= 154 ? globalMax (index) : -1; }

Patch Patch::initVoice()
{
    Patch p;
    for (int op = 0; op < kOperators; ++op)
    {
        for (int r = 0; r < 4; ++r) p.setOp (op, kRate1 + r, 99);
        for (int l = 0; l < 3; ++l) p.setOp (op, kLevel1 + l, 99);
        p.setOp (op, kLevel4, 0);
        p.setOp (op, kBreakPoint, 39);
        p.setOp (op, kOutputLevel, op == kOperators - 1 ? 99 : 0); // OP1 est le dernier opérateur du sysex
        p.setOp (op, kCoarse, 1);
        p.setOp (op, kDetune, 7);
    }
    for (int i = 0; i < 4; ++i)
    {
        p.setGlobal (kPitchEgOffset + i, 99);
        p.setGlobal (kPitchEgOffset + 4 + i, 50);
    }
    p.setGlobal (136, 1);
    p.setGlobal (137, 35);
    p.setGlobal (141, 1);
    p.setGlobal (143, 3);
    p.setGlobal (kTransposeOffset, 24);
    p.setName ("INIT VOICE");
    p.data[kMaskOffset] = 0x3f;
    return p;
}

void Patch::setOp (int opIndex, int field, int value) noexcept
{
    if (opIndex < 0 || opIndex >= kOperators || field < 0 || field >= kNumOpFields) return;
    data[static_cast<std::size_t> (opIndex * kOperatorStride + field)] = static_cast<std::uint8_t> (std::clamp (value, 0, kOpMax[field]));
}

void Patch::setGlobal (int index, int value) noexcept
{
    const int mx = globalByteMax (index);
    if (mx < 0) return;
    data[static_cast<std::size_t> (index)] = static_cast<std::uint8_t> (std::clamp (value, 0, mx));
}

void Patch::setOperatorOn (int opIndex, bool on) noexcept
{
    if (opIndex < 0 || opIndex >= kOperators) return;
    if (on) data[kMaskOffset] = static_cast<std::uint8_t> (data[kMaskOffset] | (1 << opIndex));
    else data[kMaskOffset] = static_cast<std::uint8_t> (data[kMaskOffset] & ~(1 << opIndex));
}

std::string Patch::name() const
{
    std::string s (reinterpret_cast<const char*> (&data[kNameOffset]), kNameLength);
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void Patch::setName (std::string_view n)
{
    for (int i = 0; i < kNameLength; ++i)
    {
        char c = static_cast<std::size_t> (i) < n.size() ? n[static_cast<std::size_t> (i)] : ' ';
        if (c < 32 || c > 126) c = ' ';
        data[static_cast<std::size_t> (kNameOffset + i)] = static_cast<std::uint8_t> (c);
    }
}

Patch& Patch::sanitise() noexcept
{
    for (int op = 0; op < kOperators; ++op)
        for (int f = 0; f < kNumOpFields; ++f)
        {
            auto& b = data[static_cast<std::size_t> (op * kOperatorStride + f)];
            b = static_cast<std::uint8_t> (std::min<int> (b, kOpMax[f]));
        }
    for (int i = 126; i <= 144; ++i)
    {
        const int mx = globalMax (i);
        if (mx >= 0) data[static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (std::min<int> (data[static_cast<std::size_t> (i)], mx));
    }
    for (int i = 0; i < kNameLength; ++i)
    {
        auto& c = data[static_cast<std::size_t> (kNameOffset + i)];
        if (c < 32 || c > 126) c = ' ';
    }
    data[kMaskOffset] = static_cast<std::uint8_t> (data[kMaskOffset] & 0x3f);
    return *this;
}

// ---------------------------------------------------------------------------------------------------- sysex

std::uint8_t checksum (const std::uint8_t* bytes, std::size_t size) noexcept
{
    int sum = 0;
    for (std::size_t i = 0; i < size; ++i) sum += bytes[i];
    return static_cast<std::uint8_t> ((-sum) & 0x7f);
}

std::array<std::uint8_t, 128> packVoice (const Patch& p)
{
    std::array<std::uint8_t, 128> b {};
    const auto& d = p.data;
    for (int op = 0; op < kOperators; ++op)
    {
        const auto* src = &d[static_cast<std::size_t> (op * kOperatorStride)];
        auto* dst = &b[static_cast<std::size_t> (op * 17)];
        for (int i = 0; i < 11; ++i) dst[i] = src[i];
        dst[11] = static_cast<std::uint8_t> ((src[12] << 2) | src[11]);   // courbe droite, courbe gauche
        dst[12] = static_cast<std::uint8_t> ((src[20] << 3) | src[13]);   // detune, rate scaling
        dst[13] = static_cast<std::uint8_t> ((src[15] << 2) | src[14]);   // vélocité, AMS
        dst[14] = src[16];                                                // niveau de sortie
        dst[15] = static_cast<std::uint8_t> ((src[18] << 1) | src[17]);   // coarse, mode
        dst[16] = src[19];                                                // fine
    }
    for (int i = 0; i < 9; ++i) b[static_cast<std::size_t> (102 + i)] = d[static_cast<std::size_t> (126 + i)];
    b[111] = static_cast<std::uint8_t> ((d[136] << 3) | d[135]);          // key sync, feedback
    for (int i = 0; i < 4; ++i) b[static_cast<std::size_t> (112 + i)] = d[static_cast<std::size_t> (137 + i)];
    b[116] = static_cast<std::uint8_t> ((d[143] << 4) | (d[142] << 1) | d[141]); // PMS, forme, sync
    for (int i = 0; i < 11; ++i) b[static_cast<std::size_t> (117 + i)] = d[static_cast<std::size_t> (144 + i)];
    for (auto& v : b) v &= 0x7f;
    return b;
}

Patch unpackVoice (const std::uint8_t b[128])
{
    Patch p;
    auto& d = p.data;
    for (int op = 0; op < kOperators; ++op)
    {
        const auto* src = &b[op * 17];
        auto* dst = &d[static_cast<std::size_t> (op * kOperatorStride)];
        for (int i = 0; i < 11; ++i) dst[i] = src[i];
        dst[11] = src[11] & 3;
        dst[12] = (src[11] >> 2) & 3;
        dst[13] = src[12] & 7;
        dst[20] = static_cast<std::uint8_t> (src[12] >> 3);
        dst[14] = src[13] & 3;
        dst[15] = static_cast<std::uint8_t> (src[13] >> 2);
        dst[16] = src[14];
        dst[17] = src[15] & 1;
        dst[18] = static_cast<std::uint8_t> (src[15] >> 1);
        dst[19] = src[16];
    }
    for (int i = 0; i < 9; ++i) d[static_cast<std::size_t> (126 + i)] = b[102 + i];
    d[135] = b[111] & 7;
    d[136] = static_cast<std::uint8_t> (b[111] >> 3);
    for (int i = 0; i < 4; ++i) d[static_cast<std::size_t> (137 + i)] = b[112 + i];
    d[141] = b[116] & 1;
    d[142] = (b[116] >> 1) & 7;
    d[143] = static_cast<std::uint8_t> (b[116] >> 4);
    for (int i = 0; i < 11; ++i) d[static_cast<std::size_t> (144 + i)] = b[117 + i];
    d[kMaskOffset] = 0x3f;
    p.sanitise();
    return p;
}

SysexResult parseSysex (const std::uint8_t* bytes, std::size_t size)
{
    SysexResult r;
    std::size_t pos = 0;
    while (pos < size)
    {
        if (bytes[pos] != 0xF0) { ++pos; continue; } // octets hors message : ignorés
        std::size_t end = pos + 1;
        while (end < size && bytes[end] != 0xF7) ++end;
        if (end >= size) { r.error = "unterminated sysex"; break; }
        const std::uint8_t* m = bytes + pos;
        const std::size_t len = end - pos + 1;
        if (len >= 6 && m[1] == 0x43 && (m[2] & 0xF0) == 0x00)
        {
            if (m[3] == 0x00 && m[4] == 0x01 && m[5] == 0x1B && len >= 6 + kVoiceDataBytes + 2)
            {
                Patch p;
                std::copy (m + 6, m + 6 + kVoiceDataBytes, p.data.begin());
                p.data[kMaskOffset] = 0x3f;
                p.sanitise();
                if (checksum (m + 6, kVoiceDataBytes) != m[6 + kVoiceDataBytes]) r.checksumOk = false;
                r.patches.push_back (p);
            }
            else if (m[3] == 0x09 && m[4] == 0x20 && m[5] == 0x00 && len >= 6 + 4096 + 2)
            {
                if (checksum (m + 6, 4096) != m[6 + 4096]) r.checksumOk = false;
                for (int v = 0; v < 32; ++v) r.patches.push_back (unpackVoice (m + 6 + v * 128));
            }
        }
        pos = end + 1;
    }
    if (r.patches.empty() && r.error.empty()) r.error = "no DX7 voice found";
    return r;
}

std::vector<std::uint8_t> makeSingleVoiceSysex (const Patch& p, int channel)
{
    std::vector<std::uint8_t> out = { 0xF0, 0x43, static_cast<std::uint8_t> (channel & 0x0F), 0x00, 0x01, 0x1B };
    Patch c = p;
    c.sanitise();
    for (int i = 0; i < kVoiceDataBytes; ++i) out.push_back (c.data[static_cast<std::size_t> (i)]);
    out.push_back (checksum (out.data() + 6, kVoiceDataBytes));
    out.push_back (0xF7);
    return out;
}

std::vector<std::uint8_t> makeBulkSysex (const std::vector<Patch>& voices, int channel)
{
    std::vector<std::uint8_t> out = { 0xF0, 0x43, static_cast<std::uint8_t> (channel & 0x0F), 0x09, 0x20, 0x00 };
    for (int v = 0; v < 32; ++v)
    {
        const auto packed = packVoice (v < static_cast<int> (voices.size()) ? voices[static_cast<std::size_t> (v)] : Patch::initVoice());
        out.insert (out.end(), packed.begin(), packed.end());
    }
    out.push_back (checksum (out.data() + 6, 4096));
    out.push_back (0xF7);
    return out;
}
} // namespace neptune::core::fm
