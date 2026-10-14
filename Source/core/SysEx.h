#pragma once
#include "Program.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace xd::sysex
{
// Korg Minilogue XD exclusive header: F0 42 3g 00 01 51 <func>
enum Func : uint8_t
{
    GlobalRequest = 0x0E,
    CurrentProgramRequest = 0x10,
    UserSlotStatusRequest = 0x19,
    ProgramRequest = 0x1C,
    CurrentProgramDump = 0x40,
    UserSlotStatusReply = 0x49,
    ProgramDump = 0x4C,
    GlobalDump = 0x51,
    DataLoadCompleted = 0x23,
    DataLoadError = 0x24,
    DataFormatError = 0x26
};

constexpr int programDumpSize = 1181;        // 0x4C message incl. F0/F7
constexpr int currentProgramDumpSize = 1179; // 0x40 message incl. F0/F7

std::vector<uint8_t> header (int channel, uint8_t func);
juce::MidiMessage deviceInquiry();
juce::MidiMessage request (int channel, uint8_t func);
juce::MidiMessage programRequest (int channel, int slot);
juce::MidiMessage userSlotStatusRequest (int channel, int module, int slot);
std::vector<uint8_t> currentProgramDump (int channel, const Program&);
std::vector<uint8_t> programDump (int channel, int slot, const Program&);
std::vector<uint8_t> globalDump (int channel, const std::vector<uint8_t>& unpacked);
juce::MidiMessage toMessage (const std::vector<uint8_t>& bytes);

/** Parsed exclusive message from the synth. raw = bytes between F0 and F7 (exclusive). */
struct Parsed
{
    bool valid = false;
    int channel = 0;
    uint8_t func = 0;
    int slot = -1;                // for 0x4C
    std::vector<uint8_t> payload; // unpacked data
};
Parsed parse (const uint8_t* sysexData, int size); // JUCE style: without F0/F7

struct Identity { bool valid = false; int channel = 0; int major = 0, minor = 0; };
Identity parseIdentity (const uint8_t* sysexData, int size);

/** Splits a byte stream (a .syx file) into individual F0..F7 messages (content between, exclusive). */
std::vector<std::vector<uint8_t>> splitMessages (const juce::MemoryBlock& data);

juce::String userSlotName (const Parsed& p);
} // namespace xd::sysex
