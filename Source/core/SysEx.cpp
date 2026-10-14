#include "SysEx.h"
#include "Codec.h"

namespace xd::sysex
{
std::vector<uint8_t> header (int channel, uint8_t func)
{
    return { 0xF0, 0x42, (uint8_t) (0x30 | (channel & 0x0F)), 0x00, 0x01, 0x51, func };
}

juce::MidiMessage toMessage (const std::vector<uint8_t>& b)
{
    return juce::MidiMessage (b.data(), (int) b.size());
}

juce::MidiMessage deviceInquiry()
{
    const uint8_t d[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    return juce::MidiMessage (d, (int) sizeof (d));
}

juce::MidiMessage request (int channel, uint8_t func)
{
    auto h = header (channel, func);
    h.push_back (0xF7);
    return toMessage (h);
}

juce::MidiMessage programRequest (int channel, int slot)
{
    auto h = header (channel, ProgramRequest);
    h.push_back ((uint8_t) (slot & 0x7F));
    h.push_back ((uint8_t) ((slot >> 7) & 0x7F));
    h.push_back (0xF7);
    return toMessage (h);
}

juce::MidiMessage userSlotStatusRequest (int channel, int module, int slot)
{
    auto h = header (channel, UserSlotStatusRequest);
    h.push_back ((uint8_t) module);
    h.push_back ((uint8_t) slot);
    h.push_back (0xF7);
    return toMessage (h);
}

std::vector<uint8_t> currentProgramDump (int channel, const Program& p)
{
    auto m = header (channel, CurrentProgramDump);
    auto packed = pack7 (p.data(), programSize);
    m.insert (m.end(), packed.begin(), packed.end());
    m.push_back (0xF7);
    return m;
}

std::vector<uint8_t> programDump (int channel, int slot, const Program& p)
{
    auto m = header (channel, ProgramDump);
    m.push_back ((uint8_t) (slot & 0x7F));
    m.push_back ((uint8_t) ((slot >> 7) & 0x7F));
    auto packed = pack7 (p.data(), programSize);
    m.insert (m.end(), packed.begin(), packed.end());
    m.push_back (0xF7);
    return m;
}

std::vector<uint8_t> globalDump (int channel, const std::vector<uint8_t>& unpacked)
{
    auto m = header (channel, GlobalDump);
    auto packed = pack7 (unpacked.data(), unpacked.size());
    m.insert (m.end(), packed.begin(), packed.end());
    m.push_back (0xF7);
    return m;
}

Parsed parse (const uint8_t* d, int size)
{
    Parsed p;
    // strip F0/F7 if present
    if (size > 0 && d[0] == 0xF0) { ++d; --size; }
    if (size > 0 && d[size - 1] == 0xF7) --size;
    if (size < 6 || d[0] != 0x42 || (d[1] & 0xF0) != 0x30 || d[2] != 0x00 || d[3] != 0x01 || d[4] != 0x51)
        return p;
    p.channel = d[1] & 0x0F;
    p.func = d[5];
    const uint8_t* body = d + 6;
    int n = size - 6;
    if (p.func == ProgramDump)
    {
        if (n < 2) return p;
        p.slot = body[0] | (body[1] << 7);
        body += 2; n -= 2;
    }
    else if (p.func == UserSlotStatusReply)
    {
        p.payload.assign (body, body + n); // packed, decoded in userSlotName
        p.valid = true;
        return p;
    }
    if (n > 0) p.payload = unpack7 (body, (size_t) n);
    p.valid = true;
    return p;
}

Identity parseIdentity (const uint8_t* d, int size)
{
    Identity id;
    if (size > 0 && d[0] == 0xF0) { ++d; --size; }
    if (size >= 13 && d[0] == 0x7E && d[2] == 0x06 && d[3] == 0x02 && d[4] == 0x42 && d[5] == 0x51)
    {
        id.valid = true;
        id.channel = d[1] & 0x0F;
        id.minor = d[9] | (d[10] << 7);
        id.major = d[11] | (d[12] << 7);
    }
    return id;
}

std::vector<std::vector<uint8_t>> splitMessages (const juce::MemoryBlock& data)
{
    std::vector<std::vector<uint8_t>> out;
    const auto* b = (const uint8_t*) data.getData();
    const size_t n = data.getSize();
    size_t i = 0;
    while (i < n)
    {
        if (b[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < n && b[j] != 0xF7 && b[j] != 0xF0) ++j;
        if (j < n && b[j] == 0xF7)
            out.emplace_back (b + i + 1, b + j);
        i = j;
    }
    return out;
}

juce::String userSlotName (const Parsed& p)
{
    // reply body = packed 32-byte USER SLOT STATUS (TABLE 6): name in bytes 18..31
    if (p.payload.empty()) return {};
    auto un = unpack7 (p.payload.data(), p.payload.size());
    if (un.size() < 19 || un[1] < 1 || un[1] > 5) return {};
    juce::String s;
    for (size_t i = 18; i < juce::jmin<size_t> (32, un.size()) && un[i] != 0; ++i)
    {
        if (un[i] < 32 || un[i] > 126) break;
        s << (char) un[i];
    }
    return s.trim();
}

} // namespace xd::sysex
