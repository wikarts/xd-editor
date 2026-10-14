#pragma once
#include "Program.h"
#include <set>

namespace xd
{
enum class BankId { Disk1 = 0, Disk2 = 1, Synth = 2 };
constexpr int numBanks = 3;
juce::String bankTitle (BankId);

struct Bank
{
    std::array<Program, numSlots> progs;
    std::array<AddInfo, numSlots> info;
    juce::File file;
    bool loaded = false;
    bool dirty = false;
    std::set<int> highlighted; // slots touched by the last copy (shown in yellow)

    void clear();
    void initAll();
};

/** Result of reading a file, with a human readable message for dialogs. */
struct IoResult
{
    bool ok = false;
    juce::String message;
    static IoResult success (juce::String m = {}) { return { true, std::move (m) }; }
    static IoResult fail (juce::String m) { return { false, std::move (m) }; }
};

namespace files
{
juce::File addInfoFileFor (const juce::File& f);

// single programs ---------------------------------------------------------------------------
IoResult loadProgram (const juce::File&, Program&, AddInfo&);
IoResult saveProgramSyx (const juce::File&, const Program&, const AddInfo&, int channel);
IoResult saveProgramXdProg (const juce::File&, const Program&, const AddInfo&);

// banks ----------------------------------------------------------------------------------------
/** Loads .syx (full dump / any set of program dumps) or .mnlgxdlib. For messy .syx dumps a
    *_CleanDump.syx is written next to it and becomes bank.file. */
IoResult loadBank (const juce::File&, Bank&, int channel);
IoResult saveBank (const juce::File&, Bank&, int channel); // by extension: .syx or .mnlgxdlib
IoResult exportBankList (const juce::File&, const Bank&, const juce::String& title);

void saveAddInfoBank (const juce::File& bankFile, const Bank&);
void loadAddInfoBank (const juce::File& bankFile, Bank&);
} // namespace files
} // namespace xd
